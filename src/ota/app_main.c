/*
 * app_main.c — global app data definitions expected by ota_demo modules.
 *
 * The real application entry point for hmi_dashboard is src/application/main.c.
 * This file only owns the global data structures (`app_db`, `app_cfg_nv`) that
 * the OTA business code (copied from ota_demo) references.
 *
 * .bss zero-init is sufficient: T_APP_LE_LINK::used == false marks a slot as
 * free, matching ota_demo behaviour, so no explicit init function is needed.
 */

#include "app_main.h"
#include "app_cfg.h"

T_APP_DB     app_db;
T_APP_CFG_NV app_cfg_nv;
