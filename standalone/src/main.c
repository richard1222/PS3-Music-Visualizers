#include <SDL.h>
#include <io/pad.h>
#include <sys/process.h>
#include <sysutil/sysutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <math.h>
#define MINIMP3_NO_STDIO
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
#include "library.h"
#include "ui.h"
#include "spectrum.h"
#include "runtime_ps3.h"
#include "display.h"
#include "visualizer.h"
#include "gallery.h"
#include "update.h"
#include "pc_audio.h"
#include "youtube.h"
#include "yt_net.h"
#include "yt_worker.h"
#include "yt_osk.h"
#include "yt_player.h"
#include "ftp_server.h"
#include "settings_layout.h"
#include "eq.h"
#include "frame_health.h"
#include "safety.h"
SYS_PROCESS_PARAM(1001,0x100000);
#define ASSETS "/dev_hdd0/game/SVIZ00001/USRDIR/"
#include "diagnostic.h"
static Library library;
static Entry entries[NL_ENTRIES];
static Entry art_entries[NL_ENTRIES];
static mp3dec_ex_t decoder;
static mp3dec_io_t decoder_io;
static FILE *music_file;
static SDL_AudioDeviceID audio;
static SDL_atomic_t volume,finished;
#ifdef NL_DIAGNOSTIC
static SDL_atomic_t decoded_blocks;
#endif
static float pcm[SV_FFT];
static unsigned pcm_position;
static int pc_mode,yt_mode;
static SDL_Surface *art_cache,*art_cache_source;
static int rate=48000,channels=2,playing=-1,paused,decoder_open;
static char message[160]="Choose a folder or an MP3 to begin";
static size_t file_read(void *buf,size_t size,void *userdata) { return fread(buf,1,size,(FILE*)userdata); }
static int file_seek(uint64_t position,void *userdata) {
    return position>LONG_MAX?-1:fseek((FILE*)userdata,(long)position,SEEK_SET);
}
static void audio_callback(void *userdata,Uint8 *stream,int bytes) {
    (void)userdata;
    memset(stream,0,bytes);
    size_t count=yt_mode?(size_t)yt_player_audio_read((int16_t*)stream,(int)(bytes/2)):(pc_mode?pc_audio_read((int16_t*)stream,(size_t)bytes/2):mp3dec_ex_read(&decoder,(mp3d_sample_t*)stream,(size_t)bytes/2));
#ifdef NL_DIAGNOSTIC
    if(count) SDL_AtomicAdd(&decoded_blocks,1);
#endif
    Sint16 *samples=(Sint16*)stream;
    int gain=SDL_AtomicGet(&volume);
    for(size_t i=0;i<count;i++) samples[i]=(Sint16)((int)samples[i]*gain/100);
    /* Every source funnels through this one callback, so this single
     * EQ stage covers MP3, PC Audio and YouTube alike. It runs before
     * the spectrum tap below, so the visualizer shows the tuned sound. */
    eq_process(samples,count,channels,rate);
    for(size_t i=0;i<((pc_mode||yt_mode)?(size_t)bytes/2:count);i+=(size_t)channels) {
        float v=(float)samples[i]/32768.0f;
        if(channels==2 && i+1<count) v=(v+(float)samples[i+1]/32768.0f)*0.5f;
        pcm[pcm_position++%SV_FFT]=v;
    }
    if(!pc_mode && !yt_mode && count<(size_t)bytes/2) SDL_AtomicSet(&finished,1);
}
static void stop_music(void) {
    safety_stage(1);
    NL_EVENT("audio-close-before");
    if(audio) SDL_CloseAudioDevice(audio);
    NL_EVENT("audio-close-complete");
#ifdef NL_DIAGNOSTIC
    NL_EVENT_VALUE("decoded-blocks",SDL_AtomicGet(&decoded_blocks));
#endif
    audio=0;
    safety_stage(2);
    if(pc_mode) pc_audio_stop();
    pc_mode=0;
    if(yt_mode) yt_player_stop();
    yt_mode=0;
    if(decoder_open) mp3dec_ex_close(&decoder);
    decoder_open=0;
    if(music_file) fclose(music_file);
    music_file=NULL; playing=-1; paused=0;
    memset(pcm,0,sizeof(pcm)); pcm_position=0;
    SDL_AtomicSet(&finished,0);
    safety_stage(0);
}
static int play_track(int index) {
    SDL_AudioSpec want,got;
    if(index<0 || index>=library.count) return -1;
    stop_music();
    music_file=fopen(library.tracks[index].path,"rb");
    if(!music_file) { snprintf(message,sizeof(message),"Cannot open this track"); return -1; }
    memset(&decoder,0,sizeof(decoder));
    decoder_io.read=file_read; decoder_io.read_data=music_file;
    decoder_io.seek=file_seek; decoder_io.seek_data=music_file;
    decoder_open=1;
    if(mp3dec_ex_open_cb(&decoder,&decoder_io,MP3D_DO_NOT_SCAN)) {
        stop_music(); snprintf(message,sizeof(message),"Invalid or unsupported MP3"); return -1;
    }
    rate=decoder.info.hz; channels=decoder.info.channels;
    if(rate<8000 || rate>48000 || channels<1 || channels>2) {
        stop_music(); snprintf(message,sizeof(message),"Unsupported MP3 sample format"); return -1;
    }
    SDL_zero(want); want.freq=rate; want.channels=channels;
    want.format=AUDIO_S16SYS; want.samples=2048; want.callback=audio_callback;
#ifdef NL_DIAGNOSTIC
    SDL_AtomicSet(&decoded_blocks,0);
#endif
    NL_EVENT("audio-open-before");
    audio=SDL_OpenAudioDevice(NULL,0,&want,&got,0);
    NL_EVENT(audio?"audio-open-complete":SDL_GetError());
    if(!audio) { stop_music(); snprintf(message,sizeof(message),"Audio device unavailable"); return -1; }
    playing=index; paused=0;
    snprintf(message,sizeof(message),"MP3 playback | %d Hz | %s",rate,channels==2?"Stereo":"Mono");
    SDL_PauseAudioDevice(audio,0); return 0;
}
static void play_pc(void) {
    SDL_AudioSpec want,got;
    stop_music();
    if(pc_audio_start()) { snprintf(message,sizeof(message),"Cannot start PC Audio"); return; }
    pc_mode=1; rate=48000; channels=2;
    SDL_zero(want); want.freq=rate; want.channels=2;
    want.format=AUDIO_S16SYS; want.samples=2048; want.callback=audio_callback;
    audio=SDL_OpenAudioDevice(NULL,0,&want,&got,0);
    if(!audio) { stop_music(); snprintf(message,sizeof(message),"Audio device unavailable"); return; }
    SDL_PauseAudioDevice(audio,0);
}
static void play_yt(const StreamInfo *info,const char *video_id) {
    stop_music();
    if(yt_player_start(info,video_id)) {
        const char *err=yt_player_error();
        snprintf(message,sizeof(message),"%s",err[0]?err:"YouTube playback could not start");
        return;
    }
    yt_mode=1; paused=0;
    snprintf(message,sizeof(message),"YouTube | Loading video...");
}
static void toggle_pause(void) { if(audio) { paused=!paused; SDL_PauseAudioDevice(audio,paused); } }
static void change_volume(int delta) {
    int v=SDL_AtomicGet(&volume)+delta;
    SDL_AtomicSet(&volume,v<0?0:(v>100?100:v));
}
/* EQ settings persist in USRDIR/eq.txt, the same plain-file pattern
 * as the app's other small state. Format v2 (v1.18): line 1 is the
 * legacy five whole-dB gains, unchanged, so a v1.17 file loads
 * exactly as before; each set user preset slot then adds one
 * "slot <n> g0 g1 g2 g3 g4" line (eq_format_user_slot). A missing or
 * partial file simply leaves the missing bands flat. */
static void eq_save(void) {
    FILE *f=fopen(ASSETS "eq.txt","w");
    if(!f) return;
    for(int i=0;i<EQ_BANDS;i++) fprintf(f,"%d ",eq_gain(i));
    fprintf(f,"\n");
    for(int s=0;s<EQ_USER_PRESET_COUNT;s++) {
        char line[96];
        if(eq_format_user_slot(line,sizeof(line),s)>0) fprintf(f,"%s\n",line);
    }
    fclose(f);
}
static void eq_load(void) {
    FILE *f=fopen(ASSETS "eq.txt","r");
    if(!f) return;
    char line[160];
    if(fgets(line,sizeof(line),f)) {
        int g[EQ_BANDS],off=0,used=0;
        for(int i=0;i<EQ_BANDS;i++) {
            if(sscanf(line+off,"%d%n",&g[i],&used)==1) { eq_set_gain(i,g[i]); off+=used; }
        }
    }
    while(fgets(line,sizeof(line),f)) {
        int slot,g[EQ_BANDS];
        if(eq_parse_user_slot(line,&slot,g)) eq_user_preset_set(slot,g);
    }
    fclose(f);
}
/* The name the UI shows for the current gains: a user slot wins over
 * a built-in when both match (it is the more recent intent), an
 * unmatched curve is plain "Custom". */
static const char *eq_preset_label(void) {
    int u=eq_match_user_preset();
    if(u>=0) return eq_user_preset_name[u];
    int p=eq_match_preset();
    if(p>=0) return eq_preset_name[p];
    return "Custom";
}
/* Unified preset position for cycling: built-ins 0..EQ_PRESET_COUNT-1,
 * user slots above them, -1 when the gains match nothing. */
static int eq_preset_position(void) {
    int u=eq_match_user_preset();
    if(u>=0) return EQ_PRESET_COUNT+u;
    return eq_match_preset();
}
static void eq_apply_position(int pos) {
    pos=((pos%EQ_PRESET_TOTAL)+EQ_PRESET_TOTAL)%EQ_PRESET_TOTAL;
    if(pos<EQ_PRESET_COUNT) eq_apply_preset(pos);
    else eq_apply_user_preset(pos-EQ_PRESET_COUNT);
}
/* Square on the preset row: capture the current gains into a user
 * slot - the matching slot if any, else the slot last applied, else
 * the first free slot, else slot 0. Returns the slot used. */
static int eq_save_custom(void) {
    int slot=eq_match_user_preset();
    if(slot<0) slot=eq_user_preset_last();
    if(slot<0) {
        slot=0;
        for(int s=0;s<EQ_USER_PRESET_COUNT;s++) if(!eq_user_preset_is_set(s)) { slot=s; break; }
    }
    eq_save_user_preset(slot);
    eq_save();
    return slot;
}
/* Visualizer mode persists as one int in USRDIR/viz.txt (the eq.txt
 * pattern). A missing or garbled file leaves the original bars. */
static int viz_mode;
static void viz_save(void) {
    FILE *f=fopen(ASSETS "viz.txt","w");
    if(!f) return;
    fprintf(f,"%d\n",viz_mode);
    fclose(f);
}
static void viz_load(void) {
    FILE *f=fopen(ASSETS "viz.txt","r");
    if(!f) return;
    int m=0;
    if(fscanf(f,"%d",&m)==1 && m>=0 && m<VIZ_MODE_COUNT) viz_mode=m;
    fclose(f);
}
/* One tuning step in the EQ view: a band row moves that band by one
 * dB; the preset row cycles the unified preset list (built-ins then
 * Custom 1-3; from an unmatched curve, cycling starts at Flat).
 * Every change is saved immediately. */
static void eq_adjust(int eq_selection,int delta) {
    if(eq_selection<EQ_BANDS) {
        eq_set_gain(eq_selection,eq_gain(eq_selection)+delta);
    } else {
        int p=eq_preset_position();
        if(p<0) p=0;
        eq_apply_position(p+delta);
    }
    eq_save();
}
static void draw_art(SDL_Surface *screen,SDL_Surface *art,int x,int y,int w,int h) {
    if(!art) return;
    float scale=(float)w/art->w;
    if((float)h/art->h<scale) scale=(float)h/art->h;
    int width=(int)(art->w*scale),height=(int)(art->h*scale);
    SDL_Rect dst={(x+w-width)*screen->w/1280,(y+(h-height)/2)*screen->h/720,width*screen->w/1280,height*screen->h/720};
    if(!art_cache || art_cache_source!=art || art_cache->w!=dst.w || art_cache->h!=dst.h) {
        SDL_FreeSurface(art_cache);
        art_cache=SDL_CreateRGBSurfaceWithFormat(0,dst.w,dst.h,32,SDL_PIXELFORMAT_ARGB8888);
        if(!art_cache) return;
        SDL_BlitScaled(art,NULL,art_cache,NULL);
        if(!art->format->Amask) SDL_SetSurfaceBlendMode(art_cache,SDL_BLENDMODE_NONE);
        art_cache_source=art;
    }
    SDL_BlitSurface(art_cache,NULL,screen,&dst);
}
enum { B_UP=1,B_DOWN=2,B_LEFT=4,B_RIGHT=8,B_X=16,B_O=32,B_START=64,B_L1=128,B_R1=256,B_L2=512,B_R2=1024,B_TRI=2048,B_SQ=4096 };
static unsigned pad_buttons(void) {
    static unsigned held[7]; padInfo2 info; unsigned combined=0;
    memset(&info,0,sizeof(info));
    int info_result=ioPadGetInfo2(&info);
    NL_VALUE("pad-info-result",info_result);
    if(info_result) return 0;
    NL_VALUE("pad-count",info.connected);
    NL_VALUE("pad-max",info.max);
    for(int port=0;port<7;port++) {
        padData data; memset(&data,0,sizeof(data));
        if(!(info.port_status[port]&1)) { held[port]=0; continue; }
        int result=ioPadGetData(port,&data);
        NL_VALUE("pad-port",port); NL_VALUE("pad-data-result",result); NL_VALUE("pad-data-length",data.len);
        if(result) { held[port]=0; continue; }
        if(data.len>0) held[port]=(data.BTN_UP?B_UP:0)|(data.BTN_DOWN?B_DOWN:0)|(data.BTN_LEFT?B_LEFT:0)|(data.BTN_RIGHT?B_RIGHT:0)|
        (data.BTN_CROSS?B_X:0)|(data.BTN_CIRCLE?B_O:0)|(data.BTN_START?B_START:0)|
        (data.BTN_L1?B_L1:0)|(data.BTN_R1?B_R1:0)|(data.BTN_L2?B_L2:0)|(data.BTN_R2?B_R2:0)|
        (data.BTN_TRIANGLE?B_TRI:0)|(data.BTN_SQUARE?B_SQ:0);
        combined|=held[port];
    }
    return combined;
}
static void refresh_folder(const char *folder,int *count,int *selected) {
    *count=library_browse(folder,entries,NL_ENTRIES); *selected=0;
    if(*count<0) { *count=0; snprintf(message,sizeof(message),"Folder unavailable; choose another source"); }
}
static void refresh_art_folder(const char *folder,int *count,int *selected) {
    *count=library_browse_bmp(folder,art_entries,NL_ENTRIES); *selected=0;
    if(*count<0) { *count=0; snprintf(message,sizeof(message),"Folder unavailable; choose another source"); }
}
static void scan_folder(const char *folder) {
    stop_music(); snprintf(message,sizeof(message),"Scanning MP3 files...");
    library_scan(&library,folder);
    snprintf(message,sizeof(message),"%d MP3 tracks%s%s",library.count,library.truncated?" (scan limit reached)":"",library.errors?" (some folders unavailable)":"");
}
/* --- OG Xbox menu reskin (visual only) ----------------------------------
 * Black-green palette, jewel-green tiles with a pale focus glow, and
 * the spinning mini Xbox under the title, all drawn with the existing
 * ui_rect/ui_line primitives. (Was the purple "GameCube" reskin in
 * 1.16/1.17 - same technique, new literals.) No layout logic or
 * navigation changed beyond the sidebar tile shift for the spinner. */
static Uint32 gc_shade(Uint32 a,Uint32 b,int step,int steps) {
    int ar=(int)((a>>16)&255),ag=(int)((a>>8)&255),ab=(int)(a&255);
    int br=(int)((b>>16)&255),bg=(int)((b>>8)&255),bb=(int)(b&255);
    if(steps<1) steps=1;
    return ((Uint32)(ar+(br-ar)*step/steps)<<16)|((Uint32)(ag+(bg-ag)*step/steps)<<8)|(Uint32)(ab+(bb-ab)*step/steps);
}
static void draw_gc_background(SDL_Surface *screen) {
    const int bands=24;
    for(int i=0;i<bands;i++)
        ui_rect(screen,0,i*720/bands,1280,720/bands+1,gc_shade(0x060805,0x1a2e0d,i,bands-1));
}
static void draw_gc_tile(SDL_Surface *screen,int x,int y,int w,int h,int selected) {
    if(selected) {
        ui_rect(screen,x-3,y-3,w+6,h+6,0xd9ffb3);
        ui_rect(screen,x,y,w,h,0x5c8a1e);
    } else {
        ui_rect(screen,x,y,w,h,0x243010);
        ui_rect(screen,x,y,w,3,0x3d5a1a);
    }
}
static void draw_gc_cube(SDL_Surface *screen,int cx,int cy,int size,Uint32 ticks,Uint32 rgb) {
    static const float verts[8][3]={
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
    };
    static const int edges[12][2]={
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    float a=ticks*0.0004f,ca=cosf(a),sa=sinf(a);
    const float tilt=0.6f,ct=cosf(tilt),st=sinf(tilt);
    int px[8],py[8];
    for(int i=0;i<8;i++) {
        float x=verts[i][0],y=verts[i][1],z=verts[i][2];
        float rx=x*ca+z*sa,rz=-x*sa+z*ca;
        float ry=y*ct-rz*st;
        px[i]=cx+(int)(rx*size);
        py[i]=cy+(int)(ry*size);
    }
    for(int i=0;i<12;i++)
        ui_line(screen,px[edges[i][0]],py[edges[i][0]],px[edges[i][1]],py[edges[i][1]],rgb);
}
/* --- Spinning mini Xbox (v1.18) -------------------------------------------
 * The renderer has no rotation-capable blit, so the console turntable
 * is a pre-rendered BMP frame sequence in USRDIR/art/xbox/
 * (frame_00.bmp, frame_01.bmp, ...). Frames are loaded once at
 * startup and pre-scaled to the draw box, so a frame costs one plain
 * blit (the frame watchdog at the bottom of the main loop exits the
 * app after 10 s below 10 FPS - per-frame scaling is not affordable,
 * and the shared draw_art cache would thrash against the hero panel).
 * Transparency is runtime-adaptive: a frame whose format carries an
 * alpha mask blends; a 24-bit frame gets a magenta color key. When
 * no frames are installed the wireframe cube above, recolored jewel
 * green, spins in the same spot - the build never looks broken. */
#define XBOX_FRAMES_MAX 36
#define XBOX_BOX_X 35
#define XBOX_BOX_Y 62
#define XBOX_BOX_W 160
#define XBOX_BOX_H 120
static SDL_Surface *xbox_frames[XBOX_FRAMES_MAX];
static int xbox_frame_count;
static void xbox_frames_load(SDL_Surface *screen) {
    xbox_frame_count=0;
    int tw=XBOX_BOX_W*screen->w/1280,th=XBOX_BOX_H*screen->h/720;
    if(tw<8 || th<8) return;
    for(int i=0;i<XBOX_FRAMES_MAX;i++) {
        char path[128];
        snprintf(path,sizeof(path),ASSETS "art/xbox/frame_%02d.bmp",i);
        SDL_Surface *raw=SDL_LoadBMP(path);
        if(!raw) break;
        if(raw->format->Amask) SDL_SetSurfaceBlendMode(raw,SDL_BLENDMODE_BLEND);
        else SDL_SetColorKey(raw,SDL_TRUE,SDL_MapRGB(raw->format,255,0,255));
        SDL_Surface *scaled=SDL_CreateRGBSurfaceWithFormat(0,tw,th,32,SDL_PIXELFORMAT_ARGB8888);
        if(!scaled) { SDL_FreeSurface(raw); break; }
        SDL_BlitScaled(raw,NULL,scaled,NULL);
        SDL_FreeSurface(raw);
        if(scaled->format->Amask) SDL_SetSurfaceBlendMode(scaled,SDL_BLENDMODE_BLEND);
        xbox_frames[xbox_frame_count++]=scaled;
    }
}
static void xbox_frames_close(void) {
    for(int i=0;i<xbox_frame_count;i++) SDL_FreeSurface(xbox_frames[i]);
    xbox_frame_count=0;
}
static void draw_spinner(SDL_Surface *screen,Uint32 ticks) {
    if(xbox_frame_count>0) {
        SDL_Surface *frame=xbox_frames[(ticks/66)%(Uint32)xbox_frame_count];
        SDL_Rect dst={XBOX_BOX_X*screen->w/1280,XBOX_BOX_Y*screen->h/720,0,0};
        SDL_BlitSurface(frame,NULL,screen,&dst);
    } else {
        draw_gc_cube(screen,115,122,34,ticks,0x76bc21);
    }
}
/* --- YouTube playlists & favorites (v1.18) --------------------------------
 * Two plain files in USRDIR (the eq.txt pattern): one tab-separated
 * line per entry - videoId, title, author, duration, plus a fifth
 * field "1" when the entry is a live stream. Loaded at startup,
 * rewritten on every change. The queue below plays a list from a
 * chosen entry through the normal worker extract path; auto-advance
 * happens only on a natural YT_PLAYER_ENDED (see the playback
 * housekeeping in main), never on a manual stop, and live entries
 * are skipped because live playback never raises ENDED cleanly. */
#define YT_LIST_MAX 100
enum { YT_LIST_PLAYLIST=0, YT_LIST_FAVORITES=1 };
typedef struct { SearchResult items[YT_LIST_MAX]; int count; } YtList;
static YtList yt_playlist,yt_favorites;
static SearchResult yt_queue[YT_LIST_MAX];
static int yt_queue_count,yt_queue_index,yt_queue_active;
static SearchResult yt_current;   /* the item the info view describes */
static int yt_info_from=2;        /* view O returns to from info (2/5/6) */
static const char *yt_list_path(int which) {
    return which==YT_LIST_FAVORITES?ASSETS "yt_favorites.txt":ASSETS "yt_playlist.txt";
}
static void yt_list_clean(char *s) {
    for(char *p=s;*p;p++) if(*p=='\t' || *p=='\n' || *p=='\r') *p=' ';
}
static void yt_list_save(int which) {
    const YtList *l=which==YT_LIST_FAVORITES?&yt_favorites:&yt_playlist;
    FILE *f=fopen(yt_list_path(which),"w");
    if(!f) return;
    for(int i=0;i<l->count;i++) {
        const SearchResult *r=&l->items[i];
        fprintf(f,"%s\t%s\t%s\t%s\t%d\n",r->videoId,r->title,r->author,r->duration,r->isLive?1:0);
    }
    fclose(f);
}
static void yt_list_load(int which) {
    YtList *l=which==YT_LIST_FAVORITES?&yt_favorites:&yt_playlist;
    l->count=0;
    FILE *f=fopen(yt_list_path(which),"r");
    if(!f) return;
    char line[320];
    while(l->count<YT_LIST_MAX && fgets(line,sizeof(line),f)) {
        char *fld[5]; int nf=1; fld[0]=line;
        for(char *p=line;*p;p++) {
            if(*p=='\n' || *p=='\r') { *p=0; break; }
            if(*p=='\t' && nf<5) { *p=0; fld[nf++]=p+1; }
        }
        if(nf<4 || !fld[0][0]) continue;
        SearchResult *r=&l->items[l->count];
        memset(r,0,sizeof(*r));
        snprintf(r->videoId,sizeof(r->videoId),"%s",fld[0]);
        snprintf(r->title,sizeof(r->title),"%s",fld[1]);
        snprintf(r->author,sizeof(r->author),"%s",fld[2]);
        snprintf(r->duration,sizeof(r->duration),"%s",fld[3]);
        r->isLive=(nf>=5 && fld[4][0]=='1')?1:0;
        l->count++;
    }
    fclose(f);
}
/* Returns 0 when added, 1 when the video is already listed, -1 when
 * the list is full. */
static int yt_list_add(int which,const SearchResult *item) {
    YtList *l=which==YT_LIST_FAVORITES?&yt_favorites:&yt_playlist;
    for(int i=0;i<l->count;i++) if(!strcmp(l->items[i].videoId,item->videoId)) return 1;
    if(l->count>=YT_LIST_MAX) return -1;
    SearchResult *r=&l->items[l->count++];
    *r=*item;
    yt_list_clean(r->title); yt_list_clean(r->author); yt_list_clean(r->duration);
    yt_list_save(which);
    return 0;
}
static void yt_queue_clear(void) { yt_queue_active=0; yt_queue_count=0; yt_queue_index=0; }
/* Build the play queue from a list, starting at entry `from`, with
 * live entries skipped. Returns 1 when anything is queued. */
static int yt_queue_start(const YtList *list,int from) {
    yt_queue_count=0;
    for(int i=from;i<list->count;i++)
        if(!list->items[i].isLive) yt_queue[yt_queue_count++]=list->items[i];
    yt_queue_index=0;
    yt_queue_active=yt_queue_count>0;
    if(yt_queue_active) yt_current=yt_queue[0];
    return yt_queue_active;
}
int main(int argc,char **argv) {
    (void)argc; (void)argv;
    SDL_Surface *screen=NULL,*background=NULL;
    const char *nav[]={"Music","Visualizer","Artwork","Settings","Exit"};
    const char *sources[]={"/dev_hdd0/music","/dev_hdd0","/dev_usb000","/dev_usb001"};
    char folder[NL_PATH]="/dev_hdd0/music";
    char art_folder[NL_PATH]="/dev_hdd0";
    int running=1,section=0,focus=0,view=0,selection=0,entry_count=0,source=0;
    int art_view=0,art_selection=0,art_count=0,art_sel=0,art_source=0,art_browse_view=0;
    int settings_selection=0,update_pending=0,settings_view=0,eq_selection=0;
    int yt_view=0,yt_count=0,yt_busy_kind=0;
    char yt_query[128]="";
    char yt_status[160]="";
    char upd_msg[160];
    char update_notice[48]="";
    int auto_update_checked=0;
    Uint32 boot_ticks=0;
    unsigned previous=0; Uint32 repeat_at=0; float smooth[SV_BANDS]={0};
    Gallery gallery; FrameHealth health={0};
    SpectrumPlan spectrum_plan={0};
    Uint32 fps_start=0; unsigned fps_frames=0,fps=0;
#ifdef NL_DIAGNOSTIC
    int diagnostic_gallery_sweep=0;
#endif
    SDL_AtomicSet(&volume,75);
    eq_init(); eq_load(); viz_load();
    if(NL_DIAGNOSTIC_START()) return 6;
    NL_TRACE("before-sdl-init");
    if(SDL_Init(display_init_flags()|SDL_INIT_AUDIO|SDL_INIT_TIMER)) return 1;
    if(runtime_init()) { SDL_Quit(); return 4; }
    /* Network failure must never block offline use. A one-shot
     * availability check runs shortly after startup and stays silent
     * when the network or manifest is unreachable; the full update
     * check still runs only when the user asks for it. */
    update_net_init();
    yt_set_data_dir("/dev_hdd0/game/SVIZ00001/USRDIR");
    yt_net_set_ca(ASSETS "certs/cacert.pem");
    yt_worker_start();
    yt_list_load(YT_LIST_PLAYLIST);
    yt_list_load(YT_LIST_FAVORITES);
#ifdef NL_DIAGNOSTIC
    if(argc>1 && !strcmp(argv[1],"--test-stalled-flip")) runtime_diagnostic_stall_flip(0);
    if(argc>1 && !strcmp(argv[1],"--test-exit-during-flip")) runtime_diagnostic_stall_flip(1);
    FILE *fault=fopen(ASSETS "diagnostic-stall-flip.flag","r");
    if(fault) { fclose(fault); runtime_diagnostic_stall_flip(0); NL_TRACE("fault-stalled-flip-enabled"); }
    fault=fopen(ASSETS "diagnostic-exit-flip.flag","r");
    if(fault) { fclose(fault); runtime_diagnostic_stall_flip(1); NL_TRACE("fault-exit-flip-enabled"); }
    fault=fopen(ASSETS "diagnostic-main-stall.flag","r");
    if(fault) { fclose(fault); NL_TRACE("fault-main-stall-enabled"); for(;;) usleep(50000); }
    fault=fopen(ASSETS "diagnostic-menu-stall.flag","r");
    if(fault) { fclose(fault); runtime_diagnostic_menu_open(); NL_TRACE("fault-menu-stall-enabled"); for(;;) usleep(50000); }
    fault=fopen(ASSETS "diagnostic-controls.flag","r");
    if(fault) { fclose(fault); diagnostic_frame_limit=900; SDL_AtomicSet(&diagnostic_deadline_ticks,800); NL_TRACE("controls-test-enabled"); }
    fault=fopen(ASSETS "diagnostic-music.flag","r");
    if(fault) { fclose(fault); diagnostic_frame_limit=5400; SDL_AtomicSet(&diagnostic_deadline_ticks,4000); NL_TRACE("music-test-enabled"); }
#endif
    NL_TRACE("before-window");
    screen=display_open();
    if(!screen) { display_close(); runtime_shutdown(); SDL_Quit(); return 2; }
    NL_TRACE("surface-ready");
    if(ui_font_open(ASSETS "fonts/NotoSans-Regular.ttf")) { display_close(); runtime_shutdown(); SDL_Quit(); return 3; }
    background=SDL_LoadBMP(ASSETS "art/background.bmp");
    xbox_frames_load(screen);
    gallery_open(&gallery,ASSETS "art/gallery");
    NL_EVENT_VALUE("gallery-loaded",gallery.loaded);
    int pad_init=ioPadInit(7);
    NL_VALUE("pad-init-result",pad_init);
    if(pad_init!=PAD_OK && (u32)pad_init!=PAD_ERROR_ALREADY_INITIALIZED) {
        gallery_close(&gallery); SDL_FreeSurface(background); ui_font_close();
        display_close(); runtime_shutdown(); SDL_Quit(); return 5;
    }
    refresh_folder(folder,&entry_count,&selection);
    NL_TRACE("input-and-folder-ready");
#ifdef NL_DIAGNOSTIC
    fault=fopen(ASSETS "diagnostic-autoplay.flag","r");
    if(fault) {
        fclose(fault); diagnostic_frame_limit=90; NL_EVENT("audio-autoplay-enabled");
        diagnostic_gallery_sweep=1;section=1;focus=1;
        scan_folder("/dev_hdd0/music");
        if(library.count) play_track(0);
    }
#endif
    if(safety_start()) { stop_music(); return 7; }
    boot_ticks=SDL_GetTicks();
    int yt_susp_prev=0; /* last draw-suspended state (OSK transition log) */
    while(running) {
        int update_ran=0;
        safety_beat();
        NL_TRACE("before-events");
        Uint32 start=SDL_GetTicks(); SDL_Event event;
        sysUtilCheckCallback();
        while(SDL_PollEvent(&event)) {
            if(event.type==SDL_QUIT) running=0;
        }
        NL_TRACE("events-complete");
        if(!running || runtime_exit_requested()) break;
        /* The system keyboard is serviced on EVERY frame while it is
         * up, BEFORE the draw-suspended early-out below: the VSH
         * dialog is what holds drawing suspended, and on the cancel
         * path it can only close when the app answers it with
         * oskAbort() - an answer a poll sitting behind that early-out
         * could never give (the 1.11 cancel freeze,
         * FREEZE2-DIAGNOSIS.md). The deadline check is likewise
         * unconditional, so no branch can starve the bound. */
        if(yt_osk_is_open()) {
            if(yt_osk_enforce_deadline()) {
                /* A close-path deadline expired outside the poll:
                 * the dialog was force-closed; surface it exactly
                 * like the poll's own -2 below. */
                snprintf(yt_status,sizeof(yt_status),"%s",yt_osk_last_error());
                snprintf(message,sizeof(message),"Keyboard error - see the YouTube view");
            } else {
                char text[128];
                int r=yt_osk_poll(text,sizeof(text));
                if(r==1) {
                    snprintf(yt_query,sizeof(yt_query),"%s",text);
                    yt_queue_clear();
                    if(yt_job_search(yt_query)==0) { yt_view=1; yt_busy_kind=YT_JOB_SEARCH; snprintf(yt_status,sizeof(yt_status),"Searching for \"%s\"...",yt_query); }
                    else snprintf(message,sizeof(message),"YouTube is busy");
                }
                else if(r==-1) snprintf(message,sizeof(message),"Search cancelled");
                else if(r==-2) {
                    /* The keyboard closed without a usable finish
                     * (an OSK close-path deadline fired). Surface it
                     * in the YouTube view like an open failure, not
                     * only in the footer. */
                    snprintf(yt_status,sizeof(yt_status),"%s",yt_osk_last_error());
                    snprintf(message,sizeof(message),"Keyboard error - see the YouTube view");
                }
            }
        }
        /* Draw-suspended transitions are logged while the keyboard
         * is up: the suspension is the other half of the freeze-2
         * mechanism, and one log pull must show the whole sequence. */
        {
            int susp=runtime_draw_suspended();
            if(yt_osk_is_open() && susp!=yt_susp_prev)
                yt_log_sink("main",susp?"draw-suspended entered (OSK open)":"draw-suspended exited (OSK open)");
            yt_susp_prev=susp;
            if(susp) {
                previous=0;
#ifdef NL_NATIVE_DISPLAY
                if(display_idle()) running=0;
                NL_FRAME_COMPLETE();
#endif
                SDL_Delay(16); continue;
            }
        }
        unsigned held=pad_buttons(),pressed=held&~previous; previous=held;
        if(yt_osk_is_open()) {
            /* The system keyboard owns the pad while it is up; the
             * dialog itself was already serviced above, before the
             * draw-suspended branch. Swallow app input. */
            pressed=0; held=0; previous=0;
        } else {
            yt_worker_adopt_thumbs();   /* staged thumbnails become visible here, between draws */
            int kind=0,st=yt_worker_poll(&kind);
            if(st==YT_STATE_OK || st==YT_STATE_ERROR) {
                yt_worker_consume();
                if(st==YT_STATE_ERROR) {
                    snprintf(message,sizeof(message),"%s",yt_worker_error());
                    if(kind==YT_JOB_EXTRACT && yt_queue_active) {
                        /* A queue entry that will not resolve is
                         * skipped, not fatal: advance or end the
                         * queue back at the list it came from. */
                        if(yt_queue_index+1<yt_queue_count) {
                            yt_queue_index++;
                            yt_current=yt_queue[yt_queue_index];
                            if(yt_job_extract(yt_current.videoId)==0) { yt_view=1; yt_busy_kind=YT_JOB_EXTRACT; snprintf(yt_status,sizeof(yt_status),"Up next: %s",yt_current.title); }
                            else { yt_queue_clear(); yt_view=yt_info_from; }
                        } else { yt_queue_clear(); yt_view=yt_info_from; snprintf(yt_status,sizeof(yt_status),"Queue finished"); }
                    }
                    else if(kind==YT_JOB_EXTRACT) yt_view=yt_info_from;
                    else if(kind!=YT_JOB_THUMBS) yt_view=0;
                } else if(kind==YT_JOB_SEARCH || kind==YT_JOB_TRENDING) {
                    yt_count=yt_worker_results()->count; if(yt_count>24) yt_count=24;
                    yt_view=2; selection=0;
                    snprintf(message,sizeof(message),"%s: %d results",kind==YT_JOB_SEARCH?"Search":"Trending",yt_worker_results()->count);
                    yt_worker_thumbs_clear();   /* previous results' thumbnails are UI-owned; drop them here */
                    yt_job_thumbs();
                } else if(kind==YT_JOB_EXTRACT) {
                    if(yt_queue_active) {
                        /* Queue playback goes straight from resolve
                         * to play - the info view is never shown
                         * between queue entries. A failed start ends
                         * the queue on the info view, where play_yt
                         * already put the reason in the footer. */
                        const StreamInfo *info=yt_worker_stream();
                        play_yt(info,yt_current.videoId);
                        if(yt_mode) { yt_view=4; yt_status[0]=0; snprintf(message,sizeof(message),"Playing: %s",yt_current.title); }
                        else { yt_queue_clear(); yt_view=3; }
                    } else {
                        yt_view=3;
                        yt_status[0]=0;
                        snprintf(message,sizeof(message),"Resolved - X Play | O Back to results");
                    }
                }
            }
        }
        if(yt_mode) {
            /* Playback housekeeping: surface the engine's terminal
             * states, and open the audio device at FAAD2's true
             * output rate the moment the first frame is decoded
             * (the device cannot be opened before that rate is
             * known - HE-AAC would play at the wrong speed). */
            int pst=yt_player_state();
            if(pst==YT_PLAYER_ERROR) {
                char err[160]; snprintf(err,sizeof(err),"Playback error: %s",yt_player_error());
                stop_music(); yt_view=3; snprintf(yt_status,sizeof(yt_status),"%s",err);
            } else if(pst==YT_PLAYER_ENDED) {
                stop_music();
                /* Natural end is the ONLY auto-advance trigger: a
                 * queue moves to its next entry through the same
                 * worker extract job a manual pick uses. A manual
                 * stop (the O handler below) clears the queue
                 * instead, so the two can never be confused here. */
                if(yt_queue_active && yt_queue_index+1<yt_queue_count) {
                    yt_queue_index++;
                    yt_current=yt_queue[yt_queue_index];
                    if(yt_job_extract(yt_current.videoId)==0) { yt_view=1; yt_busy_kind=YT_JOB_EXTRACT; snprintf(yt_status,sizeof(yt_status),"Up next: %s",yt_current.title); }
                    else { yt_queue_clear(); yt_view=3; snprintf(yt_status,sizeof(yt_status),"Playback finished - O Back to results"); }
                } else {
                    yt_queue_clear();
                    yt_view=3; snprintf(yt_status,sizeof(yt_status),"Playback finished - O Back to results");
                }
            } else if(!audio) {
                int fr=0,fc=0,f=yt_player_audio_format(&fr,&fc);
                if(f==1) {
                    SDL_AudioSpec want,got; SDL_zero(want);
                    want.freq=fr; want.channels=fc; want.format=AUDIO_S16SYS; want.samples=2048; want.callback=audio_callback;
                    audio=SDL_OpenAudioDevice(NULL,0,&want,&got,0);
                    if(audio) { rate=fr; channels=fc; yt_player_set_output_open(1); SDL_PauseAudioDevice(audio,paused); }
                }
            }
        }
        NL_TRACE("input-complete");
        if(held && SDL_TICKS_PASSED(start,repeat_at)) { pressed|=held&(B_UP|B_DOWN|B_L2|B_R2); repeat_at=start+180; }
        if(!held) repeat_at=start+350;
        if(pressed&B_L2) { if(section==3 && focus && settings_view==1) eq_adjust(eq_selection,-1); else change_volume(-5); }
        if(pressed&B_R2) { if(section==3 && focus && settings_view==1) eq_adjust(eq_selection,1); else change_volume(5); }
        if(pressed&B_START) toggle_pause();
        if(pressed&B_L1) { if(playing>0) play_track(playing-1); }
        if(pressed&B_R1) { if(!pc_mode && playing>=0 && playing+1<library.count) play_track(playing+1); }
        if(focus && (section==1 || (section==2 && art_view==1))) {
            if(pressed&B_LEFT) gallery_step(&gallery,-1,start);
            if(pressed&B_RIGHT) gallery_step(&gallery,1,start);
            pressed&=~(B_LEFT|B_RIGHT);
        }
        if(focus && section==2 && art_view!=1) pressed&=~(B_LEFT|B_RIGHT);
        if(yt_mode) {
            /* The player owns START (pause) and O (stop) while a
             * video plays; consume the press so the generic handlers
             * below cannot also act on it. */
            if(pressed&B_START) yt_player_set_paused(paused);
            if(pressed&B_O) {
                yt_queue_clear();   /* manual stop: the queue dies here, never advances */
                stop_music();
                yt_view=3;
                snprintf(yt_status,sizeof(yt_status),"Playback stopped - X Play again | O Back to results");
                pressed=0;
            }
        }
        if(pressed&B_O) {
            if(focus && section==2 && art_view==1) art_view=0;
            else if(focus && section==2 && art_view==3) art_view=0;
            else if(focus && section==2 && art_view==2) {
                if(art_browse_view==1) {
                    if(strcmp(art_folder,sources[art_source])) { char *last=strrchr(art_folder,'/'); if(last) *last=0; refresh_art_folder(art_folder,&art_count,&art_sel); }
                    else {art_browse_view=0;art_sel=0;}
                } else art_view=0;
            }
            else if(focus && section==0 && view==1) {
                if(strcmp(folder,sources[source])) { char *last=strrchr(folder,'/'); if(last) *last=0; refresh_folder(folder,&entry_count,&selection); }
                else {view=0;selection=0;}
            } else if(focus && section==0 && view==3) {
                if(yt_view==3) yt_view=2;
                else if(yt_view==2) { yt_view=0; selection=0; }
                else if(yt_view==0) { view=0; selection=0; }
            } else if(focus && section==0 && view==2) {view=0;selection=0;}
            else if(focus && section==3 && settings_view==1) settings_view=0;
            else focus=0;
        }
        if(pressed&B_LEFT) {
            if(section==3 && focus && settings_view==1) eq_adjust(eq_selection,-1);
            else if(section==3 && focus && settings_selection==0) change_volume(-5);
            else if(section==3 && focus) {}
            else if(section==0 && focus && view==0 && selection!=0 && selection!=4 && selection!=6) selection--;
            else focus=0;
        }
        if(pressed&B_RIGHT) {
            if(section==3 && focus && settings_view==1) eq_adjust(eq_selection,1);
            else if(section==3 && focus && settings_selection==0) change_volume(5);
            else if(section==3 && focus) {}
            else if(section==0 && focus && view==0 && selection!=3 && selection!=5 && selection!=7) selection++;
            else focus=1;
        }
        if(!focus) {
            if(pressed&B_UP) section=(section+4)%5;
            if(pressed&B_DOWN) section=(section+1)%5;
            if(pressed&B_X) { if(section==4) running=0; else { focus=1; if(section==2) art_view=0; } }
        } else if(section==0) {
            int rows=view==0?8:(view==1?entry_count+1:(view==3?(yt_view==0?4:(yt_view==2?yt_count:(yt_view==5?yt_playlist.count:(yt_view==6?yt_favorites.count:1)))):library.count));
            if(rows>0) {
                if(view==0) {
                    if(pressed&B_UP) { if(selection==6) selection=4; else if(selection==7) selection=5; else if(selection>=4) selection=selection==4?0:2; else focus=0; }
                    if(pressed&B_DOWN) selection=selection>=6?selection:(selection>=4?selection+2:(selection<2?4:5));
                } else {
                    if(pressed&B_UP) selection=(selection+rows-1)%rows;
                    if(pressed&B_DOWN) selection=(selection+1)%rows;
                }
            }
            if(pressed&B_X) {
                if(view==0) {
                    if(selection<4) { source=selection; snprintf(folder,sizeof(folder),"%s",sources[source]); refresh_folder(folder,&entry_count,&selection); view=1; }
                    else if(selection==4) {view=2;selection=0;}
                    else if(selection==5) {section=1;}
                    else if(selection==7) { view=3; yt_view=0; selection=0; snprintf(message,sizeof(message),"YouTube: search or browse trending"); }
                    else { if(pc_mode) {stop_music();snprintf(message,sizeof(message),"PC Audio disconnected");} else play_pc(); }
                } else if(view==1) {
                    if(selection==0) {scan_folder(folder);view=2;selection=0;}
                    else if(entries[selection-1].directory) {
                        char path[NL_PATH];
                        if(!library_join(path,folder,entries[selection-1].name)) { snprintf(folder,sizeof(folder),"%s",path); refresh_folder(folder,&entry_count,&selection); }
                    } else {
                        char target[NL_PATH];
                        if(!library_join(target,folder,entries[selection-1].name)) {
                            scan_folder(folder); view=2; selection=0;
                            for(int i=0;i<library.count;i++) if(!strcmp(target,library.tracks[i].path)) {selection=i;break;}
                            if(library.count) play_track(selection);
                        }
                    }
                } else if(view==3) {
                    if(yt_view==0) {
                        if(selection==0) {
                            yt_log_sink("main","search selected - opening the system keyboard");
                            if(yt_osk_open(yt_query)) {
                                /* Surface the failure in the YouTube
                                 * view itself, not only the footer:
                                 * the M1 build's silent footer-only
                                 * "Keyboard unavailable" is what made
                                 * the broken search look like a dead
                                 * button (SEARCH-DIAGNOSIS.md). */
                                snprintf(yt_status,sizeof(yt_status),"%s",yt_osk_last_error());
                                snprintf(message,sizeof(message),"Keyboard unavailable - see the YouTube view");
                            } else yt_status[0]=0;
                        }
                        else if(selection==1) {
                            yt_queue_clear();
                            if(yt_job_trending()==0) { yt_view=1; yt_busy_kind=YT_JOB_TRENDING; snprintf(yt_status,sizeof(yt_status),"Loading trending..."); }
                            else snprintf(message,sizeof(message),"YouTube is busy");
                        }
                        else if(selection==2) { yt_view=5; selection=0; snprintf(message,sizeof(message),"Playlist: %d saved videos",yt_playlist.count); }
                        else if(selection==3) { yt_view=6; selection=0; snprintf(message,sizeof(message),"Favorites: %d saved videos",yt_favorites.count); }
                    } else if(yt_view==3) {
                        const StreamInfo *info=yt_worker_stream();
                        yt_queue_clear();   /* a manual play is never a queue */
                        play_yt(info,yt_current.videoId);
                        if(yt_mode) yt_view=4;
                    } else if(yt_view==2 && selection<yt_count) {
                        const SearchResults *r=yt_worker_results();
                        yt_current=r->items[selection];
                        yt_info_from=2;
                        if(yt_job_extract(yt_current.videoId)==0) { yt_view=1; yt_busy_kind=YT_JOB_EXTRACT; snprintf(yt_status,sizeof(yt_status),"Resolving \"%s\"...",yt_current.title); }
                        else snprintf(message,sizeof(message),"YouTube is busy");
                    } else if(yt_view==5 || yt_view==6) {
                        const YtList *list=yt_view==5?&yt_playlist:&yt_favorites;
                        yt_info_from=yt_view;
                        if(yt_queue_start(list,selection)) {
                            if(yt_job_extract(yt_current.videoId)==0) { yt_view=1; yt_busy_kind=YT_JOB_EXTRACT; snprintf(yt_status,sizeof(yt_status),"Resolving \"%s\"...",yt_current.title); }
                            else { yt_queue_clear(); snprintf(message,sizeof(message),"YouTube is busy"); }
                        } else snprintf(message,sizeof(message),"Nothing playable here - live entries are skipped in queues");
                    }
                } else if(library.count) play_track(selection);
            }
            if(view==3 && (pressed&B_TRI || pressed&B_SQ)) {
                /* Triangle files the picked video into the playlist,
                 * Square into favorites - from the results list or
                 * the info view (where yt_current describes it). */
                const SearchResult *item=NULL;
                if(yt_view==2 && selection<yt_count) item=&yt_worker_results()->items[selection];
                else if(yt_view==3) item=&yt_current;
                if(item) {
                    int which=(pressed&B_SQ)?YT_LIST_FAVORITES:YT_LIST_PLAYLIST;
                    const char *lname=which==YT_LIST_FAVORITES?"favorites":"playlist";
                    int rc=yt_list_add(which,item);
                    if(rc==0) snprintf(message,sizeof(message),"Added to %s: %s",lname,item->title);
                    else if(rc==1) snprintf(message,sizeof(message),"Already in your %s",lname);
                    else snprintf(message,sizeof(message),"Your %s is full (100 entries)",lname);
                }
            }
        } else if(section==2) {
            if(art_view==0) {
                if(pressed&B_UP) art_selection=(art_selection+3)%4;
                if(pressed&B_DOWN) art_selection=(art_selection+1)%4;
                if(pressed&B_X) {
                    if(art_selection==0) art_view=1;
                    else if(art_selection==1) { art_view=2; art_browse_view=0; art_sel=0; }
                    else if(art_selection==2) {
                        char old_title[64],err[120];
                        snprintf(old_title,sizeof(old_title),"%s",gallery_title(&gallery));
                        if(gallery_remove_current(&gallery,err,sizeof(err))) snprintf(message,sizeof(message),"%s",err);
                        else snprintf(message,sizeof(message),"Removed: %s (backup kept)",old_title);
                    } else art_view=3;
                }
            } else if(art_view==2) {
                int rows=art_browse_view==0?4:art_count;
                if(rows>0) {
                    if(pressed&B_UP) art_sel=(art_sel+rows-1)%rows;
                    if(pressed&B_DOWN) art_sel=(art_sel+1)%rows;
                }
                if(pressed&B_X) {
                    if(art_browse_view==0) {
                        art_source=art_sel; snprintf(art_folder,sizeof(art_folder),"%s",sources[art_source]);
                        refresh_art_folder(art_folder,&art_count,&art_sel); art_browse_view=1;
                    } else if(art_count>0) {
                        if(art_entries[art_sel].directory) {
                            char path[NL_PATH];
                            if(!library_join(path,art_folder,art_entries[art_sel].name)) { snprintf(art_folder,sizeof(art_folder),"%s",path); refresh_art_folder(art_folder,&art_count,&art_sel); }
                        } else {
                            char target[NL_PATH],err[120];
                            if(!library_join(target,art_folder,art_entries[art_sel].name)) {
                                if(gallery_add(&gallery,target,err,sizeof(err))) snprintf(message,sizeof(message),"%s",err);
                                else snprintf(message,sizeof(message),"Added: %s",art_entries[art_sel].name);
                            }
                        }
                    }
                }
            }
        } else if(section==3) {
            if(settings_view==1) {
                if(pressed&B_UP) eq_selection=(eq_selection+EQ_BANDS)%(EQ_BANDS+1);
                if(pressed&B_DOWN) eq_selection=(eq_selection+1)%(EQ_BANDS+1);
                if(pressed&B_X) {
                    if(eq_selection<EQ_BANDS) eq_set_gain(eq_selection,0);
                    else eq_apply_preset(0);
                    eq_save();
                }
                if(pressed&B_SQ && eq_selection==EQ_BANDS) {
                    int slot=eq_save_custom();
                    snprintf(message,sizeof(message),"Saved as %s - it joins the preset cycle",eq_user_preset_name[slot]);
                }
            } else {
            if(pressed&B_UP) settings_selection=(settings_selection+4)%5;
            if(pressed&B_DOWN) settings_selection=(settings_selection+1)%5;
            if(pressed&B_X) {
                if(settings_selection==4) { settings_view=1; eq_selection=0; }
                else if(settings_selection==1) {
                    int ftp_state=ftp_server_status();
                    if(ftp_state==0 || ftp_state==-1) {
                        if(ftp_server_start(ASSETS "art/gallery")) snprintf(message,sizeof(message),"FTP server could not start");
                        else snprintf(message,sizeof(message),"FTP server starting...");
                    } else {
                        ftp_server_stop();
                        snprintf(message,sizeof(message),"FTP server off");
                    }
                } else if(settings_selection==2 && !update_pending) { update_pending=1; snprintf(message,sizeof(message),"Checking for updates..."); }
                else if(settings_selection==3) {
                    if(yt_worker_poll(NULL)==YT_STATE_RUNNING) snprintf(message,sizeof(message),"YouTube is busy - reset the session when it finishes");
                    else { yt_reset_visitor(); snprintf(message,sizeof(message),"YouTube session reset - a fresh one is minted on next use"); }
                }
            }
            }
        } else if(section==1) {
            if(pressed&B_TRI) {
                viz_mode=(viz_mode+1)%VIZ_MODE_COUNT;
                viz_save();
                snprintf(message,sizeof(message),"Visualizer: %s (Triangle switches)",visualizer_mode_name(viz_mode));
            }
            else if(pressed&B_X) toggle_pause();
        }
        NL_INPUT(pressed,section,focus);
        if(pc_mode) {
            static const char *status[]={"PC Audio stopped","Finding PC: start the Neon Lounge PC companion","PC Audio buffering...","PC Audio connected | Spotify / YouTube run on your PC"};
            int state=pc_audio_status();
            snprintf(message,sizeof(message),"%s",state<0?"PC disconnected. Choose PC Audio to stop, then reconnect.":status[state]);
        }
        if(ftp_take_gallery_dirty()) {
            /* The FTP thread only writes files; the gallery reload runs
             * here on the main thread, like Add artwork does. */
            gallery_close(&gallery); gallery_open(&gallery,ASSETS "art/gallery");
            snprintf(message,sizeof(message),"Gallery updated via FTP (%d / %d slides)",gallery.loaded,NL_GALLERY_SLIDES);
        }
        if(audio && !pc_mode && !yt_mode && SDL_AtomicGet(&finished)) {
            int next=playing+1;
            if(next<library.count) play_track(next); else {stop_music();snprintf(message,sizeof(message),"Playlist finished");}
        }
        float snapshot[SV_FFT]={0}; Spectrum spectrum;
        if(audio && !paused) {
            SDL_LockAudioDevice(audio);
            for(unsigned i=0;i<SV_FFT;i++) snapshot[i]=pcm[(pcm_position+i)%SV_FFT];
            SDL_UnlockAudioDevice(audio);
        }
        if(spectrum_plan.rate!=rate) spectrum_plan_init(&spectrum_plan,rate);
        spectrum_analyze_plan(snapshot,&spectrum_plan,&spectrum);
        for(int i=0;i<SV_BANDS;i++) smooth[i]=smooth[i]*0.70f+spectrum.bands[i]*0.30f;
        int yt_playing_view=(section==0 && focus && view==3 && yt_view==4);
        /* Frame persistence (PLAYBACK2-DIAGNOSIS fix 2): in the
         * playing view the canvas is NOT repainted from scratch -
         * no menu background, no per-frame black fill - so the
         * last presented frame stays on screen through any present
         * gap (HOLD, preroll, rebuffer) and a gap shows a frozen
         * frame, never black. The canvas is cleared exactly once,
         * on entry; after that the engine repaints the picture
         * area every frame (new frame, or the frozen copy via
         * yt_player_redraw_last) and the OSD is drawn over it. */
        static int yt_playing_prev;
        int yt_playing_entered=yt_playing_view && !yt_playing_prev;
        yt_playing_prev=yt_playing_view;
        if(!yt_playing_view) draw_gc_background(screen);
        if(yt_playing_view) {
            /* Fullscreen video: black letterbox on entry, the
             * engine blits the due frame (or repaints the frozen
             * last one), then a minimal OSD. */
            if(yt_playing_entered) ui_rect(screen,0,0,1280,720,0x000000);
            if(!yt_player_present(screen)) yt_player_redraw_last(screen);
            ui_text(screen,28,20,26,0xffffff,yt_player_title());
            char osd[200];
            unsigned long long pos=yt_player_position_ns()/1000000000ull,dur=yt_player_duration_ns()/1000000000ull;
            if(yt_player_buffering()) snprintf(osd,sizeof(osd),"Buffering...");
            else if(yt_player_paused()) snprintf(osd,sizeof(osd),"Paused | %llu:%02llu / %llu:%02llu",pos/60,pos%60,dur/60,dur%60);
            else snprintf(osd,sizeof(osd),"%llu:%02llu / %llu:%02llu",pos/60,pos%60,dur/60,dur%60);
            ui_text(screen,28,664,18,0xa8c686,osd);
            ui_text(screen,940,664,18,0xa8c686,"START Pause   O Stop");
        } else if((section==1 && focus) || (section==2 && focus && art_view==1)) {
#ifdef NL_DIAGNOSTIC
            /* Offline autoplay probe exercises every loaded slide while
             * decoding audio. The PS3 music marker never enables this. */
            if(diagnostic_gallery_sweep && diagnostic_frames%10==0) {
                if(diagnostic_frames) gallery_step(&gallery,1,start);
                NL_EVENT_VALUE("gallery-slide",gallery.index);
            }
#endif
            gallery_tick(&gallery,start);
            if(gallery.loaded) gallery_draw(&gallery,screen,0,0,1280,720,start);
            else if(background) SDL_BlitScaled(background,NULL,screen,NULL);
            if(section==1) {
                /* The draw takes the whole Spectrum: bands come from
                 * the smoothed copy the UI has always shown, while
                 * bass/rms/peak ride along for the v1.18 modes. */
                Spectrum vs=spectrum;
                memcpy(vs.bands,smooth,sizeof(smooth));
                visualizer_draw(screen,&vs,viz_mode,(int64_t)time(NULL),start);
            }
            ui_rect(screen,24,18,650,42,0x0d1206);
            ui_text(screen,38,24,26,0xf2f7ff,gallery_title(&gallery));
        } else {
            ui_rect(screen,0,0,230,635,0x11160a);
            ui_text(screen,26,26,26,0xc4f09a,"NEON LOUNGE");
            draw_spinner(screen,start);
            for(int i=0;i<5;i++) {
                if(section==i) {
                    if(focus) ui_rect(screen,11,196+i*72,208,60,0xd9ffb3);
                    ui_rect(screen,14,199+i*72,202,54,focus?0x5c8a1e:0x2c4416);
                    ui_rect(screen,14,199+i*72,4,54,0x9ede3c);
                }
                ui_text(screen,36,209+i*72,26,0xf2f7ff,nav[i]);
            }
            ui_text(screen,270,30,42,0xf2f7ff,nav[section]);
            if(update_notice[0]) ui_text(screen,880,36,26,0xffc94d,update_notice);
            if(section==0) {
                ui_text(screen,274,88,18,0xa8c686,view==1?folder:(view==3?"YouTube | Search, trending and playback":"Your music | Internal HDD and USB"));
                const char *home[]={"Music folder","Internal HDD","USB 0","USB 1","Current playlist","Start visualizer",pc_mode?"Disconnect PC Audio":"PC Audio - connect","YouTube"};
                if(view==0 && background) {
                    ui_rect(screen,270,190,950,280,0x11160a);
                    draw_art(screen,background,270,190,950,280);
                    ui_text(screen,294,232,42,0xffffff,"Your music.");
                    ui_text(screen,294,284,42,0x9ede3c,"Your lounge.");
                    ui_text(screen,294,358,18,0xa8c686,"MP3 playback and audio-reactive spectrum");
                }
                int count=view==0?8:(view==1?entry_count+1:(view==3?0:library.count));
                int first=selection>7?selection-7:0;
                for(int row=first;row<count && row<first+9;row++) {
                    int y=138+(row-first)*44,x=266,w=960,pixels=26; char label[300];
                    if(view==0) {
                        x=row<4?266+row*240:(row==6?266:(row==7?746:266+(row-4)*480));
                        y=row<4?130:((row==6||row==7)?546:496); w=row<4?230:470; pixels=row<4?18:26;
                    }
                    if(view==0) snprintf(label,sizeof(label),"%s",home[row]);
                    else if(view==1) { if(!row) snprintf(label,sizeof(label),"[ Play this folder and subfolders ]"); else snprintf(label,sizeof(label),"%s%s",entries[row-1].directory?"[Folder] ":"",entries[row-1].name); }
                    else snprintf(label,sizeof(label),"%s",library.tracks[row].name);
                    draw_gc_tile(screen,x,y,w,view==0?(row<4?56:48):40,focus && selection==row);
                    ui_text(screen,x+14,y+(view==0?(row<4?16:9):4),pixels,0xf4f6ff,label);
                }
                if(!count && view!=3) ui_text(screen,280,170,26,0xcfe0bd,"No MP3 tracks. Browse a source folder first.");
                if(view==3) {
                    if(yt_view==0) {
                        const char *ytmenu[]={"Search YouTube","Trending (Gaming)","Playlist","Favorites"};
                        for(int i=0;i<4;i++) {
                            draw_gc_tile(screen,266,138+i*44,960,40,focus && selection==i);
                            ui_text(screen,280,142+i*44,26,0xf4f6ff,ytmenu[i]);
                        }
                        if(yt_status[0]) ui_text(screen,280,330,18,0xffc94d,yt_status);
                        if(yt_query[0]) { char q[192]; snprintf(q,sizeof(q),"Last search: %s",yt_query); ui_text(screen,280,358,18,0xa8c686,q); }
                        ui_text(screen,280,388,18,0xa8c686,"Search opens the system keyboard. Pick a result, then X plays it");
                        ui_text(screen,280,414,18,0xa8c686,"(up to 720p H.264 + AAC, decoded on the console).");
                        ui_text(screen,280,452,18,0xa8c686,"Extraction based on mohasi's ps3-dev and TEE Vanced (Apache-2.0).");
                    } else if(yt_view==1) {
                        ui_text(screen,280,170,26,0xffffff,yt_status);
                        ui_text(screen,280,212,18,0xa8c686,"Working... the app stays responsive; you can leave any time.");
                    } else if(yt_view==2) {
                        const SearchResults *r=yt_worker_results();
                        int first=selection>6?selection-6:0;
                        for(int row=first;row<yt_count && row<first+7;row++) {
                            int y=138+(row-first)*52;
                            draw_gc_tile(screen,266,y,960,48,focus && selection==row);
                            SDL_Surface *thumb=yt_worker_thumb(row);
                            if(thumb) { SDL_Rect d={272*screen->w/1280,(y+4)*screen->h/720,71*screen->w/1280,40*screen->h/720}; SDL_BlitScaled(thumb,NULL,screen,&d); }
                            ui_text(screen,352,y+2,18,0xf4f6ff,r->items[row].title);
                            char meta[300];
                            if(r->items[row].isLive) snprintf(meta,sizeof(meta),"%s | LIVE",r->items[row].author);
                            else snprintf(meta,sizeof(meta),"%s | %s | %s",r->items[row].author,r->items[row].duration,r->items[row].views);
                            ui_text(screen,352,y+24,18,0xa8c686,meta);
                        }
                        if(!yt_count) ui_text(screen,280,170,26,0xcfe0bd,"No results. Try another search.");
                        else ui_text(screen,280,520,18,0xa8c686,"X Resolve | Triangle Add to playlist | Square Favorite");
                    } else if(yt_view==5 || yt_view==6) {
                        /* File-backed lists draw as text-only tiles:
                         * thumbnails belong to the worker's live
                         * results, not to saved entries. */
                        const YtList *list=yt_view==5?&yt_playlist:&yt_favorites;
                        ui_text(screen,280,104,18,0xa8c686,yt_view==5?"Your playlist | X plays from the picked entry, then keeps going":"Your favorites | X plays from the picked entry, then keeps going");
                        int first=selection>6?selection-6:0;
                        for(int row=first;row<list->count && row<first+7;row++) {
                            int y=138+(row-first)*52;
                            draw_gc_tile(screen,266,y,960,48,focus && selection==row);
                            ui_text(screen,280,y+2,18,0xf4f6ff,list->items[row].title);
                            char meta[300];
                            if(list->items[row].isLive) snprintf(meta,sizeof(meta),"%s | LIVE (listed, but skipped when the list plays)",list->items[row].author);
                            else snprintf(meta,sizeof(meta),"%s | %s",list->items[row].author,list->items[row].duration);
                            ui_text(screen,280,y+24,18,0xa8c686,meta);
                        }
                        if(!list->count) ui_text(screen,280,170,26,0xcfe0bd,yt_view==5?"Playlist is empty. Triangle on a result adds it.":"No favorites yet. Square on a result adds it.");
                    } else {
                        const StreamInfo *info=yt_worker_stream();
                        ui_text(screen,280,140,26,0xffffff,info->title[0]?info->title:yt_current.title);
                        int videos=0,audios=0; const StreamFormat *bv=NULL,*ba=NULL;
                        for(int i=0;i<info->formatCount;i++) {
                            const StreamFormat *f=&info->formats[i];
                            if(f->hasVideo) { videos++; if(!f->hasAudio && f->height<=720 && (!bv || f->height>bv->height)) bv=f; }
                            if(f->hasAudio && !f->hasVideo) { audios++; if(!ba && !f->isOpus) ba=f; }
                        }
                        char line[300];
                        snprintf(line,sizeof(line),"%d formats offered: %d video, %d audio | duration %d:%02d",info->formatCount,videos,audios,info->durationSeconds/60,info->durationSeconds%60);
                        ui_text(screen,280,190,18,0xcfe0bd,line);
                        if(bv) { snprintf(line,sizeof(line),"Best video up to 720p: itag %d, %dx%d at %d fps (%s)",bv->itag,bv->width,bv->height,bv->fps,bv->container); ui_text(screen,280,222,18,0xcfe0bd,line); }
                        if(ba) { snprintf(line,sizeof(line),"Audio: itag %d (%s)%s",ba->itag,ba->container,ba->isDrc?" DRC":""); ui_text(screen,280,254,18,0xcfe0bd,line); }
                        if(info->isLive) ui_text(screen,280,286,18,0xffc94d,"This is a live stream (live playback arrives later).");
                        if(yt_status[0]) ui_text(screen,280,318,18,0xffc94d,yt_status);
                        ui_text(screen,280,352,18,0xa8c686,"X Play | O Back | Triangle Playlist | Square Favorite | Log: USRDIR/youtube.log");
                    }
                }
            } else if(section==1 || section==2) {
                if(section==2 && focus && art_view==0) {
                    const char *opts[]={"View gallery","Add artwork","Remove artwork","Get pictures"};
                    char gallery_label[120];
                    for(int i=0;i<4;i++) {
                        draw_gc_tile(screen,266,138+i*44,960,40,art_selection==i);
                        ui_text(screen,280,142+i*44,26,0xf4f6ff,opts[i]);
                    }
                    ui_text(screen,280,340,18,0xa8c686,"Add: BMP pictures, 1280x720 or smaller, from HDD or USB");
                    ui_text(screen,280,366,18,0xa8c686,"Remove: current artwork is moved to a backup folder");
                    snprintf(gallery_label,sizeof(gallery_label),"%d / %d slides loaded | %s",gallery.loaded,NL_GALLERY_SLIDES,gallery_title(&gallery));
                    ui_text(screen,280,400,18,0xa8c686,gallery_label);
                } else if(section==2 && focus && art_view==2) {
                    int count=art_browse_view==0?4:art_count;
                    int first=art_sel>7?art_sel-7:0;
                    ui_text(screen,274,88,18,0xa8c686,art_browse_view==1?art_folder:"Choose a source to browse for BMP pictures");
                    for(int row=first;row<count && row<first+9;row++) {
                        int y=138+(row-first)*44; char label[300];
                        if(art_browse_view==0) snprintf(label,sizeof(label),"%s",sources[row]);
                        else snprintf(label,sizeof(label),"%s%s",art_entries[row].directory?"[Folder] ":"",art_entries[row].name);
                        draw_gc_tile(screen,266,y,960,40,art_sel==row);
                        ui_text(screen,280,y+4,26,0xf4f6ff,label);
                    }
                    if(!count) ui_text(screen,280,170,26,0xcfe0bd,"No BMP pictures here. Browse another folder.");
                } else if(section==2 && focus && art_view==3) {
                    const char *sites[]={"X - https://x.com","DeviantArt - https://www.deviantart.com","Pixiv - https://www.pixiv.net","ArtStation - https://www.artstation.com","Zerochan - https://www.zerochan.net","Pinterest - https://www.pinterest.com"};
                    ui_text(screen,280,140,26,0xffffff,"Get pictures from these sites:");
                    for(int i=0;i<6;i++) ui_text(screen,280,180+i*36,26,0x9ede3c,sites[i]);
                    ui_text(screen,280,420,18,0xa8c686,"Save a picture on your PC or phone, convert it to BMP");
                    ui_text(screen,280,446,18,0xa8c686,"(1280x720 or smaller), copy it to USB, then Add artwork.");
                    ui_text(screen,280,472,18,0xa8c686,"Use artwork you have permission to use.");
                } else {
                    if(gallery.loaded) gallery_draw(&gallery,screen,270,135,950,400,start);
                    else if(background) {
                        draw_art(screen,background,270,135,950,400);
                    }
                    if(section==2) ui_text(screen,280,540,18,0x9ede3c,"X Artwork options | Add / Remove / Get pictures");
                    else ui_text(screen,280,540,18,0x9ede3c,"X Fullscreen | O Back | Left/Right Artwork | Triangle Visualizer (in view)");
                    char gallery_label[120];
                    snprintf(gallery_label,sizeof(gallery_label),"%d / %d slides loaded | %s",gallery.loaded,NL_GALLERY_SLIDES,gallery_title(&gallery));
                    ui_text(screen,280,574,18,0xa8c686,gallery_label);
                }
            } else if(section==3) {
                if(settings_view==1) {
                    /* Equalizer editor: five band sliders plus a
                     * preset row, on the same 44 px menu pitch the
                     * Artwork and YouTube menus use. */
                    ui_text(screen,280,88,18,0xa8c686,"Equalizer | Applies to MP3, PC Audio and YouTube");
                    for(int i=0;i<EQ_BANDS;i++) {
                        int y=138+i*44,g=eq_gain(i);
                        char label[64],val[24];
                        snprintf(label,sizeof(label),"%s | %d Hz",eq_band_name[i],eq_band_freq[i]);
                        draw_gc_tile(screen,266,y,960,40,focus && eq_selection==i);
                        ui_text(screen,280,y+4,26,0xf4f6ff,label);
                        ui_rect(screen,660,y+14,360,12,0x1c2b0e);
                        ui_rect(screen,838,y+9,4,22,0x9ede3c);
                        if(g>0) ui_rect(screen,840,y+14,g*15,12,0x9ede3c);
                        else if(g<0) ui_rect(screen,840+g*15,y+14,-g*15,12,0x9ede3c);
                        snprintf(val,sizeof(val),"%+d dB",g);
                        ui_text(screen,1040,y+4,26,0xffffff,val);
                    }
                    {
                        int y=138+EQ_BANDS*44;
                        char plabel[96];
                        snprintf(plabel,sizeof(plabel),"Preset: %s",eq_preset_label());
                        draw_gc_tile(screen,266,y,960,40,focus && eq_selection==EQ_BANDS);
                        ui_text(screen,280,y+4,26,0xf4f6ff,plabel);
                        ui_text(screen,660,y+9,18,0xa8c686,"Left / Right choose | Square saves as Custom");
                    }
                    ui_text(screen,280,414,18,0xa8c686,"Up / Down select | Left / Right or L2 / R2 tune | X reset | O back");
                    ui_text(screen,280,440,18,0xa8c686,"Boosts add automatic headroom so loud passages do not clip.");
                    if(eq_is_flat()) ui_text(screen,280,466,18,0xcfe0bd,"All bands at 0 dB - the sound is unchanged.");
                } else {
                /* Settings rows share one uniform geometry
                 * (settings_layout.h): tiles on SET_PITCH, labels
                 * inside their tile, description lines on SET_LINE
                 * below it - no row's content can reach the next
                 * row's tile (the 1.15 hand-placed rows overlapped).
                 * Five rows now share the four-row window: sfoff
                 * scrolls the list by one when the Equalizer row is
                 * selected, so every visible row keeps the exact
                 * proven geometry. */
                char ftp_label[64],ftp_info[160],ftp_addr[48];
                int ftp_state=ftp_server_status();
                int sfoff=settings_selection>3?1:0;
                if(sfoff==0) {
                draw_gc_tile(screen,SET_X,SET_ROW_Y(0),SET_W,SET_TILE_H,focus && settings_selection==0);
                ui_text(screen,SET_TEXT_X,SET_ROW_Y(0)+SET_LABEL_DY,26,0xffffff,"Playback volume");
                ui_rect(screen,SET_TEXT_X,SET_BAR_Y(0),SET_BAR_W,SET_BAR_H,0x1c2b0e); ui_rect(screen,SET_TEXT_X,SET_BAR_Y(0),SDL_AtomicGet(&volume)*SET_BAR_W/100,SET_BAR_H,0x9ede3c);
                ui_text(screen,SET_TEXT_X,SET_HINT_Y(0),18,0xcfe0bd,"Left / Right or L2 / R2 to adjust");
                }
                snprintf(ftp_label,sizeof(ftp_label),"FTP Server: %s",ftp_state==2?"On":(ftp_state==1?"Starting...":(ftp_state==-1?"Error":"Off")));
                draw_gc_tile(screen,SET_X,SET_ROW_Y(1-sfoff),SET_W,SET_TILE_H,focus && settings_selection==1);
                ui_text(screen,SET_TEXT_X,SET_ROW_Y(1-sfoff)+SET_LABEL_DY,26,0xffffff,ftp_label);
                if(ftp_state==2 && ftp_server_address(ftp_addr,sizeof(ftp_addr))==0)
                    snprintf(ftp_info,sizeof(ftp_info),"Upload BMP pictures from a PC: %s (anonymous login)",ftp_addr);
                else if(ftp_state==1)
                    snprintf(ftp_info,sizeof(ftp_info),"Starting the FTP server...");
                else if(ftp_state==-1)
                    snprintf(ftp_info,sizeof(ftp_info),"FTP server could not start - check the network connection");
                else
                    snprintf(ftp_info,sizeof(ftp_info),"Turn on to upload pictures from a PC, no USB drive needed");
                ui_text(screen,SET_TEXT_X,SET_DESC_Y(1-sfoff),18,0xa8c686,ftp_info);
                draw_gc_tile(screen,SET_X,SET_ROW_Y(2-sfoff),SET_W,SET_TILE_H,focus && settings_selection==2);
                ui_text(screen,SET_TEXT_X,SET_ROW_Y(2-sfoff)+SET_LABEL_DY,26,0xffffff,"Check for updates");
                ui_text(screen,SET_TEXT_X,SET_DESC_Y(2-sfoff),18,0xa8c686,"Neon Lounge " NL_VERSION " | Updates from GitHub");
                ui_text(screen,SET_TEXT_X,SET_DESC_Y(2-sfoff)+SET_LINE,18,0xcfe0bd,"MP3 playback | HDD, USB, PC Audio and YouTube");
                draw_gc_tile(screen,SET_X,SET_ROW_Y(3-sfoff),SET_W,SET_TILE_H,focus && settings_selection==3);
                ui_text(screen,SET_TEXT_X,SET_ROW_Y(3-sfoff)+SET_LABEL_DY,26,0xffffff,"Reset YouTube session");
                ui_text(screen,SET_TEXT_X,SET_DESC_Y(3-sfoff),18,0xa8c686,"Clears the saved anonymous YouTube session if sign-in checks misbehave");
                char perf[120]; snprintf(perf,sizeof(perf),"%u FPS | Target 60 | Auto exit: 10 seconds stalled / below 10 FPS",fps);
                ui_text(screen,SET_TEXT_X,SET_DESC_Y(3-sfoff)+SET_LINE,18,0xa8c686,perf);
                /* The notice line only fits when the window is at
                 * the top: scrolled down (sfoff==1) it would strike
                 * through the Equalizer tile - the header badge at
                 * the top of the screen still shows it then. */
                if(update_notice[0] && sfoff==0) ui_text(screen,SET_TEXT_X,SET_DESC_Y(3)+2*SET_LINE,18,0xffc94d,update_notice);
                if(sfoff==1) {
                {
                    char eq_label[96];
                    snprintf(eq_label,sizeof(eq_label),"Equalizer: %s",eq_is_flat()?"Off (flat)":eq_preset_label());
                    draw_gc_tile(screen,SET_X,SET_ROW_Y(4-sfoff),SET_W,SET_TILE_H,focus && settings_selection==4);
                    ui_text(screen,SET_TEXT_X,SET_ROW_Y(4-sfoff)+SET_LABEL_DY,26,0xffffff,eq_label);
                    ui_text(screen,SET_TEXT_X,SET_DESC_Y(4-sfoff),18,0xa8c686,"Five-band EQ for MP3, PC Audio and YouTube | X to tune");
                }
                }
                }
            } else if(section==4) ui_text(screen,280,150,26,0xffffff,"Press X to exit Neon Lounge");
        }
        if(!yt_playing_view) {
        ui_rect(screen,0,608,1280,112,0x11160a);
        ui_text(screen,28,620,26,0xffffff,pc_mode?"PC Audio | Live stereo stream":(playing>=0?library.tracks[playing].name:"No track selected"));
        ui_text(screen,28,656,18,0xa8c686,message);
        char volume_label[30]; snprintf(volume_label,sizeof(volume_label),"Volume %d%%",SDL_AtomicGet(&volume));
        ui_text(screen,1000,622,26,0x9ede3c,volume_label);
        char fps_label[24]; snprintf(fps_label,sizeof(fps_label),"%u FPS",fps);
        ui_text(screen,1000,656,18,0xa8c686,fps_label);
        ui_text(screen,28,689,18,0xa8c686,"X Select   O Back   START Pause   L1/R1 Track   L2/R2 Volume");
        }
        NL_TRACE("before-present");
        if(display_present()) running=0;
        NL_TRACE("present-complete");
        /* Run the update check only after a frame showing "Checking for
         * updates..." has been presented, so the check never looks like
         * a silent freeze. The check itself still blocks this thread. */
        if(running) {
            if(update_pending==2) {
                update_pending=0;
                update_ran=1; safety_allow_seconds(650);
                update_check_and_install(ASSETS,upd_msg,sizeof(upd_msg));
                safety_allow_seconds(10);
                snprintf(message,sizeof(message),"%s",upd_msg);
            } else if(update_pending==1) update_pending=2;
            else if(!auto_update_checked && SDL_GetTicks()-boot_ticks>3000) {
                /* One-shot startup notice: tell the player an update is
                 * out without downloading anything. Silent on any
                 * failure; update_ran skips the slow-frame exit for
                 * the blocking fetch, as in the manual check. */
                char avail[16];
                auto_update_checked=1;
                update_ran=1;
                safety_allow_seconds(30);
                if(update_check_available(ASSETS,avail,sizeof(avail))==1) {
                    snprintf(update_notice,sizeof(update_notice),"Update %s available!",avail);
                    snprintf(message,sizeof(message),"Update %s available - Settings > Check for updates",avail);
                }
                safety_allow_seconds(10);
            }
        }
        Uint32 elapsed=SDL_GetTicks()-start;
        if(!update_ran && frame_health_update(&health,elapsed)) {
            FILE *f=fopen(ASSETS "safety-exit.log","w");
            if(f) { fprintf(f,"Slow rendering: ten seconds below 10 FPS. Last frame %u ms.\n",elapsed); fclose(f); }
            running=0;
        }
        /* Native RSX already waits for vblank; a second sleep can miss
         * the next refresh. Only pace the non-vsync fallback here. */
#ifndef NL_NATIVE_DISPLAY
        static unsigned pace; unsigned budget=(++pace%3)==0?16:17;
        if(elapsed<budget) SDL_Delay(budget-elapsed);
#endif
        fps_frames++;
        if(start-fps_start>=1000) { fps=fps_frames*1000/(start-fps_start); fps_frames=0; fps_start=start; }
        NL_TRACE("delay-complete");
        NL_FRAME_COMPLETE();
    }
    NL_TRACE("before-cleanup");
    safety_beat();
    stop_music();
    if(pad_init==PAD_OK) ioPadEnd();
    xbox_frames_close();
    gallery_close(&gallery);
    ftp_server_stop();
    yt_osk_shutdown();
    yt_worker_stop();
    update_net_shutdown();
    SDL_FreeSurface(art_cache); SDL_FreeSurface(background); ui_font_close(); display_close(); runtime_shutdown();
    safety_stop();
    SDL_Quit();
    NL_TRACE("cleanup-complete");
    NL_DIAGNOSTIC_FINISH();
    return 0;
}
