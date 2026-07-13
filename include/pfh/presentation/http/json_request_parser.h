// Personal Finance Hub - Defensive JSON Request Parser

#pragma once

#include "pfh/application/error.h"
#include "pfh/domain/typed_id.h"
#include "pfh/presentation/http/http_types.h"
#include "pfh/presentation/http/time_codec.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace pfh::presentation {

class JsonRequestParser {
public:
    using Json = nlohmann::json;

    [[nodiscard]] static application::Result<Json> parse_object(
        const HttpRequest& request);

    [[nodiscard]] static application::VoidResult reject_unknown_fields(
        const Json& object,
        const std::set<std::string>& allowed_fields);

    [[nodiscard]] static application::Result<std::string> required_string(
        const Json& object,
        std::string_view field,
        std::size_t max_length = 4096);

    [[nodiscard]] static application::Result<std::optional<std::string>>
    optional_string(
        const Json& object,
        std::string_view field,
        std::size_t max_length = 4096);

    template <typename TypedId>
    [[nodiscard]] static application::Result<TypedId> required_id(
        const Json& object,
        std::string_view field) {
        const auto key = std::string(field);
        const auto it = object.find(key);
        if (it == object.end()) {
            return application::err(application::Error(
                application::ErrorCode::MissingRequiredField,
                key + " is required"));
        }
        if (!it->is_number_integer()) {
            return application::err(application::Error(
                application::ErrorCode::InvalidFormat,
                key + " must be a positive integer"));
        }
        const auto value = it->get<std::int64_t>();
        TypedId id(value);
        if (!id.is_valid()) {
            return application::err(application::Error(
                application::ErrorCode::InvalidFormat,
                key + " must be a positive integer"));
        }
        return id;
    }

    template <typename TypedId>
    [[nodiscard]] static application::Result<std::optional<TypedId>> optional_id(
        const Json& object,
        std::string_view field) {
        const auto key = std::string(field);
        const auto it = object.find(key);
        if (it == object.end() || it->is_null()) {
            return std::optional<TypedId>{};
        }
        auto id = required_id<TypedId>(object, field);
        if (!id) {
            return application::err(id.error());
        }
        return std::optional<TypedId>(*id);
    }

    [[nodiscard]] static application::Result<std::optional<
        std::chrono::system_clock::time_point>> optional_rfc3339(
        const Json& object,
        std::string_view field);
};

} // namespace pfh::presentation
