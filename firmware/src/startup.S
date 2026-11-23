.syntax unified
.cpu cortex-m4
.thumb

.section .isr_vector, "a", %progbits
.word __stack_top
.word Reset_Handler
.rept 100
.word Default_Handler
.endr

.section .text.Reset_Handler, "ax", %progbits
.thumb_func
.global Reset_Handler
Reset_Handler:
    ldr r0, =__data_load
    ldr r1, =__data_start
    ldr r2, =__data_end
1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:
    ldr r1, =__bss_start
    ldr r2, =__bss_end
    movs r3, #0
3:
    cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b
4:
    bl main

.thumb_func
Default_Handler:
    cpsid i
5:
    wfi
    b 5b

.section .note.GNU-stack, "", %progbits
