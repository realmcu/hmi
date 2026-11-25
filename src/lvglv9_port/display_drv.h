/**
 * @file display_drv.h
 * @brief  Pulls in the LCD and touch panel driver selected in menu_config.h.
 *
 * The panel drivers live in subsys/display/device/general/{lcd,touch}/8762G.
 * Both supported panels expose the same rtk_lcd_hal_* API, so the lvgl port is
 * panel agnostic; only the resolution differs.
 */

#ifndef __DISPLAY_DRV_H__
#define __DISPLAY_DRV_H__

#include <stdint.h>
#include <stdbool.h>
#include "menu_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 *                                 LCD driver
 *============================================================================*/
#if defined(CONFIG_REALTEK_INCA3311_280456_QSPI)
#include "icna3311_280x456_qspi.h"
#define DISPLAY_DRV_WIDTH       ICNA3311_LCD_WIDTH
#define DISPLAY_DRV_HEIGHT      ICNA3311_LCD_HIGHT
#elif defined(CONFIG_REALTEK_ST7701S_480480_RGB)
#include "st7701s_480480_rgb.h"
#define DISPLAY_DRV_WIDTH       ST7701S_480480_LCD_WIDTH
#define DISPLAY_DRV_HEIGHT      ST7701S_480480_LCD_HEIGHT
#else
#error "No LCD driver selected in menu_config.h (CONFIG_REALTEK_INCA3311_280456_QSPI / CONFIG_REALTEK_ST7701S_480480_RGB)"
#endif

/*============================================================================*
 *                                Touch driver
 *============================================================================*/
#if defined(CONFIG_REALTEK_BSP_TOUCH_ZT2717_8762G)
#include "touch_zt2717.h"
#elif defined(CONFIG_REALTEK_BSP_TOUCH_GT911_8762G)
#include "touch_gt911.h"
#elif defined(CONFIG_REALTEK_BSP_TOUCH_CST836U_8762G)
#include "touch_cst836u.h"
#else
#error "No touch driver selected in menu_config.h (expected CONFIG_REALTEK_BSP_TOUCH_*_8762G)"
#endif

/*============================================================================*
 *                        Missing driver prototypes
 *============================================================================*/
/* defined in some panel drivers but not declared in their headers */
void rtk_lcd_hal_transfer_done(void);
void rtk_touch_hal_init(void);

/* all supported panels are RGB565 */
#define DISPLAY_DRV_PIXEL_BYTES         2

#ifdef __cplusplus
}
#endif

#endif /* __DISPLAY_DRV_H__ */
