#include "ftp_server.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(cond) do { if(!(cond)) { printf("FAIL line %d: %s\n",__LINE__,#cond); fails++; } } while(0)
int main(void) {
    char verb[16],name[300],out[256];
    const char *arg;
    char line[256];

    /* Command parsing. */
    strcpy(line,"USER anonymous\r\n");
    CHECK(ftp_parse_command(line,verb,sizeof(verb),&arg)==0 && !strcmp(verb,"USER") && !strcmp(arg,"anonymous"));
    strcpy(line,"stor My Pic.BMP\r\n");
    CHECK(ftp_parse_command(line,verb,sizeof(verb),&arg)==0 && !strcmp(verb,"STOR") && !strcmp(arg,"My Pic.BMP"));
    strcpy(line,"quit");
    CHECK(ftp_parse_command(line,verb,sizeof(verb),&arg)==0 && !strcmp(verb,"QUIT") && !strcmp(arg,""));
    strcpy(line,"  PASV  \r\n");
    CHECK(ftp_parse_command(line,verb,sizeof(verb),&arg)==0 && !strcmp(verb,"PASV") && !strcmp(arg,""));
    strcpy(line,"TYPE I");
    CHECK(ftp_parse_command(line,verb,sizeof(verb),&arg)==0 && !strcmp(verb,"TYPE") && !strcmp(arg,"I"));
    strcpy(line,"");
    CHECK(ftp_parse_command(line,verb,sizeof(verb),&arg)!=0);
    strcpy(line,"   \r\n");
    CHECK(ftp_parse_command(line,verb,sizeof(verb),&arg)!=0);
    strcpy(line,"AVERYVERYLONGCOMMANDNAME arg");
    CHECK(ftp_parse_command(line,verb,sizeof(verb),&arg)!=0);
    CHECK(ftp_parse_command(NULL,verb,sizeof(verb),&arg)!=0);
    CHECK(ftp_parse_command(line,verb,0,&arg)!=0);

    /* Path-traversal rejection: only bare file names survive. */
    CHECK(ftp_safe_name("miku.bmp",name,sizeof(name))==0 && !strcmp(name,"miku.bmp"));
    CHECK(ftp_safe_name("My Pic 2.BMP",name,sizeof(name))==0 && !strcmp(name,"My Pic 2.BMP"));
    CHECK(ftp_safe_name("../secret.bmp",name,sizeof(name))!=0);
    CHECK(ftp_safe_name("..\\secret.bmp",name,sizeof(name))!=0);
    CHECK(ftp_safe_name("/dev_hdd0/x.bmp",name,sizeof(name))!=0);
    CHECK(ftp_safe_name("sub/miku.bmp",name,sizeof(name))!=0);
    CHECK(ftp_safe_name("..",name,sizeof(name))!=0);
    CHECK(ftp_safe_name(".",name,sizeof(name))!=0);
    CHECK(ftp_safe_name(".hidden.bmp",name,sizeof(name))!=0);
    CHECK(ftp_safe_name("",name,sizeof(name))!=0);
    CHECK(ftp_safe_name("a/b",name,sizeof(name))!=0);
    CHECK(ftp_safe_name("a\\b",name,sizeof(name))!=0);
    CHECK(ftp_safe_name("bad\nname.bmp",name,sizeof(name))!=0);
    CHECK(ftp_safe_name("bad\tname.bmp",name,sizeof(name))!=0);
    {
        char big[300];
        memset(big,'a',sizeof(big)-1); big[sizeof(big)-1]=0;
        CHECK(ftp_safe_name(big,name,sizeof(name))!=0);
    }
    CHECK(ftp_safe_name("miku.bmp",name,4)!=0);
    CHECK(ftp_safe_name(NULL,name,sizeof(name))!=0);

    /* BMP extension check (upload validation: name rule). */
    CHECK(ftp_is_bmp_name("miku.bmp")==1);
    CHECK(ftp_is_bmp_name("miku.BMP")==1);
    CHECK(ftp_is_bmp_name("miku.BmP")==1);
    CHECK(ftp_is_bmp_name("miku.png")==0);
    CHECK(ftp_is_bmp_name("miku.bmp.bak")==0);
    CHECK(ftp_is_bmp_name("miku")==0);
    CHECK(ftp_is_bmp_name(".bmp")==1);
    CHECK(ftp_is_bmp_name("miku.")==0);
    CHECK(ftp_is_bmp_name(NULL)==0);

    /* PASV response format. */
    CHECK(ftp_format_pasv("192.168.1.193",2121,out,sizeof(out))==0 &&
          !strcmp(out,"227 Entering Passive Mode (192,168,1,193,8,73).\r\n"));
    CHECK(ftp_format_pasv("10.0.0.5",65535,out,sizeof(out))==0 &&
          !strcmp(out,"227 Entering Passive Mode (10,0,0,5,255,255).\r\n"));
    CHECK(ftp_format_pasv("1.2.3.4",1,out,sizeof(out))==0 &&
          !strcmp(out,"227 Entering Passive Mode (1,2,3,4,0,1).\r\n"));
    CHECK(ftp_format_pasv("192.168.1.256",2121,out,sizeof(out))!=0);
    CHECK(ftp_format_pasv("192.168.1",2121,out,sizeof(out))!=0);
    CHECK(ftp_format_pasv("192.168.1.1.1",2121,out,sizeof(out))!=0);
    CHECK(ftp_format_pasv("192..1.1",2121,out,sizeof(out))!=0);
    CHECK(ftp_format_pasv("abc.def.1.1",2121,out,sizeof(out))!=0);
    CHECK(ftp_format_pasv("",2121,out,sizeof(out))!=0);
    CHECK(ftp_format_pasv("192.168.1.193",0,out,sizeof(out))!=0);
    CHECK(ftp_format_pasv("192.168.1.193",65536,out,sizeof(out))!=0);
    CHECK(ftp_format_pasv("192.168.1.193",2121,out,10)!=0);
    CHECK(ftp_format_pasv(NULL,2121,out,sizeof(out))!=0);

    /* EPSV response format. */
    CHECK(ftp_format_epsv(2121,out,sizeof(out))==0 &&
          !strcmp(out,"229 Entering Extended Passive Mode (|||2121|).\r\n"));
    CHECK(ftp_format_epsv(0,out,sizeof(out))!=0);
    CHECK(ftp_format_epsv(65536,out,sizeof(out))!=0);
    CHECK(ftp_format_epsv(2121,out,10)!=0);

    /* LIST line format: 2026-11-14 10:00:00 UTC = 1794650400. */
    CHECK(ftp_format_list_line("miku.bmp",123456,1794650400L,out,sizeof(out))==0 &&
          !strcmp(out,"-rw-r--r-- 1 0 0     123456 Nov 14 10:00 miku.bmp\r\n"));
    CHECK(ftp_format_list_line("a.bmp",0,0,out,sizeof(out))==0 &&
          !strcmp(out,"-rw-r--r-- 1 0 0          0 Jan  1 00:00 a.bmp\r\n"));
    CHECK(ftp_format_list_line("bad\nname.bmp",10,0,out,sizeof(out))!=0);
    CHECK(ftp_format_list_line("",10,0,out,sizeof(out))!=0);
    CHECK(ftp_format_list_line("a.bmp",-1,0,out,sizeof(out))!=0);
    CHECK(ftp_format_list_line("a.bmp",10,-1,out,sizeof(out))!=0);
    CHECK(ftp_format_list_line("a.bmp",10,0,out,10)!=0);
    CHECK(ftp_format_list_line(NULL,10,0,out,sizeof(out))!=0);

    /* Host stubs: no server on the host, and that must be reported. */
    CHECK(ftp_server_start("/tmp")== -1);
    CHECK(ftp_server_status()==0);
    CHECK(ftp_server_address(out,sizeof(out))==-1);
    CHECK(ftp_take_gallery_dirty()==0);
    ftp_server_stop();

    if(fails) { printf("FTP tests: %d failures\n",fails); return 1; }
    printf("FTP: command parsing, path checks, PASV/EPSV, LIST format and stubs passed\n");
    return 0;
}
