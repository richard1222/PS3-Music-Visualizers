#ifndef NL_FTP_SERVER_H
#define NL_FTP_SERVER_H
#include <stddef.h>
/* Minimal FTP server for gallery uploads. Port 2121 on purpose: webMAN
 * MOD already serves FTP on port 21, and this server must never clash
 * with it. Anonymous login, one client at a time, passive mode only,
 * gallery folder only. Runs on its own thread; it never touches the
 * gallery surfaces, it only writes files and flags the main thread to
 * reload. */
#define NL_FTP_PORT 2121
#define NL_FTP_MAX_NAME 255

/* --- Pure helpers: no SDL, no PS3 APIs, host-testable. --- */

/* Split one control line into an upper-case verb and its argument.
 * Returns 0 on success (verb filled, *arg points inside line, or to the
 * terminating NUL when there is no argument), -1 for an empty line. */
int ftp_parse_command(char *line,char *verb,size_t verb_size,const char **arg);

/* Accept only a bare file name inside the gallery folder: no path
 * separators, no "." / "..", no leading dot, 1..NL_FTP_MAX_NAME chars.
 * Copies the cleaned name to out. Returns 0 when safe, -1 otherwise. */
int ftp_safe_name(const char *arg,char *name,size_t name_size);

/* 1 when the name ends in .bmp (any case), 0 otherwise. */
int ftp_is_bmp_name(const char *name);

/* Format the PASV reply for a dotted-quad address and port:
 * "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)". Returns 0, or -1
 * for a malformed address or an out-of-range port. */
int ftp_format_pasv(const char *ip,unsigned port,char *out,size_t size);

/* Format the EPSV reply: "229 Entering Extended Passive Mode (|||p|)". */
int ftp_format_epsv(unsigned port,char *out,size_t size);

/* One Unix-style LIST line: "-rw-r--r-- 1 0 0 <size> Mon DD HH:MM <name>".
 * mtime is seconds since the epoch (UTC). Returns 0, or -1 on bad input. */
int ftp_format_list_line(const char *name,long file_size,long mtime,char *out,size_t size);

/* --- Server control (PS3 only; host stubs report "not available"). --- */

/* Start the server thread serving folder (the gallery folder). Returns 0
 * when the thread started; the bind happens on the thread, so check
 * ftp_server_status() for the running/error state. */
int ftp_server_start(const char *folder);
void ftp_server_stop(void);
/* 0 off, 1 starting, 2 running, -1 error (network/bind failure). */
int ftp_server_status(void);
/* "ftp://<ps3-ip>:2121" while running, for the Settings screen.
 * Returns 0 on success, -1 when the server is not running or the
 * address is unknown. */
int ftp_server_address(char *out,size_t size);
/* 1 exactly once after each gallery change (upload/delete) so the main
 * thread can reload the gallery on its own thread; 0 otherwise. */
int ftp_take_gallery_dirty(void);
#endif
