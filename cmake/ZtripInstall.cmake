# Install rules: what goes into a release archive, split by component.
#   cli   analyze-cli (plus the MSVC runtime DLLs on Windows)
#   sdk   ztrip_core static library, headers and a CMake package
#         (find_package(ztrip) gives ztrip::core)
#   docs  README, docs, examples, beta notes and third-party notices
#   godot the Godot addon and demo project (only with ZTRIP_BUILD_GODOT)
include(CMakePackageConfigHelpers)

set(ZTRIP_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/ztrip")

if(TARGET ztrip_analyze)
  install(TARGETS ztrip_analyze ztrip_live
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT cli)

  if(WIN32)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT cli)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
  endif()
endif()

install(TARGETS ztrip_core ztrip_pffft ztrip_miniaudio ztrip_rtmidi EXPORT ztripTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT sdk)
install(DIRECTORY core/include/ztrip
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT sdk)
install(EXPORT ztripTargets
  NAMESPACE ztrip::
  DESTINATION ${ZTRIP_CMAKE_INSTALL_DIR}
  COMPONENT sdk)

configure_package_config_file(cmake/ztripConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/ztripConfig.cmake"
  INSTALL_DESTINATION ${ZTRIP_CMAKE_INSTALL_DIR})
# 0.x releases may break the API between minor versions.
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/ztripConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMinorVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/ztripConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/ztripConfigVersion.cmake"
  DESTINATION ${ZTRIP_CMAKE_INSTALL_DIR} COMPONENT sdk)

install(FILES README.md packaging/BETA.md
  DESTINATION . COMPONENT docs)
install(DIRECTORY docs/ DESTINATION docs COMPONENT docs)
install(DIRECTORY examples/ DESTINATION examples COMPONENT docs)
# pffft, miniaudio and RtMidi are compiled into the library and nlohmann/json into its
# .mgraph.json code, so their licenses ship with every archive.
install(FILES "${pffft_SOURCE_DIR}/LICENSE.txt"
  DESTINATION licenses RENAME pffft-LICENSE.txt COMPONENT docs)
install(FILES "${nlohmann_json_SOURCE_DIR}/LICENSE.MIT"
  DESTINATION licenses RENAME nlohmann_json-LICENSE.txt COMPONENT docs)
install(FILES "${miniaudio_SOURCE_DIR}/LICENSE"
  DESTINATION licenses RENAME miniaudio-LICENSE.txt COMPONENT docs)
install(FILES "${rtmidi_SOURCE_DIR}/LICENSE"
  DESTINATION licenses RENAME rtmidi-LICENSE.txt COMPONENT docs)

if(TARGET ztrip_godot)
  # The demo project with the built addon inside, ready to open in Godot.
  install(DIRECTORY hosts/godot/project/
    DESTINATION godot COMPONENT godot
    PATTERN ".godot" EXCLUDE
    PATTERN "tests" EXCLUDE
    PATTERN ".gitignore" EXCLUDE
    PATTERN "*.wav" EXCLUDE
    REGEX "music/.*\\.import$" EXCLUDE # Godot's import files for local songs
    PATTERN "*.lib" EXCLUDE
    PATTERN "*.exp" EXCLUDE)
  install(FILES hosts/godot/README.md DESTINATION godot COMPONENT godot)
endif()
