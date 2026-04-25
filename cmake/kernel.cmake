add_library(mini_os_kernel_options INTERFACE)
target_compile_options(mini_os_kernel_options INTERFACE
  -mcpu=cortex-a53
  "$<$<COMPILE_LANGUAGE:C,CXX>:-ffreestanding;-fno-builtin;-fno-stack-protector;-fno-pic;-fno-pie;-fno-unwind-tables;-fno-asynchronous-unwind-tables;-mgeneral-regs-only;-mstrict-align;-ffunction-sections;-fdata-sections>"
  "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++;-fno-exceptions;-fno-rtti;-fno-threadsafe-statics>")
target_link_libraries(mini_os_core PRIVATE mini_os_kernel_options)
target_link_libraries(mini_os_cpu PRIVATE mini_os_kernel_options)
target_link_libraries(mini_os_pl011 PRIVATE mini_os_kernel_options)
target_link_libraries(mini_os_resources PRIVATE mini_os_kernel_options)

add_executable(mini_os_kernel
  src/arch/aarch64/boot/start.S
  src/arch/aarch64/cpu.cpp
  src/arch/aarch64/vectors.S
  src/arch/aarch64/faults.S
  src/arch/aarch64/exceptions.cpp
  src/platform/qemu_virt/console.cpp
  src/platform/qemu_virt/boot_resources.cpp
  src/kernel/boot.cpp
  src/kernel/console.cpp)
set_target_properties(mini_os_kernel PROPERTIES OUTPUT_NAME kernel SUFFIX .elf)
target_link_libraries(mini_os_kernel PRIVATE
  mini_os_core mini_os_pl011 mini_os_resources mini_os_options mini_os_kernel_options)
set(KERNEL_LINKER_SCRIPT "${PROJECT_SOURCE_DIR}/src/platform/qemu_virt/kernel.ld")
set_property(TARGET mini_os_kernel APPEND PROPERTY LINK_DEPENDS "${KERNEL_LINKER_SCRIPT}")
target_link_options(mini_os_kernel PRIVATE
  "--ld-path=${CMAKE_LINKER}" -nostdlib -static
  "LINKER:-T,${KERNEL_LINKER_SCRIPT}"
  "LINKER:-Map,${CMAKE_CURRENT_BINARY_DIR}/kernel.map"
  "LINKER:--gc-sections,--no-undefined,-z,max-page-size=4096")

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_program(MINI_OS_QEMU NAMES qemu-system-aarch64 REQUIRED)
  add_test(NAME kernel.boot
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.uart
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      uart-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.fdt
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      fdt-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.monitor
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      monitor-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.exception
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      fault-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  set_tests_properties(kernel.boot kernel.uart kernel.fdt kernel.monitor kernel.exception PROPERTIES TIMEOUT 20)
  add_test(NAME kernel.elf
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/verify_elf.py"
      "$<TARGET_FILE:mini_os_kernel>")
  foreach(protocol IN ITEMS boot uart fdt monitor fault)
    add_test(NAME tools.${protocol}_runner
      COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${PROJECT_SOURCE_DIR}/tests/python" -p "test_${protocol}_runner.py")
    set_tests_properties(tools.${protocol}_runner PROPERTIES TIMEOUT 20)
  endforeach()
  set_tests_properties(kernel.elf PROPERTIES TIMEOUT 20)
endif()
