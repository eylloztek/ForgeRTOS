# Project Structure

ForgeRTOS separates the kernel, hardware port, release application, and validation firmware so each part of the repository has a clear responsibility.

## Directory layout

```text
ForgeRTOS/
├── bsp/
│   └── stm32f446re/
│       ├── board.c
│       └── uart.c
├── cmake/
├── docs/
├── include/
│   └── forge/
├── kernel/
│   ├── include/forge/kernel/
│   └── *.c
├── linker/
├── port/
│   └── cortex_m4/
├── src/
│   ├── main.c
│   ├── demo.c
│   ├── demo.h
│   ├── monitor.c
│   └── monitor.h
├── startup/
└── tests/
    └── integration/
        ├── main.c
        ├── stress.c
        └── stress.h
```

## Kernel and public API

`include/forge/` contains application-facing interfaces. Portable kernel implementation lives under `kernel/`, while kernel-only interfaces are kept under `kernel/include/forge/kernel/`.

This keeps public API boundaries distinct from scheduler/TCB internals.

## Cortex-M4 port

`port/cortex_m4/` owns architecture-specific behavior such as:

- SVC/PendSV exception paths
- task stack-frame initialization
- R4-R11 context preservation
- SysTick setup
- BASEPRI critical sections
- Cortex-M fault handling

Keeping this code separate makes the boundary between kernel policy and CPU mechanism explicit.

## BSP

`bsp/stm32f446re/` owns board/MCU-specific peripheral support used by the project, currently GPIO/LED setup and the USART2 monitor transport.

## Release application

`src/` contains a deliberately small example firmware. Its purpose is to demonstrate the kernel without embedding the entire regression history in the application entry point.

The release `main.c` is responsible for platform/kernel initialization and scheduler startup. Demo workload and UART monitoring are split into their own translation units.

## Integration validation

`tests/integration/` retains the comprehensive regression and stress firmware. It is not dead code and should remain version-controlled: it provides repeatable checks for scheduler behavior, IPC, priority inheritance, timeout handling, stack diagnostics, assertions, tracing, and UART observability.

The validation harness is compiled into its own target rather than linked into the release image.

## Build targets

```text
forgertos.elf
    release/demo firmware

forgertos_validation.elf
    integration regression + stress firmware
```

Both targets reuse the same common startup, kernel, Cortex-M4 port, BSP, and linker script. This means validation exercises the same kernel binary sources as the release image while allowing a much larger application-level test harness.
