/* Exercise the actual runtime input hook with deterministic OS/device samples. */
#define HFR_NO_UI
#include "../src/ui/ui_api.h"
#include <assert.h>
#include <stdio.h>
static int test_keys[256],test_foreground=1,test_menu;
static POINT test_cursor={1584,1200};
static RECT test_client={0,0,2560,1440};
static unsigned test_native;
static uintptr_t test_argument;
static LONGLONG test_clock=360000;
static BOOL WINAPI fake_counter(LARGE_INTEGER* p) {p->QuadPart=test_clock;return TRUE;}
static SHORT WINAPI fake_async(int key) {return test_keys[key&255]?(SHORT)0x8000:0;}
static HWND WINAPI fake_foreground(void) {return (HWND)(uintptr_t)test_foreground;}
static BOOL WINAPI fake_cursor(LPPOINT p) {*p=test_cursor;return TRUE;}
static BOOL WINAPI fake_screen(HWND w,LPPOINT p) {(void)w;(void)p;return TRUE;}
static BOOL WINAPI fake_set_cursor(int x,int y) {test_cursor=(POINT){x,y};return TRUE;}
static BOOL WINAPI fake_client(HWND w,LPRECT r) {(void)w;*r=test_client;return TRUE;}
static int fake_menu(void) {return test_menu;}
#define GetAsyncKeyState fake_async
#define GetForegroundWindow fake_foreground
#define GetCursorPos fake_cursor
#define ScreenToClient fake_screen
#define ClientToScreen fake_screen
#define SetCursorPos fake_set_cursor
#define QueryPerformanceCounter fake_counter
#define GetClientRect fake_client
#define hfr_menu_visible fake_menu
#include "../src/hfr64.c"
void hfr_d3d11_overlay(void* p) {(void)p;}
static uint32_t native_poll(uintptr_t p) {test_argument=p;return test_native;}
static uint32_t probe_detour(uintptr_t p) {return (uint32_t)p ^ 0x12345678u;}
static void test_real_prologue(const char* path) {
    FILE* f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>4096);
    rewind(f);unsigned char* file=malloc((size_t)n);assert(file);assert(fread(file,1,(size_t)n,f)==(size_t)n);fclose(f);
    IMAGE_DOS_HEADER* dos=(void*)file;assert(dos->e_magic==IMAGE_DOS_SIGNATURE);
    IMAGE_NT_HEADERS64* nt=(void*)(file+dos->e_lfanew);
    assert(nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 && nt->OptionalHeader.SizeOfImage==game->image_size);
    unsigned char* image=VirtualAlloc(NULL,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(image);
    memcpy(image,file,nt->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER* section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(image+section[i].VirtualAddress,file+section[i].PointerToRawData,section[i].SizeOfRawData);
    unsigned char saved[32];memcpy(saved,image+game->input_poll,sizeof saved);
    DWORD old;assert(VirtualProtect(image,nt->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READ,&old));
    PollFn trampoline=NULL;assert(MH_Initialize()==MH_OK);
    assert(MH_CreateHook(image+game->input_poll,probe_detour,(void**)&trampoline)==MH_OK && trampoline);
    assert(MH_EnableHook(image+game->input_poll)==MH_OK);
    /* Only the replacement is executed. No original game code or imports run. */
    assert(((PollFn)(image+game->input_poll))(987)==(987u^0x12345678u));
    assert(MH_DisableHook(image+game->input_poll)==MH_OK);
    assert(!memcmp(saved,image+game->input_poll,sizeof saved));
    assert(MH_Uninitialize()==MH_OK);VirtualFree(image,0,MEM_RELEASE);free(file);
    puts("PASS: MinHook installs on the actual game's input-function bytes, dispatches the replacement and restores the original bytes");
}
static unsigned poll(void) {return mouse_poll(123);}
static void fixture(void) {
    memset(test_keys,0,sizeof test_keys);test_native=0;test_menu=0;test_foreground=1;
    test_cursor=(POINT){1584,1200};test_client=(RECT){0,0,2560,1440};
    mouse_enabled=mouse_supported=1;mouse_bomb_button=1;mouse_toggle_held=1;
    mouse_direct=0;mouse_anchor_ready=0;guard_failed=0;
    mouse_speed_limit=0;frequency=360000;speed_pct=100;test_clock=360000;mouse_sample_time=0;
    mouse_buttons.blocked=7;mouse_window_handle=(HWND)1;mouse_poll_original=native_poll;
    subtick_player.armed=proj_slice.armed=1;
    const float pos[]={192,384},bounds[]={8,16,368,416},speeds[]={4,2,2.82842712f,1.41421356f};
    memcpy((void*)(base+game->player+game->pl_position),pos,sizeof pos);
    memcpy((void*)(base+game->bounds),bounds,sizeof bounds);
    memcpy((void*)(base+game->player+game->pl_speed_straight),speeds,sizeof speeds);
    float* scale=(float*)(base+game->player+game->pl_scale);scale[0]=scale[1]=1;
    float* origin=(float*)(base+0x53cac0);origin[0]=236;origin[1]=16;
    *(int*)(base+0xc6e064)=2560;*(int*)(base+0xc6e068)=1440;
    poll(); /* release initial button/toggle latches */
}
int main(int argc,char** argv) {
    game=&th06nc_0914_game;base=(uintptr_t)calloc(1,game->image_size);assert(base);
    fixture();assert(poll()==SUBTICK_RIGHT && test_argument==123);
    assert(mouse_target[0]==292 && mouse_target[1]==384);
    float* pos=(float*)(base+game->player+game->pl_position);
    assert(pos[0]==192 && pos[1]==384); /* no position writes */
    test_keys[VK_LBUTTON]=1;assert((poll()&7)==1);
    test_keys[VK_RBUTTON]=1;assert((poll()&7)==5);
    test_keys[VK_XBUTTON1]=1;assert((poll()&7)==7);
    unsigned previous=0,bombs=0;
    for(int i=0;i<60;++i) {unsigned p=poll();bombs+=!!((p&~previous)&2);previous=p;}
    assert(bombs==1);test_keys[VK_XBUTTON1]=0;previous=poll();test_keys[VK_XBUTTON1]=1;
    assert(((poll()&~previous)&2)!=0);
    mouse_bomb_button=2;test_keys[VK_XBUTTON1]=1;test_keys[VK_XBUTTON2]=0;assert(!(poll()&2));
    test_keys[VK_XBUTTON2]=1;assert(poll()&2);
    puts("PASS: actual hook returns native directions, fire/focus and either side button; held bombs produce one rising edge; player position untouched");

    fixture();test_native=SUBTICK_LEFT|0x100;assert(poll()==test_native);
    test_native=0;test_menu=1;test_keys[VK_LBUTTON]=test_keys[VK_XBUTTON1]=1;assert(poll()==0);
    test_menu=0;assert((poll()&3)==0);test_keys[VK_LBUTTON]=test_keys[VK_XBUTTON1]=0;poll();
    test_keys[VK_LBUTTON]=test_keys[VK_XBUTTON1]=1;assert((poll()&3)==3);
    test_foreground=0;assert(poll()==0);test_foreground=1;assert((poll()&3)==0);
    fixture();subtick_player.armed=proj_slice.armed=0;test_keys[VK_LBUTTON]=1;assert(poll()==0);
    fixture();test_native=8;test_keys[VK_LBUTTON]=1;assert(poll()==8);
    fixture();test_cursor.x=-1;test_keys[VK_LBUTTON]=1;assert(poll()==0);
    fixture();test_keys[VK_F10]=1;assert(poll()==0 && !mouse_enabled);poll();assert(!mouse_enabled);
    test_keys[VK_F10]=0;poll();test_keys[VK_F10]=1;poll();assert(mouse_enabled);
    puts("PASS: keyboard priority, menu/pause/Escape/focus/outside-window gates, click-release latches, F10 toggle and debounce");

    fixture();test_client.right=1280;test_client.bottom=720;test_cursor=(POINT){792,600};
    assert(poll()==SUBTICK_RIGHT && mouse_target[0]==292 && mouse_target[1]==384);
    test_client.right=0;assert(poll()==0);
    puts("PASS: widescreen origin, client/backbuffer scaling and invalid window size");

    const float bounds[]={8,16,368,416},scale[]={1,1};
    const float speeds[][4]={{4,2,2.82842712f,1.41421356f},{5,2.5f,3.53553391f,1.76776695f}};
    for(unsigned character=0;character<2;++character) for(int focus=0;focus<2;++focus)
    for(int target=0;target<80;++target) {
        float x=192,y=384,tx=8+((target*173)%368),ty=16+((target*73)%416);
        for(int frame=0;frame<600;++frame) {
            float before=(tx-x)*(tx-x)+(ty-y)*(ty-y),dx,dy;
            unsigned bits=mouse_follow_direction(x,y,tx,ty,speeds[character],speeds[character]+2,scale,bounds,focus);
            subtick_direction(bits|(focus?4:0),speeds[character],speeds[character]+2,&dx,&dy);
            assert(hypotf(dx,dy)<=speeds[character][focus]+0.00001f);
            x=subtick_clamp(x+dx,8,368);y=subtick_clamp(y+dy,16,416);
            float after=(tx-x)*(tx-x)+(ty-y)*(ty-y);assert(after<=before+0.01f);
        }
        assert(hypotf(tx-x,ty-y)<=speeds[character][focus]);
        assert(mouse_follow_direction(x,y,tx,ty,speeds[character],speeds[character]+2,scale,bounds,focus)==0);
    }
    assert(mouse_follow_direction(NAN,0,1,2,speeds[0],speeds[0]+2,scale,bounds,0)==0);
    puts("PASS: 320 cursor destinations across both movement speeds/focus modes converge without oscillation, excess speed or crossing playfield bounds");
    fixture();mouse_direct=1;poll();
    assert(pos[0]==192 && pos[1]==384); /* entering direct mode never jumps */
    test_cursor.x+=150;test_cursor.y-=60;mouse_present_move();
    assert(pos[0]==242 && pos[1]==364 && !(mouse_output&0xf0));
    assert(mouse_target[0]==pos[0] && mouse_target[1]==pos[1]);
    test_keys[VK_RBUTTON]=1;test_cursor.x+=60;mouse_present_move();assert(pos[0]==252);
    test_keys[VK_RBUTTON]=0;mouse_present_move();assert(pos[0]==252);
    test_cursor.x+=30;mouse_present_move();assert(pos[0]==262);
    test_cursor.x=2500;mouse_present_move();assert(pos[0]==376);
    test_cursor.x-=3;mouse_present_move();assert(pos[0]==375); /* instant wall reversal */
    test_menu=1;test_cursor.x-=90;mouse_present_move();assert(pos[0]==375);
    test_menu=0;mouse_present_move();assert(pos[0]==375);
    test_cursor.x-=30;mouse_present_move();assert(pos[0]==365);
    test_foreground=0;test_cursor.x-=120;mouse_present_move();assert(pos[0]==365);
    test_foreground=1;mouse_present_move();assert(pos[0]==365);
    subtick_player.armed=proj_slice.armed=0;test_cursor.x-=90;mouse_present_move();assert(pos[0]==365);
    subtick_player.armed=proj_slice.armed=1;mouse_present_move();assert(pos[0]==365);
    test_native=SUBTICK_LEFT;poll();test_cursor.x-=60;mouse_present_move();assert(pos[0]==365);
    test_native=0;poll();assert(pos[0]==365);
    test_cursor.x-=3;mouse_present_move();assert(pos[0]==364);
    assert(mouse_live_sprite((void*)(base+game->player+0x78c8)));
    assert(!mouse_live_sprite((void*)(base+game->player+0x420)));
    test_keys[VK_ESCAPE]=1;test_cursor.x-=60;mouse_present_move();assert(pos[0]==364);
    puts("PASS: direct display-rate movement, exact displacement, no catch-up, half-sensitivity focus, no release jump, bounds/reversal, transition re-anchoring, keyboard priority and player-only smoothing bypass");
    const int rates[]={60,144,360,1000};
    for (unsigned r=0;r<4;++r) for (int focused=0;focused<2;++focused)
    for (int character=0;character<2;++character) {
        fixture();mouse_direct=1;mouse_speed_limit=1;
        float* live_speeds=(float*)(base+game->player+game->pl_speed_straight);
        live_speeds[0]=character?5:4;live_speeds[1]=character?2.5f:2;
        poll();test_keys[VK_RBUTTON]=focused;
        for (int n=0;n<24;++n) {
            float x=pos[0],y=pos[1];
            test_clock+=360000/rates[r];
            test_cursor.x+=(n&1)?-180:180;test_cursor.y+=(n&1)?-120:120;
            mouse_present_move();
            float distance=hypotf(pos[0]-x,pos[1]-y);
            float maximum=live_speeds[focused]*60.0f/rates[r];
            assert(fabsf(distance-maximum)<0.0001f);
            assert(fabsf((pos[1]-y)/(pos[0]-x)-2.0f/3)<0.002f);
            x=pos[0];y=pos[1];test_clock+=360000/rates[r];mouse_present_move();
            assert(pos[0]==x && pos[1]==y); /* no residual swipe queued */
        }
    }
    fixture();mouse_direct=1;hfr_ui_set(UI_MOUSE_SPEED_LIMIT,1);assert(hfr_ui_get(UI_MOUSE_SPEED_LIMIT));poll();
    test_clock+=1000;test_cursor.x+=1;mouse_present_move();assert(fabsf(pos[0]-192-1.0f/3)<0.00002f);
    float before=pos[0];test_clock+=360000*10;test_cursor.x+=300;mouse_present_move();assert(fabsf(pos[0]-before-4)<0.00002f);
    before=pos[0];test_cursor.x+=300;mouse_present_move();assert(pos[0]==before); /* same-time polls cannot add travel */
    test_clock+=1000;test_cursor.x-=300;mouse_present_move();assert(fabsf(pos[0]-before+4.0f/6)<0.00002f);
    speed_pct=50;before=pos[0];test_clock+=1000;test_cursor.x+=300;mouse_present_move();assert(fabsf(pos[0]-before-4.0f/12)<0.00002f);
    hfr_ui_set(UI_MOUSE_SPEED_LIMIT,0);poll();before=pos[0];test_clock+=1000;test_cursor.x+=60;mouse_present_move();assert(pos[0]==before+20);
    puts("PASS: speed cap at 60/144/360/1000 Hz, two character speeds, focus, diagonal magnitude/direction, immediate small movements, no queued travel, stall/duplicate-poll caps, game-speed scaling and menu toggle");
    if(argc>1) test_real_prologue(argv[1]);
    free((void*)base);return 0;
}
