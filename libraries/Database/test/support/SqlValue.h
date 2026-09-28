#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Test {

/**
 * A bind/scalar cell for test SQL. Null is the default. Text coming back from
 * SQLite is stored as text; toInt() parses it the way QVariant::toInt did.
 */
class SqlValue {
public:
    enum class Kind { Null, Integer, Text };

    SqlValue() = default;
    SqlValue(int value)
        : m_kind(Kind::Integer)
        , m_integer(value)
        , m_text(std::to_string(value))
    {
    }
    SqlValue(std::int64_t value)
        : m_kind(Kind::Integer)
        , m_integer(value)
        , m_text(std::to_string(value))
    {
    }
    SqlValue(std::string value)
        : m_kind(Kind::Text)
        , m_text(std::move(value))
    {
    }
    SqlValue(std::string_view value)
        : m_kind(Kind::Text)
        , m_text(value)
    {
    }
    SqlValue(const char* value)
        : m_kind(value == nullptr ? Kind::Null : Kind::Text)
        , m_text(value == nullptr ? std::string() : std::string(value))
    {
    }

    [[nodiscard]] static SqlValue null() { return {}; }

    [[nodiscard]] bool isNull() const { return m_kind == Kind::Null; }
    [[nodiscard]] Kind kind() const { return m_kind; }
    [[nodiscard]] int toInt() const;
    [[nodiscard]] std::int64_t toInt64() const { return m_integer; }
    [[nodiscard]] const std::string& toString() const { return m_text; }

    [[nodiscard]] friend bool operator==(const SqlValue&, const SqlValue&) = default;

private:
    Kind m_kind = Kind::Null;
    std::int64_t m_integer = 0;
    std::string m_text;
};

using SqlBind = std::pair<std::string, SqlValue>;
using SqlBinds = std::vector<SqlBind>;

inline int SqlValue::toInt() const
{
    if (m_kind == Kind::Integer) {
        return static_cast<int>(m_integer);
    }
    if (m_kind != Kind::Text || m_text.empty()) {
        return 0;
    }
    try {
        return std::stoi(m_text);
    } catch (...) {
        return 0;
    }
}

}  // namespace Test
