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

#ifndef HONEYGUI_DEMOS_H
#define HONEYGUI_DEMOS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HoneyGUI task entry point
 * @param param Task parameter (unused)
 */
void honeygui_task(void *param);

/**
 * @brief HoneyGUI demos command entry
 * @param argc Argument count
 * @param argv Argument values
 * @return TRUE on success
 */
u32 honeygui_demos(u16 argc, u8 *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* HONEYGUI_DEMOS_H */
