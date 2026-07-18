// Personal Finance Hub - Production Database Client Factory

#include "pfh/bootstrap/database_client_factory.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <string>
#include <string_view>

namespace pfh::bootstrap {

namespace {

[[nodiscard]] std::string quote_conninfo(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('\'');
    for (const char c : value) {
        if (c == '\\' || c == '\'') {
            result.push_back('\\');
        }
        result.push_back(c);
    }
    result.push_back('\'');
    return result;
}

[[nodiscard]] std::string connection_string(
    const infrastructure::DatabaseConfig& config) {
    return "host=" + quote_conninfo(config.host) +
           " port=" + std::to_string(config.port) +
           " dbname=" + quote_conninfo(config.name) +
           " user=" + quote_conninfo(config.user) +
           " password=" + quote_conninfo(config.password) +
           " connect_timeout=" +
           std::to_string(config.connection_timeout.count());
}

[[nodiscard]] application::Error startup_database_error(
    std::string message) {
    return application::Error(
        application::ErrorCode::DatabaseConnectionFailed,
        std::move(message));
}

} // namespace

application::Result<drogon::orm::DbClientPtr> DatabaseClientFactory::create(
    const infrastructure::DatabaseConfig& config) {
    try {
        auto client = drogon::orm::DbClient::newPgClient(
            connection_string(config), config.pool_size);
        if (!client) {
            return application::err(startup_database_error(
                "Database client creation failed"));
        }
        return client;
    } catch (const std::exception&) {
        return application::err(startup_database_error(
            "Database client creation failed"));
    }
}

application::VoidResult DatabaseClientFactory::verify_request_role(
    const drogon::orm::DbClientPtr& client,
    std::string_view expected_role) {
    try {
        constexpr const char* kSql = R"SQL(
            SELECT current_user, rolbypassrls, rolsuper, rolcreatedb,
                   rolcreaterole, rolreplication, rolinherit
            FROM pg_roles
            WHERE rolname = current_user
        )SQL";
        const auto result = client->execSqlSync(kSql);
        if (result.empty() || result[0][0].as<std::string>() != expected_role ||
            result[0][1].as<bool>() || result[0][2].as<bool>() ||
            result[0][3].as<bool>() || result[0][4].as<bool>() ||
            result[0][5].as<bool>() || result[0][6].as<bool>()) {
            return application::err(startup_database_error(
                "Request database role violates the RLS security policy"));
        }
        const auto read_only = client->execSqlSync(
            "SHOW default_transaction_read_only");
        if (read_only.empty() ||
            read_only[0][0].as<std::string>() != "off") {
            return application::err(startup_database_error(
                "Request database role must allow write transactions"));
        }
        return application::ok();
    } catch (const std::exception&) {
        return application::err(startup_database_error(
            "Request database role validation failed"));
    }
}

application::VoidResult DatabaseClientFactory::verify_background_role(
    const drogon::orm::DbClientPtr& client,
    std::string_view expected_role) {
    try {
        constexpr const char* kRoleSql = R"SQL(
            SELECT current_user, rolbypassrls, rolsuper, rolcreatedb,
                   rolcreaterole, rolreplication, rolinherit
            FROM pg_roles
            WHERE rolname = current_user
        )SQL";
        const auto role = client->execSqlSync(kRoleSql);
        if (role.empty() || role[0][0].as<std::string>() != expected_role ||
            !role[0][1].as<bool>() || role[0][2].as<bool>() ||
            role[0][3].as<bool>() || role[0][4].as<bool>() ||
            role[0][5].as<bool>() || role[0][6].as<bool>()) {
            return application::err(startup_database_error(
                "Background database role must be non-superuser with BYPASSRLS"));
        }
        const auto read_only = client->execSqlSync("SHOW default_transaction_read_only");
        if (read_only.empty() || read_only[0][0].as<std::string>() != "on") {
            return application::err(startup_database_error(
                "Background database role must default to read-only transactions"));
        }
        return application::ok();
    } catch (const std::exception&) {
        return application::err(startup_database_error(
            "Background database role validation failed"));
    }
}

} // namespace pfh::bootstrap

#endif // PFH_HAS_POSTGRESQL
