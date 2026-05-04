add_executable(mini_os_demo tools/host/main.cpp)
target_link_libraries(mini_os_demo PRIVATE mini_os_core mini_os_options)

if(BUILD_TESTING)
  find_package(GTest CONFIG REQUIRED)
  add_executable(mini_os_tests tests/alignment_test.cpp tests/pl011_test.cpp tests/line_editor_test.cpp tests/fdt_test.cpp tests/resources_test.cpp tests/cpu_decode_test.cpp tests/cpus_test.cpp tests/monitor_test.cpp tests/exception_test.cpp tests/gic_test.cpp tests/timer_test.cpp tests/memory_test.cpp tests/mmu_test.cpp tests/heap_test.cpp tests/uart_irq_test.cpp tests/recovery_test.cpp tests/performance_test.cpp tests/topology_test.cpp tests/features_test.cpp tests/smp_test.cpp tests/scheduler_test.cpp tests/user_test.cpp tests/elf_test.cpp)
  target_link_libraries(mini_os_tests PRIVATE mini_os_core mini_os_pl011 mini_os_resources mini_os_gic mini_os_options GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(mini_os_tests
    DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30 PROPERTIES TIMEOUT 30)
endif()
