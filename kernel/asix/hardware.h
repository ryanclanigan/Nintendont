/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ASIX_HARDWARE_H
#define ASIX_HARDWARE_H
#include "global.h"
#include "lwip/netif.h"
s32 asix_hw_init(struct netif *netif);
void asix_hw_poll(struct netif *netif);
void asix_hw_shutdown(void);
int asix_hw_failed(void);
err_t asix_hw_output(struct netif *netif, struct pbuf *p);
#endif
