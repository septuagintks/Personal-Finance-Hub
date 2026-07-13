"""Keep the Domain currency catalog aligned with the V2 database seed."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
DOMAIN = ROOT / "src/domain/currency.cpp"
MIGRATION = ROOT / "migrations/V2__seed_initial_currencies.sql"


def decode_cpp_string(value: str) -> str:
    decoded = bytes(value, "ascii").decode("unicode_escape")
    return decoded.encode("latin1").decode("utf-8")


def main() -> int:
    domain_source = DOMAIN.read_text(encoding="utf-8")
    migration_source = MIGRATION.read_text(encoding="utf-8")

    domain_pattern = re.compile(
        r'\{"([A-Z]+)", "((?:\\.|[^"])*)", '
        r'"((?:\\.|[^"])*)", ([0-9]+), (true|false)\}'
    )
    domain_rows = [
        (
            code,
            decode_cpp_string(symbol),
            decode_cpp_string(display_name),
            int(precision),
            is_crypto == "true",
        )
        for code, symbol, display_name, precision, is_crypto
        in domain_pattern.findall(domain_source)
    ]

    seed_pattern = re.compile(
        r"\('([A-Z]+)', '[^']*', '([^']*)', '([^']*)', "
        r"([0-9]+), (TRUE|FALSE), TRUE\)"
    )
    seed_rows = [
        (code, symbol, display_name, int(precision), is_crypto == "TRUE")
        for code, display_name, symbol, precision, is_crypto
        in seed_pattern.findall(migration_source)
    ]

    if not domain_rows or not seed_rows:
        print("ERROR: currency catalog or V2 seed could not be parsed")
        return 1
    if domain_rows != seed_rows:
        print("ERROR: Domain currency catalog differs from V2 seed")
        domain_by_code = {row[0]: row[1:] for row in domain_rows}
        seed_by_code = {row[0]: row[1:] for row in seed_rows}
        for code in sorted(set(domain_by_code) | set(seed_by_code)):
            if domain_by_code.get(code) != seed_by_code.get(code):
                print(
                    f"  {code}: domain={domain_by_code.get(code)!r} "
                    f"seed={seed_by_code.get(code)!r}"
                )
        return 1

    print(f"Currency catalog parity: PASS ({len(domain_rows)} entries)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
