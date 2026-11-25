
#ifndef __MENU_CONFIG_H__
#define __MENU_CONFIG_H__
// <<< Use Configuration Wizard in Context Menu >>>\n
/* Menu Configuration - RTL87x2G */

/* soc series: selects the 8762G directories in subsys/display/device */
#define CONFIG_SOC_SERIES_RTL87X2G 1


/*============================================================================*
 * SDK libraries / peripheral drivers.
 *
 * These are only read by the scons build (GetDepend() in bin/, bsp/ and
 * subsys/bluetooth SConscripts). They mirror what the MDK project links:
 * lib group (lowerstack / ROM_CMSE_Lib.o / ROM_NS / rtl87x2g_sdk / gap_utils)
 * and peripheral group (gdma / gpio / i2c / spi / tim / io lib / io_dlps).
 *============================================================================*/
#define CONFIG_REALTEK_LOWERSTACK_LIB   1
#define CONFIG_REALTEK_ROM_CMSE_LIB     1
#define CONFIG_REALTEK_ROM_NS_LIB       1
#define CONFIG_REALTEK_ROM_LIB          0
#define CONFIG_REALTEK_SDK_LIB          1
#define CONFIG_REALTEK_IO_LIB           1
#define CONFIG_REALTEK_GAP_LIB          1

#define CONFIG_REALTEK_GDMA             1
#define CONFIG_REALTEK_GPIO             1
#define CONFIG_REALTEK_I2C              1
#define CONFIG_REALTEK_SPI              1
#define CONFIG_REALTEK_HWTIMER          1
#define CONFIG_REALTEK_IO_DLPS          1
/* rtl_rtc.c is not built, so drv_rtc.c (CONFIG_REALTEK_BSP_ONCHIP_RTC) must stay off */
#define CONFIG_REALTEK_RTC              0


// <h> Hardware Drivers Config
// <q> RTK_GPIO_ENABLED - GPIO peripheral driver
//==========================================================
#define CONFIG_REALTEK_BSP_GPIO 1


// <q> RTK_DLPS_ENABLED - DLPS config
//==========================================================
#define CONFIG_REALTEK_BSP_DLPS 1

// <q> RTK_Flash_ENABLED - flash config
//==========================================================
#define CONFIG_REALTEK_BSP_FLASH 1

// <q> RTK_UART_ENABLED - UART peripheral driver
//==========================================================
#define CONFIG_REALTEK_BSP_UART 0

// <q> RTK_SPI_ENABLED - SPI peripheral driver
//==========================================================
#define CONFIG_REALTEK_BSP_SPI 0


// <q> RTK_I2C_ENABLED - I2C peripheral driver
//==========================================================
#define CONFIG_REALTEK_BSP_I2C 1

// <q> RTK_RTC_ENABLED - RTC peripheral driver
//==========================================================
/* 0: drv_rtc.c needs rtl_rtc.c (CONFIG_REALTEK_RTC), which this app does not
 * build - same as the MDK project, where drv_rtc.c was removed. */
#define CONFIG_REALTEK_BSP_ONCHIP_RTC 0


// <q> RTK_ADC_ENABLED - ADC peripheral driver
//==========================================================
#define RT_USING_ADC 0



// <e> RTK_LCD_ENABLED - LCD peripheral driver
#define CONFIG_REALTEK_LCD 1
#if (CONFIG_REALTEK_LCD == 1)

// <o> LCD_DRIVER (subsys/display/device/general/lcd/8762G)
// <0=> CONFIG_REALTEK_NT35510_480800_DBIB
// <1=> CONFIG_REALTEK_ST7796_320320_DBIB
// <2=> CONFIG_REALTEK_INCA3311_280456_QSPI   (QSPI -> DBIC)
// <8=> CONFIG_REALTEK_ST7701S_480480_RGB    (RGB  -> EDPI)
#define CONFIG_REALTEK_LCD_CONFIG_DRIVER 8

#if (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 0)
#define CONFIG_REALTEK_NT35510_480800_DBIB
#elif (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 1)
#define CONFIG_REALTEK_ST7796_320320_DBIB
#elif (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 2)
#define CONFIG_REALTEK_INCA3311_280456_QSPI
#elif (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 8)
#define CONFIG_REALTEK_ST7701S_480480_RGB
#endif

/* LCD_INTERFACE is derived from the driver (DBIB 0 / DBIC 1 / EDPI 2) so the
 * two can never get out of sync */
#if (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 0) || (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 1)
#define CONFIG_REALTEK_LCD_CONFIG_INTERFACE 0
#elif (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 2)
#define CONFIG_REALTEK_LCD_CONFIG_INTERFACE 1
#elif (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 8)
#define CONFIG_REALTEK_LCD_CONFIG_INTERFACE 2
#endif

#if (CONFIG_REALTEK_LCD_CONFIG_INTERFACE == 0)
#define CONFIG_REALTEK_LCDC_DBIB
#elif (CONFIG_REALTEK_LCD_CONFIG_INTERFACE == 1)
#define CONFIG_REALTEK_LCDC_DBIC
#elif (CONFIG_REALTEK_LCD_CONFIG_INTERFACE == 2)
#define CONFIG_REALTEK_LCDC_EDPI
#endif

/* the panel is driven through the LCDC, so the common LCDC code is required */
#define CONFIG_REALTEK_LCDC

/* the ramless QSPI path is only used by the QSPI panel (ICNA3311) */
#if (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 2)
#define CONFIG_REALTEK_RAMLESS_QSPI 1
#endif

/* gates subsys/display/device/SConscript -> general/lcd/8762G */
#define CONFIG_REALTEK_8762G_DEVICE_LCD
#endif
// </e>

// <e> RTK_TOUCH_ENABLED - TOUCH peripheral driver
#define CONFIG_REALTEK_BSP_TOUCH 1
#if (CONFIG_REALTEK_BSP_TOUCH == 1)

// <o> TOUCH_DRIVER (subsys/display/device/general/touch/8762G)
// <0=> 8762G ZT2717
// <1=> 8762G GT911
// <2=> 8762G CST836U
#define CONFIG_REALTEK_TOUCH_CONFIG_DRIVER 1

#if (CONFIG_REALTEK_TOUCH_CONFIG_DRIVER == 0)
#define CONFIG_REALTEK_BSP_TOUCH_ZT2717_8762G
#elif (CONFIG_REALTEK_TOUCH_CONFIG_DRIVER == 1)
#define CONFIG_REALTEK_BSP_TOUCH_GT911_8762G
#elif (CONFIG_REALTEK_TOUCH_CONFIG_DRIVER == 2)
#define CONFIG_REALTEK_BSP_TOUCH_CST836U_8762G
#endif

/* gates subsys/display/device/SConscript -> general/touch/8762G */
#define CONFIG_REALTEK_8762G_DEVICE_TOUCH
#endif
// </e>

// </h>


// <e> Console Enabled
#define USING_CONSOLE 0
#if (USING_CONSOLE == 1)
// <q> LETTER_SHELL  - Enable Letter Shell
#define USING_LETTER_SHELL 0
#endif
// </e>


// <c> RTK External libc
#define CONFIG_REALTEK_USING_EXTERNAL_LIBC
// </c>


// <h> GUI Config

// <q> HoneyGUI Enable RTK Real GUI
#define CONFIG_REALTEK_BUILD_GUI     0

// <e> Enable LVGL
#define CONFIG_REALTEK_BUILD_LVGL_V9     1
/* subsys/lvgl/SConscript is gated on this name */
#define CONFIG_REALTEK_BUILD_LVGL        1
/* build subsys/lvgl/demos (benchmark + widgets; the other demos compile to
 * nothing because their LV_USE_DEMO_xxx is 0 in lv_conf.h) */
#define CONFIG_LV_BUILD_DEMOS            1
// </e>

// </h>


// <<< end of configuration section >>>
#endif//__MENU_CONFIG_H__
