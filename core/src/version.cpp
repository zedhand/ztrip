#include "mre/version.hpp"

namespace mre {

std::string_view version() noexcept {
    return MRE_VERSION_STRING;
}

} // namespace mre
