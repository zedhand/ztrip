# CPack: one archive per platform, ztrip-<version>-<platform>.(zip|tar.gz),
# holding the cli, sdk and docs components under a ztrip-<version>-<platform>/
# top-level folder. Build it with:
#   cpack --config build/release/CPackConfig.cmake -B build/release/packages
#
# The Godot addon can come from a second build tree: ZTRIP_BUILD_GODOT switches
# MSVC to the static runtime, which the SDK library must not use, so the
# release workflow builds the addon separately and points
# ZTRIP_PACKAGE_GODOT_BUILD_DIR at it. Its godot component then joins the
# archive.
if(WIN32)
  set(_ztrip_default_platform "windows-x64")
  set(_ztrip_default_generator "ZIP")
else()
  string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" _ztrip_default_platform)
  set(_ztrip_default_generator "TGZ")
endif()
set(ZTRIP_PACKAGE_PLATFORM "${_ztrip_default_platform}" CACHE STRING
  "Platform suffix in release archive names, e.g. linux-x86_64 or windows-x64")

set(CPACK_PACKAGE_NAME "ztrip")
set(CPACK_PACKAGE_VENDOR "ztrip")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${ZTRIP_VERSION_FULL}")
set(CPACK_SYSTEM_NAME "${ZTRIP_PACKAGE_PLATFORM}")
set(CPACK_PACKAGE_FILE_NAME "ztrip-${ZTRIP_VERSION_FULL}-${ZTRIP_PACKAGE_PLATFORM}")
set(CPACK_GENERATOR "${_ztrip_default_generator}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_VERBATIM_VARIABLES ON)

# Only ztrip's own components: dependencies fetched with FetchContent (Catch2,
# nlohmann/json) bring install rules of their own that must stay out.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_COMPONENTS_ALL docs sdk)
if(TARGET ztrip_analyze)
  list(PREPEND CPACK_COMPONENTS_ALL cli)
endif()
set(ZTRIP_PACKAGE_GODOT_BUILD_DIR "" CACHE PATH
  "Build tree of a separate ZTRIP_BUILD_GODOT build whose godot component goes into the archive")
if(TARGET ztrip_godot OR ZTRIP_PACKAGE_GODOT_BUILD_DIR)
  list(APPEND CPACK_COMPONENTS_ALL godot)
endif()
if(ZTRIP_PACKAGE_GODOT_BUILD_DIR)
  if(NOT EXISTS "${ZTRIP_PACKAGE_GODOT_BUILD_DIR}/cmake_install.cmake")
    message(FATAL_ERROR "ZTRIP_PACKAGE_GODOT_BUILD_DIR (${ZTRIP_PACKAGE_GODOT_BUILD_DIR}) is not a configured build tree")
  endif()
  # The third field names a component set: CPACK_COMPONENTS_ALL for this
  # tree, CPACK_COMPONENTS_GODOT (just godot) for the other.
  set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR};ztrip;ALL;/"
    "${ZTRIP_PACKAGE_GODOT_BUILD_DIR};ztrip-godot;GODOT;/")
  set(CPACK_COMPONENTS_GODOT godot)
endif()

include(CPack)
