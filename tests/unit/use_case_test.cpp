// Personal Finance Hub - Application Use Case Unit Tests
// Version: 1.0

#include "pfh/application/query/report_query_service.h"
#include "pfh/application/use_cases/account_query_use_cases.h"
#include "pfh/application/use_cases/create_transaction_use_case.h"
#include "pfh/application/use_cases/create_transfer_use_case.h"
#include "pfh/application/use_cases/delete_account_use_case.h"
#include "pfh/application/use_cases/delete_transaction_use_case.h"
#include "pfh/application/use_cases/refresh_exchange_rates_use_case.h"
#include "pfh/infrastructure/persistence/in_memory_account_repository.h"
#include "pfh/infrastructure/persistence/in_memory_category_repository.h"
#include "pfh/infrastructure/persistence/in_memory_exchange_rate_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include "pfh/infrastructure/persistence/in_memory_transaction_repository.h"
#include "pfh/infrastructure/persistence/in_memory_unit_of_work.h"
#include "pfh/infrastructure/persistence/in_memory_user_repository.h"
#include "test_support.h"
#include <gtest/gtest.h>
#include <memory>

using namespace pfh::domain;
using namespace pfh::application;
using namespace pfh::infrastructure;

namespace pfh::test {

class MockExchangeRateProvider final : public IExchangeRateProvider {
public:
    domain::RepositoryResult<std::vector<domain::ExchangeRate>> fetch_latest(
        const Currency& base,
        const std::vector<Currency>& targets) override {
        if (fail_) {
            return std::unexpected(RepositoryError::database("provider down"));
        }
        std::vector<ExchangeRate> rates;
        for (const auto& t : targets) {
            auto r = ExchangeRate::create(base, t, dec("2"), sample_time(), "Mock");
            if (!r) {
                return std::unexpected(RepositoryError::validation(r.error().message));
            }
            rates.push_back(*r);
        }
        return rates;
    }

    void set_fail(bool v) { fail_ = v; }

private:
    bool fail_ = false;
};

class UseCaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        store_ = std::make_unique<InMemoryStore>();
        uow_ = std::make_unique<InMemoryUnitOfWork>(*store_);
        user_repo_ = std::make_unique<InMemoryUserRepository>(*store_);
        pref_repo_ = std::make_unique<InMemoryUserPreferenceRepository>(*store_);
        tx_repo_ = std::make_unique<InMemoryTransactionRepository>(*store_);
        account_repo_ = std::make_unique<InMemoryAccountRepository>(*store_, *tx_repo_);
        rate_repo_ = std::make_unique<InMemoryExchangeRateRepository>(*store_);
        category_repo_ = std::make_unique<InMemoryCategoryRepository>(*store_);
        provider_ = std::make_unique<MockExchangeRateProvider>();
    }

    struct Seed {
        UserId user;
        AccountId cash;
        AccountId savings;
        AccountId cny_wallet;
    };

    Seed seed() {
        Seed s;
        auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto uid = user_repo_->create(tx, "alice", "hash", ccy("USD"));
            if (!uid) return std::unexpected(uid.error());
            s.user = *uid;

            Account cash(
                AccountId{}, s.user, "Cash", AccountType::Cash, "wallet", ccy("USD"));
            auto cash_id = account_repo_->save(tx, cash);
            if (!cash_id) return std::unexpected(cash_id.error());
            s.cash = *cash_id;

            Account savings(
                AccountId{}, s.user, "Savings", AccountType::Savings, "bank", ccy("USD"));
            auto sav_id = account_repo_->save(tx, savings);
            if (!sav_id) return std::unexpected(sav_id.error());
            s.savings = *sav_id;

            Account cny(
                AccountId{}, s.user, "CNY Wallet", AccountType::DigitalWallet, "wallet", ccy("CNY"));
            auto cny_id = account_repo_->save(tx, cny);
            if (!cny_id) return std::unexpected(cny_id.error());
            s.cny_wallet = *cny_id;
            return {};
        });
        EXPECT_TRUE(write.has_value()) << write.error().message;
        return s;
    }

    std::unique_ptr<InMemoryStore> store_;
    std::unique_ptr<IUnitOfWork> uow_;
    std::unique_ptr<IUserRepository> user_repo_;
    std::unique_ptr<IUserPreferenceRepository> pref_repo_;
    std::unique_ptr<ITransactionRepository> tx_repo_;
    std::unique_ptr<IAccountRepository> account_repo_;
    std::unique_ptr<IExchangeRateRepository> rate_repo_;
    std::unique_ptr<ICategoryRepository> category_repo_;
    std::unique_ptr<MockExchangeRateProvider> provider_;
};

TEST_F(UseCaseTest, CreateTransaction_WhenValidIncome_PersistsAndEmitsOutbox) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *tx_repo_, *uow_);

    CreateTransactionCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.type = TransactionType::Income;
    cmd.amount = "1000";
    cmd.currency_code = "USD";
    cmd.description = "Salary";
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->amount, "1000");
    EXPECT_EQ(result->type, TransactionType::Income);
    EXPECT_EQ(store_->outbox.size(), 1u);
    EXPECT_EQ(store_->outbox.front().event_name, "TransactionCreated");

    GetAccountBalanceUseCase balance_uc(*account_repo_);
    auto bal = balance_uc.execute(s.user, s.cash);
    ASSERT_TRUE(bal.has_value());
    EXPECT_EQ(bal->amount, "1000");
}

TEST_F(UseCaseTest, CreateTransaction_WhenWrongUserAccount_ReturnsNotFound) {
    auto s = seed();
    UserId other;
    auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto uid = user_repo_->create(tx, "bob", "hash", ccy("USD"));
        if (!uid) return std::unexpected(uid.error());
        other = *uid;
        return {};
    });
    ASSERT_TRUE(write.has_value());

    CreateTransactionUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cmd;
    cmd.user_id = other;
    cmd.account_id = s.cash; // belongs to alice
    cmd.type = TransactionType::Expense;
    cmd.amount = "10";
    cmd.currency_code = "USD";
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(UseCaseTest, CreateTransaction_WhenTransferType_ReturnsValidationError) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.type = TransactionType::Transfer;
    cmd.amount = "10";
    cmd.currency_code = "USD";
    cmd.occurred_at = sample_time();
    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
}

TEST_F(UseCaseTest, CreateTransaction_WhenCategoryBoardMismatch_ReturnsDomainRuleViolation) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.type = TransactionType::Income;
    cmd.amount = "100";
    cmd.currency_code = "USD";
    cmd.category_id = CategoryId(5);
    cmd.category_board = CategoryBoard::Expense; // wrong board for Income
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::DomainRuleViolation);
}

TEST_F(UseCaseTest, CreateTransaction_WhenCategoryWithoutBoard_ReturnsValidationError) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.type = TransactionType::Expense;
    cmd.amount = "100";
    cmd.currency_code = "USD";
    cmd.category_id = CategoryId(5);
    // category_board intentionally omitted
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
}

TEST_F(UseCaseTest, CreateTransaction_WhenCategoryBoardMatches_Succeeds) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.type = TransactionType::Expense;
    cmd.amount = "40";
    cmd.currency_code = "USD";
    cmd.category_id = CategoryId(7);
    cmd.category_board = CategoryBoard::Expense;
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    // Expense is stored with negative sign convention at the repository boundary.
    EXPECT_EQ(result->amount, "-40");
    EXPECT_EQ(result->type, TransactionType::Expense);
}

TEST_F(UseCaseTest, DeleteTransaction_WhenOwned_SoftDeletesAndEmitsOutbox) {
    auto s = seed();
    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.type = TransactionType::Income;
    cmd.amount = "50";
    cmd.currency_code = "USD";
    cmd.occurred_at = sample_time();
    auto created = create_uc.execute(cmd);
    ASSERT_TRUE(created.has_value());
    store_->outbox.clear();

    DeleteTransactionUseCase del_uc(*tx_repo_, *uow_);
    DeleteTransactionCommand dcmd;
    dcmd.user_id = s.user;
    dcmd.transaction_id = created->id;
    dcmd.deleted_at = sample_time();
    auto deleted = del_uc.execute(dcmd);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().message;
    EXPECT_EQ(store_->outbox.size(), 1u);
    EXPECT_EQ(store_->outbox.front().event_name, "TransactionDeleted");
}

TEST_F(UseCaseTest, CreateTransfer_WhenBothAmounts_PersistsGroupAndOutbox) {
    auto s = seed();
    // seed income first so cash has funds (not enforced yet, but realistic)
    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand income;
    income.user_id = s.user;
    income.account_id = s.cash;
    income.type = TransactionType::Income;
    income.amount = "500";
    income.currency_code = "USD";
    income.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(income).has_value());
    store_->outbox.clear();

    CreateTransferUseCase transfer_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand cmd;
    cmd.user_id = s.user;
    cmd.source_account_id = s.cash;
    cmd.target_account_id = s.savings;
    cmd.mode = TransferInputMode::BothAmounts;
    cmd.outgoing_amount = "100";
    cmd.incoming_amount = "100";
    cmd.description = "Move";
    cmd.occurred_at = sample_time();

    auto result = transfer_uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->transfer_group_id.is_valid());
    EXPECT_EQ(store_->transfer_groups.size(), 1u);
    EXPECT_EQ(store_->outbox.size(), 1u);
    EXPECT_EQ(store_->outbox.front().event_name, "TransferCompleted");
}

TEST_F(UseCaseTest, ReportQuery_WhenTransferExists_ExcludesFromCashFlow) {
    auto s = seed();
    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand income;
    income.user_id = s.user;
    income.account_id = s.cash;
    income.type = TransactionType::Income;
    income.amount = "1000";
    income.currency_code = "USD";
    income.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(income).has_value());

    CreateTransactionCommand expense;
    expense.user_id = s.user;
    expense.account_id = s.cash;
    expense.type = TransactionType::Expense;
    expense.amount = "200";
    expense.currency_code = "USD";
    expense.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(expense).has_value());

    CreateTransferUseCase transfer_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand tcmd;
    tcmd.user_id = s.user;
    tcmd.source_account_id = s.cash;
    tcmd.target_account_id = s.savings;
    tcmd.mode = TransferInputMode::BothAmounts;
    tcmd.outgoing_amount = "100";
    tcmd.incoming_amount = "100";
    tcmd.occurred_at = sample_time();
    ASSERT_TRUE(transfer_uc.execute(tcmd).has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto cf = reports.cash_flow(s.user);
    ASSERT_TRUE(cf.has_value()) << cf.error().message;
    EXPECT_EQ(cf->currency_code, "USD");
    EXPECT_EQ(cf->income_total, "1000");
    EXPECT_EQ(cf->expense_total, "200");
    EXPECT_EQ(cf->net_total, "800");
}

TEST_F(UseCaseTest, ReportQuery_WhenMultiCurrency_UsesExchangeRateForNetWorth) {
    auto s = seed();
    // USD income 100 on cash, CNY income 700 on cny wallet, rate USD->CNY 7
    // Wait: convert CNY balance to USD base: need CNY->USD rate or USD->CNY inverse.
    auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        // 1 USD = 7 CNY => 1 CNY = 1/7 USD
        auto r = rate_repo_->append(tx, rate("USD", "CNY", "7", 1000, "ECB"));
        if (!r) return std::unexpected(r.error());
        return {};
    });
    ASSERT_TRUE(write.has_value());

    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand usd_income;
    usd_income.user_id = s.user;
    usd_income.account_id = s.cash;
    usd_income.type = TransactionType::Income;
    usd_income.amount = "100";
    usd_income.currency_code = "USD";
    usd_income.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(usd_income).has_value());

    CreateTransactionCommand cny_income;
    cny_income.user_id = s.user;
    cny_income.account_id = s.cny_wallet;
    cny_income.type = TransactionType::Income;
    cny_income.amount = "700";
    cny_income.currency_code = "CNY";
    cny_income.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(cny_income).has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto nw = reports.net_worth(s.user);
    ASSERT_TRUE(nw.has_value()) << nw.error().message;
    EXPECT_EQ(nw->currency_code, "USD");
    // 100 USD + 700/7 = 100 + 100 = 200 USD
    EXPECT_EQ(nw->total, "200");
}

TEST_F(UseCaseTest, ReportQuery_WhenNonUsdPair_UsesUsdTriangulation) {
    // Base = EUR, holdings in CNY. No direct/reverse EUR<->CNY rate exists;
    // only USD->EUR and USD->CNY. Report must triangulate via USD.
    auto s = seed();

    // Set user base currency to EUR.
    auto pref_write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        UserPreference pref(s.user, ccy("EUR"));
        return pref_repo_->save(tx, pref);
    });
    ASSERT_TRUE(pref_write.has_value()) << pref_write.error().message;

    auto rate_write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        // 1 USD = 0.5 EUR, 1 USD = 8 CNY  => 1 CNY = 0.0625 EUR
        if (auto r = rate_repo_->append(tx, rate("USD", "EUR", "0.5", 1000, "ECB")); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = rate_repo_->append(tx, rate("USD", "CNY", "8", 1000, "ECB")); !r) {
            return std::unexpected(r.error());
        }
        return {};
    });
    ASSERT_TRUE(rate_write.has_value());

    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cny_income;
    cny_income.user_id = s.user;
    cny_income.account_id = s.cny_wallet;
    cny_income.type = TransactionType::Income;
    cny_income.amount = "800";
    cny_income.currency_code = "CNY";
    cny_income.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(cny_income).has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto nw = reports.net_worth(s.user);
    ASSERT_TRUE(nw.has_value()) << nw.error().message;
    EXPECT_EQ(nw->currency_code, "EUR");
    // CNY->EUR = (USD->EUR)/(USD->CNY) = 0.5/8 = 0.0625 ; 800 * 0.0625 = 50 EUR
    EXPECT_EQ(nw->total, "50");
}

TEST_F(UseCaseTest, ReportQuery_WhenNoRateAtAll_ReturnsError) {
    auto s = seed();
    // Holdings in CNY but base USD and NO rate present at all.
    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cny_income;
    cny_income.user_id = s.user;
    cny_income.account_id = s.cny_wallet;
    cny_income.type = TransactionType::Income;
    cny_income.amount = "700";
    cny_income.currency_code = "CNY";
    cny_income.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(cny_income).has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto nw = reports.net_worth(s.user);
    ASSERT_FALSE(nw.has_value());
    EXPECT_EQ(nw.error().code, ErrorCode::InvalidExchangeRate);
    // Must NOT leak SQL or use a default 0/1 rate.
}

TEST_F(UseCaseTest, RefreshExchangeRates_WhenProviderFails_DegradesWithoutWrite) {
    auto s = seed();
    (void)s;
    provider_->set_fail(true);
    RefreshExchangeRatesUseCase uc(*account_repo_, *rate_repo_, *provider_, *uow_);
    auto result = uc.execute(RefreshExchangeRatesCommand{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->degraded);
    EXPECT_EQ(result->appended_count, 0u);
    EXPECT_TRUE(store_->exchange_rates.empty());
    EXPECT_TRUE(store_->outbox.empty());
}

TEST_F(UseCaseTest, RefreshExchangeRates_WhenProviderOk_AppendsRatesAndOutbox) {
    auto s = seed();
    (void)s;
    RefreshExchangeRatesUseCase uc(*account_repo_, *rate_repo_, *provider_, *uow_);
    RefreshExchangeRatesCommand cmd;
    cmd.target_currency_codes = {"CNY", "EUR"};
    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->degraded);
    EXPECT_EQ(result->appended_count, 2u);
    EXPECT_EQ(store_->exchange_rates.size(), 2u);
    EXPECT_EQ(store_->outbox.size(), 1u);
    EXPECT_EQ(store_->outbox.front().event_name, "ExchangeRateRefreshed");
}

TEST_F(UseCaseTest, ErrorMapping_WhenDatabaseError_DoesNotLeakDetails) {
    // Force repo database error via nested transaction on unit of work.
    auto nested = uow_->execute_in_transaction([&](ITransactionContext&) -> RepositoryVoidResult {
        return uow_->execute_in_transaction([](ITransactionContext&) -> RepositoryVoidResult {
            return {};
        });
    });
    ASSERT_FALSE(nested.has_value());
    auto mapped = from_repository(nested.error());
    EXPECT_EQ(mapped.code, ErrorCode::InfrastructureFailure);
    EXPECT_EQ(mapped.message, "Database operation failed");
    EXPECT_TRUE(mapped.details.empty());
}

// ---- Fix 1: account archive round-trips through optimistic-locked save ----

TEST_F(UseCaseTest, AccountArchive_ThenSave_SucceedsAndBumpsVersionOnce) {
    auto s = seed();
    auto loaded = account_repo_->find_by_id(s.cash);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->version(), 1);

    // archive() must NOT touch version_, so the loaded version still matches
    // the stored version and the optimistic-lock save can succeed.
    Account acct = *loaded;
    acct.archive(sample_time());
    EXPECT_EQ(acct.version(), 1);
    EXPECT_TRUE(acct.is_archived());

    auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto r = account_repo_->save(tx, acct);
        if (!r) {
            return std::unexpected(r.error());
        }
        return {};
    });
    ASSERT_TRUE(write.has_value()) << write.error().message;

    auto after = account_repo_->find_by_id(s.cash);
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->is_archived());
    EXPECT_EQ(after->version(), 2); // repository owns the single increment
}

// ---- Fix 2: transfer resolves leg ids without a non-transactional re-read ----

TEST_F(UseCaseTest, CreateTransfer_ReturnsBothLegIds) {
    auto s = seed();
    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand cmd;
    cmd.user_id = s.user;
    cmd.source_account_id = s.cash;
    cmd.target_account_id = s.savings;
    cmd.mode = TransferInputMode::BothAmounts;
    cmd.outgoing_amount = "100";
    cmd.incoming_amount = "100";
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->transfer_group_id.is_valid());
    EXPECT_TRUE(result->outgoing_transaction_id.is_valid());
    EXPECT_TRUE(result->incoming_transaction_id.is_valid());
    EXPECT_NE(result->outgoing_transaction_id.value(),
              result->incoming_transaction_id.value());
}

TEST_F(UseCaseTest, CreateTransfer_WhenTargetArchived_Returns422) {
    auto s = seed();
    // Archive the savings account inside a proper optimistic-locked save.
    auto loaded = account_repo_->find_by_id(s.savings);
    ASSERT_TRUE(loaded.has_value());
    Account acct = *loaded;
    acct.archive(sample_time());
    auto arch = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto r = account_repo_->save(tx, acct);
        if (!r) return std::unexpected(r.error());
        return {};
    });
    ASSERT_TRUE(arch.has_value());

    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand cmd;
    cmd.user_id = s.user;
    cmd.source_account_id = s.cash;
    cmd.target_account_id = s.savings;
    cmd.mode = TransferInputMode::BothAmounts;
    cmd.outgoing_amount = "100";
    cmd.incoming_amount = "100";
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    // The precise application code survives the transaction abort.
    EXPECT_EQ(result.error().code, ErrorCode::ArchivedAccountOperation);
    // Nothing was written.
    EXPECT_TRUE(store_->transfer_groups.empty());
}

// ---- Fix 3: enriched report DTOs + dangerous account delete ----

TEST_F(UseCaseTest, Balance_ExposesLastTransactionIdAndUpdatedAt) {
    auto s = seed();
    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand income;
    income.user_id = s.user;
    income.account_id = s.cash;
    income.type = TransactionType::Income;
    income.amount = "500";
    income.currency_code = "USD";
    income.occurred_at = sample_time();
    auto created = create_uc.execute(income);
    ASSERT_TRUE(created.has_value());

    GetAccountBalanceUseCase balance_uc(*account_repo_);
    auto bal = balance_uc.execute(s.user, s.cash);
    ASSERT_TRUE(bal.has_value());
    EXPECT_EQ(bal->amount, "500");
    ASSERT_TRUE(bal->last_transaction_id.has_value());
    EXPECT_EQ(bal->last_transaction_id->value(), created->id.value());
}

TEST_F(UseCaseTest, NetWorth_SplitsAssetsAndLiabilities) {
    auto s = seed();
    // Add a Credit (liability) account and a USD->CNY rate (the seed's zero-
    // balance CNY wallet still needs a rate to convert into the USD base).
    AccountId credit;
    auto add = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        Account card(AccountId{}, s.user, "Card", AccountType::Credit, "credit", ccy("USD"));
        auto id = account_repo_->save(tx, card);
        if (!id) return std::unexpected(id.error());
        credit = *id;
        auto r = rate_repo_->append(tx, rate("USD", "CNY", "7", 1000, "ECB"));
        if (!r) return std::unexpected(r.error());
        return {};
    });
    ASSERT_TRUE(add.has_value());

    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand asset_income;
    asset_income.user_id = s.user;
    asset_income.account_id = s.cash;
    asset_income.type = TransactionType::Income;
    asset_income.amount = "1000";
    asset_income.currency_code = "USD";
    asset_income.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(asset_income).has_value());

    // Spend on the credit card => negative balance (a liability).
    CreateTransactionCommand card_expense;
    card_expense.user_id = s.user;
    card_expense.account_id = credit;
    card_expense.type = TransactionType::Expense;
    card_expense.amount = "300";
    card_expense.currency_code = "USD";
    card_expense.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(card_expense).has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto nw = reports.net_worth(s.user);
    ASSERT_TRUE(nw.has_value()) << nw.error().message;
    EXPECT_EQ(nw->total_assets, "1000");
    EXPECT_EQ(nw->total_liabilities, "-300");
    EXPECT_EQ(nw->total, "700");
}

TEST_F(UseCaseTest, Dashboard_ScopesIncomeExpenseToCurrentMonth) {
    auto s = seed();
    // Rate so the seed's zero-balance CNY wallet can convert into USD base.
    auto rw = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto r = rate_repo_->append(tx, rate("USD", "CNY", "7", 1000, "ECB"));
        if (!r) return std::unexpected(r.error());
        return {};
    });
    ASSERT_TRUE(rw.has_value());
    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);

    // "now" pinned to 2026-07-12; in-month income vs. prior-month expense.
    const auto now = std::chrono::system_clock::from_time_t(1752307200); // 2026-07-12
    const auto in_month = std::chrono::system_clock::from_time_t(1751500800); // 2026-07-03
    const auto last_month = std::chrono::system_clock::from_time_t(1749000000); // 2026-06-04

    CreateTransactionCommand cur;
    cur.user_id = s.user;
    cur.account_id = s.cash;
    cur.type = TransactionType::Income;
    cur.amount = "900";
    cur.currency_code = "USD";
    cur.occurred_at = in_month;
    ASSERT_TRUE(create_uc.execute(cur).has_value());

    CreateTransactionCommand old;
    old.user_id = s.user;
    old.account_id = s.cash;
    old.type = TransactionType::Expense;
    old.amount = "400";
    old.currency_code = "USD";
    old.occurred_at = last_month;
    ASSERT_TRUE(create_uc.execute(old).has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto dash = reports.dashboard_summary(s.user, now);
    ASSERT_TRUE(dash.has_value()) << dash.error().message;
    // Only the July income counts; the June expense is outside the window.
    EXPECT_EQ(dash->income_total, "900");
    EXPECT_EQ(dash->expense_total, "0");
    EXPECT_EQ(dash->account_count, 3u);
    EXPECT_FALSE(dash->asset_distribution.empty());
}

TEST_F(UseCaseTest, DeleteAccount_WhenInsufficientConfirmations_Rejected) {
    auto s = seed();
    DeleteAccountUseCase uc(*account_repo_, *tx_repo_, *uow_);
    DeleteAccountCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.confirmations = 1; // requires 3
    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
    // Account still present.
    EXPECT_TRUE(account_repo_->find_by_id(s.cash).has_value());
}

TEST_F(UseCaseTest, DeleteAccount_WhenConfirmed_PurgesAccountAndTransactions) {
    auto s = seed();
    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand income;
    income.user_id = s.user;
    income.account_id = s.cash;
    income.type = TransactionType::Income;
    income.amount = "250";
    income.currency_code = "USD";
    income.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(income).has_value());

    DeleteAccountUseCase uc(*account_repo_, *tx_repo_, *uow_);
    DeleteAccountCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.confirmations = DeleteAccountUseCase::kRequiredConfirmations;
    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_FALSE(account_repo_->find_by_id(s.cash).has_value());
    auto remaining = tx_repo_->find_by_account(s.cash);
    ASSERT_TRUE(remaining.has_value());
    EXPECT_TRUE(remaining->empty());
    EXPECT_EQ(store_->outbox.back().event_name, "AccountDangerouslyDeleted");
}

TEST_F(UseCaseTest, DeleteAccount_WhenOtherUsersAccount_ReturnsNotFound) {
    auto s = seed();
    UserId other;
    auto w = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto uid = user_repo_->create(tx, "bob", "hash", ccy("USD"));
        if (!uid) return std::unexpected(uid.error());
        other = *uid;
        return {};
    });
    ASSERT_TRUE(w.has_value());

    DeleteAccountUseCase uc(*account_repo_, *tx_repo_, *uow_);
    DeleteAccountCommand cmd;
    cmd.user_id = other;
    cmd.account_id = s.cash; // belongs to alice
    cmd.confirmations = DeleteAccountUseCase::kRequiredConfirmations;
    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
    EXPECT_TRUE(account_repo_->find_by_id(s.cash).has_value());
}

// ---- Review round 2 ----

// Item 2: deleting a transfer-involved account must purge BOTH legs and the
// transfer group, leaving no dangling half-transfer on the counterpart account.
TEST_F(UseCaseTest, DeleteAccount_WithTransfer_PurgesBothLegsAndGroup) {
    auto s = seed();
    CreateTransferUseCase transfer_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand tcmd;
    tcmd.user_id = s.user;
    tcmd.source_account_id = s.cash;
    tcmd.target_account_id = s.savings;
    tcmd.mode = TransferInputMode::BothAmounts;
    tcmd.outgoing_amount = "100";
    tcmd.incoming_amount = "100";
    tcmd.occurred_at = sample_time();
    ASSERT_TRUE(transfer_uc.execute(tcmd).has_value());
    ASSERT_EQ(store_->transfer_groups.size(), 1u);

    // Delete the SOURCE account; the incoming leg lives on savings.
    DeleteAccountUseCase uc(*account_repo_, *tx_repo_, *uow_);
    DeleteAccountCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.confirmations = DeleteAccountUseCase::kRequiredConfirmations;
    ASSERT_TRUE(uc.execute(cmd).has_value());

    // No transfer group and no leftover leg on the counterpart (savings).
    EXPECT_TRUE(store_->transfer_groups.empty());
    auto savings_txs = tx_repo_->find_by_account(s.savings);
    ASSERT_TRUE(savings_txs.has_value());
    EXPECT_TRUE(savings_txs->empty());
    // And no orphan transfer transactions remain user-wide.
    auto all = tx_repo_->find_by_user(s.user);
    ASSERT_TRUE(all.has_value());
    for (const auto& tx : *all) {
        EXPECT_NE(tx.type(), TransactionType::Transfer);
    }
}

// Item 3: a database failure from the rate repository must surface as
// InfrastructureFailure, not be swallowed into InvalidExchangeRate.
namespace {
class FailingRateRepository final : public IExchangeRateRepository {
public:
    domain::RepositoryResult<ExchangeRateId> append(
        ITransactionContext&, const ExchangeRate&) override {
        return std::unexpected(RepositoryError::database("append down"));
    }
    domain::RepositoryResult<ExchangeRate> find_latest(
        const Currency&, const Currency&) override {
        return std::unexpected(RepositoryError::database("db down"));
    }
    domain::RepositoryResult<ExchangeRate> find_historical(
        const Currency&, const Currency&,
        std::chrono::system_clock::time_point) override {
        return std::unexpected(RepositoryError::database("db down"));
    }
    domain::RepositoryResult<std::vector<ExchangeRate>> find_all_for_pair(
        const Currency&, const Currency&) override {
        return std::unexpected(RepositoryError::database("db down"));
    }
};
} // namespace

TEST_F(UseCaseTest, NetWorth_WhenRateRepositoryFails_ReturnsInfrastructureFailure) {
    auto s = seed();
    // CNY holding forces a conversion; the failing repo returns DatabaseError.
    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cny;
    cny.user_id = s.user;
    cny.account_id = s.cny_wallet;
    cny.type = TransactionType::Income;
    cny.amount = "700";
    cny.currency_code = "CNY";
    cny.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(cny).has_value());

    FailingRateRepository failing;
    ReportQueryService reports(*account_repo_, *tx_repo_, failing, *pref_repo_);
    auto nw = reports.net_worth(s.user);
    ASSERT_FALSE(nw.has_value());
    EXPECT_EQ(nw.error().code, ErrorCode::InfrastructureFailure);
    // Must NOT leak the DB message.
    EXPECT_EQ(nw.error().message, "Database operation failed");
}

// Item 4: a transaction stamped exactly at next-month 00:00 belongs to the
// NEXT period, not the current one.
TEST_F(UseCaseTest, Dashboard_ExcludesTransactionAtNextMonthBoundary) {
    auto s = seed();
    auto rw = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto r = rate_repo_->append(tx, rate("USD", "CNY", "7", 1000, "ECB"));
        if (!r) return std::unexpected(r.error());
        return {};
    });
    ASSERT_TRUE(rw.has_value());

    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    const auto now = std::chrono::system_clock::from_time_t(1752307200);      // 2025-07-12 08:00 UTC
    const auto next_month_start = std::chrono::system_clock::from_time_t(1754006400); // 2025-08-01 00:00 UTC

    CreateTransactionCommand at_boundary;
    at_boundary.user_id = s.user;
    at_boundary.account_id = s.cash;
    at_boundary.type = TransactionType::Income;
    at_boundary.amount = "500";
    at_boundary.currency_code = "USD";
    at_boundary.occurred_at = next_month_start;
    ASSERT_TRUE(create_uc.execute(at_boundary).has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto dash = reports.dashboard_summary(s.user, now);
    ASSERT_TRUE(dash.has_value()) << dash.error().message;
    // The Aug 1 00:00 income is outside July's [start, end) window.
    EXPECT_EQ(dash->income_total, "0");
}

// Item 6: domain service rejects same-account and non-positive transfers even
// when called directly (not just through the use case).
TEST_F(UseCaseTest, TransferDomainService_RejectsSameAccountTransfer) {
    auto built = TransferDomainService::build_from_both_amounts(
        money("100", "USD"), money("100", "USD"),
        AccountId(7), AccountId(7), // same account
        UserId(1), sample_time(), "self", TransferGroupId{});
    ASSERT_FALSE(built.has_value());
}

// Item 6: repository rejects a transfer whose leg currency mismatches the
// account currency.
TEST_F(UseCaseTest, SaveTransfer_WhenLegCurrencyMismatchesAccount_Rejected) {
    auto s = seed();
    // Build a same-currency USD transfer aggregate cash->savings, but the
    // aggregate is fine; instead craft a mismatch by targeting the CNY wallet
    // with a USD incoming leg via Mode 1 (rate USD->USD is identity but target
    // account is CNY). Simplest: cross aggregate cash(USD)->cny(CNY) with wrong
    // incoming currency is prevented at build; so we verify the repo guard by
    // routing an aggregate whose incoming account is cny but amount is USD.
    auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        // Mode 2 USD->USD but land incoming on the CNY wallet: build with both
        // USD amounts and source=cash(USD), target=cny(CNY-account).
        auto aggregate = TransferDomainService::build_from_both_amounts(
            money("100", "USD"), money("100", "USD"),
            s.cash, s.cny_wallet, s.user, sample_time(), "mismatch",
            TransferGroupId{});
        if (!aggregate) {
            // Domain layer allows this (it does not know account currencies);
            // if it ever rejects, treat as validation for the assertion below.
            return std::unexpected(RepositoryError::validation(aggregate.error().message));
        }
        auto saved = tx_repo_->save_transfer(tx, *aggregate);
        if (!saved) {
            return std::unexpected(saved.error());
        }
        return {};
    });
    ASSERT_FALSE(write.has_value());
    EXPECT_EQ(write.error().status, RepositoryStatus::ValidationError);
}

// Item 7: delete-transaction is race-safe; a second delete conflicts.
TEST_F(UseCaseTest, DeleteTransaction_SecondDelete_ReturnsConflict) {
    auto s = seed();
    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand expense;
    expense.user_id = s.user;
    expense.account_id = s.cash;
    expense.type = TransactionType::Expense;
    expense.amount = "40";
    expense.currency_code = "USD";
    expense.occurred_at = sample_time();
    auto created = create_uc.execute(expense);
    ASSERT_TRUE(created.has_value());

    DeleteTransactionUseCase del(*tx_repo_, *uow_);
    DeleteTransactionCommand dc;
    dc.user_id = s.user;
    dc.transaction_id = created->id;
    dc.deleted_at = sample_time();
    ASSERT_TRUE(del.execute(dc).has_value());

    auto second = del.execute(dc);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, ErrorCode::Conflict);
}

// Item 1: create-transaction builds its DTO from the persisted entity (id set)
// without any post-commit re-read.
TEST_F(UseCaseTest, CreateTransaction_ReturnsPersistedEntityWithId) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand income;
    income.user_id = s.user;
    income.account_id = s.cash;
    income.type = TransactionType::Income;
    income.amount = "1234";
    income.currency_code = "USD";
    income.occurred_at = sample_time();
    auto result = uc.execute(income);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->id.is_valid());
    EXPECT_EQ(result->amount, "1234");
}

// Item 8: asset distribution is aggregated by AccountType, not per-account.
TEST_F(UseCaseTest, Dashboard_AssetDistribution_AggregatesByAccountType) {
    auto s = seed();
    auto rw = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto r = rate_repo_->append(tx, rate("USD", "CNY", "7", 1000, "ECB"));
        if (!r) return std::unexpected(r.error());
        return {};
    });
    ASSERT_TRUE(rw.has_value());

    // Two Savings accounts + the seed's Cash and CNY DigitalWallet.
    AccountId savings2;
    auto add = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        Account s2(AccountId{}, s.user, "Savings2", AccountType::Savings, "bank", ccy("USD"));
        auto id = account_repo_->save(tx, s2);
        if (!id) return std::unexpected(id.error());
        savings2 = *id;
        return {};
    });
    ASSERT_TRUE(add.has_value());

    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    for (auto [acct, amt] : std::vector<std::pair<AccountId, std::string>>{
             {s.savings, "1000"}, {savings2, "500"}}) {
        CreateTransactionCommand c;
        c.user_id = s.user;
        c.account_id = acct;
        c.type = TransactionType::Income;
        c.amount = amt;
        c.currency_code = "USD";
        c.occurred_at = sample_time();
        ASSERT_TRUE(create_uc.execute(c).has_value());
    }

    const auto now = std::chrono::system_clock::from_time_t(1752307200);
    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto dash = reports.dashboard_summary(s.user, now);
    ASSERT_TRUE(dash.has_value()) << dash.error().message;

    // The two Savings accounts collapse into ONE "Savings" slice = 1500.
    int savings_slices = 0;
    for (const auto& slice : dash->asset_distribution) {
        if (slice.label == "Savings") {
            ++savings_slices;
            EXPECT_EQ(slice.amount, "1500");
        }
    }
    EXPECT_EQ(savings_slices, 1);
}

// ---- Group A: report follow-ups (timezone month window + root-category rollup) ----

// Dashboard month window honors the user's timezone: a transaction stamped at
// 2025-07-31 18:00 UTC is Aug 1 02:00 in Asia/Shanghai (UTC+8), so with a
// Shanghai preference it belongs to AUGUST, not July.
TEST_F(UseCaseTest, Dashboard_UsesUserTimezoneForMonthWindow) {
    auto s = seed();
    auto setup = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        // Shanghai timezone preference.
        UserPreference pref(s.user, ccy("USD"), "zh-CN", "Asia/Shanghai");
        if (auto r = pref_repo_->save(tx, pref); !r) return std::unexpected(r.error());
        if (auto r = rate_repo_->append(tx, rate("USD", "CNY", "7", 1000, "ECB")); !r) {
            return std::unexpected(r.error());
        }
        return {};
    });
    ASSERT_TRUE(setup.has_value());

    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    // 2025-07-31 18:00 UTC == 2025-08-01 02:00 Shanghai.
    const auto july31_evening_utc = std::chrono::system_clock::from_time_t(1753984800);
    CreateTransactionCommand income;
    income.user_id = s.user;
    income.account_id = s.cash;
    income.type = TransactionType::Income;
    income.amount = "600";
    income.currency_code = "USD";
    income.occurred_at = july31_evening_utc;
    ASSERT_TRUE(create_uc.execute(income).has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    // "now" = 2025-08-05 in Shanghai => August window; the txn falls IN August.
    const auto now_aug = std::chrono::system_clock::from_time_t(1754352000); // 2025-08-05 00:00 UTC
    auto dash_aug = reports.dashboard_summary(s.user, now_aug);
    ASSERT_TRUE(dash_aug.has_value()) << dash_aug.error().message;
    EXPECT_EQ(dash_aug->income_total, "600");

    // "now" = 2025-07-15 in Shanghai => July window; the txn is NOT in July.
    const auto now_jul = std::chrono::system_clock::from_time_t(1752537600); // 2025-07-15 00:00 UTC
    auto dash_jul = reports.dashboard_summary(s.user, now_jul);
    ASSERT_TRUE(dash_jul.has_value()) << dash_jul.error().message;
    EXPECT_EQ(dash_jul->income_total, "0");
}

// Top expense categories roll sub-categories up to their first-level parent
// when a category repository is supplied, and use the root's human name.
TEST_F(UseCaseTest, TopExpenseCategories_RollUpToRootCategory) {
    auto s = seed();
    CategoryId food_root;
    CategoryId food_dining; // child of food_root
    auto setup = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        // Rate so the seed's zero-balance CNY wallet can convert into USD base.
        if (auto r = rate_repo_->append(tx, rate("USD", "CNY", "7", 1000, "ECB")); !r) {
            return std::unexpected(r.error());
        }
        Category root(CategoryId{}, s.user, "Food", CategoryBoard::Expense);
        auto rid = category_repo_->save(tx, root);
        if (!rid) return std::unexpected(rid.error());
        food_root = *rid;

        Category child(CategoryId{}, s.user, "Dining", CategoryBoard::Expense, food_root);
        auto cid = category_repo_->save(tx, child);
        if (!cid) return std::unexpected(cid.error());
        food_dining = *cid;
        return {};
    });
    ASSERT_TRUE(setup.has_value()) << setup.error().message;

    CreateTransactionUseCase create_uc(*account_repo_, *tx_repo_, *uow_);
    // Two expenses: one on the child, one on the root — both roll to "Food".
    for (auto [cat, amt] : std::vector<std::pair<CategoryId, std::string>>{
             {food_dining, "30"}, {food_root, "20"}}) {
        CreateTransactionCommand e;
        e.user_id = s.user;
        e.account_id = s.cash;
        e.type = TransactionType::Expense;
        e.amount = amt;
        e.currency_code = "USD";
        e.category_id = cat;
        e.category_board = CategoryBoard::Expense;
        e.occurred_at = sample_time();
        ASSERT_TRUE(create_uc.execute(e).has_value());
    }

    ReportQueryService reports(
        *account_repo_, *tx_repo_, *rate_repo_, *pref_repo_, category_repo_.get());
    // sample_time() is 2024-06-25; use a matching "now" so the window covers it.
    const auto now = std::chrono::system_clock::from_time_t(1719400000); // 2024-06-26
    auto dash = reports.dashboard_summary(s.user, now);
    ASSERT_TRUE(dash.has_value()) << dash.error().message;

    // A single "Food" slice aggregating 30 + 20 = 50.
    ASSERT_EQ(dash->top_expense_categories.size(), 1u);
    const auto& slice = dash->top_expense_categories.front();
    EXPECT_EQ(slice.category_name, "Food");
    EXPECT_EQ(slice.amount, "50");
    ASSERT_TRUE(slice.category_id.has_value());
    EXPECT_EQ(slice.category_id->value(), food_root.value());
}

} // namespace pfh::test
