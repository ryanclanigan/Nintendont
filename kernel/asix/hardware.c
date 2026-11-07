/* SPDX-License-Identifier: GPL-2.0-or-later
 * New Starlet port of the adapter sequence tested by asix-probe v0.9.
 * Register/format reference: ASIX ax88179a_772d.c/.h at
 * 42feb1252fe0669845a8d6714bcc77bfc19eea1d (ASIX, GPL-2.0-or-later).
 * Only 0b95:1790 revision 0300 (AX88772D) is supported here.
 */
#include "Config.h"
#include "debug.h"
#include "usb.h"
#include "hardware.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"
#define report dbgprintf
#define RX_CAPACITY 8192
#define TX_SLOTS 8
static s32 device=-1, ven=-1, queue=-1;
static u32 queue_store[2];
static u8 control_data[256] ALIGNED(32);
static u8 rx_data[RX_CAPACITY] ALIGNED(32);
static u8 tx_data[TX_SLOTS][1568] ALIGNED(32);
static u16 tx_len[TX_SLOTS];
static unsigned tx_head, tx_count;
static int rx_pending, tx_pending, failed;
static struct {u32 polls,rx_submit,rx_done,rx_bytes,tx_submit,tx_done,frames,bad_groups,input_errors,unknown; s32 rx_submit_rc,tx_submit_rc,last_rx,last_tx,queue_rc;} diag;
static u8 *first_rx,*first_tx;
static unsigned first_rx_n,first_tx_n;
static struct _usb_msg rx_req ALIGNED(32), tx_req ALIGNED(32);
static struct ipcmessage rx_msg ALIGNED(32), tx_msg ALIGNED(32);
static u16 le16(const u8 *p) { return p[0] | ((u16)p[1]<<8); }
static u32 le32(const u8 *p) { return p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }
static int read_control(s32 fd,u8 type,u8 req,u16 val,u16 idx,u16 n,const char *label) {
    s32 rc=USB_ReadCtrlMsg(fd,type,req,val,idx,n,control_data);
    if(rc!=n+8) { report("ASIX %s: %d\n",label,rc); return -1; }
    return 0;
}
static int write_control(s32 fd,u8 req,u16 val,u16 idx,const u8 *data,u16 n,const char *label) {
    if(n) memcpy(control_data,data,n);
    s32 rc=USB_WriteCtrlMsg(fd,0x40,req,val,idx,n,n?control_data:NULL);
    if(rc!=n+8) { report("ASIX %s: %d\n",label,rc); return -1; }
    return 0;
}
static int mac_write(s32 fd,u16 reg,const u8 *data,u16 n,const char *label) { return write_control(fd,1,reg,n,data,n,label); }
static int mac8(s32 fd,u16 reg,u8 val,const char *label) { return mac_write(fd,reg,&val,1,label); }
static int mac16(s32 fd,u16 reg,u16 val,const char *label) { u8 b[2]={val,val>>8}; return mac_write(fd,reg,b,2,label); }
static int phy_read(s32 fd,u16 reg,u16 *val,const char *label) { if(read_control(fd,0xc0,2,3,reg,2,label))return -1; *val=le16(control_data);return 0; }
static int initialize_adapter(s32 fd, u16 *speed, int *full_duplex) {
    u8 byte = 2;
    if (write_control(fd, 8, 1, 0, NULL, 0, "select vendor firmware mode")) return -1;
    /* 0x06 reloads existing settings into volatile state; it does not
     * enable flash/EEPROM programming or change stored settings.
     */
    if (write_control(fd, 6, 0, 0, NULL, 0, "reload existing configuration")) return -1;
    udelay(250000);
    if (write_control(fd, 0x31, 0, 0, &byte, 1, "PHY power on")) return -1;
    udelay(500000);
    if (write_control(fd, 0xc0, 0x0500, 0, NULL, 0, "disable autodetach")) return -1;
    if (mac16(fd, 0x0b, 0, "stop RX") || mac8(fd, 0xb7, 0, "stop MAC paths")) return -1;
    /* Software checksums, no VLAN acceleration, no IP alignment padding. */
    const u8 setup[][2] = {
        {0x34,0}, {0x35,0}, {0xb9,2}, {0x55,4}, {0x54,0x10},
        {0x2b,0}, {0x41,0xff}, {0x43,0}, {0x46,0}, {0x9e,0},
        {0xb1,0}, {0xcc,1}, {0xb0,0}, {0x0e,0}
    };
    for (unsigned i = 0; i < sizeof(setup)/sizeof(setup[0]); ++i) {
        char label[32]; _sprintf(label, "MAC register %02x", setup[i][0]);
        if (mac8(fd, setup[i][0], setup[i][1], label)) return -1;
    }
    report("Waiting up to 15 seconds for Ethernet link...\n");
    u16 status, local, partner;
    int linked = 0;
    for (unsigned i = 0; i < 15; ++i) {
        /* BMSR link bit is latched low; read twice. */
        if (phy_read(fd, 1, &status, "PHY BMSR (latched)") ||
            phy_read(fd, 1, &status, "PHY BMSR (current)")) return -1;
        if (status != 0xffff && (status & 0x24) == 0x24) { linked = 1; break; }
        udelay(1000000);
    }
    if (!linked) { report("RESULT: Ethernet link/autonegotiation not ready.\n"); return -1; }
    if (phy_read(fd, 4, &local, "PHY advertised") ||
        phy_read(fd, 5, &partner, "PHY partner abilities")) return -1;
    u16 common = local & partner;
    if (common & 0x0100) { *speed = 100; *full_duplex = 1; }
    else if (common & 0x0080) { *speed = 100; *full_duplex = 0; }
    else if (common & 0x0040) { *speed = 10; *full_duplex = 1; }
    else if (common & 0x0020) { *speed = 10; *full_duplex = 0; }
    else { report("No supported 10/100 link mode found.\n"); return -1; }
    report("Ethernet link: %u Mbps %s duplex\n", *speed, *full_duplex ? "full" : "half");
    u8 usb_status;
    if (read_control(fd, 0xc0, 1, 2, 1, 1, "USB speed")) return -1;
    usb_status = control_data[0];
    if (!(usb_status & 2)) { report("USB high speed required for this test.\n"); return -1; }
    if (mac8(fd, 0xb5, 0xa5, "CDC TX delay")) return -1;
    const u8 water[] = {0x10, 0x04};
    if (mac_write(fd, 0x54, water, 2, "pause thresholds") ||
        mac8(fd, 0xb8, 0x29, "pause control")) return -1;
    u8 cdc[] = {*speed == 100 ? 0x78 : 0xfa, 0x70, *speed == 100 ? 0 : 0xff};
    if (mac_write(fd, 0x6d, cdc, 3, "RX CDC settings") ||
        mac8(fd, 0xc0, *speed == 100 ? 0x40 : 0xfa, "RX CDC count")) return -1;
    u8 queue[] = {*speed == 100 && *full_duplex ? 5 : 7,
                  *speed == 100 ? 0xc0 : 0, *speed == 100 ? 4 : 0,
                  *speed == 100 ? 6 : 3, *speed == 100 ? 0x0f : 0x3f};
    /* Match ASIX's ax_check_rx_biq_size: if the device aggregation size
     * (size>3 ? size*2 : 4) KiB reaches the host RX buffer capacity,
     * lower size to host_KiB/2-1. This keeps the 8 KiB read large enough
     * for every group the adapter is configured to emit.
     */
    unsigned device_kib = queue[3]>3 ? queue[3]*2 : 4;
    if (device_kib >= RX_CAPACITY/1024) queue[3]=(RX_CAPACITY/1024/2)-1;
    report("USB RX request=%u bytes; aggregation size register=%u\n",RX_CAPACITY,queue[3]);
    if (mac_write(fd, 0x2e, queue, 5, "RX bulk queue")) return -1;
    u16 medium = 0x0130 | (*full_duplex ? 2 : 0);
    if (mac16(fd, 0x22, medium, "enable medium") ||
        mac16(fd, 0x0b, 0x0088, "enable unicast/broadcast RX") ||
        mac8(fd, 0xb7, 3, "enable RX/TX paths")) return -1;
    return 0;
}

static s32 submit(struct _usb_msg *r, struct ipcmessage *m,u8 ep,u8 *buf,u16 n) {
    memset(r,0,sizeof(*r)); memset(m,0,sizeof(*m));
    r->fd=device;r->bulk.rpData=buf;r->bulk.wLength=n;r->bulk.bEndpoint=ep;
    r->vec[0].data=r;r->vec[0].len=64;r->vec[1].data=buf;r->vec[1].len=n;
    sync_after_write(r,sizeof(*r)); sync_after_write(buf,n);
    return IOS_IoctlvAsync(ven,21,(ep&0x80)?1:2,(ep&0x80)?1:0,r->vec,queue,m);
}
static void receive(struct netif *n,unsigned size) {
    if(size<32 || (size&7)){diag.bad_groups++;return;}
    u32 trailer=le32(rx_data+size-8);
    unsigned count=trailer&8191,off=trailer>>13,cur=0;
    if(!count || off>size-8 || (off&7) || count>(size-8-off)/8 || ((size-8-off-count*8)&15)){diag.bad_groups++;return;}
    /* Validate every frame before delivering any to lwIP. */
    for(unsigned i=0;i<count;i++) {
        u32 h=le32(rx_data+off+i*8);unsigned len=(h>>16)&32767;
        if(len<14 || cur>off || len>off-cur || !(h&((1u<<11)|(1u<<31)))){diag.bad_groups++;return;}
        cur+=(len+7)&~7u;
    }
    if(cur!=off){diag.bad_groups++;return;}
    cur=0;
    for(unsigned i=0;i<count;i++) {
        u32 h=le32(rx_data+off+i*8);unsigned len=(h>>16)&32767;
        if((h&(1u<<11)) && !(h&(1u<<31)) && len<=1514) {
            diag.frames++;
            struct pbuf *p=pbuf_alloc(PBUF_RAW,len,PBUF_POOL);
            if(p) { if(pbuf_take(p,rx_data+cur,len)!=ERR_OK || n->input(p,n)!=ERR_OK){diag.input_errors++;pbuf_free(p);} }
        }
        cur+=(len+7)&~7u;
    }
}
void asix_hw_poll(struct netif *n) {
    struct ipcmessage *m;
    diag.polls++;
    while(queue>=0 && (diag.queue_rc=mqueue_recv(queue,&m,1))==0) {
        s32 result=m->result;
        if(m==&rx_msg) {
            rx_pending=0;diag.rx_done++;diag.last_rx=result;
            if(result<0 || result>RX_CAPACITY)failed=1;
            else if(result) {sync_before_read(rx_data,result);diag.rx_bytes+=result;
                if(first_rx && !first_rx_n){first_rx_n=result;memcpy(first_rx,rx_data,result);}
                receive(n,result);
            }
        } else if(m==&tx_msg) {
            tx_pending=0;diag.tx_done++;diag.last_tx=result;
            if(result!=tx_len[tx_head])failed=1;
            if(tx_count) {tx_head=(tx_head+1)%TX_SLOTS;tx_count--;}
        } else diag.unknown++;
        mqueue_ack(m,0);
    }
    if(failed)return;
    if(!rx_pending) {
        memset(rx_data,0xa5,sizeof(rx_data));
        diag.rx_submit++;
        diag.rx_submit_rc=submit(&rx_req,&rx_msg,0x82,rx_data,sizeof(rx_data));
        if(diag.rx_submit_rc<0)failed=1;
        else rx_pending=1;
    }
    if(tx_count && !tx_pending) {
        diag.tx_submit++;
        if(first_tx && !first_tx_n){first_tx_n=tx_len[tx_head];memcpy(first_tx,tx_data[tx_head],first_tx_n);}
        diag.tx_submit_rc=submit(&tx_req,&tx_msg,3,tx_data[tx_head],tx_len[tx_head]);
        if(diag.tx_submit_rc<0)failed=1;
        else tx_pending=1;
    }
}
err_t asix_hw_output(struct netif *n,struct pbuf *p) {
    (void)n;
    if(failed)return ERR_IF;
    if(p->tot_len>1514)return ERR_BUF;
    if(tx_count==TX_SLOTS)return ERR_MEM;
    unsigned slot=(tx_head+tx_count)%TX_SLOTS;
    unsigned wire=p->tot_len<60?60:p->tot_len,total=(wire+15)&~7u;
    u8 *out=tx_data[slot];memset(out,0,total);
    u32 h=wire|(((wire+(wire>>8)+(wire>>16))&127)<<21)|0x80000000u;
    for(unsigned i=0;i<4;i++)out[i]=h>>(8*i);
    pbuf_copy_partial(p,out+8,p->tot_len,0);
    tx_len[slot]=total;tx_count++;
    return ERR_OK;
}
int asix_hw_failed(void) { return failed; }
s32 asix_hw_init(struct netif *n) {
    /* Consume the initial snapshot before USBStorage's main-loop watcher
     * starts. Do not register a second concurrent device-change request. */
    static usb_device_entry list[32] ALIGNED(32);
    static u32 args[8] ALIGNED(32);
    ven=USB_Initialize(); if(ven<0)return -1;
    s32 count=IOS_Ioctl(ven,USBV5_IOCTL_GETDEVICECHANGE,NULL,0,list,sizeof(list));
    if(count<0 || count>32)return -2;
    for(s32 i=0;i<count;i++)if(list[i].vid==0x0b95 && list[i].pid==0x1790){device=list[i].device_id;break;}
    IOS_Ioctl(ven,USBV5_IOCTL_ATTACHFINISH,NULL,0,NULL,0);
    if(device<0)return -3;
    memset(args,0,sizeof(args));args[0]=device;args[2]=1;
    if(IOS_Ioctl(ven,USBV5_IOCTL_SUSPEND_RESUME,args,sizeof(args),NULL,0)<0)return -4;
    if(read_control(device,0x80,6,0x100,0,18,"device descriptor") || le16(control_data+12)!=0x0300)return -5;
    /* Validate the endpoint layout instead of applying this register sequence
     * to other chips that share the same VID/PID. */
    if(read_control(device,0x80,6,0x200,0,9,"configuration"))return -6;
    unsigned total=le16(control_data+2);u8 config=control_data[5];
    if(total>sizeof(control_data) || total<9 || read_control(device,0x80,6,0x200,0,total,"endpoints"))return -6;
    unsigned found=0;
    for(unsigned off=0;off+2<=total;) {
        unsigned len=control_data[off];if(len<2 || len>total-off)return -6;
        if(control_data[off+1]==5 && len>=7 && (control_data[off+3]&3)==2) {
            if(control_data[off+2]==0x82)found|=1;
            if(control_data[off+2]==3)found|=2;
        }
        off+=len;
    }
    if(found!=3)return -6;
    if(USB_WriteCtrlMsg(device,0,9,config,0,0,NULL)!=8)return -7;
    if(read_control(device,0xc0,1,0x10,6,6,"MAC"))return -8;
    memcpy(n->hwaddr,control_data,6);n->hwaddr_len=6;
    unsigned any=0;for(unsigned i=0;i<6;i++)any|=n->hwaddr[i];
    if(!any || (n->hwaddr[0]&1))return -8;
    u16 speed;int duplex;
    if(initialize_adapter(device,&speed,&duplex))return -9;
    queue=mqueue_create(queue_store,2);if(queue<0)return -10;
    first_rx=heap_alloc_aligned(0,RX_CAPACITY,32);
    first_tx=heap_alloc_aligned(0,1568,32);
    return 0;
}
void asix_hw_shutdown(void) {
    failed=1;
    if(device>=0) {USB_ClearHalt(device,0x82);USB_ClearHalt(device,3);}
    /* Buffers remain allocated for the kernel lifetime, including IOS's
     * cancellation completion. No pending DMA memory is freed here. */
}

/* Called only during initialization, before the Slippi and disc threads start. */
static void boot_file(const char *name,const void *data,unsigned length) {
    FIL file;u32 written;
    if(f_open_main_drive(&file,name,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK) {
        f_write(&file,data,length,&written);f_close(&file);
    }
}
void asix_hw_boot_log(struct netif *n) {
    static char text[2048];
    int length=_sprintf(text,
        "Slippi ASIX 0.2 Starlet preflight\r\n"
        "ip=%08x mask=%08x gateway=%08x\r\n"
        "MAC=%02x:%02x:%02x:%02x:%02x:%02x\r\n"
        "device=%d ven=%d queue=%d failed=%d\r\n"
        "rx_buffer=%08x tx_buffer=%08x rx_req=%08x tx_req=%08x\r\n"
        "polls=%u queue_rc=%d unknown_completions=%u\r\n"
        "RX submits=%u submit_rc=%d done=%u last_result=%d bytes=%u pending=%d\r\n"
        "TX submits=%u submit_rc=%d done=%u last_result=%d pending=%d queued=%u\r\n"
        "frames=%u malformed_groups=%u input_errors=%u\r\n"
        "first_RX=%u first_TX=%u\r\n",
        ip4_addr_get_u32(netif_ip4_addr(n)),ip4_addr_get_u32(netif_ip4_netmask(n)),ip4_addr_get_u32(netif_ip4_gw(n)),
        n->hwaddr[0],n->hwaddr[1],n->hwaddr[2],n->hwaddr[3],n->hwaddr[4],n->hwaddr[5],
        device,ven,queue,failed,(u32)rx_data,(u32)tx_data,(u32)&rx_req,(u32)&tx_req,
        diag.polls,diag.queue_rc,diag.unknown,diag.rx_submit,diag.rx_submit_rc,diag.rx_done,diag.last_rx,diag.rx_bytes,rx_pending,
        diag.tx_submit,diag.tx_submit_rc,diag.tx_done,diag.last_tx,tx_pending,tx_count,diag.frames,diag.bad_groups,diag.input_errors,
        first_rx_n,first_tx_n);
    boot_file("/slippi_asix_boot.log",text,length);
    if(first_rx)boot_file("/slippi_asix_rx.bin",first_rx,first_rx_n);
    if(first_tx)boot_file("/slippi_asix_tx.bin",first_tx,first_tx_n);
    if(first_rx){heap_free(0,first_rx);first_rx=NULL;}
    if(first_tx){heap_free(0,first_tx);first_tx=NULL;}
}
