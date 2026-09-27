#pragma once

#include <QString>

#include <string>
#include <string_view>

inline QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}
