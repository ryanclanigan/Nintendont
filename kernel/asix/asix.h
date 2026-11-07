/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SLIPPI_ASIX_H
#define SLIPPI_ASIX_H
#include "global.h"
#include "net.h"
s32 ASIXInit(void);
/* 1 = selected, 0 = use native IOS, negative = selected driver failed. */
s32 ASIXTryInit(void);
int ASIXIsActive(void);
s32 ASIXSocket(u32 domain, u32 type, u32 protocol);
s32 ASIXClose(s32 sock);
s32 ASIXBind(s32 sock, const struct sockaddr *addr);
s32 ASIXListen(s32 sock, u32 backlog);
s32 ASIXAccept(s32 sock);
s32 ASIXSend(s32 sock, const void *data, s32 len);
s32 ASIXRecv(s32 sock, void *data, s32 len);
s32 ASIXConnect(s32 sock, const struct sockaddr *addr);
s32 ASIXSetSockOpt(s32 sock, u32 level, u32 option, const void *value, u32 len);
s32 ASIXPoll(struct pollsd *sds, s32 count, s32 timeout);
void ASIXUpdate(void);
void ASIXShutdown(void);
#endif
