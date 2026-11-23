.syntax unified
.cpu cortex-m4
.thumb

.section .isr_vector, "a", %progbits
.balign 512
.word __stack_top
.word Reset_Handler
.rept 5 /* NMI, HardFault, MemManage, BusFault, UsageFault */
.word Default_Handler
.endr
.rept 4
.word 0
.endr
.word Default_Handler /* SVCall */
.word Default_Handler /* DebugMonitor */
.word 0
.word Default_Handler /* PendSV */
.word SysTick_Handler
.rept 16
.word Default_Handler
.endr
.word DMA1_Stream5_IRQHandler
.word DMA1_Stream6_IRQHandler
.rept 13
.word Default_Handler
.endr
.word I2C1_EV_IRQHandler
.word I2C1_ER_IRQHandler
.word I2C2_EV_IRQHandler
.word I2C2_ER_IRQHandler
.rept 2
.word Default_Handler
.endr
.word USART1_IRQHandler
.word USART2_IRQHandler
.rept 19
.word Default_Handler
.endr
.word DMA2_Stream2_IRQHandler
.rept 8
.word Default_Handler
.endr
.word OTG_FS_IRQHandler
.rept 2
.word Default_Handler
.endr
.word DMA2_Stream7_IRQHandler
.rept 14
.word Default_Handler
.endr

.weak OTG_FS_IRQHandler
.thumb_set OTG_FS_IRQHandler, Default_Handler

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
    bl SystemInit
    bl main

.thumb_func
Default_Handler:
    cpsid i
5:
    wfi
    b 5b

.section .note.GNU-stack, "", %progbits
