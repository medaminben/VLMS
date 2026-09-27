#pragma once

#include <optional>
#include <string>
#include <utility>

namespace VLMS {

/**
 * Why a repository call failed. The UI maps `key` through Strings::t() --
 * never through a driver fragment -- so a librarian sees a translated
 * sentence rather than English SQL or a blank box.
 *
 * NotFound is not Sql: a query that ran and matched no rows has no driver
 * error. Treating it as one was Defect 9 (empty English warning dialogs).
 */
enum class ErrorKind {
    NotFound,
    Validation,
    Sql,
};

struct Error {
    ErrorKind kind = ErrorKind::Sql;
    std::string key;
    std::string detail;
};

/**
 * A valued outcome. Truthy exactly when a value is present.
 *
 * Failures carry an Error; there is no parallel lastError() channel.
 */
template<typename T>
class Result {
public:
    static Result ok(T value)
    {
        Result result;
        result.m_value = std::move(value);
        return result;
    }

    static Result fail(const ErrorKind kind, std::string key, std::string detail = {})
    {
        Result result;
        result.m_error = Error{kind, std::move(key), std::move(detail)};
        return result;
    }

    [[nodiscard]] explicit operator bool() const { return m_value.has_value(); }
    [[nodiscard]] bool hasValue() const { return m_value.has_value(); }
    [[nodiscard]] bool has_value() const { return m_value.has_value(); }

    [[nodiscard]] const T& value() const { return *m_value; }
    [[nodiscard]] T& value() { return *m_value; }

    [[nodiscard]] const T& operator*() const { return *m_value; }
    [[nodiscard]] T& operator*() { return *m_value; }
    [[nodiscard]] const T* operator->() const { return &*m_value; }
    [[nodiscard]] T* operator->() { return &*m_value; }

    [[nodiscard]] const Error& error() const { return m_error; }
    [[nodiscard]] ErrorKind kind() const { return m_error.kind; }

private:
    std::optional<T> m_value;
    Error m_error;
};

/**
 * An unvalued outcome -- create/update/delete. Truthy on success.
 *
 * Separate from Result so a mutation cannot be confused with "no row".
 */
class Status {
public:
    static Status ok() { return Status(); }

    static Status fail(const ErrorKind kind, std::string key, std::string detail = {})
    {
        Status status;
        status.m_ok = false;
        status.m_error = Error{kind, std::move(key), std::move(detail)};
        return status;
    }

    [[nodiscard]] explicit operator bool() const { return m_ok; }

    [[nodiscard]] const Error& error() const { return m_error; }
    [[nodiscard]] ErrorKind kind() const { return m_error.kind; }

private:
    bool m_ok = true;
    Error m_error;
};

[[nodiscard]] inline Status asStatus(const Error& error)
{
    return Status::fail(error.kind, error.key, error.detail);
}

template<typename T>
[[nodiscard]] Status asStatus(const Result<T>& result)
{
    return result ? Status::ok() : asStatus(result.error());
}

}  // namespace VLMS
