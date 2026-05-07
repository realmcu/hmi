/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*============================================================================*
 *                              Header Files
 *============================================================================*/

#include "app_gui.h"
#include "board.h"
#include "section.h"
#include "trace.h"
#include "app_panel_init.h"
#include "gui_server.h"

//#include "romfs.h"
#include "gui_components_init.h"


//#define ROMFS_ADDR (0x0240f000 + 0x400)

void app_task_init(void)
{
    gui_server_init();
}


/*-----------------------------------------------------------*/
