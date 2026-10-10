include(FetchContent)

FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1
  GIT_SHALLOW    TRUE
  FIND_PACKAGE_ARGS 3
)
FetchContent_MakeAvailable(Catch2)

add_executable(mre_tests
  test_smoke.cpp
)
target_link_libraries(mre_tests PRIVATE mre::core Catch2::Catch2WithMain)
mre_set_warnings(mre_tests)

include(Catch)
catch_discover_tests(mre_tests)
