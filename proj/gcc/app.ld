/* Run through the C preprocessor by SConstruct -> gcc/app.ld.gen, so the
 * addresses always follow ../mem_config.h and ../flash_map.h. */
#include "../mem_config.h"
#include "../flash_map.h"

/* Entry Point */
ENTRY(Reset_Handler)

#if (BUILD_BANK == 0)
#define APP_ADDR                    BANK0_APP_ADDR
#define APP_SIZE                    BANK0_APP_SIZE
#else
#define APP_ADDR                    BANK1_APP_ADDR
#define APP_SIZE                    BANK1_APP_SIZE
#endif

MEMORY
{
NS_APP (r)   : ORIGIN = APP_ADDR, LENGTH = APP_SIZE
RAM_VECTOR_SECTION (rwx) : ORIGIN = S_RAM_VECTOR_ADDR, LENGTH = S_RAM_VECTOR_SIZE
NS_RAM_GLOBAL (rwx) : ORIGIN = NS_RAM_APP_ADDR, LENGTH = NS_RAM_APP_SIZE
EXT_DATA_RAM (rwx) : ORIGIN = EXT_DATA_SRAM_GLOBAL_ADDR, LENGTH = EXT_DATA_SRAM_GLOBAL_SIZE
NS_STACK (rwx): ORIGIN = S_RAM_MAIN_STACK_START_ADDR, LENGTH = S_RAM_MAIN_STACK_SIZE
NS_HEAP(rwx): ORIGIN = NS_HEAP_ADDR, LENGTH = NS_HEAP_SIZE
PSRAM_SECTION(rwx): ORIGIN = 0x8200000, LENGTH = 0x200000
APP_TRACE (r): ORIGIN = 0x18100000, LENGTH = 0x80000
}

SECTIONS
{
    . = APP_ADDR;
    app_ns.bin  APP_ADDR :
    {
        KEEP(* (.app.flash.header))
    }AT > NS_APP

    __app_flash_load_ad__ = APP_ADDR;

    FLASH_HEADER_EXT :
    {
       KEEP(* (.app.flash.header_ext))
    }AT > NS_APP

#if (FEATURE_FLASH_SEC == 1)
    FLASH_GAP_REGION :
    {
        * (.app.flash.gap.text)
        . = ALIGN(0x1000);
        __flash_gap_region_load_ad__ = LOADADDR(FLASH_GAP_REGION);
        __flash_gap_region_exe_ad__ = ADDR(FLASH_GAP_REGION);
    }AT > NS_APP
#endif

    FLASH_TEXT :
    {
        * (.image_entry)
        *(.text*)
        . = ALIGN(4);
        *(.rodata*)
        . = ALIGN(4);
        * (.isr.text)
        * (.app.flash.text)
        * (.app.flash.rodata)
        _init_array = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        _einit_array = .;
    }AT > NS_APP

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } >NS_APP

    __exidx_start = .;
    ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        _sidata = .;
    }AT > NS_APP
    __exidx_end = .;

    RAM_VECTOR_TABLE  :
    {
        __ram_vector_table_start__ = .;
        KEEP(*(.vectors_table))
        __ram_vector_table_end__ = .;
    } > RAM_VECTOR_SECTION  AT > NS_APP

    RAM_VECTOR_TABLE_WRAPPER_NS NS_RAM_APP_ADDR :
    {
        __ram_vector_table_wrapper_ns_start__ = .;
        KEEP(* (.bss.ram_vector_table_wrapper))
        __ram_vector_table_wrapper_ns_end__ = .;
    } > NS_RAM_GLOBAL AT > NS_APP

    __ram_vector_load_ad__ = LOADADDR(RAM_VECTOR_TABLE);
    __ram_vector_table_length__ = __ram_vector_table_end__ - __ram_vector_table_start__;
    __ram_vector_exe_ad__ = ADDR(RAM_VECTOR_TABLE);
    __ram_vector_wrapper_ns_load_ad__ = LOADADDR(RAM_VECTOR_TABLE_WRAPPER_NS);
    __ram_vector_wrapper_ns_length__ = __ram_vector_table_wrapper_ns_end__ - __ram_vector_table_wrapper_ns_start__;

    ER_IRAM_NS :
    {
       . = ALIGN(0x20);
        __enter_iram_ns_ro_start__ = .;
        * (.app.ram.text)
        __enter_iram_ns_ro_end__ = .;
        __enter_iram_ns_rw_start__ = .;
        *(.data*)
        * (.app.ram.data)
        __enter_iram_ns_rw_end__ = .;
    }> NS_RAM_GLOBAL  AT > NS_APP

    ER_IRAM_NS_BSS (NOLOAD):
    {
        . = ALIGN(4);
         __enter_iram_ns_zi_start__ = .;
        *(.bss*)
        . = ALIGN(4);
        *(COMMON)
        . = ALIGN(4);
        __enter_iram_ns_zi_end__ = .;
    }> NS_RAM_GLOBAL AT > NS_APP
    __enter_iram_ns_ro_load_ad__ = LOADADDR(ER_IRAM_NS);
    __enter_iram_ns_ro_exe_ad__ = ADDR(ER_IRAM_NS);
    __enter_iram_ns_ro_length__ = __enter_iram_ns_ro_end__ - __enter_iram_ns_ro_start__;
    __enter_iram_ns_rw_length__ = __enter_iram_ns_rw_end__ - __enter_iram_ns_rw_start__;
    __enter_iram_ns_zi_length__ = __enter_iram_ns_zi_end__ - __enter_iram_ns_zi_start__;
    __enter_iram_ns_rw_load_ad__ = LOADADDR(ER_IRAM_NS) + __enter_iram_ns_ro_length__;

    DATA_SRAM_SECTION EXT_DATA_SRAM_GLOBAL_ADDR :
    {
        __data_sram_section_ro_start__ = .;
        __data_sram_section_ro_end__ = .;
        __data_sram_section_rw_start__ = .;
        *(.app.ext.datasram.data)
        __data_sram_section_rw_end__ = .;
        __data_sram_section_zi_start__ = .;
        __data_sram_section_zi_end__ = .;
    } > EXT_DATA_RAM AT > NS_APP
    __data_sram_section_ro_load_ad__ = LOADADDR(DATA_SRAM_SECTION);
    __data_sram_section_ro_length__ = __data_sram_section_ro_end__ - __data_sram_section_ro_start__;
    __data_sram_section_rw_load_ad__ = LOADADDR(DATA_SRAM_SECTION) + __data_sram_section_ro_length__;
    __data_sram_section_rw_length__ = __data_sram_section_rw_end__ - __data_sram_section_rw_start__;
    __data_sram_section_rw_exe_ad__ = ADDR(DATA_SRAM_SECTION) + __data_sram_section_ro_length__;
    __data_sram_section_zi_length__ = __data_sram_section_zi_end__ - __data_sram_section_zi_start__;

#if (FEATURE_FLASH_SEC == 1)
    __flash_sec_base_addr__ = LOADADDR(FLASH_TEXT);
    __flash_sec_region_size__ = __data_sram_section_rw_load_ad__ + __data_sram_section_rw_length__ - LOADADDR(FLASH_TEXT);
#endif

    .heap (NOLOAD):
    {
        . = ALIGN(4);
        PROVIDE ( end = . );
        _sheap = .;
        . = . + NS_HEAP_SIZE;
        . = ALIGN(4);
        _eheap = .;
    } >NS_HEAP

    .stack (NOLOAD):
    {
        . = ALIGN(4);
        _sstack = .;
        . = . + S_RAM_MAIN_STACK_SIZE;
        . = ALIGN(4);
        _estack = .;
    } > NS_STACK

    PSRAM 0x8200000:
    {
        __psram_section_ro_start__ = .;
        __psram_section_ro_end__ = .;
        __psram_section_rw_start__ = .;
        *(.app.psram.data)
        __psram_section_rw_end__ = .;
        __psram_section_zi_start__ = .;
        __psram_section_zi_end__ = .;
    } > PSRAM_SECTION AT > NS_APP
    __psram_section_ro_load_ad__ = LOADADDR(PSRAM);
    __psram_section_ro_length__ = __psram_section_ro_end__ - __psram_section_ro_start__;
    __psram_section_rw_load_ad__ = LOADADDR(PSRAM) + __psram_section_ro_length__;
    __psram_section_rw_length__ = __psram_section_rw_end__ - __psram_section_rw_start__;
    __psram_section_rw_exe_ad__ = ADDR(PSRAM) + __psram_section_ro_length__;
    __psram_section_zi_length__ = __psram_section_zi_end__ - __psram_section_zi_start__;

    App.trace 0x18100000 :
    {
        *(.TRACE*)
    }>APP_TRACE
}
