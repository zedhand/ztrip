add_executable(ztrip_analyze main.cpp)
set_target_properties(ztrip_analyze PROPERTIES OUTPUT_NAME analyze-cli)
target_link_libraries(ztrip_analyze PRIVATE ztrip::core)
ztrip_set_warnings(ztrip_analyze)
