// Personal Finance Hub - OpenExchangeRates Provider

#include "pfh/infrastructure/external/open_exchange_rates_provider.h"

#include "pfh/domain/decimal.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

namespace {

constexpr std::size_t kMaximumResponseBytes = 1024U * 1024U;
constexpr std::uint64_t kMaximumUnixTimestamp = 253402300799ULL;

enum class ObjectRole {
    Root,
    Rates,
    Other
};

struct ParsedResponse {
    std::string base;
    std::string timestamp;
    std::map<std::string, std::string> rates;
};

class RatesSax final : public nlohmann::json_sax<nlohmann::json> {
public:
    using number_integer_t = nlohmann::json::number_integer_t;
    using number_unsigned_t = nlohmann::json::number_unsigned_t;
    using number_float_t = nlohmann::json::number_float_t;
    using string_t = nlohmann::json::string_t;
    using binary_t = nlohmann::json::binary_t;

    [[nodiscard]] bool null() override { return scalar_invalid(); }

    [[nodiscard]] bool boolean(bool) override { return scalar_invalid(); }

    [[nodiscard]] bool number_integer(number_integer_t value) override {
        return number(std::to_string(value), true);
    }

    [[nodiscard]] bool number_unsigned(number_unsigned_t value) override {
        return number(std::to_string(value), true);
    }

    [[nodiscard]] bool number_float(
        number_float_t,
        const string_t& token) override {
        return number(token, false);
    }

    [[nodiscard]] bool string(string_t& value) override {
        if (frames_.empty()) {
            return fail("root must be an object");
        }
        auto& frame = frames_.back();
        if (!frame.object) {
            return true;
        }
        if (frame.role == ObjectRole::Root && frame.key == "base") {
            response_.base = value;
            base_seen_ = true;
            frame.key.clear();
            return true;
        }
        if ((frame.role == ObjectRole::Root &&
             (frame.key == "timestamp" || frame.key == "rates")) ||
            frame.role == ObjectRole::Rates) {
            return fail("required field has the wrong type");
        }
        frame.key.clear();
        return true;
    }

    [[nodiscard]] bool binary(binary_t&) override {
        return scalar_invalid();
    }

    [[nodiscard]] bool start_object(std::size_t) override {
        return start_container(true);
    }

    [[nodiscard]] bool key(string_t& value) override {
        if (frames_.empty() || !frames_.back().object) {
            return fail("object key appeared outside an object");
        }
        auto& frame = frames_.back();
        if (!frame.keys.insert(value).second) {
            return fail("response contains a duplicate object key");
        }
        frame.key = value;
        return true;
    }

    [[nodiscard]] bool end_object() override { return end_container(true); }

    [[nodiscard]] bool start_array(std::size_t) override {
        return start_container(false);
    }

    [[nodiscard]] bool end_array() override { return end_container(false); }

    [[nodiscard]] bool parse_error(
        std::size_t,
        const std::string&,
        const nlohmann::detail::exception&) override {
        return fail("response is not valid JSON");
    }

    [[nodiscard]] bool complete() const noexcept {
        return error_.empty() && root_complete_ && base_seen_ &&
               timestamp_seen_ && rates_seen_;
    }

    [[nodiscard]] const ParsedResponse& response() const noexcept {
        return response_;
    }

private:
    struct Frame {
        bool object = false;
        ObjectRole role = ObjectRole::Other;
        std::string key;
        std::set<std::string> keys;
    };

    [[nodiscard]] bool start_container(bool object) {
        if (frames_.empty()) {
            if (!object || root_started_) {
                return fail("root must be one object");
            }
            root_started_ = true;
            frames_.push_back(Frame{true, ObjectRole::Root, {}, {}});
            return true;
        }

        auto& parent = frames_.back();
        ObjectRole role = ObjectRole::Other;
        if (parent.object) {
            if (parent.role == ObjectRole::Root && parent.key == "rates") {
                if (!object) {
                    return fail("rates must be an object");
                }
                rates_seen_ = true;
                role = ObjectRole::Rates;
            } else if ((parent.role == ObjectRole::Root &&
                        (parent.key == "base" ||
                         parent.key == "timestamp")) ||
                       parent.role == ObjectRole::Rates) {
                return fail("required field has the wrong type");
            }
        }
        frames_.push_back(Frame{object, role, {}, {}});
        return true;
    }

    [[nodiscard]] bool end_container(bool object) {
        if (frames_.empty() || frames_.back().object != object) {
            return fail("response container is malformed");
        }
        const auto role = frames_.back().role;
        frames_.pop_back();
        if (role == ObjectRole::Root) {
            root_complete_ = true;
        }
        if (!frames_.empty() && frames_.back().object) {
            frames_.back().key.clear();
        }
        return true;
    }

    [[nodiscard]] bool scalar_invalid() {
        if (frames_.empty()) {
            return fail("root must be an object");
        }
        auto& frame = frames_.back();
        if (!frame.object) {
            return true;
        }
        if ((frame.role == ObjectRole::Root &&
             (frame.key == "base" || frame.key == "timestamp" ||
              frame.key == "rates")) ||
            frame.role == ObjectRole::Rates) {
            return fail("required field has the wrong type");
        }
        frame.key.clear();
        return true;
    }

    [[nodiscard]] bool number(std::string token, bool integer) {
        if (frames_.empty()) {
            return fail("root must be an object");
        }
        auto& frame = frames_.back();
        if (!frame.object) {
            return true;
        }
        if (frame.role == ObjectRole::Root && frame.key == "timestamp") {
            if (!integer) {
                return fail("timestamp must be an integer");
            }
            response_.timestamp = std::move(token);
            timestamp_seen_ = true;
            frame.key.clear();
            return true;
        }
        if (frame.role == ObjectRole::Rates) {
            if (frame.key.empty() || token.size() > 128) {
                return fail("rate token is invalid");
            }
            response_.rates.emplace(frame.key, std::move(token));
            frame.key.clear();
            return true;
        }
        if (frame.role == ObjectRole::Root &&
            (frame.key == "base" || frame.key == "rates")) {
            return fail("required field has the wrong type");
        }
        frame.key.clear();
        return true;
    }

    [[nodiscard]] bool fail(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message);
        }
        return false;
    }

    std::vector<Frame> frames_;
    ParsedResponse response_;
    std::string error_;
    bool root_started_ = false;
    bool root_complete_ = false;
    bool base_seen_ = false;
    bool timestamp_seen_ = false;
    bool rates_seen_ = false;
};

[[nodiscard]] std::string percent_encode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~';
        if (unreserved) {
            encoded.push_back(raw);
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(kHex[(c >> 4U) & 0x0fU]);
        encoded.push_back(kHex[c & 0x0fU]);
    }
    return encoded;
}

[[nodiscard]] domain::RepositoryError invalid_response() {
    return domain::RepositoryError::validation(
        "OpenExchangeRates returned an invalid response");
}

} // namespace

OpenExchangeRatesProvider::~OpenExchangeRatesProvider() {
    auto* bytes = static_cast<volatile char*>(api_key_.data());
    for (std::size_t index = 0; index < api_key_.size(); ++index) {
        bytes[index] = '\0';
    }
}

domain::RepositoryResult<std::vector<domain::ExchangeRate>>
OpenExchangeRatesProvider::fetch_latest(
    const domain::Currency& base,
    const std::vector<domain::Currency>& targets) {
    if (api_key_.empty() || api_key_.size() > 512 ||
        timeout_ <= std::chrono::milliseconds::zero() ||
        base.code() != "USD") {
        return std::unexpected(domain::RepositoryError::validation(
            "OpenExchangeRates provider configuration is invalid"));
    }

    std::map<std::string, domain::Currency> requested;
    for (const auto& target : targets) {
        if (target == base) {
            return std::unexpected(domain::RepositoryError::validation(
                "OpenExchangeRates target must differ from the base"));
        }
        requested.emplace(target.code(), target);
    }
    if (requested.empty()) {
        return std::vector<domain::ExchangeRate>{};
    }

    std::string symbols;
    for (const auto& [code, ignored] : requested) {
        (void)ignored;
        if (!symbols.empty()) {
            symbols.push_back(',');
        }
        symbols += code;
    }
    const std::string path = "/api/latest.json?app_id=" +
                             percent_encode(api_key_) + "&symbols=" +
                             percent_encode(symbols);
    auto response = transport_.get(path, timeout_);
    if (!response) {
        return std::unexpected(domain::RepositoryError::database(
            response.error().kind == HttpTransportErrorKind::Timeout
                ? "OpenExchangeRates request timed out"
                : "OpenExchangeRates request failed"));
    }
    if (response->status_code != 200) {
        return std::unexpected(domain::RepositoryError::database(
            "OpenExchangeRates returned a non-success status"));
    }
    if (response->body.empty() ||
        response->body.size() > kMaximumResponseBytes) {
        return std::unexpected(invalid_response());
    }

    RatesSax sax;
    const bool parsed = nlohmann::json::sax_parse(response->body, &sax);
    if (!parsed || !sax.complete()) {
        return std::unexpected(invalid_response());
    }
    const auto& payload = sax.response();
    if (payload.base != base.code() || payload.rates.size() != requested.size()) {
        return std::unexpected(invalid_response());
    }

    std::uint64_t timestamp = 0;
    const auto* begin = payload.timestamp.data();
    const auto* end = begin + payload.timestamp.size();
    const auto converted = std::from_chars(begin, end, timestamp);
    if (converted.ec != std::errc{} || converted.ptr != end ||
        timestamp == 0 || timestamp > kMaximumUnixTimestamp ||
        timestamp > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
        return std::unexpected(invalid_response());
    }
    const auto fetched_at = std::chrono::system_clock::time_point{
        std::chrono::seconds(static_cast<std::int64_t>(timestamp))};
    if (fetched_at > clock_.now() + std::chrono::minutes(5)) {
        return std::unexpected(invalid_response());
    }

    std::vector<domain::ExchangeRate> result;
    result.reserve(requested.size());
    for (const auto& [code, target] : requested) {
        const auto found = payload.rates.find(code);
        if (found == payload.rates.end()) {
            return std::unexpected(invalid_response());
        }
        auto decimal = domain::Decimal::parse_numeric_20_10(found->second);
        if (!decimal || !decimal->is_positive()) {
            return std::unexpected(invalid_response());
        }
        auto rate = domain::ExchangeRate::create(
            base,
            target,
            *decimal,
            fetched_at,
            std::string(provider_name()));
        if (!rate) {
            return std::unexpected(invalid_response());
        }
        result.push_back(std::move(*rate));
    }
    return result;
}

} // namespace pfh::infrastructure
