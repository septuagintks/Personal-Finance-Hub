#include "pfh/application/security/auth_models.h"
#include "pfh/application/events/outbox_publisher.h"
#include "pfh/application/events/supplemental_audit_handler.h"
#include "pfh/domain/events/simple_domain_event.h"
#include "pfh/domain/transfer_domain_service.h"
#include "pfh/infrastructure/persistence/account_repository_impl.h"
#include "pfh/infrastructure/persistence/audit_log_repository_impl.h"
#include "pfh/infrastructure/persistence/auth_session_repository_impl.h"
#include "pfh/infrastructure/persistence/category_repository_impl.h"
#include "pfh/infrastructure/persistence/drogon_unit_of_work.h"
#include "pfh/infrastructure/persistence/exchange_rate_repository_impl.h"
#include "pfh/infrastructure/persistence/postgres_job_lease_repository.h"
#include "pfh/infrastructure/persistence/postgres_outbox_repository.h"
#include "pfh/infrastructure/persistence/postgres_session_cleanup_repository.h"
#include "pfh/infrastructure/persistence/postgres_supplemental_audit_store.h"
#include "pfh/infrastructure/persistence/tag_repository_impl.h"
#include "pfh/infrastructure/persistence/transaction_repository_impl.h"
#include "pfh/infrastructure/persistence/user_preference_repository_impl.h"
#include "pfh/infrastructure/persistence/user_repository_impl.h"
#include "test_support.h"

#include <drogon/orm/DbClient.h>
#include <gtest/gtest.h>

#include <chrono>
#include <barrier>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace pfh::domain;
using namespace pfh::infrastructure;
using namespace pfh::test;
using namespace std::chrono_literals;

namespace {

struct DatabaseFixture {
    drogon::orm::DbClientPtr admin;
    drogon::orm::DbClientPtr request;
    drogon::orm::DbClientPtr background;

    void reset() const {
        admin->execSqlSync(R"SQL(
            DO $reset$
            DECLARE table_list text;
            BEGIN
                SELECT string_agg(format('%I.%I', schemaname, tablename), ', ')
                INTO table_list
                FROM pg_tables
                WHERE schemaname = 'public'
                  AND tablename NOT IN (
                      'flyway_schema_history',
                      'currencies',
                      'system_category_templates');
                IF table_list IS NOT NULL THEN
                    EXECUTE 'TRUNCATE TABLE ' || table_list ||
                            ' RESTART IDENTITY CASCADE';
                END IF;
            END
            $reset$;
        )SQL");
    }
};

std::unique_ptr<DatabaseFixture> database;

class MutableClock final : public pfh::application::IClock {
public:
    explicit MutableClock(std::chrono::system_clock::time_point now)
        : now_(now) {}

    [[nodiscard]] std::chrono::system_clock::time_point now() const override {
        return now_;
    }

    void advance(std::chrono::seconds duration) {
        now_ += duration;
    }

private:
    std::chrono::system_clock::time_point now_;
};

class AlwaysFailingEventBus final : public pfh::application::IEventBus {
public:
    void subscribe(std::shared_ptr<pfh::application::IEventHandler>) override {}

    [[nodiscard]] pfh::application::EventHandlingResult publish(
        const pfh::application::OutboxMessage&) override {
        return std::unexpected(pfh::application::EventHandlingError{
            "provider temporarily unavailable", "AlwaysFailingEventBus"});
    }
};

[[nodiscard]] std::string required_environment(std::string_view name) {
    const std::string key(name);
    const char* value = std::getenv(key.c_str());
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error(key + " is required");
    }
    return value;
}

[[nodiscard]] drogon::orm::DbClientPtr connect(
    std::string_view variable,
    std::size_t connections) {
    auto client = drogon::orm::DbClient::newPgClient(
        required_environment(variable), connections);
    if (!client) {
        throw std::runtime_error(std::string(variable) + " created no client");
    }
    client->execSqlSync("SELECT 1");
    return client;
}

template <typename T>
[[nodiscard]] T require_result(
    RepositoryResult<T> result,
    std::string_view operation) {
    if (!result) {
        throw std::runtime_error(
            std::string(operation) + ": " + result.error().message);
    }
    return std::move(*result);
}

void require_result(
    RepositoryVoidResult result,
    std::string_view operation) {
    if (!result) {
        throw std::runtime_error(
            std::string(operation) + ": " + result.error().message);
    }
}

[[nodiscard]] std::int64_t scalar_count(
    const drogon::orm::DbClientPtr& client,
    std::string_view sql) {
    const auto result = client->execSqlSync(std::string(sql));
    if (result.size() != 1) {
        throw std::runtime_error("count query returned an unexpected row count");
    }
    return result[0][0].as<std::int64_t>();
}

struct SeededUser {
    UserId user;
    AccountId cash;
    AccountId savings;
    AccountId cny;
};

[[nodiscard]] UserId create_user(
    std::string username,
    std::string_view currency = "USD") {
    UserRepositoryImpl users(database->request);
    DrogonUnitOfWork uow(database->request);
    UserId user_id;
    require_result(
        uow.execute_in_transaction(
            [&](ITransactionContext& tx) -> RepositoryVoidResult {
                auto created = users.create(
                    tx, username, "test-password-hash", ccy(currency));
                if (!created) {
                    return std::unexpected(created.error());
                }
                user_id = *created;
                return {};
            }),
        "create test user");
    return user_id;
}

[[nodiscard]] SeededUser seed_user(std::string username) {
    SeededUser ids;
    ids.user = create_user(std::move(username));
    AccountRepositoryImpl accounts(database->request, ids.user);
    DrogonUnitOfWork uow(database->request, ids.user);
    require_result(
        uow.execute_in_transaction(
            [&](ITransactionContext& tx) -> RepositoryVoidResult {
                const auto save = [&](std::string name,
                                      AccountType type,
                                      std::string subtype,
                                      std::string_view currency)
                    -> RepositoryResult<AccountId> {
                    return accounts.save(
                        tx,
                        Account(
                            AccountId{}, ids.user, std::move(name), type,
                            std::move(subtype), ccy(currency)));
                };
                auto cash = save("Cash Wallet", AccountType::Cash, "wallet", "USD");
                if (!cash) return std::unexpected(cash.error());
                ids.cash = *cash;
                auto savings = save("Savings", AccountType::Savings, "bank", "USD");
                if (!savings) return std::unexpected(savings.error());
                ids.savings = *savings;
                auto cny_account = save(
                    "CNY Wallet", AccountType::DigitalWallet, "wallet", "CNY");
                if (!cny_account) return std::unexpected(cny_account.error());
                ids.cny = *cny_account;
                return {};
            }),
        "seed test accounts");
    return ids;
}

class PostgreSQLIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        database->reset();
    }
};

TEST_F(PostgreSQLIntegrationTest, UnitOfWorkCommitsAndRollsBackBusinessWithOutbox) {
    UserRepositoryImpl users(database->request);

    DrogonUnitOfWork committed(database->request);
    UserId committed_user;
    auto commit = committed.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto created = users.create(tx, "committed", "hash", ccy("USD"));
            if (!created) return std::unexpected(created.error());
            committed_user = *created;
            committed.register_event(std::make_shared<SimpleDomainEvent>(
                "UserCreated", "User", committed_user.to_string(),
                R"({"source":"postgres-test"})"));
            auto read_your_writes = users.find_by_id(tx, committed_user);
            return read_your_writes
                ? RepositoryVoidResult{}
                : RepositoryVoidResult(std::unexpected(read_your_writes.error()));
        });
    ASSERT_TRUE(commit.has_value()) << commit.error().message;
    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM users"), 1);
    EXPECT_EQ(
        scalar_count(database->admin, "SELECT count(*) FROM domain_events_outbox"),
        1);

    DrogonUnitOfWork action_error(database->request);
    auto rolled_back = action_error.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto created = users.create(tx, "action-error", "hash", ccy("USD"));
            if (!created) return std::unexpected(created.error());
            action_error.register_event(std::make_shared<SimpleDomainEvent>(
                "UserCreated", "User", created->to_string(), "{}"));
            return std::unexpected(RepositoryError::validation("forced rollback"));
        });
    ASSERT_FALSE(rolled_back.has_value());

    DrogonUnitOfWork exception(database->request);
    auto exception_result = exception.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto created = users.create(tx, "exception", "hash", ccy("USD"));
            if (!created) return std::unexpected(created.error());
            throw std::runtime_error("forced action exception");
        });
    ASSERT_FALSE(exception_result.has_value());
    EXPECT_EQ(exception_result.error().status, RepositoryStatus::DatabaseError);

    DrogonUnitOfWork outbox_error(database->request);
    auto invalid_outbox = outbox_error.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto created = users.create(tx, "outbox-error", "hash", ccy("USD"));
            if (!created) return std::unexpected(created.error());
            outbox_error.register_event(std::make_shared<SimpleDomainEvent>(
                "UserCreated", "User", created->to_string(), "{"));
            return {};
        });
    ASSERT_FALSE(invalid_outbox.has_value());

    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM users"), 1);
    EXPECT_EQ(
        scalar_count(database->admin, "SELECT count(*) FROM domain_events_outbox"),
        1);
}

TEST_F(PostgreSQLIntegrationTest, RequestRoleRlsIsFailClosedAndPoolContextDoesNotLeak) {
    const auto alice = seed_user("alice");
    const auto bob = seed_user("bob");
    AccountRepositoryImpl alice_accounts(database->request, alice.user);
    AccountRepositoryImpl bob_accounts(database->request, bob.user);

    for (int iteration = 0; iteration < 12; ++iteration) {
        auto alice_rows = alice_accounts.find_active_by_user(alice.user);
        ASSERT_TRUE(alice_rows.has_value()) << alice_rows.error().message;
        EXPECT_EQ(alice_rows->size(), 3U);
        auto bob_rows = bob_accounts.find_active_by_user(bob.user);
        ASSERT_TRUE(bob_rows.has_value()) << bob_rows.error().message;
        EXPECT_EQ(bob_rows->size(), 3U);
    }

    auto hidden = bob_accounts.find_by_id_for_user(alice.cash, bob.user);
    ASSERT_FALSE(hidden.has_value());
    EXPECT_EQ(hidden.error().status, RepositoryStatus::NotFound);

    EXPECT_EQ(
        scalar_count(database->request, "SELECT count(*) FROM accounts"),
        0);
    EXPECT_EQ(
        scalar_count(database->background, "SELECT count(*) FROM accounts"),
        6);
    EXPECT_EQ(
        database->background->execSqlSync("SHOW default_transaction_read_only")[0][0]
            .as<std::string>(),
        "on");

    bool background_write_rejected = false;
    try {
        database->background->execSqlSync(
            "INSERT INTO accounts "
            "(user_id,name,type,subtype,category,currency_code) "
            "VALUES (1,'forbidden','cash','test','asset','USD')");
    } catch (const drogon::orm::DrogonDbException&) {
        background_write_rejected = true;
    }
    EXPECT_TRUE(background_write_rejected);
}

TEST_F(PostgreSQLIntegrationTest, CoreRepositoriesRoundTripAndEnforceTenantIsolation) {
    const auto alice = seed_user("alice");
    const auto bob = seed_user("bob");
    UserPreferenceRepositoryImpl preferences(database->request, alice.user);
    CategoryRepositoryImpl categories(database->request, alice.user);
    TagRepositoryImpl tags(database->request, alice.user);
    TransactionRepositoryImpl transactions(database->request, alice.user);
    AuditLogRepositoryImpl audits;
    AuthSessionRepositoryImpl sessions(database->request);
    DrogonUnitOfWork uow(database->request, alice.user);

    auto fallback = preferences.find_by_user(alice.user);
    ASSERT_TRUE(fallback.has_value()) << fallback.error().message;
    EXPECT_EQ(fallback->base_currency().code(), "USD");

    CategoryId root_id;
    CategoryId child_id;
    TagId tag_id;
    TransactionId transaction_id;
    const auto now = sample_time();
    const std::string token_hash(64, 'a');
    auto write = uow.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto preference = preferences.save(
                tx,
                UserPreference(
                    alice.user, ccy("CNY"), "en-US", "UTC", "YYYY-MM-DD",
                    "1,234.56", ThemeMode::Dark, HomePage::Reports,
                    ReportPeriod::CurrentYear));
            if (!preference) return preference;

            auto root = categories.save(
                tx,
                Category(
                    CategoryId{}, alice.user, "Food", CategoryBoard::Expense));
            if (!root) return std::unexpected(root.error());
            root_id = *root;
            auto child = categories.save(
                tx,
                Category(
                    CategoryId{}, alice.user, "Lunch", CategoryBoard::Expense,
                    root_id));
            if (!child) return std::unexpected(child.error());
            child_id = *child;

            auto tag = tags.save(tx, Tag(TagId{}, alice.user, "work"));
            if (!tag) return std::unexpected(tag.error());
            tag_id = *tag;

            auto saved = transactions.save_single(
                tx,
                Transaction(
                    TransactionId{}, alice.user, alice.cash,
                    money("12.34567890", "USD"), TransactionType::Expense,
                    now, "Lunch", child_id));
            if (!saved) return std::unexpected(saved.error());
            transaction_id = saved->id();
            auto related = tags.replace_transaction_tags(
                tx, transaction_id, alice.user, {tag_id});
            if (!related) return std::unexpected(related.error());

            auto audit = audits.append(
                tx,
                AuditLogEntry{
                    alice.user,
                    AuditActorType::User,
                    AuditAction::Create,
                    "Transaction",
                    transaction_id.to_string(),
                    "",
                    "{}",
                    R"({"source":"fixture"})",
                    now});
            if (!audit) return audit;

            return sessions.save_refresh_token(
                tx,
                pfh::application::RefreshTokenRecord{
                    0,
                    alice.user,
                    token_hash,
                    "session-a",
                    now + std::chrono::hours(2),
                    now,
                    std::nullopt});
        });
    ASSERT_TRUE(write.has_value()) << write.error().message;

    auto stored_preference = preferences.find_by_user(alice.user);
    ASSERT_TRUE(stored_preference.has_value()) << stored_preference.error().message;
    EXPECT_EQ(stored_preference->base_currency().code(), "CNY");
    EXPECT_EQ(stored_preference->theme(), ThemeMode::Dark);
    auto root = categories.resolve_root_id_for_user(child_id, alice.user);
    ASSERT_TRUE(root.has_value()) << root.error().message;
    EXPECT_EQ(*root, root_id);
    auto stored_tags = tags.find_by_transaction(transaction_id, alice.user);
    ASSERT_TRUE(stored_tags.has_value()) << stored_tags.error().message;
    ASSERT_EQ(stored_tags->size(), 1U);
    EXPECT_EQ(stored_tags->front().name(), "work");
    auto stored_transaction = transactions.find_by_id(transaction_id);
    ASSERT_TRUE(stored_transaction.has_value()) << stored_transaction.error().message;
    EXPECT_EQ(stored_transaction->amount().to_string(), "-12.3456789 USD");
    auto stored_token = sessions.find_refresh_token(token_hash);
    ASSERT_TRUE(stored_token.has_value()) << stored_token.error().message;
    EXPECT_EQ(stored_token->session_id, "session-a");
    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM audit_logs"), 1);

    CategoryRepositoryImpl bob_categories(database->request, bob.user);
    TagRepositoryImpl bob_tags(database->request, bob.user);
    TransactionRepositoryImpl bob_transactions(database->request, bob.user);
    EXPECT_FALSE(bob_categories.find_by_id_for_user(child_id, bob.user).has_value());
    EXPECT_FALSE(bob_tags.find_by_id_for_user(tag_id, bob.user).has_value());
    EXPECT_FALSE(bob_transactions.find_by_id(transaction_id).has_value());
}

TEST_F(PostgreSQLIntegrationTest, AccountOptimisticLockAndBalanceCacheTrackSourceVersion) {
    const auto ids = seed_user("alice");
    AccountRepositoryImpl accounts(database->request, ids.user);
    TransactionRepositoryImpl transactions(database->request, ids.user);

    auto original = accounts.find_by_id(ids.cash);
    ASSERT_TRUE(original.has_value()) << original.error().message;
    DrogonUnitOfWork update_uow(database->request, ids.user);
    auto first_update = update_uow.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = accounts.save(
                tx,
                Account(
                    original->id(), original->owner(), "Renamed Cash",
                    original->type(), original->subtype(), original->currency(),
                    original->description(), original->is_archived(),
                    original->archived_at(), original->created_at(), sample_time(),
                    original->version()));
            return saved
                ? RepositoryVoidResult{}
                : RepositoryVoidResult(std::unexpected(saved.error()));
        });
    ASSERT_TRUE(first_update.has_value()) << first_update.error().message;

    DrogonUnitOfWork stale_uow(database->request, ids.user);
    auto stale = stale_uow.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = accounts.save(
                tx,
                Account(
                    original->id(), original->owner(), "Stale Cash",
                    original->type(), original->subtype(), original->currency(),
                    original->description(), false, std::nullopt,
                    original->created_at(), sample_time(), original->version()));
            return saved
                ? RepositoryVoidResult{}
                : RepositoryVoidResult(std::unexpected(saved.error()));
        });
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().status, RepositoryStatus::Conflict);

    DrogonUnitOfWork transactions_uow(database->request, ids.user);
    auto write = transactions_uow.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            for (const auto& [amount, type] :
                 std::vector<std::pair<std::string, TransactionType>>{
                     {"1000", TransactionType::Income},
                     {"250", TransactionType::Expense}}) {
                auto saved = transactions.save_single(
                    tx,
                    Transaction(
                        TransactionId{}, ids.user, ids.cash,
                        money(amount, "USD"), type, sample_time()));
                if (!saved) return std::unexpected(saved.error());
            }
            return {};
        });
    ASSERT_TRUE(write.has_value()) << write.error().message;

    auto balance = accounts.balance_of(ids.cash);
    ASSERT_TRUE(balance.has_value()) << balance.error().message;
    EXPECT_EQ(balance->balance.to_string(), "750 USD");
    const auto cache = database->admin->execSqlSync(
        "SELECT source_version, last_transaction_id FROM account_balance_cache "
        "WHERE account_id=$1",
        ids.cash.value());
    ASSERT_EQ(cache.size(), 1U);
    EXPECT_EQ(cache[0][0].as<std::int64_t>(), 1);
    EXPECT_GT(cache[0][1].as<std::int64_t>(), 0);

    DrogonUnitOfWork invalidate_uow(database->request, ids.user);
    auto added = invalidate_uow.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = transactions.save_single(
                tx,
                Transaction(
                    TransactionId{}, ids.user, ids.cash, money("1", "USD"),
                    TransactionType::Income, sample_time()));
            return saved
                ? RepositoryVoidResult{}
                : RepositoryVoidResult(std::unexpected(saved.error()));
        });
    ASSERT_TRUE(added.has_value()) << added.error().message;
    EXPECT_EQ(
        scalar_count(
            database->admin,
            "SELECT count(*) FROM account_balance_cache"),
        0);
    auto rebuilt = accounts.balance_of(ids.cash);
    ASSERT_TRUE(rebuilt.has_value()) << rebuilt.error().message;
    EXPECT_EQ(rebuilt->balance.to_string(), "751 USD");
}

TEST_F(PostgreSQLIntegrationTest, TransfersPersistAllFeeSourcesAndRollbackAtomically) {
    const auto ids = seed_user("alice");
    TransactionRepositoryImpl transactions(database->request, ids.user);

    struct FeeCase {
        FeeSource source;
        AccountId account;
        const char* currency;
    };
    const std::vector<FeeCase> fees{
        {FeeSource::SourceAccount, ids.cash, "USD"},
        {FeeSource::TargetAccount, ids.savings, "USD"},
        {FeeSource::ThirdParty, ids.cny, "CNY"}};

    for (const auto& fee : fees) {
        DrogonUnitOfWork uow(database->request, ids.user);
        auto write = uow.execute_in_transaction(
            [&](ITransactionContext& tx) -> RepositoryVoidResult {
                auto aggregate = TransferDomainService::build_from_both_amounts(
                    money("100", "USD"), money("100", "USD"), ids.cash,
                    ids.savings, ids.user, sample_time(), "fee transfer",
                    TransferGroupId{},
                    TransferFee{
                        fee.source, fee.account, money("2", fee.currency)});
                if (!aggregate) {
                    return std::unexpected(RepositoryError::validation(
                        aggregate.error().message));
                }
                auto saved = transactions.save_transfer(tx, *aggregate);
                return saved
                    ? RepositoryVoidResult{}
                    : RepositoryVoidResult(std::unexpected(saved.error()));
            });
        ASSERT_TRUE(write.has_value()) << write.error().message;
    }

    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM transfer_groups"), 3);
    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM transactions"), 9);
    EXPECT_EQ(
        scalar_count(
            database->admin,
            "SELECT count(*) FROM transactions WHERE type='adjustment' "
            "AND amount=-2"),
        3);

    const auto before_groups =
        scalar_count(database->admin, "SELECT count(*) FROM transfer_groups");
    const auto before_transactions =
        scalar_count(database->admin, "SELECT count(*) FROM transactions");
    DrogonUnitOfWork rollback(database->request, ids.user);
    auto rolled_back = rollback.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto aggregate = TransferDomainService::build_from_both_amounts(
                money("10", "USD"), money("10", "USD"), ids.cash,
                ids.savings, ids.user, sample_time(), "rollback transfer",
                TransferGroupId{},
                TransferFee{FeeSource::ThirdParty, ids.cny, money("1", "CNY")});
            if (!aggregate) {
                return std::unexpected(RepositoryError::validation(
                    aggregate.error().message));
            }
            auto saved = transactions.save_transfer(tx, *aggregate);
            if (!saved) return std::unexpected(saved.error());
            return std::unexpected(RepositoryError::validation("forced rollback"));
        });
    ASSERT_FALSE(rolled_back.has_value());
    EXPECT_EQ(
        scalar_count(database->admin, "SELECT count(*) FROM transfer_groups"),
        before_groups);
    EXPECT_EQ(
        scalar_count(database->admin, "SELECT count(*) FROM transactions"),
        before_transactions);
}

TEST_F(PostgreSQLIntegrationTest, NumericBoundariesAndHistoricalRatesRoundTripExactly) {
    const auto ids = seed_user("alice");
    TransactionRepositoryImpl transactions(database->request, ids.user);
    ExchangeRateRepositoryImpl rates(database->request);
    DrogonUnitOfWork uow(database->request, ids.user);

    auto amounts = uow.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            for (const char* amount : {"0.00000001", "999999999999.99999999"}) {
                auto saved = transactions.save_single(
                    tx,
                    Transaction(
                        TransactionId{}, ids.user, ids.cash, money(amount, "USD"),
                        TransactionType::Income, sample_time()));
                if (!saved) return std::unexpected(saved.error());
            }
            return {};
        });
    ASSERT_TRUE(amounts.has_value()) << amounts.error().message;
    auto stored = transactions.find_by_user(ids.user);
    ASSERT_TRUE(stored.has_value()) << stored.error().message;
    ASSERT_EQ(stored->size(), 2U);
    EXPECT_EQ(stored->front().amount().to_string(), "0.00000001 USD");
    EXPECT_EQ(stored->back().amount().to_string(), "999999999999.99999999 USD");

    DrogonUnitOfWork invalid_uow(database->request, ids.user);
    auto invalid_amount = invalid_uow.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = transactions.save_single(
                tx,
                Transaction(
                    TransactionId{}, ids.user, ids.cash,
                    money("1.000000001", "USD"), TransactionType::Income,
                    sample_time()));
            return saved
                ? RepositoryVoidResult{}
                : RepositoryVoidResult(std::unexpected(saved.error()));
        });
    ASSERT_FALSE(invalid_amount.has_value());
    EXPECT_EQ(invalid_amount.error().status, RepositoryStatus::ValidationError);

    DrogonUnitOfWork rate_uow(database->request);
    auto rate_write = rate_uow.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            for (const auto& value : {
                     rate("USD", "CNY", "0.0000000001", 1000, "fixture"),
                     rate("USD", "CNY", "7.1234567890", 2000, "fixture"),
                     rate(
                         "USD", "CNY", "9999999999.9999999999", 3000,
                         "fixture")}) {
                auto appended = rates.append(tx, value);
                if (!appended) return std::unexpected(appended.error());
            }
            return {};
        });
    ASSERT_TRUE(rate_write.has_value()) << rate_write.error().message;
    auto historical = rates.find_historical(
        ccy("USD"), ccy("CNY"), time_at(2500));
    ASSERT_TRUE(historical.has_value()) << historical.error().message;
    EXPECT_EQ(historical->rate().to_string(), "7.123456789");
    auto latest = rates.find_latest(ccy("USD"), ccy("CNY"));
    ASSERT_TRUE(latest.has_value()) << latest.error().message;
    EXPECT_EQ(latest->rate().to_string(), "9999999999.9999999999");

    bool append_only_guard = false;
    try {
        database->admin->execSqlSync("UPDATE exchange_rates SET rate=1");
    } catch (const drogon::orm::DrogonDbException&) {
        append_only_guard = true;
    }
    EXPECT_TRUE(append_only_guard);
}

TEST_F(PostgreSQLIntegrationTest, ConcurrentOutboxClaimsAreDisjoint) {
    database->admin->execSqlSync(R"SQL(
        INSERT INTO domain_events_outbox (
            id, event_name, aggregate_type, aggregate_id, payload,
            next_retry_at, occurred_at)
        SELECT gen_random_uuid(), 'ConcurrentEvent', 'Fixture', value::text,
               '{}'::jsonb, NOW(), NOW()
        FROM generate_series(1, 20) AS value
    )SQL");

    PostgresOutboxRepository first_repository(database->request);
    PostgresOutboxRepository second_repository(database->request);
    RepositoryResult<pfh::application::OutboxClaimBatch> first =
        std::unexpected(RepositoryError::database("not started"));
    RepositoryResult<pfh::application::OutboxClaimBatch> second =
        std::unexpected(RepositoryError::database("not started"));
    std::barrier start(3);

    std::jthread first_worker([&] {
        start.arrive_and_wait();
        first = first_repository.claim_due(
            std::chrono::system_clock::time_point{}, 5min, 10, "worker-a");
    });
    std::jthread second_worker([&] {
        start.arrive_and_wait();
        second = second_repository.claim_due(
            std::chrono::system_clock::time_point{}, 5min, 10, "worker-b");
    });
    start.arrive_and_wait();
    first_worker.join();
    second_worker.join();

    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(second.has_value()) << second.error().message;
    ASSERT_EQ(first->claimed.size(), 10U);
    ASSERT_EQ(second->claimed.size(), 10U);
    std::set<std::string> ids;
    std::set<std::string> tokens;
    for (const auto& message : first->claimed) {
        ids.insert(message.id);
        tokens.insert(message.claim_token);
    }
    for (const auto& message : second->claimed) {
        ids.insert(message.id);
        tokens.insert(message.claim_token);
    }
    EXPECT_EQ(ids.size(), 20U);
    EXPECT_EQ(tokens.size(), 20U);
    EXPECT_EQ(
        scalar_count(
            database->admin,
            "SELECT count(*) FROM domain_events_outbox "
            "WHERE status='processing' AND claim_token IS NOT NULL"),
        20);
}

TEST_F(PostgreSQLIntegrationTest, StaleOutboxTokenCannotCompleteRecoveredLease) {
    constexpr std::string_view outbox_id =
        "10000000-0000-0000-0000-000000000001";
    database->admin->execSqlSync(R"SQL(
        INSERT INTO domain_events_outbox (
            id, event_name, payload, next_retry_at, occurred_at)
        VALUES (
            '10000000-0000-0000-0000-000000000001',
            'RecoverableEvent', '{}'::jsonb, NOW(), NOW())
    )SQL");
    PostgresOutboxRepository repository(database->request);

    auto first = repository.claim_due(
        std::chrono::system_clock::now(), 5min, 1, "crashed-worker");
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_EQ(first->claimed.size(), 1U);
    const auto stale_token = first->claimed.front().claim_token;
    database->admin->execSqlSync(
        "UPDATE domain_events_outbox SET locked_at=NOW()-INTERVAL '10 minutes' "
        "WHERE id=$1::uuid",
        std::string(outbox_id));

    auto recovered = repository.claim_due(
        std::chrono::system_clock::time_point::max(),
        5min,
        1,
        "replacement-worker");
    ASSERT_TRUE(recovered.has_value()) << recovered.error().message;
    ASSERT_EQ(recovered->claimed.size(), 1U);
    EXPECT_EQ(recovered->claimed.front().retry_count, 1);
    EXPECT_NE(recovered->claimed.front().claim_token, stale_token);

    auto stale_completion = repository.mark_published(
        outbox_id, stale_token, std::chrono::system_clock::time_point{});
    ASSERT_FALSE(stale_completion.has_value());
    EXPECT_EQ(stale_completion.error().status, RepositoryStatus::Conflict);
    auto current_completion = repository.mark_published(
        outbox_id,
        recovered->claimed.front().claim_token,
        std::chrono::system_clock::time_point{});
    ASSERT_TRUE(current_completion.has_value()) << current_completion.error().message;
}

TEST_F(PostgreSQLIntegrationTest, OutboxBackoffDeadLetterAndAuditAreDurable) {
    constexpr std::string_view outbox_id =
        "20000000-0000-0000-0000-000000000001";
    database->admin->execSqlSync(R"SQL(
        INSERT INTO domain_events_outbox (
            id, event_name, aggregate_type, aggregate_id, payload,
            next_retry_at, occurred_at)
        VALUES (
            '20000000-0000-0000-0000-000000000001',
            'ExchangeRateRefreshed', 'ExchangeRate', 'USD/CNY',
            '{"baseCurrency":"USD","targetCurrency":"CNY"}'::jsonb,
            NOW(), NOW())
    )SQL");

    PostgresOutboxRepository outbox(database->request);
    PostgresSupplementalAuditStore audit_store(database->request);
    pfh::application::SupplementalAuditHandler audit_handler(audit_store);
    AlwaysFailingEventBus event_bus;
    MutableClock clock(std::chrono::system_clock::time_point{});
    pfh::application::OutboxPublisher publisher(
        outbox,
        event_bus,
        clock,
        pfh::application::OutboxPublisherConfig{1, 5min, 10},
        &audit_handler);
    constexpr std::int64_t expected_delays[] = {60, 300, 900, 3600, 21600};

    for (std::size_t retry = 0; retry < std::size(expected_delays); ++retry) {
        auto result = publisher.run_once("failure-worker");
        ASSERT_TRUE(result.has_value()) << result.error().message;
        EXPECT_EQ(result->claimed, 1U);
        const auto state = database->admin->execSqlSync(
            "SELECT status::text, retry_count, "
            "EXTRACT(EPOCH FROM (next_retry_at-last_failed_at))::bigint "
            "FROM domain_events_outbox WHERE id=$1::uuid",
            std::string(outbox_id));
        ASSERT_EQ(state.size(), 1U);
        EXPECT_EQ(state[0][1].as<int>(), static_cast<int>(retry + 1));
        EXPECT_EQ(state[0][2].as<std::int64_t>(), expected_delays[retry]);
        if (retry + 1 < std::size(expected_delays)) {
            EXPECT_EQ(state[0][0].as<std::string>(), "failed");
            database->admin->execSqlSync(
                "UPDATE domain_events_outbox "
                "SET next_retry_at=NOW()-INTERVAL '1 second' "
                "WHERE id=$1::uuid",
                std::string(outbox_id));
        } else {
            EXPECT_EQ(state[0][0].as<std::string>(), "dead_letter");
            EXPECT_EQ(result->dead_lettered, 1U);
            EXPECT_EQ(result->dead_letters_audited, 1U);
        }
        clock.advance(1s);
    }

    EXPECT_EQ(
        scalar_count(database->admin, "SELECT count(*) FROM audit_logs"),
        1);
    EXPECT_EQ(
        scalar_count(
            database->admin,
            "SELECT count(*) FROM outbox_handler_receipts"),
        1);
    auto repeated = publisher.run_once("failure-worker");
    ASSERT_TRUE(repeated.has_value()) << repeated.error().message;
    EXPECT_EQ(repeated->dead_letters_audited, 0U);
    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM audit_logs"), 1);
}

TEST_F(PostgreSQLIntegrationTest, SupplementalAuditReceiptRollsBackWithInvalidAudit) {
    constexpr std::string_view outbox_id =
        "30000000-0000-0000-0000-000000000001";
    database->admin->execSqlSync(R"SQL(
        INSERT INTO domain_events_outbox (
            id, event_name, payload, status, retry_count, max_retry_count,
            next_retry_at, last_error, occurred_at)
        VALUES (
            '30000000-0000-0000-0000-000000000001',
            'DeadEvent', '{}'::jsonb, 'dead_letter', 5, 5,
            NOW(), 'delivery exhausted', NOW())
    )SQL");
    PostgresSupplementalAuditStore store(database->request);
    AuditLogEntry invalid{
        std::nullopt,
        AuditActorType::System,
        AuditAction::SecurityEvent,
        "DomainEventOutbox",
        std::string(outbox_id),
        "",
        "",
        "{",
        sample_time()};
    auto failed = store.append_once(outbox_id, "fixture-handler", invalid);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(
        scalar_count(
            database->admin,
            "SELECT count(*) FROM outbox_handler_receipts"),
        0);
    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM audit_logs"), 0);

    invalid.metadata_json = "{}";
    auto appended = store.append_once(outbox_id, "fixture-handler", invalid);
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    EXPECT_TRUE(*appended);
    auto duplicate = store.append_once(outbox_id, "fixture-handler", invalid);
    ASSERT_TRUE(duplicate.has_value()) << duplicate.error().message;
    EXPECT_FALSE(*duplicate);
    EXPECT_EQ(
        scalar_count(
            database->admin,
            "SELECT count(*) FROM outbox_handler_receipts"),
        1);
    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM audit_logs"), 1);
}

TEST_F(PostgreSQLIntegrationTest, JobLeaseAndSessionCleanupUseDatabaseClock) {
    PostgresJobLeaseRepository first(database->request);
    PostgresJobLeaseRepository second(database->request);
    const auto app_future = std::chrono::system_clock::time_point::max();
    const auto app_past = std::chrono::system_clock::time_point{};

    auto acquired = first.try_acquire("refresh-rates", "instance-a", app_past, 5min);
    ASSERT_TRUE(acquired.has_value()) << acquired.error().message;
    ASSERT_TRUE(acquired->has_value());
    const auto stale_token = (*acquired)->token;
    auto blocked = second.try_acquire(
        "refresh-rates", "instance-b", app_future, 5min);
    ASSERT_TRUE(blocked.has_value()) << blocked.error().message;
    EXPECT_FALSE(blocked->has_value());

    database->admin->execSqlSync(
        "UPDATE scheduled_job_leases "
        "SET lease_until=NOW()-INTERVAL '1 second' "
        "WHERE job_name='refresh-rates'");
    auto replacement = second.try_acquire(
        "refresh-rates", "instance-b", app_past, 5min);
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    ASSERT_TRUE(replacement->has_value());
    EXPECT_NE((*replacement)->token, stale_token);
    auto stale_release = first.release(
        "refresh-rates", "instance-a", stale_token, app_future);
    ASSERT_TRUE(stale_release.has_value()) << stale_release.error().message;
    EXPECT_FALSE(*stale_release);
    auto current_release = second.release(
        "refresh-rates", "instance-b", (*replacement)->token, app_past);
    ASSERT_TRUE(current_release.has_value()) << current_release.error().message;
    EXPECT_TRUE(*current_release);

    const auto user = create_user("cleanup-user");
    database->admin->execSqlSync(
        "INSERT INTO refresh_tokens "
        "(user_id,token_hash,session_id,expires_at) VALUES "
        "($1,$2,'expired-refresh',NOW()-INTERVAL '1 minute'),"
        "($1,$3,'future-refresh',NOW()+INTERVAL '1 hour')",
        user.value(), std::string(64, 'b'), std::string(64, 'c'));
    database->admin->execSqlSync(
        "INSERT INTO revoked_access_tokens (issuer,jti,expires_at) VALUES "
        "('fixture','expired-access',NOW()-INTERVAL '1 minute'),"
        "('fixture','future-access',NOW()+INTERVAL '1 hour')");
    database->admin->execSqlSync(
        "INSERT INTO revoked_sessions "
        "(session_id,user_id,expires_at,revoked_at,reason) VALUES "
        "('expired-session',$1,NOW()-INTERVAL '1 second',"
        "NOW()-INTERVAL '1 hour','fixture'),"
        "('future-session',$1,NOW()+INTERVAL '1 hour',NOW(),'fixture')",
        user.value());

    PostgresSessionCleanupRepository cleanup(database->request);
    auto cleaned = cleanup.delete_expired(app_future, 10);
    ASSERT_TRUE(cleaned.has_value()) << cleaned.error().message;
    EXPECT_EQ(cleaned->refresh_tokens_deleted, 1U);
    EXPECT_EQ(cleaned->revoked_access_tokens_deleted, 1U);
    EXPECT_EQ(cleaned->revoked_sessions_deleted, 1U);
    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM refresh_tokens"), 1);
    EXPECT_EQ(
        scalar_count(database->admin, "SELECT count(*) FROM revoked_access_tokens"),
        1);
    EXPECT_EQ(
        scalar_count(database->admin, "SELECT count(*) FROM revoked_sessions"),
        1);
}

TEST_F(PostgreSQLIntegrationTest, TransferThreeAccountLockConflictRollsBackWholeAggregate) {
    const auto ids = seed_user("alice");
    AccountRepositoryImpl accounts(database->request, ids.user);
    TransactionRepositoryImpl transactions(database->request, ids.user);
    std::promise<void> locked;
    std::promise<void> release;
    const auto released = release.get_future().share();
    RepositoryVoidResult holder_result =
        std::unexpected(RepositoryError::database("not started"));

    std::jthread holder([&] {
        DrogonUnitOfWork uow(database->request, ids.user);
        holder_result = uow.execute_in_transaction(
            [&](ITransactionContext& tx) -> RepositoryVoidResult {
                auto account = accounts.find_by_id_for_update(
                    tx, ids.cny, ids.user);
                if (!account) return std::unexpected(account.error());
                locked.set_value();
                released.wait();
                return {};
            });
    });
    locked.get_future().wait();

    DrogonUnitOfWork contender(database->request, ids.user);
    auto conflicted = contender.execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto aggregate = TransferDomainService::build_from_both_amounts(
                money("100", "USD"), money("100", "USD"), ids.cash,
                ids.savings, ids.user, sample_time(), "locked fee transfer",
                TransferGroupId{},
                TransferFee{FeeSource::ThirdParty, ids.cny, money("2", "CNY")});
            if (!aggregate) {
                return std::unexpected(RepositoryError::validation(
                    aggregate.error().message));
            }
            auto saved = transactions.save_transfer(tx, *aggregate);
            return saved
                ? RepositoryVoidResult{}
                : RepositoryVoidResult(std::unexpected(saved.error()));
        });
    release.set_value();
    holder.join();

    ASSERT_TRUE(holder_result.has_value()) << holder_result.error().message;
    ASSERT_FALSE(conflicted.has_value());
    EXPECT_EQ(conflicted.error().status, RepositoryStatus::DatabaseError);
    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM transfer_groups"), 0);
    EXPECT_EQ(scalar_count(database->admin, "SELECT count(*) FROM transactions"), 0);
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    try {
        database = std::make_unique<DatabaseFixture>(DatabaseFixture{
            connect("PFH_TEST_DB_ADMIN", 1),
            connect("PFH_TEST_DB_REQUEST", 6),
            connect("PFH_TEST_DB_BACKGROUND", 2)});

        const auto request_role = database->request->execSqlSync(
            "SELECT current_user, rolsuper, rolbypassrls FROM pg_roles "
            "WHERE rolname=current_user");
        const auto background_role = database->background->execSqlSync(
            "SELECT current_user, rolsuper, rolbypassrls FROM pg_roles "
            "WHERE rolname=current_user");
        if (request_role.size() != 1 || background_role.size() != 1 ||
            request_role[0][1].as<bool>() || request_role[0][2].as<bool>() ||
            background_role[0][1].as<bool>() ||
            !background_role[0][2].as<bool>()) {
            throw std::runtime_error("PostgreSQL test role boundary is invalid");
        }
    } catch (const std::exception& error) {
        std::cerr << "PostgreSQL integration setup failed: " << error.what() << '\n';
        return 2;
    }
    return RUN_ALL_TESTS();
}
