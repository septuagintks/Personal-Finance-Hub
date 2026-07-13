// Personal Finance Hub - Clock Port

#pragma once

#include <chrono>

namespace pfh::application {

class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual std::chrono::system_clock::time_point now() const = 0;
};

} // namespace pfh::application
