// Personal Finance Hub - Logging Infrastructure
// Version: 1.0
// C++23
// This file provides logging utilities with structured context

#pragma once

#include "pfh/infrastructure/config.h"
#include "pfh/domain/typed_id.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <string>

namespace pfh::infrastructure {

/// @brief Logger wrapper with context support
class Logger {
public:
    /// @brief Initialize global logger with configuration
    static void initialize(const LoggingConfig& config) {
        std::vector<spdlog::sink_ptr> sinks;

        // Console sink
        if (config.output == LogOutput::Console || config.output == LogOutput::Both) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
            sinks.push_back(console_sink);
        }

        // File sink
        if (config.output == LogOutput::File || config.output == LogOutput::Both) {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.file, true);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
            sinks.push_back(file_sink);
        }

        auto logger = std::make_shared<spdlog::logger>("pfh", sinks.begin(), sinks.end());
        logger->set_level(to_spdlog_level(config.level));
        logger->flush_on(spdlog::level::err);

        spdlog::set_default_logger(logger);
        spdlog::info("Logger initialized - level: {}, output: {}",
                     to_string(config.level), to_string(config.output));
    }

    // Context-aware logging.
    //
    // The user message is formatted first with fmt::format(fmt::runtime(...)),
    // then the fully-rendered line (context prefix + message) is emitted to
    // spdlog as a single literal argument. Using fmt::runtime avoids the
    // consteval format-string check firing on the already-substituted string,
    // and emitting one literal keeps the placeholder count correct regardless
    // of how many args the caller passed.

    template <typename... Args>
    static void trace(const std::string& trace_id, fmt::format_string<Args...> fmt_str, Args&&... args) {
        spdlog::trace("{}", with_trace(trace_id, fmt::format(fmt_str, std::forward<Args>(args)...)));
    }

    template <typename... Args>
    static void debug(const std::string& trace_id, fmt::format_string<Args...> fmt_str, Args&&... args) {
        spdlog::debug("{}", with_trace(trace_id, fmt::format(fmt_str, std::forward<Args>(args)...)));
    }

    template <typename... Args>
    static void info(const std::string& trace_id, fmt::format_string<Args...> fmt_str, Args&&... args) {
        spdlog::info("{}", with_trace(trace_id, fmt::format(fmt_str, std::forward<Args>(args)...)));
    }

    template <typename... Args>
    static void warn(const std::string& trace_id, fmt::format_string<Args...> fmt_str, Args&&... args) {
        spdlog::warn("{}", with_trace(trace_id, fmt::format(fmt_str, std::forward<Args>(args)...)));
    }

    template <typename... Args>
    static void error(const std::string& trace_id, fmt::format_string<Args...> fmt_str, Args&&... args) {
        spdlog::error("{}", with_trace(trace_id, fmt::format(fmt_str, std::forward<Args>(args)...)));
    }

    template <typename... Args>
    static void critical(const std::string& trace_id, fmt::format_string<Args...> fmt_str, Args&&... args) {
        spdlog::critical("{}", with_trace(trace_id, fmt::format(fmt_str, std::forward<Args>(args)...)));
    }

    /// @brief Log with user context (info level).
    template <typename... Args>
    static void info_user(const std::string& trace_id, domain::UserId user_id,
                          fmt::format_string<Args...> fmt_str, Args&&... args) {
        spdlog::info("[TraceId:{}] [UserId:{}] {}", trace_id, user_id.value(),
                     fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    /// @brief Log with user context (error level).
    template <typename... Args>
    static void error_user(const std::string& trace_id, domain::UserId user_id,
                           fmt::format_string<Args...> fmt_str, Args&&... args) {
        spdlog::error("[TraceId:{}] [UserId:{}] {}", trace_id, user_id.value(),
                      fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    /// @brief Log with an error-context label (error level).
    template <typename... Args>
    static void error_with_context(const std::string& trace_id, const std::string& context,
                                   fmt::format_string<Args...> fmt_str, Args&&... args) {
        spdlog::error("[TraceId:{}] [Context:{}] {}", trace_id, context,
                      fmt::format(fmt_str, std::forward<Args>(args)...));
    }

private:
    /// @brief Compose the "[TraceId:...] message" line as a single literal string.
    [[nodiscard]] static std::string with_trace(const std::string& trace_id,
                                                 const std::string& message) {
        return "[TraceId:" + trace_id + "] " + message;
    }

    [[nodiscard]] static spdlog::level::level_enum to_spdlog_level(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return spdlog::level::trace;
            case LogLevel::Debug: return spdlog::level::debug;
            case LogLevel::Info: return spdlog::level::info;
            case LogLevel::Warning: return spdlog::level::warn;
            case LogLevel::Error: return spdlog::level::err;
            case LogLevel::Critical: return spdlog::level::critical;
            default: return spdlog::level::info;
        }
    }

    [[nodiscard]] static std::string to_string(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "trace";
            case LogLevel::Debug: return "debug";
            case LogLevel::Info: return "info";
            case LogLevel::Warning: return "warning";
            case LogLevel::Error: return "error";
            case LogLevel::Critical: return "critical";
            default: return "unknown";
        }
    }

    [[nodiscard]] static std::string to_string(LogOutput output) {
        switch (output) {
            case LogOutput::Console: return "console";
            case LogOutput::File: return "file";
            case LogOutput::Both: return "both";
            default: return "unknown";
        }
    }
};

} // namespace pfh::infrastructure
