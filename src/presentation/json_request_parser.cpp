// Personal Finance Hub - Defensive JSON Request Parser

#include "pfh/presentation/http/json_request_parser.h"

#include <string>

namespace pfh::presentation {

application::Result<JsonRequestParser::Json> JsonRequestParser::parse_object(
    const HttpRequest& request) {
    try {
        auto parsed = Json::parse(request.body);
        if (!parsed.is_object()) {
            return application::err(application::Error::validation(
                "Request body must be a JSON object"));
        }
        return parsed;
    } catch (const Json::parse_error&) {
        return application::err(application::Error::validation(
            "Request body contains invalid JSON"));
    }
}

application::VoidResult JsonRequestParser::reject_unknown_fields(
    const Json& object,
    const std::set<std::string>& allowed_fields) {
    for (const auto& [key, _] : object.items()) {
        if (!allowed_fields.contains(key)) {
            return application::err(application::Error::validation(
                "Unknown request field: " + key));
        }
    }
    return application::ok();
}

application::Result<std::string> JsonRequestParser::required_string(
    const Json& object,
    std::string_view field,
    std::size_t max_length) {
    const auto key = std::string(field);
    const auto it = object.find(key);
    if (it == object.end()) {
        return application::err(application::Error(
            application::ErrorCode::MissingRequiredField,
            key + " is required"));
    }
    if (!it->is_string()) {
        return application::err(application::Error(
            application::ErrorCode::InvalidFormat,
            key + " must be a string"));
    }
    auto value = it->get<std::string>();
    if (value.empty()) {
        return application::err(application::Error::validation(
            key + " must not be empty"));
    }
    if (value.size() > max_length) {
        return application::err(application::Error::validation(
            key + " exceeds the maximum length"));
    }
    return value;
}

application::Result<std::string>
JsonRequestParser::required_string_allow_empty(
    const Json& object,
    std::string_view field,
    std::size_t max_length) {
    const auto key = std::string(field);
    const auto it = object.find(key);
    if (it == object.end()) {
        return application::err(application::Error(
            application::ErrorCode::MissingRequiredField,
            key + " is required"));
    }
    if (!it->is_string()) {
        return application::err(application::Error(
            application::ErrorCode::InvalidFormat,
            key + " must be a string"));
    }
    auto value = it->get<std::string>();
    if (value.size() > max_length) {
        return application::err(application::Error::validation(
            key + " exceeds the maximum length"));
    }
    return value;
}

application::Result<std::optional<std::string>>
JsonRequestParser::optional_string(
    const Json& object,
    std::string_view field,
    std::size_t max_length) {
    const auto key = std::string(field);
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::optional<std::string>{};
    }
    auto value = required_string(object, field, max_length);
    if (!value) {
        return application::err(value.error());
    }
    return std::optional<std::string>(*value);
}

application::Result<std::optional<std::string>>
JsonRequestParser::optional_string_allow_empty(
    const Json& object,
    std::string_view field,
    std::size_t max_length) {
    const auto key = std::string(field);
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::optional<std::string>{};
    }
    auto value = required_string_allow_empty(object, field, max_length);
    if (!value) return application::err(value.error());
    return std::optional<std::string>(std::move(*value));
}

application::Result<std::int64_t> JsonRequestParser::positive_integer(
    const Json& value,
    std::string_view field) {
    const auto name = std::string(field);
    if (value.is_number_unsigned()) {
        const auto raw = value.get<std::uint64_t>();
        if (raw == 0 ||
            raw > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            return application::err(application::Error(
                application::ErrorCode::InvalidFormat,
                name + " must be a positive 64-bit integer"));
        }
        return static_cast<std::int64_t>(raw);
    }
    if (!value.is_number_integer()) {
        return application::err(application::Error(
            application::ErrorCode::InvalidFormat,
            name + " must be a positive integer"));
    }
    const auto raw = value.get<std::int64_t>();
    if (raw <= 0) {
        return application::err(application::Error(
            application::ErrorCode::InvalidFormat,
            name + " must be a positive integer"));
    }
    return raw;
}

application::Result<std::optional<std::chrono::system_clock::time_point>>
JsonRequestParser::optional_rfc3339(
    const Json& object,
    std::string_view field) {
    auto text = optional_string(object, field, 64);
    if (!text) {
        return application::err(text.error());
    }
    if (!text->has_value()) {
        return std::optional<std::chrono::system_clock::time_point>{};
    }
    auto parsed = TimeCodec::parse_rfc3339(**text);
    if (!parsed) {
        return application::err(parsed.error());
    }
    return std::optional<std::chrono::system_clock::time_point>(*parsed);
}

} // namespace pfh::presentation
