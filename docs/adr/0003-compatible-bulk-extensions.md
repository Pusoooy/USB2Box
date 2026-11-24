# ADR 0003 — Compatible bulk extensions

Status: accepted for the M12 follow-up. Requirements U08, I06, P09, P11, P14, X09 and X12.

The firmware keeps USB protocol 1.0 framing and the existing endpoints. SYS 12 reports an optional u32 bitmask. An older device responds with unknown opcode, which the SDK interprets as zero capabilities. Existing synchronous UART/ADC reads and I²C command 5 keep their original response layouts.

- Bit 0 enables round-robin arbitration between ADC and both UART streams on 0x82. Each stream has separate sequence and credit. A nonzero 32-bit tag allows a host to discard complete frames left by a previous subscription. A frame partly handed to the USB stack is completed when the pipe is still configured. Physical bus reset clears partial state.
- Bit 1 enables an eight-event I²C FIFO per controller, independent of the master job queue. Drop newest on overflow, retaining earlier events and reporting counters. TX events report bytes supplied to the controller, whose transmission and ACK timing need HIL.
- Bit 2 enables period-boundary updates of PWM frequency and duty for an active channel. The driver writes PSC, ARR and CCR together while update events are disabled and restores natural rollover. Active output retains polarity, pulse count and original stop deadline. Polarity changes require an idle channel because the timer polarity bit has no preload.
- The Host mock runs the same dispatcher and stream mux with endpoint-tagged pipe records. This transport envelope is local to the simulator.

The existing GPIO, timer, and DMA assignments remain the same. The release link step measured 1,324 bytes of added static RAM versus the M12 baseline, mostly for I²C queues. USB timing, I²C IRQ timing, and PWM waveforms require physical tests.
