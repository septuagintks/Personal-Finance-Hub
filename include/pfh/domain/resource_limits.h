// Personal Finance Hub - Per-user resource limits

#pragma once

#include <cstddef>

namespace pfh::domain {

// Deleted or archived rows still count. This prevents delete/recreate cycles
// from turning bounded user metadata into unbounded retained history.
inline constexpr std::size_t kMaximumAccountsPerUser = 256;
inline constexpr std::size_t kMaximumCategoriesPerUser = 1024;
inline constexpr std::size_t kMaximumTagsPerUser = 512;

} // namespace pfh::domain
