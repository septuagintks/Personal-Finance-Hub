"""Generate fixed Phase 2 PostgreSQL fixtures and measure bounded API paths."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


@dataclass(frozen=True)
class Profile:
    rows: int
    accounts: int
    dimensions: int
    list_p95_ms: float
    dashboard_p95_ms: float
    csv_first_byte_p95_ms: float


PROFILES = {
    "daily": Profile(10_000, 20, 200, 300, 500, 2_000),
    "stress": Profile(100_000, 50, 500, 500, 800, 5_000),
}


class PerformanceFailure(RuntimeError):
    pass


def fixture_sql(profile_name: str, user_id: int) -> str:
    profile = PROFILES[profile_name]
    transfer_groups = max(1, profile.rows // 50)
    regular_rows = profile.rows - transfer_groups * 2
    category_count = profile.dimensions // 2
    tag_count = profile.dimensions - category_count
    prefix = f"PFH-PERF-{profile_name}"
    fixture_prefix = "PFH-PERF-%"
    return f"""
BEGIN;

DELETE FROM transaction_tag_relations
WHERE user_id = {user_id}
  AND transaction_id IN (
      SELECT id FROM transactions
      WHERE user_id = {user_id} AND description LIKE '{fixture_prefix}');
DELETE FROM account_balance_cache
WHERE user_id = {user_id}
  AND account_id IN (
      SELECT id FROM accounts
      WHERE user_id = {user_id} AND name LIKE '{fixture_prefix} Account %');
DELETE FROM transactions
WHERE user_id = {user_id} AND description LIKE '{fixture_prefix}';
DELETE FROM transfer_groups
WHERE user_id = {user_id} AND note LIKE '{fixture_prefix} Transfer %';
DELETE FROM transaction_tags
WHERE user_id = {user_id} AND name LIKE '{fixture_prefix} Tag %';
DELETE FROM categories
WHERE user_id = {user_id} AND name LIKE '{fixture_prefix} Category %';
DELETE FROM accounts
WHERE user_id = {user_id} AND name LIKE '{fixture_prefix} Account %';

UPDATE user_preferences
SET locale = 'en-US', timezone = 'America/New_York', updated_at = NOW()
WHERE user_id = {user_id};

INSERT INTO accounts (
    user_id, name, type, subtype, category, currency_code, description)
SELECT
    {user_id},
    format('{prefix} Account %s', position),
    CASE
        WHEN position = {profile.accounts} THEN 'crypto'::account_type
        WHEN position % 7 = 0 THEN 'credit'::account_type
        WHEN position % 3 = 0 THEN 'savings'::account_type
        ELSE 'cash'::account_type
    END,
    'performance-fixture',
    CASE WHEN position % 7 = 0 THEN 'liability'::account_category
         ELSE 'asset'::account_category END,
    CASE
        WHEN position = {profile.accounts} THEN 'BTC'
        WHEN position % 3 = 0 THEN 'USD'
        WHEN position % 3 = 1 THEN 'CNY'
        ELSE 'EUR'
    END,
    CASE WHEN position = {profile.accounts}
         THEN 'missing-rate boundary; zero active balance'
         ELSE 'fixed Phase 2 performance fixture' END
FROM generate_series(1, {profile.accounts}) AS position;

INSERT INTO categories (user_id, name, board, source, sort_order)
SELECT
    {user_id},
    format('{prefix} Category %s', position),
    CASE WHEN position % 2 = 0 THEN 'income'::category_board
         ELSE 'expense'::category_board END,
    'user'::category_source,
    position
FROM generate_series(1, {category_count}) AS position;

INSERT INTO transaction_tags (user_id, name)
SELECT {user_id}, format('{prefix} Tag %s', position)
FROM generate_series(1, {tag_count}) AS position;

WITH
account_set AS (
    SELECT array_agg(id ORDER BY id) AS ids
    FROM accounts
    WHERE user_id = {user_id} AND name LIKE '{prefix} Account %'
),
income_set AS (
    SELECT array_agg(id ORDER BY id) AS ids
    FROM categories
    WHERE user_id = {user_id} AND name LIKE '{prefix} Category %'
      AND board = 'income'::category_board
),
expense_set AS (
    SELECT array_agg(id ORDER BY id) AS ids
    FROM categories
    WHERE user_id = {user_id} AND name LIKE '{prefix} Category %'
      AND board = 'expense'::category_board
),
generated AS (
    SELECT
        position,
        CASE
            WHEN position % 20 = 0 THEN 'adjustment'::transaction_type
            WHEN position % 10 = 0 THEN 'income'::transaction_type
            ELSE 'expense'::transaction_type
        END AS transaction_type,
        accounts.ids[1 + ((position - 1) % {profile.accounts - 1})] AS account_id,
        incomes.ids[1 + ((position - 1) % cardinality(incomes.ids))] AS income_category,
        expenses.ids[1 + ((position - 1) % cardinality(expenses.ids))] AS expense_category
    FROM generate_series(1, {regular_rows}) AS position
    CROSS JOIN account_set AS accounts
    CROSS JOIN income_set AS incomes
    CROSS JOIN expense_set AS expenses
)
INSERT INTO transactions (
    user_id, account_id, category_id, type, amount, currency_code,
    description, transaction_time, created_at, updated_at)
SELECT
    {user_id},
    generated.account_id,
    CASE WHEN generated.transaction_type = 'income'::transaction_type
         THEN generated.income_category ELSE generated.expense_category END,
    generated.transaction_type,
    CASE
        WHEN generated.transaction_type = 'income'::transaction_type
            THEN (10 + generated.position % 5000)::numeric(20,8)
        WHEN generated.transaction_type = 'adjustment'::transaction_type
             AND generated.position % 40 = 0
            THEN (1 + generated.position % 100)::numeric(20,8)
        ELSE -(1 + generated.position % 1000)::numeric(20,8)
    END,
    account.currency_code,
    format('{prefix}-regular-%s', generated.position),
    NOW() - make_interval(
        secs => ((generated.position::bigint * 31536000) / {regular_rows})::int),
    NOW() - make_interval(
        secs => ((generated.position::bigint * 31536000) / {regular_rows})::int)
        + INTERVAL '1 second',
    NOW()
FROM generated
JOIN accounts AS account ON account.id = generated.account_id;

INSERT INTO transfer_groups (
    user_id, note, transfer_mode, exchange_rate,
    exchange_rate_provider, exchange_rate_snapshot_time)
SELECT
    {user_id},
    format('{prefix} Transfer %s', position),
    2,
    0.1400000000,
    'performance-fixture',
    NOW() - make_interval(
        secs => ((position::bigint * 31536000) / {transfer_groups})::int)
FROM generate_series(1, {transfer_groups}) AS position;

WITH
fixture_accounts AS (
    SELECT
        MIN(id) FILTER (WHERE currency_code = 'CNY') AS source_id,
        MIN(id) FILTER (WHERE currency_code = 'USD') AS target_id
    FROM accounts
    WHERE user_id = {user_id} AND name LIKE '{prefix} Account %'
),
groups AS (
    SELECT
        id,
        row_number() OVER (ORDER BY id) AS position,
        exchange_rate_snapshot_time AS occurred_at
    FROM transfer_groups
    WHERE user_id = {user_id} AND note LIKE '{prefix} Transfer %'
)
INSERT INTO transactions (
    user_id, account_id, category_id, type, amount, currency_code,
    description, transfer_group_id, transaction_time, created_at, updated_at)
SELECT
    {user_id}, fixture_accounts.source_id, NULL::BIGINT, 'transfer'::transaction_type,
    -100.00000000, 'CNY', format('{prefix}-transfer-out-%s', groups.position),
    groups.id, groups.occurred_at, groups.occurred_at + INTERVAL '1 second', NOW()
FROM groups CROSS JOIN fixture_accounts
UNION ALL
SELECT
    {user_id}, fixture_accounts.target_id, NULL::BIGINT, 'transfer'::transaction_type,
    14.00000000, 'USD', format('{prefix}-transfer-in-%s', groups.position),
    groups.id, groups.occurred_at, groups.occurred_at + INTERVAL '1 second', NOW()
FROM groups CROSS JOIN fixture_accounts;

WITH
fixture_transactions AS (
    SELECT id, row_number() OVER (ORDER BY id) AS position
    FROM transactions
    WHERE user_id = {user_id} AND description LIKE '{prefix}-regular-%'
),
fixture_tags AS (
    SELECT array_agg(id ORDER BY id) AS ids
    FROM transaction_tags
    WHERE user_id = {user_id} AND name LIKE '{prefix} Tag %'
)
INSERT INTO transaction_tag_relations (transaction_id, tag_id, user_id)
SELECT
    transaction.id,
    tags.ids[1 + ((transaction.position - 1) % cardinality(tags.ids))],
    {user_id}
FROM fixture_transactions AS transaction
CROSS JOIN fixture_tags AS tags
WHERE transaction.position % 3 = 0;

INSERT INTO exchange_rates (
    base_currency_code, target_currency_code, rate, source, fetched_at)
SELECT pair.base, pair.target, pair.rate, 'pfh-performance-fixture', NOW() - INTERVAL '2 years'
FROM (VALUES
    ('USD', 'CNY', 7.2000000000::numeric(20,10)),
    ('USD', 'EUR', 0.9200000000::numeric(20,10))
) AS pair(base, target, rate)
WHERE NOT EXISTS (
    SELECT 1 FROM exchange_rates existing
    WHERE existing.base_currency_code = pair.base
      AND existing.target_currency_code = pair.target
      AND existing.source = 'pfh-performance-fixture');

ANALYZE accounts;
ANALYZE categories;
ANALYZE transaction_tags;
ANALYZE transaction_tag_relations;
ANALYZE transfer_groups;
ANALYZE transactions;

COMMIT;
"""


def run_psql(arguments: argparse.Namespace) -> None:
    database_url = os.environ.get("PFH_PERF_DATABASE_URL")
    if not database_url:
        raise PerformanceFailure("PFH_PERF_DATABASE_URL is required for fixture seeding")
    if not arguments.confirm_test_database:
        raise PerformanceFailure(
            "--confirm-test-database is required because fixture seeding changes test data"
        )
    sql = fixture_sql(arguments.profile, arguments.user_id)
    command = [arguments.psql, "-X", "--set=ON_ERROR_STOP=1", "--quiet"]
    psql_environment = os.environ.copy()
    psql_environment["PGDATABASE"] = database_url
    seeded = subprocess.run(
        command,
        input=sql,
        text=True,
        capture_output=True,
        check=False,
        env=psql_environment,
    )
    if seeded.returncode != 0:
        summary = seeded.stderr.strip().splitlines()[-1:] or ["psql failed"]
        raise PerformanceFailure(f"fixture seed failed: {summary[0]}")
    prefix = f"PFH-PERF-{arguments.profile}-%"
    count_query = (
        "SELECT count(*) FROM transactions "
        f"WHERE user_id={arguments.user_id} AND description LIKE '{prefix}'"
    )
    counted = subprocess.run(
        [arguments.psql, "-X", "-At", "-c", count_query],
        text=True,
        capture_output=True,
        check=False,
        env=psql_environment,
    )
    if counted.returncode != 0:
        raise PerformanceFailure("fixture count verification failed")
    actual = int(counted.stdout.strip())
    expected = PROFILES[arguments.profile].rows
    if actual != expected:
        raise PerformanceFailure(f"fixture expected {expected} rows, found {actual}")
    print(f"Phase 2 {arguments.profile} fixture: PASS rows={actual} user={arguments.user_id}")


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def measure_request(url: str, token: str, timeout: float) -> tuple[float, float, int]:
    request = Request(url, headers={"Authorization": f"Bearer {token}", "Accept": "*/*"})
    started = time.perf_counter()
    try:
        with urlopen(request, timeout=timeout) as response:
            first = response.read(1)
            first_byte_ms = (time.perf_counter() - started) * 1000
            body = first + response.read()
            total_ms = (time.perf_counter() - started) * 1000
            if response.status != 200:
                raise PerformanceFailure(f"GET {request.selector} returned HTTP {response.status}")
            return first_byte_ms, total_ms, len(body)
    except HTTPError as error:
        raise PerformanceFailure(f"GET {request.selector} returned HTTP {error.code}") from error
    except URLError as error:
        raise PerformanceFailure(f"GET {request.selector} could not reach the service") from error


def benchmark(arguments: argparse.Namespace) -> None:
    token = os.environ.get("PFH_PERF_ACCESS_TOKEN")
    if not token:
        raise PerformanceFailure("PFH_PERF_ACCESS_TOKEN is required for API benchmarking")
    base = arguments.base_url.rstrip("/")
    profile = PROFILES[arguments.profile]
    now = datetime.now(timezone.utc)
    export_days = 366 if arguments.profile == "daily" else 28
    from_value = (now - timedelta(days=export_days)).isoformat().replace("+00:00", "Z")
    to_value = (now + timedelta(days=1)).isoformat().replace("+00:00", "Z")
    endpoints = {
        "transactions_first_page": (
            "/api/v1/transactions?" + urlencode({"pageSize": 50}),
            arguments.iterations,
        ),
        "transactions_filter": (
            "/api/v1/transactions?"
            + urlencode({"pageSize": 50, "keyword": f"PFH-PERF-{arguments.profile}"}),
            arguments.iterations,
        ),
        "dashboard": ("/api/v1/reports/dashboard-summary", arguments.iterations),
        "csv_export": (
            "/api/v1/exports/transactions.csv?"
            + urlencode({"from": from_value, "to": to_value}),
            min(arguments.iterations, 5),
        ),
    }
    results: dict[str, dict[str, float | int]] = {}
    for name, (path, iterations) in endpoints.items():
        for _ in range(arguments.warmup):
            measure_request(base + path, token, arguments.timeout)
        first_bytes: list[float] = []
        totals: list[float] = []
        response_bytes = 0
        for _ in range(iterations):
            first_byte, total, response_bytes = measure_request(
                base + path, token, arguments.timeout
            )
            first_bytes.append(first_byte)
            totals.append(total)
        results[name] = {
            "iterations": iterations,
            "first_byte_p50_ms": round(statistics.median(first_bytes), 2),
            "first_byte_p95_ms": round(percentile(first_bytes, 0.95), 2),
            "total_p50_ms": round(statistics.median(totals), 2),
            "total_p95_ms": round(percentile(totals, 0.95), 2),
            "response_bytes": response_bytes,
        }

    failures = []
    if results["transactions_first_page"]["total_p95_ms"] > profile.list_p95_ms:
        failures.append("transaction first-page p95 exceeds budget")
    if results["transactions_filter"]["total_p95_ms"] > profile.list_p95_ms:
        failures.append("transaction filter p95 exceeds budget")
    if results["dashboard"]["total_p95_ms"] > profile.dashboard_p95_ms:
        failures.append("dashboard p95 exceeds budget")
    if results["csv_export"]["first_byte_p95_ms"] > profile.csv_first_byte_p95_ms:
        failures.append("CSV first-byte p95 exceeds budget")

    document = {
        "profile": arguments.profile,
        "rows": profile.rows,
        "measured_at": datetime.now(timezone.utc).isoformat(),
        "results": results,
        "budgets_passed": not failures,
    }
    serialized = json.dumps(document, indent=2, sort_keys=True)
    print(serialized)
    if arguments.output:
        Path(arguments.output).write_text(serialized + "\n", encoding="utf-8")
    if failures and arguments.enforce:
        raise PerformanceFailure("; ".join(failures))


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser()
    commands = root.add_subparsers(dest="command", required=True)
    seed = commands.add_parser("seed")
    seed.add_argument("--profile", choices=PROFILES, required=True)
    seed.add_argument("--user-id", type=int, required=True)
    seed.add_argument("--psql", default="psql")
    seed.add_argument("--confirm-test-database", action="store_true")
    seed.set_defaults(action=run_psql)

    run = commands.add_parser("benchmark")
    run.add_argument("--profile", choices=PROFILES, required=True)
    run.add_argument("--base-url", default=os.environ.get("PFH_PERF_BASE_URL", ""))
    run.add_argument("--iterations", type=int, default=30)
    run.add_argument("--warmup", type=int, default=3)
    run.add_argument("--timeout", type=float, default=30)
    run.add_argument("--output")
    run.add_argument("--enforce", action="store_true")
    run.set_defaults(action=benchmark)
    return root


def main() -> int:
    arguments = parser().parse_args()
    if getattr(arguments, "user_id", 1) <= 0:
        raise PerformanceFailure("user id must be positive")
    if arguments.command == "benchmark" and not arguments.base_url:
        raise PerformanceFailure("--base-url or PFH_PERF_BASE_URL is required")
    if getattr(arguments, "iterations", 1) <= 0 or getattr(arguments, "warmup", 0) < 0:
        raise PerformanceFailure("iterations and warmup must be non-negative")
    arguments.action(arguments)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except PerformanceFailure as error:
        print(f"Phase 2 performance: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
