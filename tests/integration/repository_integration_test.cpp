// Personal Finance Hub - Repository Integration Tests (In-Memory)
// Version: 1.0
//
// These tests exercise repository consistency rules without PostgreSQL:
// user isolation, optimistic locking, balance cache rebuild, append-only
// exchange rates, historical rate selection, and UnitOfWork commit/rollback
// with outbox co-commit.
//
// When Drogon/PostgreSQL adapters land, the same scenarios should be re-run
// against a real test database.

#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/domain/events/simple_domain_event.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_exchange_rate_repository.h"
#include "pfh/domain/repositories/i_transaction_repository.h"
#include "pfh/domain/repositories/i_user_preference_repository.h"
#include "pfh/domain/repositories/i_user_repository.h"
#include "pfh/domain/transfer_domain_service.h"
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
using namespace pfh::infrastructure;
using namespace pfh::test;

namespace pfh::test {

class RepositoryIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        store_ = std::make_unique<InMemoryStore>();
        uow_ = std::make_unique<InMemoryUnitOfWork>(*store_);
        user_repo_ = std::make_unique<InMemoryUserRepository>(*store_);
        pref_repo_ = std::make_unique<InMemoryUserPreferenceRepository>(*store_);
        tx_repo_ = std::make_unique<InMemoryTransactionRepository>(*store_);
        account_repo_ = std::make_unique<InMemoryAccountRepository>(*store_, *tx_repo_);
        rate_repo_ = std::make_unique<InMemoryExchangeRateRepository>(*store_);
    }

    std::unique_ptr<InMemoryStore> store_;
    std::unique_ptr<application::IUnitOfWork> uow_;
    std::unique_ptr<IUserRepository> user_repo_;
    std::unique_ptr<IUserPreferenceRepository> pref_repo_;
    std::unique_ptr<ITransactionRepository> tx_repo_;
    std::unique_ptr<IAccountRepository> account_repo_;
    std::unique_ptr<IExchangeRateRepository> rate_repo_;

    // Helper: create user + two accounts in one transaction.
    struct FixtureIds {
        UserId user;
        AccountId cash;
        AccountId savings;
        AccountId cny;
    };

    FixtureIds seed_user_with_accounts(const std::string& username = "alice") {
        FixtureIds ids;
        auto result = uow_->execute_in_transaction([&](ITransactionContext& tx)
                                                       -> RepositoryVoidResult {
            auto uid = user_repo_->create(tx, username, "hash", ccy("USD"));
            if (!uid) {
                return std::unexpected(uid.error());
            }
            ids.user = *uid;

            Account cash(
                AccountId{}, // invalid => create
                ids.user,
                "Cash Wallet",
                AccountType::Cash,
                "wallet",
                ccy("USD"));
            auto cash_id = account_repo_->save(tx, cash);
            if (!cash_id) {
                return std::unexpected(cash_id.error());
            }
            ids.cash = *cash_id;

            Account savings(
                AccountId{},
                ids.user,
                "Savings",
                AccountType::Savings,
                "bank",
                ccy("USD"));
            auto savings_id = account_repo_->save(tx, savings);
            if (!savings_id) {
                return std::unexpected(savings_id.error());
            }
            ids.savings = *savings_id;

            Account cny_wallet(
                AccountId{},
                ids.user,
                "CNY Wallet",
                AccountType::DigitalWallet,
                "wallet",
                ccy("CNY"));
            auto cny_id = account_repo_->save(tx, cny_wallet);
            if (!cny_id) {
                return std::unexpected(cny_id.error());
            }
            ids.cny = *cny_id;
            return {};
        });
        EXPECT_TRUE(result.has_value()) << result.error().message;
        return ids;
    }
};

// ---- Unit of Work commit / rollback + outbox co-commit ----

TEST_F(RepositoryIntegrationTest, UnitOfWork_WhenActionSucceeds_CommitsBusinessAndOutbox) {
    auto commit = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto uid = user_repo_->create(tx, "bob", "hash", ccy("USD"));
        if (!uid) {
            return std::unexpected(uid.error());
        }
        uow_->register_event(std::make_shared<SimpleDomainEvent>(
            "UserCreated",
            "User",
            uid->to_string(),
            R"({"username":"bob"})"));
        return {};
    });
    ASSERT_TRUE(commit.has_value());
    ASSERT_EQ(store_->users.size(), 1u);
    ASSERT_EQ(store_->outbox.size(), 1u);
    EXPECT_EQ(store_->outbox[0].event_name, "UserCreated");
    EXPECT_EQ(store_->outbox[0].status, application::OutboxStatus::Pending);
}

TEST_F(RepositoryIntegrationTest, UnitOfWork_WhenActionFails_RollsBackBusinessAndOutbox) {
    auto failed = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto uid = user_repo_->create(tx, "carol", "hash", ccy("USD"));
        if (!uid) {
            return std::unexpected(uid.error());
        }
        uow_->register_event(std::make_shared<SimpleDomainEvent>(
            "UserCreated", "User", uid->to_string(), "{}"));
        return std::unexpected(RepositoryError::validation("forced failure"));
    });
    ASSERT_FALSE(failed.has_value());
    EXPECT_TRUE(store_->users.empty());
    EXPECT_TRUE(store_->outbox.empty())
        << "Outbox must not retain rows after rollback";
}

// ---- User isolation ----

TEST_F(RepositoryIntegrationTest, AccountRepository_WhenQueryingOtherUser_ReturnsNotFound) {
    auto alice = seed_user_with_accounts("alice");
    UserId bob;
    auto bob_create = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto uid = user_repo_->create(tx, "bob", "hash", ccy("USD"));
        if (!uid) {
            return std::unexpected(uid.error());
        }
        bob = *uid;
        return {};
    });
    ASSERT_TRUE(bob_create.has_value());

    // Alice's account is invisible to Bob.
    auto result = account_repo_->find_by_id_for_user(alice.cash, bob);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, RepositoryStatus::NotFound);
}

TEST_F(RepositoryIntegrationTest, TransactionRepository_WhenQueryingByUser_ReturnsOnlyOwnTransactions) {
    auto alice = seed_user_with_accounts("alice");
    auto bob = seed_user_with_accounts("bob");

    auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        Transaction alice_tx(
            TransactionId{},
            alice.user,
            alice.cash,
            money("100", "USD"),
            TransactionType::Income,
            sample_time(),
            "Alice salary");
        auto a = tx_repo_->save_single(tx, alice_tx);
        if (!a) {
            return std::unexpected(a.error());
        }

        Transaction bob_tx(
            TransactionId{},
            bob.user,
            bob.cash,
            money("50", "USD"),
            TransactionType::Income,
            sample_time(),
            "Bob salary");
        auto b = tx_repo_->save_single(tx, bob_tx);
        if (!b) {
            return std::unexpected(b.error());
        }
        return {};
    });
    ASSERT_TRUE(write.has_value());

    auto alice_txs = tx_repo_->find_by_user(alice.user);
    ASSERT_TRUE(alice_txs.has_value());
    ASSERT_EQ(alice_txs->size(), 1u);
    EXPECT_EQ(alice_txs->front().user_id(), alice.user);
    EXPECT_EQ(alice_txs->front().description(), "Alice salary");
}

TEST_F(RepositoryIntegrationTest, TransactionRepository_TimeRangeIsHalfOpen) {
    const auto ids = seed_user_with_accounts();
    const auto start = sample_time();
    const auto end = start + std::chrono::hours(1);
    auto write = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            for (const auto at : {start, end}) {
                auto saved = tx_repo_->save_single(
                    tx,
                    Transaction(
                        TransactionId{}, ids.user, ids.cash,
                        money("1", "USD"), TransactionType::Income, at));
                if (!saved) return std::unexpected(saved.error());
            }
            return {};
        });
    ASSERT_TRUE(write.has_value());

    auto result = tx_repo_->find_by_account(ids.cash, start, end);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ(result->front().occurred_at(), start);

    auto user_result = tx_repo_->find_by_user_in_range(
        ids.user, start, end);
    ASSERT_TRUE(user_result.has_value());
    ASSERT_EQ(user_result->size(), 1U);
    EXPECT_EQ(user_result->front().occurred_at(), start);
}

// ---- Optimistic locking ----

TEST_F(RepositoryIntegrationTest, AccountRepository_WhenVersionMismatch_ReturnsConflict) {
    auto ids = seed_user_with_accounts();
    auto current = account_repo_->find_by_id(ids.cash);
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->version(), 1);

    // First update with correct version succeeds and bumps version to 2.
    auto first = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        Account updated(
            current->id(),
            current->owner(),
            "Renamed Cash",
            current->type(),
            current->subtype(),
            current->currency(),
            current->description(),
            current->is_archived(),
            current->archived_at(),
            current->created_at(),
            sample_time(),
            current->version()); // expected current version
        auto r = account_repo_->save(tx, updated);
        if (!r) {
            return std::unexpected(r.error());
        }
        return {};
    });
    ASSERT_TRUE(first.has_value());

    auto after = account_repo_->find_by_id(ids.cash);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->version(), 2);
    EXPECT_EQ(after->name(), "Renamed Cash");

    // Stale update with old version fails.
    auto stale = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        Account stale_update(
            current->id(),
            current->owner(),
            "Stale Name",
            current->type(),
            current->subtype(),
            current->currency(),
            current->description(),
            current->is_archived(),
            current->archived_at(),
            current->created_at(),
            sample_time(),
            1); // stale version
        auto r = account_repo_->save(tx, stale_update);
        if (!r) {
            return std::unexpected(r.error());
        }
        return {};
    });
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().status, RepositoryStatus::Conflict);
}

// ---- Balance cache rebuild ----

TEST_F(RepositoryIntegrationTest, AccountRepository_WhenBalanceCacheMiss_RebuildsFromTransactions) {
    auto ids = seed_user_with_accounts();

    auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        Transaction income(
            TransactionId{},
            ids.user,
            ids.cash,
            money("1000", "USD"),
            TransactionType::Income,
            sample_time(),
            "Salary");
        auto a = tx_repo_->save_single(tx, income);
        if (!a) {
            return std::unexpected(a.error());
        }

        Transaction expense(
            TransactionId{},
            ids.user,
            ids.cash,
            money("250", "USD"),
            TransactionType::Expense,
            sample_time(),
            "Rent");
        auto b = tx_repo_->save_single(tx, expense);
        if (!b) {
            return std::unexpected(b.error());
        }
        return {};
    });
    ASSERT_TRUE(write.has_value());

    auto snapshot = account_repo_->balance_of(ids.cash);
    ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
    EXPECT_EQ(snapshot->balance.to_string(), "750 USD");

    // Second call should hit cache (same result).
    auto again = account_repo_->balance_of(ids.cash);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->balance.to_string(), "750 USD");
    EXPECT_TRUE(store_->balance_cache.contains(ids.cash.value()));
}

TEST_F(RepositoryIntegrationTest, TransactionRepository_WhenAmountExceedsNumericScale_RejectsWrite) {
    auto ids = seed_user_with_accounts();

    auto write = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            Transaction too_precise(
                TransactionId{},
                ids.user,
                ids.cash,
                money("1.123456789", "USD"),
                TransactionType::Income,
                sample_time(),
                "Direct repository boundary test");
            auto saved = tx_repo_->save_single(tx, too_precise);
            if (!saved) {
                return std::unexpected(saved.error());
            }
            return {};
        });

    ASSERT_FALSE(write.has_value());
    EXPECT_EQ(write.error().status, RepositoryStatus::ValidationError);
    EXPECT_TRUE(store_->transactions.empty());
}

// ---- Transfer aggregate atomic write ----

TEST_F(RepositoryIntegrationTest, TransactionRepository_WhenSavingTransfer_PersistsGroupAndBothSides) {
    auto ids = seed_user_with_accounts();

    auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto aggregate = TransferDomainService::build_from_both_amounts(
            money("100", "USD"),
            money("100", "USD"),
            ids.cash,
            ids.savings,
            ids.user,
            sample_time(),
            "Internal move",
            TransferGroupId{});
        if (!aggregate) {
            return std::unexpected(RepositoryError::validation(aggregate.error().message));
        }
        auto group = tx_repo_->save_transfer(tx, *aggregate);
        if (!group) {
            return std::unexpected(group.error());
        }
        return {};
    });
    ASSERT_TRUE(write.has_value()) << write.error().message;

    EXPECT_EQ(store_->transfer_groups.size(), 1u);
    auto all = tx_repo_->find_by_user(ids.user);
    ASSERT_TRUE(all.has_value());
    ASSERT_EQ(all->size(), 2u);
    for (const auto& tx : *all) {
        EXPECT_EQ(tx.type(), TransactionType::Transfer);
        EXPECT_TRUE(tx.transfer_group_id().has_value());
        EXPECT_TRUE(tx.transfer_group_id()->is_valid());
    }

    // Signed amount convention after persistence:
    // one outgoing (negative) and one incoming (positive).
    int negative_count = 0;
    int positive_count = 0;
    for (const auto& tx : *all) {
        if (tx.amount().is_negative()) {
            ++negative_count;
        } else if (tx.amount().is_positive()) {
            ++positive_count;
        }
    }
    EXPECT_EQ(negative_count, 1);
    EXPECT_EQ(positive_count, 1);

    // Persisted transfer group must carry the exact input mode (Mode 2 here),
    // not an inferred value.
    ASSERT_EQ(store_->transfer_groups.size(), 1u);
    EXPECT_EQ(store_->transfer_groups.begin()->second.transfer_mode,
              static_cast<int>(TransferMode::OutgoingAndIncoming));
}

TEST_F(RepositoryIntegrationTest, TransactionRepository_RejectsInvalidCreateShapes) {
    auto ids = seed_user_with_accounts();

    const auto expect_single_rejected = [&](const Transaction& candidate) {
        auto write = uow_->execute_in_transaction(
            [&](ITransactionContext& tx) -> RepositoryVoidResult {
                auto saved = tx_repo_->save_single(tx, candidate);
                return saved ? RepositoryVoidResult{}
                             : RepositoryVoidResult(std::unexpected(saved.error()));
            });
        ASSERT_FALSE(write.has_value());
        EXPECT_EQ(write.error().status, RepositoryStatus::ValidationError);
        EXPECT_TRUE(store_->transactions.empty());
    };

    // Public single-row persistence is create-only, non-grouped and non-zero;
    // Income/Expense accept positive magnitudes before storage sign mapping.
    expect_single_rejected(Transaction(
        TransactionId{}, ids.user, ids.cash, money("100", "USD"),
        TransactionType::Transfer, sample_time(), "orphan transfer leg"));
    expect_single_rejected(Transaction(
        TransactionId{99}, ids.user, ids.cash, money("1", "USD"),
        TransactionType::Income, sample_time(), "persisted id"));

    Transaction deleted(
        TransactionId{}, ids.user, ids.cash, money("1", "USD"),
        TransactionType::Income, sample_time(), "deleted row");
    deleted.mark_deleted(sample_time());
    expect_single_rejected(deleted);

    expect_single_rejected(Transaction(
        TransactionId{}, ids.user, ids.cash, money("-1", "USD"),
        TransactionType::Adjustment, sample_time(), "grouped adjustment",
        std::nullopt, TransferGroupId{}));
    expect_single_rejected(Transaction(
        TransactionId{}, ids.user, ids.cash, money("0", "USD"),
        TransactionType::Adjustment, sample_time(), "zero adjustment"));
    expect_single_rejected(Transaction(
        TransactionId{}, ids.user, ids.cash, money("-1", "USD"),
        TransactionType::Expense, sample_time(), "signed expense"));
    expect_single_rejected(Transaction(
        TransactionId{}, ids.user, ids.cash, money("-1", "USD"),
        TransactionType::Income, sample_time(), "negative income"));
    expect_single_rejected(Transaction(
        TransactionId{}, ids.user, ids.cash, money("1", "USD"),
        static_cast<TransactionType>(999), sample_time(), "invalid type"));

    // A valid group id denotes a persisted aggregate and must not be reused by
    // the create path, even when the Domain aggregate itself is consistent.
    auto persisted_group = TransferDomainService::build_from_both_amounts(
        money("10", "USD"), money("10", "USD"), ids.cash, ids.savings,
        ids.user, sample_time(), "persisted group", TransferGroupId{77});
    ASSERT_TRUE(persisted_group.has_value());
    auto group_write = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto saved = tx_repo_->save_transfer(tx, *persisted_group);
            return saved ? RepositoryVoidResult{}
                         : RepositoryVoidResult(std::unexpected(saved.error()));
        });
    ASSERT_FALSE(group_write.has_value());
    EXPECT_EQ(group_write.error().status, RepositoryStatus::ValidationError);
    EXPECT_TRUE(store_->transfer_groups.empty());
    EXPECT_TRUE(store_->transactions.empty());
}

TEST_F(RepositoryIntegrationTest, TransactionRepository_WhenSavingCrossCurrencyMode1_PersistsMode1) {
    auto ids = seed_user_with_accounts();

    auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        // Mode 1 cross-currency: outgoing USD + rate => incoming CNY.
        auto aggregate = TransferDomainService::build_from_outgoing_and_rate(
            money("1000", "USD"),
            ids.cash,
            ids.cny,
            rate("USD", "CNY", "7.18"),
            ids.user,
            sample_time(),
            "cross move",
            TransferGroupId{});
        if (!aggregate) {
            return std::unexpected(RepositoryError::validation(aggregate.error().message));
        }
        auto group = tx_repo_->save_transfer(tx, *aggregate);
        if (!group) {
            return std::unexpected(group.error());
        }
        return {};
    });
    ASSERT_TRUE(write.has_value()) << write.error().message;
    ASSERT_EQ(store_->transfer_groups.size(), 1u);
    // Cross-currency Mode 1 must persist as Mode 1, not be inferred as "cross => 1"
    // coincidentally; the value comes from the aggregate's recorded mode.
    EXPECT_EQ(store_->transfer_groups.begin()->second.transfer_mode,
              static_cast<int>(TransferMode::OutgoingAndRate));
}

TEST_F(RepositoryIntegrationTest, TransactionRepository_WhenSavingTransferFee_PersistsWholeAggregate) {
    auto ids = seed_user_with_accounts();

    auto write = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto aggregate = TransferDomainService::build_from_both_amounts(
                money("100", "USD"),
                money("100", "USD"),
                ids.cash,
                ids.savings,
                ids.user,
                sample_time(),
                "Internal move",
                TransferGroupId{},
                TransferFee{FeeSource::SourceAccount, ids.cash, money("2", "USD")});
            if (!aggregate) {
                return std::unexpected(RepositoryError::validation(
                    aggregate.error().message));
            }
            auto saved = tx_repo_->save_transfer(tx, *aggregate);
            if (!saved) return std::unexpected(saved.error());
            return {};
        });
    ASSERT_TRUE(write.has_value()) << write.error().message;

    ASSERT_EQ(store_->transfer_groups.size(), 1u);
    auto all = tx_repo_->find_by_user(ids.user);
    ASSERT_TRUE(all.has_value());
    ASSERT_EQ(all->size(), 3u);
    const auto group_id = store_->transfer_groups.begin()->second.id;
    std::size_t adjustment_count = 0;
    for (const auto& transaction : *all) {
        ASSERT_TRUE(transaction.transfer_group_id().has_value());
        EXPECT_EQ(*transaction.transfer_group_id(), group_id);
        if (transaction.type() == TransactionType::Adjustment) {
            ++adjustment_count;
            EXPECT_EQ(transaction.amount().to_string(), "-2 USD");
            EXPECT_EQ(transaction.account_id(), ids.cash);
        }
    }
    EXPECT_EQ(adjustment_count, 1u);
}

TEST_F(RepositoryIntegrationTest, TransactionRepository_WhenTransferFeeWriteRollsBack_LeavesNoRows) {
    auto ids = seed_user_with_accounts();

    auto write = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto aggregate = TransferDomainService::build_from_both_amounts(
                money("100", "USD"),
                money("100", "USD"),
                ids.cash,
                ids.savings,
                ids.user,
                sample_time(),
                "Internal move",
                TransferGroupId{},
                TransferFee{FeeSource::ThirdParty, ids.cny, money("5", "CNY")});
            if (!aggregate) {
                return std::unexpected(RepositoryError::validation(
                    aggregate.error().message));
            }
            auto saved = tx_repo_->save_transfer(tx, *aggregate);
            if (!saved) return std::unexpected(saved.error());
            return std::unexpected(RepositoryError::validation("force rollback"));
        });

    ASSERT_FALSE(write.has_value());
    EXPECT_TRUE(store_->transfer_groups.empty());
    EXPECT_TRUE(store_->transactions.empty());
}

TEST_F(RepositoryIntegrationTest, TransactionRepository_WhenPurgingTransfer_DeletesGroupedFee) {
    auto ids = seed_user_with_accounts();

    auto create = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            auto aggregate = TransferDomainService::build_from_both_amounts(
                money("100", "USD"),
                money("100", "USD"),
                ids.cash,
                ids.savings,
                ids.user,
                sample_time(),
                "Internal move",
                TransferGroupId{},
                TransferFee{FeeSource::ThirdParty, ids.cny, money("5", "CNY")});
            if (!aggregate) {
                return std::unexpected(RepositoryError::validation(
                    aggregate.error().message));
            }
            auto saved = tx_repo_->save_transfer(tx, *aggregate);
            return saved ? RepositoryVoidResult{} : std::unexpected(saved.error());
        });
    ASSERT_TRUE(create.has_value()) << create.error().message;
    ASSERT_EQ(store_->transactions.size(), 3u);

    auto purge = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            // Purging the fee-only third-party account must still remove the
            // entire transfer aggregate, not just its Adjustment row.
            return tx_repo_->physical_delete_transfers_touching_account(tx, ids.cny);
        });
    ASSERT_TRUE(purge.has_value()) << purge.error().message;
    EXPECT_TRUE(store_->transfer_groups.empty());
    EXPECT_TRUE(store_->transactions.empty());
}

// ---- Exchange rate append-only + historical query ----

TEST_F(RepositoryIntegrationTest, ExchangeRateRepository_WhenAppending_NeverOverwritesHistory) {
    auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        auto r1 = rate_repo_->append(tx, rate("USD", "CNY", "7.10", 1000, "ECB"));
        if (!r1) {
            return std::unexpected(r1.error());
        }
        auto r2 = rate_repo_->append(tx, rate("USD", "CNY", "7.25", 2000, "ECB"));
        if (!r2) {
            return std::unexpected(r2.error());
        }
        return {};
    });
    ASSERT_TRUE(write.has_value());
    EXPECT_EQ(store_->exchange_rates.size(), 2u);

    auto all = rate_repo_->find_all_for_pair(ccy("USD"), ccy("CNY"));
    ASSERT_TRUE(all.has_value());
    ASSERT_EQ(all->size(), 2u);
}

TEST_F(RepositoryIntegrationTest, ExchangeRateRepository_WhenQueryingHistorical_UsesLatestAtOrBeforeTarget) {
    auto write = uow_->execute_in_transaction([&](ITransactionContext& tx) -> RepositoryVoidResult {
        if (auto r = rate_repo_->append(tx, rate("USD", "CNY", "7.10", 1000)); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = rate_repo_->append(tx, rate("USD", "CNY", "7.20", 2000)); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = rate_repo_->append(tx, rate("USD", "CNY", "7.30", 3000)); !r) {
            return std::unexpected(r.error());
        }
        return {};
    });
    ASSERT_TRUE(write.has_value());

    // Target time between second and third snapshot => second rate.
    auto historical = rate_repo_->find_historical(ccy("USD"), ccy("CNY"), time_at(2500));
    ASSERT_TRUE(historical.has_value()) << historical.error().message;
    EXPECT_EQ(historical->rate().to_string(), "7.2");

    auto latest = rate_repo_->find_latest(ccy("USD"), ccy("CNY"));
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->rate().to_string(), "7.3");
}

TEST_F(RepositoryIntegrationTest, ExchangeRateRepository_HistoryIncludesAnchorAndRange) {
    auto write = uow_->execute_in_transaction(
        [&](ITransactionContext& tx) -> RepositoryVoidResult {
            for (const auto& value : {
                     rate("USD", "CNY", "7.1", 1000),
                     rate("USD", "CNY", "7.2", 2000),
                     rate("USD", "CNY", "7.3", 3000),
                     rate("USD", "CNY", "7.4", 4000)}) {
                auto appended = rate_repo_->append(tx, value);
                if (!appended) return std::unexpected(appended.error());
            }
            return {};
        });
    ASSERT_TRUE(write.has_value()) << write.error().message;

    auto history = rate_repo_->find_history_for_pair(
        ccy("USD"), ccy("CNY"), time_at(2500), time_at(4000));
    ASSERT_TRUE(history.has_value()) << history.error().message;
    ASSERT_EQ(history->size(), 3U);
    EXPECT_EQ((*history)[0].rate().to_string(), "7.2");
    EXPECT_EQ((*history)[1].rate().to_string(), "7.3");
    EXPECT_EQ((*history)[2].rate().to_string(), "7.4");
}

// ---- Preference fallback ----

TEST_F(RepositoryIntegrationTest, UserPreferenceRepository_WhenMissingRow_FallsBackToUserBaseCurrency) {
    auto ids = seed_user_with_accounts();
    auto pref = pref_repo_->find_by_user(ids.user);
    ASSERT_TRUE(pref.has_value()) << pref.error().message;
    EXPECT_EQ(pref->base_currency().code(), "USD");
    // Defaults now match the DB schema (user_preferences.locale/timezone).
    EXPECT_EQ(pref->locale(), "zh-CN");
    EXPECT_EQ(pref->timezone(), "Asia/Shanghai");
}

} // namespace pfh::test
