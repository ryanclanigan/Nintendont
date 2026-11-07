/* SPDX-License-Identifier: GPL-2.0-or-later
 * Serialized NO_SYS lwIP backend for Slippi's TCP listener and UDP discovery.
 * All raw API calls, callbacks and USB completions run under one IOS queue
 * mutex. Waiters release it before sleeping so broadcast and streaming threads
 * cannot enter lwIP concurrently or starve one another.
 */
#include "Config.h"
#include "debug.h"
#include "asix.h"
#include "hardware.h"
#include "lwip/init.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#define SERVER_FD 1
#define UDP_FD 2
#define RX_SIZE 16384
#define WAIT_MS 5000
void *asix_lwip_heap;
void asix_assert(const char *message) {dbgprintf("ASIX assertion: %s\n",message);Shutdown();}
static int active;
int ASIXIsActive(void) {return active;}
static struct netif nic;
static struct tcp_pcb *server, *client;
static struct udp_pcb *discovery;
static int server_open, udp_open, accepted, dead;
static s32 client_fd=3, mutex=-1;
static u32 mutex_store[1];
static struct ipcmessage token;
static u8 received[RX_SIZE];
static unsigned rx_head,rx_count;
static u32 clock_ticks,clock_ms,clock_remainder;
extern s32 top_fd;
extern u32 current_ip_address,NetworkStarted;
extern u8 wifi_mac_address[6];

u32 sys_now(void) {
    u32 ticks=read32(HW_TIMER),delta=ticks-clock_ticks;
    clock_ticks=ticks;
    /* 1 tick = 0.5267 us; retain sub-ms precision and handle counter wrap. */
    u64 scaled=(u64)delta*5267+clock_remainder;
    clock_ms+=scaled/10000000;clock_remainder=scaled%10000000;
    return clock_ms;
}
u32 asix_random(void) { u32 r=read32(HW_TIMER)^0x61736978;IOSC_GenerateRand((u8*)&r,4);return r; }
static void lock(void) {struct ipcmessage *m;mqueue_recv(mutex,&m,0);}
static void unlock(void) {mqueue_send(mutex,&token,0);}
static void pump(void) {asix_hw_poll(&nic);sys_check_timeouts();}
static void pause_unlocked(void) {unlock();mdelay(1);lock();}
static int expired(u32 start) {return (u32)(sys_now()-start)>=WAIT_MS;}
static int valid_client(s32 s) {return s==client_fd && accepted;}
static void client_error(void *arg,err_t err) {(void)arg;(void)err;client=NULL;dead=1;}
static err_t client_receive(void *arg,struct tcp_pcb *pcb,struct pbuf *p,err_t err) {
    (void)arg;(void)pcb;
    if(!p) {dead=1;return ERR_OK;}
    if(err!=ERR_OK)return err;
    if(p->tot_len>RX_SIZE-rx_count)return ERR_MEM;
    unsigned offset=0;
    for(struct pbuf *q=p;q;q=q->next)for(unsigned i=0;i<q->len;i++)
        received[(rx_head+rx_count+offset++)%RX_SIZE]=((u8*)q->payload)[i];
    rx_count+=p->tot_len;pbuf_free(p);
    /* Window credit is returned only when the application consumes bytes. */
    return ERR_OK;
}
static err_t client_accept(void *arg,struct tcp_pcb *pcb,err_t err) {
    (void)arg;
    if(err!=ERR_OK)return err;
    if(client || accepted) {tcp_abort(pcb);return ERR_ABRT;}
    client=pcb;dead=0;rx_head=rx_count=0;
    tcp_recv(pcb,client_receive);tcp_err(pcb,client_error);
    tcp_nagle_disable(pcb);
    return ERR_OK;
}
static void drop_client(void) {
    if(client) {tcp_recv(client,NULL);tcp_err(client,NULL);tcp_arg(client,NULL);tcp_abort(client);client=NULL;}
    accepted=0;dead=0;rx_head=rx_count=0;
    /* Old descriptors must not become valid on the next connection. */
    if(++client_fd<3)client_fd=3;
}
static err_t netif_setup(struct netif *n) {
    n->name[0]='a';n->name[1]='x';n->mtu=1500;
    n->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP;
    n->output=etharp_output;n->linkoutput=asix_hw_output;
    return ERR_OK;
}
static char *trim(char *s) {
    while(*s==' ' || *s=='\t')s++;
    unsigned n=strlen(s);while(n && (s[n-1]==' ' || s[n-1]=='\t' || s[n-1]=='\r'))s[--n]=0;
    return s;
}
static int load_config(ip4_addr_t *ip,ip4_addr_t *mask,ip4_addr_t *gw) {
    FIL file;u32 size;static char text[512];
    if(f_open_main_drive(&file,"/slippi_asix.ini",FA_READ|FA_OPEN_EXISTING)!=FR_OK)return -1;
    FRESULT rc=f_read(&file,text,sizeof(text)-1,&size);f_close(&file);
    if(rc!=FR_OK || size>=sizeof(text)-1)return -1;
    text[size]=0;unsigned seen=0;
    ip4_addr_set_zero(gw);
    for(char *line=text;line && *line;) {
        char *next=strchr(line,'\n');if(next)*next++=0;
        line=trim(line);
        if(*line && *line!='#' && *line!=';') {
            char *eq=strchr(line,'=');if(!eq)return -1;*eq++=0;
            char *key=trim(line),*value=trim(eq);ip4_addr_t *target;unsigned bit;
            if(!strcmp(key,"ip")){target=ip;bit=1;}
            else if(!strcmp(key,"netmask")){target=mask;bit=2;}
            else if(!strcmp(key,"gateway")){target=gw;bit=4;}
            else return -1;
            if((seen&bit) || !ip4addr_aton(value,target))return -1;
            seen|=bit;
        }
        line=next;
    }
    u32 addr=lwip_ntohl(ip4_addr_get_u32(ip)),m=lwip_ntohl(ip4_addr_get_u32(mask));
    u32 gateway=lwip_ntohl(ip4_addr_get_u32(gw)),host=addr&~m;
    if((seen&3)!=3 || !m || m==0xffffffff || ((~m)&((~m)+1)) || !host || host==~m ||
       !(addr>>24) || (addr>>24)==127 || (addr>>24)>=224)return -1;
    if(gateway && (!(gateway>>24) || (gateway>>24)==127 || (gateway>>24)>=224 || (gateway&m)!=(addr&m) || gateway==addr || !(gateway&~m) || (gateway&~m)==~m))return -1;
    return 0;
}
s32 ASIXTryInit(void) {
    if(asix_hw_probe()!=1)return 0;
    s32 result=ASIXInit();
    return result<0?result:1;
}
s32 ASIXInit(void) {
    ip4_addr_t ip,mask,gw;
    if(load_config(&ip,&mask,&gw)) {dbgprintf("ASIX: missing or invalid /slippi_asix.ini\n");return -101;}
    mutex=mqueue_create(mutex_store,1);if(mutex<0)return -102;
    mqueue_send(mutex,&token,0);clock_ticks=read32(HW_TIMER);
    asix_lwip_heap=heap_alloc_aligned(0,128*1024+64,32);
    if(!asix_lwip_heap)return -104;
    lwip_init();
    if(!netif_add(&nic,&ip,&mask,&gw,NULL,netif_setup,ethernet_input))return -103;
    s32 result=asix_hw_init(&nic);
    if(result<0){asix_hw_boot_log(&nic);return result;}
    netif_set_default(&nic);netif_set_up(&nic);netif_set_link_up(&nic);
    memcpy(wifi_mac_address,nic.hwaddr,6);current_ip_address=ip4_addr_get_u32(&ip);
    /* Exercise the actual Starlet data path before game/disc threads start.
     * Diagnostics are written here so FatFS is not called from socket threads. */
    etharp_request(&nic,&gw);
    u32 preflight=sys_now();
    while((u32)(sys_now()-preflight)<3000) {pump();mdelay(1);}
    asix_hw_boot_log(&nic);
    top_fd=0;NetworkStarted=1;active=1;
    dbgprintf("ASIX: static IP %08x, Slippi TCP 51441\n",current_ip_address);
    return 0;
}
s32 ASIXSocket(u32 domain,u32 type,u32 protocol) {
    if(domain!=AF_INET || (protocol!=IPPROTO_IP && protocol!=IPPROTO_TCP))return -1;
    lock();s32 result=-1;
    if(type==SOCK_STREAM && !server_open) {server=tcp_new();if(server){server_open=1;result=SERVER_FD;}}
    else if(type==SOCK_DGRAM && protocol==IPPROTO_IP && !udp_open) {discovery=udp_new();if(discovery){udp_open=1;ip_set_option(discovery,SOF_BROADCAST);result=UDP_FD;}}
    unlock();return result;
}
s32 ASIXBind(s32 s,const struct sockaddr *addr) {
    const struct sockaddr_in *a=(const struct sockaddr_in*)addr;
    lock();s32 result=-1;
    if(s==SERVER_FD && server_open && server && a->sin_family==AF_INET && (!a->sin_addr.s_addr || a->sin_addr.s_addr==current_ip_address))
        result=tcp_bind(server,IP_ANY_TYPE,lwip_ntohs(a->sin_port));
    unlock();return result;
}
s32 ASIXListen(s32 s,u32 backlog) {
    (void)backlog;lock();s32 result=-1;
    if(s==SERVER_FD && server_open && server) {
        err_t err;struct tcp_pcb *p=tcp_listen_with_backlog_and_err(server,1,&err);
        if(p){server=p;tcp_accept(server,client_accept);result=0;}
    }
    unlock();return result;
}
s32 ASIXAccept(s32 s) {
    lock();
    for(;;) {
        if(s!=SERVER_FD || !server_open || asix_hw_failed()){unlock();return -1;}
        pump();
        if(client && !accepted) {accepted=1;s32 result=client_fd;unlock();return result;}
        pause_unlocked();
    }
}
s32 ASIXClose(s32 s) {
    lock();s32 result=0;
    if(valid_client(s))drop_client();
    else if(s==SERVER_FD && server_open) {if(server)tcp_close(server);server=NULL;server_open=0;if(client)drop_client();}
    else if(s==UDP_FD && udp_open) {udp_remove(discovery);discovery=NULL;udp_open=0;}
    else result=-1;
    unlock();return result;
}
s32 ASIXConnect(s32 s,const struct sockaddr *addr) {
    const struct sockaddr_in *a=(const struct sockaddr_in*)addr;
    lock();s32 result=-1;
    if(s==UDP_FD && udp_open && a->sin_family==AF_INET) {
        ip_addr_t dest;ip_addr_set_ip4_u32(&dest,a->sin_addr.s_addr);
        result=udp_connect(discovery,&dest,lwip_ntohs(a->sin_port));
    }
    unlock();return result;
}
s32 ASIXSend(s32 s,const void *data,s32 len) {
    if(len<0 || (!data && len))return -1;
    lock();u32 start=sys_now();
    if(s==UDP_FD && udp_open) {
        s32 result=-1;
        if(len<=1472) {struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,len,PBUF_RAM);if(p){pbuf_take(p,data,len);result=udp_send(discovery,p)==ERR_OK?len:-1;pbuf_free(p);}}
        pump();unlock();return result;
    }
    s32 sent=0;
    while(sent<len) {
        pump();
        if(!valid_client(s) || !client || dead || asix_hw_failed() || expired(start)){unlock();return -1;}
        u16_t n=tcp_sndbuf(client);if(n>len-sent)n=len-sent;
        err_t err=n?tcp_write(client,(const u8*)data+sent,n,TCP_WRITE_FLAG_COPY):ERR_MEM;
        if(err==ERR_OK){sent+=n;tcp_output(client);}
        else if(err!=ERR_MEM){unlock();return -1;}
        else pause_unlocked();
    }
    unlock();return sent;
}
s32 ASIXRecv(s32 s,void *data,s32 len) {
    if(len<=0 || !data)return -1;
    lock();u32 start=sys_now();
    /* Slippi currently requires its four-byte message prefix in one read.
     * Collect that prefix, while larger body reads may return partial data. */
    unsigned minimum=len<4?len:4;
    while(rx_count<minimum && valid_client(s) && client && !dead && !asix_hw_failed() && !expired(start)) {pump();if(rx_count<minimum)pause_unlocked();}
    if(!valid_client(s) || asix_hw_failed()){unlock();return -1;}
    if(!rx_count){s32 result=dead?0:-1;unlock();return result;}
    unsigned n=rx_count<(unsigned)len?rx_count:(unsigned)len;
    for(unsigned i=0;i<n;i++)((u8*)data)[i]=received[(rx_head+i)%RX_SIZE];
    rx_head=(rx_head+n)%RX_SIZE;rx_count-=n;
    if(client){tcp_recved(client,n);tcp_output(client);}
    unlock();return n;
}
s32 ASIXSetSockOpt(s32 s,u32 level,u32 option,const void *value,u32 len) {
    lock();s32 result=-1;
    if(valid_client(s) && client && level==IPPROTO_TCP && option==TCP_NODELAY && value && len==4){if(*(const u32*)value)tcp_nagle_disable(client);else tcp_nagle_enable(client);result=0;}
    unlock();return result;
}
s32 ASIXPoll(struct pollsd *sds,s32 count,s32 timeout) {
    if(!sds || count<1 || count>16)return -1;
    lock();u32 start=sys_now();
    for(;;) {
        pump();s32 ready=0;
        for(s32 i=0;i<count;i++) {
            s32 s=sds[i].socket;u32 events=0;
            if(valid_client(s)) {
                if(rx_count || dead)events|=POLLIN;
                if(client && !dead && tcp_sndbuf(client))events|=POLLOUT;
                if(dead)events|=POLLHUP;
            } else if(s==SERVER_FD && server_open){if(client && !accepted)events|=POLLIN;}
            else if(s==UDP_FD && udp_open)events|=POLLOUT;
            else events|=POLLNVAL;
            if(asix_hw_failed())events|=POLLERR;
            sds[i].revents=events&(sds[i].events|POLLERR|POLLHUP|POLLNVAL);
            if(sds[i].revents)ready++;
        }
        if(ready || (timeout>=0 && (u32)(sys_now()-start)>=(u32)timeout)){unlock();return ready;}
        pause_unlocked();
    }
}
void ASIXUpdate(void) {lock();pump();unlock();}
void ASIXShutdown(void) {asix_hw_shutdown();}
