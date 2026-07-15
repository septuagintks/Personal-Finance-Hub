// Personal Finance Hub - Cursor Pagination Contract

#pragma once

#include "pfh/application/error.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pfh::application {

inline constexpr std::size_t kDefaultPageSize = 50;
inline constexpr std::size_t kMaximumPageSize = 200;

struct CursorPageRequest {
    std::optional<std::string> cursor;
    std::size_t page_size = kDefaultPageSize;

    [[nodiscard]] VoidResult validate() const {
        if (page_size == 0 || page_size > kMaximumPageSize) {
            return err(Error::validation("pageSize must be between 1 and 200"));
        }
        if (cursor.has_value() && (cursor->empty() || cursor->size() > 512U)) {
            return err(Error::validation("cursor is invalid"));
        }
        return ok();
    }
};

template <typename T>
struct CursorPage {
    std::vector<T> items;
    std::optional<std::string> next_cursor;
};

} // namespace pfh::application
