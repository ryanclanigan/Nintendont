/* SPDX-License-Identifier: GPL-2.0-only
 * Register-level IOS test double for the production RTL8152B port.
 * Models byte enables, indirect MII, self-clearing reset and read-only link. */
static int chip_vendor=0x0b95,rtl_calls,rtl_fail_at,rtl_short_at,rtl_stuck,rtl_no_ll;
static int rtl_multi,rtl_cdc_only,rtl_full_speed;
static u8 rtl_regs[2][65536];
static void rtl_seed(u16 silicon) {
    memset(rtl_regs,0,sizeof(rtl_regs));rtl_calls=rtl_fail_at=rtl_short_at=rtl_stuck=rtl_no_ll=0;
    rtl_regs[1][0xe612]=silicon;rtl_regs[1][0xe613]=silicon>>8;
    rtl_regs[1][0xe84f]=2;rtl_regs[1][0xe908]=11;
    const u8 mac[6]={2,0x12,0x34,0x56,0x78,0x9a};memcpy(rtl_regs[1]+0xc000,mac,6);
}
static int rtl_transfer(u8 type,u16 reg,u16 index,u16 n,void *data) {
    assert(!(reg&3) && !(n&3) && n && (unsigned)reg+n<=65536);
    unsigned space=!!(index&0x100);u8 *r=rtl_regs[space]+reg,*b=data;
    rtl_calls++;
    if(rtl_calls==rtl_fail_at)return -6;
    if(rtl_calls==rtl_short_at)return n+7;
    if(type==0xc0) {
        assert((index&255)==0);
        if(space && reg==0xe810 && !rtl_stuck)rtl_regs[1][0xe813]&=~0x10;
        if(space && reg==0xe84c && rtl_no_ll)rtl_regs[1][0xe84f]&=~2;
        memcpy(b,r,n);
    } else {
        assert(type==0x40);unsigned enable=index&255;assert(enable);
        for(unsigned i=0;i<n;i++) {
            unsigned en=i<4?enable&15:i>=n-4?enable>>4:15;
            if(en&(1<<(i&3)))r[i]=b[i];
        }
    }
    return n+8;
}
