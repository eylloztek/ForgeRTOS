# Metrics and Benchmark Methodology

ForgeRTOS distinguishes **validation observations** from **cycle-accurate performance benchmarks**. Integration workloads prove that subsystems continue to behave correctly under load; they do not by themselves establish a precise context-switch cost or maximum throughput figure.

## Observed validation metrics

A representative sustained validation run reached:

```text
Messages sent:                      838432
Messages received:                  838432
Completed sync/PI cycles:            26200
Completed ACK handoffs:              26200
Maximum observed queue latency:          3 ticks
Ordering errors:                         0
Checksum errors:                         0
Queue send timeouts:                     0
Queue receive timeouts:                  0
ACK timeouts:                            0
Priority-inheritance validation errors:  0
Timing errors:                           0
Stress stack errors:                     0
```

Trace observations included sequence numbers above 3.3 million while the 128-entry trace ring continued to overwrite old records correctly.

These values are evidence of sustained integration correctness. They should not be converted into throughput or latency claims by dividing counters from unrelated manually sampled timestamps.

## Binary size

Both firmware images were built with GCC 14.3.1 using the project's
freestanding Cortex-M4 configuration.

| Image | `.text` | `.data` | `.bss` | FLASH usage | RAM usage |
|---|---:|---:|---:|---:|---:|
| `forgertos.elf` | 10,616 B | 0 B | 6,928 B | 10,616 B / 512 KiB (2.02%) | 6,928 B / 128 KiB (5.29%) |
| `forgertos_validation.elf` | 21,556 B | 12 B | 10,640 B | 21,568 B / 512 KiB (4.11%) | 10,656 B / 128 KiB (8.13%) |

The validation image is intentionally larger because it contains the
deterministic regression harness, sustained stress workload, additional task
stacks, synchronization objects, and validation counters.

The release image excludes these validation-only resources and contains only
the compact demonstration application and UART monitor.

## Context-switch benchmark

A cycle-accurate context-switch benchmark should use the Cortex-M DWT cycle counter and a dedicated benchmark firmware with UART output and general tracing disabled during the measured interval.

Recommended procedure:

1. Enable `DWT_CYCCNT`.
2. Create two controlled equal-priority tasks.
3. Capture the cycle count immediately before requesting a switch.
4. Capture the count when the peer task resumes.
5. Repeat enough samples to report minimum, median, and maximum cycles.
6. Document compiler flags and core frequency with the result.

Do not infer PendSV cost from UART timestamps; serial transmission, task scheduling, and monitor activity contaminate the measurement.

## Queue throughput benchmark

Use an exact sampling window in a dedicated benchmark configuration:

```text
start_tick
start_message_count
...
end_tick
end_message_count
```

Then calculate:

```text
messages_per_second =
    (end_message_count - start_message_count) * 1000
    / (end_tick - start_tick)
```

Run separate scenarios for:

- non-blocking queue operations
- blocking producer/consumer operation
- different item sizes
- different queue capacities

## Measurement discipline

When publishing performance numbers, include:

- MCU and core clock
- compiler/toolchain version
- optimization flags
- tick frequency
- enabled diagnostics/tracing
- benchmark source and iteration count
- minimum/median/maximum where applicable

Instrumentation can materially alter the timing being measured. ForgeRTOS therefore keeps UART reporting out of PendSV, SysTick, and synchronization hot paths and treats measurement code as an explicit benchmark configuration.
