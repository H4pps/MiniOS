add_library(mini_os_kernel_options INTERFACE)
target_compile_options(mini_os_kernel_options INTERFACE
  -mcpu=cortex-a53
  "$<$<COMPILE_LANGUAGE:C,CXX>:-ffreestanding;-fno-builtin;-fno-stack-protector;-fno-pic;-fno-pie;-fno-unwind-tables;-fno-asynchronous-unwind-tables;-mgeneral-regs-only;-mstrict-align;-ffunction-sections;-fdata-sections>"
  "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++;-fno-exceptions;-fno-rtti;-fno-threadsafe-statics>")
target_link_libraries(mini_os_gic PRIVATE mini_os_kernel_options)
target_link_libraries(mini_os_virtio PRIVATE mini_os_kernel_options)
target_link_libraries(mini_os_core PRIVATE mini_os_kernel_options)
target_link_libraries(mini_os_cpu PRIVATE mini_os_kernel_options)
target_link_libraries(mini_os_pl011 PRIVATE mini_os_kernel_options)
target_link_libraries(mini_os_resources PRIVATE mini_os_kernel_options)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(mini_os_user_demo src/user/aarch64/start.S src/user/demo.cpp)
target_include_directories(mini_os_user_demo PRIVATE include)
target_link_libraries(mini_os_user_demo PRIVATE mini_os_options mini_os_kernel_options)
set_target_properties(mini_os_user_demo PROPERTIES OUTPUT_NAME user-demo SUFFIX .elf)
set(USER_LINKER_SCRIPT "${PROJECT_SOURCE_DIR}/src/user/aarch64/user.ld")
set_property(TARGET mini_os_user_demo APPEND PROPERTY LINK_DEPENDS "${USER_LINKER_SCRIPT}")
target_link_options(mini_os_user_demo PRIVATE "--ld-path=${CMAKE_LINKER}" -nostdlib -static
  "LINKER:-T,${USER_LINKER_SCRIPT}" "LINKER:--gc-sections,--no-undefined,-z,max-page-size=4096")
set(USER_EMBEDDED_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/user_image.cpp")
add_custom_command(OUTPUT "${USER_EMBEDDED_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/embed_elf.py"
    "$<TARGET_FILE:mini_os_user_demo>" "${USER_EMBEDDED_SOURCE}"
  DEPENDS mini_os_user_demo "${PROJECT_SOURCE_DIR}/scripts/embed_elf.py" VERBATIM)

add_executable(mini_os_kernel
  src/arch/aarch64/boot/start.S
  src/arch/aarch64/dma.cpp
  src/platform/qemu_virt/virtio.cpp
  src/arch/aarch64/cpu.cpp
  src/arch/aarch64/smp.cpp
  src/arch/aarch64/boot/secondary.S
  src/platform/qemu_virt/smp.cpp
  src/arch/aarch64/tasks.cpp
  src/arch/aarch64/tasks.S
  src/arch/aarch64/task_probe.S
  src/platform/qemu_virt/tasks.cpp
  src/kernel/tasks.cpp
  src/arch/aarch64/user.cpp
  src/arch/aarch64/user_entry.S
  src/arch/aarch64/user_program.S
  src/kernel/user_runtime.cpp
  src/platform/qemu_virt/user.cpp
  src/arch/aarch64/features.cpp
  src/arch/aarch64/timer.cpp
  src/platform/qemu_virt/timer.cpp
  src/platform/qemu_virt/memory.cpp
  src/platform/qemu_virt/heap.cpp
  src/platform/qemu_virt/performance.cpp
  src/arch/aarch64/mmu.cpp
  src/platform/qemu_virt/mmu.cpp
  src/arch/aarch64/irq.cpp
  src/arch/aarch64/irq_probe.S
  src/platform/qemu_virt/interrupts.cpp
  src/arch/aarch64/vectors.S
  src/arch/aarch64/recovery_probe.S
  src/arch/aarch64/faults.S
  src/arch/aarch64/exceptions.cpp
  src/platform/qemu_virt/console.cpp
  src/platform/qemu_virt/boot_resources.cpp
  src/kernel/boot.cpp
  src/kernel/console.cpp
  "${USER_EMBEDDED_SOURCE}")
set_target_properties(mini_os_kernel PROPERTIES OUTPUT_NAME kernel SUFFIX .elf)
target_link_libraries(mini_os_kernel PRIVATE
  mini_os_core mini_os_pl011 mini_os_resources mini_os_gic mini_os_virtio mini_os_options mini_os_kernel_options)
set(KERNEL_LINKER_SCRIPT "${PROJECT_SOURCE_DIR}/src/platform/qemu_virt/kernel.ld")
set_property(TARGET mini_os_kernel APPEND PROPERTY LINK_DEPENDS "${KERNEL_LINKER_SCRIPT}")
target_link_options(mini_os_kernel PRIVATE
  "--ld-path=${CMAKE_LINKER}" -nostdlib -static
  "LINKER:-T,${KERNEL_LINKER_SCRIPT}"
  "LINKER:-Map,${CMAKE_CURRENT_BINARY_DIR}/kernel.map"
  "LINKER:--gc-sections,--no-undefined,-z,max-page-size=4096")

if(BUILD_TESTING)
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
  add_test(NAME kernel.irq
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      irq-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.timer
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      timer-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.memory
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      memory-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.mmu
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      mmu-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.mmu_fault
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      mmu-fault-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.heap
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      heap-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.uart_irq
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      uart-irq-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.recovery
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      recovery-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.performance
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      performance-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.cpu_discovery
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      cpu-discovery-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.smp
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      smp-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.tasks
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      task-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.user
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      user-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.virtio
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      virtio-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  set_tests_properties(kernel.virtio PROPERTIES TIMEOUT 20)
  add_test(NAME kernel.elf_loader
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/qemu.py"
      elf-test --image "$<TARGET_FILE:mini_os_kernel>" --qemu "${MINI_OS_QEMU}")
  add_test(NAME kernel.user_elf
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/verify_user_elf.py"
      "$<TARGET_FILE:mini_os_kernel>" "$<TARGET_FILE:mini_os_user_demo>")
  set_tests_properties(kernel.elf_loader kernel.user_elf PROPERTIES TIMEOUT 20)
  set_tests_properties(kernel.boot kernel.uart kernel.fdt kernel.monitor kernel.exception kernel.irq kernel.timer kernel.memory kernel.mmu kernel.mmu_fault kernel.heap kernel.uart_irq kernel.recovery kernel.performance kernel.cpu_discovery kernel.smp kernel.tasks kernel.user PROPERTIES TIMEOUT 20)
  add_test(NAME kernel.elf
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/verify_elf.py"
      "$<TARGET_FILE:mini_os_kernel>")
  foreach(protocol IN ITEMS boot uart fdt monitor fault irq timer memory mmu mmu_fault heap fdt_edit uart_irq recovery stack_fault performance cpu_discovery smp task user elf_loader user_image virtio)
    add_test(NAME tools.${protocol}_runner
      COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${PROJECT_SOURCE_DIR}/tests/python" -p "test_${protocol}_runner.py")
    set_tests_properties(tools.${protocol}_runner PROPERTIES TIMEOUT 20)
  endforeach()
  set_tests_properties(kernel.elf PROPERTIES TIMEOUT 20)
endif()
