#include "ftp_server.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef NEON_PS3
#include <SDL.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/select.h>
#include <net/netctl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "gallery.h"
#include "update.h"
#endif

/* ------------------------------------------------------------------ */
/* Pure helpers (host-testable).                                       */
/* ------------------------------------------------------------------ */

int ftp_parse_command(char *line,char *verb,size_t verb_size,const char **arg) {
    char *p,*vstart;
    size_t vlen=0;
    if(!line || !verb || !verb_size || !arg) return -1;
    /* Trim trailing whitespace/CR/LF in place. */
    {
        size_t n=strlen(line);
        while(n && (line[n-1]==' '||line[n-1]=='\t'||line[n-1]=='\r'||line[n-1]=='\n')) line[--n]=0;
    }
    p=line;
    while(*p==' '||*p=='\t') p++;
    vstart=p;
    while(*p && *p!=' ' && *p!='\t') { p++; vlen++; }
    if(!vlen || vlen>=verb_size) return -1;
    for(size_t i=0;i<vlen;i++) {
        char c=vstart[i];
        verb[i]=(c>='a' && c<='z')?(char)(c-'a'+'A'):c;
    }
    verb[vlen]=0;
    while(*p==' '||*p=='\t') p++;
    *arg=p;
    return 0;
}

int ftp_safe_name(const char *arg,char *name,size_t name_size) {
    size_t len;
    if(!arg || !name || !name_size) return -1;
    len=strlen(arg);
    if(!len || len>NL_FTP_MAX_NAME || len>=name_size) return -1;
    if(arg[0]=='.') return -1;
    if(!strcmp(arg,".") || !strcmp(arg,"..")) return -1;
    for(size_t i=0;i<len;i++) {
        unsigned char c=(unsigned char)arg[i];
        if(c=='/' || c=='\\' || c<0x20 || c==0x7f) return -1;
    }
    memcpy(name,arg,len+1);
    return 0;
}

int ftp_is_bmp_name(const char *name) {
    const char *ext;
    if(!name) return 0;
    ext=strrchr(name,'.');
    if(!ext || !ext[1]) return 0;
    if(ext[1]!='b' && ext[1]!='B') return 0;
    if(ext[2]!='m' && ext[2]!='M') return 0;
    if(ext[3]!='p' && ext[3]!='P') return 0;
    return ext[4]==0;
}

int ftp_format_pasv(const char *ip,unsigned port,char *out,size_t size) {
    unsigned oct[4];
    const char *p;
    if(!ip || !out || !size || !port || port>65535) return -1;
    p=ip;
    for(int i=0;i<4;i++) {
        unsigned v=0;
        int digits=0;
        while(*p>='0' && *p<='9') { v=v*10+(unsigned)(*p-'0'); if(v>255) return -1; p++; digits++; }
        if(!digits) return -1;
        oct[i]=v;
        if(i<3) { if(*p!='.') return -1; p++; }
    }
    if(*p) return -1;
    if(snprintf(out,size,"227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\r\n",
                oct[0],oct[1],oct[2],oct[3],port/256,port%256)>=(int)size) return -1;
    return 0;
}

int ftp_format_epsv(unsigned port,char *out,size_t size) {
    if(!out || !size || !port || port>65535) return -1;
    if(snprintf(out,size,"229 Entering Extended Passive Mode (|||%u|).\r\n",port)>=(int)size) return -1;
    return 0;
}

int ftp_format_list_line(const char *name,long file_size,long mtime,char *out,size_t size) {
    static const char *months[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    time_t t;
    struct tm *tmv;
    if(!name || !*name || !out || !size || file_size<0 || mtime<0) return -1;
    if(strchr(name,'\r') || strchr(name,'\n')) return -1;
    t=(time_t)mtime;
    tmv=gmtime(&t);
    if(!tmv || tmv->tm_mon<0 || tmv->tm_mon>11) return -1;
    if(snprintf(out,size,"-rw-r--r-- 1 0 0 %10ld %s %2d %02d:%02d %s\r\n",
                file_size,months[tmv->tm_mon],tmv->tm_mday,tmv->tm_hour,tmv->tm_min,name)>=(int)size) return -1;
    return 0;
}

#ifdef NEON_PS3
/* ------------------------------------------------------------------ */
/* Server (PS3 only). One client at a time, on its own SDL thread.     */
/* The thread only writes files inside the gallery folder and sets the */
/* dirty flag; the main thread reloads the gallery surfaces itself,    */
/* so playback and the visualizer are never blocked or raced.          */
/* ------------------------------------------------------------------ */

#define FTP_LINE_CAP 1024
#define FTP_TEMP_NAME ".nl-ftp-upload.tmp"

static SDL_Thread *worker;
static SDL_atomic_t cancel_flag,state_flag,dirty_flag;
static char serve_folder[512];
static char serve_ip[16];
static char serve_addr[48];

static int send_all(int fd,const void *buf,size_t len) {
    const char *p=(const char*)buf;
    while(len) {
        int n=send(fd,p,(int)len,0);
        if(n<=0) return -1;
        p+=n; len-=(size_t)n;
    }
    return 0;
}
static int send_reply(int fd,const char *text) { return send_all(fd,text,strlen(text)); }

/* 1 readable, 0 timeout, -1 cancelled. Slices keep stop() responsive. */
static int wait_readable(int fd,int timeout_ms) {
    int waited=0;
    while(waited<timeout_ms) {
        fd_set rf;
        struct timeval tv;
        int r;
        if(SDL_AtomicGet(&cancel_flag)) return -1;
        FD_ZERO(&rf); FD_SET(fd,&rf);
        tv.tv_sec=0; tv.tv_usec=100000;
        r=select(fd+1,&rf,NULL,NULL,&tv);
        if(r>0) return 1;
        if(r<0) return 0;
        waited+=100;
    }
    return 0;
}

typedef struct { int fd; char buf[1024]; size_t pos,len; } LineBuf;
/* 1 line read, 0 EOF/cancel, -1 line too long (rest consumed). */
static int line_read(LineBuf *lb,char *out,size_t cap) {
    size_t n=0;
    int overflow=0;
    for(;;) {
        while(lb->pos<lb->len) {
            char c=lb->buf[lb->pos++];
            if(c=='\n') { out[n]=0; return overflow?-1:1; }
            if(c=='\r') continue;
            if(n+1<cap) out[n++]=c; else overflow=1;
        }
        if(SDL_AtomicGet(&cancel_flag)) return 0;
        {
            fd_set rf;
            struct timeval tv;
            int r,got;
            FD_ZERO(&rf); FD_SET(lb->fd,&rf);
            tv.tv_sec=0; tv.tv_usec=200000;
            r=select(lb->fd+1,&rf,NULL,NULL,&tv);
            if(r<=0) continue;
            got=recv(lb->fd,lb->buf,sizeof(lb->buf),0);
            if(got<=0) return 0;
            lb->pos=0; lb->len=(size_t)got;
        }
    }
}

static int open_pasv(unsigned *port_out) {
    int fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr;
    socklen_t len;
    int yes=1;
    if(fd<0) return -1;
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=0;
    if(bind(fd,(struct sockaddr*)&addr,sizeof(addr)) || listen(fd,1)) { close(fd); return -1; }
    len=sizeof(addr);
    if(getsockname(fd,(struct sockaddr*)&addr,&len)) { close(fd); return -1; }
    *port_out=ntohs(addr.sin_port);
    return fd;
}

static int accept_data(int pasv_fd) {
    int r=wait_readable(pasv_fd,10000);
    struct sockaddr_in from;
    socklen_t len;
    if(r!=1) return -1;
    len=sizeof(from);
    return accept(pasv_fd,(struct sockaddr*)&from,&len);
}

/* Dotted-quad string for the PASV reply: the local address of this
 * control connection if known, else the netctl address from startup. */
static int session_ip(int ctrl,char *out,size_t size) {
    struct sockaddr_in addr;
    socklen_t len=sizeof(addr);
    if(getsockname(ctrl,(struct sockaddr*)&addr,&len)==0 && addr.sin_addr.s_addr!=0) {
        unsigned ip=ntohl(addr.sin_addr.s_addr);
        if(snprintf(out,size,"%u.%u.%u.%u",(ip>>24)&255,(ip>>16)&255,(ip>>8)&255,ip&255)<(int)size) return 0;
    }
    if(serve_ip[0]) { snprintf(out,size,"%s",serve_ip); return 0; }
    return -1;
}

static int count_bmp_files(const char *folder) {
    DIR *dir=opendir(folder);
    struct dirent *entry;
    int count=0;
    if(!dir) return -1;
    while((entry=readdir(dir))) {
        if(entry->d_name[0]=='.') continue;
        if(ftp_is_bmp_name(entry->d_name)) count++;
    }
    closedir(dir);
    return count;
}

static int join_path(char *out,size_t size,const char *folder,const char *name) {
    return snprintf(out,size,"%s/%s",folder,name)>=(int)size?-1:0;
}

static int do_list(int ctrl,int data_fd,int names_only) {
    DIR *dir=opendir(serve_folder);
    struct dirent *entry;
    int ok=1;
    if(!dir) return -1;
    if(send_reply(ctrl,"150 Opening data connection.\r\n")) { closedir(dir); return -1; }
    while((entry=readdir(dir))) {
        char line[512],path[768];
        struct stat st;
        if(entry->d_name[0]=='.') continue;
        if(!ftp_is_bmp_name(entry->d_name)) continue;
        if(names_only) {
            if(snprintf(line,sizeof(line),"%s\r\n",entry->d_name)>=(int)sizeof(line)) continue;
        } else {
            if(join_path(path,sizeof(path),serve_folder,entry->d_name)) continue;
            if(stat(path,&st)) continue;
            if(ftp_format_list_line(entry->d_name,(long)st.st_size,(long)st.st_mtime,line,sizeof(line))) continue;
        }
        if(send_all(data_fd,line,strlen(line))) { ok=0; break; }
    }
    closedir(dir);
    close(data_fd);
    return send_reply(ctrl,ok?"226 Transfer complete.\r\n":"426 Transfer aborted.\r\n");
}

static int do_retr(int ctrl,int data_fd,const char *name) {
    char path[768];
    FILE *f;
    unsigned char buf[8192];
    size_t got;
    int ok=1;
    if(join_path(path,sizeof(path),serve_folder,name)) return send_reply(ctrl,"550 Invalid file name.\r\n");
    f=fopen(path,"rb");
    if(!f) return send_reply(ctrl,"550 File not found.\r\n");
    if(send_reply(ctrl,"150 Opening data connection.\r\n")) { fclose(f); return -1; }
    while((got=fread(buf,1,sizeof(buf),f))>0) {
        if(send_all(data_fd,buf,got)) { ok=0; break; }
    }
    if(ferror(f)) ok=0;
    fclose(f);
    close(data_fd);
    return send_reply(ctrl,ok?"226 Transfer complete.\r\n":"426 Transfer aborted.\r\n");
}

static int do_stor(int ctrl,int data_fd,const char *name) {
    char dest[768],temp[768],err[120];
    FILE *out;
    unsigned char buf[8192];
    long total=0;
    int idle_timeouts=0,ok=1,too_big=0;
    if(join_path(dest,sizeof(dest),serve_folder,name) ||
       join_path(temp,sizeof(temp),serve_folder,FTP_TEMP_NAME))
        return send_reply(ctrl,"550 Invalid file name.\r\n");
    {
        FILE *probe=fopen(dest,"rb");
        if(probe) { fclose(probe); return send_reply(ctrl,"550 That artwork is already in the gallery.\r\n"); }
    }
    {
        int count=count_bmp_files(serve_folder);
        if(count<0) return send_reply(ctrl,"550 Gallery folder unavailable.\r\n");
        if(count>=NL_GALLERY_SLIDES) return send_reply(ctrl,"550 Gallery is full (7). Remove one first.\r\n");
    }
    out=fopen(temp,"wb");
    if(!out) return send_reply(ctrl,"550 Could not save to the gallery folder.\r\n");
    if(send_reply(ctrl,"150 Opening data connection.\r\n")) { fclose(out); remove(temp); return -1; }
    for(;;) {
        int r=wait_readable(data_fd,2000);
        int got;
        if(r<0) { ok=0; break; }
        if(r==0) { if(++idle_timeouts>=5) { ok=0; break; } continue; }
        idle_timeouts=0;
        got=recv(data_fd,buf,sizeof(buf),0);
        if(got<0) { ok=0; break; }
        if(got==0) break;
        total+=(long)got;
        if(total>NL_GALLERY_MAX_BYTES || fwrite(buf,1,(size_t)got,out)!=(size_t)got) { too_big=1; ok=0; break; }
    }
    fclose(out);
    close(data_fd);
    if(!ok) {
        remove(temp);
        return send_reply(ctrl,too_big?"552 Picture is too large (8 MB max).\r\n":"426 Transfer aborted.\r\n");
    }
    /* Same validation as Add artwork: one shared helper, one rule set. */
    if(gallery_validate_bmp(temp,err,sizeof(err))) {
        char reply[200];
        remove(temp);
        snprintf(reply,sizeof(reply),"550 %s\r\n",err);
        return send_reply(ctrl,reply);
    }
    {
        FILE *probe=fopen(dest,"rb");
        if(probe) { fclose(probe); remove(temp); return send_reply(ctrl,"550 That artwork is already in the gallery.\r\n"); }
        if(count_bmp_files(serve_folder)>=NL_GALLERY_SLIDES) { remove(temp); return send_reply(ctrl,"550 Gallery is full (7). Remove one first.\r\n"); }
    }
    if(rename(temp,dest)) {
        remove(temp);
        return send_reply(ctrl,"550 Could not save to the gallery folder.\r\n");
    }
    SDL_AtomicSet(&dirty_flag,1);
    return send_reply(ctrl,"226 Transfer complete.\r\n");
}

static int do_dele(int ctrl,const char *name) {
    char path[768];
    if(join_path(path,sizeof(path),serve_folder,name)) return send_reply(ctrl,"550 Invalid file name.\r\n");
    if(remove(path)) return send_reply(ctrl,"550 Could not delete that file.\r\n");
    SDL_AtomicSet(&dirty_flag,1);
    return send_reply(ctrl,"250 File deleted.\r\n");
}

static void serve_client(int ctrl) {
    LineBuf lb;
    int pasv_fd=-1,logged_in=0,user_seen=0;
    char line[FTP_LINE_CAP];
    lb.fd=ctrl; lb.pos=0; lb.len=0;
    if(send_reply(ctrl,"220 Neon Lounge FTP ready.\r\n")) return;
    for(;;) {
        char verb[16],name[NL_FTP_MAX_NAME+1];
        const char *arg=NULL;
        int rc=line_read(&lb,line,sizeof(line));
        if(rc==0) break;
        if(rc<0) { if(send_reply(ctrl,"500 Line too long.\r\n")) break; continue; }
        if(ftp_parse_command(line,verb,sizeof(verb),&arg)) continue;
        if(!strcmp(verb,"QUIT")) { send_reply(ctrl,"221 Goodbye.\r\n"); break; }
        if(!strcmp(verb,"USER")) { user_seen=1; logged_in=0; if(send_reply(ctrl,"331 Anonymous access granted.\r\n")) break; continue; }
        if(!strcmp(verb,"PASS")) {
            if(!user_seen) { if(send_reply(ctrl,"503 Login with USER first.\r\n")) break; }
            else { logged_in=1; if(send_reply(ctrl,"230 Login successful.\r\n")) break; }
            continue;
        }
        if(!strcmp(verb,"NOOP")) { if(send_reply(ctrl,"200 OK.\r\n")) break; continue; }
        if(!logged_in) { if(send_reply(ctrl,"530 Please log in first.\r\n")) break; continue; }
        if(!strcmp(verb,"SYST")) { if(send_reply(ctrl,"215 UNIX Type: L8\r\n")) break; }
        else if(!strcmp(verb,"FEAT")) { if(send_reply(ctrl,"211 No features.\r\n")) break; }
        else if(!strcmp(verb,"OPTS")) { if(send_reply(ctrl,"200 OK.\r\n")) break; }
        else if(!strcmp(verb,"TYPE")) {
            if(!strcmp(arg,"I") || !strcmp(arg,"A")) { if(send_reply(ctrl,"200 Type set.\r\n")) break; }
            else if(send_reply(ctrl,"504 Unknown transfer type.\r\n")) break;
        }
        else if(!strcmp(verb,"PWD")) { if(send_reply(ctrl,"257 \"/\" is the current directory.\r\n")) break; }
        else if(!strcmp(verb,"CWD")) {
            if(!*arg || !strcmp(arg,"/") || !strcmp(arg,".")) { if(send_reply(ctrl,"250 Directory changed.\r\n")) break; }
            else if(send_reply(ctrl,"550 Only the gallery folder is available.\r\n")) break;
        }
        else if(!strcmp(verb,"CDUP")) { if(send_reply(ctrl,"250 Directory changed.\r\n")) break; }
        else if(!strcmp(verb,"PASV") || !strcmp(verb,"EPSV")) {
            unsigned port=0;
            char ip[32],reply[96];
            int extended=!strcmp(verb,"EPSV");
            if(pasv_fd>=0) { close(pasv_fd); pasv_fd=-1; }
            pasv_fd=open_pasv(&port);
            if(pasv_fd<0 || session_ip(ctrl,ip,sizeof(ip))) {
                if(pasv_fd>=0) { close(pasv_fd); pasv_fd=-1; }
                if(send_reply(ctrl,"425 Cannot open passive connection.\r\n")) break;
            } else if(extended?ftp_format_epsv(port,reply,sizeof(reply)):ftp_format_pasv(ip,port,reply,sizeof(reply))) {
                close(pasv_fd); pasv_fd=-1;
                if(send_reply(ctrl,"425 Cannot open passive connection.\r\n")) break;
            } else if(send_reply(ctrl,reply)) break;
        }
        else if(!strcmp(verb,"PORT")) { if(send_reply(ctrl,"502 Active mode is not supported. Use passive mode.\r\n")) break; }
        else if(!strcmp(verb,"LIST") || !strcmp(verb,"NLST")) {
            int data_fd;
            if(pasv_fd<0) { if(send_reply(ctrl,"425 Use PASV first.\r\n")) break; continue; }
            data_fd=accept_data(pasv_fd);
            close(pasv_fd); pasv_fd=-1;
            if(data_fd<0) { if(send_reply(ctrl,"425 Cannot open data connection.\r\n")) break; continue; }
            if(do_list(ctrl,data_fd,!strcmp(verb,"NLST"))) break;
        }
        else if(!strcmp(verb,"RETR")) {
            int data_fd;
            if(ftp_safe_name(arg,name,sizeof(name))) { if(send_reply(ctrl,"550 Invalid file name.\r\n")) break; continue; }
            if(pasv_fd<0) { if(send_reply(ctrl,"425 Use PASV first.\r\n")) break; continue; }
            data_fd=accept_data(pasv_fd);
            close(pasv_fd); pasv_fd=-1;
            if(data_fd<0) { if(send_reply(ctrl,"425 Cannot open data connection.\r\n")) break; continue; }
            if(do_retr(ctrl,data_fd,name)) break;
        }
        else if(!strcmp(verb,"STOR")) {
            int data_fd;
            if(ftp_safe_name(arg,name,sizeof(name))) { if(send_reply(ctrl,"550 Invalid file name.\r\n")) break; continue; }
            if(!ftp_is_bmp_name(name)) { if(send_reply(ctrl,"550 Only BMP pictures can be added.\r\n")) break; continue; }
            if(pasv_fd<0) { if(send_reply(ctrl,"425 Use PASV first.\r\n")) break; continue; }
            data_fd=accept_data(pasv_fd);
            close(pasv_fd); pasv_fd=-1;
            if(data_fd<0) { if(send_reply(ctrl,"425 Cannot open data connection.\r\n")) break; continue; }
            if(do_stor(ctrl,data_fd,name)) break;
        }
        else if(!strcmp(verb,"DELE")) {
            if(ftp_safe_name(arg,name,sizeof(name))) { if(send_reply(ctrl,"550 Invalid file name.\r\n")) break; continue; }
            if(do_dele(ctrl,name)) break;
        }
        else if(!strcmp(verb,"SIZE")) {
            char path[768];
            struct stat st;
            char reply[64];
            if(ftp_safe_name(arg,name,sizeof(name)) || join_path(path,sizeof(path),serve_folder,name) || stat(path,&st)) {
                if(send_reply(ctrl,"550 File not found.\r\n")) break;
            } else {
                snprintf(reply,sizeof(reply),"213 %ld\r\n",(long)st.st_size);
                if(send_reply(ctrl,reply)) break;
            }
        }
        else if(!strcmp(verb,"ABOR")) { if(send_reply(ctrl,"226 Aborted.\r\n")) break; }
        else if(send_reply(ctrl,"502 Command not implemented.\r\n")) break;
        if(SDL_AtomicGet(&cancel_flag)) break;
    }
    if(pasv_fd>=0) close(pasv_fd);
}

static int run(void *arg) {
    int listen_fd=-1,yes=1;
    struct sockaddr_in addr;
    (void)arg;
    SDL_AtomicSet(&state_flag,1);
    /* Retry network setup here: a failure at startup must not disable
     * the FTP server for the whole session. Runs on this thread, so
     * the UI never blocks on it. */
    if(update_net_init()) { SDL_AtomicSet(&state_flag,-1); return 0; }
    {
        union net_ctl_info info;
        memset(&info,0,sizeof(info));
        if(netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS,&info)==0 && info.ip_address[0]) {
            snprintf(serve_ip,sizeof(serve_ip),"%s",info.ip_address);
            snprintf(serve_addr,sizeof(serve_addr),"ftp://%s:%d",serve_ip,NL_FTP_PORT);
        }
    }
    listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if(listen_fd<0) { SDL_AtomicSet(&state_flag,-1); return 0; }
    setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons(NL_FTP_PORT);
    if(bind(listen_fd,(struct sockaddr*)&addr,sizeof(addr)) || listen(listen_fd,1)) {
        close(listen_fd);
        SDL_AtomicSet(&state_flag,-1);
        return 0;
    }
    SDL_AtomicSet(&state_flag,2);
    while(!SDL_AtomicGet(&cancel_flag)) {
        fd_set rf;
        struct timeval tv;
        int r;
        FD_ZERO(&rf); FD_SET(listen_fd,&rf);
        tv.tv_sec=0; tv.tv_usec=200000;
        r=select(listen_fd+1,&rf,NULL,NULL,&tv);
        if(r<=0) continue;
        {
            struct sockaddr_in from;
            socklen_t len=sizeof(from);
            int ctrl=accept(listen_fd,(struct sockaddr*)&from,&len);
            if(ctrl<0) continue;
            serve_client(ctrl);
            close(ctrl);
        }
    }
    close(listen_fd);
    SDL_AtomicSet(&state_flag,0);
    return 0;
}

int ftp_server_start(const char *folder) {
    ftp_server_stop();
    if(!folder || !*folder) return -1;
    if(snprintf(serve_folder,sizeof(serve_folder),"%s",folder)>=(int)sizeof(serve_folder)) return -1;
    serve_ip[0]=0; serve_addr[0]=0;
    SDL_AtomicSet(&cancel_flag,0);
    SDL_AtomicSet(&state_flag,1);
    worker=SDL_CreateThread(run,"FTP server",NULL);
    if(!worker) { SDL_AtomicSet(&state_flag,0); return -1; }
    return 0;
}
void ftp_server_stop(void) {
    SDL_AtomicSet(&cancel_flag,1);
    if(worker) SDL_WaitThread(worker,NULL);
    worker=NULL;
    SDL_AtomicSet(&state_flag,0);
}
int ftp_server_status(void) { return SDL_AtomicGet(&state_flag); }
int ftp_server_address(char *out,size_t size) {
    if(!out || !size) return -1;
    if(SDL_AtomicGet(&state_flag)!=2 || !serve_addr[0]) return -1;
    if(snprintf(out,size,"%s",serve_addr)>=(int)size) return -1;
    return 0;
}
int ftp_take_gallery_dirty(void) {
    return SDL_AtomicCAS(&dirty_flag,1,0)?1:0;
}

#else /* !NEON_PS3: host stubs */

int ftp_server_start(const char *folder) { (void)folder; return -1; }
void ftp_server_stop(void) {}
int ftp_server_status(void) { return 0; }
int ftp_server_address(char *out,size_t size) { (void)out; (void)size; return -1; }
int ftp_take_gallery_dirty(void) { return 0; }

#endif
