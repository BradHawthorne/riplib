#include "ripscrip.h"
#include "ripscrip2.h"
#include "drawing.h"
#include "riplib_version.h"
#include "rip_icn.h"
#include "rip_icons.h"
#include "bgi_font.h"
#include <cstring>
#include <cstdio>

extern "C" int consumer_sent(void);
static rip_state_t session;
static uint8_t framebuffer[640*400];
static int transfers;
static void transfer(void *user, const rip_block_transfer_t *request) {
    if (user==&transfers && request->pending && request->protocol==1 &&
        std::strcmp(request->filename,"demo.icn")==0) ++transfers;
}
int main() {
    comp_context_t ctx={};
    ctx.target=framebuffer;
    draw_init(framebuffer,640,640,400);
    rip_init_first(&session);
    ripscrip2_init(&session.rip2_state);
    bgi_font_set_char_spacing(100);
    uint16_t width=0, height=0;
    const uint8_t invalid_icon[1]={0};
    if (rip_icn_measure(invalid_icon,1,&width,&height) ||
        rip_icon_pending_requests(&session.icon_state)!=0) return 2;
    rip_set_transfer_handler(&session,transfer,&transfers);
    const char *wire="!|2\x1b" "0000CACHE|3\x1b" "01020000demo.icn<>|2R0000redraw|";
    for(const char *p=wire;*p;++p) rip_process(&session,&ctx,(uint8_t)*p);
    bool valid=std::strcmp(session.host_directory,"CACHE")==0 && transfers==1 &&
               consumer_sent()==0 && rip_request_refresh(&session) && consumer_sent()==1;
    psram_arena_destroy(&session.psram_arena);
    if (!valid) return 1;
    std::puts("C++ consumer linked and exercised the C host-service API");
    return 0;
}
