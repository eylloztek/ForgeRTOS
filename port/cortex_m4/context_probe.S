.syntax unified
.cpu cortex-m4
.thumb

.extern fr_task_yield


.section .text.fr_arch_context_probe_yield,"ax",%progbits
.align 2
.global fr_arch_context_probe_yield
.type fr_arch_context_probe_yield, %function
.thumb_func

fr_arch_context_probe_yield:
    stmdb sp!, {r3-r11, lr}

    str r0, [sp]

    ldmia r0, {r4-r11}

    bl fr_task_yield

    ldr r0, [sp]

    ldr r1, [r0, #0]
    cmp r4, r1
    bne .Lcontext_probe_failed

    ldr r1, [r0, #4]
    cmp r5, r1
    bne .Lcontext_probe_failed

    ldr r1, [r0, #8]
    cmp r6, r1
    bne .Lcontext_probe_failed

    ldr r1, [r0, #12]
    cmp r7, r1
    bne .Lcontext_probe_failed

    ldr r1, [r0, #16]
    cmp r8, r1
    bne .Lcontext_probe_failed

    ldr r1, [r0, #20]
    cmp r9, r1
    bne .Lcontext_probe_failed

    ldr r1, [r0, #24]
    cmp r10, r1
    bne .Lcontext_probe_failed

    ldr r1, [r0, #28]
    cmp r11, r1
    bne .Lcontext_probe_failed

    movs r0, #1
    b .Lcontext_probe_restore


.Lcontext_probe_failed:
    movs r0, #0


.Lcontext_probe_restore:
    ldmia sp!, {r3-r11, lr}
    bx lr

.size fr_arch_context_probe_yield, .-fr_arch_context_probe_yield