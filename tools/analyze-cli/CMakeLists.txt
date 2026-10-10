add_executable(mre_analyze main.cpp)
set_target_properties(mre_analyze PROPERTIES OUTPUT_NAME analyze-cli)
target_link_libraries(mre_analyze PRIVATE mre::core)
mre_set_warnings(mre_analyze)
