/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef MENU_CONFIG_H__
#define MENU_CONFIG_H__
// <<< Use Configuration Wizard in Context Menu >>>\n
/* Automatically generated file; DO NOT EDIT. */


// <h> Soc Device Config
#define CONFIG_REALTEK_8773E_DEVICE
// </h>

// <q> RTK_LCD_C05300_390_450_QSPI_ENABLED - LCD_C05300_390_450_QSPI peripheral driver
//==========================================================
#define CONFIG_REALTEK_LCD_C05300_390_450_QSPI 0

// <q> RTK_LCD_SH8601Z_410_502_QSPI_ENABLED - LCD_SH8601Z_410_502_QSPI peripheral driver
//==========================================================
#define CONFIG_REALTEK_LCD_SH8601Z_410_502_QSPI 1

// <q> RTK_LCD_ST7265_800480_RGB_ENABLED - LCD_ST7265_800480_RGB peripheral driver
//==========================================================
#define CONFIG_REALTEK_LCD_ST7265_800480_RGB 0

// <q> RTK_NV3041A_480_272_QSPI_ENABLED - NV3041A_480_272_QSPI peripheral driver
//==========================================================
#define CONFIG_REALTEK_NV3041A_480_272_QSPI 0

// <q> RTK_SH8601A_454454_QSPI_ENABLED - SH8601A_454454_QSPI peripheral driver
//==========================================================
#define CONFIG_REALTEK_SH8601A_454454_QSPI 0

// <q> RTK_ST7701S_480480_RGB_ENABLED - ST7701S_480480_RGB peripheral driver
//==========================================================
#define CONFIG_REALTEK_ST7701S_480480_RGB 0

// <q> RTK_TOUCH_CHSC6417_ENABLED - TOUCH_CHSC6417 peripheral driver
//==========================================================
#define CONFIG_REALTEK_TOUCH_CHSC6417 1

// <q> RTK_TOUCH_GT911_ENABLED - TOUCH_GT911 peripheral driver
//==========================================================
#define CONFIG_REALTEK_TOUCH_GT911 0

// <q> RTK_TOUCH_LW_ENABLED - TOUCH_LW peripheral driver
//==========================================================
#define CONFIG_REALTEK_TOUCH_LW 0

// <q> RTK_KEY_BUTTON_8773E_ENABLED - KEY_BUTTON_8773E peripheral driver
//==========================================================
#define CONFIG_REALTEK_KEY_BUTTON_8773E 1

// <c> HoneyGUI Enable Build Lib
#define CONFIG_REALTEK_HONEYGUI_DEV_LIB  1
// </c>

// <c> HoneyGUI Enable Build Source Code
//#define CONFIG_REALTEK_HONEYGUI_DEV_SRC
// </c>


// <h> HoneyGUI Demo Select
// <c> RTK GUI Base Demo
// #define CONFIG_REALTEK_BUILD_EXAMPLE_IMAGE_WIDGET
// </c>

// <c> RTK GUI Base Demo
// #define CONFIG_REALTEK_BUILD_EXAMPLE_TEXT_WIDGET
// </c>

// <c> RTK GUI Base Demo
// #define CONFIG_REALTEK_BUILD_EXAMPLE_GRAY_WIDGET
// </c>

// <c> RTK GUI 3D Demo
// #define CONFIG_REALTEK_BUILD_REAL_DOG_3D
// </c>

// <c> RTK GUI 3D Demo
// #define CONFIG_REALTEK_BUILD_REAL_EARTH_3D
// </c>

// <c> RTK GUI View Demo
// #define CONFIG_REALTEK_BUILD_REAL_VIEW
// </c>

// <c> RTK GUI List Demo
// #define CONFIG_REALTEK_BUILD_EXAMPLE_LIST_WIDGET
// </c>

// <c> RTK GUI Menu Cellular Demo
// #define CONFIG_REALTEK_BUILD_EXAMPLE_MENU_CELLULAR_WIDGET
// </c>

// <c> RTK GUI Canvas Demo
// #define CONFIG_REALTEK_BUILD_REAL_CANCAS
// </c>

// <c> RTK GUI Menu Cellular Demo
// #define CONFIG_REALTEK_BUILD_EXAMPLE_QBCODE_WIDGET
// </c>

// <c> RTK GUI Paint Engine Demo
// #define CONFIG_REALTEK_BUILD_REAL_PAINTENGINE
// </c>

// <c> RTK GUI NanoVG Demo
// #define CONFIG_REALTEK_BUILD_REAL_NANOVG
// </c>

// <c> RTK GUI Box2D Demo
// #define CONFIG_REALTEK_BUILD_REAL_BOX2D
// </c>

// <c> RTK GUI LiteGFX Demo
// #define CONFIG_REALTEK_BUILD_REAL_LITEGFX_DEMO
// </c>

// <c> RTK GUI Demo 368 448
// #define CONFIG_REALTEK_BUILD_GUI_448_368_DEMO
// </c>

// <c> RTK GUI Demo 454 454
// #define CONFIG_REALTEK_BUILD_GUI_454_454_DEMO
// </c>

// <c> RTK GUI Demo 410 502
// #define CONFIG_REALTEK_BUILD_GUI_410_502_DEMO
// </c>

// <c> RTK GUI TEST
// #define CONFIG_REALTEK_BUILD_TEST
// </c>

// <c> RTK GUI AUTO TEST
// #define CONFIG_REALTEK_BUILD_HONEYGUI_AUTO_TEST
// </c>

// <c> RTK GUI Demo 280 456
// #define CONFIG_REALTEK_BUILD_GUI_280_456_DEMO
// </c>

// <c> RTK GUI Demo 240 240
// #define CONFIG_REALTEK_BUILD_GUI_240_240_DEMO
// </c>

// <c> RTK GUI Demo 240 320
// #define CONFIG_REALTEK_BUILD_GUI_240_320_DEMO
// </c>

// <c> RTK GUI Demo 320 384
// #define CONFIG_REALTEK_BUILD_GUI_320_384_DEMO
// </c>

// <c> RTK GUI Demo 320 385
// #define CONFIG_REALTEK_BUILD_GUI_320_385_DEMO
// </c>

// <c> RTK GUI Demo 800 480
// #define CONFIG_REALTEK_BUILD_GUI_800_480_DEMO
// </c>

// </h>

// <h> HoneyGUI Config Function

// <c> Enable RTK GUI ROMFS
#define CONFIG_REALTEK_ROMFS
// </c>

#if (CONFIG_REALTEK_BUILD_HONEYGUI_SRC == 1)
#define CONFIG_REALTEK_HONEYGUI

// <c> RTK GUI Font Enable STB
#define CONFIG_REALTEK_BUILD_GUI_FONT_STB
// </c>

// <c> RTK GUI Font Enable FREETYPE
// #define CONFIG_REALTEK_BUILD_GUI_FONT_FREETYPE
// </c>

// <c> RTK GUI Font Enable RTK MEM
#define CONFIG_REALTEK_BUILD_GUI_FONT_RTK_MEM
// </c>

// <c> RTK GUI Font Enable TTF SVG
// #define CONFIG_REALTEK_BUILD_GUI_FONT_TTF_SVG
// </c>

// <c> RTK GUI Enable VGLITE GPU
// #define CONFIG_REALTEK_BUILD_VG_LITE
// </c>



// <c> RTK GUI Enable SasA
// #define CONFIG_REALTEK_BUILD_SCRIPT_AS_A_APP
// </c>

// <c> RTK GUI Enable cJSON
#define CONFIG_REALTEK_BUILD_CJSON
// </c>
// <c> RTK GUI Enable web
// #define CONFIG_REALTEK_BUILD_WEB
// </c>
// <c> RTK GUI Enable KeyBoard And Pinyin
#define CONFIG_REALTEK_BUILD_PINYIN
// </c>

// <c> RTK GUI Enable u8g2
// #define CONFIG_REALTEK_BUILD_U8G2
// </c>

// <c> RTK GUI Enable Painter Engine only enable for RTL8772F and simulation
// #define CONFIG_REALTEK_BUILD_PAINTER_ENGINE
// </c>

// <c> RTK GUI Enable LiteGFX
// #define CONFIG_REALTEK_BUILD_LITE_GFX
// </c>

// <c> RTK GUI Enable LetterShell
//#define CONFIG_REALTEK_BUILD_LETTER_SHELL
// </c>

// <c> RTK GUI Enable Monkey Test Log
// #define CONFIG_REALTEK_BUILD_MONKEY_TEST
// </c>

// <c> RTK GUI BOX2D
#define CONFIG_REALTEK_BUILD_GUI_BOX2D
// </c>

// <c> CONFIG_REALTEK_BUILD_LITE3D
#define CONFIG_REALTEK_BUILD_LITE3D
// </c>

// <c> RTK GUI Enable SasA
// #define CONFIG_REALTEK_BUILD_WATCHFACE_UPDATE
// </c>

// <c> h.264 decoder
#define CONFIG_REALTEK_H264_DECODER     1
#if (CONFIG_REALTEK_H264_DECODER == 1)
#define CONFIG_REALTEK_H264BSD
#endif
// </c>
#endif
// </h>

// <<< end of configuration section >>>
#endif//MENU_CONFIG_H__
