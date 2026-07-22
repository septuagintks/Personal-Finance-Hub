// Personal Finance Hub - Configuration Management
// Version: 1.0
// C++23
// This file defines configuration structure and loading interface

#pragma once

#include "pfh/application/error.h"
#include <chrono>
#include <cstdint>
#include <string>

namespace pfh::infrastructure {

/// @brief Server configuration
struct ServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 8080;
    std::uint32_t threads = 4;
    std::uint32_t request_worker_threads = 8;
    std::uint32_t request_queue_capacity = 256;
    std::uint64_t request_queue_byte_capacity = 16U * 1024U * 1024U;
    std::uint64_t maximum_request_body_bytes = 1024U * 1024U;
    std::uint32_t auth_worker_threads = 2;
    std::uint32_t auth_queue_capacity = 16;
    std::uint64_t auth_queue_byte_capacity = 1024U * 1024U;
    std::uint32_t auth_rate_limit_attempts = 20;
    std::chrono::seconds auth_rate_limit_window{60};
    std::uint32_t auth_rate_limit_sources = 10'000;
};

/// @brief Database configuration
struct DatabaseConfig {
    std::string host = "localhost";
    std::uint16_t port = 5432;
    std::string name = "pfh_dev";
    std::string user = "pfh_user";
    std::string password;
    std::uint32_t pool_size = 10;
    std::chrono::seconds connection_timeout{30};
};

/// @brief JWT configuration
struct JwtConfig {
    std::string secret;
    std::string issuer = "pfh-api";
    std::string audience = "pfh-client";
    std::chrono::seconds access_token_expiry{900};   // 15 minutes
    std::chrono::seconds refresh_token_expiry{2592000}; // 30 days
    std::chrono::seconds clock_skew{30};
};

struct SecurityConfig {
    // Optional Argon2id pepper. Loaded from PFH_PASSWORD_PEPPER only; never
    // parsed from a checked-in JSON file.
    std::string password_pepper;
};

/// @brief Logging configuration
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

enum class LogOutput {
    Console,
    File,
    Both
};

struct LoggingConfig {
    LogLevel level = LogLevel::Info;
    LogOutput output = LogOutput::Console;
    std::string file = "logs/pfh.log";
    std::uint64_t maximum_file_size_bytes = 10U * 1024U * 1024U;
    // Total files on disk, including the active file.
    std::uint32_t maximum_file_count = 5;
};

/// @brief Scheduler configuration
struct SchedulerConfig {
    bool enabled = true;
    std::uint32_t worker_threads = 2;
    std::uint32_t queue_capacity = 64;
    std::chrono::seconds outbox_publish_interval{5};
    std::uint32_t outbox_batch_size = 100;
    std::chrono::seconds outbox_processing_timeout{300};
    std::chrono::minutes exchange_rate_refresh_interval{60};
    std::chrono::minutes session_cleanup_interval{1440};
    std::uint32_t session_cleanup_batch_size = 1000;
    std::chrono::minutes outbox_retention_interval{1440};
    std::chrono::hours published_outbox_retention{24 * 30};
    std::uint32_t outbox_retention_batch_size = 1000;
    std::chrono::seconds job_execution_timeout{30};
    std::chrono::seconds job_lease_duration{120};
};

/// @brief FreeCurrencyAPI primary + exchangerate.fun fallback configuration.
struct ExchangeRateConfig {
    std::string provider = "mock";
    std::string api_key;
    std::chrono::seconds request_timeout{10};
};

/// @brief Application configuration root
struct AppConfig {
    std::string environment = "development";
    ServerConfig server;
    DatabaseConfig database;
    DatabaseConfig background_database = [] {
        DatabaseConfig value;
        value.user = "pfh_background";
        value.pool_size = 2;
        return value;
    }();
    JwtConfig jwt;
    SecurityConfig security;
    LoggingConfig logging;
    SchedulerConfig scheduler;
    ExchangeRateConfig exchange_rate;
};

/// @brief Configuration loader interface
class IConfigLoader {
public:
    virtual ~IConfigLoader() = default;

    /// @brief Load configuration from file or environment.
    /// @return AppConfig on success; application::Error (ConfigurationError) on failure,
    ///         so the presentation layer can map it uniformly.
    [[nodiscard]] virtual application::Result<AppConfig> load() = 0;
};

} // namespace pfh::infrastructure
