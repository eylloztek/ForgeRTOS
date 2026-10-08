# Changelog

All notable ForgeRTOS changes are documented in this file.

## [0.1.0] - 2026-10-08

### Added

- Bare-metal STM32F446RE startup, vector table, and linker configuration.
- Cortex-M4 MSP/PSP bootstrap and exception-driven task startup.
- PendSV task-to-task context switching with R4-R11 preservation.
- SysTick-based 1 kHz kernel time base and wrap-safe timing API.
- Nested BASEPRI critical sections and explicit kernel interrupt-priority rules.
- Cortex-M HardFault, MemManage, BusFault, and UsageFault diagnostics.
- Fixed-capacity task registry and task-state model.
- Preemptive fixed-priority scheduler with equal-priority round robin.
- Kernel-owned idle task and task sleeping.
- Binary and counting semaphores.
- Non-recursive mutexes with direct handoff and priority inheritance.
- Fixed-capacity message queues with blocking send/receive.
- Event flags with wait-any, wait-all, and clear-on-exit semantics.
- Shared finite/indefinite timeout model for synchronization and IPC waits.
- Stack watermark, guard-region, saved-SP, and overflow diagnostics.
- Kernel assertions and runtime invariant scanning.
- Fixed-capacity kernel event trace ring.
- Register-level USART2 monitor for the NUCLEO-F446RE ST-LINK virtual COM port.
- Separate integration regression/stress firmware target.
- Compact release demonstration firmware.

### Validation

- Sustained validation reached 838,432 successful queue sends and receives.
- 26,200 mutex/priority-inheritance/ACK cycles completed in the captured run.
- Maximum observed queue latency was 3 ticks in the captured run.
- Ordering, checksum, queue-timeout, ACK-timeout, priority-inheritance, timing, and stress-stack error counters remained zero.
- Trace sequence exceeded 3.3 million events while the fixed 128-entry ring continued operating through wraparound.
