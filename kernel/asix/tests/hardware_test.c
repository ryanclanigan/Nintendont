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
    if(req==USBV5_IOCTL_GETDEVICECHANGE){enumeration_calls++;assert(olen==384);usb_device_entry *d=out;memset(d,0,olen);d->device_id=(s32)0xdf1f0021;d->vid=0x0b95;d->pid=0x1790;return enumerated_devices;}
    if(req==USBV5_IOCTL_ATTACHFINISH)return 0;
    if(req==USBV5_IOCTL_SUSPEND_RESUME){assert(ilen==32 && ((s32*)in)[0]==(s32)0xdf1f0021 && ((u32*)in)[2]==1);return resume_status;}
    assert(!"unexpected IOS ioctl");return -1;
}
s32 IOS_IoctlvAsync(s32 fd,s32 req,s32 in,s32 out,void *vec,int q,void *m){(void)fd;(void)req;(void)in;(void)out;(void)vec;(void)q;(void)m;return 0;}
s32 USB_ReadCtrlMsg(s32 fd,u8 type,u8 req,u16 value,u16 index,u16 length,void *data){
    assert(fd==(s32)0xdf1f0021);u8 *b=data;memset(b,0,length);
    if(type==0x80 && req==6 && value==0x100){
        if(descriptor_error)return -6;
        assert(length==18);b[0]=18;b[1]=1;b[8]=0x95;b[9]=0x0b;b[10]=0x90;b[11]=0x17;b[12]=chip_revision;b[13]=chip_revision>>8;
    } else if(type==0x80 && req==6 && value==0x200){
        static const u8 config[32]={9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,0xff,0,0,0,7,5,0x82,2,0,2,0,7,5,3,2,0,2,0};
        assert(length==9 || length==32);memcpy(b,config,length);
    } else if(type==0x80 && req==8){assert(length==1);b[0]=current_config;}
    else if(type==0xc0 && req==1 && value==0x10){const u8 mac[6]={0x9c,0x69,0xd3,0x70,0x27,3};assert(length==6);memcpy(b,mac,6);}
    else if(type==0xc0 && req==2){assert(value==3 && length==2);u16 v=index==1?0x24:0x100;b[0]=v;b[1]=v>>8;}
    else if(type==0xc0 && req==1 && value==2){assert(length==1);b[0]=2;}
    else assert(!"unexpected USB read request");
    return length+8;
}
s32 USB_WriteCtrlMsg(s32 fd,u8 type,u8 req,u16 value,u16 index,u16 n,void *data){
    assert(fd==(s32)0xdf1f0021);(void)index;(void)data;
    if(type==0 && req==9){assert(n==0);set_config_count++;current_config=value;}
    else assert(type==0x40);
    return n+8;
}
int f_open_main_drive(FIL *f,const char *p,int mode){assert(mode==(FA_CREATE_ALWAYS|FA_WRITE));f->pos=!strcmp(p,"/slippi_asix_boot.log");return 0;}
int f_write(FIL *f,const void *data,unsigned n,unsigned *written){if(f->pos){assert(n<sizeof(saved_log));memcpy(saved_log,data,n);saved_log[n]=0;}*written=n;return 0;}
int f_close(FIL *f){(void)f;return 0;}
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
    puts("PASS hardware: absent/unsupported/supported probes, cached enumeration, storage snapshot, negative ID, resume error with valid descriptor, rejected descriptor failure, configuration verification, boot logs");
    return 0;
}
