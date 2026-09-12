.syntax unified
.cpu cortex-m4
.thumb

.equ FR_SVC_START_FIRST_TASK, 0
.equ FR_SVC_YIELD,            1

.extern fr_scheduler_current_saved_sp
.extern fr_scheduler_yield_from_isr
.extern fr_fault_handle_c


.section .text.fr_arch_enter_thread_psp,"ax",%progbits
.align 2
.global fr_arch_enter_thread_psp
.type fr_arch_enter_thread_psp, %function
.thumb_func

fr_arch_enter_thread_psp:
    msr psp, r0

    mrs r2, control
    orr r2, r2, #2
    msr control, r2
    isb

    bx r1

.size fr_arch_enter_thread_psp, .-fr_arch_enter_thread_psp


.section .text.fr_port_start_first_task,"ax",%progbits
.align 2
.global fr_port_start_first_task
.type fr_port_start_first_task, %function
.thumb_func

fr_port_start_first_task:
    svc #FR_SVC_START_FIRST_TASK

1:
    b 1b

.size fr_port_start_first_task, .-fr_port_start_first_task


.section .text.fr_port_yield,"ax",%progbits
.align 2
.global fr_port_yield
.type fr_port_yield, %function
.thumb_func

fr_port_yield:
    svc #FR_SVC_YIELD
    bx lr

.size fr_port_yield, .-fr_port_yield


.section .text.SVC_Handler,"ax",%progbits
.align 2
.global SVC_Handler
.type SVC_Handler, %function
.thumb_func

SVC_Handler:
    tst lr, #4
    ite eq
    mrseq r0, msp
    mrsne r0, psp

    ldr r1, [r0, #24]
    subs r1, r1, #2
    ldrb r2, [r1]

    cmp r2, #FR_SVC_START_FIRST_TASK
    beq .Lsvc_start_first

    cmp r2, #FR_SVC_YIELD
    beq .Lsvc_yield

    b .Lsvc_invalid


.Lsvc_start_first:
    bl fr_scheduler_current_saved_sp
    b .Lsvc_restore


.Lsvc_yield:
    mrs r0, psp
    stmdb r0!, {r4-r11}

    bl fr_scheduler_yield_from_isr

    b .Lsvc_restore


.Lsvc_restore:
    cbz r0, .Lsvc_invalid

    ldmia r0!, {r4-r11}
    msr psp, r0

    mrs r1, control
    orr r1, r1, #2
    msr control, r1
    isb

    ldr lr, =0xFFFFFFFD
    bx lr


.Lsvc_invalid:
    udf #2

.Lsvc_invalid_loop:
    b .Lsvc_invalid_loop

.size SVC_Handler, .-SVC_Handler


.section .text.fr_port_task_return_trap,"ax",%progbits
.align 2
.global fr_port_task_return_trap
.type fr_port_task_return_trap, %function
.thumb_func

fr_port_task_return_trap:
    udf #1

2:
    b 2b

.size fr_port_task_return_trap, .-fr_port_task_return_trap


.macro FR_FAULT_ENTRY handler, fault_number
.section .text.\handler,"ax",%progbits
.align 2
.global \handler
.type \handler, %function
.thumb_func

\handler:
    mov r1, lr

    tst lr, #4
    ite eq
    mrseq r0, msp
    mrsne r0, psp

    movs r2, #\fault_number

    b fr_fault_handle_c

.size \handler, .-\handler
.endm

FR_FAULT_ENTRY HardFault_Handler, 3
FR_FAULT_ENTRY MemManage_Handler, 4
FR_FAULT_ENTRY BusFault_Handler, 5
FR_FAULT_ENTRY UsageFault_Handler, 6