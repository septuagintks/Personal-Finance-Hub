"""Offline release-candidate checks for the Web edge and dependency boundary."""

import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []
    nginx = read("docker/frontend/nginx.conf")
    web_dockerfile = read("docker/frontend/Dockerfile")
    api_dockerfile = read("Dockerfile")
    compose = read("docker-compose.yml")
    vite = read("frontend/vite.config.ts")
    styles = read("frontend/src/styles/main.css")
    playwright = read("frontend/playwright.config.ts")
    performance = read("tools/phase2_performance.py")
    transport = read("src/infrastructure/external/curl_http_transport.cpp")
    package = json.loads(read("frontend/package.json"))
    vcpkg = json.loads(read("vcpkg.json"))

    for header in (
        "Content-Security-Policy",
        "X-Frame-Options",
        "X-Content-Type-Options",
        "Referrer-Policy",
        "Permissions-Policy",
        "Cross-Origin-Opener-Policy",
    ):
        require(header in nginx, f"Web edge must emit {header}", failures)
    require(
        "frame-ancestors 'none'" in nginx
        and 'X-Frame-Options "DENY"' in nginx
        and "script-src 'self'" in nginx,
        "Web edge CSP and clickjacking boundary are incomplete",
        failures,
    )
    require(
        "server app:8080" in nginx
        and "proxy_pass http://pfh_backend" in nginx
        and "http://backend:8080" not in nginx,
        "Web edge must proxy to the Compose app service",
        failures,
    )
    require(
        "max-age=31536000, immutable" in nginx and 'default "no-store"' in nginx,
        "Hashed assets and HTML must have distinct cache policies",
        failures,
    )
    require(
        "FROM node:24.15.0-alpine AS build" in web_dockerfile
        and "pnpm install --frozen-lockfile" in web_dockerfile
        and "USER pfh" in web_dockerfile,
        "Web image must cold-build from the lockfile and run non-root",
        failures,
    )
    require(
        "http://127.0.0.1:8080/livez" in api_dockerfile
        and "/api/v1/currencies" not in api_dockerfile.split("HEALTHCHECK", 1)[-1],
        "Backend image healthcheck must use queue-independent liveness",
        failures,
    )
    require(
        re.search(r"(?m)^  web:\s*$", compose) is not None
        and 'dockerfile: docker/frontend/Dockerfile' in compose
        and compose.count("read_only: true") >= 2
        and compose.count("no-new-privileges:true") >= 2
        and compose.count("cap_drop:") >= 2,
        "Compose must include hardened read-only Web and API services",
        failures,
    )
    app_section = compose.split("\n  app:", 1)[1].split("\n  web:", 1)[0]
    require(
        "\n    ports:" not in app_section
        and '\n    expose:\n      - "8080"' in app_section,
        "Compose Backend must remain internal to the same-origin Web edge",
        failures,
    )
    require(
        "manifest: true" in vite
        and "sourcemap: false" in vite
        and "charts:" in vite,
        "Vite release output must be auditable, map-free, and isolate charts",
        failures,
    )
    require(
        "@media (prefers-reduced-motion: reduce)" in styles,
        "Global styles must honor reduced-motion preference",
        failures,
    )
    require(
        "PLAYWRIGHT_FULL_BROWSERS" in playwright
        and "Desktop Chrome" in playwright
        and "Desktop Firefox" in playwright
        and "Desktop Safari" in playwright,
        "Playwright must expose an explicit Chromium, Firefox, and WebKit release matrix",
        failures,
    )
    scripts = package.get("scripts", {})
    for script in ("bundle:check", "dependency:check", "security:check", "release:check"):
        require(script in scripts, f"Frontend package is missing {script}", failures)
    require(
        "element-plus" not in package.get("dependencies", {}),
        "Unused Element Plus must not ship in the release dependency graph",
        failures,
    )
    require(
        re.fullmatch(r"[0-9a-f]{40}", vcpkg.get("builtin-baseline", "")) is not None,
        "vcpkg baseline must be an immutable commit hash",
        failures,
    )
    require(
        '"daily": Profile(10_000, 20, 200' in performance
        and '"stress": Profile(100_000, 50, 500' in performance,
        "Performance fixtures must retain the fixed Daily and Stress profiles",
        failures,
    )
    require(
        'psql_environment["PGDATABASE"] = database_url' in performance
        and "--confirm-test-database" in performance
        and 'fixture_prefix = "PFH-PERF-%"' in performance
        and "[arguments.psql, database_url" not in performance,
        "Performance seeding must isolate profiles, protect test data, and keep credentials out of argv",
        failures,
    )
    require(
        performance.count("NULL::BIGINT, 'transfer'::transaction_type") == 2,
        "Performance transfer fixtures must type nullable category ids for PostgreSQL unions",
        failures,
    )
    require(
        'export_days = 365 if arguments.profile == "daily" else 28' in performance,
        "Performance CSV windows must stay within the 366-day exclusive range",
        failures,
    )
    require(
        "static_cast<volatile char*>(value.data())" in transport
        and 'CURLOPT_FOLLOWLOCATION, 0L' in transport
        and 'CURLOPT_PROTOCOLS_STR, "https"' in transport,
        "Provider transport must cleanse credential-bearing URLs and stay on HTTPS",
        failures,
    )

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}")
        return 1
    print("Release hardening structural contracts: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
