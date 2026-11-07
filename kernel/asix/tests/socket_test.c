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
s32 asix_hw_init(struct netif *n){const u8 mac[6]={0x9c,0x69,0xd3,0x70,0x27,3};memcpy(n->hwaddr,mac,6);n->hwaddr_len=6;return 0;}
int asix_hw_failed(void){return 0;}
void asix_hw_shutdown(void){}
static unsigned boot_logs;
void asix_hw_boot_log(struct netif *n){(void)n;boot_logs++;}
void asix_hw_poll(struct netif *n){
    if(block_output && host_ms>=unblock_at)block_output=0;
    if(!count)return;unsigned slot=head;head=(head+1)%64;count--;
    struct pbuf *p=pbuf_alloc(PBUF_RAW,sizes[slot],PBUF_POOL);assert(p);assert(pbuf_take(p,incoming[slot],sizes[slot])==ERR_OK);assert(n->input(p,n)==ERR_OK);
}
static int dhcp_serving,drop_discovers,drop_renewals,dhcp_nak,address_conflict;
static unsigned discovers,requests,renewals,rebinds,declines;
static u32 offered_dns=0xc0a88901;
static void dhcp_reply(const u8 *query,unsigned length,u8 type,int bad_xid) {
    assert(length>=240);u8 msg[320];memset(msg,0,sizeof(msg));
    msg[0]=2;msg[1]=1;msg[2]=6;memcpy(msg+4,query+4,4);
    if(bad_xid)msg[7]^=1;
    w32(msg+16,0xc0a889fa);w32(msg+20,0xc0a88901);memcpy(msg+28,nic.hwaddr,6);w32(msg+236,0x63825363);
    unsigned o=240;msg[o++]=53;msg[o++]=1;msg[o++]=type;
    u8 keys[]={54,51,1,3,6,58,59};u32 vals[]={0xc0a88901,600,0xffffff00,0xc0a88901,offered_dns,60,120};
    for(unsigned i=0;i<7;i++){msg[o++]=keys[i];msg[o++]=4;w32(msg+o,vals[i]);o+=4;}msg[o++]=255;
    u8 *ip=new_frame(42+o,0x800)+14,*udp=ip+20;ip[0]=0x45;ip[8]=64;ip[9]=17;
    w16(ip+2,28+o);w32(ip+12,0xc0a88901);w32(ip+16,0xffffffff);w16(ip+10,checksum(ip,20,0));
    w16(udp,67);w16(udp+2,68);w16(udp+4,8+o);memcpy(udp+8,msg,o);
}
static void serve_dhcp(const u8 *ip,const u8 *udp,unsigned length) {
    assert(b16(udp)==68 && length>=240);const u8 *msg=udp+8;
    assert(msg[0]==1 && b32(msg+236)==0x63825363 && !memcmp(msg+28,nic.hwaddr,6));
    unsigned type=0;
    for(unsigned i=240;i<length;) {
        unsigned key=msg[i++];if(key==255)break;if(!key)continue;
        assert(i<length);unsigned size=msg[i++];assert(size<=length-i);
        if(key==53 && size==1)type=msg[i];i+=size;
    }
    if(type==1 || type==3)assert(b16(msg+10)==(b32(msg+12)?0:0x8000));
    if(type==1) {
        discovers++;if(!dhcp_serving)return;
        if(drop_discovers){drop_discovers--;return;}
        dhcp_reply(msg,length,2,1); /* wrong transaction must not be accepted */
        dhcp_reply(msg,length,2,0);
    } else if(type==3) {
        requests++;if(!dhcp_serving)return;
        if(b32(msg+12)) {
            if(b32(ip+16)==0xffffffff)rebinds++;
            else {renewals++;if(drop_renewals)return;}
        }
        dhcp_reply(msg,length,dhcp_nak?6:5,0);dhcp_nak=0;
    } else if(type==4)declines++;
    else assert(type==7);
}
err_t asix_hw_output(struct netif *n,struct pbuf *p){
    (void)n;if(block_output){rejected_outputs++;return ERR_MEM;}
    u8 f[1514];assert(p->tot_len<=sizeof(f));pbuf_copy_partial(p,f,p->tot_len,0);
    if(b16(f+12)==0x806){
        if(b16(f+20)==1){u8 *a=new_frame(42,0x806)+14;w16(a,1);w16(a+2,0x800);a[4]=6;a[5]=4;w16(a+6,2);memcpy(a+8,peer_mac,6);w32(a+14,0xc0a88901);if(address_conflict && b32(f+28)==0 && b32(f+38)==0xc0a889fa){w32(a+14,0xc0a889fa);address_conflict--;}memcpy(a+18,nic.hwaddr,6);w32(a+24,0xc0a889fa);}return ERR_OK;
    }
    assert(b16(f+12)==0x800);u8 *ip=f+14;unsigned ih=(ip[0]&15)*4;assert(checksum(ip,ih,0)==0);
    if(ip[9]==17) {
        assert(!(b16(ip+6)&0x3fff));
        u8 *udp=ip+ih;unsigned length=b16(udp+4)-8;
        if(b16(udp+2)==67){serve_dhcp(ip,udp,length);return ERR_OK;}
        if(b16(udp+2)==20582){assert(b32(ip+16)==0xffffffff && length==48);assert(!memcmp(udp+8,"SLIP_READY",10));broadcasts++;}
        else assert(!"unexpected UDP destination");
        return ERR_OK;
    }
    if(ip[9]!=6)return ERR_OK;
    u8 *t=ip+ih;unsigned th=(t[12]>>4)*4,len=b16(ip+2)-ih-th;
    assert(checksum(t,th+len,6+th+len+0xc0a8+0x89fa+0xc0a8+0x8901)==0);
    if(t[13]&4)return ERR_OK;
    if((t[13]&0x12)==0x12){synacks++;server_seq=b32(t+4)+1;segment(0x10,NULL,0);}
    if(len && drop_next_data){drop_next_data=0;dropped_segments++;return ERR_OK;}
    if(len){u32 seq=b32(t+4);if(seq>server_seq){segment(0x10,NULL,0);return ERR_OK;}if(seq<server_seq)retransmits++;else {for(unsigned i=0;i<len;i++)assert(t[th+i]==(u8)((stream_bytes+i)*37+13));stream_bytes+=len;server_seq+=len;}segment(0x10,NULL,0);}
    return ERR_OK;
}
int main(int argc,char **argv){
    if(argc>1) {
        missing_config=1;dhcp_serving=strcmp(argv[1],"timeout")!=0;drop_discovers=1;
        address_conflict=!strcmp(argv[1],"conflict");
        s32 result=ASIXTryInit();
        if(dhcp_serving) {
            assert(result==1 && ASIXIsActive() && NetworkStarted && use_dhcp && discovers>=2);
            assert(current_ip_address==lwip_htonl(0xc0a889fa) && boot_logs==1);
            assert(ip4_addr_get_u32(ip_2_ip4(dns_getserver(0)))==lwip_htonl(0xc0a88901));
            if(!strcmp(argv[1],"conflict")) { assert(declines>0);puts("PASS DHCP boot: conflicting offered address is declined before retrying"); }
            puts("PASS DHCP boot: no configuration file, full automatic initialization, retry, address and DNS");
        } else {
            assert(result==-106 && !ASIXIsActive() && !NetworkStarted && current_ip_address==0 && boot_logs==1);
            puts("PASS DHCP boot: no server gives bounded timeout and diagnostics, without activating network");
        }
        return 0;
    }
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
    config_text="ip=192.168.137.250\nnetmask=255.255.255.0\ndns=224.0.0.1\n";assert(load_config(&ip,&mask,&gw)<0);
    config_text="ip=192.168.137.250\nnetmask=255.255.255.0\ngateway=192.168.137.1\ndns=1.1.1.1\n";assert(load_config(&ip,&mask,&gw)==0 && ip4_addr_get_u32(&dns_server)==lwip_htonl(0x01010101));
    config_text="invalid";config_reads=0;detected=0;
    assert(ASIXTryInit()==0 && !ASIXIsActive() && config_reads==0);
    detected=-5;assert(ASIXTryInit()==0 && !ASIXIsActive() && config_reads==0);
    detected=1;assert(ASIXTryInit()==-101 && !ASIXIsActive());
    missing_config=1;assert(load_config(&ip,&mask,&gw)==0 && use_dhcp);
    missing_config=0;config_text="dhcp=1\n";assert(load_config(&ip,&mask,&gw)==0 && use_dhcp);
    config_text="dhcp=1\nip=192.168.137.250\n";assert(load_config(&ip,&mask,&gw)<0);
    config_text="dhcp=2\n";assert(load_config(&ip,&mask,&gw)<0);
    config_text="dhcp=1\ndhcp=1\n";assert(load_config(&ip,&mask,&gw)<0);
    config_text=good;assert(ASIXTryInit()==1 && ASIXIsActive());
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
    use_dhcp=1;netif_set_addr(&nic,IP4_ADDR_ANY4,IP4_ADDR_ANY4,IP4_ADDR_ANY4);
    dhcp_serving=1;drop_discovers=1;dhcp_nak=1;
    assert(acquire_dhcp()==0 && discovers>=3 && requests>=2 && dhcp_supplied_address(&nic));
    assert(current_ip_address==lwip_htonl(0xc0a889fa));
    assert(ip4_addr_get_u32(netif_ip4_netmask(&nic))==lwip_htonl(0xffffff00));
    assert(ip4_addr_get_u32(netif_ip4_gw(&nic))==lwip_htonl(0xc0a88901));
    assert(ip4_addr_get_u32(ip_2_ip4(dns_getserver(0)))==lwip_htonl(offered_dns));
    offered_dns=0x08080808;host_ms+=60000;
    for(unsigned i=0;i<8;i++)ASIXUpdate();
    assert(renewals>0 && dhcp_supplied_address(&nic));
    assert(ip4_addr_get_u32(ip_2_ip4(dns_getserver(0)))==lwip_htonl(offered_dns));
    drop_renewals=1;
    for(unsigned step=0;step<3;step++){host_ms+=60000;for(unsigned i=0;i<8;i++)ASIXUpdate();}
    assert(rebinds>0 && dhcp_supplied_address(&nic));
    /* Loss of the server cannot leave the expired address advertised. */
    dhcp_serving=0;struct dhcp *lease=netif_dhcp_data(&nic);lease->t0_timeout=lease->lease_used+1;
    host_ms+=60000;ASIXUpdate();assert(!dhcp_supplied_address(&nic) && current_ip_address==0);
    dhcp_stop(&nic);netif_set_addr(&nic,IP4_ADDR_ANY4,IP4_ADDR_ANY4,IP4_ADDR_ANY4);
    u32 began=sys_now();assert(acquire_dhcp()==-106 && (u32)(sys_now()-began)>=DHCP_WAIT_MS && current_ip_address==0);
    puts("PASS DHCP: discover loss/retry, invalid transaction rejection, NAK recovery, address/mask/router/DNS, renewal, rebinding, lease expiry and startup timeout");
    printf("PASS: automatic selection/fallback, signed USB IDs, config rejection, fragmented prefix, 250000 replay bytes, retransmission/backpressure, UDP discovery, RST/reconnect and FIN\n");return 0;
}
