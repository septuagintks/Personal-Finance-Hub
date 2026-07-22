// Personal Finance Hub - Report Resource Capacity and Rejection Metrics

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pfh::presentation {

struct ReportResourceCapacity {
    std::size_t aggregate_rows = 0;
    std::size_t detailed_rows = 0;
    std::size_t input_bytes = 0;
    std::size_t csv_output_bytes = 0;
    std::size_t breakdown_buckets = 0;
    std::size_t breakdown_expansions = 0;
    std::size_t historical_rate_points = 0;
    std::size_t cash_flow_months = 0;
    std::size_t csv_range_days = 0;
};

struct ReportResourceSnapshot {
    ReportResourceCapacity capacity;
    std::uint64_t report_query_rejections = 0;
    std::uint64_t csv_export_rejections = 0;
};

class ReportResourceMetrics {
public:
    explicit ReportResourceMetrics(ReportResourceCapacity capacity)
        : capacity_(capacity) {}

    void record_report_query_rejection() noexcept {
        report_query_rejections_.fetch_add(1, std::memory_order_relaxed);
    }

    void record_csv_export_rejection() noexcept {
        csv_export_rejections_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] ReportResourceSnapshot snapshot() const noexcept {
        return ReportResourceSnapshot{
            capacity_,
            report_query_rejections_.load(std::memory_order_relaxed),
            csv_export_rejections_.load(std::memory_order_relaxed)};
    }

private:
    const ReportResourceCapacity capacity_;
    std::atomic<std::uint64_t> report_query_rejections_{0};
    std::atomic<std::uint64_t> csv_export_rejections_{0};
};

} // namespace pfh::presentation
