/**
*****************************************************************************************
*     Copyright(c) 2017, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
   * @file
   * @brief     This file handles BLE peripheral application routines.
   * @author    jane
   * @date      2017-06-06
   * @version   v1.0
   **************************************************************************************
   * @attention
   * <h2><center>&copy; COPYRIGHT 2017 Realtek Semiconductor Corporation</center></h2>
   **************************************************************************************
  */

#ifndef _BLE_GAP_MSG_APP__
#define _BLE_GAP_MSG_APP__

#ifdef __cplusplus
extern "C" {
#endif

#include "app_msg.h"
#include "stdbool.h"
#include "stddef.h"



/**
 * Single List structure for ble
 */
struct ble_slist_node
{
    struct ble_slist_node *next;
};
typedef struct ble_slist_node ble_slist_t;

static __inline void ble_slist_append(ble_slist_t *l, ble_slist_t *n)
{
    struct ble_slist_node *node;

    node = l;
    while (node->next) { node = node->next; }

    /* append the node to the tail */
    node->next = n;
    n->next = NULL;
}
static __inline ble_slist_t *ble_slist_remove(ble_slist_t *l, ble_slist_t *n)
{
    /* remove slist head */
    struct ble_slist_node *node = l;
    while (node->next && node->next != n) { node = node->next; }

    /* remove node */
    if (node->next != (ble_slist_t *)0) { node->next = node->next->next; }

    return l;
}
static __inline ble_slist_t *ble_slist_first(ble_slist_t *l)
{
    return l->next;
}
static __inline ble_slist_t *ble_slist_tail(ble_slist_t *l)
{
    while (l->next) { l = l->next; }

    return l;
}
static __inline ble_slist_t *ble_slist_next(ble_slist_t *n)
{
    return n->next;
}

#define ble_container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))


typedef void (*P_LE_MSG_HANDLER_CBACK)(T_IO_MSG *p_gap_msg);

typedef struct _T_LE_MSG_CBACK_ITEM
{
    ble_slist_t slist;
    P_LE_MSG_HANDLER_CBACK              cback;
} T_LE_MSG_CBACK_ITEM;

void hmi_handle_bt_io_msg(T_IO_MSG io_msg);

void hmi_le_msg_cback_register(P_LE_MSG_HANDLER_CBACK cback);
void hmi_le_msg_cback_unregister(P_LE_MSG_HANDLER_CBACK cback);

/* Start (or restart) legacy advertising, tolerant of a still-tearing-down link:
 * retries automatically when the GAP device state settles.  Use this instead of
 * le_adv_start() for any "return to receiver/advertising" transition. */
void hmi_ble_gap_start_adv(void);

/* Current GAP advertising state (@ref GAP_ADV_STATE_* in gap_msg.h). */
uint8_t hmi_ble_gap_get_adv_state(void);


#ifdef __cplusplus
}
#endif

#endif

