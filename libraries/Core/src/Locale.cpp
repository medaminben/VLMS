#include <VLMS/Core/Locale.h>

namespace VLMS {

std::string Locale::s_code = kDefaultCode;

const std::string& Locale::code()
{
    return s_code;
}

void Locale::setCode(std::string_view code)
{
    if (code == "ar" || code == "fr" || code == "en") {
        s_code.assign(code);
    } else {
        s_code = kDefaultCode;
    }
}

bool Locale::isRtl()
{
    return s_code == "ar";
}

}  // namespace VLMS
