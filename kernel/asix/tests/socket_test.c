/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercises the production socket bridge and real lwIP stack with a wire peer.
 */
#include "../asix.c"
#include "../device_list.h"
u32 host_ms, current_ip_address, NetworkStarted; s32 top_fd;u8 wifi_mac_address[6];
static const char *config_text="ip=192.168.137.250\nnetmask=255.255.255.0\ngateway=192.168.137.1\n";
static struct ipcmessage *mutex_token;
static int detected=1,config_reads,missing_config;
u32 test_read32(u32 addr){(void)addr;return host_ms*1899;}
void mdelay(int ms){host_ms+=ms;}
int mqueue_create(void *s,int n){(void)s;assert(n==1);return 1;}
int mqueue_recv(int q,struct ipcmessage **m,int flags){assert(q==1 && !flags && mutex_token);*m=mutex_token;mutex_token=NULL;return 0;}
int mqueue_send(int q,struct ipcmessage *m,int flags){assert(q==1 && !flags && !mutex_token);mutex_token=m;return 0;}
void *heap_alloc_aligned(int h,int n,int a){(void)h;(void)a;return calloc(1,n);}
void Shutdown(void){assert(!"unexpected shutdown");exit(1);}
int IOSC_GenerateRand(u8 *data,u32 n){for(unsigned i=0;i<n;i++)data[i]=17+i;return 0;}
int f_open_main_drive(FIL *f,const char *p,int m){config_reads++;(void)p;(void)m;f->pos=0;return missing_config?FR_NO_FILE:0;}
int f_read(FIL *f,void *d,unsigned max,unsigned *size){(void)f;*size=strlen(config_text);if(*size>max)*size=max;memcpy(d,config_text,*size);return 0;}
int f_close(FIL *f){(void)f;return 0;}
static const u8 peer_mac[6]={0x40,0xc2,0xba,0x0f,0x0d,0x9b};
static u32 peer_seq=1000,server_seq,stream_bytes;
static unsigned synacks,broadcasts,retransmits;
static int block_output,drop_next_data;
static u32 unblock_at;
static unsigned rejected_outputs,dropped_segments;
static u8 incoming[64][1514];static unsigned sizes[64],head,count;
static u16 b16(const u8 *p){return ((u16)p[0]<<8)|p[1];}
static u32 b32(const u8 *p){return ((u32)b16(p)<<16)|b16(p+2);}
static void w16(u8 *p,u16 x){p[0]=x>>8;p[1]=x;}
static void w32(u8 *p,u32 x){w16(p,x>>16);w16(p+2,x);}
static u16 checksum(const u8 *p,unsigned n,u32 sum){for(unsigned i=0;i<n;i+=2)sum+=((u16)p[i]<<8)|(i+1<n?p[i+1]:0);while(sum>>16)sum=(sum&65535)+(sum>>16);return ~sum;}
static u8 *new_frame(unsigned n,u16 protocol){assert(count<64);unsigned slot=(head+count++)%64;sizes[slot]=n;u8 *f=incoming[slot];memset(f,0,n);memcpy(f,nic.hwaddr,6);memcpy(f+6,peer_mac,6);w16(f+12,protocol);return f;}
static void segment(u8 flags,const void *payload,unsigned n){
    u8 *f=new_frame(54+n,0x800),*ip=f+14,*tcp=ip+20;
    ip[0]=0x45;w16(ip+2,40+n);ip[8]=64;ip[9]=6;w32(ip+12,0xc0a88901);w32(ip+16,0xc0a889fa);w16(ip+10,checksum(ip,20,0));
    w16(tcp,40000);w16(tcp+2,51441);w32(tcp+4,peer_seq);w32(tcp+8,server_seq);tcp[12]=0x50;tcp[13]=flags;w16(tcp+14,32768);
    if(n)memcpy(tcp+20,payload,n);
    w16(tcp+16,checksum(tcp,20+n,6+20+n+0xc0a8+0x8901+0xc0a8+0x89fa));
    peer_seq+=n+!!(flags&2)+!!(flags&1);
}
s32 asix_hw_probe(void){return detected;}
static int legacy_native;
int asix_hw_default_to_ios(void){return legacy_native;}
s32 asix_hw_init(struct netif *n){const u8 mac[6]={0x9c,0x69,0xd3,0x70,0x27,3};memcpy(n->hwaddr,mac,6);n->hwaddr_len=6;return 0;}
int asix_hw_failed(void){return 0;}
void asix_hw_shutdown(void){}
void asix_hw_boot_log(struct netif *n){(void)n;}
void asix_hw_poll(struct netif *n){
    if(block_output && host_ms>=unblock_at)block_output=0;
    if(!count)return;unsigned slot=head;head=(head+1)%64;count--;
    struct pbuf *p=pbuf_alloc(PBUF_RAW,sizes[slot],PBUF_POOL);assert(p);assert(pbuf_take(p,incoming[slot],sizes[slot])==ERR_OK);assert(n->input(p,n)==ERR_OK);
}
err_t asix_hw_output(struct netif *n,struct pbuf *p){
    (void)n;if(block_output){rejected_outputs++;return ERR_MEM;}
    u8 f[1514];assert(p->tot_len<=sizeof(f));pbuf_copy_partial(p,f,p->tot_len,0);
    if(b16(f+12)==0x806){
        if(b16(f+20)==1){u8 *a=new_frame(42,0x806)+14;w16(a,1);w16(a+2,0x800);a[4]=6;a[5]=4;w16(a+6,2);memcpy(a+8,peer_mac,6);w32(a+14,0xc0a88901);memcpy(a+18,nic.hwaddr,6);w32(a+24,0xc0a889fa);}return ERR_OK;
    }
    assert(b16(f+12)==0x800);u8 *ip=f+14;unsigned ih=(ip[0]&15)*4;assert(checksum(ip,ih,0)==0);
    if(ip[9]==17){u8 *udp=ip+ih;assert(b32(ip+16)==0xffffffff && b16(udp+2)==20582 && b16(udp+4)==56);assert(!memcmp(udp+8,"SLIP_READY",10));broadcasts++;return ERR_OK;}
    if(ip[9]!=6)return ERR_OK;
    u8 *t=ip+ih;unsigned th=(t[12]>>4)*4,len=b16(ip+2)-ih-th;
    assert(checksum(t,th+len,6+th+len+0xc0a8+0x89fa+0xc0a8+0x8901)==0);
    if(t[13]&4)return ERR_OK;
    if((t[13]&0x12)==0x12){synacks++;server_seq=b32(t+4)+1;segment(0x10,NULL,0);}
    if(len && drop_next_data){drop_next_data=0;dropped_segments++;return ERR_OK;}
    if(len){u32 seq=b32(t+4);if(seq>server_seq){segment(0x10,NULL,0);return ERR_OK;}if(seq<server_seq)retransmits++;else {for(unsigned i=0;i<len;i++)assert(t[th+i]==(u8)((stream_bytes+i)*37+13));stream_bytes+=len;server_seq+=len;}segment(0x10,NULL,0);}
    return ERR_OK;
}
int main(void){
    usb_device_entry entries[2]={{(s32)0x80010001,0x0b95,0x1790,0},{123,0x1234,0x5678,0}};
    s32 device_id=0;
    assert(asix_find_device(entries,2,&device_id) && device_id==(s32)0x80010001);
    entries[0].device_id=42;assert(asix_find_device(entries,2,&device_id) && device_id==42);
    entries[0].pid=0x7720;assert(asix_find_device(entries,2,&device_id));
    entries[0].pid=0x1791;assert(!asix_find_device(entries,2,&device_id));
    assert(!asix_find_device(entries,0,&device_id));
    assert(!asix_find_device(entries,33,&device_id));
    ip4_addr_t ip,mask,gw;
    assert(load_config(&ip,&mask,&gw)==0);
    const char *good=config_text;
    config_text="ip=192.168.137.0\nnetmask=255.255.255.0\n";assert(load_config(&ip,&mask,&gw)<0);
    config_text="ip=192.168.137.250\nnetmask=255.0.255.0\n";assert(load_config(&ip,&mask,&gw)<0);
    config_text="ip=192.168.137.250\nip=192.168.137.249\nnetmask=255.255.255.0\n";assert(load_config(&ip,&mask,&gw)<0);
    config_text="ip=192.168.137.250\nnetmask=255.255.255.0\ngateway=10.0.0.1\n";assert(load_config(&ip,&mask,&gw)<0);
    config_text="invalid";config_reads=0;detected=0;
    assert(ASIXTryInit()==0 && !ASIXIsActive() && config_reads==0);
    detected=-5;assert(ASIXTryInit()==0 && !ASIXIsActive() && config_reads==0);
    detected=1;assert(ASIXTryInit()==-101 && !ASIXIsActive());
    missing_config=1;legacy_native=1;assert(ASIXTryInit()==0 && !ASIXIsActive());
    legacy_native=0;assert(ASIXTryInit()==-101 && !ASIXIsActive());
    missing_config=0;legacy_native=1;assert(ASIXTryInit()==-101 && !ASIXIsActive());
    legacy_native=0;config_text=good;assert(ASIXTryInit()==1 && ASIXIsActive());
    assert(ASIXSocket(AF_INET,SOCK_STREAM,IPPROTO_IP)==SERVER_FD);
    struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=lwip_htons(51441);
    assert(ASIXBind(SERVER_FD,(struct sockaddr*)&addr)==0);assert(ASIXListen(SERVER_FD,1)==0);
    segment(2,NULL,0);s32 fd=ASIXAccept(SERVER_FD);assert(fd>=3 && synacks==1);
    struct pollsd poller={fd,POLLIN,0};assert(ASIXPoll(&poller,1,0)==0);
    u8 prefix[4]={0,0,0,15};segment(0x18,prefix,1);segment(0x18,prefix+1,3);
    assert(ASIXPoll(&poller,1,10)==1);u8 readback[4];assert(ASIXRecv(fd,readback,4)==4 && !memcmp(prefix,readback,4));
    block_output=1;unblock_at=host_ms+100;drop_next_data=1;
    u8 data[25000];for(unsigned base=0;base<250000;base+=sizeof(data)){for(unsigned i=0;i<sizeof(data);i++)data[i]=(u8)((base+i)*37+13);assert(ASIXSend(fd,data,sizeof(data))==sizeof(data));}
    for(unsigned i=0;i<200;i++)ASIXPoll(&poller,1,1);
    assert(stream_bytes==250000 && rejected_outputs>0 && dropped_segments==1);
    assert(ASIXSocket(AF_INET,SOCK_DGRAM,IPPROTO_IP)==UDP_FD);addr.sin_addr.s_addr=0xffffffff;addr.sin_port=lwip_htons(20582);
    assert(ASIXConnect(UDP_FD,(struct sockaddr*)&addr)==0);u8 ready[48]={0};memcpy(ready,"SLIP_READY",10);
    assert(ASIXSend(UDP_FD,ready,sizeof(ready))==48 && broadcasts==1);
    segment(0x14,NULL,0);assert(ASIXPoll(&poller,1,10)==1);assert(ASIXSend(fd,data,1)<0);assert(ASIXClose(fd)==0);
    peer_seq=2000;server_seq=0;segment(2,NULL,0);s32 next=ASIXAccept(SERVER_FD);assert(next!=fd);
    assert(ASIXRecv(fd,readback,4)<0);
    poller.socket=next;segment(0x11,NULL,0);assert(ASIXPoll(&poller,1,10)==1);assert(ASIXRecv(next,readback,4)==0);assert(ASIXClose(next)==0);assert(ASIXClose(SERVER_FD)==0);assert(ASIXClose(UDP_FD)==0);
    printf("PASS: automatic selection/fallback, signed USB IDs, config rejection, fragmented prefix, 250000 replay bytes, retransmission/backpressure, UDP discovery, RST/reconnect and FIN\n");return 0;
}
