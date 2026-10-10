#pragma once

#include <string_view>

namespace ztrip {

// Library version string, e.g. "0.1.0-beta.1" (see docs/RELEASING.md).
std::string_view version() noexcept;

} // namespace ztrip
