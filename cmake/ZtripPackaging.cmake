# CPack: one archive per platform, ztrip-<version>-<platform>.(zip|tar.gz),
# holding the cli, sdk and docs components under a ztrip-<version>-<platform>/
# top-level folder. Build it with:
#   cpack --config build/release/CPackConfig.cmake -B build/release/packages
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
if(TARGET ztrip_godot)
  list(APPEND CPACK_COMPONENTS_ALL godot)
endif()

include(CPack)
