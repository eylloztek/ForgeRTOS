.syntax unified
.cpu cortex-m4
.thumb

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


.section .text.SVC_Handler,"ax",%progbits
.align 2
.global SVC_Handler
.type SVC_Handler, %function
.thumb_func

SVC_Handler:
    bx lr

.size SVC_Handler, .-SVC_Handler

.extern fr_fault_handle_c

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