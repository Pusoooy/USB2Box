ENTRY(Reset_Handler)

MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 256K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K
}

__stack_top = ORIGIN(RAM) + LENGTH(RAM);
__stack_limit = __stack_top - 8K;

SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > FLASH
    .data : {
        . = ALIGN(4);
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT > FLASH
    __data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM
    ASSERT(__bss_end <= __stack_limit, "SRAM overflow into reserved stack")
}
