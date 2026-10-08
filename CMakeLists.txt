cmake_minimum_required(VERSION 3.20)

project(ForgeRTOS VERSION 0.1.0 LANGUAGES C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(FR_BUILD_VALIDATION "Build the integration/stress validation firmware" OFF)

set(FR_COMMON_SOURCES
    startup/startup_stm32f446re.S
    kernel/scheduler.c
    kernel/semaphore.c
    kernel/mutex.c
    kernel/queue.c
    kernel/event_flags.c
    kernel/wait_timeout.c
    kernel/assert.c
    kernel/diagnostics.c
    kernel/trace.c
    kernel/task.c
    kernel/tick.c
    port/cortex_m4/context_probe.S
    port/cortex_m4/cortex_m4.c
    port/cortex_m4/critical.c
    port/cortex_m4/fault.c
    port/cortex_m4/pendsv.c
    port/cortex_m4/systick.c
    port/cortex_m4/task_context.c
    port/cortex_m4/portasm.S
    bsp/stm32f446re/board.c
    bsp/stm32f446re/uart.c
)

set(FR_COMMON_COMPILE_OPTIONS
    -mcpu=cortex-m4
    -mthumb
    -ffreestanding
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -Og
    -g3
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

function(fr_configure_firmware target output_stem)
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/kernel/include
    )

    target_compile_options(${target} PRIVATE
        ${FR_COMMON_COMPILE_OPTIONS}
    )

    target_link_options(${target} PRIVATE
        -mcpu=cortex-m4
        -mthumb
        -nostdlib
        -nostartfiles
        -Wl,--gc-sections
        -Wl,--print-memory-usage
        -Wl,-Map=${CMAKE_BINARY_DIR}/${output_stem}.map
        -T${CMAKE_SOURCE_DIR}/linker/stm32f446re.ld
    )

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex
                $<TARGET_FILE:${target}>
                ${CMAKE_BINARY_DIR}/${output_stem}.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${target}>
                ${CMAKE_BINARY_DIR}/${output_stem}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
        COMMENT "Generating ${output_stem}.hex/.bin and printing image size"
    )
endfunction()

add_executable(forgertos.elf
    ${FR_COMMON_SOURCES}
    src/main.c
    src/demo.c
    src/monitor.c
)

fr_configure_firmware(forgertos.elf forgertos)

if(FR_BUILD_VALIDATION)
    add_executable(forgertos_validation.elf
        ${FR_COMMON_SOURCES}
        tests/integration/main.c
        tests/integration/stress.c
    )

    target_include_directories(forgertos_validation.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/integration
    )

    fr_configure_firmware(forgertos_validation.elf forgertos_validation)
endif()
