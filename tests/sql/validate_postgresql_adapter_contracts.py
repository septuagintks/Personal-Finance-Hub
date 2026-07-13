"""Offline structural checks for the PostgreSQL persistence adapters."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []
    cmake = read("CMakeLists.txt")
    support = read(
        "include/pfh/infrastructure/persistence/postgres_repository_support.h"
    )
    rls_header = read("include/pfh/infrastructure/persistence/rls_session.h")
    rls_source = read("src/infrastructure/persistence/rls_session.cpp")
    account = read("src/infrastructure/persistence/account_repository_impl.cpp")
    transaction = read(
        "src/infrastructure/persistence/transaction_repository_impl.cpp"
    )
    user = read("src/infrastructure/persistence/user_repository_impl.cpp")
    preference = read(
        "src/infrastructure/persistence/user_preference_repository_impl.cpp"
    )
    active_currency = read(
        "src/infrastructure/persistence/postgres_active_currency_query.cpp"
    )

    required_sources = [
        "drogon_unit_of_work.cpp",
        "postgres_repository_support.cpp",
        "postgres_active_currency_query.cpp",
        "account_repository_impl.cpp",
        "transaction_repository_impl.cpp",
        "category_repository_impl.cpp",
        "user_repository_impl.cpp",
        "user_preference_repository_impl.cpp",
        "exchange_rate_repository_impl.cpp",
        "postgres_result_set.cpp",
        "rls_session.cpp",
    ]
    require(
        "if(PFH_HAS_POSTGRESQL)" not in cmake,
        "CMake must branch on PFH_BUILD_POSTGRESQL, not a C++ macro",
        failures,
    )
    for source in required_sources:
        require(source in cmake, f"CMake does not list {source}", failures)
    require(
        "pfh_postgresql_adapter_compile_gate" in cmake,
        "PostgreSQL adapter compile gate is missing",
        failures,
    )

    adapter_roots = [
        ROOT / "include/pfh/infrastructure/persistence",
        ROOT / "src/infrastructure/persistence",
    ]
    invalid_id_access = re.compile(r"\.value\b(?!\s*\()")
    for adapter_root in adapter_roots:
        for path in adapter_root.iterdir():
            if path.suffix not in {".h", ".cpp"}:
                continue
            for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), start=1
            ):
                if invalid_id_access.search(line):
                    failures.append(
                        f"{path.relative_to(ROOT)}:{line_number}: "
                        "TypedId must use value()"
                    )

    tenant_sources = [
        "src/infrastructure/persistence/account_repository_impl.cpp",
        "src/infrastructure/persistence/transaction_repository_impl.cpp",
        "src/infrastructure/persistence/category_repository_impl.cpp",
        "src/infrastructure/persistence/user_preference_repository_impl.cpp",
    ]
    for path in tenant_sources:
        require(
            "db_->execSqlSync" not in read(path),
            f"{path} performs an unpinned pooled read on an RLS table",
            failures,
        )

    require(
        "set_config('app.current_user_id', $1, true)" in rls_source,
        "RLS user id must be transaction-local",
        failures,
    )
    require(
        "resetAppUserId" not in rls_header + rls_source,
        "SET LOCAL must clear at transaction end; manual pooled RESET is unsafe",
        failures,
    )
    require(
        'execSqlSync("COMMIT")' not in support
        and 'execSqlSync("COMMIT")'
        not in read("src/infrastructure/persistence/drogon_unit_of_work.cpp"),
        "Drogon transactions must commit through their lifecycle callback",
        failures,
    )
    require(
        "commit_result.get()" in support,
        "Unit of Work must wait for Drogon's commit result",
        failures,
    )
    require(
        "User creation requires an unscoped registration transaction" in user
        and "!tenant.has_value() || *tenant != user.id()" in user,
        "Global users-table writes must enforce registration/authenticated scope",
        failures,
    )
    require(
        "find user by username in transaction" in user
        and "find user preferences in transaction" in preference
        and "load_preference((*context)->transaction(), user_id)" in preference,
        "User and preference adapters must provide transaction-aware reads",
        failures,
    )
    require(
        "FROM accounts" in active_currency
        and "FROM users" in active_currency
        and "UNION" in active_currency,
        "Active-currency query must include account and reporting-base currencies",
        failures,
    )

    for token in (
        "COALESCE(MAX(version), 0)",
        "last_transaction_value",
        "INSERT INTO account_balance_cache",
    ):
        require(token in account, f"Balance cache contract is missing: {token}", failures)

    for token in (
        "target_groups AS MATERIALIZED",
        "affected_accounts AS MATERIALIZED",
        "locked_accounts AS MATERIALIZED",
        "FOR UPDATE OF a NOWAIT",
        "deleted_transactions AS",
        "deleted_groups AS",
        "deleted_tag_relations AS",
        "adjustment, adjustment.amount(), group_id",
    ):
        require(
            token in transaction,
            f"Transfer aggregate persistence contract is missing: {token}",
            failures,
        )

    account_purge_start = transaction.find(
        "TransactionRepositoryImpl::physical_delete_by_account"
    )
    transfer_purge_start = transaction.find(
        "TransactionRepositoryImpl::physical_delete_transfers_touching_account"
    )
    account_purge = transaction[account_purge_start:transfer_purge_start]
    cache_delete = account_purge.find("deleted_balance_cache AS")
    transaction_delete = account_purge.find("DELETE FROM transactions")
    require(
        account_purge_start >= 0
        and transfer_purge_start > account_purge_start
        and cache_delete >= 0
        and transaction_delete > cache_delete,
        "Account purge must invalidate balance cache before deleting referenced transactions",
        failures,
    )
    transfer_purge = transaction[transfer_purge_start:]
    transfer_cache_delete = transfer_purge.find("deleted_balance_cache AS")
    transfer_transaction_delete = transfer_purge.find("deleted_transactions AS")
    require(
        transfer_cache_delete >= 0
        and transfer_transaction_delete > transfer_cache_delete,
        "Transfer purge must invalidate all affected caches before deleting legs",
        failures,
    )
    require(
        "FROM accounts a\n            JOIN transactions t" in transaction
        and "FOR UPDATE OF a NOWAIT" in transaction,
        "Soft delete must lock its account aggregate before changing balance facts",
        failures,
    )
    require(
        "FROM accounts WHERE id = $1 AND user_id = $2 FOR UPDATE"
        in account,
        "Balance cache rebuild must lock the account aggregate",
        failures,
    )

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}")
        return 1

    print("PostgreSQL adapter structural contracts: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
