// Personal Finance Hub - RFC 3339 Time Codec

#pragma once

#include "pfh/application/error.h"

#include <chrono>
#include <string>
#include <string_view>

namespace pfh::presentation {

class TimeCodec {
public:
    [[nodiscard]] static application::Result<std::chrono::system_clock::time_point>
    parse_rfc3339(std::string_view value);

    [[nodiscard]] static std::string format_rfc3339(
        std::chrono::system_clock::time_point value);
};

} // namespace pfh::presentation
