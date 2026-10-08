# UART Monitor

ForgeRTOS exposes runtime status through USART2 on the NUCLEO-F446RE ST-LINK virtual COM port. The monitor is intentionally implemented as a low-priority task so application work can preempt serial transmission.

## Hardware configuration

```text
Peripheral : USART2
TX         : PA2 / AF7
RX         : PA3 / AF7
Baud       : 115200
Format     : 8 data bits, no parity, 1 stop bit
Flow ctrl  : none
Clock      : 16 MHz APB1 peripheral clock
```

The driver configures RCC, GPIOA alternate functions, and USART2 directly at register level.

## Release-image commands

Send commands as individual characters without CR/LF:

```text
h / ?   help
v       ForgeRTOS version
s       application + UART + trace status
k       kernel invariants + assertion/fault state
t       newest trace entries
```

Example status:

```text
STATUS tick=12491 tasks=3 sent=125 recv=125 q=0 latmax=1 derr=0 trace=128/128 ovw=314 rxerr=0
```

Example kernel report:

```text
KERNEL tasks=3 run=1 ready=0 blocked=2 current=3 inv=0x00000000 assert=0 fault=0x00000000
```

Interpretation:

- `derr=0`: release-demo data/task error aggregate is clear
- `rxerr=0`: UART receive error counter is clear
- `inv=0x00000000`: runtime invariant scanner found no violation
- `assert=0`: no kernel assertion failure recorded
- `fault=0x00000000`: no Cortex-M fault record present

## Validation-image commands

The validation firmware adds stress reporting and trace clearing:

```text
h / ?   help
s       kernel/UART/trace status
t       newest trace entries
x       stress workload status
c       clear trace ring
```

A typical stress report:

```text
STRESS run=1 sent=838432 recv=838432 q=0/8 sync=26200/26201 mutex=26200 ack=26200 err=0
STRESSCHK order=0 crc=0 laterr=0 latmax=3 qtx=0 qrx=0 ackto=0 pi=0 timing=0 stack=0
```

## Terminal configuration

Use:

```text
115200 baud
8 data bits
no parity
1 stop bit
no flow control
```

The current RX path is polling-based. The monitor protocol is therefore intentionally based on one-byte commands. Disable terminal settings that automatically append CR/LF to each command when possible.

If a terminal sends bursts faster than the monitor drains USART2, the hardware overrun flag can be raised because this release does not implement an interrupt-driven software RX ring buffer.

## Design rationale

Kernel hot paths never wait for UART transmission. Instead:

```text
scheduler / synchronization / timeout path
                │
                ▼
        in-memory trace ring
                │
                ▼
       low-priority monitor task
                │
                ▼
             USART2
```

This preserves observability without putting serial I/O inside PendSV, SysTick, or critical synchronization paths.
