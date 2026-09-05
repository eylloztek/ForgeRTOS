cmake_minimum_required(VERSION 3.20)

project(ForgeRTOS VERSION 0.1.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(TARGET forgertos.elf)

add_executable(${TARGET}
    startup/startup_stm32f446re.S
    port/cortex_m4/cortex_m4.c
    port/cortex_m4/systick.c
    port/cortex_m4/portasm.S
    bsp/stm32f446re/board.c
    src/main.c
)

target_include_directories(${TARGET} PRIVATE include)

target_compile_options(${TARGET} PRIVATE
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

target_link_options(${TARGET} PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -nostdlib
    -nostartfiles
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_BINARY_DIR}/forgertos.map
    -T${CMAKE_SOURCE_DIR}/linker/stm32f446re.ld
)

add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_BINARY_DIR}/forgertos.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_BINARY_DIR}/forgertos.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
    COMMENT "Generating HEX/BIN images and printing image size"
)
