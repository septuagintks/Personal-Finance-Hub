// Personal Finance Hub - Foundational Resource API Tests

#include "pfh/application/services/auth_service.h"
#include "pfh/application/services/finance_application_service.h"
#include "pfh/infrastructure/persistence/in_memory_audit_log_repository.h"
#include "pfh/infrastructure/persistence/in_memory_auth_session_repository.h"
#include "pfh/infrastructure/persistence/in_memory_registration_defaults_repository.h"
#include "pfh/infrastructure/persistence/in_memory_request_scope.h"
#include "pfh/infrastructure/persistence/in_memory_operations_repository.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include "pfh/infrastructure/persistence/in_memory_unit_of_work_factory.h"
#include "pfh/infrastructure/persistence/in_memory_user_repository.h"
#include "pfh/presentation/api_application.h"

#include "test_support.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace pfh::test {

using namespace application;
using namespace domain;
using namespace infrastructure;
using namespace presentation;

class ResourceClock final : public IClock {
public:
    [[nodiscard]] AuthTimePoint now() const override {
        return std::chrono::system_clock::from_time_t(1'783'987'200);
    }
};

class ResourceHasher final : public IPasswordHasher {
public:
    [[nodiscard]] Result<std::string> hash(
        std::string_view password) const override {
        return "hash:" + std::string(password);
    }
    [[nodiscard]] Result<bool> verify(
        std::string_view password,
        std::string_view encoded_hash) const override {
        return encoded_hash == "hash:" + std::string(password);
    }
};

class ResourceTokenService final : public ITokenService {
public:
    [[nodiscard]] Result<IssuedAccessToken> issue_access_token(
        UserId user_id,
        UserRole role,
        std::string_view session_id,
        AuthTimePoint issued_at) const override {
        AccessTokenClaims claims;
        claims.issuer = "pfh-api";
        claims.audience = "pfh-client";
        claims.user_id = user_id;
        claims.role = role;
        claims.session_id = std::string(session_id);
        claims.token_id = "resource-jti-" + std::to_string(++sequence_);
        claims.issued_at = issued_at;
        claims.not_before = issued_at;
        claims.expires_at = issued_at + access_token_lifetime();
        const auto token = "resource-jwt-" + claims.token_id;
        claims_.insert_or_assign(token, claims);
        return IssuedAccessToken{token, claims};
    }

    [[nodiscard]] Result<AccessTokenClaims> validate_access_token(
        std::string_view token,
        AuthTimePoint now) const override {
        const auto found = claims_.find(std::string(token));
        if (found == claims_.end() || found->second.expires_at <= now) {
            return err(Error::unauthorized());
        }
        return found->second;
    }

    [[nodiscard]] Result<std::string> generate_opaque_token(
        std::size_t bytes) const override {
        return "resource-refresh-" + std::to_string(bytes) + "-" +
               std::to_string(++sequence_);
    }

    [[nodiscard]] Result<std::string> hash_opaque_token(
        std::string_view token) const override {
        return "digest:" + std::string(token);
    }

    [[nodiscard]] std::chrono::seconds access_token_lifetime() const noexcept override {
        return std::chrono::seconds(900);
    }
    [[nodiscard]] std::chrono::seconds refresh_token_lifetime() const noexcept override {
        return std::chrono::seconds(2'592'000);
    }

private:
    mutable int sequence_ = 0;
    mutable std::map<std::string, AccessTokenClaims> claims_;
};

class ResourceApiTest : public ::testing::Test {
protected:
    ResourceApiTest()
        : auth_uow_factory_(store_),
          users_(store_),
          defaults_(store_),
          sessions_(store_),
          auth_audits_(store_),
          auth_service_(
              users_, users_, defaults_, sessions_, auth_audits_,
              auth_uow_factory_, hasher_, tokens_, clock_),
          scopes_(store_),
          finance_(scopes_, clock_, request_hasher_),
          operations_repository_(store_),
          operations_service_(
              operations_repository_, clock_, {}, false, 10),
          auth_controller_(auth_service_),
          account_controller_(finance_),
          category_controller_(finance_),
          tag_controller_(finance_),
          preference_controller_(finance_),
          currency_controller_(finance_),
          transaction_controller_(finance_),
          transfer_controller_(finance_),
          report_controller_(finance_),
          maintenance_controller_(finance_),
          operations_controller_(operations_service_),
          jwt_filter_(tokens_, sessions_, users_, clock_),
          app_(
              auth_controller_, jwt_filter_, account_controller_,
              category_controller_, tag_controller_, preference_controller_,
              currency_controller_, transaction_controller_, transfer_controller_,
              report_controller_, &maintenance_controller_,
              &operations_controller_) {}

    [[nodiscard]] nlohmann::json register_user(std::string username) {
        const auto response = request(
            HttpMethod::Post,
            "/api/v1/auth/register",
            {{"username", std::move(username)},
             {"password", "correct horse battery staple"}});
        EXPECT_EQ(response.status, 201) << response.body;
        return nlohmann::json::parse(response.body);
    }

    [[nodiscard]] HttpResponse request(
        HttpMethod method,
        std::string path,
        nlohmann::json body = nlohmann::json::object(),
        std::string token = {},
        std::map<std::string, std::string> query = {},
        std::map<std::string, std::string> headers = {}) {
        HttpRequest value;
        value.method = method;
        value.path = std::move(path);
        value.query = std::move(query);
        value.headers = std::move(headers);
        if (method == HttpMethod::Post || method == HttpMethod::Put) {
            value.body = body.dump();
        }
        if (!token.empty()) {
            value.headers.insert_or_assign("Authorization", "Bearer " + token);
        }
        if (method == HttpMethod::Post &&
            (value.path == "/api/v1/accounts" ||
             value.path == "/api/v1/categories" ||
             value.path == "/api/v1/tags" ||
             value.path == "/api/v1/transactions" ||
             value.path == "/api/v1/transfers") &&
            !value.header("Idempotency-Key").has_value()) {
            value.headers.emplace(
                "Idempotency-Key",
                "resource-test-" + std::to_string(++request_sequence_));
        }
        return app_.handle(std::move(value));
    }

    [[nodiscard]] nlohmann::json create_account(const std::string& token) {
        const auto response = request(
            HttpMethod::Post,
            "/api/v1/accounts",
            {{"name", "Daily Wallet"},
             {"type", "digital_wallet"},
             {"subtype", "wallet"},
             {"currencyCode", "CNY"},
             {"description", ""}},
            token);
        EXPECT_EQ(response.status, 201) << response.body;
        return nlohmann::json::parse(response.body);
    }

    InMemoryStore store_;
    InMemoryUnitOfWorkFactory auth_uow_factory_;
    InMemoryUserRepository users_;
    InMemoryRegistrationDefaultsRepository defaults_;
    InMemoryAuthSessionRepository sessions_;
    InMemoryAuditLogRepository auth_audits_;
    ResourceHasher hasher_;
    ResourceTokenService tokens_;
    ResourceClock clock_;
    DeterministicRequestHasher request_hasher_;
    AuthService auth_service_;
    InMemoryRequestScopeFactory scopes_;
    FinanceApplicationService finance_;
    InMemoryOperationsRepository operations_repository_;
    OperationsApplicationService operations_service_;
    AuthController auth_controller_;
    AccountController account_controller_;
    CategoryController category_controller_;
    TagController tag_controller_;
    PreferenceController preference_controller_;
    CurrencyController currency_controller_;
    TransactionController transaction_controller_;
    TransferController transfer_controller_;
    ReportController report_controller_;
    MaintenanceController maintenance_controller_;
    OperationsController operations_controller_;
    JwtFilter jwt_filter_;
    ApiApplication app_;
    std::uint64_t request_sequence_ = 0;
};

TEST_F(ResourceApiTest, AccountLifecycleFiltersAndOptimisticConcurrencyAreEnforced) {
    const auto alice = register_user("alice-resources@example.com");
    const auto bob = register_user("bob-resources@example.com");
    const auto alice_token = alice["accessToken"].get<std::string>();
    const auto bob_token = bob["accessToken"].get<std::string>();
    const auto account = create_account(alice_token);
    const auto id = account["id"].get<std::int64_t>();
    const auto path = "/api/v1/accounts/" + std::to_string(id);
    EXPECT_EQ(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            store_.accounts.at(id).created_at().time_since_epoch()).count() %
            1000,
        0);

    const auto detail = request(HttpMethod::Get, path, {}, alice_token);
    ASSERT_EQ(detail.status, 200) << detail.body;
    EXPECT_EQ(detail.headers.at("Cache-Control"), "no-store");
    EXPECT_EQ(detail.headers.at("ETag"), R"("1")");
    const auto detail_body = nlohmann::json::parse(detail.body);
    EXPECT_EQ(detail_body["version"], 1);
    EXPECT_FALSE(detail_body["createdAt"].get<std::string>().empty());
    EXPECT_FALSE(detail_body["updatedAt"].get<std::string>().empty());
    EXPECT_TRUE(detail_body["archivedAt"].is_null());
    EXPECT_EQ(request(HttpMethod::Get, path, {}, bob_token).status, 404);

    const auto balance = request(
        HttpMethod::Get,
        path + "/balance",
        {}, alice_token);
    ASSERT_EQ(balance.status, 200) << balance.body;
    EXPECT_EQ(nlohmann::json::parse(balance.body)["balance"], "0");

    const nlohmann::json update_body{
        {"name", "Primary Wallet"},
        {"type", "digital_wallet"},
        {"subtype", "mobile wallet"},
        {"category", "asset"},
        {"currencyCode", "USD"},
        {"description", "Daily spending"}};
    auto missing_description = update_body;
    missing_description.erase("description");
    EXPECT_EQ(request(
        HttpMethod::Put, path, missing_description, alice_token, {},
        {{"If-Match", R"("1")"}}).status, 400);

    const auto updated = request(
        HttpMethod::Put, path, update_body, alice_token, {},
        {{"If-Match", R"("1")"}});
    ASSERT_EQ(updated.status, 200) << updated.body;
    EXPECT_EQ(updated.headers.at("ETag"), R"("2")");
    const auto updated_body = nlohmann::json::parse(updated.body);
    EXPECT_EQ(updated_body["name"], "Primary Wallet");
    EXPECT_EQ(updated_body["currencyCode"], "USD");
    EXPECT_EQ(updated_body["version"], 2);
    EXPECT_EQ(store_.outbox.back().event_name, "AccountUpdated");
    EXPECT_EQ(store_.audit_logs.back().action, AuditAction::Update);
    const auto before_audit = nlohmann::json::parse(
        store_.audit_logs.back().before_value_json);
    const auto after_audit = nlohmann::json::parse(
        store_.audit_logs.back().after_value_json);
    EXPECT_EQ(before_audit["name"], "Daily Wallet");
    EXPECT_EQ(before_audit["type"], "digital_wallet");
    EXPECT_EQ(before_audit["subtype"], "wallet");
    EXPECT_EQ(before_audit["category"], "asset");
    EXPECT_EQ(before_audit["currencyCode"], "CNY");
    EXPECT_EQ(before_audit["description"], "");
    EXPECT_FALSE(before_audit["isArchived"]);
    EXPECT_EQ(after_audit["name"], "Primary Wallet");
    EXPECT_EQ(after_audit["subtype"], "mobile wallet");
    EXPECT_EQ(after_audit["currencyCode"], "USD");
    EXPECT_EQ(after_audit["description"], "Daily spending");

    EXPECT_EQ(request(
        HttpMethod::Put, path, update_body, alice_token, {},
        {{"If-Match", R"("1")"}}).status, 409);
    EXPECT_EQ(request(
        HttpMethod::Put, path, update_body, bob_token, {},
        {{"If-Match", R"("2")"}}).status, 404);

    const auto archive = request(
        HttpMethod::Post,
        path + "/archive", {}, alice_token, {},
        {{"If-Match", R"("2")"}});
    EXPECT_EQ(archive.status, 204) << archive.body;
    ASSERT_TRUE(store_.accounts.contains(id));
    EXPECT_TRUE(store_.accounts.at(id).is_archived());
    EXPECT_EQ(store_.accounts.at(id).version(), 3);
    EXPECT_EQ(store_.audit_logs.back().action, AuditAction::Archive);
    EXPECT_EQ(store_.outbox.back().event_name, "AccountArchived");

    const auto active = request(
        HttpMethod::Get, "/api/v1/accounts", {}, alice_token,
        {{"status", "active"}});
    const auto archived = request(
        HttpMethod::Get, "/api/v1/accounts", {}, alice_token,
        {{"status", "archived"}});
    const auto all = request(
        HttpMethod::Get, "/api/v1/accounts", {}, alice_token,
        {{"status", "all"}});
    ASSERT_EQ(active.status, 200) << active.body;
    ASSERT_EQ(archived.status, 200) << archived.body;
    ASSERT_EQ(all.status, 200) << all.body;
    EXPECT_TRUE(nlohmann::json::parse(active.body).empty());
    ASSERT_EQ(nlohmann::json::parse(archived.body).size(), 1U);
    ASSERT_EQ(nlohmann::json::parse(all.body).size(), 1U);
    EXPECT_EQ(request(
        HttpMethod::Get, "/api/v1/accounts", {}, alice_token,
        {{"status", "unknown"}}).status, 400);

    EXPECT_EQ(request(
        HttpMethod::Post, path + "/restore", {}, alice_token, {},
        {{"If-Match", R"("2")"}}).status, 409);
    const auto restore = request(
        HttpMethod::Post, path + "/restore", {}, alice_token, {},
        {{"If-Match", R"("3")"}});
    ASSERT_EQ(restore.status, 204) << restore.body;
    EXPECT_FALSE(store_.accounts.at(id).is_archived());
    EXPECT_EQ(store_.accounts.at(id).version(), 4);
    EXPECT_EQ(store_.audit_logs.back().action, AuditAction::Update);
    EXPECT_EQ(store_.outbox.back().event_name, "AccountRestored");

    const auto restored_detail = request(HttpMethod::Get, path, {}, alice_token);
    ASSERT_EQ(restored_detail.status, 200) << restored_detail.body;
    EXPECT_EQ(restored_detail.headers.at("ETag"), R"("4")");
    EXPECT_EQ(request(
        HttpMethod::Post, path + "/restore", {}, alice_token, {},
        {{"If-Match", R"("4")"}}).status, 409);
}

TEST_F(ResourceApiTest, AccountCurrencyFreezesAfterDeletedTransactionHistory) {
    const auto user = register_user("account-currency@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto account = create_account(token);
    const auto id = account["id"].get<std::int64_t>();
    const auto path = "/api/v1/accounts/" + std::to_string(id);
    const nlohmann::json usd_update{
        {"name", "Dollar Wallet"},
        {"type", "digital_wallet"},
        {"subtype", "wallet"},
        {"category", "asset"},
        {"currencyCode", "USD"},
        {"description", ""}};

    const auto updated = request(
        HttpMethod::Put, path, usd_update, token, {},
        {{"If-Match", R"("1")"}});
    ASSERT_EQ(updated.status, 200) << updated.body;
    ASSERT_EQ(nlohmann::json::parse(updated.body)["version"], 2);

    const auto created = request(
        HttpMethod::Post, "/api/v1/transactions",
        {{"accountId", id}, {"type", "income"}, {"amount", "10"},
         {"currencyCode", "USD"}}, token);
    ASSERT_EQ(created.status, 201) << created.body;
    const auto transaction_id =
        nlohmann::json::parse(created.body)["id"].get<std::int64_t>();
    ASSERT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/transactions/" + std::to_string(transaction_id),
        {}, token).status, 204);
    ASSERT_TRUE(store_.transactions.at(transaction_id).is_deleted());

    auto cny_update = usd_update;
    cny_update["currencyCode"] = "CNY";
    const auto rejected = request(
        HttpMethod::Put, path, cny_update, token, {},
        {{"If-Match", R"("2")"}});
    ASSERT_EQ(rejected.status, 422) << rejected.body;
    EXPECT_EQ(store_.accounts.at(id).currency().code(), "USD");
    EXPECT_EQ(store_.accounts.at(id).version(), 2);
}

TEST_F(ResourceApiTest, ArchivedAccountsRejectTransactionsAndTransfers) {
    const auto user = register_user("archived-account-writes@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto archived = create_account(token);
    const auto archived_id = archived["id"].get<std::int64_t>();
    const auto archived_path =
        "/api/v1/accounts/" + std::to_string(archived_id);
    ASSERT_EQ(request(
        HttpMethod::Post, archived_path + "/archive", {}, token, {},
        {{"If-Match", R"("1")"}}).status, 204);

    const auto transaction = request(
        HttpMethod::Post, "/api/v1/transactions",
        {{"accountId", archived_id}, {"type", "income"},
         {"amount", "1"}, {"currencyCode", "CNY"}}, token);
    EXPECT_EQ(transaction.status, 422) << transaction.body;

    const auto active = create_account(token);
    const auto transfer = request(
        HttpMethod::Post, "/api/v1/transfers",
        {{"sourceAccountId", archived_id}, {"targetAccountId", active["id"]},
         {"mode", "BothAmounts"}, {"outgoingAmount", "1"},
         {"incomingAmount", "1"}}, token);
    EXPECT_EQ(transfer.status, 422) << transfer.body;
}

TEST_F(ResourceApiTest, DangerousDeleteRequiresExactConfirmationCount) {
    const auto user = register_user("delete-account@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto account = create_account(token);
    const auto path = "/api/v1/accounts/" +
        std::to_string(account["id"].get<std::int64_t>());

    EXPECT_EQ(request(
        HttpMethod::Delete, path, {}, token, {{"confirmations", "2"}}).status, 400);
    EXPECT_EQ(request(
        HttpMethod::Delete, path, {}, token, {{"confirmations", "3"}}).status, 204);
    EXPECT_FALSE(store_.audit_logs.empty());
    EXPECT_EQ(store_.audit_logs.back().action, AuditAction::DangerousDelete);
}

TEST_F(ResourceApiTest, CategoryTreeRejectsDuplicateAndParentDeletion) {
    const auto user = register_user("categories@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto root_response = request(
        HttpMethod::Post,
        "/api/v1/categories",
        {{"board", "expense"}, {"name", "Education"}}, token);
    ASSERT_EQ(root_response.status, 201) << root_response.body;
    const auto root = nlohmann::json::parse(root_response.body);
    const auto root_id = root["id"].get<std::int64_t>();

    EXPECT_EQ(request(
        HttpMethod::Post,
        "/api/v1/categories",
        {{"board", "expense"}, {"name", "Education"}}, token).status, 409);
    const auto child = request(
        HttpMethod::Post,
        "/api/v1/categories",
        {{"board", "expense"}, {"name", "Books"}, {"parentId", root_id}}, token);
    ASSERT_EQ(child.status, 201) << child.body;
    const auto child_id = nlohmann::json::parse(child.body)["id"].get<std::int64_t>();

    const auto tree = request(
        HttpMethod::Get, "/api/v1/categories", {}, token, {{"board", "expense"}});
    ASSERT_EQ(tree.status, 200) << tree.body;
    const auto tree_body = nlohmann::json::parse(tree.body);
    const auto found = std::ranges::find_if(tree_body, [root_id](const auto& item) {
        return item["id"] == root_id;
    });
    ASSERT_NE(found, tree_body.end());
    ASSERT_EQ((*found)["children"].size(), 1U);

    EXPECT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/categories/" + std::to_string(root_id), {}, token).status, 409);
    EXPECT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/categories/" + std::to_string(child_id), {}, token).status, 204);
    EXPECT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/categories/" + std::to_string(root_id), {}, token).status, 204);
}

TEST_F(ResourceApiTest, SystemTemplateCannotSpoofNameOrHierarchy) {
    const auto user = register_user("templates@example.com");
    const auto token = user["accessToken"].get<std::string>();
    store_.category_templates.emplace(
        900,
        SystemCategoryTemplate{
            900, "Travel", "zh-CN", "Expense", std::nullopt,
            CategoryBoard::Expense, 10, true});

    EXPECT_EQ(request(
        HttpMethod::Post,
        "/api/v1/categories",
        {{"templateId", 900}, {"name", "Spoofed"}, {"board", "expense"}},
        token).status, 400);
    const auto activated = request(
        HttpMethod::Post,
        "/api/v1/categories",
        {{"templateId", 900}, {"name", "Travel"}, {"board", "expense"}},
        token);
    ASSERT_EQ(activated.status, 201) << activated.body;
    const auto activated_body = nlohmann::json::parse(activated.body);
    EXPECT_EQ(activated_body["source"], "system");
    const auto id = activated_body["id"].get<std::int64_t>();
    ASSERT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/categories/" + std::to_string(id), {}, token).status, 204);
    const auto same_name = request(
        HttpMethod::Post, "/api/v1/categories",
        {{"board", "expense"}, {"name", "Travel"}}, token);
    ASSERT_EQ(same_name.status, 201) << same_name.body;
    EXPECT_EQ(nlohmann::json::parse(same_name.body)["id"], id);
    EXPECT_EQ(nlohmann::json::parse(same_name.body)["source"], "system");
}

TEST_F(ResourceApiTest, CategoryUpdateDeleteAndRestorePreserveIdentity) {
    const auto alice = register_user("category-lifecycle@example.com");
    const auto bob = register_user("category-lifecycle-bob@example.com");
    const auto token = alice["accessToken"].get<std::string>();
    const auto bob_token = bob["accessToken"].get<std::string>();
    const auto created = request(
        HttpMethod::Post, "/api/v1/categories",
        {{"board", "expense"}, {"name", "Travel"}}, token);
    ASSERT_EQ(created.status, 201) << created.body;
    const auto id = nlohmann::json::parse(created.body)["id"].get<std::int64_t>();
    const auto path = "/api/v1/categories/" + std::to_string(id);

    const auto updated = request(
        HttpMethod::Put, path, {{"name", "Trips"}, {"sortOrder", 42}}, token);
    ASSERT_EQ(updated.status, 200) << updated.body;
    const auto updated_body = nlohmann::json::parse(updated.body);
    EXPECT_EQ(updated_body["board"], "expense");
    EXPECT_EQ(updated_body["sortOrder"], 42);
    EXPECT_EQ(request(
        HttpMethod::Put, path,
        {{"name", "Trips"}, {"sortOrder", 42}, {"board", "income"}},
        token).status, 400);
    EXPECT_EQ(request(
        HttpMethod::Put, path, {{"name", "Stolen"}, {"sortOrder", 0}},
        bob_token).status, 404);

    ASSERT_EQ(request(HttpMethod::Delete, path, {}, token).status, 204);
    const auto deleted = request(
        HttpMethod::Get, "/api/v1/categories", {}, token,
        {{"status", "deleted"}});
    ASSERT_EQ(deleted.status, 200) << deleted.body;
    ASSERT_EQ(nlohmann::json::parse(deleted.body).size(), 1U);
    EXPECT_TRUE(nlohmann::json::parse(deleted.body)[0]["isDeleted"]);

    const auto recreated = request(
        HttpMethod::Post, "/api/v1/categories",
        {{"board", "expense"}, {"name", "Trips"}}, token);
    ASSERT_EQ(recreated.status, 201) << recreated.body;
    EXPECT_EQ(nlohmann::json::parse(recreated.body)["id"], id);
    EXPECT_FALSE(nlohmann::json::parse(recreated.body)["isDeleted"]);

    ASSERT_EQ(request(HttpMethod::Delete, path, {}, token).status, 204);
    const auto restored = request(HttpMethod::Post, path + "/restore", {}, token);
    ASSERT_EQ(restored.status, 200) << restored.body;
    EXPECT_EQ(nlohmann::json::parse(restored.body)["id"], id);
    EXPECT_EQ(request(HttpMethod::Post, path + "/restore", {}, token).status, 409);
    EXPECT_EQ(request(HttpMethod::Post, path + "/restore", {}, bob_token).status, 404);
}

TEST_F(ResourceApiTest, TagsAreUniqueAndRelationsAreTenantScoped) {
    const auto alice = register_user("tag-alice@example.com");
    const auto bob = register_user("tag-bob@example.com");
    const auto alice_token = alice["accessToken"].get<std::string>();
    const auto bob_token = bob["accessToken"].get<std::string>();
    const auto account = create_account(alice_token);
    const auto alice_id = UserId(alice["userId"].get<std::int64_t>());
    const auto account_id = AccountId(account["id"].get<std::int64_t>());

    const auto tag_response = request(
        HttpMethod::Post, "/api/v1/tags", {{"name", "tax"}}, alice_token);
    ASSERT_EQ(tag_response.status, 201) << tag_response.body;
    const auto tag_id = nlohmann::json::parse(tag_response.body)["id"].get<std::int64_t>();
    EXPECT_EQ(request(
        HttpMethod::Post, "/api/v1/tags", {{"name", "tax"}}, alice_token).status, 409);

    const auto tx_id = TransactionId(store_.next_transaction_id++);
    store_.transactions.emplace(
        tx_id.value(),
        Transaction(
            tx_id, alice_id, account_id, money("-12", "CNY"),
            TransactionType::Expense, sample_time(), "seed"));
    const auto path = "/api/v1/transactions/" + tx_id.to_string() + "/tags";
    const auto attached = request(
        HttpMethod::Put, path, {{"tagIds", {tag_id}}}, alice_token);
    ASSERT_EQ(attached.status, 200) << attached.body;
    EXPECT_EQ(nlohmann::json::parse(attached.body).size(), 1U);
    nlohmann::json too_many = nlohmann::json::array();
    for (std::int64_t id = 1; id <= 65; ++id) {
        too_many.push_back(id);
    }
    EXPECT_EQ(request(
        HttpMethod::Put, path, {{"tagIds", std::move(too_many)}},
        alice_token).status, 400);
    EXPECT_EQ(request(
        HttpMethod::Put, path, {{"tagIds", {tag_id}}}, bob_token).status, 404);
    const auto deleted = request(
        HttpMethod::Delete,
        "/api/v1/accounts/" + account_id.to_string(), {}, alice_token,
        {{"confirmations", "3"}});
    EXPECT_EQ(deleted.status, 204) << deleted.body;
    EXPECT_FALSE(store_.transaction_tag_relations.contains(tx_id.value()));
}

TEST_F(ResourceApiTest, TagRenameAndSameNameCreateRestoreTheHistoricalTag) {
    const auto user = register_user("tag-lifecycle@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto created = request(
        HttpMethod::Post, "/api/v1/tags", {{"name", "tax"}}, token);
    ASSERT_EQ(created.status, 201) << created.body;
    const auto id = nlohmann::json::parse(created.body)["id"].get<std::int64_t>();
    const auto path = "/api/v1/tags/" + std::to_string(id);

    const auto renamed = request(
        HttpMethod::Put, path, {{"name", "annual-tax"}}, token);
    ASSERT_EQ(renamed.status, 200) << renamed.body;
    EXPECT_EQ(nlohmann::json::parse(renamed.body)["name"], "annual-tax");
    ASSERT_EQ(request(HttpMethod::Delete, path, {}, token).status, 204);

    const auto deleted = request(
        HttpMethod::Get, "/api/v1/tags", {}, token, {{"status", "deleted"}});
    ASSERT_EQ(deleted.status, 200) << deleted.body;
    EXPECT_EQ(nlohmann::json::parse(deleted.body)[0]["id"], id);
    const auto recreated = request(
        HttpMethod::Post, "/api/v1/tags", {{"name", "annual-tax"}}, token);
    ASSERT_EQ(recreated.status, 201) << recreated.body;
    EXPECT_EQ(nlohmann::json::parse(recreated.body)["id"], id);

    ASSERT_EQ(request(HttpMethod::Delete, path, {}, token).status, 204);
    const auto restored = request(HttpMethod::Post, path + "/restore", {}, token);
    ASSERT_EQ(restored.status, 200) << restored.body;
    EXPECT_FALSE(nlohmann::json::parse(restored.body)["isDeleted"]);
}

TEST_F(ResourceApiTest, PreferencesValidateTimezoneAndCurrency) {
    const auto user = register_user("preferences@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto get = request(
        HttpMethod::Get, "/api/v1/users/me/preferences", {}, token);
    ASSERT_EQ(get.status, 200) << get.body;
    EXPECT_EQ(nlohmann::json::parse(get.body)["timezone"], "Asia/Shanghai");

    nlohmann::json body{
        {"baseCurrency", "USD"}, {"locale", "en-US"},
        {"timezone", "America/New_York"}, {"dateFormat", "YYYY-MM-DD"},
        {"numberFormat", "1,234.56"}, {"theme", "dark"},
        {"defaultHomePage", "accounts"},
        {"defaultReportPeriod", "current_year"}};
    const auto updated = request(
        HttpMethod::Put, "/api/v1/users/me/preferences", body, token);
    ASSERT_EQ(updated.status, 200) << updated.body;
    EXPECT_EQ(nlohmann::json::parse(updated.body)["baseCurrency"], "USD");
    body["timezone"] = "Not/A_Real_Zone";
    EXPECT_EQ(request(
        HttpMethod::Put, "/api/v1/users/me/preferences", body, token).status, 400);
    body["timezone"] = "America/New_York";
    body["locale"] = "en_US";
    EXPECT_EQ(request(
        HttpMethod::Put, "/api/v1/users/me/preferences", body, token).status, 400);
    body["locale"] = "fr-FR";
    EXPECT_EQ(request(
        HttpMethod::Put, "/api/v1/users/me/preferences", body, token).status, 400);
    const auto unchanged = request(
        HttpMethod::Get, "/api/v1/users/me/preferences", {}, token);
    ASSERT_EQ(unchanged.status, 200) << unchanged.body;
    EXPECT_EQ(nlohmann::json::parse(unchanged.body)["locale"], "en-US");
}

TEST_F(ResourceApiTest, CurrencyCatalogIsPublicAndComplete) {
    const auto response = request(HttpMethod::Get, "/api/v1/currencies");
    ASSERT_EQ(response.status, 200) << response.body;
    const auto body = nlohmann::json::parse(response.body);
    EXPECT_EQ(body.size(), 33U);
    EXPECT_EQ(body.front()["code"], "USD");
    EXPECT_TRUE(std::ranges::any_of(body, [](const auto& currency) {
        return currency["code"] == "BTC" && currency["isCrypto"] == true;
    }));
    ASSERT_TRUE(response.headers.contains("ETag"));

    HttpRequest cached;
    cached.method = HttpMethod::Get;
    cached.path = "/api/v1/currencies";
    cached.headers.emplace("If-None-Match", response.headers.at("ETag"));
    const auto not_modified = app_.handle(std::move(cached));
    EXPECT_EQ(not_modified.status, 304);
    EXPECT_TRUE(not_modified.body.empty());
}

TEST_F(ResourceApiTest, TransactionApiKeepsRestMagnitudeAndStorageSignSeparate) {
    const auto user = register_user("transactions@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto account = create_account(token);
    const auto account_id = account["id"].get<std::int64_t>();
    const auto category_response = request(
        HttpMethod::Post,
        "/api/v1/categories",
        {{"board", "expense"}, {"name", "Meals API"}}, token);
    ASSERT_EQ(category_response.status, 201) << category_response.body;
    const auto category_id =
        nlohmann::json::parse(category_response.body)["id"].get<std::int64_t>();

    const auto created = request(
        HttpMethod::Post,
        "/api/v1/transactions",
        {{"accountId", account_id}, {"type", "expense"},
         {"amount", "45.00"}, {"currencyCode", "CNY"},
         {"categoryId", category_id}, {"description", "lunch"}}, token);
    ASSERT_EQ(created.status, 201) << created.body;
    const auto body = nlohmann::json::parse(created.body);
    EXPECT_EQ(body["amount"], "45");
    EXPECT_EQ(body["type"], "expense");
    const auto transaction_id = body["id"].get<std::int64_t>();
    ASSERT_TRUE(store_.transactions.contains(transaction_id));
    EXPECT_EQ(
        store_.transactions.at(transaction_id).amount().amount().to_string(), "-45");
    EXPECT_GT(
        store_.transactions.at(transaction_id).occurred_at().time_since_epoch().count(), 0);

    const auto adjustment = request(
        HttpMethod::Post,
        "/api/v1/transactions",
        {{"accountId", account_id}, {"type", "adjustment"},
         {"amount", "-2.50"}, {"currencyCode", "CNY"},
         {"categoryId", category_id}}, token);
    ASSERT_EQ(adjustment.status, 201) << adjustment.body;
    EXPECT_EQ(nlohmann::json::parse(adjustment.body)["amount"], "-2.5");
}

TEST_F(ResourceApiTest, TransactionApiRejectsBadTypesAmountsCurrencyAndBoard) {
    const auto user = register_user("transaction-validation@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto account = create_account(token);
    const auto account_id = account["id"].get<std::int64_t>();
    const auto category_response = request(
        HttpMethod::Post,
        "/api/v1/categories",
        {{"board", "expense"}, {"name", "Expense Only API"}}, token);
    ASSERT_EQ(category_response.status, 201) << category_response.body;
    const auto category_id =
        nlohmann::json::parse(category_response.body)["id"].get<std::int64_t>();
    const auto base = nlohmann::json{
        {"accountId", account_id}, {"type", "expense"},
        {"amount", "1"}, {"currencyCode", "CNY"},
        {"categoryId", category_id}};

    auto number_amount = base;
    number_amount["amount"] = 1.25;
    EXPECT_EQ(request(
        HttpMethod::Post, "/api/v1/transactions", number_amount, token).status, 400);
    for (const auto& non_canonical : {"+1", " 1", "1 "}) {
        auto invalid = base;
        invalid["amount"] = non_canonical;
        EXPECT_EQ(request(
            HttpMethod::Post, "/api/v1/transactions", invalid, token).status, 400);
    }
    auto overflow = base;
    overflow["amount"] = "1000000000000.00000000";
    EXPECT_EQ(request(
        HttpMethod::Post, "/api/v1/transactions", overflow, token).status, 400);
    auto mismatch = base;
    mismatch["currencyCode"] = "USD";
    EXPECT_EQ(request(
        HttpMethod::Post, "/api/v1/transactions", mismatch, token).status, 422);
    auto wrong_board = base;
    wrong_board["type"] = "income";
    EXPECT_EQ(request(
        HttpMethod::Post, "/api/v1/transactions", wrong_board, token).status, 422);
    auto direct_transfer = base;
    direct_transfer["type"] = "transfer";
    direct_transfer.erase("categoryId");
    EXPECT_EQ(request(
        HttpMethod::Post, "/api/v1/transactions", direct_transfer, token).status, 400);
}

TEST_F(ResourceApiTest, TransactionCreateIsIdempotentAndTenantScoped) {
    const auto alice = register_user("idempotency-alice@example.com");
    const auto bob = register_user("idempotency-bob@example.com");
    const auto alice_token = alice["accessToken"].get<std::string>();
    const auto bob_token = bob["accessToken"].get<std::string>();
    const auto alice_account = create_account(alice_token)["id"].get<std::int64_t>();
    const auto bob_account = create_account(bob_token)["id"].get<std::int64_t>();
    const auto alice_body = nlohmann::json{
        {"accountId", alice_account}, {"type", "expense"},
        {"amount", "12.50"}, {"currencyCode", "CNY"},
        {"description", "idempotent lunch"},
        {"occurredAt", "2026-07-16T12:34:56.123456789Z"}};
    const std::map<std::string, std::string> key{
        {"Idempotency-Key", "same-operation-key"}};
    const auto transactions_before = store_.transactions.size();
    const auto outbox_before = store_.outbox.size();

    const auto first = request(
        HttpMethod::Post, "/api/v1/transactions", alice_body,
        alice_token, {}, key);
    const auto replay = request(
        HttpMethod::Post, "/api/v1/transactions", alice_body,
        alice_token, {}, key);
    ASSERT_EQ(first.status, 201) << first.body;
    ASSERT_EQ(replay.status, 201) << replay.body;
    EXPECT_EQ(replay.body, first.body);
    EXPECT_EQ(
        nlohmann::json::parse(first.body)["occurredAt"],
        "2026-07-16T12:34:56.123456Z");
    EXPECT_EQ(store_.transactions.size(), transactions_before + 1U);
    EXPECT_EQ(store_.outbox.size(), outbox_before + 1U);

    auto changed = alice_body;
    changed["amount"] = "13.00";
    const auto conflict = request(
        HttpMethod::Post, "/api/v1/transactions", changed,
        alice_token, {}, key);
    ASSERT_EQ(conflict.status, 409) << conflict.body;
    EXPECT_EQ(store_.transactions.size(), transactions_before + 1U);

    auto bob_body = alice_body;
    bob_body["accountId"] = bob_account;
    const auto other_tenant = request(
        HttpMethod::Post, "/api/v1/transactions", bob_body,
        bob_token, {}, key);
    ASSERT_EQ(other_tenant.status, 201) << other_tenant.body;
    EXPECT_EQ(store_.transactions.size(), transactions_before + 2U);
}

TEST_F(ResourceApiTest, FinancialCreatesRequireValidIdempotencyKey) {
    const auto user = register_user("idempotency-required@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto account = create_account(token);

    HttpRequest missing;
    missing.method = HttpMethod::Post;
    missing.path = "/api/v1/transactions";
    missing.body = nlohmann::json{
        {"accountId", account["id"]}, {"type", "expense"},
        {"amount", "1"}, {"currencyCode", "CNY"}}.dump();
    missing.headers.emplace("Authorization", "Bearer " + token);
    EXPECT_EQ(app_.handle(std::move(missing)).status, 400);

    const auto invalid = request(
        HttpMethod::Post,
        "/api/v1/transactions",
        {{"accountId", account["id"]}, {"type", "expense"},
         {"amount", "1"}, {"currencyCode", "CNY"}},
        token,
        {},
        {{"Idempotency-Key", "contains a space"}});
    EXPECT_EQ(invalid.status, 400);
}

TEST_F(ResourceApiTest, TransactionSoftDeleteIsOwnedAndConflictSafe) {
    const auto alice = register_user("delete-tx-alice@example.com");
    const auto bob = register_user("delete-tx-bob@example.com");
    const auto alice_token = alice["accessToken"].get<std::string>();
    const auto bob_token = bob["accessToken"].get<std::string>();
    const auto account = create_account(alice_token);
    const auto created = request(
        HttpMethod::Post,
        "/api/v1/transactions",
        {{"accountId", account["id"]}, {"type", "expense"},
         {"amount", "5"}, {"currencyCode", "CNY"}}, alice_token);
    ASSERT_EQ(created.status, 201) << created.body;
    const auto id = nlohmann::json::parse(created.body)["id"].get<std::int64_t>();
    const auto path = "/api/v1/transactions/" + std::to_string(id);

    EXPECT_EQ(request(HttpMethod::Delete, path, {}, bob_token).status, 404);
    EXPECT_EQ(request(HttpMethod::Delete, path, {}, alice_token).status, 204);
    EXPECT_TRUE(store_.transactions.at(id).is_deleted());
    EXPECT_EQ(request(HttpMethod::Delete, path, {}, alice_token).status, 409);
}

TEST_F(ResourceApiTest, TransactionLedgerUsesStableCursorAndHistoricalMetadata) {
    const auto alice = register_user("ledger-alice@example.com");
    const auto bob = register_user("ledger-bob@example.com");
    const auto token = alice["accessToken"].get<std::string>();
    const auto bob_token = bob["accessToken"].get<std::string>();
    const auto account_id = create_account(token)["id"].get<std::int64_t>();

    const auto category_response = request(
        HttpMethod::Post,
        "/api/v1/categories",
        {{"board", "expense"}, {"name", "Historical meals"}},
        token);
    ASSERT_EQ(category_response.status, 201) << category_response.body;
    const auto category_id = nlohmann::json::parse(
        category_response.body)["id"].get<std::int64_t>();
    const auto tag_response = request(
        HttpMethod::Post,
        "/api/v1/tags",
        {{"name", "client-trip"}},
        token);
    ASSERT_EQ(tag_response.status, 201) << tag_response.body;
    const auto tag_id = nlohmann::json::parse(
        tag_response.body)["id"].get<std::int64_t>();

    std::vector<std::int64_t> ids;
    for (const auto& [description, amount] :
         std::vector<std::pair<std::string, std::string>>{
             {"client lunch", "12.00"},
             {"client coffee", "4.50"},
             {"client dinner", "25.00"}}) {
        const auto created = request(
            HttpMethod::Post,
            "/api/v1/transactions",
            {{"accountId", account_id},
             {"type", "expense"},
             {"amount", amount},
             {"currencyCode", "CNY"},
             {"categoryId", category_id},
             {"tagIds", nlohmann::json::array({tag_id})},
             {"description", description},
             {"occurredAt", "2026-07-16T08:00:00+08:00"}},
            token);
        ASSERT_EQ(created.status, 201) << created.body;
        const auto body = nlohmann::json::parse(created.body);
        ids.push_back(body["id"].get<std::int64_t>());
        ASSERT_EQ(body["tags"].size(), 1U);
    }

    const auto first = request(
        HttpMethod::Get,
        "/api/v1/transactions",
        {},
        token,
        {{"pageSize", "2"}, {"keyword", "client"}});
    ASSERT_EQ(first.status, 200) << first.body;
    const auto first_page = nlohmann::json::parse(first.body);
    ASSERT_EQ(first_page["items"].size(), 2U);
    EXPECT_EQ(first_page["items"][0]["id"], ids[2]);
    EXPECT_EQ(first_page["items"][1]["id"], ids[1]);
    ASSERT_TRUE(first_page["nextCursor"].is_string());

    const auto second = request(
        HttpMethod::Get,
        "/api/v1/transactions",
        {},
        token,
        {{"pageSize", "2"},
         {"keyword", "client"},
         {"cursor", first_page["nextCursor"].get<std::string>()}});
    ASSERT_EQ(second.status, 200) << second.body;
    const auto second_page = nlohmann::json::parse(second.body);
    ASSERT_EQ(second_page["items"].size(), 1U);
    EXPECT_EQ(second_page["items"][0]["id"], ids[0]);
    EXPECT_TRUE(second_page["nextCursor"].is_null());

    const auto filtered = request(
        HttpMethod::Get,
        "/api/v1/transactions",
        {},
        token,
        {{"accountId", std::to_string(account_id)},
         {"type", "expense"},
         {"categoryId", std::to_string(category_id)},
         {"tagId", std::to_string(tag_id)},
         {"from", "2026-07-16T00:00:00Z"},
         {"to", "2026-07-17T00:00:00Z"}});
    ASSERT_EQ(filtered.status, 200) << filtered.body;
    EXPECT_EQ(nlohmann::json::parse(filtered.body)["items"].size(), 3U);

    EXPECT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/categories/" + std::to_string(category_id),
        {}, token).status, 204);
    EXPECT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/tags/" + std::to_string(tag_id),
        {}, token).status, 204);
    const auto detail = request(
        HttpMethod::Get,
        "/api/v1/transactions/" + std::to_string(ids[0]),
        {}, token);
    ASSERT_EQ(detail.status, 200) << detail.body;
    const auto detail_body = nlohmann::json::parse(detail.body);
    EXPECT_EQ(detail_body["categoryName"], "Historical meals");
    EXPECT_TRUE(detail_body["categoryDeleted"]);
    ASSERT_EQ(detail_body["tags"].size(), 1U);
    EXPECT_EQ(detail_body["tags"][0]["name"], "client-trip");
    EXPECT_TRUE(detail_body["tags"][0]["isDeleted"]);
    EXPECT_EQ(request(
        HttpMethod::Get,
        "/api/v1/transactions/" + std::to_string(ids[0]),
        {}, bob_token).status, 404);

    EXPECT_EQ(request(
        HttpMethod::Get,
        "/api/v1/transactions",
        {}, token,
        {{"from", "2025-01-01T00:00:00Z"},
         {"to", "2026-07-17T00:00:00Z"}}).status, 400);
    EXPECT_EQ(request(
        HttpMethod::Get,
        "/api/v1/transactions",
        {}, token,
        {{"cursor", "not-a-valid-cursor"}}).status, 400);
}

TEST_F(ResourceApiTest, TransactionCorrectionIsAtomicIdempotentAndTraceable) {
    const auto user = register_user("correction@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto account_id = create_account(token)["id"].get<std::int64_t>();
    const auto tag_response = request(
        HttpMethod::Post, "/api/v1/tags", {{"name", "verified"}}, token);
    ASSERT_EQ(tag_response.status, 201) << tag_response.body;
    const auto tag_id = nlohmann::json::parse(
        tag_response.body)["id"].get<std::int64_t>();
    const auto created = request(
        HttpMethod::Post,
        "/api/v1/transactions",
        {{"accountId", account_id},
         {"type", "expense"},
         {"amount", "10.00"},
         {"currencyCode", "CNY"},
         {"description", "wrong amount"}},
        token);
    ASSERT_EQ(created.status, 201) << created.body;
    const auto original_id = nlohmann::json::parse(
        created.body)["id"].get<std::int64_t>();
    const nlohmann::json correction_body{
        {"accountId", account_id},
        {"type", "expense"},
        {"amount", "12.50"},
        {"currencyCode", "CNY"},
        {"tagIds", nlohmann::json::array({tag_id})},
        {"description", "correct amount"},
        {"occurredAt", "2026-07-16T12:30:00Z"}};
    const auto correction_path =
        "/api/v1/transactions/" + std::to_string(original_id) + "/correction";
    const std::map<std::string, std::string> key{
        {"Idempotency-Key", "correction-key-1"}};
    const auto transactions_before = store_.transactions.size();
    const auto audits_before = store_.audit_logs.size();
    const auto outbox_before = store_.outbox.size();

    const auto corrected = request(
        HttpMethod::Post, correction_path, correction_body, token, {}, key);
    const auto replay = request(
        HttpMethod::Post, correction_path, correction_body, token, {}, key);
    ASSERT_EQ(corrected.status, 201) << corrected.body;
    ASSERT_EQ(replay.status, 201) << replay.body;
    EXPECT_EQ(replay.body, corrected.body);
    const auto replacement = nlohmann::json::parse(corrected.body);
    const auto replacement_id = replacement["id"].get<std::int64_t>();
    EXPECT_EQ(replacement["amount"], "12.5");
    EXPECT_EQ(replacement["correctsTransactionId"], original_id);
    EXPECT_TRUE(store_.transactions.at(original_id).is_deleted());
    EXPECT_FALSE(store_.transactions.at(replacement_id).is_deleted());
    EXPECT_EQ(store_.transactions.size(), transactions_before + 1U);
    ASSERT_TRUE(store_.transaction_corrections.contains(original_id));
    EXPECT_EQ(
        store_.transaction_corrections.at(original_id)
            .replacement_transaction_id.value(),
        replacement_id);
    EXPECT_EQ(store_.audit_logs.size(), audits_before + 1U);
    EXPECT_EQ(store_.outbox.size(), outbox_before + 1U);
    EXPECT_EQ(store_.outbox.back().event_name, "TransactionCorrected");

    const auto original_detail = request(
        HttpMethod::Get,
        "/api/v1/transactions/" + std::to_string(original_id),
        {}, token);
    ASSERT_EQ(original_detail.status, 200) << original_detail.body;
    const auto original_json = nlohmann::json::parse(original_detail.body);
    EXPECT_EQ(original_json["correctedByTransactionId"], replacement_id);
    EXPECT_FALSE(original_json["deletedAt"].is_null());

    const auto list = request(
        HttpMethod::Get, "/api/v1/transactions", {}, token);
    ASSERT_EQ(list.status, 200) << list.body;
    const auto items = nlohmann::json::parse(list.body)["items"];
    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items[0]["id"], replacement_id);

    EXPECT_EQ(request(
        HttpMethod::Post,
        correction_path,
        correction_body,
        token,
        {},
        {{"Idempotency-Key", "correction-key-2"}}).status, 409);

    const auto active = request(
        HttpMethod::Post,
        "/api/v1/transactions",
        {{"accountId", account_id},
         {"type", "expense"},
         {"amount", "3"},
         {"currencyCode", "CNY"}},
        token);
    ASSERT_EQ(active.status, 201) << active.body;
    const auto active_id = nlohmann::json::parse(active.body)["id"].get<std::int64_t>();
    auto invalid_tags = correction_body;
    invalid_tags["tagIds"] = nlohmann::json::array({999999});
    const auto failed = request(
        HttpMethod::Post,
        "/api/v1/transactions/" + std::to_string(active_id) + "/correction",
        invalid_tags,
        token,
        {},
        {{"Idempotency-Key", "correction-invalid-tag"}});
    ASSERT_EQ(failed.status, 404) << failed.body;
    EXPECT_FALSE(store_.transactions.at(active_id).is_deleted());
    EXPECT_FALSE(store_.transaction_corrections.contains(active_id));

    const auto target_response = request(
        HttpMethod::Post,
        "/api/v1/accounts",
        {{"name", "Correction transfer target"},
         {"type", "savings"},
         {"subtype", "bank"},
         {"currencyCode", "CNY"}},
        token);
    ASSERT_EQ(target_response.status, 201) << target_response.body;
    const auto target_id = nlohmann::json::parse(
        target_response.body)["id"].get<std::int64_t>();
    const auto transfer = request(
        HttpMethod::Post,
        "/api/v1/transfers",
        {{"sourceAccountId", account_id},
         {"targetAccountId", target_id},
         {"mode", "BothAmounts"},
         {"outgoingAmount", "5"},
         {"incomingAmount", "5"}},
        token);
    ASSERT_EQ(transfer.status, 201) << transfer.body;
    const auto transfer_id = nlohmann::json::parse(
        transfer.body)["outgoingTransactionId"].get<std::int64_t>();
    const auto transfer_correction = request(
        HttpMethod::Post,
        "/api/v1/transactions/" + std::to_string(transfer_id) + "/correction",
        correction_body,
        token,
        {},
        {{"Idempotency-Key", "reject-transfer-leg-correction"}});
    ASSERT_EQ(transfer_correction.status, 422) << transfer_correction.body;
    EXPECT_FALSE(store_.transactions.at(transfer_id).is_deleted());
}

TEST_F(ResourceApiTest, TransferApiPersistsSignedAggregateAndQueriesMagnitudes) {
    const auto alice = register_user("transfer-alice@example.com");
    const auto bob = register_user("transfer-bob@example.com");
    const auto alice_token = alice["accessToken"].get<std::string>();
    const auto bob_token = bob["accessToken"].get<std::string>();
    const auto create_currency_account = [&](std::string name, std::string currency) {
        const auto response = request(
            HttpMethod::Post,
            "/api/v1/accounts",
            {{"name", std::move(name)}, {"type", "savings"},
             {"subtype", "bank"}, {"currencyCode", std::move(currency)}},
            alice_token);
        EXPECT_EQ(response.status, 201) << response.body;
        return nlohmann::json::parse(response.body)["id"].get<std::int64_t>();
    };
    const auto source_id = create_currency_account("CNY Source", "CNY");
    const auto target_id = create_currency_account("USD Target", "USD");

    const auto created = request(
        HttpMethod::Post,
        "/api/v1/transfers",
        {{"sourceAccountId", source_id}, {"targetAccountId", target_id},
         {"mode", "BothAmounts"}, {"outgoingAmount", "718.00"},
         {"incomingAmount", "100.00"}, {"feeAmount", "2.00"},
         {"feeSource", "SourceAccount"}, {"description", "settlement"}},
        alice_token);
    ASSERT_EQ(created.status, 201) << created.body;
    const auto body = nlohmann::json::parse(created.body);
    EXPECT_EQ(body["outgoingAmount"], "718");
    EXPECT_EQ(body["incomingAmount"], "100");
    EXPECT_EQ(body["feeAmount"], "2");
    EXPECT_TRUE(body["rate"].is_string());
    const auto group_id = body["transferGroupId"].get<std::int64_t>();

    int negative_transfer_legs = 0;
    int positive_transfer_legs = 0;
    int negative_adjustments = 0;
    std::int64_t adjustment_id = 0;
    for (const auto& [transaction_id, transaction] : store_.transactions) {
        if (transaction.transfer_group_id() !=
            std::optional<TransferGroupId>(TransferGroupId(group_id))) {
            continue;
        }
        if (transaction.type() == TransactionType::Transfer &&
            transaction.amount().is_negative()) ++negative_transfer_legs;
        if (transaction.type() == TransactionType::Transfer &&
            transaction.amount().is_positive()) ++positive_transfer_legs;
        if (transaction.type() == TransactionType::Adjustment &&
            transaction.amount().is_negative()) {
            ++negative_adjustments;
            adjustment_id = transaction_id;
        }
    }
    EXPECT_EQ(negative_transfer_legs, 1);
    EXPECT_EQ(positive_transfer_legs, 1);
    EXPECT_EQ(negative_adjustments, 1);
    ASSERT_GT(adjustment_id, 0);

    const auto outgoing_id = body["outgoingTransactionId"].get<std::int64_t>();
    EXPECT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/transactions/" + std::to_string(outgoing_id),
        {}, alice_token).status, 422);
    EXPECT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/transactions/" + std::to_string(adjustment_id),
        {}, alice_token).status, 422);
    EXPECT_FALSE(store_.transactions.at(adjustment_id).is_deleted());

    const auto query_path = "/api/v1/transfers/" + std::to_string(group_id);
    const auto queried = request(HttpMethod::Get, query_path, {}, alice_token);
    ASSERT_EQ(queried.status, 200) << queried.body;
    EXPECT_EQ(nlohmann::json::parse(queried.body)["outgoingAmount"], "718");
    EXPECT_EQ(request(HttpMethod::Get, query_path, {}, bob_token).status, 404);
    EXPECT_EQ(request(HttpMethod::Delete, query_path, {}, bob_token).status, 404);
    EXPECT_EQ(request(HttpMethod::Delete, query_path, {}, alice_token).status, 204);
    EXPECT_EQ(request(HttpMethod::Delete, query_path, {}, alice_token).status, 409);
    const auto deleted = request(HttpMethod::Get, query_path, {}, alice_token);
    ASSERT_EQ(deleted.status, 200) << deleted.body;
    EXPECT_TRUE(nlohmann::json::parse(deleted.body)["deletedAt"].is_string());
    for (const auto& [_, transaction] : store_.transactions) {
        if (transaction.transfer_group_id() ==
            std::optional<TransferGroupId>(TransferGroupId(group_id))) {
            EXPECT_TRUE(transaction.is_deleted());
        }
    }
}

TEST_F(ResourceApiTest, TransferListUsesStableAggregateCursorAndAccountFilter) {
    const auto user = register_user("transfer-list@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto source = create_account(token)["id"].get<std::int64_t>();
    const auto target_response = request(
        HttpMethod::Post, "/api/v1/accounts",
        {{"name", "Transfer list target"}, {"type", "savings"},
         {"subtype", "bank"}, {"currencyCode", "USD"}}, token);
    ASSERT_EQ(target_response.status, 201) << target_response.body;
    const auto target = nlohmann::json::parse(
        target_response.body)["id"].get<std::int64_t>();
    const auto post = [&](std::string amount, std::string occurred_at) {
        return request(
            HttpMethod::Post, "/api/v1/transfers",
            {{"sourceAccountId", source}, {"targetAccountId", target},
             {"mode", "BothAmounts"}, {"outgoingAmount", amount},
             {"incomingAmount", "1"}, {"occurredAt", occurred_at}}, token);
    };
    ASSERT_EQ(post("7", "2026-07-16T00:00:00Z").status, 201);
    ASSERT_EQ(post("14", "2026-07-17T00:00:00Z").status, 201);
    ASSERT_EQ(post("21", "2026-07-18T00:00:00Z").status, 201);

    const auto first = request(
        HttpMethod::Get, "/api/v1/transfers", {}, token,
        {{"accountId", std::to_string(source)}, {"pageSize", "2"}});
    ASSERT_EQ(first.status, 200) << first.body;
    const auto first_body = nlohmann::json::parse(first.body);
    ASSERT_EQ(first_body["items"].size(), 2U);
    EXPECT_EQ(first_body["items"][0]["outgoingAmount"], "21");
    EXPECT_EQ(first_body["items"][1]["outgoingAmount"], "14");
    ASSERT_TRUE(first_body["nextCursor"].is_string());
    const auto second = request(
        HttpMethod::Get, "/api/v1/transfers", {}, token,
        {{"accountId", std::to_string(source)},
         {"pageSize", "2"},
         {"cursor", first_body["nextCursor"].get<std::string>()}});
    ASSERT_EQ(second.status, 200) << second.body;
    const auto second_body = nlohmann::json::parse(second.body);
    ASSERT_EQ(second_body["items"].size(), 1U);
    EXPECT_EQ(second_body["items"][0]["outgoingAmount"], "7");
    EXPECT_TRUE(second_body["nextCursor"].is_null());

    EXPECT_EQ(request(
        HttpMethod::Get, "/api/v1/transfers", {}, token,
        {{"from", "2026-01-01T00:00:00Z"},
         {"to", "2027-02-01T00:00:00Z"}}).status, 400);
    EXPECT_EQ(request(
        HttpMethod::Get, "/api/v1/transfers", {}, token,
        {{"cursor", "not-a-transfer-cursor"}}).status, 400);
}

TEST_F(ResourceApiTest, TransferCorrectionIsAtomicIdempotentAndTraceable) {
    const auto user = register_user("transfer-correction@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto source = create_account(token)["id"].get<std::int64_t>();
    const auto target_response = request(
        HttpMethod::Post, "/api/v1/accounts",
        {{"name", "Correction target"}, {"type", "savings"},
         {"subtype", "bank"}, {"currencyCode", "USD"}}, token);
    ASSERT_EQ(target_response.status, 201) << target_response.body;
    const auto target = nlohmann::json::parse(
        target_response.body)["id"].get<std::int64_t>();
    const auto original = request(
        HttpMethod::Post, "/api/v1/transfers",
        {{"sourceAccountId", source}, {"targetAccountId", target},
         {"mode", "OutgoingAndRate"}, {"outgoingAmount", "70"},
         {"rate", "0.1428571429"}, {"feeAmount", "1"},
         {"feeSource", "SourceAccount"}}, token);
    ASSERT_EQ(original.status, 201) << original.body;
    const auto original_id = nlohmann::json::parse(
        original.body)["transferGroupId"].get<std::int64_t>();
    const auto correction_body = nlohmann::json{
        {"sourceAccountId", source}, {"targetAccountId", target},
        {"mode", "BothAmounts"}, {"outgoingAmount", "72"},
        {"incomingAmount", "10"}, {"feeAmount", "2"},
        {"feeSource", "TargetAccount"},
        {"description", "corrected transfer"}};
    const std::map<std::string, std::string> key{
        {"Idempotency-Key", "transfer-correction-key"}};
    const auto groups_before = store_.transfer_groups.size();
    const auto transactions_before = store_.transactions.size();
    const auto corrected = request(
        HttpMethod::Post,
        "/api/v1/transfers/" + std::to_string(original_id) + "/correction",
        correction_body, token, {}, key);
    ASSERT_EQ(corrected.status, 201) << corrected.body;
    const auto corrected_body = nlohmann::json::parse(corrected.body);
    const auto replacement_id =
        corrected_body["transferGroupId"].get<std::int64_t>();
    EXPECT_EQ(corrected_body["correctsTransferGroupId"], original_id);
    EXPECT_EQ(corrected_body["feeSource"], "TargetAccount");
    EXPECT_EQ(store_.transfer_groups.size(), groups_before + 1U);
    EXPECT_EQ(store_.transactions.size(), transactions_before + 3U);
    ASSERT_TRUE(store_.transfer_corrections.contains(original_id));
    EXPECT_EQ(
        store_.transfer_corrections.at(original_id).replacement_group_id.value(),
        replacement_id);

    const auto original_detail = request(
        HttpMethod::Get,
        "/api/v1/transfers/" + std::to_string(original_id), {}, token);
    ASSERT_EQ(original_detail.status, 200) << original_detail.body;
    const auto original_json = nlohmann::json::parse(original_detail.body);
    EXPECT_TRUE(original_json["deletedAt"].is_string());
    EXPECT_EQ(original_json["correctedByTransferGroupId"], replacement_id);
    const auto replacement_detail = request(
        HttpMethod::Get,
        "/api/v1/transfers/" + std::to_string(replacement_id), {}, token);
    ASSERT_EQ(replacement_detail.status, 200) << replacement_detail.body;
    EXPECT_EQ(
        nlohmann::json::parse(replacement_detail.body)["correctsTransferGroupId"],
        original_id);

    const auto replay = request(
        HttpMethod::Post,
        "/api/v1/transfers/" + std::to_string(original_id) + "/correction",
        correction_body, token, {}, key);
    ASSERT_EQ(replay.status, 201) << replay.body;
    EXPECT_EQ(replay.body, corrected.body);
    EXPECT_EQ(store_.transfer_groups.size(), groups_before + 1U);
    EXPECT_EQ(store_.transactions.size(), transactions_before + 3U);

    const auto second_original = request(
        HttpMethod::Post, "/api/v1/transfers",
        {{"sourceAccountId", source}, {"targetAccountId", target},
         {"mode", "BothAmounts"}, {"outgoingAmount", "7"},
         {"incomingAmount", "1"}}, token);
    ASSERT_EQ(second_original.status, 201) << second_original.body;
    const auto second_id = nlohmann::json::parse(
        second_original.body)["transferGroupId"].get<std::int64_t>();
    auto invalid = correction_body;
    invalid["feeSource"] = "ThirdParty";
    invalid["feeAccountId"] = 999999;
    const auto groups_before_failure = store_.transfer_groups.size();
    const auto transactions_before_failure = store_.transactions.size();
    const auto failed = request(
        HttpMethod::Post,
        "/api/v1/transfers/" + std::to_string(second_id) + "/correction",
        invalid, token, {},
        {{"Idempotency-Key", "transfer-correction-failure"}});
    ASSERT_EQ(failed.status, 404) << failed.body;
    EXPECT_EQ(store_.transfer_groups.size(), groups_before_failure);
    EXPECT_EQ(store_.transactions.size(), transactions_before_failure);
    EXPECT_FALSE(store_.transfer_corrections.contains(second_id));
    const auto still_active = request(
        HttpMethod::Get,
        "/api/v1/transfers/" + std::to_string(second_id), {}, token);
    ASSERT_EQ(still_active.status, 200) << still_active.body;
    EXPECT_TRUE(nlohmann::json::parse(still_active.body)["deletedAt"].is_null());
}

TEST_F(ResourceApiTest, TransferApiSupportsRateModesAndThirdPartyFee) {
    const auto user = register_user("transfer-modes@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto create_currency_account = [&](std::string name, std::string currency) {
        const auto response = request(
            HttpMethod::Post, "/api/v1/accounts",
            {{"name", std::move(name)}, {"type", "savings"},
             {"subtype", "bank"}, {"currencyCode", std::move(currency)}}, token);
        EXPECT_EQ(response.status, 201) << response.body;
        return nlohmann::json::parse(response.body)["id"].get<std::int64_t>();
    };
    const auto cny = create_currency_account("CNY", "CNY");
    const auto usd = create_currency_account("USD", "USD");
    const auto hkd = create_currency_account("Fee HKD", "HKD");

    const auto outgoing_rate = request(
        HttpMethod::Post, "/api/v1/transfers",
        {{"sourceAccountId", cny}, {"targetAccountId", usd},
         {"mode", "OutgoingAndRate"}, {"outgoingAmount", "700"},
         {"rate", "0.1428571429"}}, token);
    ASSERT_EQ(outgoing_rate.status, 201) << outgoing_rate.body;
    EXPECT_TRUE(nlohmann::json::parse(outgoing_rate.body)["incomingAmount"].is_string());

    const auto incoming_rate = request(
        HttpMethod::Post, "/api/v1/transfers",
        {{"sourceAccountId", usd}, {"targetAccountId", cny},
         {"mode", "IncomingAndRate"}, {"incomingAmount", "700"},
         {"rate", "7"}, {"feeAmount", "3"},
         {"feeSource", "ThirdParty"}, {"feeAccountId", hkd}}, token);
    ASSERT_EQ(incoming_rate.status, 201) << incoming_rate.body;
    EXPECT_EQ(nlohmann::json::parse(incoming_rate.body)["outgoingAmount"], "100");
}

TEST_F(ResourceApiTest, TransferCreateReplaysWholeAggregateExactlyOnce) {
    const auto user = register_user("transfer-idempotency@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto source = create_account(token)["id"].get<std::int64_t>();
    const auto target_response = request(
        HttpMethod::Post, "/api/v1/accounts",
        {{"name", "Transfer Target"}, {"type", "savings"},
         {"subtype", "bank"}, {"currencyCode", "USD"}}, token);
    ASSERT_EQ(target_response.status, 201) << target_response.body;
    const auto target =
        nlohmann::json::parse(target_response.body)["id"].get<std::int64_t>();
    const auto body = nlohmann::json{
        {"sourceAccountId", source}, {"targetAccountId", target},
        {"mode", "BothAmounts"}, {"outgoingAmount", "70"},
        {"incomingAmount", "10"}, {"feeAmount", "1"},
        {"feeSource", "SourceAccount"}};
    const std::map<std::string, std::string> key{
        {"Idempotency-Key", "transfer-operation-key"}};
    const auto transactions_before = store_.transactions.size();
    const auto groups_before = store_.transfer_groups.size();
    const auto outbox_before = store_.outbox.size();

    const auto first = request(
        HttpMethod::Post, "/api/v1/transfers", body, token, {}, key);
    const auto replay = request(
        HttpMethod::Post, "/api/v1/transfers", body, token, {}, key);
    ASSERT_EQ(first.status, 201) << first.body;
    ASSERT_EQ(replay.status, 201) << replay.body;
    EXPECT_EQ(replay.body, first.body);

    const auto user_id = user["userId"].get<std::int64_t>();
    auto& idempotency = store_.idempotency.at(
        std::to_string(user_id) + "\ncreate_transfer\ntransfer-operation-key");
    const auto complete_values = idempotency.response_values;
    idempotency.response_values = {
        {"transfer_group_id", complete_values.at("transfer_group_id")},
        {"outgoing_transaction_id", complete_values.at("outgoing_transaction_id")},
        {"incoming_transaction_id", complete_values.at("incoming_transaction_id")},
        {"outgoing_amount", complete_values.at("outgoing_amount")},
        {"incoming_amount", complete_values.at("incoming_amount")},
        {"rate", complete_values.at("rate")},
        {"fee_amount", complete_values.at("fee_amount")}};
    const auto legacy_replay = request(
        HttpMethod::Post, "/api/v1/transfers", body, token, {}, key);
    ASSERT_EQ(legacy_replay.status, 201) << legacy_replay.body;
    EXPECT_EQ(legacy_replay.body, first.body);
    EXPECT_EQ(store_.transfer_groups.size(), groups_before + 1U);
    EXPECT_EQ(store_.transactions.size(), transactions_before + 3U);
    EXPECT_EQ(store_.outbox.size(), outbox_before + 1U);

    auto changed = body;
    changed["incomingAmount"] = "11";
    EXPECT_EQ(request(
        HttpMethod::Post, "/api/v1/transfers", changed, token, {}, key).status,
        409);
    EXPECT_EQ(store_.transfer_groups.size(), groups_before + 1U);
}

TEST_F(ResourceApiTest, TransferValidationRollsBackWithoutPartialRows) {
    const auto user = register_user("transfer-invalid@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto source = create_account(token)["id"].get<std::int64_t>();
    const auto target_response = request(
        HttpMethod::Post, "/api/v1/accounts",
        {{"name", "Target"}, {"type", "savings"}, {"subtype", "bank"},
         {"currencyCode", "USD"}}, token);
    ASSERT_EQ(target_response.status, 201) << target_response.body;
    const auto target =
        nlohmann::json::parse(target_response.body)["id"].get<std::int64_t>();
    const auto groups_before = store_.transfer_groups.size();
    const auto transactions_before = store_.transactions.size();

    const auto bad_shape = request(
        HttpMethod::Post, "/api/v1/transfers",
        {{"sourceAccountId", source}, {"targetAccountId", target},
         {"mode", "OutgoingAndRate"}, {"outgoingAmount", "10"},
         {"incomingAmount", "2"}, {"rate", "0.2"}}, token);
    EXPECT_EQ(bad_shape.status, 400);
    auto numeric_rate = nlohmann::json{
        {"sourceAccountId", source}, {"targetAccountId", target},
        {"mode", "OutgoingAndRate"}, {"outgoingAmount", "10"},
        {"rate", 0.2}};
    EXPECT_EQ(request(
        HttpMethod::Post, "/api/v1/transfers", numeric_rate, token).status, 400);
    auto padded_rate = numeric_rate;
    padded_rate["rate"] = " 0.2";
    EXPECT_EQ(request(
        HttpMethod::Post, "/api/v1/transfers", padded_rate, token).status, 400);
    const auto bad_fee = request(
        HttpMethod::Post, "/api/v1/transfers",
        {{"sourceAccountId", source}, {"targetAccountId", target},
         {"mode", "BothAmounts"}, {"outgoingAmount", "10"},
         {"incomingAmount", "2"}, {"feeAmount", "1"},
         {"feeSource", "ThirdParty"}, {"feeAccountId", 999999}}, token);
    EXPECT_EQ(bad_fee.status, 404);
    EXPECT_EQ(store_.transfer_groups.size(), groups_before);
    EXPECT_EQ(store_.transactions.size(), transactions_before);
}

TEST_F(ResourceApiTest, ReportApiUsesUserTimezoneMonthWindowsAndRootCategories) {
    const auto user = register_user("reports@example.com");
    const auto token = user["accessToken"].get<std::string>();
    const auto account_id = create_account(token)["id"].get<std::int64_t>();
    const auto root_response = request(
        HttpMethod::Post, "/api/v1/categories",
        {{"board", "expense"}, {"name", "Food Report"}}, token);
    ASSERT_EQ(root_response.status, 201) << root_response.body;
    const auto root_id =
        nlohmann::json::parse(root_response.body)["id"].get<std::int64_t>();
    const auto child_response = request(
        HttpMethod::Post, "/api/v1/categories",
        {{"board", "expense"}, {"name", "Dining Report"},
         {"parentId", root_id}}, token);
    ASSERT_EQ(child_response.status, 201) << child_response.body;
    const auto child_id =
        nlohmann::json::parse(child_response.body)["id"].get<std::int64_t>();

    const auto post_transaction = [&](std::string type, std::string amount,
                                      std::string occurred_at,
                                      std::optional<std::int64_t> category,
                                      std::string description) {
        nlohmann::json body{
            {"accountId", account_id}, {"type", std::move(type)},
            {"amount", std::move(amount)}, {"currencyCode", "CNY"},
            {"occurredAt", std::move(occurred_at)},
            {"description", std::move(description)}};
        if (category.has_value()) body["categoryId"] = *category;
        const auto response = request(
            HttpMethod::Post, "/api/v1/transactions", body, token);
        EXPECT_EQ(response.status, 201) << response.body;
    };
    // Local Shanghai month boundary: these are one second apart but belong to
    // June and July respectively.
    post_transaction(
        "income", "100", "2026-06-30T23:59:59+08:00",
        std::nullopt, "June income");
    post_transaction(
        "income", "200", "2026-07-01T00:00:00+08:00",
        std::nullopt, "=SUM(1,2)");
    post_transaction(
        "expense", "50", "2026-07-10T12:00:00+08:00",
        child_id, "Dining");

    const auto cash_flow = request(
        HttpMethod::Get, "/api/v1/reports/cash-flow", {}, token,
        {{"startDate", "2026-06"}, {"endDate", "2026-07"},
         {"periodType", "MONTH"}});
    ASSERT_EQ(cash_flow.status, 200) << cash_flow.body;
    const auto trends = nlohmann::json::parse(cash_flow.body)["trends"];
    ASSERT_EQ(trends.size(), 2U);
    EXPECT_EQ(trends[0]["period"], "2026-06");
    EXPECT_EQ(trends[0]["income"], "100");
    EXPECT_EQ(trends[1]["period"], "2026-07");
    EXPECT_EQ(trends[1]["income"], "200");
    EXPECT_EQ(trends[1]["expense"], "50");

    const auto dashboard = request(
        HttpMethod::Get, "/api/v1/reports/dashboard-summary", {}, token);
    ASSERT_EQ(dashboard.status, 200) << dashboard.body;
    const auto dashboard_body = nlohmann::json::parse(dashboard.body);
    EXPECT_EQ(dashboard_body["monthlyIncome"], "200");
    EXPECT_EQ(dashboard_body["monthlyExpense"], "50");
    EXPECT_EQ(dashboard_body["generatedAt"], "2026-07-14T00:00:00Z");
    EXPECT_EQ(
        dashboard_body["netWorth"]["generatedAt"],
        dashboard_body["generatedAt"]);
    ASSERT_EQ(dashboard_body["topExpenseCategories"].size(), 1U);
    EXPECT_EQ(dashboard_body["topExpenseCategories"][0]["categoryId"], root_id);
    EXPECT_EQ(dashboard_body["topExpenseCategories"][0]["categoryName"], "Food Report");

    const auto analysis = request(
        HttpMethod::Get, "/api/v1/reports/analysis", {}, token,
        {{"startDate", "2026-06"}, {"endDate", "2026-07"},
         {"dimension", "root_category"}});
    ASSERT_EQ(analysis.status, 200) << analysis.body;
    const auto analysis_body = nlohmann::json::parse(analysis.body);
    EXPECT_EQ(analysis_body["baseCurrency"], "CNY");
    EXPECT_EQ(analysis_body["rateStatus"], "historical");
    EXPECT_EQ(analysis_body["dimension"], "root_category");
    EXPECT_FALSE(analysis_body["dimensionOverlaps"].get<bool>());
    ASSERT_EQ(analysis_body["netWorthTrend"].size(), 2U);
    EXPECT_EQ(analysis_body["netWorthTrend"][0]["netWorth"], "100");
    EXPECT_EQ(analysis_body["netWorthTrend"][1]["netWorth"], "250");
    const auto& breakdown = analysis_body["breakdown"];
    ASSERT_EQ(breakdown.size(), 2U);
    EXPECT_EQ(breakdown[0]["label"], "Food Report");
    EXPECT_EQ(breakdown[0]["expense"], "50");
    EXPECT_EQ(breakdown[1]["label"], "Uncategorized");
    EXPECT_EQ(breakdown[1]["income"], "300");

    const auto exported = request(
        HttpMethod::Get, "/api/v1/exports/transactions.csv", {}, token,
        {{"from", "2026-06-01T00:00:00+08:00"},
         {"to", "2026-08-01T00:00:00+08:00"}});
    ASSERT_EQ(exported.status, 200) << exported.body;
    EXPECT_EQ(exported.headers.at("Content-Type"), "text/csv; charset=utf-8");
    EXPECT_EQ(exported.headers.at("X-Export-Row-Count"), "3");
    EXPECT_NE(
        exported.headers.at("Content-Disposition").find("transactions-20260601-20260731.csv"),
        std::string::npos);
    EXPECT_NE(exported.body.find("\"2026-07-01T00:00:00+08:00\""), std::string::npos);
    EXPECT_NE(exported.body.find("\"'=SUM(1,2)\""), std::string::npos);
    EXPECT_NE(exported.body.find("\"50\""), std::string::npos);

    ASSERT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/categories/" + std::to_string(child_id), {}, token).status, 204);
    ASSERT_EQ(request(
        HttpMethod::Delete,
        "/api/v1/categories/" + std::to_string(root_id), {}, token).status, 204);
    const auto historical = request(
        HttpMethod::Get, "/api/v1/reports/dashboard-summary", {}, token);
    ASSERT_EQ(historical.status, 200) << historical.body;
    const auto historical_categories =
        nlohmann::json::parse(historical.body)["topExpenseCategories"];
    ASSERT_EQ(historical_categories.size(), 1U);
    EXPECT_EQ(historical_categories[0]["categoryName"], "Food Report");
}

TEST_F(ResourceApiTest, ReportApiEnforcesRangeAndMissingRateErrors) {
    const auto user = register_user("report-errors@example.com");
    const auto token = user["accessToken"].get<std::string>();
    EXPECT_EQ(request(
        HttpMethod::Get, "/api/v1/reports/cash-flow", {}, token,
        {{"startDate", "2026-07"}, {"endDate", "2026-01"},
         {"periodType", "MONTH"}}).status, 400);
    EXPECT_EQ(request(
        HttpMethod::Get, "/api/v1/reports/cash-flow", {}, token,
        {{"startDate", "2026-07"}, {"endDate", "2026-08"},
         {"periodType", "DAY"}}).status, 400);
    EXPECT_EQ(request(
        HttpMethod::Get, "/api/v1/reports/cash-flow", {}, token,
        {{"startDate", "0001-01"}, {"endDate", "0001-01"},
         {"periodType", "MONTH"}}).status, 400);

    const auto usd_account = request(
        HttpMethod::Post, "/api/v1/accounts",
        {{"name", "USD Missing Rate"}, {"type", "savings"},
         {"subtype", "bank"}, {"currencyCode", "USD"}}, token);
    ASSERT_EQ(usd_account.status, 201) << usd_account.body;
    const auto account_id =
        nlohmann::json::parse(usd_account.body)["id"].get<std::int64_t>();
    const auto posted = request(
        HttpMethod::Post, "/api/v1/transactions",
        {{"accountId", account_id}, {"type", "income"}, {"amount", "10"},
         {"currencyCode", "USD"}}, token);
    ASSERT_EQ(posted.status, 201) << posted.body;
    EXPECT_EQ(request(
        HttpMethod::Get, "/api/v1/reports/net-worth", {}, token).status, 422);
}

TEST_F(ResourceApiTest, EmptyReportDoesNotLeakAnotherUsersFinancialData) {
    const auto alice = register_user("report-owner@example.com");
    const auto bob = register_user("report-reader@example.com");
    const auto alice_token = alice["accessToken"].get<std::string>();
    const auto bob_token = bob["accessToken"].get<std::string>();
    const auto account = create_account(alice_token);
    ASSERT_EQ(request(
        HttpMethod::Post, "/api/v1/transactions",
        {{"accountId", account["id"]}, {"type", "income"},
         {"amount", "99"}, {"currencyCode", "CNY"}}, alice_token).status, 201);

    const auto bob_net_worth = request(
        HttpMethod::Get, "/api/v1/reports/net-worth", {}, bob_token);
    ASSERT_EQ(bob_net_worth.status, 200) << bob_net_worth.body;
    EXPECT_EQ(nlohmann::json::parse(bob_net_worth.body)["netWorth"], "0");

    const auto bob_analysis = request(
        HttpMethod::Get, "/api/v1/reports/analysis", {}, bob_token,
        {{"startDate", "2026-07"}, {"endDate", "2026-07"},
         {"dimension", "account"}});
    ASSERT_EQ(bob_analysis.status, 200) << bob_analysis.body;
    const auto bob_report = nlohmann::json::parse(bob_analysis.body);
    ASSERT_EQ(bob_report["netWorthTrend"].size(), 1U);
    EXPECT_EQ(bob_report["netWorthTrend"][0]["netWorth"], "0");
    EXPECT_TRUE(bob_report["breakdown"].empty());

    const std::map<std::string, std::string> export_range{
        {"from", "2026-07-01T00:00:00+08:00"},
        {"to", "2026-08-01T00:00:00+08:00"}};
    const auto alice_export = request(
        HttpMethod::Get, "/api/v1/exports/transactions.csv", {},
        alice_token, export_range);
    const auto bob_export = request(
        HttpMethod::Get, "/api/v1/exports/transactions.csv", {},
        bob_token, export_range);
    ASSERT_EQ(alice_export.status, 200) << alice_export.body;
    ASSERT_EQ(bob_export.status, 200) << bob_export.body;
    EXPECT_EQ(alice_export.headers.at("X-Export-Row-Count"), "1");
    EXPECT_EQ(bob_export.headers.at("X-Export-Row-Count"), "0");
    EXPECT_NE(alice_export.body.find("\"99\""), std::string::npos);
    EXPECT_EQ(bob_export.body.find("\"99\""), std::string::npos);
}

TEST_F(ResourceApiTest, NonFinancialCreatesReplayExactlyOncePerTenant) {
    const auto alice = register_user("alice-idempotent-resources@example.com");
    const auto token = alice["accessToken"].get<std::string>();
    const nlohmann::json account_request{
        {"name", "Idempotent Wallet"},
        {"type", "digital_wallet"},
        {"subtype", "wallet"},
        {"currencyCode", "CNY"},
        {"description", ""}};
    const std::map<std::string, std::string> account_key{
        {"Idempotency-Key", "account-intent-1"}};
    const auto created = request(
        HttpMethod::Post,
        "/api/v1/accounts",
        account_request,
        token,
        {},
        account_key);
    const auto replayed = request(
        HttpMethod::Post,
        "/api/v1/accounts",
        account_request,
        token,
        {},
        account_key);
    ASSERT_EQ(created.status, 201) << created.body;
    ASSERT_EQ(replayed.status, 201) << replayed.body;
    EXPECT_EQ(created.body, replayed.body);
    EXPECT_EQ(store_.accounts.size(), 1U);

    auto changed_account = account_request;
    changed_account["name"] = "Different Wallet";
    EXPECT_EQ(request(
        HttpMethod::Post,
        "/api/v1/accounts",
        changed_account,
        token,
        {},
        account_key).status, 409);

    const nlohmann::json category_request{
        {"board", "expense"}, {"name", "Learning"}};
    const std::map<std::string, std::string> category_key{
        {"Idempotency-Key", "category-intent-1"}};
    const auto category = request(
        HttpMethod::Post,
        "/api/v1/categories",
        category_request,
        token,
        {},
        category_key);
    const auto category_replay = request(
        HttpMethod::Post,
        "/api/v1/categories",
        category_request,
        token,
        {},
        category_key);
    ASSERT_EQ(category.status, 201) << category.body;
    EXPECT_EQ(category.body, category_replay.body);

    const nlohmann::json tag_request{{"name", "Quarterly"}};
    const std::map<std::string, std::string> tag_key{
        {"Idempotency-Key", "tag-intent-1"}};
    const auto tag = request(
        HttpMethod::Post,
        "/api/v1/tags",
        tag_request,
        token,
        {},
        tag_key);
    const auto tag_replay = request(
        HttpMethod::Post,
        "/api/v1/tags",
        tag_request,
        token,
        {},
        tag_key);
    ASSERT_EQ(tag.status, 201) << tag.body;
    EXPECT_EQ(tag.body, tag_replay.body);
}

TEST_F(ResourceApiTest, MaintenanceAuditAndBalanceRebuildStayTenantScoped) {
    const auto alice = register_user("alice-maintenance@example.com");
    const auto bob = register_user("bob-maintenance@example.com");
    const auto alice_token = alice["accessToken"].get<std::string>();
    const auto bob_token = bob["accessToken"].get<std::string>();
    const auto account = create_account(alice_token);
    const auto account_id = account["id"].get<std::int64_t>();
    const auto transaction = request(
        HttpMethod::Post,
        "/api/v1/transactions",
        {{"accountId", account_id},
         {"type", "income"},
         {"amount", "125.50"},
         {"currencyCode", "CNY"},
         {"description", "Maintenance baseline"}},
        alice_token);
    ASSERT_EQ(transaction.status, 201) << transaction.body;

    const auto rebuilt = request(
        HttpMethod::Post,
        "/api/v1/maintenance/accounts/" + std::to_string(account_id) +
            "/balance-cache/rebuild",
        {},
        alice_token);
    ASSERT_EQ(rebuilt.status, 200) << rebuilt.body;
    const auto rebuilt_body = nlohmann::json::parse(rebuilt.body);
    ASSERT_EQ(rebuilt_body["accounts"].size(), 1U);
    EXPECT_EQ(rebuilt_body["accounts"][0]["balance"], "125.5");
    EXPECT_GT(rebuilt_body["accounts"][0]["sourceVersion"].get<int>(), 0);
    EXPECT_EQ(request(
        HttpMethod::Post,
        "/api/v1/maintenance/accounts/" + std::to_string(account_id) +
            "/balance-cache/rebuild",
        {},
        bob_token).status, 404);

    const auto alice_audit = request(
        HttpMethod::Get,
        "/api/v1/maintenance/audit-logs",
        {},
        alice_token,
        {{"resourceType", "BalanceCache"}});
    ASSERT_EQ(alice_audit.status, 200) << alice_audit.body;
    const auto alice_body = nlohmann::json::parse(alice_audit.body);
    ASSERT_EQ(alice_body["items"].size(), 1U);
    EXPECT_EQ(alice_body["items"][0]["resourceId"],
              std::to_string(account_id));
    EXPECT_TRUE(alice_body["items"][0]["traceId"].is_string());
    EXPECT_FALSE(alice_body["items"][0].contains("beforeValue"));
    EXPECT_FALSE(alice_body["items"][0].contains("afterValue"));
    EXPECT_FALSE(alice_body["items"][0].contains("metadata"));

    const auto bob_audit = request(
        HttpMethod::Get,
        "/api/v1/maintenance/audit-logs",
        {},
        bob_token,
        {{"resourceType", "BalanceCache"}});
    ASSERT_EQ(bob_audit.status, 200) << bob_audit.body;
    EXPECT_TRUE(nlohmann::json::parse(bob_audit.body)["items"].empty());
}

TEST_F(ResourceApiTest, MaintenanceAuditCursorDoesNotDependOnOccurredAtOrder) {
    const auto registered = register_user("audit-cursor@example.com");
    const auto user_id = registered["userId"].get<std::int64_t>();
    const auto token = registered["accessToken"].get<std::string>();
    const auto base = clock_.now();
    for (const auto offset : {3, 1, 2}) {
        domain::AuditLogEntry entry;
        entry.id = store_.next_audit_log_id++;
        entry.operator_user_id = domain::UserId(user_id);
        entry.actor_type = domain::AuditActorType::User;
        entry.action = domain::AuditAction::Update;
        entry.resource_type = "CursorProbe";
        entry.resource_id = std::to_string(offset);
        entry.metadata_json = "{}";
        entry.occurred_at = base + std::chrono::hours(offset);
        store_.audit_logs.push_back(std::move(entry));
    }

    const auto first = request(
        HttpMethod::Get,
        "/api/v1/maintenance/audit-logs",
        {},
        token,
        {{"resourceType", "CursorProbe"}, {"pageSize", "2"}});
    ASSERT_EQ(first.status, 200) << first.body;
    const auto first_body = nlohmann::json::parse(first.body);
    ASSERT_EQ(first_body["items"].size(), 2U);
    ASSERT_TRUE(first_body["nextCursor"].is_string());

    const auto second = request(
        HttpMethod::Get,
        "/api/v1/maintenance/audit-logs",
        {},
        token,
        {{"resourceType", "CursorProbe"},
         {"pageSize", "2"},
         {"cursor", first_body["nextCursor"].get<std::string>()}});
    ASSERT_EQ(second.status, 200) << second.body;
    const auto second_body = nlohmann::json::parse(second.body);
    ASSERT_EQ(second_body["items"].size(), 1U);

    std::set<std::int64_t> ids;
    for (const auto& item : first_body["items"]) {
        ids.insert(item["id"].get<std::int64_t>());
    }
    ids.insert(second_body["items"][0]["id"].get<std::int64_t>());
    EXPECT_EQ(ids.size(), 3U);
}

TEST_F(ResourceApiTest, OperatorRoutesUseCurrentServerRoleAndSanitizeDeadLetters) {
    EXPECT_EQ(request(HttpMethod::Get, "/livez").status, 200);
    EXPECT_EQ(request(HttpMethod::Get, "/readyz").status, 200);

    const auto registered = register_user("phase2-operator@example.com");
    const auto user_id = registered["userId"].get<std::int64_t>();
    const auto user_token = registered["accessToken"].get<std::string>();
    EXPECT_EQ(request(
        HttpMethod::Get,
        "/api/v1/operations/summary",
        {},
        user_token).status, 403);

    auto& record = store_.users.at(user_id);
    record.user = domain::User(
        domain::UserId(user_id),
        record.user.username(),
        domain::UserRole::Operator);
    EXPECT_EQ(request(
        HttpMethod::Get,
        "/api/v1/operations/summary",
        {},
        user_token).status, 401);

    const auto login = request(
        HttpMethod::Post,
        "/api/v1/auth/login",
        {{"username", "phase2-operator@example.com"},
         {"password", "correct horse battery staple"}});
    ASSERT_EQ(login.status, 200) << login.body;
    const auto login_body = nlohmann::json::parse(login.body);
    ASSERT_EQ(login_body["roles"], nlohmann::json::array({"OPERATOR"}));
    const auto operator_token = login_body["accessToken"].get<std::string>();

    application::OutboxMessage dead_letter;
    dead_letter.id = "dead-letter-1";
    dead_letter.event_name = "SensitiveEvent";
    dead_letter.aggregate_type = "Account";
    dead_letter.aggregate_id = "42";
    dead_letter.payload_json = R"({"token":"must-not-leak"})";
    dead_letter.status = application::OutboxStatus::DeadLetter;
    dead_letter.retry_count = 5;
    dead_letter.max_retry_count = 5;
    dead_letter.last_error = "database password must-not-leak";
    dead_letter.last_failed_handler = "projection";
    dead_letter.last_failed_at = clock_.now();
    dead_letter.created_at = clock_.now();
    store_.outbox.push_back(dead_letter);

    const auto listed = request(
        HttpMethod::Get,
        "/api/v1/operations/dead-letters",
        {},
        operator_token);
    ASSERT_EQ(listed.status, 200) << listed.body;
    EXPECT_EQ(listed.body.find("must-not-leak"), std::string::npos);
    const auto listed_body = nlohmann::json::parse(listed.body);
    ASSERT_EQ(listed_body["items"].size(), 1U);
    EXPECT_FALSE(listed_body["items"][0].contains("payload"));
    EXPECT_FALSE(listed_body["items"][0].contains("lastError"));

    const std::map<std::string, std::string> retry_key{
        {"Idempotency-Key", "dead-letter-retry-1"}};
    EXPECT_EQ(request(
        HttpMethod::Post,
        "/api/v1/operations/dead-letters/dead-letter-1/retry",
        {},
        operator_token,
        {},
        {{"Idempotency-Key", "invalid key"}}).status, 400);
    const auto retried = request(
        HttpMethod::Post,
        "/api/v1/operations/dead-letters/dead-letter-1/retry",
        {},
        operator_token,
        {},
        retry_key);
    const auto replayed = request(
        HttpMethod::Post,
        "/api/v1/operations/dead-letters/dead-letter-1/retry",
        {},
        operator_token,
        {},
        retry_key);
    ASSERT_EQ(retried.status, 202) << retried.body;
    ASSERT_EQ(replayed.status, 202) << replayed.body;
    EXPECT_FALSE(nlohmann::json::parse(retried.body)["replayed"]);
    EXPECT_TRUE(nlohmann::json::parse(replayed.body)["replayed"]);
    const auto retried_event = std::ranges::find_if(
        store_.outbox,
        [](const auto& message) { return message.id == "dead-letter-1"; });
    ASSERT_NE(retried_event, store_.outbox.end());
    EXPECT_EQ(retried_event->status, application::OutboxStatus::Failed);
    EXPECT_EQ(store_.outbox_retry_commands.size(), 1U);
    EXPECT_EQ(std::ranges::count_if(
        store_.audit_logs,
        [](const auto& entry) {
            return entry.actor_type == domain::AuditActorType::Operator &&
                   entry.action == domain::AuditAction::Retry;
        }), 1);

    const auto summary = request(
        HttpMethod::Get,
        "/api/v1/operations/summary",
        {},
        operator_token);
    ASSERT_EQ(summary.status, 200) << summary.body;
    EXPECT_EQ(request(
        HttpMethod::Get,
        "/api/v1/operations/metrics",
        {},
        operator_token).status, 200);
}

} // namespace pfh::test
