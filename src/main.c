/**
*********************24, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
   * @file      main.c
   * @brief     Source file for display project, mainly used for initialize modules
   * @author    Luke
   * @date      2024-08-29
   * @version   v1.0
   **************************************************************************************
   * @attention
   * <h2><center>&copy; COPYRIGHT 2023 Realtek Semiconductor Corporation</center></h2>
   **************************************************************************************
  */

/*============================================================================*
 *                              Header Files
 *============================================================================*/

#include "trace.h"
#include "display_init.h"
#include "os_sched.h"





/* Non-Secure main. */
int main(void)
{
    DBG_DIRECT("Non-Secure World: main");

    display_system_clock_init();
    display_components_init();
    display_task_init();

    /* Start scheduler. */
    os_sched_start();
}
/*-----------------------------------------------------------*/
