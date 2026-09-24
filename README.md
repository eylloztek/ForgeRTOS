# ForgeRTOS

ForgeRTOS is a small preemptive real-time operating system kernel for ARM Cortex-M4, developed from scratch in C and ARM Thumb assembly. The project is built around the STM32F446RE and focuses on understanding the low-level mechanisms behind an RTOS, including task contexts, exception handling, scheduling, synchronization, inter-process communication, timing, tracing, and kernel diagnostics.

Rather than relying on an existing RTOS or CMSIS-RTOS layer, ForgeRTOS implements its own kernel primitives and Cortex-M4 port while keeping architecture-specific code separated from the portable kernel logic.

## Development Roadmap

- ~~Bootstrap the bare-metal Cortex-M4 target with custom startup code, linker script, CMake build system, and GPIO test.~~
- ~~Document and inspect the Cortex-M4 execution model, vector table, MSP, PSP, CONTROL, IPSR, and VTOR.~~
- ~~Implement the MSP-to-PSP transition and inspect Cortex-M exception stacking with SVC.~~
- ~~Configure the Cortex-M4 SysTick timer and establish a 1 ms hardware time base.~~
- ~~Add the kernel tick layer and wrap-safe time API.~~
- ~~Implement nested BASEPRI-based critical sections and kernel interrupt-priority rules.~~
- ~~Add Cortex-M fault diagnostics for HardFault, MemManage, BusFault, and UsageFault exceptions.~~
- ~~Define task control blocks, task states, priorities, stack ownership, and a fixed-capacity task registry.~~
- ~~Initialize synthetic Cortex-M4 task contexts with software-saved registers and hardware exception frames.~~
- ~~Add the first cooperative scheduler and start the highest-priority READY task through SVC exception return.~~
- ~~Implement cooperative task yielding and full task-to-task context switching.~~
- ~~Move context switching from SVC to PendSV.~~
- ~~Preserve and restore R4-R11 through the PendSV context-switch path.~~
- ~~Validate repeated task context switching and register preservation under stress.~~
- ~~Enable preemptive scheduling using SysTick and PendSV.~~
- ~~Implement fixed-priority scheduling.~~
- ~~Add round-robin scheduling for tasks with equal priority.~~
- ~~Add an idle task and defined CPU idle behavior.~~
- ~~Implement task sleeping and the BLOCKED state.~~
- ~~Add timeout and wake-up management for blocked tasks.~~
- ~~Implement binary semaphores.~~
- ~~Implement counting semaphores.~~
- ~~Implement mutex synchronization.~~
- Add a reproducible priority-inversion test.
- Implement mutex priority inheritance.
- Add kernel message queues.
- Implement blocking queue send and receive operations.
- Add task event flags.
- Add timeout support to kernel synchronization and IPC objects.
- Add task stack watermark and overflow diagnostics.
- Add kernel assertions and runtime invariants.
- Implement kernel event tracing.
- Add a UART-based kernel monitor interface.
- Add RTOS stress tests covering tasks, semaphores, mutexes, queues, and timing.
- Prepare the final demo, benchmarks, architecture documentation, and the first ForgeRTOS release.
