# ForgeRTOS v0.1.0

ForgeRTOS v0.1.0 is the first tagged release of the STM32F446RE / ARM Cortex-M4 RTOS kernel.

## Highlights

- Preemptive fixed-priority scheduling
- Round-robin scheduling for equal priorities
- PendSV Cortex-M4 context switching with R4-R11 preservation
- SysTick timing, task sleep, and blocked-task deadlines
- Binary and counting semaphores
- Non-recursive mutexes with priority inheritance
- Blocking fixed-capacity message queues
- Event flags with wait-any / wait-all support
- Stack watermark, guard, and saved-SP diagnostics
- Kernel assertions and runtime invariant scanning
- In-memory event tracing
- UART-based runtime monitor
- Separate integration/stress validation firmware

## Validation summary

A representative sustained validation run exercised seven user tasks while combining queue traffic, semaphore handoffs, mutex contention, repeated priority inheritance, sleep/timeouts, tracing, stack diagnostics, and UART monitoring.

Captured result:

```text
838432 messages sent
838432 messages received
26200 completed mutex / priority-inheritance cycles
26200 ACK handoffs
maximum observed queue latency: 3 ticks
stress error count: 0
```

No ordering, checksum, queue-timeout, ACK-timeout, priority-inheritance, timing, or stress-stack error was recorded in the captured snapshot. The trace sequence exceeded 3.3 million events while the fixed-capacity ring continued operating correctly.

## Repository organization

The release firmware and validation firmware are intentionally separate:

```text
forgertos.elf             compact release demonstration
forgertos_validation.elf  regression + sustained stress validation
```

The comprehensive validation harness remains under `tests/integration/`, keeping the release `src/main.c` small without discarding executable regression coverage.

## Known limitations

- Single supported target: STM32F446RE / NUCLEO-F446RE.
- UART RX monitor path is polling-based and intended for single-byte commands.
- No dynamic memory allocator.
- No MPU-based task isolation.
- No tickless idle.
- No formal safety certification.

## Tag

```text
v0.1.0
```
