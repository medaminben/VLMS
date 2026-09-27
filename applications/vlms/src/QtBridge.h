#pragma once

#include <VLMS/Core/Date.h>
#include <VLMS/Core/Strings.h>

#include <QDate>
#include <QString>
#include <QStringList>

#include <string>
#include <string_view>
#include <vector>

namespace VLMS {

[[nodiscard]] inline QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

[[nodiscard]] inline std::string ss(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] inline QStringList qsl(const std::vector<std::string>& values)
{
    QStringList out;
    out.reserve(static_cast<int>(values.size()));
    for (const std::string& value : values) {
        out.append(qs(value));
    }
    return out;
}

[[nodiscard]] inline std::vector<std::string> svl(const QStringList& values)
{
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(values.size()));
    for (const QString& value : values) {
        out.push_back(ss(value));
    }
    return out;
}

[[nodiscard]] inline QDate qd(const Date& date)
{
    if (!date.isValid()) {
        return {};
    }
    return QDate(date.year(), date.month(), date.day());
}

[[nodiscard]] inline Date cd(const QDate& date)
{
    if (!date.isValid()) {
        return {};
    }
    return Date(date.year(), date.month(), date.day());
}

[[nodiscard]] inline QString T(std::string_view key,
                               std::string_view param = {},
                               std::string_view value = {})
{
    return qs(Strings::t(key, param, value));
}

[[nodiscard]] inline QString T2(std::string_view key,
                                std::string_view p1,
                                std::string_view v1,
                                std::string_view p2,
                                std::string_view v2)
{
    return qs(Strings::t2(key, p1, v1, p2, v2));
}

}  // namespace VLMS
