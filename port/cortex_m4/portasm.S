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