
#ifndef __MENU_CONFIG_H__
#define __MENU_CONFIG_H__
// <<< Use Configuration Wizard in Context Menu >>>\n
/* Automatically generated file; DO NOT EDIT. */
/* Menu Configuration */

// <h> DEVICES
// <o> DEVICE_SELECT  -  0-2


// <0=> 8762D
// <1=> 8762G
// <2=> 8772F
#define DEVICE_VER 0
// </h>

#if DEVICE_VER == 0
#define CONFIG_REALTEK_8762D_DEVICE
#elif DEVICE_VER == 1
#define CONFIG_REALTEK_8762G_DEVICE
#elif DEVICE_VER ==2
#define CONFIG_REALTEK_8762F_DEVICE
#endif



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
#define CONFIG_REALTEK_BSP_ONCHIP_RTC 1


// <q> RTK_ADC_ENABLED - ADC peripheral driver
//==========================================================
#define RT_USING_ADC 0



// <e> RTK_LCD_ENABLED - LCD peripheral driver
#define CONFIG_REALTEK_BSP_LCD 1
#if (CONFIG_REALTEK_BSP_LCD == 1)

// <o> SELECT_LCD_DRIVER

// <0=> NT35510
// <1=> ST7796
// <2=> ICNA3311
// <3=> ST7796_8762D
// <7=> ST7701S

#define CONFIG_REALTEK_LCD_CONFIG_DRIVER 0

#if (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 0)
#define CONFIG_REALTEK_ST77916G5_320_QSPI
#elif (CONFIG_REALTEK_LCD_CONFIG_DRIVER == 1)
#define CONFIG_REALTEK_ST7789_240_SPI
#endif
// </e>

// <e> RTK_TOUCH_ENABLED - TOUCH peripheral driver
#define CONFIG_REALTEK_BSP_TOUCH 1

// <o> SELECT_TOUCH_DRIVER

// <0=> BSP_TOUCH_CST816D

#define CONFIG_REALTEK_TOUCH_CONFIG_DRIVER 0

#if (CONFIG_REALTEK_TOUCH_CONFIG_DRIVER == 0)
#define CONFIG_REALTEK_BSP_TOUCH_CST816D
#endif
#endif
// </e>

// </h>


// <e> BLE
#define CONFIG_REALTEK_BLE_TASK 0
#if (CONFIG_REALTEK_BLE_TASK == 1)

// <e> BLE SDK Select
#define USING_BLE_SDK 0
#if (USING_BLE_SDK == 1)
// <o> BLE SDK

// <0=> TUYA SDK
// <1=> XIAOMI SDK

#define BLE_SDK_CONFIG 0
#if (BLE_SDK_CONFIG == 0)
#define TUYA_BLE_SDK
#ifdef TUYA_BLE_SDK
// <c> TUYA_MBEDTLS
//#define TUYA_MBEDTLS
// </c>
#endif
#endif
#endif
// </e>
// <e> BLE_SERVICE_ENABLED
#define USING_BLE_SERVICE 1
#endif
#if (USING_BLE_SERVICE == 1)
// <q> BAS_SERVICE
#define BAS_SERVICE 1
#endif
#if (USING_BLE_SERVICE == 1)
// <q> IAS_SERVICE
#define IAS_SERVICE 0
#endif
#if (USING_BLE_SERVICE == 1)
// <q> SIMPLE_BLE_SERVICE
#define SIMPLE_BLE_SERVICE 1
#endif
#if (USING_BLE_SERVICE == 1)
// <q> WRISTBAND_PRIVATE_SERVICE
#define RTK_BLE_WRISTBAND 0
#endif
#if (USING_BLE_SERVICE == 1)
// <q> OTA_SERVICE
#define CONFIG_RTK_OTA_SERVICE 1
// <q> DFU_SERVICE
#define CONFIG_REALTEK_DFU_SERVICE 1
#endif
#if (USING_BLE_SERVICE == 1)
// <q> OTA_SERVICE
#define CONFIG_REALTEK_OTA_SERVICE 1
#endif
// </e>
// </e>

// <e> BR
#if defined(USING_8772F_DEVICE)
#define RTK_BR_TASK  0
#if (RTK_BR_TASK == 1)
// <e> BR_PROFILE_ENABLED
#define USING_BR_PROFILE 1
#endif
#if (USING_BR_PROFILE == 1)
// <c> SDP
#define BR_PROFILE_SDP
// </c>
#endif
#if (USING_BR_PROFILE == 1)
// <c> SPP
#define BR_PROFILE_SPP
// </c>
#endif
#if (USING_BR_PROFILE == 1)
// <c> HFP
//#define BR_PROFILE_HFP
// </c>
#endif
#if (USING_BR_PROFILE == 1)
// <c> A2DP
#define BR_PROFILE_A2DP
// </c>
#endif
#if (USING_BR_PROFILE == 1)
// <c> AVRCP
#define BR_PROFILE_AVRCP
// </c>
#endif
#if (USING_BR_PROFILE == 1)
// <c> PBAP
#define BR_PROFILE_PBAP
// </c>
#endif
// </e>
#endif
// </e>





// <e> Console Enabled
#define USING_CONSOLE 0
#if (USING_CONSOLE == 1)
// <q> LETTER_SHELL  - Enable Letter Shell
#define USING_LETTER_SHELL 0
#endif

// </e>

// <c> Watch Clock Enabled
#define CONFIG_REALTEK_MODULE_WATCH_CLOCK 0
// </c>


// <c> RTK DataBase
// #define CONFIG_REALTEK_MODULE_DATABASE
// </c>

// <c> RTK External libc
#define CONFIG_REALTEK_USING_EXTERNAL_LIBC
// </c>

// <h> HoneyGUI Framework Config


// <e> HoneyGUI Enable RTK Real GUI
#define CONFIG_REALTEK_BUILD_GUI     0

#if (CONFIG_REALTEK_BUILD_GUI == 1)

// <c> RTK GUI Demo
//#define BUILD_USING_RTK_GUI_DEMO
// </c>

// <c> RTK GUI Use OS Heap
#define CONFIG_REALTEK_BUILD_GUI_OS_HEAP
// </c>

// <c> RTK GUI Font Enable STB
//#define CONFIG_REALTEK_BUILD_GUI_FONT_STB
// </c>

// <c> RTK GUI Font Enable FREETYPE
//#define CONFIG_REALTEK_BUILD_GUI_FONT_FREETYPE
// </c>

// <c> RTK GUI Font Enable RTK MEM
#define CONFIG_REALTEK_BUILD_GUI_FONT_RTK_MEM
// </c>

// <c> RTK GUI Font Enable TTF SVG
//#define CONFIG_REALTEK_BUILD_GUI_FONT_TTF_SVG
// </c>

// <c> RTK GUI Enable VGLITE GPU
//#define CONFIG_REALTEK_BUILD_VG_LITE
// </c>

// <c> RTK GUI Enable PPE1.0
//#define CONFIG_REALTEK_BUILD_PPE
// </c>


// <c> RTK GUI Enable SasA
//#define CONFIG_REALTEK_BUILD_SCRIPT_AS_A_APP
// </c>


#endif

// </e>

// <e> HoneyGUI Enable LVGL
#define CONFIG_REALTEK_BUILD_LVGL_V8     0
#define CONFIG_REALTEK_BUILD_LVGL_V9     1

#if (CONFIG_REALTEK_BUILD_LVGL_V8 == 1) || (CONFIG_REALTEK_BUILD_LVGL_V9 == 1)

// <c> HoneyGUI Enalbe LVGL APP
// #define CONFIG_REALTEK_BUILD_LVGL_DEMO_APP
// </c>

// <c> HoneyGUI Enable LVGL EXAMPLES
#define CONFIG_REALTEK_BUILD_LVGL_EXAMPLES
// </c>

// <c> HoneyGUI Enable LVGL RLOTTIE
// #define CONFIG_REALTEK_BUILD_LVGL_RLOTTIE
// </c>

#endif

// </e>


// </h>


// <<< end of configuration section >>>
#endif//__MENU_CONFIG_H__
