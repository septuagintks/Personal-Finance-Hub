// Personal Finance Hub - PostgreSQL Monthly Cash-flow Projection

#include "pfh/infrastructure/persistence/postgres_cash_flow_projection.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/application/input_constraints.h"
#include "pfh/infrastructure/persistence/postgres_repository_support.h"
#include "pfh/infrastructure/persistence/postgres_result_set.h"

#include <cstdint>
#include <set>

namespace pfh::infrastructure {

namespace {

// Any 120 consecutive calendar months span fewer than 3660 days, including
// leap years and timezone offset changes. Reject wider internal queries before
// scanning tenant rows even if a caller bypasses ReportQueryService.
constexpr auto kMaximumProjectionSpan = std::chrono::days{3660};

} // namespace

domain::RepositoryResult<
    std::vector<application::CashFlowMonthlyProjection>>
PostgresCashFlowProjection::aggregate_monthly(
    const application::CashFlowProjectionQuery& query) {
    if (!db_ || query.user_id != tenant_user_id_ ||
        query.to <= query.from ||
        query.to - query.from > kMaximumProjectionSpan ||
        query.timezone.empty() ||
        query.timezone.size() > 64U) {
        return std::unexpected(domain::RepositoryError::validation(
            "Cash-flow projection query is invalid"));
    }

    return postgres::execute_tenant_read<
        std::vector<application::CashFlowMonthlyProjection>>(
        db_, tenant_user_id_, "aggregate monthly cash flow",
        [&](const auto& transaction) -> domain::RepositoryResult<
            std::vector<application::CashFlowMonthlyProjection>> {
            // Source admission is evaluated before any rate lookup. Each
            // LATERAL lookup then uses idx_exchange_rates_pair_time and chooses
            // the newest rate at-or-before the business instant. The rounded
            // CTEs reproduce Decimal's scale-10 Half-Even operation boundaries:
            // triangulation rounds the cross-rate first, then every conversion
            // rounds the resulting amount before aggregation.
            constexpr const char* kSql = R"SQL(
                WITH source AS MATERIALIZED (
                    SELECT entry.type,
                           entry.amount,
                           entry.currency_code,
                           entry.transaction_time,
                           pg_column_size(entry)::bigint AS source_bytes,
                           to_char(
                               date_trunc(
                                   'month',
                                   timezone($4, entry.transaction_time)),
                               'YYYY-MM') AS period
                    FROM transactions AS entry
                    WHERE entry.user_id = $1
                      AND entry.deleted_at IS NULL
                      AND entry.transaction_time >= $2
                      AND entry.transaction_time < $3
                    LIMIT $6
                ), source_meta AS MATERIALIZED (
                    SELECT COUNT(*)::bigint AS source_row_count,
                           COALESCE(SUM(source_bytes), 0)::bigint
                               AS source_byte_count
                    FROM source
                ), bounded_source AS MATERIALIZED (
                    SELECT source.*
                    FROM source
                    CROSS JOIN source_meta
                    WHERE source_meta.source_row_count <= $7
                      AND source_meta.source_byte_count <= $8
                      AND source.type <> 'transfer'::transaction_type
                ), rates AS (
                    SELECT bounded_source.*,
                           direct_rate.rate AS direct_rate_value,
                           reverse_rate.rate AS reverse_rate_value,
                           usd_source.rate AS usd_source_rate_value,
                           usd_target.rate AS usd_target_rate_value
                    FROM bounded_source
                    LEFT JOIN LATERAL (
                        SELECT rate
                        FROM exchange_rates
                        WHERE base_currency_code = bounded_source.currency_code
                          AND target_currency_code = $5
                          AND fetched_at <= bounded_source.transaction_time
                        ORDER BY fetched_at DESC, id DESC
                        LIMIT 1
                    ) AS direct_rate ON
                        bounded_source.amount <> 0 AND
                        bounded_source.currency_code <> $5
                    LEFT JOIN LATERAL (
                        SELECT rate
                        FROM exchange_rates
                        WHERE base_currency_code = $5
                          AND target_currency_code = bounded_source.currency_code
                          AND fetched_at <= bounded_source.transaction_time
                        ORDER BY fetched_at DESC, id DESC
                        LIMIT 1
                    ) AS reverse_rate ON
                        bounded_source.amount <> 0 AND
                        bounded_source.currency_code <> $5 AND
                        direct_rate.rate IS NULL
                    LEFT JOIN LATERAL (
                        SELECT rate
                        FROM exchange_rates
                        WHERE base_currency_code = 'USD'
                          AND target_currency_code = bounded_source.currency_code
                          AND fetched_at <= bounded_source.transaction_time
                        ORDER BY fetched_at DESC, id DESC
                        LIMIT 1
                    ) AS usd_source ON
                        bounded_source.amount <> 0 AND
                        direct_rate.rate IS NULL AND
                        reverse_rate.rate IS NULL AND
                        bounded_source.currency_code <> 'USD' AND $5 <> 'USD'
                    LEFT JOIN LATERAL (
                        SELECT rate
                        FROM exchange_rates
                        WHERE base_currency_code = 'USD'
                          AND target_currency_code = $5
                          AND fetched_at <= bounded_source.transaction_time
                        ORDER BY fetched_at DESC, id DESC
                        LIMIT 1
                    ) AS usd_target ON
                        bounded_source.amount <> 0 AND
                        direct_rate.rate IS NULL AND
                        reverse_rate.rate IS NULL AND
                        bounded_source.currency_code <> 'USD' AND $5 <> 'USD'
                ), cross_scaled AS (
                    SELECT rates.*,
                           CASE
                               WHEN usd_source_rate_value IS NOT NULL AND
                                    usd_target_rate_value IS NOT NULL
                                   THEN (usd_target_rate_value /
                                         usd_source_rate_value) *
                                        10000000000::numeric
                               ELSE NULL
                           END AS cross_scaled_value
                    FROM rates
                ), cross_rounded AS (
                    SELECT cross_scaled.*,
                           CASE
                               WHEN cross_scaled_value IS NULL THEN NULL
                               WHEN abs(cross_scaled_value -
                                        trunc(cross_scaled_value)) < 0.5
                                   THEN trunc(cross_scaled_value)
                               WHEN abs(cross_scaled_value -
                                        trunc(cross_scaled_value)) > 0.5
                                   THEN trunc(cross_scaled_value) +
                                        sign(cross_scaled_value)
                               WHEN mod(abs(trunc(cross_scaled_value)), 2) = 0
                                   THEN trunc(cross_scaled_value)
                               ELSE trunc(cross_scaled_value) +
                                    sign(cross_scaled_value)
                           END / 10000000000::numeric AS rounded_cross_rate
                    FROM cross_scaled
                ), valid_cross AS (
                    SELECT cross_rounded.*,
                           CASE
                               WHEN rounded_cross_rate > 0 AND
                                    rounded_cross_rate <=
                                        9999999999.9999999999::numeric
                                   THEN rounded_cross_rate
                               ELSE NULL
                           END AS cross_rate
                    FROM cross_rounded
                ), unrounded AS (
                    SELECT valid_cross.*,
                           CASE
                               WHEN amount = 0 OR currency_code = $5
                                   THEN abs(amount)
                               WHEN direct_rate_value IS NOT NULL
                                   THEN abs(amount) * direct_rate_value
                               WHEN reverse_rate_value IS NOT NULL
                                   THEN abs(amount) / reverse_rate_value
                               WHEN cross_rate IS NOT NULL
                                   THEN abs(amount) * cross_rate
                               ELSE NULL
                           END AS raw_value
                    FROM valid_cross
                ), scaled AS (
                    SELECT unrounded.*,
                           raw_value * 10000000000::numeric AS raw_scaled
                    FROM unrounded
                ), rounded AS (
                    SELECT scaled.*,
                           CASE
                               WHEN raw_scaled IS NULL THEN NULL
                               WHEN abs(raw_scaled - trunc(raw_scaled)) < 0.5
                                   THEN trunc(raw_scaled)
                               WHEN abs(raw_scaled - trunc(raw_scaled)) > 0.5
                                   THEN trunc(raw_scaled) + sign(raw_scaled)
                               WHEN mod(abs(trunc(raw_scaled)), 2) = 0
                                   THEN trunc(raw_scaled)
                               ELSE trunc(raw_scaled) + sign(raw_scaled)
                           END / 10000000000::numeric AS converted
                    FROM scaled
                ), monthly AS (
                    SELECT period,
                           COALESCE(SUM(converted) FILTER (
                               WHERE type = 'income'::transaction_type OR
                                     (type = 'adjustment'::transaction_type AND
                                      amount >= 0)), 0) AS income,
                           COALESCE(SUM(converted) FILTER (
                               WHERE type = 'expense'::transaction_type OR
                                     (type = 'adjustment'::transaction_type AND
                                      amount < 0)), 0) AS expense,
                           COALESCE(BOOL_OR(
                               converted IS NULL AND amount <> 0), FALSE)
                               AS missing_rate
                    FROM rounded
                    GROUP BY period
                )
                SELECT monthly.period,
                       COALESCE(monthly.income, 0)::text AS income,
                       COALESCE(monthly.expense, 0)::text AS expense,
                       COALESCE(monthly.missing_rate, FALSE) AS missing_rate,
                       source_meta.source_row_count,
                       source_meta.source_byte_count
                FROM source_meta
                LEFT JOIN monthly ON TRUE
                ORDER BY monthly.period
            )SQL";
            const auto rows = transaction->execSqlSync(
                kSql,
                query.user_id.value(),
                pg::toDbTimestamp(query.from),
                pg::toDbTimestamp(query.to),
                query.timezone,
                query.base_currency.code(),
                static_cast<std::int64_t>(
                    application::kMaximumAggregateReportRows + 1U),
                static_cast<std::int64_t>(
                    application::kMaximumAggregateReportRows),
                static_cast<std::int64_t>(
                    application::kMaximumReportInputBytes));
            if (rows.empty()) {
                return std::unexpected(domain::RepositoryError::database(
                    "Cash-flow projection returned no admission metadata"));
            }

            const auto source_rows = pg::getBigInt(rows[0], 4);
            const auto source_bytes = pg::getBigInt(rows[0], 5);
            if (source_rows < 0 || source_bytes < 0) {
                return std::unexpected(domain::RepositoryError::database(
                    "Cash-flow projection returned invalid admission metadata"));
            }
            if (source_rows > static_cast<std::int64_t>(
                                  application::kMaximumAggregateReportRows)) {
                return std::unexpected(domain::RepositoryError::resource_limit(
                    "Cash-flow projection row limit exceeded; narrow the requested range"));
            }
            if (source_bytes > static_cast<std::int64_t>(
                                   application::kMaximumReportInputBytes)) {
                return std::unexpected(domain::RepositoryError::resource_limit(
                    "Cash-flow projection input byte limit exceeded; narrow the requested range"));
            }
            if (rows.size() > 120U) {
                return std::unexpected(domain::RepositoryError::database(
                    "Cash-flow projection exceeded 120 month buckets"));
            }

            std::vector<application::CashFlowMonthlyProjection> result;
            result.reserve(rows.size());
            std::set<std::string> periods;
            for (const auto& row : rows) {
                if (pg::getBigInt(row, 4) != source_rows ||
                    pg::getBigInt(row, 5) != source_bytes) {
                    return std::unexpected(domain::RepositoryError::database(
                        "Cash-flow projection admission metadata is inconsistent"));
                }
                const auto period = pg::getOptionalString(row, 0);
                if (!period.has_value()) {
                    // Empty and transfer-only ranges both have no observed
                    // cash-flow month, but still carry admission metadata.
                    if (rows.size() == 1U) {
                        continue;
                    }
                    return std::unexpected(domain::RepositoryError::database(
                        "Cash-flow projection returned a missing month bucket"));
                }
                const auto income = domain::Decimal::parse(
                    pg::getNumericAsString(row, 1));
                const auto expense = domain::Decimal::parse(
                    pg::getNumericAsString(row, 2));
                if (period->size() != 7U || (*period)[4] != '-' ||
                    !periods.insert(*period).second || !income || !expense) {
                    return std::unexpected(domain::RepositoryError::database(
                        "Cash-flow projection returned invalid data"));
                }
                result.push_back(application::CashFlowMonthlyProjection{
                    *period, *income, *expense, pg::getBool(row, 3)});
            }
            return result;
        });
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
