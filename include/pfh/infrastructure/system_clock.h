// Personal Finance Hub - System Clock Adapter

#pragma once

#include "pfh/application/ports/i_clock.h"

namespace pfh::infrastructure {

class SystemClock final : public application::IClock {
public:
    [[nodiscard]] std::chrono::system_clock::time_point now() const override {
        return std::chrono::system_clock::now();
    }
};

} // namespace pfh::infrastructure
