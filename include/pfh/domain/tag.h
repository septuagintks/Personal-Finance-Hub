// Personal Finance Hub - Transaction Tag Entity

#pragma once

#include "pfh/domain/typed_id.h"
#include "pfh/domain/user.h"

#include <chrono>
#include <optional>
#include <string>

namespace pfh::domain {

class Tag {
public:
    using TimePoint = std::chrono::system_clock::time_point;

    Tag(
        TagId id,
        UserId owner,
        std::string name,
        std::optional<TimePoint> deleted_at = std::nullopt,
        TimePoint created_at = std::chrono::system_clock::now(),
        TimePoint updated_at = std::chrono::system_clock::now())
        : id_(id),
          owner_(owner),
          name_(std::move(name)),
          deleted_at_(deleted_at),
          created_at_(created_at),
          updated_at_(updated_at) {}

    [[nodiscard]] TagId id() const noexcept { return id_; }
    [[nodiscard]] UserId owner() const noexcept { return owner_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::optional<TimePoint>& deleted_at() const noexcept {
        return deleted_at_;
    }
    [[nodiscard]] bool is_deleted() const noexcept { return deleted_at_.has_value(); }
    [[nodiscard]] TimePoint created_at() const noexcept { return created_at_; }
    [[nodiscard]] TimePoint updated_at() const noexcept { return updated_at_; }

private:
    TagId id_;
    UserId owner_;
    std::string name_;
    std::optional<TimePoint> deleted_at_;
    TimePoint created_at_;
    TimePoint updated_at_;
};

} // namespace pfh::domain
