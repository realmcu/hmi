/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * Licensed under the Realtek License, Version 1.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License from Realtek
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <string.h>

#include "ameba_soc.h"
#include "os_wrapper.h"

#include "gui_version.h"
#include "gui_server.h"

#define LOG_TAG "HoneyGUI-Demos"

static void honeygui_print_version(void) {
    RTK_LOGI(LOG_TAG, "===== HoneyGUI Version Information =====\n");
    RTK_LOGI(LOG_TAG, "Version:     %s\n", VERSION_TAG);
    RTK_LOGI(LOG_TAG, "Branch:      %s\n", VERSION_BRANCH);
    RTK_LOGI(LOG_TAG, "Commit:      %s\n", VERSION_COMMIT);
    RTK_LOGI(LOG_TAG, "Build Date:  %s\n", VERSION_BUILD_DATE);
    RTK_LOGI(LOG_TAG, "Repo Status: %s\n", VERSION_REPO_STATUS);
    RTK_LOGI(LOG_TAG, "========================================\n");
}

void honeygui_task(void *param) {
    UNUSED(param);

    RTK_LOGI(LOG_TAG, "honeygui_task start...\n");

    honeygui_print_version();

    gui_server_init();
    gui_set_keep_active_time(1000000);
    // TODO: Initialize HoneyGUI and run demos here

    RTK_LOGI(LOG_TAG, "honeygui_task end\n");

    rtos_task_delete(NULL);
}

u32 honeygui_demos(u16 argc, u8  *argv[]) {
    UNUSED(argc);
    UNUSED(argv);
    rtos_task_create(NULL, "honeygui_task", honeygui_task, NULL, 1024 * 32, 1);
    return TRUE;
}

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE cmd_table_honeygui_demos[] = {
    {"honeygui_demos", honeygui_demos},
};
