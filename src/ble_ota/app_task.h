/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_TASK_H_
#define _APP_TASK_H_

#include "app_msg.h"
#include "stdbool.h"

/** @defgroup PERIPH_APP_TASK Peripheral App Task
  * @brief Peripheral App Task
  * @{
  */

extern void driver_init(void);

/**
 * @brief  Initialize App task
 * @return void
 */
void app_task_init(void);

/**
 * \brief    send msg queue to app task.
 *
 * \param[in]   p_handle   The handle to the message queue being peeked.
 *
 * \return           The status of the message queue peek.
 * \retval true      Message queue was peeked successfully.
 * \retval false     Message queue was failed to peek.
 */
bool app_send_msg_to_apptask(T_IO_MSG *p_msg);


/** End of PERIPH_APP_TASK
* @}
*/


#endif

