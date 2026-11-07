/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SLIPPI_ASIX_DEVICE_LIST_H
#define SLIPPI_ASIX_DEVICE_LIST_H
#include "global.h"
#include "usb.h"
/* IOS VEN IDs are opaque signed values, not ordinary file descriptors.
 * Finding a matching entry must be tracked independently of the ID's sign. */
static inline int asix_find_device(const usb_device_entry *list, unsigned count, s32 *device) {
    unsigned i;
    if (!list || !device || count > USB_MAX_DEVICES) return 0;
    for (i = 0; i < count; i++) {
        if (list[i].vid == 0x0b95 && list[i].pid == 0x1790) {
            *device = list[i].device_id;
            return 1;
        }
    }
    return 0;
}
#endif
