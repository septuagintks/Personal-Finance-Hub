// Personal Finance Hub - Application Use Case Unit Tests
// Version: 1.0

#include "pfh/application/query/report_query_service.h"
#include "pfh/application/use_cases/account_query_use_cases.h"
#include "pfh/application/use_cases/create_transaction_use_case.h"
#include "pfh/application/use_cases/create_transfer_use_case.h"
#include "pfh/application/use_cases/delete_account_use_case.h"
#include "pfh/application/use_cases/delete_transaction_use_case.h"
#include "pfh/application/use_cases/refresh_exchange_rates_use_case.h"
#include "pfh/application/use_cases/resource_use_cases.h"
#include "pfh/infrastructure/persistence/in_memory_account_repository.h"
#include "pfh/infrastructure/persistence/in_memory_audit_log_repository.h"
#include "pfh/infrastructure/persistence/in_memory_category_repository.h"
#include "pfh/infrastructure/persistence/in_memory_exchange_rate_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include "pfh/infrastructure/persistence/in_memory_tag_repository.h"
#include "pfh/infrastructure/persistence/in_memory_transaction_repository.h"
#include "pfh/infrastructure/persistence/in_memory_unit_of_work.h"
#include "pfh/infrastructure/persistence/in_memory_user_repository.h"
#include "test_support.h"
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

using namespace pfh::domain;
using namespace pfh::application;
using namespace pfh::infrastructure;

namespace pfh::test {

class MockExchangeRateProvider final : public IExchangeRateProvider {
public:
    [[nodiscard]] std::string_view provider_name() const noexcept override {
        return "Mock";
    }

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
        if (omit_last_ && !rates.empty()) {
            rates.pop_back();
        }
        if (duplicate_first_ && !rates.empty()) {
            rates.push_back(rates.front());
        }
        return rates;
    }

    void set_fail(bool v) { fail_ = v; }
    void set_omit_last(bool v) { omit_last_ = v; }
    void set_duplicate_first(bool v) { duplicate_first_ = v; }

private:
    bool fail_ = false;
    bool omit_last_ = false;
    bool duplicate_first_ = false;
};

class UseCaseClock final : public IClock {
public:
    [[nodiscard]] std::chrono::system_clock::time_point now() const override {
        return sample_time();
    }
};

class UseCaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        store_ = std::make_unique<InMemoryStore>();
        uow_ = std::make_unique<InMemoryUnitOfWork>(*store_);
        user_repo_ = std::make_unique<InMemoryUserRepository>(*store_);
        pref_repo_ = std::make_unique<InMemoryUserPreferenceRepository>(*store_);
        tx_repo_ = std::make_unique<InMemoryTransactionRepository>(*store_);
        auto account_repo =
            std::make_unique<InMemoryAccountRepository>(*store_, *tx_repo_);
        active_currency_query_ = account_repo.get();
        account_repo_ = std::move(account_repo);
        rate_repo_ = std::make_unique<InMemoryExchangeRateRepository>(*store_);
        category_repo_ = std::make_unique<InMemoryCategoryRepository>(*store_);
        audit_repo_ = std::make_unique<InMemoryAuditLogRepository>(*store_);
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
    IActiveCurrencyQuery* active_currency_query_ = nullptr;
    std::unique_ptr<IExchangeRateRepository> rate_repo_;
    std::unique_ptr<ICategoryRepository> category_repo_;
    std::unique_ptr<IAuditLogRepository> audit_repo_;
    std::unique_ptr<MockExchangeRateProvider> provider_;
    UseCaseClock clock_;
};

TEST_F(UseCaseTest, ApplicationBoundariesRejectInvalidIdsAndEnums) {
    InMemoryTagRepository tags(*store_);

    EXPECT_FALSE(ListAccountsUseCase(*account_repo_).execute(UserId{}).has_value());
    EXPECT_FALSE(GetAccountBalanceUseCase(*account_repo_)
                     .execute(UserId{}, AccountId{})
                     .has_value());
    EXPECT_FALSE(ListCategoriesUseCase(*category_repo_)
                     .execute(UserId{}, std::nullopt)
                     .has_value());
    EXPECT_FALSE(ListTagsUseCase(tags).execute(UserId{}).has_value());
    EXPECT_FALSE(GetUserPreferenceUseCase(*pref_repo_).execute(UserId{}).has_value());

    const auto seeded = seed();
    EXPECT_FALSE(ListCategoriesUseCase(*category_repo_)
                     .execute(seeded.user, static_cast<CategoryBoard>(999))
                     .has_value());

    ReportQueryService reports(
        *account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    EXPECT_FALSE(reports.net_worth(UserId{}).has_value());

    CreateTransactionCommand transaction;
    transaction.user_id = seeded.user;
    transaction.account_id = seeded.cash;
    transaction.type = static_cast<TransactionType>(999);
    transaction.amount = "1";
    transaction.currency_code = "USD";
    auto invalid_transaction = CreateTransactionUseCase(
        *account_repo_, *category_repo_, *tx_repo_, *uow_).execute(transaction);
    ASSERT_FALSE(invalid_transaction.has_value());
    EXPECT_EQ(invalid_transaction.error().code, ErrorCode::ValidationError);

    CreateAccountCommand account;
    account.user_id = seeded.user;
    account.name = "Invalid enum";
    account.type = static_cast<AccountType>(999);
    account.subtype = "invalid";
    account.currency_code = "USD";
    auto invalid_account = CreateAccountUseCase(
        *account_repo_, *audit_repo_, *uow_).execute(account);
    ASSERT_FALSE(invalid_account.has_value());
    EXPECT_EQ(invalid_account.error().code, ErrorCode::ValidationError);

    CreateCategoryCommand category;
    category.user_id = seeded.user;
    category.board = static_cast<CategoryBoard>(999);
    category.name = "Invalid enum";
    auto invalid_category = CreateCategoryUseCase(
        *category_repo_, *pref_repo_, *audit_repo_, *uow_).execute(category);
    ASSERT_FALSE(invalid_category.has_value());
    EXPECT_EQ(invalid_category.error().code, ErrorCode::ValidationError);

    UpdateUserPreferenceCommand preference;
    preference.user_id = seeded.user;
    preference.base_currency = "USD";
    preference.locale = "en-US";
    preference.timezone = "UTC";
    preference.date_format = "YYYY-MM-DD";
    preference.number_format = "1,234.56";
    preference.theme = static_cast<ThemeMode>(999);
    auto invalid_preference = UpdateUserPreferenceUseCase(
        *pref_repo_, *audit_repo_, *uow_).execute(preference);
    ASSERT_FALSE(invalid_preference.has_value());
    EXPECT_EQ(invalid_preference.error().code, ErrorCode::ValidationError);
}

TEST_F(UseCaseTest, CreateTransaction_WhenValidIncome_PersistsAndEmitsOutbox) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);

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

    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);
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
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);
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
    CategoryId category_id;
    auto setup = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = category_repo_->save(
                tx, Category(CategoryId{}, s.user, "Expense", CategoryBoard::Expense));
            if (!saved) return std::unexpected(saved.error());
            category_id = *saved;
            return {};
        });
    ASSERT_TRUE(setup.has_value());
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.type = TransactionType::Income;
    cmd.amount = "100";
    cmd.currency_code = "USD";
    cmd.category_id = category_id;
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::DomainRuleViolation);
}

TEST_F(UseCaseTest, CreateTransaction_WhenCategoryDoesNotExist_ReturnsNotFound) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.type = TransactionType::Expense;
    cmd.amount = "100";
    cmd.currency_code = "USD";
    cmd.category_id = CategoryId(5);
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(UseCaseTest, CreateTransaction_WhenCategoryBoardMatches_Succeeds) {
    auto s = seed();
    CategoryId category_id;
    auto setup = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = category_repo_->save(
                tx, Category(CategoryId{}, s.user, "Expense", CategoryBoard::Expense));
            if (!saved) return std::unexpected(saved.error());
            category_id = *saved;
            return {};
        });
    ASSERT_TRUE(setup.has_value());
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.type = TransactionType::Expense;
    cmd.amount = "40";
    cmd.currency_code = "USD";
    cmd.category_id = category_id;
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    // Expense is stored with negative sign convention at the repository boundary.
    EXPECT_EQ(result->amount, "-40");
    EXPECT_EQ(result->type, TransactionType::Expense);
}

TEST_F(UseCaseTest, CreateTransaction_WhenCategoryBelongsToOtherUser_ReturnsNotFound) {
    auto s = seed();
    CategoryId foreign_category;
    auto setup = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto other = user_repo_->create(tx, "category-owner", "hash", ccy("USD"));
            if (!other) return std::unexpected(other.error());
            auto category = category_repo_->save(
                tx, Category(CategoryId{}, *other, "Private", CategoryBoard::Expense));
            if (!category) return std::unexpected(category.error());
            foreign_category = *category;
            return {};
        });
    ASSERT_TRUE(setup.has_value());

    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand cmd;
    cmd.user_id = s.user;
    cmd.account_id = s.cash;
    cmd.type = TransactionType::Expense;
    cmd.amount = "10";
    cmd.currency_code = "USD";
    cmd.category_id = foreign_category;
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(UseCaseTest, DeleteTransaction_WhenOwned_SoftDeletesAndEmitsOutbox) {
    auto s = seed();
    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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

    InMemoryUnitOfWork request_uow(*store_, s.user);
    DeleteTransactionUseCase del_uc(*tx_repo_, *audit_repo_, request_uow);
    DeleteTransactionCommand dcmd;
    dcmd.user_id = s.user;
    dcmd.transaction_id = created->id;
    dcmd.deleted_at = sample_time();
    auto deleted = del_uc.execute(dcmd);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().message;
    ASSERT_EQ(store_->audit_logs.size(), 1u);
    EXPECT_EQ(store_->audit_logs.front().action, AuditAction::Delete);
    EXPECT_EQ(store_->audit_logs.front().resource_type, "Transaction");
    EXPECT_EQ(store_->audit_logs.front().resource_id, created->id.to_string());
    EXPECT_EQ(store_->outbox.size(), 1u);
    EXPECT_EQ(store_->outbox.front().event_name, "TransactionDeleted");
}

TEST_F(UseCaseTest, CreateTransfer_WhenBothAmounts_PersistsGroupAndOutbox) {
    auto s = seed();
    // seed income first so cash has funds (not enforced yet, but realistic)
    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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

TEST_F(UseCaseTest, CreateTransfer_WhenSourceFee_PersistsGroupedAdjustment) {
    auto s = seed();
    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand cmd;
    cmd.user_id = s.user;
    cmd.source_account_id = s.cash;
    cmd.target_account_id = s.savings;
    cmd.mode = TransferInputMode::BothAmounts;
    cmd.outgoing_amount = "100";
    cmd.incoming_amount = "100";
    cmd.fee_amount = "2";
    cmd.fee_source = FeeSource::SourceAccount;
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->fee_amount, std::optional<std::string>("2"));

    auto transactions = tx_repo_->find_by_user(s.user);
    ASSERT_TRUE(transactions.has_value());
    ASSERT_EQ(transactions->size(), 3u);
    std::size_t transfer_count = 0;
    std::size_t adjustment_count = 0;
    for (const auto& transaction : *transactions) {
        ASSERT_TRUE(transaction.transfer_group_id().has_value());
        EXPECT_EQ(*transaction.transfer_group_id(), result->transfer_group_id);
        if (transaction.type() == TransactionType::Transfer) {
            ++transfer_count;
        } else if (transaction.type() == TransactionType::Adjustment) {
            ++adjustment_count;
            EXPECT_EQ(transaction.account_id(), s.cash);
            EXPECT_EQ(transaction.amount().to_string(), "-2 USD");
        }
    }
    EXPECT_EQ(transfer_count, 2u);
    EXPECT_EQ(adjustment_count, 1u);

    auto source_balance = account_repo_->balance_of(s.cash);
    auto target_balance = account_repo_->balance_of(s.savings);
    ASSERT_TRUE(source_balance.has_value());
    ASSERT_TRUE(target_balance.has_value());
    EXPECT_EQ(source_balance->balance.to_string(), "-102 USD");
    EXPECT_EQ(target_balance->balance.to_string(), "100 USD");

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto cash_flow = reports.cash_flow(s.user);
    ASSERT_TRUE(cash_flow.has_value()) << cash_flow.error().message;
    EXPECT_EQ(cash_flow->income_total, "0");
    EXPECT_EQ(cash_flow->expense_total, "2");
    EXPECT_EQ(cash_flow->net_total, "-2");
}

TEST_F(UseCaseTest, CreateTransfer_WhenTargetFee_DeductsFromTargetBalance) {
    auto s = seed();
    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand cmd;
    cmd.user_id = s.user;
    cmd.source_account_id = s.cash;
    cmd.target_account_id = s.savings;
    cmd.mode = TransferInputMode::BothAmounts;
    cmd.outgoing_amount = "100";
    cmd.incoming_amount = "100";
    cmd.fee_amount = "3";
    cmd.fee_source = FeeSource::TargetAccount;
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    auto source_balance = account_repo_->balance_of(s.cash);
    auto target_balance = account_repo_->balance_of(s.savings);
    ASSERT_TRUE(source_balance.has_value());
    ASSERT_TRUE(target_balance.has_value());
    EXPECT_EQ(source_balance->balance.to_string(), "-100 USD");
    EXPECT_EQ(target_balance->balance.to_string(), "97 USD");
}

TEST_F(UseCaseTest, CreateTransfer_WhenThirdPartyFee_DeductsInThirdAccountCurrency) {
    auto s = seed();
    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand cmd;
    cmd.user_id = s.user;
    cmd.source_account_id = s.cash;
    cmd.target_account_id = s.savings;
    cmd.mode = TransferInputMode::BothAmounts;
    cmd.outgoing_amount = "100";
    cmd.incoming_amount = "100";
    cmd.fee_amount = "5";
    cmd.fee_source = FeeSource::ThirdParty;
    cmd.fee_account_id = s.cny_wallet;
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    auto fee_balance = account_repo_->balance_of(s.cny_wallet);
    ASSERT_TRUE(fee_balance.has_value());
    EXPECT_EQ(fee_balance->balance.to_string(), "-5 CNY");
}

TEST_F(UseCaseTest, CreateTransfer_WhenFeeFieldsAreInconsistent_RejectsWithoutWrites) {
    auto s = seed();
    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand base;
    base.user_id = s.user;
    base.source_account_id = s.cash;
    base.target_account_id = s.savings;
    base.mode = TransferInputMode::BothAmounts;
    base.outgoing_amount = "100";
    base.incoming_amount = "100";
    base.occurred_at = sample_time();

    auto amount_without_source = base;
    amount_without_source.fee_amount = "2";
    auto first = uc.execute(amount_without_source);
    ASSERT_FALSE(first.has_value());
    EXPECT_EQ(first.error().code, ErrorCode::ValidationError);

    auto source_without_amount = base;
    source_without_amount.fee_source = FeeSource::SourceAccount;
    auto second = uc.execute(source_without_amount);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, ErrorCode::ValidationError);

    auto source_with_third_party_id = base;
    source_with_third_party_id.fee_amount = "2";
    source_with_third_party_id.fee_source = FeeSource::SourceAccount;
    source_with_third_party_id.fee_account_id = s.cny_wallet;
    auto third = uc.execute(source_with_third_party_id);
    ASSERT_FALSE(third.has_value());
    EXPECT_EQ(third.error().code, ErrorCode::ValidationError);

    auto third_party_without_id = base;
    third_party_without_id.fee_amount = "2";
    third_party_without_id.fee_source = FeeSource::ThirdParty;
    auto fourth = uc.execute(third_party_without_id);
    ASSERT_FALSE(fourth.has_value());
    EXPECT_EQ(fourth.error().code, ErrorCode::ValidationError);

    EXPECT_TRUE(store_->transactions.empty());
    EXPECT_TRUE(store_->transfer_groups.empty());
    EXPECT_TRUE(store_->outbox.empty());
}

TEST_F(UseCaseTest, CreateTransfer_WhenFeeAmountIsInvalid_RejectsWithoutWrites) {
    auto s = seed();
    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);

    for (const auto* invalid_fee : {"0", "-1", "1.123456789"}) {
        CreateTransferCommand cmd;
        cmd.user_id = s.user;
        cmd.source_account_id = s.cash;
        cmd.target_account_id = s.savings;
        cmd.mode = TransferInputMode::BothAmounts;
        cmd.outgoing_amount = "100";
        cmd.incoming_amount = "100";
        cmd.fee_amount = invalid_fee;
        cmd.fee_source = FeeSource::SourceAccount;
        cmd.occurred_at = sample_time();

        auto result = uc.execute(cmd);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
    }

    EXPECT_TRUE(store_->transactions.empty());
    EXPECT_TRUE(store_->transfer_groups.empty());
    EXPECT_TRUE(store_->outbox.empty());
}

TEST_F(UseCaseTest, CreateTransfer_WhenThirdPartyFeeAccountIsForeign_ReturnsNotFound) {
    auto s = seed();
    AccountId foreign_account;
    auto setup = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto other = user_repo_->create(tx, "fee-owner", "hash", ccy("USD"));
            if (!other) return std::unexpected(other.error());
            auto saved = account_repo_->save(
                tx,
                Account(AccountId{}, *other, "Fee", AccountType::Cash, "wallet", ccy("USD")));
            if (!saved) return std::unexpected(saved.error());
            foreign_account = *saved;
            return {};
        });
    ASSERT_TRUE(setup.has_value());

    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand cmd;
    cmd.user_id = s.user;
    cmd.source_account_id = s.cash;
    cmd.target_account_id = s.savings;
    cmd.mode = TransferInputMode::BothAmounts;
    cmd.outgoing_amount = "100";
    cmd.incoming_amount = "100";
    cmd.fee_amount = "2";
    cmd.fee_source = FeeSource::ThirdParty;
    cmd.fee_account_id = foreign_account;
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
    EXPECT_TRUE(store_->transactions.empty());
    EXPECT_TRUE(store_->transfer_groups.empty());
}

TEST_F(UseCaseTest, CreateTransfer_WhenThirdPartyFeeAccountIsArchived_Returns422) {
    auto s = seed();
    auto loaded = account_repo_->find_by_id(s.cny_wallet);
    ASSERT_TRUE(loaded.has_value());
    Account archived = *loaded;
    archived.archive(sample_time());
    auto saved = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto result = account_repo_->save(tx, archived);
            return result ? RepositoryVoidResult{} : std::unexpected(result.error());
        });
    ASSERT_TRUE(saved.has_value());

    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand cmd;
    cmd.user_id = s.user;
    cmd.source_account_id = s.cash;
    cmd.target_account_id = s.savings;
    cmd.mode = TransferInputMode::BothAmounts;
    cmd.outgoing_amount = "100";
    cmd.incoming_amount = "100";
    cmd.fee_amount = "2";
    cmd.fee_source = FeeSource::ThirdParty;
    cmd.fee_account_id = s.cny_wallet;
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ArchivedAccountOperation);
    EXPECT_TRUE(store_->transactions.empty());
    EXPECT_TRUE(store_->transfer_groups.empty());
}

TEST_F(UseCaseTest, CreateTransfer_WhenModeContainsDerivedField_ReturnsValidationError) {
    auto s = seed();
    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand cmd;
    cmd.user_id = s.user;
    cmd.source_account_id = s.cash;
    cmd.target_account_id = s.cny_wallet;
    cmd.mode = TransferInputMode::OutgoingAndRate;
    cmd.outgoing_amount = "100";
    cmd.incoming_amount = "718";
    cmd.rate = "7.18";
    cmd.occurred_at = sample_time();

    auto result = uc.execute(cmd);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
    EXPECT_TRUE(store_->transactions.empty());
}

TEST_F(UseCaseTest, ReportQuery_WhenTransferExists_ExcludesFromCashFlow) {
    auto s = seed();
    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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

    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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

    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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
    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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
    RefreshExchangeRatesUseCase uc(
        *active_currency_query_, *rate_repo_, *provider_, *uow_, clock_);
    auto result = uc.execute(RefreshExchangeRatesCommand{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->degraded);
    EXPECT_EQ(result->appended_count, 0u);
    // No rate rows are written on degradation.
    EXPECT_TRUE(store_->exchange_rates.empty());
    // A degradation/alert event IS recorded (no historical rate seeded => the
    // event flags the hard-outage case).
    ASSERT_EQ(store_->outbox.size(), 1u);
    const auto& rec = store_->outbox.front();
    EXPECT_EQ(rec.event_name, "ExchangeRateRefreshFailed");
    EXPECT_EQ(rec.occurred_at, sample_time());
    EXPECT_NE(rec.payload_json.find("\"provider\":\"Mock\""), std::string::npos);
    EXPECT_NE(rec.payload_json.find("\"historicalAvailable\":false"), std::string::npos);
}

TEST_F(UseCaseTest, RefreshExchangeRates_WhenProviderFailsButHistoryExists_FlagsFallback) {
    auto s = seed();
    (void)s; // accounts seed the active-currency set (CNY) used as refresh targets
    // Seed a historical USD->CNY rate so the degradation has a usable fallback.
    auto rw = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto r = rate_repo_->append(tx, rate("USD", "CNY", "7", 1000, "ECB"));
        if (!r) return std::unexpected(r.error());
        return {};
    });
    ASSERT_TRUE(rw.has_value());
    const auto outbox_before = store_->outbox.size();

    provider_->set_fail(true);
    RefreshExchangeRatesUseCase uc(
        *active_currency_query_, *rate_repo_, *provider_, *uow_, clock_);
    auto result = uc.execute(RefreshExchangeRatesCommand{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->degraded);
    ASSERT_EQ(store_->outbox.size(), outbox_before + 1);
    const auto& rec = store_->outbox.back();
    EXPECT_EQ(rec.event_name, "ExchangeRateRefreshFailed");
    EXPECT_NE(rec.payload_json.find("\"historicalAvailable\":true"), std::string::npos);
}

TEST_F(UseCaseTest, RefreshExchangeRates_WhenOnlySomeHistoryExists_FlagsNoFallback) {
    auto s = seed();
    (void)s;
    auto rw = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto r = rate_repo_->append(tx, rate("USD", "CNY", "7", 1000, "ECB"));
        if (!r) return std::unexpected(r.error());
        return {};
    });
    ASSERT_TRUE(rw.has_value());

    provider_->set_fail(true);
    RefreshExchangeRatesUseCase uc(
        *active_currency_query_, *rate_repo_, *provider_, *uow_, clock_);
    RefreshExchangeRatesCommand cmd;
    cmd.target_currency_codes = {"CNY", "EUR"};
    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->degraded);
    EXPECT_NE(result->message.find("historical fallback is incomplete"),
              std::string::npos);
    EXPECT_NE(store_->outbox.back().payload_json.find(
                  "\"historicalAvailable\":false"),
              std::string::npos);
}

TEST_F(UseCaseTest, RefreshExchangeRates_WhenProviderOk_AppendsRatesAndOutbox) {
    auto s = seed();
    (void)s;
    RefreshExchangeRatesUseCase uc(
        *active_currency_query_, *rate_repo_, *provider_, *uow_, clock_);
    RefreshExchangeRatesCommand cmd;
    cmd.target_currency_codes = {"CNY", "EUR"};
    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->degraded);
    EXPECT_EQ(result->appended_count, 2u);
    EXPECT_EQ(store_->exchange_rates.size(), 2u);
    ASSERT_EQ(store_->outbox.size(), 2u);
    EXPECT_EQ(store_->outbox.front().event_name, "ExchangeRateRefreshed");
    EXPECT_NE(store_->outbox.front().payload_json.find("\"targetCurrency\":"),
              std::string::npos);
}

TEST_F(UseCaseTest, RefreshExchangeRates_IncludesBaseCurrencyWithoutMatchingAccount) {
    auto s = seed();
    auto preference_write = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            return pref_repo_->save(tx, UserPreference(s.user, ccy("EUR")));
        });
    ASSERT_TRUE(preference_write.has_value()) << preference_write.error().message;

    RefreshExchangeRatesUseCase uc(
        *active_currency_query_, *rate_repo_, *provider_, *uow_, clock_);
    auto result = uc.execute();

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->appended_count, 2u);  // CNY account + EUR report base; USD is pivot.
    std::set<std::string> targets;
    for (const auto& [_, exchange_rate] : store_->exchange_rates) {
        targets.insert(exchange_rate.target().code());
    }
    EXPECT_EQ(targets, (std::set<std::string>{"CNY", "EUR"}));
}

TEST_F(UseCaseTest, RefreshExchangeRates_WhenProviderOmitsPair_RejectsWholeResponse) {
    auto s = seed();
    (void)s;
    provider_->set_omit_last(true);
    RefreshExchangeRatesUseCase uc(
        *active_currency_query_, *rate_repo_, *provider_, *uow_, clock_);
    RefreshExchangeRatesCommand cmd;
    cmd.target_currency_codes = {"CNY", "EUR"};

    auto result = uc.execute(cmd);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ExternalServiceError);
    EXPECT_TRUE(store_->exchange_rates.empty());
    EXPECT_TRUE(store_->outbox.empty());
}

TEST_F(UseCaseTest, RefreshExchangeRates_WhenProviderDuplicatesPair_RejectsWholeResponse) {
    auto s = seed();
    (void)s;
    provider_->set_duplicate_first(true);
    RefreshExchangeRatesUseCase uc(
        *active_currency_query_, *rate_repo_, *provider_, *uow_, clock_);
    RefreshExchangeRatesCommand cmd;
    cmd.target_currency_codes = {"CNY", "EUR"};

    auto result = uc.execute(cmd);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ExternalServiceError);
    EXPECT_TRUE(store_->exchange_rates.empty());
    EXPECT_TRUE(store_->outbox.empty());
}

TEST(DomainEventTest, ExchangeRateEvents_EscapeStringsAsValidJson) {
    ExchangeRateRefreshedEvent refreshed(
        "Provider \"A\"\\line", "USD", "CNY", sample_time());
    const auto refreshed_json = nlohmann::json::parse(refreshed.payload_json());
    EXPECT_EQ(refreshed_json.at("provider"), "Provider \"A\"\\line");
    EXPECT_EQ(refreshed_json.at("baseCurrency"), "USD");
    EXPECT_EQ(refreshed_json.at("targetCurrency"), "CNY");

    ExchangeRateRefreshFailedEvent failed(
        "Provider\nB", "USD", false, "timeout\tupstream", sample_time());
    const auto failed_json = nlohmann::json::parse(failed.payload_json());
    EXPECT_EQ(failed_json.at("provider"), "Provider\nB");
    EXPECT_EQ(failed_json.at("reason"), "timeout\tupstream");
    EXPECT_FALSE(failed_json.at("historicalAvailable").get<bool>());

    // duration_cast truncates negative sub-second values toward zero. Event
    // epoch seconds instead use floor so pre-epoch payloads remain correct.
    const auto pre_epoch = std::chrono::system_clock::time_point{} -
                           std::chrono::milliseconds(500);
    TransactionCreatedEvent pre_epoch_event(
        UserId{1}, TransactionId{2}, AccountId{3}, pre_epoch);
    const auto pre_epoch_json =
        nlohmann::json::parse(pre_epoch_event.payload_json());
    EXPECT_EQ(pre_epoch_json.at("occurredAt"), -1);
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

    auto thrown = uow_->execute_in_transaction(
        [](ITransactionContext&) -> RepositoryVoidResult {
            throw std::runtime_error("sensitive action detail");
        });
    ASSERT_FALSE(thrown.has_value());
    EXPECT_EQ(thrown.error().status, RepositoryStatus::DatabaseError);

    auto recovered = uow_->execute_in_transaction(
        [](ITransactionContext&) -> RepositoryVoidResult { return {}; });
    EXPECT_TRUE(recovered.has_value());
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
    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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
    // Add a Credit (liability) account. The seed's CNY wallet has zero balance,
    // which must convert to zero USD without requiring an exchange-rate row.
    AccountId credit;
    auto add = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        Account card(AccountId{}, s.user, "Card", AccountType::Credit, "credit", ccy("USD"));
        auto id = account_repo_->save(tx, card);
        if (!id) return std::unexpected(id.error());
        credit = *id;
        return {};
    });
    ASSERT_TRUE(add.has_value());

    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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
    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);

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
    InMemoryUnitOfWork request_uow(*store_, s.user);
    DeleteAccountUseCase uc(
        *account_repo_, *tx_repo_, request_uow, *audit_repo_);
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
    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand income;
    income.user_id = s.user;
    income.account_id = s.cash;
    income.type = TransactionType::Income;
    income.amount = "250";
    income.currency_code = "USD";
    income.occurred_at = sample_time();
    ASSERT_TRUE(create_uc.execute(income).has_value());

    InMemoryUnitOfWork request_uow(*store_, s.user);
    DeleteAccountUseCase uc(
        *account_repo_, *tx_repo_, request_uow, *audit_repo_);
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
    ASSERT_EQ(store_->audit_logs.size(), 1u);
    EXPECT_EQ(store_->audit_logs.front().action, AuditAction::DangerousDelete);
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

    InMemoryUnitOfWork request_uow(*store_, other);
    DeleteAccountUseCase uc(
        *account_repo_, *tx_repo_, request_uow, *audit_repo_);
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
    InMemoryUnitOfWork request_uow(*store_, s.user);
    DeleteAccountUseCase uc(
        *account_repo_, *tx_repo_, request_uow, *audit_repo_);
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
    domain::RepositoryResult<std::vector<ExchangeRate>> find_history_for_pair(
        const Currency&, const Currency&,
        std::chrono::system_clock::time_point,
        std::chrono::system_clock::time_point) override {
        return std::unexpected(RepositoryError::database("db down"));
    }
};

class CountingTransactionRepository final : public ITransactionRepository {
public:
    explicit CountingTransactionRepository(ITransactionRepository& delegate)
        : delegate_(delegate) {}

    RepositoryResult<Transaction> find_by_id(TransactionId id) override {
        return delegate_.find_by_id(id);
    }
    RepositoryResult<Transaction> find_by_id_for_update(
        ITransactionContext& tx, TransactionId id) override {
        return delegate_.find_by_id_for_update(tx, id);
    }
    RepositoryResult<Transaction> save_single(
        ITransactionContext& tx, const Transaction& value) override {
        return delegate_.save_single(tx, value);
    }
    RepositoryResult<TransferPersistResult> save_transfer(
        ITransactionContext& tx, const TransferAggregate& value) override {
        return delegate_.save_transfer(tx, value);
    }
    RepositoryResult<std::vector<Transaction>> find_by_account(
        AccountId account_id,
        std::optional<std::chrono::system_clock::time_point> from,
        std::optional<std::chrono::system_clock::time_point> to,
        bool include_deleted) override {
        return delegate_.find_by_account(
            account_id, from, to, include_deleted);
    }
    RepositoryResult<std::vector<Transaction>> find_by_user(
        UserId user_id, bool include_deleted) override {
        ++find_by_user_calls;
        return delegate_.find_by_user(user_id, include_deleted);
    }
    RepositoryResult<std::vector<Transaction>> find_by_user_in_range(
        UserId user_id,
        std::optional<std::chrono::system_clock::time_point> from,
        std::optional<std::chrono::system_clock::time_point> to,
        bool include_deleted) override {
        ++range_calls;
        return delegate_.find_by_user_in_range(
            user_id, from, to, include_deleted);
    }
    RepositoryResult<TransferSnapshot> find_transfer_by_group(
        TransferGroupId group_id, UserId user_id) override {
        return delegate_.find_transfer_by_group(group_id, user_id);
    }
    RepositoryVoidResult soft_delete(
        ITransactionContext& tx, TransactionId id, UserId user_id,
        std::chrono::system_clock::time_point deleted_at) override {
        return delegate_.soft_delete(tx, id, user_id, deleted_at);
    }
    RepositoryVoidResult physical_delete_by_account(
        ITransactionContext& tx, AccountId account_id) override {
        return delegate_.physical_delete_by_account(tx, account_id);
    }
    RepositoryVoidResult physical_delete_transfers_touching_account(
        ITransactionContext& tx, AccountId account_id) override {
        return delegate_.physical_delete_transfers_touching_account(
            tx, account_id);
    }

    std::size_t find_by_user_calls = 0;
    std::size_t range_calls = 0;

private:
    ITransactionRepository& delegate_;
};

class CountingRateRepository final : public IExchangeRateRepository {
public:
    explicit CountingRateRepository(IExchangeRateRepository& delegate)
        : delegate_(delegate) {}

    RepositoryResult<ExchangeRateId> append(
        ITransactionContext& tx, const ExchangeRate& value) override {
        return delegate_.append(tx, value);
    }
    RepositoryResult<ExchangeRate> find_latest(
        const Currency& base, const Currency& target) override {
        ++latest_calls;
        return delegate_.find_latest(base, target);
    }
    RepositoryResult<ExchangeRate> find_historical(
        const Currency& base, const Currency& target,
        std::chrono::system_clock::time_point at) override {
        ++historical_calls;
        return delegate_.find_historical(base, target, at);
    }
    RepositoryResult<std::vector<ExchangeRate>> find_all_for_pair(
        const Currency& base, const Currency& target) override {
        ++all_calls;
        return delegate_.find_all_for_pair(base, target);
    }
    RepositoryResult<std::vector<ExchangeRate>> find_history_for_pair(
        const Currency& base, const Currency& target,
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to) override {
        ++history_calls;
        return delegate_.find_history_for_pair(base, target, from, to);
    }

    std::size_t latest_calls = 0;
    std::size_t historical_calls = 0;
    std::size_t all_calls = 0;
    std::size_t history_calls = 0;

private:
    IExchangeRateRepository& delegate_;
};
} // namespace

TEST_F(UseCaseTest, NetWorth_WhenRateRepositoryFails_ReturnsInfrastructureFailure) {
    auto s = seed();
    // CNY holding forces a conversion; the failing repo returns DatabaseError.
    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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

TEST_F(UseCaseTest, ReportsBatchTransactionsAndCacheRateHistoryPerRequest) {
    const auto s = seed();
    auto rate_write = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto appended = rate_repo_->append(
                tx, rate("USD", "CNY", "7", 1000));
            return appended ? RepositoryVoidResult{}
                            : RepositoryVoidResult(
                                  std::unexpected(appended.error()));
        });
    ASSERT_TRUE(rate_write.has_value()) << rate_write.error().message;

    CreateTransactionUseCase create(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
    for (const auto at : {sample_time(), sample_time() + std::chrono::hours(1)}) {
        CreateTransactionCommand command;
        command.user_id = s.user;
        command.account_id = s.cny_wallet;
        command.type = TransactionType::Income;
        command.amount = "70";
        command.currency_code = "CNY";
        command.occurred_at = at;
        ASSERT_TRUE(create.execute(command).has_value());
    }

    CountingTransactionRepository transactions(*tx_repo_);
    CountingRateRepository rates(*rate_repo_);
    ReportQueryService reports(
        *account_repo_, transactions, rates, *pref_repo_);
    const auto now = sample_time() + std::chrono::days(1);
    auto dashboard = reports.dashboard_summary(s.user, now);
    ASSERT_TRUE(dashboard.has_value()) << dashboard.error().message;
    EXPECT_EQ(transactions.range_calls, 1U);
    EXPECT_EQ(transactions.find_by_user_calls, 0U);
    EXPECT_EQ(rates.history_calls, 2U);
    EXPECT_EQ(rates.historical_calls, 0U);
    EXPECT_EQ(rates.latest_calls, 0U);
    EXPECT_EQ(rates.all_calls, 0U);

    auto trend = reports.cash_flow_trend(s.user, 2024, 6, 2024, 6);
    ASSERT_TRUE(trend.has_value()) << trend.error().message;
    EXPECT_EQ(transactions.range_calls, 2U);
    EXPECT_EQ(transactions.find_by_user_calls, 0U);
    EXPECT_EQ(rates.history_calls, 4U);
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

    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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
    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand expense;
    expense.user_id = s.user;
    expense.account_id = s.cash;
    expense.type = TransactionType::Expense;
    expense.amount = "40";
    expense.currency_code = "USD";
    expense.occurred_at = sample_time();
    auto created = create_uc.execute(expense);
    ASSERT_TRUE(created.has_value());

    InMemoryUnitOfWork request_uow(*store_, s.user);
    DeleteTransactionUseCase del(*tx_repo_, *audit_repo_, request_uow);
    DeleteTransactionCommand dc;
    dc.user_id = s.user;
    dc.transaction_id = created->id;
    dc.deleted_at = sample_time();
    ASSERT_TRUE(del.execute(dc).has_value());
    ASSERT_EQ(store_->audit_logs.size(), 1u);

    auto second = del.execute(dc);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, ErrorCode::Conflict);
    EXPECT_EQ(store_->audit_logs.size(), 1u);
}

// Item 1: create-transaction builds its DTO from the persisted entity (id set)
// without any post-commit re-read.
TEST_F(UseCaseTest, CreateTransaction_ReturnsPersistedEntityWithId) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);
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

    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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

TEST_F(UseCaseTest, Dashboard_PercentagesUseHalfEvenRounding) {
    auto s = seed();
    auto setup = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto id = rate_repo_->append(tx, rate("USD", "CNY", "7", 1000, "ECB"));
            return id ? RepositoryVoidResult{}
                      : RepositoryVoidResult(std::unexpected(id.error()));
        });
    ASSERT_TRUE(setup.has_value());
    CreateTransactionUseCase create(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
    for (const auto& [account, amount] :
         std::vector<std::pair<AccountId, std::string>>{
             {s.cash, "1"}, {s.savings, "2"}}) {
        CreateTransactionCommand command;
        command.user_id = s.user;
        command.account_id = account;
        command.type = TransactionType::Income;
        command.amount = amount;
        command.currency_code = "USD";
        command.occurred_at = sample_time();
        ASSERT_TRUE(create.execute(command).has_value());
    }
    ReportQueryService reports(
        *account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto dashboard = reports.dashboard_summary(
        s.user, std::chrono::system_clock::from_time_t(1719400000));
    ASSERT_TRUE(dashboard.has_value()) << dashboard.error().message;
    for (const auto& slice : dashboard->asset_distribution) {
        if (slice.label == "Cash") {
            EXPECT_EQ(slice.percentage, "33.3%");
        }
        if (slice.label == "Savings") {
            EXPECT_EQ(slice.percentage, "66.7%");
        }
    }
}

// ---- Item 10: amount exceeding DB NUMERIC(20,8) is rejected before write ----

TEST_F(UseCaseTest, CreateTransaction_WhenAmountExceedsDbPrecision_Rejected) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);

    // 13 integer digits: beyond NUMERIC(20,8)'s 12-digit integer part.
    CreateTransactionCommand too_big;
    too_big.user_id = s.user;
    too_big.account_id = s.cash;
    too_big.type = TransactionType::Income;
    too_big.amount = "1000000000000"; // 1e12
    too_big.currency_code = "USD";
    too_big.occurred_at = sample_time();
    auto r1 = uc.execute(too_big);
    ASSERT_FALSE(r1.has_value());
    EXPECT_EQ(r1.error().code, ErrorCode::ValidationError);

    // 9 fractional digits: beyond scale 8 (would round on write).
    CreateTransactionCommand too_precise;
    too_precise.user_id = s.user;
    too_precise.account_id = s.cash;
    too_precise.type = TransactionType::Income;
    too_precise.amount = "1.123456789";
    too_precise.currency_code = "USD";
    too_precise.occurred_at = sample_time();
    auto r2 = uc.execute(too_precise);
    ASSERT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code, ErrorCode::ValidationError);

    // Precision beyond Decimal's internal scale must not be rounded away before
    // the DB-boundary check.
    too_precise.amount = "1.00000000001";
    auto r3 = uc.execute(too_precise);
    ASSERT_FALSE(r3.has_value());
    EXPECT_EQ(r3.error().code, ErrorCode::ValidationError);
}

TEST_F(UseCaseTest, CreateTransaction_WhenDescriptionExceedsApplicationLimit_Rejected) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);

    CreateTransactionCommand command;
    command.user_id = s.user;
    command.account_id = s.cash;
    command.type = TransactionType::Income;
    command.amount = "1";
    command.currency_code = "USD";
    command.description = std::string(4097, 'x');
    command.occurred_at = sample_time();

    const auto result = uc.execute(command);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
    EXPECT_TRUE(store_->transactions.empty());
}

TEST_F(UseCaseTest, CreateTransaction_WhenDecimalTextIsNotPlain_Rejected) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);

    for (const auto& amount : {"+1", " 1", "1 ", ".5", "1."}) {
        CreateTransactionCommand command;
        command.user_id = s.user;
        command.account_id = s.cash;
        command.type = TransactionType::Income;
        command.amount = amount;
        command.currency_code = "USD";
        command.occurred_at = sample_time();
        const auto result = uc.execute(command);
        ASSERT_FALSE(result.has_value()) << amount;
        EXPECT_EQ(result.error().code, ErrorCode::ValidationError) << amount;
    }
    EXPECT_TRUE(store_->transactions.empty());
}

TEST_F(UseCaseTest, CreateTransfer_WhenDescriptionExceedsApplicationLimit_Rejected) {
    auto s = seed();
    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);

    CreateTransferCommand command;
    command.user_id = s.user;
    command.source_account_id = s.cash;
    command.target_account_id = s.savings;
    command.mode = TransferInputMode::BothAmounts;
    command.outgoing_amount = "1";
    command.incoming_amount = "1";
    command.description = std::string(4097, 'x');
    command.occurred_at = sample_time();

    const auto result = uc.execute(command);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
    EXPECT_TRUE(store_->transactions.empty());
    EXPECT_TRUE(store_->transfer_groups.empty());
}

TEST_F(UseCaseTest, CreateTransfer_EnforcesDbAmountScaleAndRoundsDerivedLeg) {
    auto s = seed();
    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);

    CreateTransferCommand invalid;
    invalid.user_id = s.user;
    invalid.source_account_id = s.cash;
    invalid.target_account_id = s.cny_wallet;
    invalid.mode = TransferInputMode::BothAmounts;
    invalid.outgoing_amount = "1.123456789";
    invalid.incoming_amount = "8";
    invalid.occurred_at = sample_time();
    auto rejected = uc.execute(invalid);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, ErrorCode::ValidationError);

    CreateTransferCommand derived;
    derived.user_id = s.user;
    derived.source_account_id = s.cash;
    derived.target_account_id = s.cny_wallet;
    derived.mode = TransferInputMode::OutgoingAndRate;
    derived.outgoing_amount = "1";
    derived.rate = "7.123456789";
    derived.occurred_at = sample_time();
    auto created = uc.execute(derived);
    ASSERT_TRUE(created.has_value()) << created.error().message;
    EXPECT_EQ(created->incoming_amount, "7.12345679");
    auto stored = tx_repo_->find_by_id(created->incoming_transaction_id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->amount().amount().to_string(), "7.12345679");
}

// ---- Item 14: in-memory repos provide read-your-writes within a transaction ----

TEST_F(UseCaseTest, UserRepository_ReadYourWrites_WithinTransaction) {
    auto s = seed();
    // Save a preference change and re-read it in the SAME transaction; the
    // updated value must be visible (staged shadows committed).
    auto ok = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        UserPreference updated(s.user, ccy("EUR"), "de-DE", "Europe/Berlin");
        if (auto r = pref_repo_->save(tx, updated); !r) return std::unexpected(r.error());
        // Read-your-writes: must see EUR / Berlin, not the seeded USD default.
        auto reread = pref_repo_->find_by_user(tx, s.user);
        if (!reread) return std::unexpected(reread.error());
        EXPECT_EQ(reread->base_currency().code(), "EUR");
        EXPECT_EQ(reread->timezone(), "Europe/Berlin");
        return {};
    });
    ASSERT_TRUE(ok.has_value()) << ok.error().message;

    // After commit the change persists.
    auto after = pref_repo_->find_by_user(s.user);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->base_currency().code(), "EUR");
}

TEST_F(UseCaseTest, UserRepository_ReadYourWrites_FindByUsername) {
    UserId uid;
    auto ok = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto created = user_repo_->create(tx, "dave", "hash", ccy("USD"));
        if (!created) return std::unexpected(created.error());
        uid = *created;
        // The just-created (staged, uncommitted) user must be findable by name.
        auto found = user_repo_->find_by_username(tx, "dave");
        if (!found) return std::unexpected(found.error());
        EXPECT_EQ(found->id().value(), uid.value());
        return {};
    });
    ASSERT_TRUE(ok.has_value()) << ok.error().message;
}

TEST_F(UseCaseTest, InMemoryRepositories_RejectBrokenForeignKeys) {
    auto s = seed();

    auto wrong_currency = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            Transaction invalid(
                TransactionId{},
                s.user,
                s.cash,
                money("10", "CNY"),
                TransactionType::Income,
                sample_time(),
                "Wrong account currency");
            auto saved = tx_repo_->save_single(tx, invalid);
            return saved ? RepositoryVoidResult{} : std::unexpected(saved.error());
        });
    ASSERT_FALSE(wrong_currency.has_value());
    EXPECT_EQ(wrong_currency.error().status, RepositoryStatus::ValidationError);

    auto missing_pref = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            return pref_repo_->save(
                tx, UserPreference(UserId(9999), ccy("USD")));
        });
    ASSERT_FALSE(missing_pref.has_value());
    EXPECT_EQ(missing_pref.error().status, RepositoryStatus::NotFound);

    auto cross_board_parent = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto parent = category_repo_->save(
                tx, Category(CategoryId{}, s.user, "Income", CategoryBoard::Income));
            if (!parent) return std::unexpected(parent.error());
            auto child = category_repo_->save(
                tx, Category(CategoryId{}, s.user, "Wrong child",
                             CategoryBoard::Expense, *parent));
            if (!child) return std::unexpected(child.error());
            return {};
        });
    ASSERT_FALSE(cross_board_parent.has_value());
    EXPECT_EQ(cross_board_parent.error().status, RepositoryStatus::ValidationError);
}

TEST_F(UseCaseTest, InMemoryRepositories_RejectTenantAndBoardReassignment) {
    auto s = seed();
    UserId other_user;
    CategoryId category_id;
    auto setup = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto other = user_repo_->create(tx, "other-owner", "hash", ccy("USD"));
            if (!other) return std::unexpected(other.error());
            other_user = *other;
            auto category = category_repo_->save(
                tx, Category(CategoryId{}, s.user, "Food", CategoryBoard::Expense));
            if (!category) return std::unexpected(category.error());
            category_id = *category;
            return {};
        });
    ASSERT_TRUE(setup.has_value()) << setup.error().message;

    auto cash = account_repo_->find_by_id(s.cash);
    ASSERT_TRUE(cash.has_value());
    Account reassigned_account(
        cash->id(), other_user, cash->name(), cash->type(), cash->subtype(),
        cash->currency(), cash->description(), cash->is_archived(),
        cash->archived_at(), cash->created_at(), cash->updated_at(),
        cash->version(),
        cash->has_category_override()
            ? std::optional<AccountCategory>(cash->category())
            : std::nullopt);
    auto account_move = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = account_repo_->save(tx, reassigned_account);
            if (!saved) return std::unexpected(saved.error());
            return {};
        });
    ASSERT_FALSE(account_move.has_value());
    EXPECT_EQ(account_move.error().status, RepositoryStatus::ValidationError);

    auto category_move = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = category_repo_->save(
                tx, Category(category_id, other_user, "Food", CategoryBoard::Expense));
            if (!saved) return std::unexpected(saved.error());
            return {};
        });
    ASSERT_FALSE(category_move.has_value());
    EXPECT_EQ(category_move.error().status, RepositoryStatus::ValidationError);

    auto board_move = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = category_repo_->save(
                tx, Category(category_id, s.user, "Food", CategoryBoard::Income));
            if (!saved) return std::unexpected(saved.error());
            return {};
        });
    ASSERT_FALSE(board_move.has_value());
    EXPECT_EQ(board_move.error().status, RepositoryStatus::ValidationError);
}

TEST_F(UseCaseTest, CategoryRepository_RejectsTreesDeeperThanReadLimit) {
    auto s = seed();
    std::optional<CategoryId> parent;
    for (int depth = 0; depth < kMaxCategoryTreeDepth; ++depth) {
        CategoryId created;
        auto write = uow_->execute_in_transaction(
            [&](ITransactionContext& tx) -> RepositoryVoidResult {
                auto saved = category_repo_->save(
                    tx,
                    Category(
                        CategoryId{},
                        s.user,
                        "Level " + std::to_string(depth + 1),
                        CategoryBoard::Expense,
                        parent));
                if (!saved) {
                    return std::unexpected(saved.error());
                }
                created = *saved;
                return {};
            });
        ASSERT_TRUE(write.has_value()) << write.error().message;
        parent = created;
    }

    ASSERT_TRUE(parent.has_value());
    auto root = category_repo_->resolve_root_id_for_user(*parent, s.user);
    ASSERT_TRUE(root.has_value()) << root.error().message;

    auto too_deep = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = category_repo_->save(
                tx,
                Category(
                    CategoryId{},
                    s.user,
                    "Level 65",
                    CategoryBoard::Expense,
                    parent));
            return saved ? RepositoryVoidResult{}
                         : RepositoryVoidResult(std::unexpected(saved.error()));
        });
    ASSERT_FALSE(too_deep.has_value());
    EXPECT_EQ(too_deep.error().status, RepositoryStatus::ValidationError);
}

// ---- Item 12: strongly-typed events carry userId/occurredAt; outbox stores time ----

TEST_F(UseCaseTest, TransactionCreatedEvent_CarriesRequiredFieldsAndOutboxTime) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand income;
    income.user_id = s.user;
    income.account_id = s.cash;
    income.type = TransactionType::Income;
    income.amount = "100";
    income.currency_code = "USD";
    income.occurred_at = sample_time();
    auto result = uc.execute(income);
    ASSERT_TRUE(result.has_value());

    ASSERT_EQ(store_->outbox.size(), 1u);
    const auto& rec = store_->outbox.front();
    EXPECT_EQ(rec.event_name, "TransactionCreated");
    EXPECT_EQ(rec.aggregate_type, "Transaction");
    // Payload carries the contract fields.
    EXPECT_NE(rec.payload_json.find("\"userId\":" + std::to_string(s.user.value())),
              std::string::npos);
    EXPECT_NE(rec.payload_json.find("\"accountId\":" + std::to_string(s.cash.value())),
              std::string::npos);
    EXPECT_NE(rec.payload_json.find("\"occurredAt\":"), std::string::npos);
    // Outbox row records the event time (== the transaction's occurred_at).
    EXPECT_EQ(rec.occurred_at, sample_time());
}

TEST_F(UseCaseTest, TransferCompletedEvent_CarriesSourceTargetAndUser) {
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
    ASSERT_TRUE(uc.execute(cmd).has_value());

    ASSERT_EQ(store_->outbox.size(), 1u);
    const auto& rec = store_->outbox.front();
    EXPECT_EQ(rec.event_name, "TransferCompleted");
    EXPECT_NE(rec.payload_json.find("\"sourceAccountId\":" +
                                    std::to_string(s.cash.value())),
              std::string::npos);
    EXPECT_NE(rec.payload_json.find("\"targetAccountId\":" +
                                    std::to_string(s.savings.value())),
              std::string::npos);
    EXPECT_NE(rec.payload_json.find("\"userId\":"), std::string::npos);
    EXPECT_EQ(rec.occurred_at, sample_time());
}

// ---- Item 13: Account category override persists and drives net worth ----

TEST_F(UseCaseTest, AccountCategoryOverride_PersistsAndOverridesDerivedCategory) {
    auto s = seed();
    AccountId invest;
    auto add = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        // Investment normally derives to Asset; override it to Liability.
        Account acct(
            AccountId{}, s.user, "Managed Fund", AccountType::Investment, "fund",
            ccy("USD"), "", false, std::nullopt,
            std::chrono::system_clock::now(), std::chrono::system_clock::now(),
            1, AccountCategory::Liability);
        auto id = account_repo_->save(tx, acct);
        if (!id) return std::unexpected(id.error());
        invest = *id;
        return {};
    });
    ASSERT_TRUE(add.has_value());

    auto loaded = account_repo_->find_by_id(invest);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->has_category_override());
    EXPECT_EQ(loaded->category(), AccountCategory::Liability);
    // Without override, Investment would derive to Asset.
    EXPECT_EQ(Account::default_category_for(AccountType::Investment),
              AccountCategory::Asset);
}

TEST_F(UseCaseTest, AccountWithoutOverride_DerivesCategoryFromType) {
    auto s = seed();
    // The seed's Cash account has no override.
    auto cash = account_repo_->find_by_id(s.cash);
    ASSERT_TRUE(cash.has_value());
    EXPECT_FALSE(cash->has_category_override());
    EXPECT_EQ(cash->category(), AccountCategory::Asset);
}

// ---- Item 9: signed Adjustment semantics (reversal / refund / FX gain) ----

TEST_F(UseCaseTest, Adjustment_PositiveIsInflow_NegativeIsOutflow) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);

    // Baseline income 1000.
    CreateTransactionCommand income;
    income.user_id = s.user;
    income.account_id = s.cash;
    income.type = TransactionType::Income;
    income.amount = "1000";
    income.currency_code = "USD";
    income.occurred_at = sample_time();
    ASSERT_TRUE(uc.execute(income).has_value());

    // Positive adjustment (e.g. refund/subsidy) increases balance.
    CreateTransactionCommand pos_adj;
    pos_adj.user_id = s.user;
    pos_adj.account_id = s.cash;
    pos_adj.type = TransactionType::Adjustment;
    pos_adj.amount = "50";
    pos_adj.currency_code = "USD";
    pos_adj.occurred_at = sample_time();
    ASSERT_TRUE(uc.execute(pos_adj).has_value());

    // Negative adjustment (e.g. fee/correction) decreases balance.
    CreateTransactionCommand neg_adj;
    neg_adj.user_id = s.user;
    neg_adj.account_id = s.cash;
    neg_adj.type = TransactionType::Adjustment;
    neg_adj.amount = "-30";
    neg_adj.currency_code = "USD";
    neg_adj.occurred_at = sample_time();
    ASSERT_TRUE(uc.execute(neg_adj).has_value());

    GetAccountBalanceUseCase balance_uc(*account_repo_);
    auto bal = balance_uc.execute(s.user, s.cash);
    ASSERT_TRUE(bal.has_value());
    // 1000 + 50 - 30 = 1020.
    EXPECT_EQ(bal->amount, "1020");
}

TEST_F(UseCaseTest, Adjustment_ZeroAmount_Rejected) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand adj;
    adj.user_id = s.user;
    adj.account_id = s.cash;
    adj.type = TransactionType::Adjustment;
    adj.amount = "0";
    adj.currency_code = "USD";
    adj.occurred_at = sample_time();
    auto result = uc.execute(adj);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationError);
}

TEST_F(UseCaseTest, CashFlow_SignedAdjustments_SplitAcrossIncomeAndExpense) {
    auto s = seed();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);

    // Positive adjustment => inflow (income side).
    CreateTransactionCommand pos_adj;
    pos_adj.user_id = s.user;
    pos_adj.account_id = s.cash;
    pos_adj.type = TransactionType::Adjustment;
    pos_adj.amount = "40";
    pos_adj.currency_code = "USD";
    pos_adj.occurred_at = sample_time();
    ASSERT_TRUE(uc.execute(pos_adj).has_value());

    // Negative adjustment => outflow (expense side).
    CreateTransactionCommand neg_adj;
    neg_adj.user_id = s.user;
    neg_adj.account_id = s.cash;
    neg_adj.type = TransactionType::Adjustment;
    neg_adj.amount = "-15";
    neg_adj.currency_code = "USD";
    neg_adj.occurred_at = sample_time();
    ASSERT_TRUE(uc.execute(neg_adj).has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto cf = reports.cash_flow(s.user);
    ASSERT_TRUE(cf.has_value()) << cf.error().message;
    EXPECT_EQ(cf->income_total, "40");
    EXPECT_EQ(cf->expense_total, "15");
    EXPECT_EQ(cf->net_total, "25");
}

// ---- Item 11: commands without a business time stamp "now", never epoch 0 ----

TEST_F(UseCaseTest, CreateTransaction_WhenNoOccurredAt_StampsRecentTime) {
    auto s = seed();
    const auto before = std::chrono::system_clock::now();
    CreateTransactionUseCase uc(*account_repo_, *category_repo_, *tx_repo_, *uow_);
    CreateTransactionCommand income;
    income.user_id = s.user;
    income.account_id = s.cash;
    income.type = TransactionType::Income;
    income.amount = "100";
    income.currency_code = "USD";
    // occurred_at deliberately left unset (nullopt).
    auto result = uc.execute(income);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    const auto after = std::chrono::system_clock::now();

    auto stored = tx_repo_->find_by_id(result->id);
    ASSERT_TRUE(stored.has_value());
    // Must be "now", not the 1970 epoch.
    EXPECT_GE(stored->occurred_at(), before);
    EXPECT_LE(stored->occurred_at(), after);
    EXPECT_GT(stored->occurred_at(), std::chrono::system_clock::from_time_t(1000000000));
}

TEST_F(UseCaseTest, CreateTransfer_WhenNoOccurredAt_StampsRecentTime) {
    auto s = seed();
    const auto before = std::chrono::system_clock::now();
    CreateTransferUseCase uc(*account_repo_, *tx_repo_, *uow_);
    CreateTransferCommand cmd;
    cmd.user_id = s.user;
    cmd.source_account_id = s.cash;
    cmd.target_account_id = s.savings;
    cmd.mode = TransferInputMode::BothAmounts;
    cmd.outgoing_amount = "100";
    cmd.incoming_amount = "100";
    // occurred_at left unset.
    auto result = uc.execute(cmd);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    const auto after = std::chrono::system_clock::now();

    auto leg = tx_repo_->find_by_id(result->outgoing_transaction_id);
    ASSERT_TRUE(leg.has_value());
    EXPECT_GE(leg->occurred_at(), before);
    EXPECT_LE(leg->occurred_at(), after);
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

    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
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

TEST_F(UseCaseTest, Dashboard_WhenTimezoneIsUnknown_FailsInsteadOfUsingUtc) {
    auto s = seed();
    auto setup = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            return pref_repo_->save(
                tx, UserPreference(s.user, ccy("USD"), "en-US", "Invalid/Zone"));
        });
    ASSERT_TRUE(setup.has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto dashboard = reports.dashboard_summary(s.user, sample_time());
    ASSERT_FALSE(dashboard.has_value());
    EXPECT_EQ(dashboard.error().code, ErrorCode::ConfigurationError);
}

TEST_F(UseCaseTest, Dashboard_WhenTimezoneIsEmpty_FailsInsteadOfUsingUtc) {
    auto s = seed();
    auto setup = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            return pref_repo_->save(
                tx, UserPreference(s.user, ccy("USD"), "en-US", ""));
        });
    ASSERT_TRUE(setup.has_value());

    ReportQueryService reports(*account_repo_, *tx_repo_, *rate_repo_, *pref_repo_);
    auto dashboard = reports.dashboard_summary(s.user, sample_time());
    ASSERT_FALSE(dashboard.has_value());
    EXPECT_EQ(dashboard.error().code, ErrorCode::ConfigurationError);
}

// Top expense categories roll sub-categories up to their first-level parent
// when a category repository is supplied, and use the root's human name.
TEST_F(UseCaseTest, TopExpenseCategories_RollUpToRootCategory) {
    auto s = seed();
    CategoryId food_root;
    CategoryId food_dining; // child of food_root
    CategoryId transport_root;
    auto setup = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        Category root(CategoryId{}, s.user, "Food", CategoryBoard::Expense);
        auto rid = category_repo_->save(tx, root);
        if (!rid) return std::unexpected(rid.error());
        food_root = *rid;

        Category child(CategoryId{}, s.user, "Dining", CategoryBoard::Expense, food_root);
        auto cid = category_repo_->save(tx, child);
        if (!cid) return std::unexpected(cid.error());
        food_dining = *cid;

        auto transport = category_repo_->save(
            tx,
            Category(
                CategoryId{}, s.user, "Transport", CategoryBoard::Expense));
        if (!transport) return std::unexpected(transport.error());
        transport_root = *transport;
        return {};
    });
    ASSERT_TRUE(setup.has_value()) << setup.error().message;

    CreateTransactionUseCase create_uc(
        *account_repo_, *category_repo_, *tx_repo_, *uow_);
    // Two expenses: one on the child, one on the root — both roll to "Food".
    for (auto [cat, amt] : std::vector<std::pair<CategoryId, std::string>>{
             {food_dining, "30"}, {food_root, "20"},
             {transport_root, "50"}}) {
        CreateTransactionCommand e;
        e.user_id = s.user;
        e.account_id = s.cash;
        e.type = TransactionType::Expense;
        e.amount = amt;
        e.currency_code = "USD";
        e.category_id = cat;
        e.occurred_at = sample_time();
        ASSERT_TRUE(create_uc.execute(e).has_value());
    }

    ReportQueryService reports(
        *account_repo_, *tx_repo_, *rate_repo_, *pref_repo_, category_repo_.get());
    // sample_time() is 2024-06-25; use a matching "now" so the window covers it.
    const auto now = std::chrono::system_clock::from_time_t(1719400000); // 2024-06-26
    auto dash = reports.dashboard_summary(s.user, now);
    ASSERT_TRUE(dash.has_value()) << dash.error().message;

    // Food aggregates 30 + 20 = 50. Transport also totals 50, so the stable
    // descending sort must preserve the first-seen Food bucket as the tie-break.
    ASSERT_EQ(dash->top_expense_categories.size(), 2u);
    const auto& slice = dash->top_expense_categories.front();
    EXPECT_EQ(slice.category_name, "Food");
    EXPECT_EQ(slice.amount, "50");
    ASSERT_TRUE(slice.category_id.has_value());
    EXPECT_EQ(slice.category_id->value(), food_root.value());
    EXPECT_EQ(dash->top_expense_categories[1].category_name, "Transport");
}

} // namespace pfh::test
