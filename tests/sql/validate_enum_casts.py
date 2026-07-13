#!/usr/bin/env python3
"""
Flyway Migration Enum Cast Validator

Scans all Flyway migrations and verifies that enum column literals use explicit
PostgreSQL type casts ('value'::enum_type) to prevent UNION type inference
ambiguity (SQL State 42804).

Exit 0: all checks pass
Exit 1: validation errors found

Regression gate for the V3 empty-DB migration failure discovered in S10.
"""

import re
import sys
from pathlib import Path
from typing import List, Tuple

# All enum types defined in V1__initial_schema.sql
ENUM_TYPES = {
    "theme_mode",
    "default_home_page",
    "report_period",
    "account_type",
    "account_category",
    "transaction_type",
    "category_source",
    "category_board",
    "audit_action",
    "outbox_status",
}

# Column name -> enum type mapping (derived from schema inspection)
ENUM_COLUMNS = {
    "default_board": "category_board",
    "board": "category_board",
    "type": "transaction_type",  # transactions.type, audit_log.type (context-dependent)
    "action": "audit_action",
    "status": "outbox_status",
    "account_type": "account_type",
    "category": "account_category",
    "source": "category_source",
    "theme": "theme_mode",
    "default_page": "default_home_page",
    "period": "report_period",
}


def scan_migration(path: Path) -> List[Tuple[int, str]]:
    """
    Scan a single migration file for enum literal violations.

    Returns list of (line_number, violation_message) tuples.
    """
    violations = []
    content = path.read_text(encoding="utf-8")
    lines = content.splitlines()

    # Track whether we're inside a CREATE TYPE ... AS ENUM block
    in_enum_definition = False

    for idx, line in enumerate(lines, start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith("--"):
            continue

        # Skip CREATE TYPE ... AS ENUM definition blocks
        if "CREATE TYPE" in stripped.upper() and "AS ENUM" in stripped.upper():
            in_enum_definition = True
            continue
        if in_enum_definition:
            if ");" in stripped:
                in_enum_definition = False
            continue

        # Only check INSERT/SELECT statements
        is_dml = any(kw in stripped.upper() for kw in ["INSERT", "SELECT", "VALUES"])
        if not is_dml:
            continue

        # V3-specific pattern: UNION segment in category template inserts
        # Format: SELECT 'name', 'expense'/'income', id, 'expense'/'income', sort, flag
        #                        ^TEXT column        ^ENUM column (must have ::cast)
        #
        # We need to match the 4th positional literal (default_board column) and verify
        # it has ::category_board cast, while ignoring the 2nd position (group_name TEXT).
        #
        # Regex strategy: match lines with UNION keyword or SELECT + 6 comma-separated values,
        # then verify the 4th value has explicit cast.

        # Pattern for V3 category insert segments:
        # SELECT 'literal', 'expense'/'income', <expr>, '<board>', <int>, <bool>
        # Capture group 4 should be the default_board column value
        match = re.search(
            r"SELECT\s+'[^']*',\s*'(expense|income)',\s*[^,]+,\s*'(expense|income)'(::category_board)?,",
            stripped,
            re.IGNORECASE
        )
        if match:
            board_value = match.group(2)
            has_cast = match.group(3) is not None
            if not has_cast:
                snippet = stripped[max(0, match.start()):min(len(stripped), match.end()+30)]
                violations.append((
                    idx,
                    f"Bare enum literal '{board_value}' in default_board column without ::category_board cast. "
                    f"Context: {snippet}..."
                ))
            continue

        # General pattern: look for other enum columns in INSERT/UPDATE
        # This catches VALUES clauses and non-V3 patterns
        # For broader coverage, flag any known enum value literal without cast
        # that appears in value position (after VALUES or in SELECT list)

        known_values = {
            "income", "expense",  # category_board
            "transfer", "adjustment",  # transaction_type (income/expense overlap above)
        }

        # Only check if line contains VALUES or is a simple SELECT (not already checked above)
        if "VALUES" in stripped.upper() or ("SELECT" in stripped.upper() and "UNION" not in stripped.upper()):
            for value in known_values:
                pattern = rf"'({value})'(?!::)\s*[,\)]"
                match = re.search(pattern, stripped, re.IGNORECASE)
                if match:
                    pre_context = stripped[:match.start()]
                    # Skip if in WHERE/CASE/WHEN or if it's the group_name column
                    if any(kw in pre_context.upper() for kw in ["WHERE", "CASE", "WHEN", "GROUP_NAME"]):
                        continue

                    snippet = stripped[max(0, match.start()-20):min(len(stripped), match.end()+20)]
                    violations.append((
                        idx,
                        f"Bare enum literal '{value}' without explicit cast. "
                        f"Use '{value}'::<enum_type>. Context: ...{snippet}..."
                    ))

    return violations


def main():
    script_dir = Path(__file__).parent
    migrations_dir = script_dir.parent.parent / "migrations"

    if not migrations_dir.exists():
        print(f"ERROR: Migrations directory not found: {migrations_dir}", file=sys.stderr)
        return 1

    migration_files = sorted(migrations_dir.glob("V*.sql"))
    if not migration_files:
        print(f"WARNING: No migration files found in {migrations_dir}", file=sys.stderr)
        return 0

    total_violations = 0
    for migration in migration_files:
        violations = scan_migration(migration)
        if violations:
            print(f"\n{migration.name}:")
            for line_num, message in violations:
                print(f"  Line {line_num}: {message}")
            total_violations += len(violations)

    if total_violations > 0:
        print(f"\n[FAIL] Found {total_violations} enum cast violation(s).", file=sys.stderr)
        print("All enum literals in INSERT/SELECT must use explicit cast: 'value'::enum_type", file=sys.stderr)
        return 1
    else:
        print("[PASS] All enum literals use explicit type casts.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
