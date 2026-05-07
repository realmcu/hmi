/**
*****************************************************************************************
*     Copyright(c) 2017, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
   * @file      bt_task.h
   * @brief     Routines to create BT task and handle events & messages
   * @author    jane
   * @date      2017-06-02
   * @version   v1.0
   **************************************************************************************
   * @attention
   * <h2><center>&copy; COPYRIGHT 2017 Realtek Semiconductor Corporation</center></h2>
   **************************************************************************************
  */
#ifndef _BT_TASK_H_
#define _BT_TASK_H_
#include "app_msg.h"
#include "stdbool.h"

/** @defgroup PERIPH_BT_TASK Peripheral BT Task
  * @brief Peripheral BT Task
  * @{
  */

/**
 * @brief  Initialize BT task
 * @return void
 */
void bt_task_init(void);

bool send_msg_to_bt_task(T_IO_MSG *p_msg);
/** End of PERIPH_BT_TASK
* @}
*/


#endif

