// Personal Finance Hub - JSON Configuration Loader Implementation
// Version: 1.0
// C++23
// This file implements configuration loading from JSON files

#pragma once

#include "pfh/infrastructure/config.h"
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <nlohmann/json.hpp>

namespace pfh::infrastructure {

/// @brief JSON-based configuration loader
class JsonConfigLoader : public IConfigLoader {
public:
    /// @brief Constructor with config file path
    /// @param config_path Path to JSON config file (e.g., "config/config.local.json")
    explicit JsonConfigLoader(std::filesystem::path config_path)
        : config_path_(std::move(config_path)) {}

    /// @brief Load configuration from JSON file
    [[nodiscard]] application::Result<AppConfig> load() override {
        using application::Error;

        // Check if file exists
        if (!std::filesystem::exists(config_path_)) {
            return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                "Config file not found", config_path_.string()));
        }

        // Read file content
        std::ifstream file(config_path_);
        if (!file.is_open()) {
            return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                "Failed to open config file", config_path_.string()));
        }

        // Parse JSON
        nlohmann::json json;
        try {
            file >> json;
        } catch (const nlohmann::json::exception& e) {
            return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                "Failed to parse JSON", e.what()));
        }

        // Convert to AppConfig
        try {
            AppConfig config;

            config.environment = json.value("environment", std::string("development"));

            // Server config
            if (json.contains("server")) {
                const auto& server = json["server"];
                config.server.host = server.value("host", std::string("0.0.0.0"));
                config.server.port = server.value<std::uint16_t>("port", 8080);
                config.server.threads = server.value<std::uint32_t>("threads", 4);
                config.server.request_worker_threads = server.value<std::uint32_t>(
                    "request_worker_threads", 8);
                config.server.request_queue_capacity = server.value<std::uint32_t>(
                    "request_queue_capacity", 256);
                config.server.request_queue_byte_capacity = server.value<std::uint64_t>(
                    "request_queue_byte_capacity", 16U * 1024U * 1024U);
                config.server.maximum_request_body_bytes = server.value<std::uint64_t>(
                    "maximum_request_body_bytes", 1024U * 1024U);
                config.server.auth_worker_threads = server.value<std::uint32_t>(
                    "auth_worker_threads", 2);
                config.server.auth_queue_capacity = server.value<std::uint32_t>(
                    "auth_queue_capacity", 16);
                config.server.auth_queue_byte_capacity = server.value<std::uint64_t>(
                    "auth_queue_byte_capacity", 1024U * 1024U);
                config.server.auth_rate_limit_attempts = server.value<std::uint32_t>(
                    "auth_rate_limit_attempts", 20);
                config.server.auth_rate_limit_window = std::chrono::seconds(
                    server.value<std::int64_t>(
                        "auth_rate_limit_window_seconds", 60));
                config.server.auth_rate_limit_sources = server.value<std::uint32_t>(
                    "auth_rate_limit_sources", 10'000);
            }

            // Database config
            if (json.contains("database")) {
                const auto& db = json["database"];
                config.database.host = db.value("host", std::string("localhost"));
                config.database.port = db.value<std::uint16_t>("port", 5432);
                config.database.name = db.value("name", std::string("pfh_dev"));
                config.database.user = db.value("user", std::string("pfh_user"));
                config.database.password = db.value("password", std::string(""));
                config.database.pool_size = db.value<std::uint32_t>("pool_size", 10);
                config.database.connection_timeout = std::chrono::seconds(
                    db.value<std::int64_t>("connection_timeout", 30)
                );
            }

            if (json.contains("background_database")) {
                const auto& db = json["background_database"];
                config.background_database.host = db.value(
                    "host", config.database.host);
                config.background_database.port = db.value<std::uint16_t>(
                    "port", config.database.port);
                config.background_database.name = db.value(
                    "name", config.database.name);
                config.background_database.user = db.value(
                    "user", std::string("pfh_background"));
                config.background_database.password = db.value(
                    "password", std::string(""));
                config.background_database.pool_size = db.value<std::uint32_t>(
                    "pool_size", 2);
                config.background_database.connection_timeout = std::chrono::seconds(
                    db.value<std::int64_t>("connection_timeout", 30));
            } else {
                config.background_database.host = config.database.host;
                config.background_database.port = config.database.port;
                config.background_database.name = config.database.name;
            }

            // JWT config
            if (json.contains("jwt")) {
                const auto& jwt = json["jwt"];
                config.jwt.secret = jwt.value("secret", std::string(""));
                config.jwt.issuer = jwt.value("issuer", std::string("pfh-api"));
                config.jwt.audience = jwt.value("audience", std::string("pfh-client"));
                config.jwt.access_token_expiry = std::chrono::seconds(
                    jwt.value<std::int64_t>("access_token_expiry_seconds", 900)
                );
                config.jwt.refresh_token_expiry = std::chrono::seconds(
                    jwt.value<std::int64_t>("refresh_token_expiry_seconds", 2592000)
                );
                config.jwt.clock_skew = std::chrono::seconds(
                    jwt.value<std::int64_t>("clock_skew_seconds", 30));
            }

            // Logging config
            if (json.contains("logging")) {
                const auto& logging = json["logging"];
                config.logging.level = parse_log_level(logging.value("level", std::string("info")));
                config.logging.output = parse_log_output(logging.value("output", std::string("console")));
                config.logging.file = logging.value("file", std::string("logs/pfh.log"));
                config.logging.maximum_file_size_bytes =
                    logging.value<std::uint64_t>(
                        "maximum_file_size_bytes", 10U * 1024U * 1024U);
                config.logging.maximum_file_count =
                    logging.value<std::uint32_t>(
                        "maximum_file_count", 5);
            }

            // Scheduler config
            if (json.contains("scheduler")) {
                const auto& scheduler = json["scheduler"];
                config.scheduler.enabled = scheduler.value("enabled", true);
                config.scheduler.worker_threads = scheduler.value<std::uint32_t>(
                    "worker_threads", 2);
                config.scheduler.queue_capacity = scheduler.value<std::uint32_t>(
                    "queue_capacity", 64);
                config.scheduler.outbox_publish_interval = std::chrono::seconds(
                    scheduler.value<std::int64_t>(
                        "outbox_publish_interval_seconds", 5));
                config.scheduler.outbox_batch_size = scheduler.value<std::uint32_t>(
                    "outbox_batch_size", 100);
                config.scheduler.outbox_processing_timeout = std::chrono::seconds(
                    scheduler.value<std::int64_t>(
                        "outbox_processing_timeout_seconds", 300));
                config.scheduler.exchange_rate_refresh_interval = std::chrono::minutes(
                    scheduler.value<std::int64_t>("exchange_rate_refresh_interval_minutes", 60)
                );
                config.scheduler.session_cleanup_interval = std::chrono::minutes(
                    scheduler.value<std::int64_t>(
                        "session_cleanup_interval_minutes", 1440));
                config.scheduler.session_cleanup_batch_size =
                    scheduler.value<std::uint32_t>(
                        "session_cleanup_batch_size", 1000);
                config.scheduler.outbox_retention_interval =
                    std::chrono::minutes(scheduler.value<std::int64_t>(
                        "outbox_retention_interval_minutes", 1440));
                const auto retention_days = scheduler.value<std::int64_t>(
                    "published_outbox_retention_days", 30);
                if (retention_days < 1 || retention_days > 3650) {
                    return std::unexpected(Error(
                        application::ErrorCode::ConfigurationError,
                        "Published Outbox retention is invalid"));
                }
                config.scheduler.published_outbox_retention =
                    std::chrono::hours(24 * retention_days);
                config.scheduler.outbox_retention_batch_size =
                    scheduler.value<std::uint32_t>(
                        "outbox_retention_batch_size", 1000);
                config.scheduler.job_execution_timeout = std::chrono::seconds(
                    scheduler.value<std::int64_t>(
                        "job_execution_timeout_seconds", 30));
                config.scheduler.job_lease_duration = std::chrono::seconds(
                    scheduler.value<std::int64_t>(
                        "job_lease_duration_seconds", 120));
            }

            // Exchange rate config
            if (json.contains("exchange_rate")) {
                const auto& exchange_rate = json["exchange_rate"];
                config.exchange_rate.provider = exchange_rate.value("provider", std::string("mock"));
                config.exchange_rate.api_key = exchange_rate.value("api_key", std::string(""));
                config.exchange_rate.request_timeout = std::chrono::seconds(
                    exchange_rate.value<std::int64_t>(
                        "request_timeout_seconds", 10));
            }

            // Apply environment variable overrides (higher priority than JSON).
            // A malformed override (e.g. bad port) fails loudly.
            if (auto overlay = apply_env_overrides(config); !overlay) {
                return std::unexpected(overlay.error());
            }

            // Validate required fields
            if (config.jwt.secret.empty()) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "JWT secret is required but not provided"));
            }
            // Reject unreplaced placeholder secrets so a config template cannot
            // be started in production with a dummy signing key.
            if (config.jwt.secret.starts_with("REPLACE_WITH_")) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "JWT secret still holds a placeholder value; set a real secret",
                    "secret starts with REPLACE_WITH_"));
            }
            if (config.jwt.secret.size() < 32) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "JWT secret must contain at least 32 bytes"));
            }
            if (config.jwt.issuer.empty() || config.jwt.issuer.size() > 128 ||
                config.jwt.audience.empty() || config.jwt.audience.size() > 128) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "JWT issuer and audience must contain 1 to 128 bytes"));
            }
            if (config.jwt.access_token_expiry <= std::chrono::seconds::zero() ||
                config.jwt.refresh_token_expiry <= config.jwt.access_token_expiry) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "JWT token lifetimes are invalid"));
            }
            if (config.jwt.clock_skew < std::chrono::seconds::zero() ||
                config.jwt.clock_skew > std::chrono::minutes(5)) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "JWT clock skew must be between 0 and 300 seconds"));
            }
            if (config.security.password_pepper.size() > 1024) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "Password pepper exceeds 1024 bytes"));
            }
            if (config.security.password_pepper.starts_with("REPLACE_WITH_")) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "Password pepper still holds a placeholder value; set a real pepper or leave it empty"));
            }
            const bool file_logging =
                config.logging.output == LogOutput::File ||
                config.logging.output == LogOutput::Both;
            if (file_logging &&
                (config.logging.file.empty() ||
                 config.logging.file.size() > 4096 ||
                 config.logging.maximum_file_size_bytes < 64U * 1024U ||
                 config.logging.maximum_file_size_bytes >
                     100U * 1024U * 1024U ||
                 config.logging.maximum_file_count == 0 ||
                 config.logging.maximum_file_count > 20)) {
                return std::unexpected(Error(
                    application::ErrorCode::ConfigurationError,
                    "File logging rotation configuration is invalid"));
            }
            if (config.server.threads == 0 ||
                config.server.threads > 64 ||
                config.server.request_worker_threads == 0 ||
                config.server.request_worker_threads > 64 ||
                config.server.request_queue_capacity == 0 ||
                config.server.request_queue_capacity > 2048 ||
                config.server.request_queue_byte_capacity == 0 ||
                config.server.request_queue_byte_capacity >
                    256U * 1024U * 1024U ||
                config.server.maximum_request_body_bytes == 0 ||
                config.server.maximum_request_body_bytes >
                    16U * 1024U * 1024U ||
                config.server.maximum_request_body_bytes >
                    config.server.request_queue_byte_capacity ||
                config.server.auth_worker_threads == 0 ||
                config.server.auth_worker_threads > 8 ||
                config.server.auth_queue_capacity == 0 ||
                config.server.auth_queue_capacity > 256 ||
                config.server.auth_queue_byte_capacity == 0 ||
                config.server.auth_queue_byte_capacity >
                    16U * 1024U * 1024U ||
                config.server.maximum_request_body_bytes >
                    config.server.auth_queue_byte_capacity ||
                config.server.auth_rate_limit_attempts == 0 ||
                config.server.auth_rate_limit_attempts > 10'000 ||
                config.server.auth_rate_limit_window <=
                    std::chrono::seconds::zero() ||
                config.server.auth_rate_limit_window > std::chrono::hours(24) ||
                config.server.auth_rate_limit_sources == 0 ||
                config.server.auth_rate_limit_sources > 1'000'000 ||
                config.database.pool_size == 0 ||
                config.background_database.pool_size == 0) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "Server worker and database pool configuration is invalid"));
            }
            if (config.scheduler.worker_threads == 0 ||
                config.scheduler.worker_threads > 64 ||
                config.scheduler.queue_capacity == 0 ||
                config.scheduler.queue_capacity > 10000 ||
                config.scheduler.outbox_batch_size == 0 ||
                config.scheduler.outbox_batch_size >
                    static_cast<std::uint32_t>(
                        std::numeric_limits<std::int32_t>::max()) ||
                config.scheduler.session_cleanup_batch_size == 0 ||
                config.scheduler.session_cleanup_batch_size >
                    static_cast<std::uint32_t>(
                        std::numeric_limits<std::int32_t>::max()) ||
                config.scheduler.outbox_retention_batch_size == 0 ||
                config.scheduler.outbox_retention_batch_size > 10000 ||
                config.scheduler.outbox_publish_interval <=
                    std::chrono::seconds::zero() ||
                config.scheduler.outbox_processing_timeout <=
                    config.scheduler.job_execution_timeout ||
                config.scheduler.exchange_rate_refresh_interval <=
                    std::chrono::minutes::zero() ||
                config.scheduler.session_cleanup_interval <=
                    std::chrono::minutes::zero() ||
                config.scheduler.outbox_retention_interval <=
                    std::chrono::minutes::zero() ||
                config.scheduler.outbox_retention_interval >
                    std::chrono::hours(24 * 7) ||
                config.scheduler.published_outbox_retention <
                    std::chrono::hours(24) ||
                config.scheduler.published_outbox_retention >
                    std::chrono::hours(24 * 3650) ||
                config.scheduler.job_execution_timeout <=
                    std::chrono::seconds::zero() ||
                config.scheduler.job_lease_duration <=
                    config.scheduler.job_execution_timeout) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "Scheduler configuration is invalid"));
            }
            if (config.exchange_rate.provider.empty() ||
                config.exchange_rate.provider.size() > 64 ||
                config.exchange_rate.api_key.size() > 512 ||
                config.exchange_rate.request_timeout <=
                    std::chrono::seconds::zero() ||
                config.exchange_rate.request_timeout > std::chrono::minutes(5) ||
                (config.scheduler.enabled &&
                 config.exchange_rate.request_timeout >
                     config.scheduler.job_execution_timeout / 2)) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "Exchange-rate provider configuration is invalid"));
            }

            return config;

        } catch (const std::exception& e) {
            return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                "Failed to parse config", e.what()));
        }
    }

private:
    std::filesystem::path config_path_;

    [[nodiscard]] static LogLevel parse_log_level(const std::string& level) {
        if (level == "trace") return LogLevel::Trace;
        if (level == "debug") return LogLevel::Debug;
        if (level == "info") return LogLevel::Info;
        if (level == "warn" || level == "warning") return LogLevel::Warning;
        if (level == "error") return LogLevel::Error;
        if (level == "critical") return LogLevel::Critical;
        return LogLevel::Info;
    }

    [[nodiscard]] static LogOutput parse_log_output(const std::string& output) {
        if (output == "console") return LogOutput::Console;
        if (output == "file") return LogOutput::File;
        if (output == "both") return LogOutput::Both;
        return LogOutput::Console;
    }

    /// @brief Apply environment variable overrides to configuration.
    ///
    /// Environment variables take precedence over JSON file values. This allows
    /// sensitive values (secrets, passwords) to be injected at runtime without
    /// storing them in version-controlled config files.
    ///
    /// Supported environment variables (PFH_ prefix preferred; unprefixed kept
    /// for backward compatibility with earlier tests/docs):
    /// - PFH_ENVIRONMENT / ENVIRONMENT
    /// - PFH_JWT_SECRET / JWT_SECRET
    /// - PFH_PASSWORD_PEPPER
    /// - PFH_DB_HOST / DB_HOST
    /// - PFH_DB_PORT / DB_PORT
    /// - PFH_DB_NAME / DB_NAME
    /// - PFH_DB_USER / DB_USER
    /// - PFH_DB_PASSWORD / DB_PASSWORD
    /// - PFH_BACKGROUND_DB_HOST
    /// - PFH_BACKGROUND_DB_PORT
    /// - PFH_BACKGROUND_DB_NAME
    /// - PFH_BACKGROUND_DB_USER
    /// - PFH_BACKGROUND_DB_PASSWORD
    /// - PFH_FREECURRENCYAPI_API_KEY (preferred)
    /// - PFH_EXCHANGE_RATE_API_KEY / EXCHANGE_RATE_API_KEY (legacy aliases)
    ///
    /// @param config Configuration to apply overrides to (modified in-place).
    /// @return empty on success; a ConfigurationError when a provided value is
    ///         malformed (e.g. a non-numeric or out-of-range PFH_DB_PORT). An
    ///         invalid deployment value must fail loudly, not be silently
    ///         dropped back to the JSON/default.
    [[nodiscard]] static application::VoidResult apply_env_overrides(AppConfig& config) {
        using application::Error;
        auto env_or = [](const char* preferred, const char* fallback) -> const char* {
            if (const char* v = std::getenv(preferred); v != nullptr && v[0] != '\0') {
                return v;
            }
            if (const char* v = std::getenv(fallback); v != nullptr && v[0] != '\0') {
                return v;
            }
            return nullptr;
        };

        if (const char* v = env_or("PFH_ENVIRONMENT", "ENVIRONMENT")) {
            config.environment = v;
        }
        if (const char* v = env_or("PFH_JWT_SECRET", "JWT_SECRET")) {
            config.jwt.secret = v;
        }
        if (const char* v = std::getenv("PFH_PASSWORD_PEPPER");
            v != nullptr && v[0] != '\0') {
            config.security.password_pepper = v;
        }
        if (const char* v = env_or("PFH_DB_HOST", "DB_HOST")) {
            config.database.host = v;
        }
        if (const char* v = env_or("PFH_DB_PORT", "DB_PORT")) {
            // A bad port must be a hard error: silently keeping the default
            // would send the app to the wrong database in production.
            const std::string port_str = v;
            std::size_t consumed = 0;
            long parsed = 0;
            try {
                parsed = std::stol(port_str, &consumed);
            } catch (const std::exception&) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "PFH_DB_PORT is not a valid integer", port_str));
            }
            if (consumed != port_str.size() || parsed < 1 || parsed > 65535) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "PFH_DB_PORT out of range (1-65535)", port_str));
            }
            config.database.port = static_cast<std::uint16_t>(parsed);
        }
        if (const char* v = env_or("PFH_DB_NAME", "DB_NAME")) {
            config.database.name = v;
        }
        if (const char* v = env_or("PFH_DB_USER", "DB_USER")) {
            config.database.user = v;
        }
        if (const char* v = env_or("PFH_DB_PASSWORD", "DB_PASSWORD")) {
            config.database.password = v;
        }
        if (const char* v = std::getenv("PFH_BACKGROUND_DB_HOST");
            v != nullptr && v[0] != '\0') {
            config.background_database.host = v;
        }
        if (const char* v = std::getenv("PFH_BACKGROUND_DB_PORT");
            v != nullptr && v[0] != '\0') {
            const std::string port_str = v;
            std::size_t consumed = 0;
            long parsed = 0;
            try {
                parsed = std::stol(port_str, &consumed);
            } catch (const std::exception&) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "PFH_BACKGROUND_DB_PORT is not a valid integer", port_str));
            }
            if (consumed != port_str.size() || parsed < 1 || parsed > 65535) {
                return std::unexpected(Error(application::ErrorCode::ConfigurationError,
                    "PFH_BACKGROUND_DB_PORT out of range (1-65535)", port_str));
            }
            config.background_database.port = static_cast<std::uint16_t>(parsed);
        }
        if (const char* v = std::getenv("PFH_BACKGROUND_DB_NAME");
            v != nullptr && v[0] != '\0') {
            config.background_database.name = v;
        }
        if (const char* v = std::getenv("PFH_BACKGROUND_DB_USER");
            v != nullptr && v[0] != '\0') {
            config.background_database.user = v;
        }
        if (const char* v = std::getenv("PFH_BACKGROUND_DB_PASSWORD");
            v != nullptr && v[0] != '\0') {
            config.background_database.password = v;
        }
        if (const char* v = std::getenv("PFH_FREECURRENCYAPI_API_KEY");
            v != nullptr && v[0] != '\0') {
            config.exchange_rate.api_key = v;
        } else if (const char* v = env_or(
                       "PFH_EXCHANGE_RATE_API_KEY",
                       "EXCHANGE_RATE_API_KEY")) {
            config.exchange_rate.api_key = v;
        }
        return {};
    }
};

} // namespace pfh::infrastructure
