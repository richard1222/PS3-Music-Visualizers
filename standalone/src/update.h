#ifndef NL_UPDATE_H
#define NL_UPDATE_H
#include <stddef.h>
#define NL_VERSION "1.00"
#define NL_UPDATE_URL "https://raw.githubusercontent.com/richard1222/PS3-Music-Visualizers/main/standalone/update.json"
#define NL_UPDATE_MAX_EBOOT (8*1024*1024)
typedef struct { char version[16]; char url[512]; char sha256[65]; } UpdateManifest;
int update_parse_version(const char *s,int *major,int *minor);
int update_is_newer(const char *manifest_version);
int update_parse_manifest(const char *text,UpdateManifest *out);
int update_sha256_file(const char *path,char *hex_out);
int update_net_init(void);
void update_net_shutdown(void);
int update_check_and_install(const char *assets_dir,char *msg,size_t msg_size);
#endif
