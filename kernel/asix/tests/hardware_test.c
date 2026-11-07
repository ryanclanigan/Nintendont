/* SPDX-License-Identifier: GPL-2.0-or-later
 * Run the production initialization sequence with simulated IOS control I/O.
 * The adapter ID, resume error and descriptors model the reported Wii failure.
 */
#define mqueue_ack test_ack
#include "global.h"
void test_ack(struct ipcmessage *m,int result);
#include "../hardware.c"
#include "lwip/init.h"
void *asix_lwip_heap;
u32 host_ms;
static int resume_status=-6,descriptor_error,current_config=1,set_config_count;
static int enumerated_devices=1,chip_revision=0x0300,enumeration_calls,seed_calls;
static int chip_pid=0x1790,legacy_chipcode,phy_mode,eeprom_board=0xffff;
static int host_enable=1,link_ready=1,control_failure,alternate,bad_endpoints;
static int mii_owned,writes_by_request[256],input_frames;static u8 frame_copy[1514];
static char saved_log[4096];
u32 test_read32(u32 a){(void)a;return host_ms*1899;}
u32 sys_now(void){return host_ms;}
u32 asix_random(void){return 1234;}
void asix_assert(const char *s){fprintf(stderr,"lwIP assertion: %s\n",s);exit(1);}
void mdelay(int ms){host_ms+=ms;}
void udelay(int us){host_ms+=us/1000;}
void Shutdown(void){assert(!"unexpected shutdown");exit(1);}
void *heap_alloc_aligned(int heap,int n,int a){(void)heap;(void)a;return calloc(1,n);}
void heap_free(int heap,void *p){(void)heap;free(p);}
void sync_after_write(void *p,int n){(void)p;(void)n;}
void sync_before_read(void *p,int n){(void)p;(void)n;}
int mqueue_create(void *s,int n){(void)s;assert(n==2);return 99;}
int mqueue_recv(int q,struct ipcmessage **m,int flags){(void)q;(void)m;(void)flags;return -8;}
int mqueue_send(int q,struct ipcmessage *m,int flags){(void)q;(void)m;(void)flags;return 0;}
void test_ack(struct ipcmessage *m,int result){(void)m;(void)result;}
s32 USB_Initialize(void){return 1;}
void USBStorage_SeedDeviceList(const usb_device_entry *d,u32 count){assert(count==(unsigned)enumerated_devices);if(count)assert(d[0].device_id==(s32)0xdf1f0021);seed_calls++;}
s32 USB_ClearHalt(s32 fd,u8 ep){assert(fd==(s32)0xdf1f0021);(void)ep;return 0;}
s32 IOS_Ioctl(s32 fd,s32 req,void *in,s32 ilen,void *out,s32 olen){
    assert(fd==1);
    if(req==USBV5_IOCTL_GETDEVICECHANGE){enumeration_calls++;assert(olen==384);usb_device_entry *d=out;memset(d,0,olen);d->device_id=(s32)0xdf1f0021;d->vid=0x0b95;d->pid=chip_pid;return enumerated_devices;}
    if(req==USBV5_IOCTL_ATTACHFINISH)return 0;
    if(req==USBV5_IOCTL_SUSPEND_RESUME){assert(ilen==32 && ((s32*)in)[0]==(s32)0xdf1f0021 && ((u32*)in)[2]==1);return resume_status;}
    assert(!"unexpected IOS ioctl");return -1;
}
s32 IOS_IoctlvAsync(s32 fd,s32 req,s32 in,s32 out,void *vec,int q,void *m){(void)fd;(void)req;(void)in;(void)out;(void)vec;(void)q;(void)m;return 0;}
s32 USB_ReadCtrlMsg(s32 fd,u8 type,u8 req,u16 value,u16 index,u16 length,void *data){
    assert(fd==(s32)0xdf1f0021);u8 *b=data;memset(b,0,length);
    if(type==0x80 && req==6 && value==0x100){
        if(descriptor_error)return -6;
        assert(length==18);b[0]=18;b[1]=1;b[8]=0x95;b[9]=0x0b;b[10]=chip_pid;b[11]=chip_pid>>8;b[12]=chip_revision;b[13]=chip_revision>>8;
    } else if(type==0x80 && req==6 && value==0x200){
        u8 config[32]={9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,0xff,0,0,0,7,5,0x82,2,0,2,0,7,5,3,2,0,2,0};
        assert(length==9 || length==32);
        config[12]=alternate;config[20]=0x84;config[27]=bad_endpoints?0x85:5;
        memcpy(b,config,length);
    } else if(type==0x80 && req==8){assert(length==1);b[0]=current_config;}
    else if(type==0xc0 && ((req==1 && value==0x10) || req==0x13 || req==0x17)){const u8 mac[6]={0x9c,0x69,0xd3,0x70,0x27,3};assert(length==6);memcpy(b,mac,6);}
    else if(type==0xc0 && req==2){assert(value==3 && length==2);u16 v=index==1?(link_ready?0x24:0):index==9?0x200:index==10?0x800:0x100;b[0]=v;b[1]=v>>8;}
    else if(type==0xc0 && req==1 && value==2){assert(length==1);b[0]=2;}
    else if(type==0xc0 && req==0x19){assert(length==2);b[0]=b[1]=16;}
    else if(type==0xc0 && req==0x21){assert(length==1);b[0]=phy_mode;}
    else if(type==0xc0 && req==9){assert(length==1);b[0]=legacy_chipcode|(host_enable?1:0);}
    else if(type==0xc0 && req==0x0b){
        assert(length==2);
        if(value==0x17){b[0]=eeprom_board;b[1]=eeprom_board>>8;}
        else {const u8 mac[6]={0x9c,0x69,0xd3,0x70,0x27,3};assert(value>=4 && value<=6);memcpy(b,mac+(value-4)*2,2);}
    } else if(type==0xc0 && req==7){
        assert(mii_owned && value==16 && length==2);
        u16 v=index==0?0:index==1?(link_ready?0x24:0):index==9?0x200:index==10?0x800:0x100;
        b[0]=v;b[1]=v>>8;
    } else assert(!"unexpected USB read request");
    return control_failure ? -6:length+8;
}
s32 USB_WriteCtrlMsg(s32 fd,u8 type,u8 req,u16 value,u16 index,u16 n,void *data){
    assert(fd==(s32)0xdf1f0021);(void)index;(void)data;
    if(type==0 && req==9){assert(n==0);set_config_count++;current_config=value;}
    else if(type==1 && req==11){assert(value==alternate && index==0);}
    else {
        assert(type==0x40);writes_by_request[req]++;
        assert(req!=0x0c && req!=0x0d && req!=0x0e); /* no EEPROM programming */
        if(family!=AX_772DE && family!=AX_178A) {
            if(req==6)mii_owned=1;
            if(req==0x0a)mii_owned=0;
            if(req==8)assert(mii_owned && n==2 && value==16);
        }
    }
    return control_failure ? -6:n+8;
}
int f_open_main_drive(FIL *f,const char *p,int mode){assert(mode==(FA_CREATE_ALWAYS|FA_WRITE));f->pos=!strcmp(p,"/slippi_asix_boot.log");return 0;}
int f_write(FIL *f,const void *data,unsigned n,unsigned *written){if(f->pos){assert(n<sizeof(saved_log));memcpy(saved_log,data,n);saved_log[n]=0;}*written=n;return 0;}
int f_close(FIL *f){(void)f;return 0;}
static err_t test_input(struct pbuf *p,struct netif *n) {
    (void)n;assert(p->tot_len<=sizeof(frame_copy));
    pbuf_copy_partial(p,frame_copy,p->tot_len,0);input_frames++;pbuf_free(p);return ERR_OK;
}
static void put32(u8 *p,u32 v){for(unsigned i=0;i<4;i++)p[i]=v>>(i*8);}
static void packet_tests(struct netif *n) {
    u8 frame[1514];for(unsigned i=0;i<sizeof(frame);i++)frame[i]=i;
    n->input=test_input;
    /* Legacy stream: every possible split, including inside the header/pad. */
    family=AX_772;reset_legacy_rx();
    for(unsigned split=1;split<70;split++) {
        u8 packet[70];put32(packet,65|((65u^65535)<<16));memcpy(packet+4,frame,65);packet[69]=0;
        int before=input_frames;
        memcpy(rx_data,packet,split);receive(n,split);
        memcpy(rx_data,packet+split,70-split);receive(n,70-split);
        assert(input_frames==before+1 && !memcmp(frame_copy,frame,65));
    }
    /* Corrupt length complement is rejected, then the next transfer recovers. */
    unsigned bad=diag.bad_groups;memset(rx_data,0,4);receive(n,4);
    assert(diag.bad_groups==bad+1);
    put32(rx_data,60|((60u^65535)<<16));memcpy(rx_data+4,frame,60);receive(n,64);
    assert(!memcmp(frame_copy,frame,60));
    family=AX_178A;memset(rx_data,0,80);memcpy(rx_data,frame,60);
    put32(rx_data+64,60u<<16);put32(rx_data+68,0x80000000u);put32(rx_data+76,(64u<<16)|2);
    int before=input_frames;receive(n,80);assert(input_frames==before+1);
    assert(!memcmp(frame_copy,frame,60));
    put32(rx_data+64,0x20000000u|(60u<<16));receive(n,80);assert(input_frames==before+1);
    put32(rx_data+64,1500u<<16);bad=diag.bad_groups;receive(n,80);assert(diag.bad_groups==bad+1);
    family=AX_772DE;memset(rx_data,0,80);memcpy(rx_data,frame,60);
    put32(rx_data+64,(60u<<16)|0x800);put32(rx_data+72,(64u<<13)|1);
    before=input_frames;receive(n,80);assert(input_frames==before+1);
    family=AX_172;memcpy(rx_data,frame,60);receive(n,60);assert(input_frames==before+2);
    /* TX format boundaries: short Ethernet, bulk max-packet and MTU. */
    enum asix_family families[]={AX_172,AX_172A,AX_772,AX_772B,AX_178,AX_178A,AX_772DE};
    unsigned lengths[]={42,60,504,508,512,1514};
    for(unsigned f=0;f<7;f++)for(unsigned j=0;j<6;j++) {
        family=families[f];tx_count=tx_head=0;failed=0;
        struct pbuf *p=pbuf_alloc(PBUF_RAW,lengths[j],PBUF_RAM);assert(p);
        assert(pbuf_take(p,frame,lengths[j])==ERR_OK && asix_hw_output(n,p)==ERR_OK);pbuf_free(p);
        unsigned wire=lengths[j]<60?60:lengths[j];
        unsigned head=family==AX_172?0:family==AX_178A || family==AX_772DE?8:4;
        assert(!memcmp(tx_data[0]+head,frame,lengths[j]));
        for(unsigned k=lengths[j];k<wire;k++)assert(tx_data[0][head+k]==0);
        if(family==AX_772DE)assert((le32(tx_data[0])&0x1fffff)==wire && !(tx_len[0]&7));
        else if(family==AX_178A)assert(le32(tx_data[0])==wire && le32(tx_data[0]+4)==((wire+8)%512?0:0x80008000u));
        else if(family!=AX_172) {
            assert(le16(tx_data[0])==wire && le16(tx_data[0]+2)==(wire^65535));
            assert(tx_len[0]==wire+4+((wire+4)%512?0:4));
        }
    }
}
static void family_tests(struct netif *n) {
    struct {int pid,rev,chip;enum asix_family expected;} cases[]={
        {0x1720,1,0,AX_172},{0x172a,1,0,AX_172A},
        {0x7720,1,0,AX_772},{0x772a,1,0x10,AX_772},
        {0x772b,1,0x20,AX_772B},{0x772b,2,0x20,AX_772B},
        {0x1780,1,0,AX_178},{0x178a,0x100,0,AX_178A},
        {0x1790,0x300,0,AX_772DE}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        chip_pid=cases[i].pid;chip_revision=cases[i].rev;legacy_chipcode=cases[i].chip;
        probed=0;memset(writes_by_request,0,sizeof(writes_by_request));
        assert(asix_hw_init(n)==0 && family==cases[i].expected);
        assert(bulk_in==0x84 && bulk_out==5 && max_packet==512);
        if(family!=AX_178A && family!=AX_772DE)assert(writes_by_request[8] && !mii_owned);
        if(legacy_chipcode==0x20)assert(writes_by_request[0x2a] && writes_by_request[0x2c] && writes_by_request[0x2e]);
        asix_hw_boot_log(n);assert(strstr(saved_log,family_name()));
    }
    chip_pid=0x1780;chip_revision=1;eeprom_board=12;probed=0;
    assert(asix_hw_init(n)==0);asix_hw_boot_log(n);
    eeprom_board=2;probed=0;assert(asix_hw_init(n)==-9);eeprom_board=0xffff;
    chip_pid=0x772b;legacy_chipcode=0x20;phy_mode=4;probed=0;
    assert(asix_hw_init(n)==-9);phy_mode=0;
    chip_pid=0x172a;probed=0;phy_mode=4;
    assert(asix_hw_init(n)==0);asix_hw_boot_log(n);phy_mode=0;
    chip_pid=0x178a;chip_revision=0x100;alternate=1;probed=0;
    assert(asix_hw_init(n)==0);asix_hw_boot_log(n);alternate=0;
    bad_endpoints=1;probed=0;assert(asix_hw_init(n)==-6);bad_endpoints=0;
    chip_pid=0x1790;
    int revs[]={0x100,0x200,0x400,0x500};
    for(unsigned i=0;i<4;i++){chip_revision=revs[i];probed=0;assert(asix_hw_probe()==0);}
    chip_pid=0x772a;chip_revision=1;legacy_chipcode=0x10;host_enable=0;probed=0;
    assert(asix_hw_init(n)==-9);host_enable=1;
    probed=0;link_ready=0;assert(asix_hw_init(n)==-9);link_ready=1;
    probed=0;control_failure=1;assert(asix_hw_probe()<0);control_failure=0;
}
int main(void){
    struct netif n;memset(&n,0,sizeof(n));
    enumerated_devices=0;assert(asix_hw_probe()==0 && !device_present && seed_calls==1);
    probed=0;enumerated_devices=1;chip_revision=0x0200;
    assert(asix_hw_probe()==0 && !device_present);
    probed=0;chip_revision=0x0300;assert(asix_hw_probe()==1);
    int calls=enumeration_calls;assert(asix_hw_probe()==1 && enumeration_calls==calls);
    assert(asix_hw_init(&n)==0 && enumeration_calls==calls);assert(device==(s32)0xdf1f0021 && device_present);
    assert(resume_result==-6 && n.hwaddr[0]==0x9c && set_config_count==0);
    asix_hw_boot_log(&n);assert(strstr(saved_log,"resume_result=-6") && strstr(saved_log,"matched=1"));
    probed=0;descriptor_error=1;assert(asix_hw_init(&n)==-5);
    asix_hw_boot_log(&n);assert(strstr(saved_log,"last_control_result=-6") && strstr(saved_log,"request=06"));
    probed=0;descriptor_error=0;resume_status=0;current_config=0;
    assert(asix_hw_init(&n)==0 && set_config_count==1 && current_config==1);
    asix_hw_boot_log(&n);
    asix_lwip_heap=calloc(1,MEM_SIZE+4096);assert(asix_lwip_heap);lwip_init();
    family_tests(&n);packet_tests(&n);
    probed=0;assert(asix_hw_init(&n)==0);
    memset(rx_data,0x5a,80);capture_rx(80);capture_rx(16);
    assert(rx_trace_n==104 && rx_trace_groups==2 && le32(rx_trace)==80 && le32(rx_trace+84)==16);
    for(unsigned i=4;i<84;i++)assert(rx_trace[i]==0x5a);
    unsigned saved=rx_trace_n;rx_trace_n=RX_TRACE_CAPACITY-2;capture_rx(16);
    assert(rx_trace_n==RX_TRACE_CAPACITY-2 && rx_trace_dropped==1);rx_trace_n=saved;
    asix_hw_boot_log(&n);assert(!rx_trace && strstr(saved_log,"rx_trace_bytes=104 groups=2 dropped=1"));
    puts("PASS RX trace: multiple groups, little-endian lengths, capacity bound and boot-log persistence");
    puts("PASS USB 2.0 families: AX88172/A, AX88772/A/B/C/D/E, AX88178/A; packet splits, malformed RX, TX boundaries, endpoint selection, USB 3 exclusion, MII/link/control failures");
    puts("PASS hardware: absent/unsupported/supported probes, cached enumeration, storage snapshot, negative ID, resume error with valid descriptor, rejected descriptor failure, configuration verification, boot logs");
    return 0;
}
