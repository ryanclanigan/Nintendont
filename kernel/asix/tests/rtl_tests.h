/* SPDX-License-Identifier: GPL-2.0-only */
static void rtl_packets(struct netif *n) {
    n->input=test_input;family=RTL_8152;
    u8 frame[1514];for(unsigned i=0;i<sizeof(frame);i++)frame[i]=i^0x5a;
    /* All frame sizes, including USB packet and eight-byte alignment edges. */
    for(unsigned len=14;len<=1514;len++) {
        unsigned end=24+len+4,padded=(end+7)&~7u;
        memset(rx_data,0,padded);put32(rx_data,len+4);memcpy(rx_data+24,frame,len);
        int before=input_frames;receive(n,end);
        assert(input_frames==before+1 && !memcmp(frame_copy,frame,len));
        receive(n,padded);assert(input_frames==before+2);
        /* CRC status discards just that frame. */
        put32(rx_data,(len+4)|0x8000);receive(n,padded);assert(input_frames==before+2);
        tx_count=tx_head=0;failed=0;
        struct pbuf *p=pbuf_alloc(PBUF_RAW,len,PBUF_RAM);assert(p);
        assert(pbuf_take(p,frame,len)==ERR_OK && asix_hw_output(n,p)==ERR_OK);pbuf_free(p);
        unsigned wire=len<60?60:len;
        assert(tx_len[0]==wire+8 && le32(tx_data[0])==(wire|0xc0000000u));
        assert(!le32(tx_data[0]+4) && !memcmp(tx_data[0]+8,frame,len));
        for(unsigned i=len;i<wire;i++)assert(!tx_data[0][8+i]);
    }
    /* Two frames: malformed second descriptor must prevent partial delivery. */
    memset(rx_data,0,176);put32(rx_data,64);memcpy(rx_data+24,frame,60);
    put32(rx_data+88,64);memcpy(rx_data+112,frame,60);
    int before=input_frames;receive(n,176);assert(input_frames==before+2);
    put32(rx_data+88,0x7fff);unsigned bad=diag.bad_groups;receive(n,176);
    assert(input_frames==before+2 && diag.bad_groups==bad+1);
    /* Every truncated prefix is rejected except a complete first frame. */
    put32(rx_data+88,64);
    for(unsigned len=1;len<176;len++) {
        before=input_frames;bad=diag.bad_groups;receive(n,len);
        if(len==88)assert(input_frames==before+1);
        else assert(input_frames==before && diag.bad_groups==bad+1);
    }
    put32(rx_data,17);bad=diag.bad_groups;receive(n,88);assert(diag.bad_groups==bad+1);
    put32(rx_data,1519);bad=diag.bad_groups;receive(n,88);assert(diag.bad_groups==bad+1);
    /* Bounded TX queue; no overwrite of a pending slot. */
    tx_count=TX_SLOTS;struct pbuf *p=pbuf_alloc(PBUF_RAW,60,PBUF_RAM);
    assert(asix_hw_output(n,p)==ERR_MEM);pbuf_free(p);tx_count=0;
}
static void rtl_tests(struct netif *n) {
    chip_vendor=0x0bda;chip_pid=0x8152;chip_revision=0x2000;probed=0;rtl_seed(0x4c10);
    assert(asix_identity(0x0bda,0x8152)->family==RTL_8152);
    assert(!asix_identity(0x0bda,0x8153) && !asix_identity(0x0bda,0x8156));
    assert(asix_hw_init(n)==0 && family==RTL_8152 && rtl_silicon==0x4c10);
    assert(link_speed==100 && link_duplex==1 && n->hwaddr[0]==2);
    assert(!memcmp(rtl_regs[1]+0xf800,rtl8152_patch,sizeof(rtl8152_patch)));
    for(unsigned i=0;i<sizeof(rtl8152_patch_bp)/2;i+=2)
        assert(le16(rtl_regs[1]+rtl8152_patch_bp[i])==rtl8152_patch_bp[i+1]);
    assert((le16(rtl_regs[0]+0xd406)&0x90)==0x10); /* aggregation/zero mode off */
    assert((le32(rtl_regs[1]+0xc010)&15)==10); /* unicast and broadcast */
    assert((rtl_regs[1][0xe813]&12)==12 && !(le16(rtl_regs[1]+0xe85a)&8));
    assert(le16(rtl_regs[1]+0xb408)==0x1e1 && le16(rtl_regs[1]+0xb400)==0x9200);
    asix_hw_boot_log(n);assert(strstr(saved_log,"RTL8152B") && strstr(saved_log,"silicon=4c10"));
    /* Each individual vendor operation fails closed on an error or short I/O. */
    int operations=rtl_calls;
    for(int i=1;i<=operations;i++) {
        rtl_seed(0x4c10);rtl_fail_at=i;probed=0;assert(asix_hw_init(n)<0);
        rtl_seed(0x4c10);rtl_short_at=i;probed=0;assert(asix_hw_init(n)<0);
    }
    /* Revision 7 uses no revision-2 patch. USB revision alone is not trusted. */
    rtl_seed(0x4800);probed=0;assert(asix_hw_init(n)==0 && rtl_silicon==0x4800);
    assert(!le32(rtl_regs[1]+0xf800));
    u16 unsupported[]={0,0xffff,0x4c00,0x5c10,0x6000};
    for(unsigned i=0;i<sizeof(unsupported)/2;i++) {
        rtl_seed(unsupported[i]);probed=0;assert(asix_hw_init(n)==-12 && rtl_calls==1);
    }
    rtl_seed(0x4c10);rtl_regs[1][0xc000]=1;probed=0;assert(asix_hw_init(n)==-8 && rtl_calls==2);
    rtl_seed(0x4c10);memset(rtl_regs[1]+0xc000,0,8);probed=0;assert(asix_hw_init(n)==-8);
    rtl_seed(0x4800);rtl_stuck=1;probed=0;u32 start=host_ms;
    assert(asix_hw_init(n)==-9 && host_ms-start<1000);
    rtl_seed(0x4800);rtl_no_ll=1;probed=0;start=host_ms;
    assert(asix_hw_init(n)==-9 && host_ms-start<1000);
    rtl_seed(0x4800);rtl_regs[1][0xe908]=0;probed=0;start=host_ms;
    assert(asix_hw_init(n)==-9 && host_ms-start>=15000 && host_ms-start<16000);
    rtl_seed(0x4800);rtl_regs[1][0xe908]=2;probed=0;
    assert(asix_hw_init(n)==0 && link_speed==10 && !link_duplex);
    rtl_seed(0x4800);rtl_multi=1;current_config=1;probed=0;
    assert(asix_hw_init(n)==0 && current_config==2);rtl_multi=0;
    rtl_seed(0x4800);rtl_cdc_only=1;probed=0;assert(asix_hw_init(n)==-6 && !rtl_calls);rtl_cdc_only=0;
    rtl_seed(0x4800);probed=0;assert(asix_hw_init(n)==0);
    /* Word/byte lanes must not damage adjacent read-only/command bytes. */
    rtl_regs[1][0xe813]=0xaa;rtl_regs[1][0xe812]=0xbb;
    assert(rtl_write(device,RTL_PLA,0xe813,1,0x55)==0 && rtl_regs[1][0xe812]==0xbb && rtl_regs[1][0xe813]==0x55);
    assert(rtl_write(device,RTL_PLA,0xe612,2,0x1234)==0 && le16(rtl_regs[1]+0xe612)==0x1234);
    rtl_seed(0x4800);rtl_full_speed=1;probed=0;
    assert(asix_hw_init(n)==0 && max_packet==64);
    assert(le32(rtl_regs[1]+0xc0a4)==0x60 && le32(rtl_regs[1]+0xc0a8)==0x78);
    rtl_packets(n);rtl_full_speed=0;
    rtl_seed(0x4800);probed=0;assert(asix_hw_init(n)==0 && max_packet==512);
    rtl_packets(n);
    puts("PASS RTL8152B: revisions 2/7, ID/silicon guards, byte lanes, volatile patch, all control error/short-I/O points, bounded reset/link timeouts, vendor configuration selection, RX/FCS/CRC/malformed aggregates, all 14..1514 TX/RX lengths");
}
