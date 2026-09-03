.syntax unified
.cpu cortex-m4
.thumb

.extern main
.extern _estack
.extern _sidata
.extern _sdata
.extern _edata
.extern _sbss
.extern _ebss

.section .isr_vector,"a",%progbits
.align 9
.global g_pfnVectors
.type g_pfnVectors, %object

g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word DebugMon_Handler
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler

.size g_pfnVectors, .-g_pfnVectors

.section .text.Reset_Handler,"ax",%progbits
.align 2
.global Reset_Handler
.type Reset_Handler, %function
.thumb_func

Reset_Handler:
    ldr r0, =0xE000ED08
    ldr r1, =g_pfnVectors
    str r1, [r0]
    dsb
    isb

    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata

1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b

2:
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0

3:
    cmp r0, r1
    bcs 4f
    str r2, [r0], #4
    b 3b

4:
    bl main

5:
    b 5b

.size Reset_Handler, .-Reset_Handler

.section .text.Default_Handler,"ax",%progbits
.align 2
.global Default_Handler
.type Default_Handler, %function
.thumb_func

Default_Handler:
    b Default_Handler

.size Default_Handler, .-Default_Handler

.macro WEAK_DEFAULT handler
    .weak \handler
    .thumb_set \handler, Default_Handler
.endm

WEAK_DEFAULT NMI_Handler
WEAK_DEFAULT HardFault_Handler
WEAK_DEFAULT MemManage_Handler
WEAK_DEFAULT BusFault_Handler
WEAK_DEFAULT UsageFault_Handler
WEAK_DEFAULT SVC_Handler
WEAK_DEFAULT DebugMon_Handler
WEAK_DEFAULT PendSV_Handler
WEAK_DEFAULT SysTick_Handler