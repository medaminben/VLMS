#pragma once

#include <string>
#include <string_view>

namespace VLMS {

class Locale {
public:
    static constexpr const char* kDefaultCode = "ar";

    static const std::string& code();
    static void setCode(std::string_view code);
    static bool isRtl();

private:
    static std::string s_code;
};

}  // namespace VLMS
