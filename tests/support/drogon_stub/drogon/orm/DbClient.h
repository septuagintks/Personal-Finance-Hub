// Compile-only subset of the public Drogon DbClient transaction API.
#pragma once

#include <drogon/orm/Result.h>

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace drogon::orm {

class DatabaseFailure : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override {
        return "compile-only database failure";
    }
};

class DrogonDbException : public std::exception {
public:
    [[nodiscard]] const DatabaseFailure& base() const noexcept { return base_; }
    [[nodiscard]] const char* what() const noexcept override { return base_.what(); }

private:
    DatabaseFailure base_;
};

class Transaction;

class DbClient {
public:
    virtual ~DbClient() = default;

    template <typename... Arguments>
    Result execSqlSync(
        const std::string&,
        Arguments&&...) {
        return Result{nullptr};
    }

    [[nodiscard]] std::shared_ptr<Transaction> newTransaction(
        const std::function<void(bool)>& commit_callback = {});

    [[nodiscard]] static std::shared_ptr<DbClient> newPgClient(
        const std::string&,
        std::size_t) {
        return std::make_shared<DbClient>();
    }
};

class Transaction : public DbClient {
public:
    explicit Transaction(std::function<void(bool)> commit_callback = {})
        : commit_callback_(std::move(commit_callback)) {}

    void rollback() {}
    void setCommitCallback(const std::function<void(bool)>& callback) {
        commit_callback_ = callback;
    }

private:
    std::function<void(bool)> commit_callback_;
};

inline std::shared_ptr<Transaction> DbClient::newTransaction(
    const std::function<void(bool)>& commit_callback) {
    return std::make_shared<Transaction>(commit_callback);
}

using DbClientPtr = std::shared_ptr<DbClient>;

}  // namespace drogon::orm
