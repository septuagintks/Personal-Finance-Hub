// Personal Finance Hub - Application Use Case Unit Tests
// Version: 1.0

#include "pfh/application/query/report_query_service.h"
#include "pfh/application/use_cases/account_query_use_cases.h"
#include "pfh/application/use_cases/create_transaction_use_case.h"
#include "pfh/application/use_cases/create_transfer_use_case.h"
#include "pfh/application/use_cases/delete_transaction_use_case.h"
#include "pfh/application/use_cases/refresh_exchange_rates_use_case.h"
#include "pfh/infrastructure/persistence/in_memory_account_repository.h"
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

} // namespace pfh::test
