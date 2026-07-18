// Compile-only subset of Drogon ORM Result/Row/Field.
#pragma once

#include <cstddef>
#include <vector>

namespace drogon::orm {

class Field {
public:
    [[nodiscard]] bool isNull() const noexcept { return false; }

    template <typename T>
    [[nodiscard]] T as() const {
        return T{};
    }
};

class Row {
public:
    [[nodiscard]] Field operator[](std::size_t) const { return {}; }
};

class Result {
public:
    Result() = delete;
    explicit Result(std::nullptr_t) {}

    using const_iterator = std::vector<Row>::const_iterator;

    [[nodiscard]] bool empty() const noexcept { return rows_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }
    [[nodiscard]] std::size_t affectedRows() const noexcept { return 0; }
    [[nodiscard]] const Row& operator[](std::size_t index) const {
        return rows_[index];
    }
    [[nodiscard]] const_iterator begin() const noexcept { return rows_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return rows_.end(); }

private:
    std::vector<Row> rows_;
};

}  // namespace drogon::orm
