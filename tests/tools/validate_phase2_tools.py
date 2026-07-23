"""Offline contracts for the Phase 2 performance and pressure tools."""

from argparse import Namespace
from contextlib import contextmanager
import json
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools import phase2_performance as performance  # noqa: E402
from tools import phase2_pressure as pressure  # noqa: E402


@contextmanager
def environment(**values: str):
    with patch.dict(os.environ, values, clear=True):
        yield


class PerformanceToolContracts(unittest.TestCase):
    def test_percentile_and_month_shift_boundaries(self) -> None:
        self.assertEqual(performance.percentile([9, 1, 5, 3], 0.5), 3)
        self.assertEqual(performance.percentile([], 0.95), 0.0)
        instant = performance.datetime(2026, 1, 15, tzinfo=performance.timezone.utc)
        self.assertEqual(performance.shifted_month(instant, -1), "2025-12")
        self.assertEqual(performance.shifted_month(instant, 12), "2027-01")
        self.assertEqual(
            performance.analysis_month_range("daily", instant),
            ("2016-02", "2026-01"),
        )
        self.assertEqual(
            performance.analysis_month_range("stress", instant),
            ("2025-12", "2025-12"),
        )

    def test_explain_summary_walks_nested_nodes(self) -> None:
        document = [
            {
                "Planning Time": 1.23456,
                "Execution Time": 8.76543,
                "Plan": {
                    "Node Type": "Limit",
                    "Actual Rows": 50,
                    "Shared Hit Blocks": 12,
                    "Shared Read Blocks": 3,
                    "Plans": [
                        {
                            "Node Type": "Index Scan",
                            "Index Name": "idx_transactions_user_time",
                            "Relation Name": "transactions",
                        },
                        {
                            "Node Type": "Seq Scan",
                            "Relation Name": "transfer_groups",
                        },
                    ],
                },
            }
        ]

        self.assertEqual(
            performance.summarize_plan(document),
            {
                "planning_ms": 1.235,
                "execution_ms": 8.765,
                "actual_rows": 50,
                "shared_hit_blocks": 12,
                "shared_read_blocks": 3,
                "index_names": ["idx_transactions_user_time"],
                "sequential_relations": ["transfer_groups"],
            },
        )

    def test_explain_summary_rejects_invalid_envelopes(self) -> None:
        for document in ({}, [], [{"Plan": "invalid"}], [{}, {}]):
            with self.subTest(document=document):
                with self.assertRaises(performance.PerformanceFailure):
                    performance.summarize_plan(document)

    def test_explain_matrix_covers_bounded_phase2_paths(self) -> None:
        queries = performance.explain_queries("daily", 42)
        self.assertEqual(
            set(queries),
            {"transaction_page", "transfer_page", "transfer_members", "cash_flow_months"},
        )
        for query, budget, _ in queries.values():
            self.assertIn("42", query)
            self.assertGreater(budget, 0)
        self.assertTrue(queries["transaction_page"][2])
        self.assertTrue(queries["transfer_page"][2])
        self.assertFalse(queries["cash_flow_months"][2])

    def test_benchmark_argument_bounds(self) -> None:
        valid = performance.parser().parse_args(
            ["benchmark", "--profile", "daily", "--base-url", "http://127.0.0.1:8080"]
        )
        performance.validate_arguments(valid)

        invalid_cases = (
            ("base_url", "http://user:secret@localhost:8080"),
            ("base_url", "http://localhost:8080/api"),
            ("base_url", "http://localhost:99999"),
            ("iterations", 0),
            ("iterations", 1_001),
            ("warmup", -1),
            ("warmup", 101),
            ("timeout", 0),
            ("timeout", 301),
            ("timeout", float("nan")),
        )
        for name, value in invalid_cases:
            candidate = Namespace(**vars(valid))
            setattr(candidate, name, value)
            with self.subTest(name=name, value=value):
                with self.assertRaises(performance.PerformanceFailure):
                    performance.validate_arguments(candidate)


class PressureToolContracts(unittest.TestCase):
    def arguments(self, scenario: str, *extra: str) -> Namespace:
        return pressure.parser().parse_args(
            ["--scenario", scenario, "--base-url", "http://127.0.0.1:8080", *extra]
        )

    def test_request_plans_are_bounded_and_scenario_specific(self) -> None:
        with environment(PFH_PRESSURE_ACCESS_TOKEN="short-lived-token"):
            read = pressure.request_plan(self.arguments("read"))
            self.assertEqual((read.method, read.path, read.body), ("GET", "/api/v1/reports/dashboard-summary", None))
            self.assertEqual(read.headers["Authorization"], "Bearer short-lived-token")

            csv = pressure.request_plan(self.arguments("csv"))
            self.assertEqual(csv.method, "GET")
            self.assertIn("/api/v1/exports/transactions.csv?", csv.path)
            self.assertEqual(csv.headers["Accept"], "text/csv")

            queue = pressure.request_plan(self.arguments("queue", "--body-bytes", "128"))
            self.assertEqual(len(queue.body or b""), 128)
            self.assertEqual(json.loads(queue.body or b"{}"), {"padding": "x" * 114})
            self.assertFalse(queue.disconnect)

            disconnect = pressure.request_plan(
                self.arguments("disconnect", "--body-bytes", "128")
            )
            self.assertTrue(disconnect.disconnect)

        with environment(PFH_PRESSURE_AUTH_USERNAME="load@example.test"):
            auth = pressure.request_plan(self.arguments("auth"))
            self.assertEqual(auth.method, "POST")
            self.assertEqual(json.loads(auth.body or b"{}")["username"], "load@example.test")
            self.assertNotIn("Authorization", auth.headers)

    def test_protected_scenarios_require_an_access_token(self) -> None:
        for scenario in ("read", "csv", "queue", "disconnect"):
            with self.subTest(scenario=scenario), environment():
                with self.assertRaises(pressure.PressureFailure):
                    pressure.request_plan(self.arguments(scenario))

    def test_metrics_parser_and_delta_ignore_unrelated_series(self) -> None:
        raw = """
# HELP pfh_http_admission_rejections_total bounded admission failures
pfh_http_admission_rejections_total{reason="request_queue"} 7
pfh_http_admission_rejections_total{reason="auth_queue"} 2.5
pfh_report_resource_rejections_total{surface="csv_export"} 3
process_resident_memory_bytes 12345
"""
        parsed = pressure.parse_admission_metrics(raw)
        self.assertEqual(len(parsed), 3)
        self.assertEqual(parsed['pfh_http_admission_rejections_total{reason="request_queue"}'], 7)
        self.assertEqual(
            pressure.metric_delta(
                {'pfh_http_admission_rejections_total{reason="request_queue"}': 5},
                parsed,
            )['pfh_http_admission_rejections_total{reason="request_queue"}'],
            2,
        )
        self.assertIsNone(pressure.metric_delta(None, parsed))

    def test_pressure_argument_bounds(self) -> None:
        valid = self.arguments("auth")
        pressure.validate_arguments(valid)
        invalid_cases = (
            ("base_url", "ftp://localhost"),
            ("base_url", "http://localhost:8080/api"),
            ("base_url", "http://localhost:99999"),
            ("requests", 0),
            ("requests", 100_001),
            ("concurrency", 0),
            ("concurrency", 257),
            ("body_bytes", 0),
            ("body_bytes", 8 * 1024 * 1024 + 1),
            ("timeout", 0),
            ("timeout", float("inf")),
            ("recovery_timeout", 301),
            ("cooldown", 61),
            ("pid", -1),
            ("max_peak_rss_growth_mib", -1),
            ("max_peak_rss_growth_mib", float("nan")),
            ("max_final_rss_growth_mib", -1),
        )
        for name, value in invalid_cases:
            candidate = Namespace(**vars(valid))
            setattr(candidate, name, value)
            with self.subTest(name=name, value=value):
                with self.assertRaises(pressure.PressureFailure):
                    pressure.validate_arguments(candidate)

    def test_saturation_requires_matching_operator_metric_growth(self) -> None:
        self.assertIn(
            "operator admission metrics",
            pressure.saturation_failure("queue", None) or "",
        )
        self.assertIn(
            "did not reach",
            pressure.saturation_failure(
                "auth",
                {'pfh_http_admission_rejections_total{reason="request_queue"}': 2},
            )
            or "",
        )
        self.assertIsNone(
            pressure.saturation_failure(
                "auth",
                {'pfh_http_admission_rejections_total{reason="auth_rate_limit"}': 1},
            )
        )
        self.assertIsNone(pressure.saturation_failure("read", None))

    def test_invalid_environment_pid_is_a_controlled_failure(self) -> None:
        with environment(PFH_PRESSURE_SERVER_PID="not-a-pid"):
            with self.assertRaisesRegex(
                pressure.PressureFailure,
                "PFH_PRESSURE_SERVER_PID must be an integer",
            ):
                pressure.parser()


if __name__ == "__main__":
    unittest.main(verbosity=2)
