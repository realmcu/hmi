#include "../mem_config.h"
#include "flash_map.h"
#include "config.h"
/* Entry Point */
ENTRY(Reset_Handler)

#if (CONFIG_REALTEK_COMPILE_BANK0 == 1)
#define APP_FLASH_ADDR                    BANK0_APP_ADDR
#define APP_FLASH_SIZE                    BANK0_APP_SIZE
#else
#define APP_FLASH_ADDR                    BANK1_APP_ADDR
#define APP_FLASH_SIZE                    BANK1_APP_SIZE
#endif

MEMORY
{

APP_FLASH (r)   : ORIGIN = APP_FLASH_ADDR, LENGTH = APP_FLASH_SIZE
APP_GLOBAL_DATA (rw) : ORIGIN = APP_GLOBAL_ADDR, LENGTH = APP_GLOBAL_SIZE
SHARE_RAM_DATA(rw) : ORIGIN = DSP_SHM_GLOBAL_ADDR, LENGTH = DSP_SHM_GLOBAL_SIZE
RAM_TEXT(r) : ORIGIN = APP_RAM_TEXT_ADDR, LENGTH = APP_RAM_TEXT_SIZE
APP_TRACE (r): ORIGIN = 0x08800000, LENGTH = 0x400000

}
  /** (.start_entry)*/
/* Define output sections */
SECTIONS
{
    . = APP_FLASH_ADDR;
    dashboard.bin APP_FLASH_ADDR :
    {
        __image_base__ = .;
        KEEP(* (.flash.header))
    }AT > APP_FLASH

    FLASH_TEXT :
    {
      /* build startup_rtl87x3e_gcc.o(RESET)*/
      *.s.obj(RESET)

      KEEP(* (+RO))
      * (.isr.text)
      *(.text*)
      . = ALIGN(4);
      *(.rodata*)
      . = ALIGN(4);
    __flash_start_load_addr__ = LOADADDR(FLASH_TEXT);
    __flash_start_exe_addr__ = ADDR(FLASH_TEXT);
    }AT > APP_FLASH

    RAM_VECTOR_TABLE 0x280000:
    {
        __ram_vector_table_start__ = .;
        KEEP(*(VECTOR))
        __ram_vector_table_end__ = .;
    }AT > APP_FLASH

    __ram_vector_load_ad__ = LOADADDR(RAM_VECTOR_TABLE);
    __ram_vector_table_length__ = __ram_vector_table_end__ - __ram_vector_table_start__;

    RAM_GLOBAL_DATA_SECTION :
    {
        . = ALIGN(4);
        __ram_dataon_rw_start__ = .;
        *(.data*)
        . = ALIGN(4);
        __ram_dataon_rw_end__ = .;

        . = ALIGN(4);
        __ram_dataon_zi_start__ = .;
        *(.bss*)
        . = ALIGN(4);
        __ram_dataon_zi_end__ = .;
        PROVIDE(end = .);
    }> APP_GLOBAL_DATA AT > APP_FLASH
    __ram_rw_load_addr__ = LOADADDR(RAM_GLOBAL_DATA_SECTION);
    __ram_rw_dst_addr__ = ADDR(RAM_GLOBAL_DATA_SECTION);
    __ram_dataon_rw_length__ = __ram_dataon_rw_end__ - __ram_dataon_rw_start__;
    __ram_dataon_zi_length__ = __ram_dataon_zi_end__ - __ram_dataon_zi_start__;
    __ram_global_data_length__ = __ram_dataon_zi_end__ - __ram_dataon_rw_start__;

    SHARE_RAM_DATA_SECTION :
    {
        __share_ram_data_start_addr__ = .;
        * (.shm.data)
        __share_ram_data_end_addr__ = .;
    }> SHARE_RAM_DATA AT > APP_FLASH
    __share_ram_code_length__ = __share_ram_data_end_addr__ - __share_ram_data_start_addr__;
    __share_ram_load_addr__ = LOADADDR(SHARE_RAM_DATA_SECTION);
    __share_ram_dst_addr__ = ADDR(SHARE_RAM_DATA_SECTION);

    RAM_TEXT_SECTION :
    {
        __ram_code_start_addr__ = .;
        * (.ram_text)
        __ram_code_end_addr__ = .;
    }>RAM_TEXT AT > APP_FLASH
    __ram_code_load_addr__ = LOADADDR(RAM_TEXT_SECTION);
    __ram_code_exe_addr__ = ADDR(RAM_TEXT_SECTION);
    __ram_code_length__ = __ram_code_end_addr__ - __ram_code_start_addr__;

    __image_size__ = __ram_code_load_addr__ + __ram_code_length__ - __image_base__;

    app.trace 0x08800000:
    {
        *(.TRACE*)
    }>APP_TRACE

}
