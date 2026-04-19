add_executable(mini_os_demo tools/host/main.cpp)
target_link_libraries(mini_os_demo PRIVATE mini_os_core mini_os_options)

if(BUILD_TESTING)
  find_package(GTest CONFIG REQUIRED)
  add_executable(mini_os_tests tests/alignment_test.cpp)
  target_link_libraries(mini_os_tests PRIVATE mini_os_core mini_os_options GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(mini_os_tests
    DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30 PROPERTIES TIMEOUT 30)
endif()
