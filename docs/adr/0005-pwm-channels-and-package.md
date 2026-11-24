# ADR 0005 — Shared PWM timebases and Windows candidate

Status accepted. This implements P03, P05, P08–P14 and the G19 development versioning requirement.

The STM32F401 LQFP64 pin and AF XML used in M0 lists TIM5 CH2 on PA1 and TIM3 CH2 on PC7, both AF2. Each is acquired separately through the existing resource manager. PA1 conflicts with ADC1_IN1, and PC7 conflicts with the USART6 RX candidate. TIM5 and TIM3 retain their original timebases and CH1 mappings. TIM4 capture remains on PB6. There is no additional timer or DMA allocation. Release static RAM increases by 160 B over ADR 0004. PCB routing and electrical behavior still require inspection and measurement. [Pinned ST data](../m0-resource-audit.md)

Each timer has two output states and one period. A channel can start, stop or exhaust its budget while the sibling runs. Shared PSC, ARR and active CCR values transfer together on the update event. A frequency update re-quantizes both duties and preserves each pulse budget and deadline. Active polarity changes return a value error because the timers cannot preload that polarity change alongside the period. A stopped channel is forced low with its configured polarity.

Version 1.0 devices advertise the additive PWM channel operations through SYS 12 bit 6. Operations 1–5 still address CH1. SDK version 0.1.1 reflects the compatible additions and fixes. The Windows x64 candidate was staged and verified from Linux. Native Windows execution and V1 release acceptance remain pending.
