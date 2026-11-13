#include <SDL.h>
#include <io/pad.h>
#include <sys/process.h>
#include <sysutil/sysutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
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
static int rate=48000,channels=2,playing=-1,paused,decoder_open;
static char message[160]="Choose a folder or an MP3 to begin";
static size_t file_read(void *buf,size_t size,void *userdata) { return fread(buf,1,size,(FILE*)userdata); }
static int file_seek(uint64_t position,void *userdata) {
    return position>LONG_MAX?-1:fseek((FILE*)userdata,(long)position,SEEK_SET);
}
static void audio_callback(void *userdata,Uint8 *stream,int bytes) {
    (void)userdata;
    memset(stream,0,bytes);
    size_t count=mp3dec_ex_read(&decoder,(mp3d_sample_t*)stream,(size_t)bytes/2);
#ifdef NL_DIAGNOSTIC
    if(count) SDL_AtomicAdd(&decoded_blocks,1);
#endif
    Sint16 *samples=(Sint16*)stream;
    int gain=SDL_AtomicGet(&volume);
    for(size_t i=0;i<count;i++) samples[i]=(Sint16)((int)samples[i]*gain/100);
    for(size_t i=0;i<count;i+=(size_t)channels) {
        float v=(float)samples[i]/32768.0f;
        if(channels==2 && i+1<count) v=(v+(float)samples[i+1]/32768.0f)*0.5f;
        pcm[pcm_position++%SV_FFT]=v;
    }
    if(count<(size_t)bytes/2) SDL_AtomicSet(&finished,1);
}
static void stop_music(void) {
    NL_EVENT("audio-close-before");
    if(audio) SDL_CloseAudioDevice(audio);
    NL_EVENT("audio-close-complete");
#ifdef NL_DIAGNOSTIC
    NL_EVENT_VALUE("decoded-blocks",SDL_AtomicGet(&decoded_blocks));
#endif
    audio=0;
    if(decoder_open) mp3dec_ex_close(&decoder);
    decoder_open=0;
    if(music_file) fclose(music_file);
    music_file=NULL; playing=-1; paused=0;
    memset(pcm,0,sizeof(pcm)); pcm_position=0;
    SDL_AtomicSet(&finished,0);
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
static void toggle_pause(void) { if(audio) { paused=!paused; SDL_PauseAudioDevice(audio,paused); } }
static void change_volume(int delta) {
    int v=SDL_AtomicGet(&volume)+delta;
    SDL_AtomicSet(&volume,v<0?0:(v>100?100:v));
}
static void draw_art(SDL_Surface *screen,SDL_Surface *art,int x,int y,int w,int h) {
    if(!art) return;
    float scale=(float)w/art->w;
    if((float)h/art->h<scale) scale=(float)h/art->h;
    int width=(int)(art->w*scale),height=(int)(art->h*scale);
    SDL_Rect dst={(x+w-width)*screen->w/1280,(y+(h-height)/2)*screen->h/720,width*screen->w/1280,height*screen->h/720};
    SDL_BlitScaled(art,NULL,screen,&dst);
}
enum { B_UP=1,B_DOWN=2,B_LEFT=4,B_RIGHT=8,B_X=16,B_O=32,B_START=64,B_L1=128,B_R1=256,B_L2=512,B_R2=1024 };
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
        (data.BTN_L1?B_L1:0)|(data.BTN_R1?B_R1:0)|(data.BTN_L2?B_L2:0)|(data.BTN_R2?B_R2:0);
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
int main(int argc,char **argv) {
    (void)argc; (void)argv;
    SDL_Surface *screen=NULL,*background=NULL;
    const char *nav[]={"Music","Visualizer","Artwork","Settings","Exit"};
    const char *sources[]={"/dev_hdd0/music","/dev_hdd0","/dev_usb000","/dev_usb001"};
    char folder[NL_PATH]="/dev_hdd0/music";
    char art_folder[NL_PATH]="/dev_hdd0";
    int running=1,section=0,focus=0,view=0,selection=0,entry_count=0,source=0;
    int art_view=0,art_selection=0,art_count=0,art_sel=0,art_source=0,art_browse_view=0;
    int settings_selection=0,update_pending=0;
    char upd_msg[160];
    unsigned previous=0; Uint32 repeat_at=0; float smooth[SV_BANDS]={0};
    Gallery gallery;
#ifdef NL_DIAGNOSTIC
    int diagnostic_gallery_sweep=0;
#endif
    SDL_AtomicSet(&volume,75);
    if(NL_DIAGNOSTIC_START()) return 6;
    NL_TRACE("before-sdl-init");
    if(SDL_Init(display_init_flags()|SDL_INIT_AUDIO|SDL_INIT_TIMER)) return 1;
    if(runtime_init()) { SDL_Quit(); return 4; }
    /* Network failure must never block offline use; the update check
     * reports it only when the user asks for an update. */
    update_net_init();
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
    while(running) {
        NL_TRACE("before-events");
        Uint32 start=SDL_GetTicks(); SDL_Event event;
        sysUtilCheckCallback();
        while(SDL_PollEvent(&event)) {
            if(event.type==SDL_QUIT) running=0;
        }
        NL_TRACE("events-complete");
        if(!running || runtime_exit_requested()) break;
        if(runtime_draw_suspended()) {
            previous=0;
#ifdef NL_NATIVE_DISPLAY
            if(display_idle()) running=0;
            NL_FRAME_COMPLETE();
#endif
            SDL_Delay(16); continue;
        }
        unsigned held=pad_buttons(),pressed=held&~previous; previous=held;
        NL_TRACE("input-complete");
        if(held && SDL_TICKS_PASSED(start,repeat_at)) { pressed|=held&(B_UP|B_DOWN|B_L2|B_R2); repeat_at=start+180; }
        if(!held) repeat_at=start+350;
        if(pressed&B_L2) change_volume(-5);
        if(pressed&B_R2) change_volume(5);
        if(pressed&B_START) toggle_pause();
        if(pressed&B_L1) { if(playing>0) play_track(playing-1); }
        if(pressed&B_R1) { if(playing>=0 && playing+1<library.count) play_track(playing+1); }
        if(focus && (section==1 || (section==2 && art_view==1))) {
            if(pressed&B_LEFT) gallery_step(&gallery,-1,start);
            if(pressed&B_RIGHT) gallery_step(&gallery,1,start);
            pressed&=~(B_LEFT|B_RIGHT);
        }
        if(focus && section==2 && art_view!=1) pressed&=~(B_LEFT|B_RIGHT);
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
            } else if(focus && section==0 && view==2) {view=0;selection=0;}
            else focus=0;
        }
        if(pressed&B_LEFT) {
            if(section==3 && focus && settings_selection==0) change_volume(-5);
            else if(section==3 && focus) {}
            else if(section==0 && focus && view==0 && selection!=0 && selection!=4) selection--;
            else focus=0;
        }
        if(pressed&B_RIGHT) {
            if(section==3 && focus && settings_selection==0) change_volume(5);
            else if(section==3 && focus) {}
            else if(section==0 && focus && view==0 && selection!=3 && selection!=5) selection++;
            else focus=1;
        }
        if(!focus) {
            if(pressed&B_UP) section=(section+4)%5;
            if(pressed&B_DOWN) section=(section+1)%5;
            if(pressed&B_X) { if(section==4) running=0; else { focus=1; if(section==2) art_view=0; } }
        } else if(section==0) {
            int rows=view==0?6:(view==1?entry_count+1:library.count);
            if(rows>0) {
                if(view==0) {
                    if(pressed&B_UP) { if(selection>=4) selection=selection==4?0:2; else focus=0; }
                    if(pressed&B_DOWN) selection=selection<2?4:5;
                } else {
                    if(pressed&B_UP) selection=(selection+rows-1)%rows;
                    if(pressed&B_DOWN) selection=(selection+1)%rows;
                }
            }
            if(pressed&B_X) {
                if(view==0) {
                    if(selection<4) { source=selection; snprintf(folder,sizeof(folder),"%s",sources[source]); refresh_folder(folder,&entry_count,&selection); view=1; }
                    else if(selection==4) {view=2;selection=0;}
                    else {section=1;}
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
                } else if(library.count) play_track(selection);
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
            if(pressed&B_UP) settings_selection=0;
            if(pressed&B_DOWN) settings_selection=1;
            if(pressed&B_X && settings_selection==1 && !update_pending) { update_pending=1; snprintf(message,sizeof(message),"Checking for updates..."); }
        } else if(section==1 && (pressed&B_X)) toggle_pause();
        NL_INPUT(pressed,section,focus);
        if(audio && SDL_AtomicGet(&finished)) {
            int next=playing+1;
            if(next<library.count) play_track(next); else {stop_music();snprintf(message,sizeof(message),"Playlist finished");}
        }
        float snapshot[SV_FFT]={0}; Spectrum spectrum;
        if(audio && !paused) {
            SDL_LockAudioDevice(audio);
            for(unsigned i=0;i<SV_FFT;i++) snapshot[i]=pcm[(pcm_position+i)%SV_FFT];
            SDL_UnlockAudioDevice(audio);
        }
        spectrum_analyze(snapshot,rate,&spectrum);
        for(int i=0;i<SV_BANDS;i++) smooth[i]=smooth[i]*0.70f+spectrum.bands[i]*0.30f;
        ui_rect(screen,0,0,1280,720,0x08111f);
        if((section==1 && focus) || (section==2 && focus && art_view==1)) {
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
            if(section==1) visualizer_draw(screen,smooth,(int64_t)time(NULL),start);
            ui_rect(screen,24,18,650,42,0x0b1524);
            ui_text(screen,38,24,26,0xf2f7ff,gallery_title(&gallery));
        } else {
            ui_rect(screen,0,0,230,635,0x101d30);
            ui_text(screen,26,30,26,0x32d5ed,"NEON LOUNGE");
            for(int i=0;i<5;i++) {
                if(section==i) { ui_rect(screen,14,114+i*72,202,54,focus?0x263c50:0x156e83); ui_rect(screen,14,114+i*72,4,54,0x35dfeb); }
                ui_text(screen,36,124+i*72,26,0xf2f7ff,nav[i]);
            }
            ui_text(screen,270,30,42,0xf2f7ff,nav[section]);
            if(section==0) {
                ui_text(screen,274,88,18,0x80b4c8,view==1?folder:"Your music | Internal HDD and USB");
                const char *home[]={"Music folder","Internal HDD","USB 0","USB 1","Current playlist","Start visualizer"};
                if(view==0 && background) {
                    ui_rect(screen,270,190,950,280,0x101a29);
                    draw_art(screen,background,270,190,950,280);
                    ui_text(screen,294,232,42,0xffffff,"Your music.");
                    ui_text(screen,294,284,42,0x35dfeb,"Your lounge.");
                    ui_text(screen,294,358,18,0x91b4c9,"MP3 playback and audio-reactive spectrum");
                }
                int count=view==0?6:(view==1?entry_count+1:library.count);
                int first=selection>7?selection-7:0;
                for(int row=first;row<count && row<first+9;row++) {
                    int y=138+(row-first)*44,x=266,w=960,pixels=26; char label[300];
                    if(view==0) {
                        x=row<4?266+row*240:266+(row-4)*480;
                        y=row<4?130:496; w=row<4?230:470; pixels=row<4?18:26;
                    }
                    if(view==0) snprintf(label,sizeof(label),"%s",home[row]);
                    else if(view==1) { if(!row) snprintf(label,sizeof(label),"[ Play this folder and subfolders ]"); else snprintf(label,sizeof(label),"%s%s",entries[row-1].directory?"[Folder] ":"",entries[row-1].name); }
                    else snprintf(label,sizeof(label),"%s",library.tracks[row].name);
                    ui_rect(screen,x,y,w,40,focus && selection==row?0x195366:0x15283c);
                    ui_text(screen,x+14,y+4,pixels,0xf4f6ff,label);
                }
                if(!count) ui_text(screen,280,170,26,0xc2ccd9,"No MP3 tracks. Browse a source folder first.");
            } else if(section==1 || section==2) {
                if(section==2 && focus && art_view==0) {
                    const char *opts[]={"View gallery","Add artwork","Remove artwork","Get pictures"};
                    char gallery_label[120];
                    for(int i=0;i<4;i++) {
                        ui_rect(screen,266,138+i*44,960,40,art_selection==i?0x195366:0x15283c);
                        ui_text(screen,280,142+i*44,26,0xf4f6ff,opts[i]);
                    }
                    ui_text(screen,280,340,18,0x91b4c9,"Add: BMP pictures, 1280x720 or smaller, from HDD or USB");
                    ui_text(screen,280,366,18,0x91b4c9,"Remove: current artwork is moved to a backup folder");
                    snprintf(gallery_label,sizeof(gallery_label),"%d / %d slides loaded | %s",gallery.loaded,NL_GALLERY_SLIDES,gallery_title(&gallery));
                    ui_text(screen,280,400,18,0x91b4c9,gallery_label);
                } else if(section==2 && focus && art_view==2) {
                    int count=art_browse_view==0?4:art_count;
                    int first=art_sel>7?art_sel-7:0;
                    ui_text(screen,274,88,18,0x80b4c8,art_browse_view==1?art_folder:"Choose a source to browse for BMP pictures");
                    for(int row=first;row<count && row<first+9;row++) {
                        int y=138+(row-first)*44; char label[300];
                        if(art_browse_view==0) snprintf(label,sizeof(label),"%s",sources[row]);
                        else snprintf(label,sizeof(label),"%s%s",art_entries[row].directory?"[Folder] ":"",art_entries[row].name);
                        ui_rect(screen,266,y,960,40,art_sel==row?0x195366:0x15283c);
                        ui_text(screen,280,y+4,26,0xf4f6ff,label);
                    }
                    if(!count) ui_text(screen,280,170,26,0xc2ccd9,"No BMP pictures here. Browse another folder.");
                } else if(section==2 && focus && art_view==3) {
                    const char *sites[]={"X - x.com","DeviantArt - deviantart.com","Pixiv - pixiv.net","ArtStation - artstation.com","Zerochan - zerochan.net","Pinterest - pinterest.com"};
                    ui_text(screen,280,140,26,0xffffff,"Get pictures from these sites:");
                    for(int i=0;i<6;i++) ui_text(screen,280,180+i*36,26,0x32d5ed,sites[i]);
                    ui_text(screen,280,420,18,0x91b4c9,"Save a picture on your PC or phone, convert it to BMP");
                    ui_text(screen,280,446,18,0x91b4c9,"(1280x720 or smaller), copy it to USB, then Add artwork.");
                    ui_text(screen,280,472,18,0x91b4c9,"Use artwork you have permission to use.");
                } else {
                    if(gallery.loaded) gallery_draw(&gallery,screen,270,135,950,400,start);
                    else if(background) {
                        draw_art(screen,background,270,135,950,400);
                    }
                    if(section==2) ui_text(screen,280,540,18,0x32d5ed,"X Artwork options | Add / Remove / Get pictures");
                    else ui_text(screen,280,540,18,0x32d5ed,"X Fullscreen | O Back | Left/Right Artwork | Auto: 12 seconds");
                    char gallery_label[120];
                    snprintf(gallery_label,sizeof(gallery_label),"%d / %d slides loaded | %s",gallery.loaded,NL_GALLERY_SLIDES,gallery_title(&gallery));
                    ui_text(screen,280,574,18,0x91b4c9,gallery_label);
                }
            } else if(section==3) {
                ui_rect(screen,266,138,960,40,focus && settings_selection==0?0x195366:0x101a29);
                ui_text(screen,280,142,26,0xffffff,"Playback volume");
                ui_rect(screen,280,200,660,12,0x294054); ui_rect(screen,280,200,SDL_AtomicGet(&volume)*660/100,12,0x32d5ed);
                ui_text(screen,280,250,18,0xc2ccd9,"Left / Right or L2 / R2 to adjust");
                ui_rect(screen,266,300,960,40,focus && settings_selection==1?0x195366:0x101a29);
                ui_text(screen,280,304,26,0xffffff,"Check for updates");
                ui_text(screen,280,360,18,0x91b4c9,"Neon Lounge " NL_VERSION " | Updates from GitHub");
                ui_text(screen,280,386,18,0xc2ccd9,"MP3 playback | Read-only HDD and USB browsing");
            } else if(section==4) ui_text(screen,280,150,26,0xffffff,"Press X to exit Neon Lounge");
        }
        ui_rect(screen,0,608,1280,112,0x101a29);
        ui_text(screen,28,620,26,0xffffff,playing>=0?library.tracks[playing].name:"No track selected");
        ui_text(screen,28,656,18,0x91b4c9,message);
        char volume_label[30]; snprintf(volume_label,sizeof(volume_label),"Volume %d%%",SDL_AtomicGet(&volume));
        ui_text(screen,1000,622,26,0x3de0ed,volume_label);
        ui_text(screen,28,689,18,0x91b4c9,"X Select   O Back   START Pause   L1/R1 Track   L2/R2 Volume");
        NL_TRACE("before-present");
        if(display_present()) running=0;
        NL_TRACE("present-complete");
        /* Run the update check only after a frame showing "Checking for
         * updates..." has been presented, so the check never looks like
         * a silent freeze. The check itself still blocks this thread. */
        if(running) {
            if(update_pending==2) {
                update_pending=0;
                update_check_and_install(ASSETS,upd_msg,sizeof(upd_msg));
                snprintf(message,sizeof(message),"%s",upd_msg);
            } else if(update_pending==1) update_pending=2;
        }
        Uint32 elapsed=SDL_GetTicks()-start;
        if(elapsed<16) SDL_Delay(16-elapsed);
        NL_TRACE("delay-complete");
        NL_FRAME_COMPLETE();
    }
    NL_TRACE("before-cleanup");
    stop_music();
    if(pad_init==PAD_OK) ioPadEnd();
    gallery_close(&gallery);
    update_net_shutdown();
    SDL_FreeSurface(background); ui_font_close(); display_close(); runtime_shutdown();
    SDL_Quit();
    NL_TRACE("cleanup-complete");
    NL_DIAGNOSTIC_FINISH();
    return 0;
}
