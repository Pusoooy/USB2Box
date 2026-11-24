# ADR 0006 — SPI transfer paths, bounded storage and TIM3 ADC trigger

Status accepted. Requirements S11, A07, F07, F09, F10 and F21.

SPI segments up to 8 B use a register-driven SPI1 interrupt. Each interrupt processes at most one received word and feeds the next word. The main loop completes the job after BSY clears. The short path uses the configured SPI clock, word width and CS timing. It can introduce an inter-word pause, and physical timing remains a HIL item.

Longer segments claim the original DMA2 streams 0 and 3 as a pair. An occupied stream gives the host its original resource ID, while shorts can still use the interrupt path. END or ABORT releases the streams when the transaction has stopped. The TX source is the aligned bytes in the queued job, and RX has a separate lease until the result is committed or recovered on abort.

ADC and SPI allocate from 33 halfword-aligned blocks of 256 B each. The 8,192 B ADC capture and 256 B SPI RX lease can coexist in either allocation order. Short and long SPI transfers share the same RX lease. Session exit stops both services before releasing their storage. The memory pool and resource allocator run in the main loop.

RM0368 Rev 6 Table 43 and the STM32F401 HAL expose TIM3 TRGO as an ADC1 regular trigger. An ADC capture can attach to the already-active group 1 timebase after arming DMA. ADC checks its conversion and aggregate sample-rate limits against the timer's applied PSC and ARR. While attached, TIM3 starts and frequency/duty updates return busy. A user stop, pulse budget, deadline or session termination may still stop the timer. Captures end with source-loss state if the last channel stops first. The ADC uses its existing DMA stream and reserves TIM2 so the same configuration can run another capture with its usual timer. All pin and DMA routes retain the previous mapping. [Resource review](../m0-resource-audit.md)

Debug enables HAL parameter checks and project invariants. The failure handler records file and line for SWD, stops the application and disconnects USB before halting. Release keeps bounded runtime checks for corrupt result sizes, and records collected SPI/I²C I/O failures and timeouts in the device ring without changing the job reply. Wire extensions are additive. Native Windows execution, trigger phase and SPI waveforms remain physical acceptance items.
