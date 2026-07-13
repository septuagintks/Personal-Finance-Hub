// Compile-only subset of trantor::Date used by PostgreSQL adapter sources.
#pragma once

#include <cstdint>

namespace trantor {

class Date {
public:
    Date() = default;
    explicit Date(std::int64_t microseconds) : microseconds_(microseconds) {}

    [[nodiscard]] std::int64_t microSecondsSinceEpoch() const noexcept {
        return microseconds_;
    }

private:
    std::int64_t microseconds_ = 0;
};

}  // namespace trantor
