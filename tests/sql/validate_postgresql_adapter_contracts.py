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
    auth_sessions = read(
        "src/infrastructure/persistence/auth_session_repository_impl.cpp"
    )
    registration_defaults = read(
        "src/infrastructure/persistence/registration_defaults_repository_impl.cpp"
    )
    composition = read("src/bootstrap/production_composition_root.cpp")
    token_service = read(
        "src/infrastructure/security/openssl_token_service.cpp"
    )
    password_hasher = read(
        "src/infrastructure/security/argon2_password_hasher.cpp"
    )
    auth_migration = read("migrations/V4__authentication_session_security.sql")

    required_sources = [
        "drogon_unit_of_work.cpp",
        "drogon_unit_of_work_factory.cpp",
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
        "auth_session_repository_impl.cpp",
        "audit_log_repository_impl.cpp",
        "registration_defaults_repository_impl.cpp",
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
    require(
        "pfh_production_bootstrap_compile_gate" in cmake
        and "pfh_production_security_compile_gate" in cmake,
        "Production bootstrap/security compile gates are missing",
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
    require(
        "FOR UPDATE" in auth_sessions
        and "revoked_sessions" in auth_sessions
        and "token_hash" in auth_sessions
        and "INSERT INTO refresh_tokens" in auth_sessions,
        "Auth session adapter must lock/rotate token hashes and support session revocation",
        failures,
    )
    require(
        "bind_tenant_once" in read(
            "src/infrastructure/persistence/drogon_transaction_context.cpp"
        )
        and "require_transaction(tx_iface, user_id)" in registration_defaults
        and "initialize registration defaults" in registration_defaults
        and "categories_initialized = TRUE" in registration_defaults,
        "Registration bootstrap must bind once and initialize defaults atomically",
        failures,
    )
    require(
        "request_db_ == background_db_" in composition
        and "PostgresActiveCurrencyQuery" in composition
        and "*users_,\n        *users_" in composition,
        "Composition root must separate request/background clients and inject request adapters",
        failures,
    )
    require(
        "argon2id_hash_encoded" in password_hasher
        and "argon2id_verify" in password_hasher
        and "RAND_bytes" in password_hasher,
        "Password hashing must use salted Argon2id",
        failures,
    )
    require(
        '"HS256"' in token_service
        and "CRYPTO_memcmp" in token_service
        and "EVP_DigestSign" in token_service
        and "RAND_bytes" in token_service,
        "JWT and opaque tokens must use OpenSSL-backed HS256 and secure randomness",
        failures,
    )
    require(
        "CREATE TABLE revoked_sessions" in auth_migration
        and "security_event" in auth_migration,
        "V4 must persist session-family revocation and authentication audit actions",
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
