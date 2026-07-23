"""Run bounded Phase 2 API saturation and process-resource checks."""

from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
import http.client
import json
import math
import os
from pathlib import Path
import re
import secrets
import ssl
import threading
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urlsplit
from urllib.request import Request, urlopen


class PressureFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class RequestPlan:
    method: str
    path: str
    body: bytes | None
    headers: dict[str, str]
    accepted_statuses: frozenset[int]
    disconnect: bool = False


@dataclass(frozen=True)
class RequestResult:
    status: int
    elapsed_ms: float
    response_bytes: int


def configured_pid() -> int | None:
    raw = os.environ.get("PFH_PRESSURE_SERVER_PID", "0")
    try:
        value = int(raw)
    except ValueError as error:
        raise PressureFailure("PFH_PRESSURE_SERVER_PID must be an integer") from error
    return value or None


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def read_process_sample(pid: int) -> tuple[int, float]:
    status_path = Path(f"/proc/{pid}/status")
    stat_path = Path(f"/proc/{pid}/stat")
    if not status_path.is_file() or not stat_path.is_file():
        raise PressureFailure(f"process {pid} is not available through /proc")
    rss_kib = 0
    for line in status_path.read_text(encoding="utf-8").splitlines():
        if line.startswith("VmRSS:"):
            rss_kib = int(line.split()[1])
            break
    fields = stat_path.read_text(encoding="utf-8").split()
    ticks = int(fields[13]) + int(fields[14])
    cpu_seconds = ticks / os.sysconf("SC_CLK_TCK")
    return rss_kib, cpu_seconds


class ProcessSampler:
    def __init__(self, pid: int | None) -> None:
        self.pid = pid
        self.baseline_rss_kib: int | None = None
        self.peak_rss_kib: int | None = None
        self.final_rss_kib: int | None = None
        self.start_cpu_seconds: float | None = None
        self.final_cpu_seconds: float | None = None
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        if self.pid is None:
            return
        rss, cpu = read_process_sample(self.pid)
        self.baseline_rss_kib = rss
        self.peak_rss_kib = rss
        self.start_cpu_seconds = cpu

        def sample() -> None:
            while not self._stop.wait(0.05):
                try:
                    current_rss, _ = read_process_sample(self.pid or 0)
                except (OSError, PressureFailure, ValueError):
                    return
                self.peak_rss_kib = max(self.peak_rss_kib or 0, current_rss)

        self._thread = threading.Thread(target=sample, name="pfh-rss-sampler", daemon=True)
        self._thread.start()

    def stop(self, cooldown_seconds: float) -> None:
        if self.pid is None:
            return
        time.sleep(cooldown_seconds)
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        self.final_rss_kib, self.final_cpu_seconds = read_process_sample(self.pid)
        self.peak_rss_kib = max(self.peak_rss_kib or 0, self.final_rss_kib)

    def document(self) -> dict[str, float | int] | None:
        if self.pid is None or self.baseline_rss_kib is None:
            return None
        return {
            "baseline_rss_kib": self.baseline_rss_kib,
            "peak_rss_kib": self.peak_rss_kib or self.baseline_rss_kib,
            "final_rss_kib": self.final_rss_kib or self.baseline_rss_kib,
            "peak_growth_mib": round(
                ((self.peak_rss_kib or self.baseline_rss_kib) - self.baseline_rss_kib)
                / 1024,
                2,
            ),
            "final_growth_mib": round(
                ((self.final_rss_kib or self.baseline_rss_kib) - self.baseline_rss_kib)
                / 1024,
                2,
            ),
            "cpu_seconds": round(
                (self.final_cpu_seconds or 0.0) - (self.start_cpu_seconds or 0.0),
                3,
            ),
        }


def auth_headers(token: str | None) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"} if token else {}


def request_plan(arguments: argparse.Namespace) -> RequestPlan:
    token = os.environ.get("PFH_PRESSURE_ACCESS_TOKEN")
    if arguments.scenario in {"read", "csv", "queue", "disconnect"} and not token:
        raise PressureFailure(
            f"PFH_PRESSURE_ACCESS_TOKEN is required for {arguments.scenario} pressure"
        )

    if arguments.scenario in {"read", "queue"}:
        return RequestPlan(
            "GET",
            "/api/v1/reports/dashboard-summary",
            None,
            auth_headers(token),
            frozenset({200, 422, 429, 503}),
        )
    if arguments.scenario == "csv":
        now = datetime.now(timezone.utc)
        query = urlencode(
            {
                "from": (now - timedelta(days=365)).isoformat().replace("+00:00", "Z"),
                "to": (now + timedelta(days=1)).isoformat().replace("+00:00", "Z"),
            }
        )
        return RequestPlan(
            "GET",
            f"/api/v1/exports/transactions.csv?{query}",
            None,
            {**auth_headers(token), "Accept": "text/csv"},
            frozenset({200, 413, 422, 429, 503}),
        )
    if arguments.scenario == "auth":
        username = os.environ.get(
            "PFH_PRESSURE_AUTH_USERNAME",
            f"pressure-{secrets.token_hex(8)}@example.test",
        )
        body = json.dumps(
            {"username": username, "password": secrets.token_urlsafe(32)},
            separators=(",", ":"),
        ).encode("utf-8")
        return RequestPlan(
            "POST",
            "/api/v1/auth/login",
            body,
            {"Content-Type": "application/json"},
            frozenset({401, 429, 503}),
        )

    prefix = b'{"padding":"'
    suffix = b'"}'
    if arguments.body_bytes >= len(prefix) + len(suffix):
        body = prefix + b"x" * (arguments.body_bytes - len(prefix) - len(suffix)) + suffix
    else:
        body = b"x" * arguments.body_bytes
    return RequestPlan(
        "POST",
        "/api/v1/transactions",
        body,
        {**auth_headers(token), "Content-Type": "application/json"},
        frozenset({400, 413, 429, 503}),
        disconnect=True,
    )


def perform_request(base_url: str, plan: RequestPlan, timeout: float) -> RequestResult:
    started = time.perf_counter()
    request = Request(
        base_url + plan.path,
        data=plan.body,
        headers=plan.headers,
        method=plan.method,
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            body = response.read()
            return RequestResult(
                response.status,
                (time.perf_counter() - started) * 1000,
                len(body),
            )
    except HTTPError as error:
        body = error.read()
        return RequestResult(
            error.code,
            (time.perf_counter() - started) * 1000,
            len(body),
        )
    except (OSError, URLError, TimeoutError):
        return RequestResult(0, (time.perf_counter() - started) * 1000, 0)


def perform_disconnect(base_url: str, plan: RequestPlan, timeout: float) -> RequestResult:
    parsed = urlsplit(base_url)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname:
        raise PressureFailure("disconnect pressure requires an HTTP(S) base URL")
    started = time.perf_counter()
    port = parsed.port or (443 if parsed.scheme == "https" else 80)
    if parsed.scheme == "https":
        connection: http.client.HTTPConnection = http.client.HTTPSConnection(
            parsed.hostname,
            port,
            timeout=timeout,
            context=ssl.create_default_context(),
        )
    else:
        connection = http.client.HTTPConnection(parsed.hostname, port, timeout=timeout)
    connected = False
    try:
        connection.connect()
        connected = True
        connection.putrequest(plan.method, plan.path)
        for name, value in plan.headers.items():
            connection.putheader(name, value)
        connection.putheader("Content-Length", str(len(plan.body or b"")))
        connection.endheaders()
        if plan.body:
            connection.send(plan.body)
        return RequestResult(-1, (time.perf_counter() - started) * 1000, 0)
    except OSError:
        status = -1 if connected else 0
        return RequestResult(status, (time.perf_counter() - started) * 1000, 0)
    finally:
        connection.close()


METRIC_PATTERN = re.compile(
    r'^(pfh_(?:http_admission_rejections_total|report_resource_rejections_total)\{[^}]+\})\s+([0-9]+(?:\.[0-9]+)?)$',
    re.MULTILINE,
)


def parse_admission_metrics(text: str) -> dict[str, float]:
    return {name: float(value) for name, value in METRIC_PATTERN.findall(text)}


def admission_metrics(base_url: str, timeout: float) -> dict[str, float] | None:
    token = os.environ.get("PFH_PRESSURE_OPERATOR_TOKEN")
    if not token:
        return None
    request = Request(
        base_url + "/api/v1/operations/metrics",
        headers={"Authorization": f"Bearer {token}", "Accept": "text/plain"},
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            text = response.read().decode("utf-8", errors="replace")
    except (HTTPError, OSError, URLError, TimeoutError):
        return None
    return parse_admission_metrics(text)


def metric_delta(
    before: dict[str, float] | None,
    after: dict[str, float] | None,
) -> dict[str, float] | None:
    if before is None or after is None:
        return None
    return {
        name: round(value - before.get(name, 0.0), 3)
        for name, value in sorted(after.items())
    }


def saturation_failure(
    scenario: str,
    delta: dict[str, float] | None,
) -> str | None:
    required_reasons = {
        "auth": ("auth_rate_limit", "auth_queue"),
        "queue": ("request_queue",),
    }.get(scenario)
    if required_reasons is None:
        return None
    if delta is None:
        return f"operator admission metrics are required for {scenario} saturation"
    observed = sum(
        value
        for name, value in delta.items()
        if any(f'reason="{reason}"' in name for reason in required_reasons)
    )
    if observed <= 0:
        return f"{scenario} pressure did not reach the required admission boundary"
    return None


def wait_for_recovery(base_url: str, timeout: float) -> float | None:
    started = time.perf_counter()
    deadline = started + timeout
    while time.perf_counter() < deadline:
        result = perform_request(
            base_url,
            RequestPlan("GET", "/api/v1/currencies", None, {}, frozenset({200})),
            min(2.0, timeout),
        )
        if result.status == 200:
            return (time.perf_counter() - started) * 1000
        time.sleep(0.1)
    return None


def run(arguments: argparse.Namespace) -> None:
    base_url = arguments.base_url.rstrip("/")
    plan = request_plan(arguments)
    process = ProcessSampler(arguments.pid)
    metrics_before = admission_metrics(base_url, arguments.timeout)
    process.start()
    started = time.perf_counter()
    results: list[RequestResult] = []
    operation = perform_disconnect if plan.disconnect else perform_request
    with ThreadPoolExecutor(max_workers=arguments.concurrency) as executor:
        futures = [
            executor.submit(operation, base_url, plan, arguments.timeout)
            for _ in range(arguments.requests)
        ]
        for future in as_completed(futures):
            results.append(future.result())
    wall_ms = (time.perf_counter() - started) * 1000
    recovery_ms = wait_for_recovery(base_url, arguments.recovery_timeout)
    process.stop(arguments.cooldown)
    metrics_after = admission_metrics(base_url, arguments.timeout)

    statuses = Counter(result.status for result in results)
    latencies = [result.elapsed_ms for result in results]
    unexpected = {
        status: count
        for status, count in statuses.items()
        if status not in plan.accepted_statuses and not (plan.disconnect and status == -1)
    }
    failures: list[str] = []
    if unexpected:
        failures.append(f"unexpected response statuses: {unexpected}")
    if statuses.get(0, 0) > math.floor(arguments.requests * 0.05):
        failures.append("transport failure rate exceeds 5 percent")
    if recovery_ms is None:
        failures.append("service did not recover before the deadline")

    process_document = process.document()
    if process_document:
        if float(process_document["peak_growth_mib"]) > arguments.max_peak_rss_growth_mib:
            failures.append("peak RSS growth exceeds budget")
        if float(process_document["final_growth_mib"]) > arguments.max_final_rss_growth_mib:
            failures.append("final RSS growth exceeds recovery budget")

    deltas = metric_delta(metrics_before, metrics_after)
    if failure := saturation_failure(arguments.scenario, deltas):
        failures.append(failure)

    document = {
        "scenario": arguments.scenario,
        "requests": arguments.requests,
        "concurrency": arguments.concurrency,
        "body_bytes": len(plan.body or b""),
        "measured_at": datetime.now(timezone.utc).isoformat(),
        "wall_ms": round(wall_ms, 2),
        "latency_p50_ms": round(percentile(latencies, 0.50), 2),
        "latency_p95_ms": round(percentile(latencies, 0.95), 2),
        "response_bytes": sum(result.response_bytes for result in results),
        "status_counts": {str(status): count for status, count in sorted(statuses.items())},
        "recovery_ms": round(recovery_ms, 2) if recovery_ms is not None else None,
        "process": process_document,
        "metric_delta": deltas,
        "budgets_passed": not failures,
    }
    serialized = json.dumps(document, indent=2, sort_keys=True)
    print(serialized)
    if arguments.output:
        Path(arguments.output).write_text(serialized + "\n", encoding="utf-8")
    if failures and arguments.enforce:
        raise PressureFailure("; ".join(failures))


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser()
    root.add_argument("--scenario", choices=("read", "csv", "auth", "queue", "disconnect"), required=True)
    root.add_argument("--base-url", default=os.environ.get("PFH_PRESSURE_BASE_URL", ""))
    root.add_argument("--requests", type=int, default=100)
    root.add_argument("--concurrency", type=int, default=8)
    root.add_argument("--body-bytes", type=int, default=64 * 1024)
    root.add_argument("--timeout", type=float, default=30)
    root.add_argument("--recovery-timeout", type=float, default=10)
    root.add_argument("--cooldown", type=float, default=2)
    root.add_argument("--pid", type=int, default=configured_pid())
    root.add_argument("--max-peak-rss-growth-mib", type=float, default=256)
    root.add_argument("--max-final-rss-growth-mib", type=float, default=96)
    root.add_argument("--output")
    root.add_argument("--enforce", action="store_true")
    return root


def validate_arguments(arguments: argparse.Namespace) -> None:
    if not arguments.base_url:
        raise PressureFailure("--base-url or PFH_PRESSURE_BASE_URL is required")
    try:
        parsed = urlsplit(arguments.base_url)
        parsed.port
    except ValueError as error:
        raise PressureFailure("base URL is invalid") from error
    if (
        parsed.scheme not in {"http", "https"}
        or not parsed.hostname
        or any(character.isspace() for character in arguments.base_url)
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
        or parsed.path not in {"", "/"}
    ):
        raise PressureFailure("base URL must be an HTTP(S) origin without credentials")
    if arguments.requests <= 0 or arguments.requests > 100_000:
        raise PressureFailure("requests must be between 1 and 100000")
    if arguments.concurrency <= 0 or arguments.concurrency > 512:
        raise PressureFailure("concurrency must be between 1 and 512")
    if arguments.body_bytes < 1 or arguments.body_bytes > 8 * 1024 * 1024:
        raise PressureFailure("body bytes must be between 1 byte and 8 MiB")
    if (
        not math.isfinite(arguments.timeout)
        or arguments.timeout <= 0
        or arguments.timeout > 300
    ):
        raise PressureFailure("timeout must be between 0 and 300 seconds")
    if (
        not math.isfinite(arguments.recovery_timeout)
        or arguments.recovery_timeout <= 0
        or arguments.recovery_timeout > 300
    ):
        raise PressureFailure("recovery timeout must be between 0 and 300 seconds")
    if (
        not math.isfinite(arguments.cooldown)
        or arguments.cooldown < 0
        or arguments.cooldown > 60
    ):
        raise PressureFailure("cooldown must be between 0 and 60 seconds")
    if arguments.pid is not None and arguments.pid <= 0:
        raise PressureFailure("pid must be positive")
    if (
        not math.isfinite(arguments.max_peak_rss_growth_mib)
        or not math.isfinite(arguments.max_final_rss_growth_mib)
        or arguments.max_peak_rss_growth_mib < 0
        or arguments.max_final_rss_growth_mib < 0
    ):
        raise PressureFailure("RSS growth budgets must be finite and non-negative")


def main() -> int:
    arguments = parser().parse_args()
    validate_arguments(arguments)
    run(arguments)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except PressureFailure as error:
        print(f"Phase 2 pressure: FAIL: {error}", file=os.sys.stderr)
        raise SystemExit(1)
