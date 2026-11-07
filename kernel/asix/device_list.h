/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SLIPPI_ASIX_DEVICE_LIST_H
#define SLIPPI_ASIX_DEVICE_LIST_H
#include "global.h"
#include "usb.h"
enum asix_family { AX_NONE, AX_172, AX_172A, AX_772, AX_772B, AX_178, AX_178A, AX_772DE };
struct asix_id { u16 vid,pid; enum asix_family family; u32 gpio; };
/* Legacy identities and board GPIO sequences: Linux v6.18 asix_devices.c.
 * Never infer compatibility from the vendor alone. 1790 is shared by USB 3
 * chips: only bcdDevice 0300 selects the USB 2.0 AX88772D/E protocol. */
static const struct asix_id asix_ids[] = {
    {0x077b,0x2226,AX_172,0x130103},
    {0x0846,0x1040,AX_172,0x130103},
    {0x2001,0x1a00,AX_172,0x9f9d9f},
    {0x0b95,0x1720,AX_172,0x130103},
    {0x07b8,0x420a,AX_172,0x1f1d1f},
    {0x08dd,0x90ff,AX_172,0x130103},
    {0x08dd,0x0114,AX_178,0},
    {0x0557,0x2009,AX_172,0x130103},
    {0x0411,0x003d,AX_172,0x130103},
    {0x0411,0x006e,AX_178,0},
    {0x6189,0x182d,AX_172,0x130103},
    {0x0df6,0x0056,AX_178,0},
    {0x0df6,0x061c,AX_178,0},
    {0x07aa,0x0017,AX_172,0x130103},
    {0x1189,0x0893,AX_172,0x130103},
    {0x1631,0x6200,AX_172,0x130103},
    {0x04f1,0x3008,AX_172,0x130103},
    {0x17ef,0x7203,AX_772B,0},
    {0x0b95,0x772b,AX_772B,0},
    {0x0b95,0x7720,AX_772,0},
    {0x0b95,0x1780,AX_178,0},
    {0x0789,0x0160,AX_178,0},
    {0x13b1,0x0018,AX_772,0},
    {0x1557,0x7720,AX_772,0},
    {0x07d1,0x3c05,AX_772,0},
    {0x2001,0x3c05,AX_772,0},
    {0x2001,0x1a02,AX_772,0},
    {0x1737,0x0039,AX_178,0},
    {0x04bb,0x0930,AX_178,0},
    {0x050d,0x5055,AX_178,0},
    {0x05ac,0x1402,AX_772,0},
    {0x0b95,0x772a,AX_772,0},
    {0x14ea,0xab11,AX_178,0},
    {0x0db0,0xa877,AX_772,0},
    {0x0b95,0x7e2b,AX_772B,0},
    {0x0b95,0x172a,AX_172A,0},

    /* Additional identities in ASIX legacy driver e87b8576e6fa3accd373cff326850ab0a216df36. */
    {0x125e,0x180d,AX_772,0},
    {0x0421,0x772a,AX_772,0},
    {0x0b95,0x178a,AX_178A,0},
    {0x0b95,0x1790,AX_772DE,0}
};
static const struct asix_id *asix_identity(u16 vid,u16 pid) {
    for(unsigned i=0;i<sizeof(asix_ids)/sizeof(asix_ids[0]);i++)
        if(asix_ids[i].vid==vid && asix_ids[i].pid==pid)return &asix_ids[i];
    return NULL;
}
/* VEN device IDs are opaque signed values; negative IDs are valid. */
static inline int asix_find_device(const usb_device_entry *list,unsigned count,s32 *device) {
    if(!list || !device || count>USB_MAX_DEVICES)return 0;
    for(unsigned i=0;i<count;i++)if(asix_identity(list[i].vid,list[i].pid)) {
        *device=list[i].device_id;return 1;
    }
    return 0;
}
#endif
