#include "ztrip/version.hpp"

namespace ztrip {

std::string_view version() noexcept {
    return ZTRIP_VERSION_STRING;
}

} // namespace ztrip
