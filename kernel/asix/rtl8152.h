/* SPDX-License-Identifier: GPL-2.0-only
 * RTL8152B Starlet port. Register/firmware/framing reference:
 * Realtek Semiconductor Corp. (c) 2015, U-Boot v2025.10
 * drivers/usb/eth/r8152.c, r8152.h and r8152_fw.c (GPL-2.0).
 * Cross-checked with Linux v6.18 drivers/net/usb/r8152.c.
 * Every USB operation is checked. All writes are volatile controller/PHY/RAM.
 */
#include "rtl8152_patch.h"
#define RTL_PLA 0x0100
#define RTL_USB 0
static u16 rtl_silicon;
static int rtl_read(s32 fd,u16 type,u16 reg,unsigned width,u32 *value) {
    if(read_control(fd,0xc0,5,reg&~3u,type,4,"RTL register read"))return -1;
    *value=le32(control_data)>>((reg&3)*8);
    if(width==1)*value&=255;else if(width==2)*value&=65535;
    return 0;
}
static int rtl_write(s32 fd,u16 type,u16 reg,unsigned width,u32 value) {
    unsigned shift=reg&3;u8 b[4];
    value<<=shift*8;
    for(unsigned i=0;i<4;i++)b[i]=value>>(i*8);
    unsigned enable=width==4?255:(width==2?0x33:0x11)<<shift;
    return write_control(fd,5,reg&~3u,type|enable,b,4,"RTL register write");
}
static int rtl_modify(s32 fd,u16 type,u16 reg,unsigned width,u32 clear,u32 set) {
    u32 value;if(rtl_read(fd,type,reg,width,&value))return -1;
    return rtl_write(fd,type,reg,width,(value&~clear)|set);
}
static int rtl_wait(s32 fd,u16 type,u16 reg,u32 mask,int set) {
    for(unsigned i=0;i<500;i++) {
        u32 value;if(rtl_read(fd,type,reg,4,&value))return -1;
        if((value&mask)==(set?mask:0))return 0;
        udelay(1000);
    }
    report("RTL register timeout: %04x mask %08x\n",reg,mask);return -1;
}
static int rtl_ocp_write(s32 fd,u16 reg,u16 value) {
    if(rtl_write(fd,RTL_PLA,0xe86c,2,reg&0xf000))return -1;
    return rtl_write(fd,RTL_PLA,0xb000|(reg&0xfff),2,value);
}
static int rtl_mii_read(s32 fd,unsigned reg,u32 *value) {
    if(rtl_write(fd,RTL_PLA,0xe86c,2,0xa000))return -1;
    return rtl_read(fd,RTL_PLA,0xb400+reg*2,2,value);
}
static int rtl_mii_write(s32 fd,unsigned reg,u16 value) {return rtl_ocp_write(fd,0xa400+reg*2,value);}
static int rtl_identify(s32 fd) {
    u32 value;if(rtl_read(fd,RTL_PLA,0xe612,2,&value))return -1;
    rtl_silicon=value&0x7cf0;
    /* RTL8152B revisions supported by this port. Not RTL8153/8156/8157. */
    return rtl_silicon==0x4c10 || rtl_silicon==0x4800 ? 0:-1;
}
static int rtl_patch(s32 fd) {
    if(rtl_silicon!=0x4c10)return 0;
    u8 zero[16]={0};
    if(write_control(fd,5,0xfc28,RTL_PLA|255,zero,16,"RTL clear RAM breakpoints"))return -1;
    udelay(6000);
    if(rtl_write(fd,RTL_PLA,0xfc26,2,0))return -1;
    for(unsigned off=0;off<sizeof(rtl8152_patch);) {
        unsigned size=sizeof(rtl8152_patch)-off;if(size>64)size=64;
        if(write_control(fd,5,0xf800+off,RTL_PLA|255,rtl8152_patch+off,size,"RTL volatile RAM patch"))return -1;
        off+=size;
    }
    for(unsigned i=0;i<sizeof(rtl8152_patch_bp)/sizeof(rtl8152_patch_bp[0]);i+=2)
        if(rtl_write(fd,RTL_PLA,rtl8152_patch_bp[i],2,rtl8152_patch_bp[i+1]))return -1;
    return 0;
}
static int init_rtl8152(s32 fd) {
    /* Disable ALDPS/power cut before touching the MAC/PHY. */
    if(rtl_ocp_write(fd,0x2010,0x0310))return -1;
    udelay(20000);
    if(rtl_modify(fd,RTL_USB,0xd800,2,0x0100,0) ||
       rtl_modify(fd,RTL_USB,0xd432,2,1,0) ||
       rtl_modify(fd,RTL_PLA,0xe84c,2,0,0xc0) ||
       rtl_modify(fd,RTL_PLA,0xe0c0,4,0x0f0f0f0f,0x07014f07) ||
       rtl_write(fd,RTL_PLA,0xe022,2,15) ||
       rtl_modify(fd,RTL_USB,0xd428,2,0,0x8000) ||
       rtl_write(fd,RTL_USB,0xcbfc,2,1000) ||
       rtl_modify(fd,RTL_USB,0xd428,2,0x8000,0))return -1;
    /* Bound USB RX aggregation by disabling it for the first Wii port. Each
     * received frame still has the standard RTL 24-byte descriptor/FCS. */
    if(rtl_modify(fd,RTL_USB,0xd406,2,0x80,0x10) ||
       rtl_modify(fd,RTL_PLA,0xc010,4,15,0) ||
       rtl_modify(fd,RTL_PLA,0xe85a,2,0,8))return -1;
    u32 bmcr;if(rtl_mii_read(fd,0,&bmcr))return -1;
    if((bmcr&0x0800) && rtl_mii_write(fd,0,bmcr&~0x0800))return -1;
    if(rtl_patch(fd) || rtl_write(fd,RTL_PLA,0xe81c,1,0) ||
       rtl_write(fd,RTL_PLA,0xe813,1,0) ||
       rtl_modify(fd,RTL_PLA,0xe84f,1,0x80,0) ||
       rtl_modify(fd,RTL_PLA,0xe8de,2,0x4000,0) ||
       rtl_wait(fd,RTL_PLA,0xe84c,0x02000000,1) ||
       rtl_modify(fd,RTL_PLA,0xe8de,2,0,0x8000) ||
       rtl_wait(fd,RTL_PLA,0xe84c,0x02000000,1) ||
       rtl_modify(fd,RTL_PLA,0xe810,4,0,0x10000000) ||
       rtl_wait(fd,RTL_PLA,0xe810,0x10000000,0))return -1;
    if(rtl_write(fd,RTL_PLA,0xc0a0,4,0x00080002) ||
       rtl_write(fd,RTL_PLA,0xc0a4,4,max_packet==64?0x60:0x38) ||
       rtl_write(fd,RTL_PLA,0xc0a8,4,max_packet==64?0x78:0x48) ||
       rtl_write(fd,RTL_PLA,0xe618,4,0x00400008) ||
       rtl_write(fd,RTL_USB,0xd40a,1,3) ||
       rtl_write(fd,RTL_USB,0xd40c,4,0x7a120180) ||
       rtl_write(fd,RTL_USB,0xd434,4,0x101) ||
       rtl_write(fd,RTL_PLA,0xc016,2,1518) ||
       rtl_modify(fd,RTL_PLA,0xe610,2,0,0x80) ||
       rtl_modify(fd,RTL_PLA,0xe854,2,0x40,0) ||
       rtl_modify(fd,RTL_PLA,0xe080,2,2,0))return -1;
    /* Software checksums and no pause advertisement, as in the ASIX paths. */
    if(rtl_mii_write(fd,4,0x01e1) || rtl_mii_write(fd,0,0x9200))return -1;
    u32 status=0;
    for(unsigned i=0;i<150;i++) {
        if(rtl_read(fd,RTL_PLA,0xe908,1,&status))return -1;
        if(status&2)break;
        udelay(100000);
    }
    if(!(status&2))return -1;
    link_speed=(status&8)?100:10;link_duplex=!!(status&1);
    if(rtl_modify(fd,RTL_PLA,0xc0b4,2,1,0) || rtl_modify(fd,RTL_PLA,0xc0b4,2,0,1) ||
       rtl_modify(fd,RTL_PLA,0xe813,1,0,12) || rtl_modify(fd,RTL_PLA,0xe85a,2,8,0) ||
       rtl_modify(fd,RTL_PLA,0xc010,4,15,10))return -1;
    rx_request=RX_CAPACITY;return 0;
}
