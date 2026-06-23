/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic Bluetooth (BR/EDR) link table implementation.
 */

#include "platform_autoconf.h"

#ifdef CONFIG_BT_EXT

#include <string.h>

#include "bt_classic_link.h"

T_BT_CLASSIC_DB bt_classic_db;

T_BT_CLASSIC_BR_LINK *bt_classic_find_br_link(uint8_t *bd_addr)
{
	uint8_t i;

	if (bd_addr == NULL) {
		return NULL;
	}

	for (i = 0; i < BT_CLASSIC_MAX_BR_LINK_NUM; i++) {
		if (bt_classic_db.br_link[i].used &&
			memcmp(bt_classic_db.br_link[i].bd_addr, bd_addr, 6) == 0) {
			return &bt_classic_db.br_link[i];
		}
	}
	return NULL;
}

T_BT_CLASSIC_BR_LINK *bt_classic_alloc_br_link(uint8_t *bd_addr)
{
	T_BT_CLASSIC_BR_LINK *p_link;
	uint8_t i;

	if (bd_addr == NULL) {
		return NULL;
	}

	p_link = bt_classic_find_br_link(bd_addr);
	if (p_link != NULL) {
		return p_link;
	}

	for (i = 0; i < BT_CLASSIC_MAX_BR_LINK_NUM; i++) {
		if (!bt_classic_db.br_link[i].used) {
			p_link = &bt_classic_db.br_link[i];
			memset(p_link, 0, sizeof(*p_link));
			p_link->used = true;
			p_link->id   = i;
			memcpy(p_link->bd_addr, bd_addr, 6);
			return p_link;
		}
	}
	return NULL;
}

bool bt_classic_free_br_link(T_BT_CLASSIC_BR_LINK *p_link)
{
	if (p_link != NULL && p_link->used) {
		memset(p_link, 0, sizeof(*p_link));
		return true;
	}
	return false;
}

void bt_classic_link_db_reset(void)
{
	memset(&bt_classic_db, 0, sizeof(bt_classic_db));
}

#endif /* CONFIG_BT_EXT */
