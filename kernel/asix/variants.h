/* SPDX-License-Identifier: GPL-2.0-or-later
 * USB 2.0 initialization adapted from Linux v6.18 asix_devices.c/ax88172a.c
 * (David Hollis, Phil Chang, James Painter, TiVo, OMICRON) and ASIX's
 * ax88179_178a.c at 42feb1252fe0669845a8d6714bcc77bfc19eea1d.
 * Register writes are volatile; EEPROM is only read, never programmed.
 */
static const struct asix_id *identity;
static enum asix_family family;
static u16 revision, link_speed;
static int link_duplex;
static u8 bulk_in,bulk_out,phy_address;
static u16 max_packet=512,rx_request=RX_CAPACITY;
static u8 legacy_frame[1518];
static unsigned legacy_header_n,legacy_have,legacy_length,legacy_padding;
static u8 legacy_header[4];
static const char *family_name(void) {
    switch(family) {
    case AX_172:return "AX88172";case AX_172A:return "AX88172A";
    case AX_772:return "AX88772/A (chipcode selected)";
    case AX_772B:return "AX88772B/C";case AX_178:return "AX88178";
    case AX_178A:return "AX88178A";case AX_772DE:return "AX88772D/E";
    default:return "none";
    }
}
static int legacy_cmd(s32 fd,u8 cmd,u16 value,u16 index) {
    return write_control(fd,cmd,value,index,NULL,0,"legacy register");
}
static int legacy_reset(s32 fd,u16 value,unsigned delay) {
    if(legacy_cmd(fd,0x20,value,0))return -1;
    udelay(delay*1000);return 0;
}
static int mii_acquire(s32 fd) {
    for(unsigned i=0;i<30;i++) {
        if(legacy_cmd(fd,6,0,0))return -1;
        udelay(1000);
        if(read_control(fd,0xc0,9,0,0,1,"MII ownership"))return -1;
        if(control_data[0]&1)return 0;
    }
    legacy_cmd(fd,0x0a,0,0);return -1;
}
static int mii_read(s32 fd,u16 reg,u16 *value) {
    if(mii_acquire(fd))return -1;
    int rc=read_control(fd,0xc0,7,phy_address,reg,2,"legacy PHY read");
    if(!rc)*value=le16(control_data);
    int finish=legacy_cmd(fd,0x0a,0,0);
    return rc || finish ? -1:0;
}
static int mii_write(s32 fd,u16 reg,u16 value) {
    u8 b[2]={value,value>>8};
    if(mii_acquire(fd))return -1;
    int rc=write_control(fd,8,phy_address,reg,b,2,"legacy PHY write");
    int finish=legacy_cmd(fd,0x0a,0,0);
    return rc || finish ? -1:0;
}
static int wait_link(s32 fd,int modern,int gigabit) {
    u16 status=0,a,b;
    for(unsigned i=0;i<150;i++) {
        int rc=modern ? phy_read(fd,1,&status,"PHY status") : mii_read(fd,1,&status);
        if(rc)return -1;
        rc=modern ? phy_read(fd,1,&status,"PHY status current") : mii_read(fd,1,&status);
        if(rc)return -1;
        if(status!=0xffff && (status&0x24)==0x24)break;
        udelay(100000);
    }
    if(status==0xffff || (status&0x24)!=0x24)return -1;
    if((modern?phy_read(fd,4,&a,"PHY local"):mii_read(fd,4,&a)) ||
       (modern?phy_read(fd,5,&b,"PHY peer"):mii_read(fd,5,&b)))return -1;
    u16 common=a&b;
    link_speed=common&0x180?100:10;
    link_duplex=(common&0x100) || (!(common&0x180) && (common&0x40));

    if(gigabit) {
        if((modern?phy_read(fd,9,&a,"PHY gigabit local"):mii_read(fd,9,&a)) ||
           (modern?phy_read(fd,10,&b,"PHY gigabit peer"):mii_read(fd,10,&b)))return -1;
        if((a&0x200) && (b&0x800)){link_speed=1000;link_duplex=1;}
    }
    return (common&0x1e0) || link_speed==1000 ? 0:-1;
}
static int init_legacy(s32 fd) {
    int embedded=1;u16 chip=0,eeprom=0xffff,v;
    rx_request=2048;
    if(family==AX_172) {
        for(int shift=16;shift>=0;shift-=8) {
            if(legacy_cmd(fd,0x1f,(identity->gpio>>shift)&255,0))return -1;
            udelay(5000);
        }
    }
    if(family==AX_172A) {
        if(read_control(fd,0xc0,0x21,0,0,1,"PHY interface mode"))return -1;
        unsigned mode=control_data[0]&12;
        if(mode!=0 && mode!=4)return -1;
        embedded=mode==0;
    }
    if(read_control(fd,0xc0,0x19,0,0,2,"PHY address"))return -1;
    phy_address=control_data[embedded?1:0];
    if(phy_address>31)return -1;
    if(family!=AX_172A)embedded=phy_address==16;
    if(family==AX_178) {
        if(read_control(fd,0xc0,0x0b,0x17,0,2,"board PHY configuration"))return -1;
        eeprom=le16(control_data);
        /* ASIX reference board reset, including GPIO0 boards and GPIO1 boards. */
        unsigned mode=eeprom==0xffff?0:eeprom&127;
        unsigned use0=eeprom==0xffff || !(eeprom&128);
        unsigned base=use0?1:4,level=use0?2:8;
        if(legacy_cmd(fd,0x1f,0x80|base|((use0 && mode==0)?0:level),0))return -1;
        udelay(25000);
        if(((eeprom>>8)==1) && !use0) {
            if(legacy_cmd(fd,0x1f,base,0))return -1;
            udelay(350000);
            if(legacy_cmd(fd,0x1f,base|level,0))return -1;
            udelay(350000);
        } else {
            unsigned extra=use0 && mode==0?0:level;
            if(legacy_cmd(fd,0x1f,base|extra|0x30,0))return -1;
            udelay(15000);
            if(legacy_cmd(fd,0x1f,base|extra|0x10,0))return -1;
            udelay(245000);
            if(legacy_cmd(fd,0x1f,base|extra|0x30,0))return -1;
        }
        if(legacy_cmd(fd,0x22,0,0) || legacy_reset(fd,0x48,150))return -1;
        if(mode==0) {if(mii_write(fd,0x14,0x82))return -1;}
        else if(mode==12) {
            if(mii_write(fd,31,5) || mii_write(fd,12,0) || mii_read(fd,1,&v) ||
               mii_write(fd,1,v|0x80) || mii_write(fd,31,0))return -1;
        } else {report("Unsupported AX88178 external PHY mode %u\n",mode);return -1;}
        rx_request=2048;
    } else if(family==AX_172A) {
        if(legacy_reset(fd,0x40,150) || legacy_reset(fd,0,150) ||
           legacy_reset(fd,embedded?0x20:0x40,150))return -1;
    } else if(family!=AX_172) {
        if(read_control(fd,0xc0,9,0,0,1,"legacy chipcode"))return -1;
        chip=control_data[0]&0x70;
        if(chip!=0 && chip!=0x10 && chip!=0x20)return -1;
        if(chip==0x20) {
            if(read_control(fd,0xc0,0x21,0,0,1,"B/C interface mode") || (control_data[0]&12))return -1;
        }
        if(legacy_cmd(fd,0x1f,chip?0x80:0xb0,0))return -1;
        udelay(5000);
        if(legacy_cmd(fd,0x22,embedded|(chip?0x10:0),0))return -1;
        if(!chip) {
            if(embedded) {
                if(legacy_reset(fd,0x40,150) || legacy_reset(fd,0,150) ||
                   legacy_reset(fd,0x28,150))return -1;
            } else if(legacy_reset(fd,0x48,150))return -1;
        } else {
            if(legacy_reset(fd,0x60,10) || legacy_reset(fd,0x20,600) ||
               legacy_reset(fd,0,0) || legacy_reset(fd,0x20,200))return -1;
            if(chip==0x10) {
                if(mii_write(fd,0x14,0x442c) || mii_write(fd,0x15,0x03c8) ||
                   mii_write(fd,0x16,0x4044))return -1;
            } else {
                if(legacy_cmd(fd,0x2a,0x8000,0x8001) ||
                   legacy_cmd(fd,0x2c,0,0) || legacy_cmd(fd,0x2e,0,0))return -1;
            }
        }
    }
    if(legacy_cmd(fd,0x10,0,0) || mii_write(fd,0,0x8000))return -1;
    /* Wait for BMCR reset to clear before advertising/restarting. */
    for(unsigned i=0;i<100;i++) {
        if(mii_read(fd,0,&v))return -1;
        if(!(v&0x8000))break;
        if(i==99)return -1;
        udelay(10000);
    }
    /* No pause negotiation: lwIP owns checksums and flow control is disabled. */
    if(mii_write(fd,4,0x01e1))return -1;
    if(family==AX_178 && mii_write(fd,9,0x200))return -1;
    if(mii_write(fd,0,0x1200))return -1;
    if(family!=AX_172 && legacy_cmd(fd,0x12,chip?0x1615:0x0c15,chip?0x1a:0x0e))return -1;
    if(wait_link(fd,0,family==AX_178))return -1;
    u16 medium=family==AX_172?4:0x104;
    if(link_duplex)medium|=2;
    if(family!=AX_172 && link_speed==100)medium|=0x200;
    if(family==AX_178) {medium|=8;if(link_speed==1000)medium|=1;}
    if(legacy_cmd(fd,0x1b,medium,0) || legacy_cmd(fd,0x10,0x88,0))return -1;
    return 0;
}
static int init_178a(s32 fd) {
    if(mac16(fd,0x26,0,"PHY power down") || mac16(fd,0x26,0x20,"PHY power up"))return -1;
    udelay(200000);
    if(mac8(fd,0x33,3,"enable clocks"))return -1;
    udelay(100000);
    u8 q[5]={7,0x70,0,3,0x0f};
    if(mac_write(fd,0x2e,q,5,"8K bulk queue") || mac8(fd,0x34,0,"software RX checksum") ||
       mac8(fd,0x35,0,"software TX checksum") || mac8(fd,0x54,0x34,"pause high") ||
       mac8(fd,0x55,0x52,"pause low"))return -1;
    /* Keep IPE off: RX data begins with the Ethernet header. */
    if(mac16(fd,0x0b,0,"stop RX"))return -1;
    u8 adv[2]={0xe1,1},gig[2]={0,2},restart[2]={0,0x12};
    if(write_control(fd,2,3,4,adv,2,"advertise 10/100") ||
       write_control(fd,2,3,9,gig,2,"advertise gigabit") ||
       write_control(fd,2,3,0,restart,2,"restart autonegotiation"))return -1;
    if(wait_link(fd,1,1))return -1;
    u16 mode=0x100|(link_duplex?2:0)|(link_speed==1000?9:link_speed==100?0x200:0);
    return mac16(fd,0x22,mode,"enable medium") || mac16(fd,0x0b,0x188,"enable RX") ? -1:0;
}
