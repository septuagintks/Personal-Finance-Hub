// Personal Finance Hub - Foundational Resource API Tests

#include "pfh/application/services/auth_service.h"
#include "pfh/application/services/finance_application_service.h"
#include "pfh/infrastructure/persistence/in_memory_audit_log_repository.h"
#include "pfh/infrastructure/persistence/in_memory_auth_session_repository.h"
#include "pfh/infrastructure/persistence/in_memory_registration_defaults_repository.h"
#include "pfh/infrastructure/persistence/in_memory_request_scope.h"
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
        std::string_view session_id,
        AuthTimePoint issued_at) const override {
        AccessTokenClaims claims;
        claims.issuer = "pfh-api";
        claims.audience = "pfh-client";
        claims.user_id = user_id;
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
          auth_controller_(auth_service_),
          account_controller_(finance_),
          category_controller_(finance_),
          tag_controller_(finance_),
          preference_controller_(finance_),
          currency_controller_(finance_),
          transaction_controller_(finance_),
          transfer_controller_(finance_),
          report_controller_(finance_),
          jwt_filter_(tokens_, sessions_, clock_),
          app_(
              auth_controller_, jwt_filter_, account_controller_,
              category_controller_, tag_controller_, preference_controller_,
              currency_controller_, transaction_controller_, transfer_controller_,
              report_controller_) {}

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
            (value.path == "/api/v1/transactions" ||
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
    AuthController auth_controller_;
    AccountController account_controller_;
    CategoryController category_controller_;
    TagController tag_controller_;
    PreferenceController preference_controller_;
    CurrencyController currency_controller_;
    TransactionController transaction_controller_;
    TransferController transfer_controller_;
    ReportController report_controller_;
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
    EXPECT_EQ(nlohmann::json::parse(activated.body)["source"], "system");
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
    EXPECT_EQ(request(HttpMethod::Delete, query_path, {}, alice_token).status, 404);
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
                                      std::optional<std::int64_t> category) {
        nlohmann::json body{
            {"accountId", account_id}, {"type", std::move(type)},
            {"amount", std::move(amount)}, {"currencyCode", "CNY"},
            {"occurredAt", std::move(occurred_at)}};
        if (category.has_value()) body["categoryId"] = *category;
        const auto response = request(
            HttpMethod::Post, "/api/v1/transactions", body, token);
        EXPECT_EQ(response.status, 201) << response.body;
    };
    // Local Shanghai month boundary: these are one second apart but belong to
    // June and July respectively.
    post_transaction("income", "100", "2026-06-30T23:59:59+08:00", std::nullopt);
    post_transaction("income", "200", "2026-07-01T00:00:00+08:00", std::nullopt);
    post_transaction("expense", "50", "2026-07-10T12:00:00+08:00", child_id);

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
}

} // namespace pfh::test
