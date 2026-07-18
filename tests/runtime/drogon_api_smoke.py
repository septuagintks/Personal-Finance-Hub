#!/usr/bin/env python3
"""Run a credential-safe API smoke test against the real Drogon server."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import secrets
import shlex
import signal
import subprocess
import sys
import tempfile
import time
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import ProxyHandler, Request, build_opener


class SmokeFailure(RuntimeError):
    pass


class Client:
    def __init__(self, base_url: str) -> None:
        self.base_url = base_url
        self.opener = build_opener(ProxyHandler({}))

    def request(
        self,
        method: str,
        path: str,
        body: Any | None = None,
        token: str | None = None,
        headers: dict[str, str] | None = None,
        raw_body: bytes | None = None,
    ) -> tuple[int, dict[str, str], bytes]:
        request_headers = dict(headers or {})
        if token:
            request_headers["Authorization"] = f"Bearer {token}"
        data = raw_body
        if body is not None:
            data = json.dumps(body, separators=(",", ":")).encode("utf-8")
            request_headers["Content-Type"] = "application/json"
        request = Request(
            self.base_url + path,
            data=data,
            headers=request_headers,
            method=method,
        )
        try:
            with self.opener.open(request, timeout=10) as response:
                return (
                    response.status,
                    response_headers(response.headers),
                    response.read(),
                )
        except HTTPError as error:
            return (
                error.code,
                response_headers(error.headers),
                error.read(),
            )


def response_headers(message: Any) -> dict[str, str]:
    headers = {name.lower(): value for name, value in message.items()}
    content_types = message.get_all("Content-Type") or []
    headers["x-content-type-count"] = str(len(content_types))
    return headers


def decoded(body: bytes) -> Any:
    if not body:
        return None
    return json.loads(body.decode("utf-8"))


def expect_status(
    response: tuple[int, dict[str, str], bytes],
    expected: int,
    operation: str,
) -> Any:
    status, _, body = response
    if status != expected:
        keys: list[str] = []
        try:
            parsed = decoded(body)
            if isinstance(parsed, dict):
                keys = sorted(parsed.keys())
        except (UnicodeDecodeError, json.JSONDecodeError):
            pass
        raise SmokeFailure(
            f"{operation}: expected HTTP {expected}, got {status}; body keys={keys}"
        )
    return decoded(body)


def expect_trace(response: tuple[int, dict[str, str], bytes], operation: str) -> None:
    _, headers, body = response
    if not headers.get("x-trace-id"):
        raise SmokeFailure(f"{operation}: X-Trace-Id is missing")
    lowered = body.decode("utf-8", errors="replace").lower()
    forbidden = ("select ", "postgresql", ".cpp", "/users/", "authorization:")
    if any(value in lowered for value in forbidden):
        raise SmokeFailure(f"{operation}: response leaked an implementation detail")


def register(client: Client, username: str, password: str) -> dict[str, Any]:
    response = client.request(
        "POST",
        "/api/v1/auth/register",
        {
            "username": username,
            "password": password,
            "baseCurrency": "USD",
            "preferredLocale": "en-US",
        },
    )
    body = expect_status(response, 201, "register")
    expect_trace(response, "register")
    if not all(body.get(key) for key in ("userId", "accessToken", "refreshToken")):
        raise SmokeFailure("register: token pair or user id is missing")
    return body


def login(client: Client, username: str, password: str) -> dict[str, Any]:
    return expect_status(
        client.request(
            "POST",
            "/api/v1/auth/login",
            {"username": username, "password": password},
        ),
        200,
        "login",
    )


def create_account(
    client: Client,
    token: str,
    name: str,
    currency: str = "USD",
) -> dict[str, Any]:
    return expect_status(
        client.request(
            "POST",
            "/api/v1/accounts",
            {
                "name": name,
                "type": "savings",
                "subtype": "fixture",
                "currencyCode": currency,
            },
            token,
            {"Idempotency-Key": f"runtime-account-{secrets.token_hex(8)}"},
        ),
        201,
        "create account",
    )


def run_smoke(client: Client) -> None:
    currencies_response = client.request("GET", "/api/v1/currencies")
    currencies = expect_status(currencies_response, 200, "currencies")
    _, currency_headers, _ = currencies_response
    if len(currencies) != 33 or not currency_headers.get("etag"):
        raise SmokeFailure("currencies: expected 33 rows and ETag")
    if (
        currency_headers.get("x-content-type-count") != "1"
        or currency_headers.get("content-type")
        != "application/json; charset=utf-8"
    ):
        raise SmokeFailure("currencies: expected one JSON Content-Type header")
    expect_trace(currencies_response, "currencies")
    cached = client.request(
        "GET",
        "/api/v1/currencies",
        headers={"If-None-Match": currency_headers["etag"]},
    )
    expect_status(cached, 304, "currencies conditional request")

    malformed = client.request(
        "POST",
        "/api/v1/auth/login",
        raw_body=b"{not-json",
        headers={"Content-Type": "application/json"},
    )
    expect_status(malformed, 400, "malformed login")
    expect_trace(malformed, "malformed login")

    suffix = secrets.token_hex(8)
    alice_name = f"alice-{suffix}@example.test"
    bob_name = f"bob-{suffix}@example.test"
    password = secrets.token_urlsafe(24)
    alice = register(client, alice_name, password)
    bob = register(client, bob_name, password)

    wrong_password = client.request(
        "POST",
        "/api/v1/auth/login",
        {"username": alice_name, "password": secrets.token_urlsafe(24)},
    )
    expect_status(wrong_password, 401, "wrong-password login")
    expect_trace(wrong_password, "wrong-password login")

    rotated_response = client.request(
        "POST",
        "/api/v1/auth/refresh",
        {"refreshToken": alice["refreshToken"]},
    )
    rotated = expect_status(rotated_response, 200, "refresh")
    if rotated["refreshToken"] == alice["refreshToken"]:
        raise SmokeFailure("refresh: token was not rotated")
    reuse = client.request(
        "POST",
        "/api/v1/auth/refresh",
        {"refreshToken": alice["refreshToken"]},
    )
    expect_status(reuse, 401, "refresh reuse")
    expect_status(
        client.request("GET", "/api/v1/accounts", token=rotated["accessToken"]),
        401,
        "session revocation",
    )

    alice_session = login(client, alice_name, password)
    alice_token = alice_session["accessToken"]
    bob_token = bob["accessToken"]
    source = create_account(client, alice_token, "Source Wallet")
    target = create_account(client, alice_token, "Target Wallet")
    bob_accounts = expect_status(
        client.request("GET", "/api/v1/accounts", token=bob_token),
        200,
        "Bob account list",
    )
    if bob_accounts:
        raise SmokeFailure("tenant isolation: Bob observed Alice accounts")
    expect_status(
        client.request(
            "GET",
            f"/api/v1/accounts/{source['id']}/balance",
            token=bob_token,
        ),
        404,
        "foreign account balance",
    )

    category = expect_status(
        client.request(
            "POST",
            "/api/v1/categories",
            {"board": "expense", "name": "Runtime Expense"},
            alice_token,
            {"Idempotency-Key": f"runtime-category-{suffix}"},
        ),
        201,
        "create category",
    )
    tag = expect_status(
        client.request(
            "POST",
            "/api/v1/tags",
            {"name": "runtime"},
            alice_token,
            {"Idempotency-Key": f"runtime-tag-{suffix}"},
        ),
        201,
        "create tag",
    )
    preference = expect_status(
        client.request("GET", "/api/v1/users/me/preferences", token=alice_token),
        200,
        "get preference",
    )
    if preference["baseCurrency"] != "USD":
        raise SmokeFailure("preference: unexpected base currency")

    income = expect_status(
        client.request(
            "POST",
            "/api/v1/transactions",
            {
                "accountId": source["id"],
                "type": "income",
                "amount": "250.125",
                "currencyCode": "USD",
                "occurredAt": "2026-07-10T08:30:00+08:00",
            },
            alice_token,
            {"Idempotency-Key": f"runtime-income-{suffix}"},
        ),
        201,
        "create income",
    )
    expense = expect_status(
        client.request(
            "POST",
            "/api/v1/transactions",
            {
                "accountId": source["id"],
                "type": "expense",
                "amount": "50.125",
                "currencyCode": "USD",
                "categoryId": category["id"],
                "occurredAt": "2026-07-11T09:45:00+08:00",
            },
            alice_token,
            {"Idempotency-Key": f"runtime-expense-{suffix}"},
        ),
        201,
        "create expense",
    )
    expect_status(
        client.request(
            "PUT",
            f"/api/v1/transactions/{expense['id']}/tags",
            {"tagIds": [tag["id"]]},
            alice_token,
        ),
        200,
        "attach tag",
    )
    numeric_amount = client.request(
        "POST",
        "/api/v1/transactions",
        {
            "accountId": source["id"],
            "type": "income",
            "amount": 1.25,
            "currencyCode": "USD",
        },
        alice_token,
        {"Idempotency-Key": f"runtime-invalid-amount-{suffix}"},
    )
    expect_status(numeric_amount, 400, "JSON number amount rejection")
    expect_trace(numeric_amount, "JSON number amount rejection")

    transfer = expect_status(
        client.request(
            "POST",
            "/api/v1/transfers",
            {
                "sourceAccountId": source["id"],
                "targetAccountId": target["id"],
                "mode": "BothAmounts",
                "outgoingAmount": "25",
                "incomingAmount": "25",
                "occurredAt": "2026-07-12T10:00:00+08:00",
            },
            alice_token,
            {"Idempotency-Key": f"runtime-transfer-{suffix}"},
        ),
        201,
        "create transfer",
    )
    if not isinstance(transfer["outgoingAmount"], str):
        raise SmokeFailure("transfer: amount is not serialized as a string")
    expect_status(
        client.request(
            "GET",
            f"/api/v1/transfers/{transfer['transferGroupId']}",
            token=bob_token,
        ),
        404,
        "foreign transfer",
    )

    balance = expect_status(
        client.request(
            "GET",
            f"/api/v1/accounts/{source['id']}/balance",
            token=alice_token,
        ),
        200,
        "source balance",
    )
    if balance["balance"] != "175":
        raise SmokeFailure("balance: income/expense/transfer result is incorrect")
    expect_status(
        client.request("GET", "/api/v1/reports/net-worth", token=alice_token),
        200,
        "net worth",
    )
    expect_status(
        client.request(
            "GET", "/api/v1/reports/dashboard-summary", token=alice_token
        ),
        200,
        "dashboard",
    )
    query = urlencode(
        {"startDate": "2026-07", "endDate": "2026-07", "periodType": "MONTH"}
    )
    expect_status(
        client.request(
            "GET", f"/api/v1/reports/cash-flow?{query}", token=alice_token
        ),
        200,
        "cash flow",
    )
    bob_net_worth = expect_status(
        client.request("GET", "/api/v1/reports/net-worth", token=bob_token),
        200,
        "Bob net worth",
    )
    if bob_net_worth["netWorth"] != "0":
        raise SmokeFailure("tenant isolation: Bob observed Alice net worth")

    logout = client.request(
        "POST",
        "/api/v1/auth/logout",
        {"refreshToken": alice_session["refreshToken"]},
        alice_token,
    )
    expect_status(logout, 204, "logout")
    expect_status(
        client.request("GET", "/api/v1/accounts", token=alice_token),
        401,
        "access after logout",
    )
    if income.get("amount") != "250.125":
        raise SmokeFailure("transaction: amount did not round-trip as a string")


def runtime_config(port: int) -> dict[str, Any]:
    return {
        "environment": "integration-test",
        "server": {"host": "127.0.0.1", "port": port, "threads": 2},
        "database": {
            "host": "127.0.0.1",
            "port": 5432,
            "name": "override-required",
            "user": "override-required",
            "password": "REPLACE_WITH_REQUEST_PASSWORD",
            "pool_size": 6,
            "connection_timeout": 5,
        },
        "background_database": {
            "host": "127.0.0.1",
            "port": 5432,
            "name": "override-required",
            "user": "override-required-background",
            "password": "REPLACE_WITH_BACKGROUND_PASSWORD",
            "pool_size": 2,
            "connection_timeout": 5,
        },
        "jwt": {
            "secret": "REPLACE_WITH_JWT_SECRET",
            "issuer": "pfh-api",
            "audience": "pfh-client",
            "access_token_expiry_seconds": 900,
            "refresh_token_expiry_seconds": 2592000,
            "clock_skew_seconds": 30,
        },
        "logging": {"level": "warning", "output": "console"},
        "scheduler": {"enabled": False},
        "exchange_rate": {
            "provider": "freecurrencyapi",
            "api_key": "",
            "request_timeout_seconds": 5,
        },
    }


def database_environment(conninfo: str, prefix: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for item in shlex.split(conninfo):
        key, separator, value = item.partition("=")
        if not separator:
            raise SmokeFailure(f"invalid {prefix} database conninfo field")
        fields[key] = value

    mapping = {
        "host": "HOST",
        "port": "PORT",
        "dbname": "NAME",
        "user": "USER",
        "password": "PASSWORD",
    }
    missing = [name for name in mapping if not fields.get(name)]
    if missing:
        raise SmokeFailure(f"{prefix} database conninfo is missing fields: {missing}")
    return {f"{prefix}_{suffix}": fields[name] for name, suffix in mapping.items()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--port", required=True, type=int)
    args = parser.parse_args()
    required = ("PFH_TEST_DB_REQUEST", "PFH_TEST_DB_BACKGROUND")
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        raise SmokeFailure(f"missing required database environment: {missing}")

    environment = os.environ.copy()
    environment.update(
        database_environment(os.environ["PFH_TEST_DB_REQUEST"], "PFH_DB")
    )
    environment.update(
        database_environment(
            os.environ["PFH_TEST_DB_BACKGROUND"], "PFH_BACKGROUND_DB"
        )
    )
    environment["PFH_JWT_SECRET"] = secrets.token_urlsafe(48)
    with tempfile.TemporaryDirectory(prefix="pfh-runtime-") as temporary:
        root = Path(temporary)
        config_dir = root / "config"
        config_dir.mkdir()
        (config_dir / "config.local.json").write_text(
            json.dumps(runtime_config(args.port)), encoding="utf-8"
        )
        log_path = root / "server.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen(
                [str(args.server)],
                cwd=root,
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
        client = Client(f"http://127.0.0.1:{args.port}")
        failure: BaseException | None = None
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise SmokeFailure(
                        f"server exited during startup with code {process.returncode}"
                    )
                try:
                    if client.request("GET", "/api/v1/currencies")[0] == 200:
                        break
                except URLError:
                    time.sleep(0.1)
            else:
                raise SmokeFailure("server did not become ready within 20 seconds")
            run_smoke(client)
        except (SmokeFailure, URLError) as error:
            failure = error
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                    raise SmokeFailure("server did not stop within 15 seconds")
        if failure is not None:
            log_lines = log_path.read_text(
                encoding="utf-8", errors="replace"
            ).splitlines()
            if log_lines:
                print("Drogon server log tail:", file=sys.stderr)
                print("\n".join(log_lines[-20:]), file=sys.stderr)
            raise SmokeFailure(str(failure)) from failure
        if process.returncode != 0:
            raise SmokeFailure(
                f"server returned non-zero after graceful stop: {process.returncode}"
            )
    print("Drogon/PostgreSQL API runtime smoke: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SmokeFailure as error:
        print(f"Drogon/PostgreSQL API runtime smoke: FAIL: {error}")
        raise SystemExit(1)
