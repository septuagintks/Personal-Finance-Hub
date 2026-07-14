// Personal Finance Hub - Outbox Publisher and Handler Tests

#include "pfh/application/events/local_event_bus.h"
#include "pfh/application/events/outbox_publisher.h"
#include "pfh/application/events/supplemental_audit_handler.h"
#include "pfh/infrastructure/persistence/in_memory_outbox_repository.h"
#include "pfh/infrastructure/persistence/in_memory_supplemental_audit_store.h"

#include <gtest/gtest.h>

#include <barrier>
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace pfh::application {
namespace {

using namespace std::chrono_literals;
using infrastructure::InMemoryOutboxRepository;
using infrastructure::InMemoryStore;
using infrastructure::InMemorySupplementalAuditStore;

class MutableClock final : public IClock {
public:
    explicit MutableClock(std::chrono::system_clock::time_point now)
        : now_(now) {}

    [[nodiscard]] std::chrono::system_clock::time_point now() const override {
        return now_;
    }

    void advance(std::chrono::seconds duration) { now_ += duration; }

private:
    std::chrono::system_clock::time_point now_;
};

class AlwaysFailingHandler final : public IEventHandler {
public:
    [[nodiscard]] std::string_view handler_name() const noexcept override {
        return "AlwaysFailingHandler";
    }

    [[nodiscard]] bool handles(std::string_view) const noexcept override {
        return true;
    }

    [[nodiscard]] EventHandlingResult handle(
        const OutboxMessage&) override {
        return std::unexpected(EventHandlingError{
            "provider temporarily down",
            {}});
    }
};

class FlakySupplementalAuditStore final : public ISupplementalAuditStore {
public:
    explicit FlakySupplementalAuditStore(ISupplementalAuditStore& delegate)
        : delegate_(delegate) {}

    [[nodiscard]] domain::RepositoryResult<bool> append_once(
        std::string_view outbox_id,
        std::string_view handler_name,
        const domain::AuditLogEntry& entry) override {
        if (fail_next_) {
            fail_next_ = false;
            return std::unexpected(domain::RepositoryError::database(
                "supplemental audit unavailable"));
        }
        return delegate_.append_once(outbox_id, handler_name, entry);
    }

private:
    ISupplementalAuditStore& delegate_;
    bool fail_next_ = true;
};

[[nodiscard]] OutboxMessage message(
    std::string id,
    std::chrono::system_clock::time_point now) {
    OutboxMessage result;
    result.id = std::move(id);
    result.event_name = "ExchangeRateRefreshed";
    result.aggregate_type = "ExchangeRate";
    result.aggregate_id = "USD/CNY";
    result.payload_json = R"({"baseCurrency":"USD","targetCurrency":"CNY"})";
    result.next_retry_at = now;
    result.occurred_at = now;
    result.created_at = now;
    return result;
}

TEST(OutboxRepositoryTest, ConcurrentClaimsNeverShareOwnership) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    InMemoryStore store;
    for (int index = 0; index < 20; ++index) {
        store.outbox.push_back(message("event-" + std::to_string(index), now));
    }
    InMemoryOutboxRepository first_repository(store);
    InMemoryOutboxRepository second_repository(store);
    domain::RepositoryResult<OutboxClaimBatch> first =
        std::unexpected(domain::RepositoryError::database("not started"));
    domain::RepositoryResult<OutboxClaimBatch> second =
        std::unexpected(domain::RepositoryError::database("not started"));
    std::barrier start(3);

    std::jthread first_worker([&] {
        start.arrive_and_wait();
        first = first_repository.claim_due(now, 5min, 10, "worker-a");
    });
    std::jthread second_worker([&] {
        start.arrive_and_wait();
        second = second_repository.claim_due(now, 5min, 10, "worker-b");
    });
    start.arrive_and_wait();
    first_worker.join();
    second_worker.join();

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->claimed.size(), 10U);
    EXPECT_EQ(second->claimed.size(), 10U);

    std::set<std::string> claimed_ids;
    std::set<std::string> claim_tokens;
    for (const auto& claimed : first->claimed) {
        claimed_ids.insert(claimed.id);
        claim_tokens.insert(claimed.claim_token);
    }
    for (const auto& claimed : second->claimed) {
        claimed_ids.insert(claimed.id);
        claim_tokens.insert(claimed.claim_token);
    }
    EXPECT_EQ(claimed_ids.size(), 20U);
    EXPECT_EQ(claim_tokens.size(), 20U);
}

TEST(OutboxRepositoryTest, ExpiredLeaseIsRecoveredOrDeadLettered) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    InMemoryStore store;

    auto retryable = message("retryable", now);
    retryable.status = OutboxStatus::Processing;
    retryable.locked_at = now - 6min;
    retryable.locked_by = "crashed-worker";
    retryable.claim_token = "stale-token-a";
    store.outbox.push_back(retryable);

    auto exhausted = message("exhausted", now);
    exhausted.status = OutboxStatus::Processing;
    exhausted.retry_count = 4;
    exhausted.max_retry_count = 5;
    exhausted.locked_at = now - 6min;
    exhausted.locked_by = "crashed-worker";
    exhausted.claim_token = "stale-token-b";
    store.outbox.push_back(exhausted);

    InMemoryOutboxRepository repository(store);
    auto claimed = repository.claim_due(now, 5min, 10, "replacement-worker");

    ASSERT_TRUE(claimed.has_value());
    ASSERT_EQ(claimed->claimed.size(), 1U);
    EXPECT_EQ(claimed->claimed.front().id, "retryable");
    EXPECT_EQ(claimed->claimed.front().retry_count, 1);
    EXPECT_EQ(claimed->claimed.front().locked_by, "replacement-worker");
    ASSERT_EQ(claimed->recovered_dead_letters.size(), 1U);
    EXPECT_EQ(claimed->recovered_dead_letters.front().id, "exhausted");
    EXPECT_EQ(store.outbox[1].status, OutboxStatus::DeadLetter);
    EXPECT_TRUE(store.outbox[1].claim_token.empty());
}

TEST(OutboxPublisherTest, FailureUsesBoundedBackoffThenDeadLetters) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    InMemoryStore store;
    auto pending = message("event-1", now);
    pending.max_retry_count = 2;
    store.outbox.push_back(pending);

    MutableClock clock(now);
    InMemoryOutboxRepository repository(store);
    LocalEventBus event_bus;
    event_bus.subscribe(std::make_shared<AlwaysFailingHandler>());
    OutboxPublisher publisher(repository, event_bus, clock);

    auto first = publisher.run_once("worker-a");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->claimed, 1U);
    EXPECT_EQ(first->failed, 1U);
    EXPECT_EQ(store.outbox.front().status, OutboxStatus::Failed);
    EXPECT_EQ(store.outbox.front().retry_count, 1);
    EXPECT_EQ(store.outbox.front().next_retry_at, now + 1min);

    clock.advance(59s);
    auto too_early = publisher.run_once("worker-a");
    ASSERT_TRUE(too_early.has_value());
    EXPECT_EQ(too_early->claimed, 0U);

    clock.advance(1s);
    auto second = publisher.run_once("worker-a");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->claimed, 1U);
    EXPECT_EQ(second->dead_lettered, 1U);
    EXPECT_EQ(store.outbox.front().status, OutboxStatus::DeadLetter);
    EXPECT_EQ(store.outbox.front().retry_count, 2);
    EXPECT_EQ(store.outbox.front().last_error, "provider temporarily down");
    EXPECT_EQ(
        store.outbox.front().last_failed_handler,
        "AlwaysFailingHandler");
    EXPECT_EQ(store.outbox.front().last_failed_at, now + 1min);
}

TEST(OutboxPublisherTest, SuccessfulDeliveryPublishesAndAuditsExactlyOnce) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    InMemoryStore store;
    store.outbox.push_back(message("event-1", now));
    MutableClock clock(now);
    InMemoryOutboxRepository repository(store);
    InMemorySupplementalAuditStore audit_store(store);
    auto handler = std::make_shared<SupplementalAuditHandler>(audit_store);
    LocalEventBus event_bus;
    event_bus.subscribe(handler);
    OutboxPublisher publisher(
        repository, event_bus, clock, {}, handler.get());

    auto first = publisher.run_once("worker-a");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->claimed, 1U);
    EXPECT_EQ(first->published, 1U);
    ASSERT_EQ(store.audit_logs.size(), 1U);
    EXPECT_EQ(store.outbox.front().status, OutboxStatus::Published);

    auto second = publisher.run_once("worker-a");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->claimed, 0U);
    EXPECT_EQ(store.audit_logs.size(), 1U);

    auto without_subscriber = message("event-2", now);
    without_subscriber.event_name = "TransactionCreated";
    store.outbox.push_back(without_subscriber);
    auto no_op = publisher.run_once("worker-a");
    ASSERT_TRUE(no_op.has_value());
    EXPECT_EQ(no_op->published, 1U);
    EXPECT_EQ(store.outbox.back().status, OutboxStatus::Published);
    EXPECT_EQ(store.audit_logs.size(), 1U);
}

TEST(OutboxRepositoryTest, StaleClaimTokenCannotCompleteNewLease) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    InMemoryStore store;
    store.outbox.push_back(message("event-1", now));
    InMemoryOutboxRepository repository(store);

    auto first = repository.claim_due(now, 5min, 1, "worker-a");
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->claimed.size(), 1U);
    const auto stale_token = first->claimed.front().claim_token;

    auto failed = repository.mark_failed(
        "event-1", stale_token, "TestHandler", "temporary", now, now);
    ASSERT_TRUE(failed.has_value());
    auto second = repository.claim_due(now, 5min, 1, "worker-b");
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(second->claimed.size(), 1U);
    const auto current_token = second->claimed.front().claim_token;
    ASSERT_NE(current_token, stale_token);

    auto stale_completion = repository.mark_published(
        "event-1", stale_token, now);
    ASSERT_FALSE(stale_completion.has_value());
    EXPECT_EQ(stale_completion.error().status, domain::RepositoryStatus::Conflict);
    EXPECT_EQ(store.outbox.front().status, OutboxStatus::Processing);

    EXPECT_TRUE(repository.mark_published("event-1", current_token, now));
    EXPECT_EQ(store.outbox.front().status, OutboxStatus::Published);
}

TEST(SupplementalAuditHandlerTest, DuplicateDeliveryAppendsOneAudit) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    InMemoryStore store;
    InMemorySupplementalAuditStore audit_store(store);
    SupplementalAuditHandler handler(audit_store);
    const auto event = message("event-1", now);

    EXPECT_TRUE(handler.handle(event));
    EXPECT_TRUE(handler.handle(event));
    ASSERT_EQ(store.audit_logs.size(), 1U);
    EXPECT_EQ(store.audit_logs.front().actor_type,
              domain::AuditActorType::System);
    EXPECT_FALSE(store.audit_logs.front().operator_user_id.has_value());
    EXPECT_EQ(store.outbox_handler_receipts.size(), 1U);
}

TEST(SupplementalAuditHandlerTest, FailedDeadLetterAuditIsRetriedIdempotently) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    InMemoryStore store;
    auto dead = message("dead-event", now);
    dead.status = OutboxStatus::DeadLetter;
    dead.retry_count = dead.max_retry_count;
    dead.last_error = "delivery exhausted";
    store.outbox.push_back(dead);

    MutableClock clock(now);
    InMemoryOutboxRepository repository(store);
    InMemorySupplementalAuditStore durable_store(store);
    FlakySupplementalAuditStore flaky_store(durable_store);
    SupplementalAuditHandler handler(flaky_store);
    LocalEventBus event_bus;
    OutboxPublisher publisher(repository, event_bus, clock, {}, &handler);

    auto first = publisher.run_once("worker-a");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->audit_failures, 1U);
    EXPECT_TRUE(store.audit_logs.empty());

    auto second = publisher.run_once("worker-a");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->dead_letters_audited, 1U);
    ASSERT_EQ(store.audit_logs.size(), 1U);

    auto third = publisher.run_once("worker-a");
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(third->dead_letters_audited, 0U);
    EXPECT_EQ(store.audit_logs.size(), 1U);
}

TEST(SupplementalAuditHandlerTest,
     DeadLetterReceiptIsIndependentFromNormalDeliveryReceipt) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    InMemoryStore store;
    auto dead = message("dead-event", now);
    dead.status = OutboxStatus::DeadLetter;
    dead.retry_count = dead.max_retry_count;
    dead.last_error = "downstream handler failed";
    dead.last_failed_at = now;
    store.outbox.push_back(dead);

    MutableClock clock(now);
    InMemoryOutboxRepository repository(store);
    InMemorySupplementalAuditStore audit_store(store);
    SupplementalAuditHandler handler(audit_store);
    LocalEventBus event_bus;

    ASSERT_TRUE(handler.handle(dead));
    ASSERT_EQ(store.audit_logs.size(), 1U);
    OutboxPublisher publisher(repository, event_bus, clock, {}, &handler);
    auto published = publisher.run_once("worker-a");

    ASSERT_TRUE(published.has_value());
    EXPECT_EQ(published->dead_letters_audited, 1U);
    EXPECT_EQ(store.audit_logs.size(), 2U);
    EXPECT_EQ(store.outbox_handler_receipts.size(), 2U);
}

} // namespace
} // namespace pfh::application
