#ifndef NL_UPDATE_H
#define NL_UPDATE_H
#include <stddef.h>
#define NL_VERSION "1.02"
#define NL_UPDATE_URL "https://raw.githubusercontent.com/richard1222/PS3-Music-Visualizers/main/standalone/update.json"
#define NL_UPDATE_MAX_PKG (64*1024*1024)
#define NL_PACKAGES_DIR "/dev_hdd0/packages/"
/* update_parse_manifest returns this when the manifest is readable but
 * lacks the pkg_url/pkg_sha256 fields the pkg-download updater needs. */
#define UPDATE_ERR_INCOMPLETE (-2)
/* The updater downloads a PKG, not an EBOOT: the running EBOOT.BIN is
 * locked by the system and can never be replaced in place. url/sha256
 * below are the package fields from the manifest. */
typedef struct { char version[16]; char pkg_url[512]; char pkg_sha256[65]; } UpdateManifest;
int update_parse_version(const char *s,int *major,int *minor);
int update_is_newer(const char *manifest_version);
int update_parse_manifest(const char *text,UpdateManifest *out);
int update_sha256_file(const char *path,char *hex_out);
int update_net_init(void);
void update_net_shutdown(void);
int update_check_and_install(const char *assets_dir,char *msg,size_t msg_size);
#endif
