#ifndef NEON_LIBRARY_H
#define NEON_LIBRARY_H
#define NL_PATH 512
#define NL_TRACKS 512
#define NL_ENTRIES 256
typedef struct { char path[NL_PATH]; char name[256]; long mtime; int plays; } Track;
typedef struct { Track tracks[NL_TRACKS]; int count, truncated, errors; } Library;
typedef struct { char name[256]; int directory; } Entry;
int library_scan(Library *library, const char *root);
int library_browse(const char *root, Entry *entries, int capacity);
int library_browse_bmp(const char *root, Entry *entries, int capacity);
int library_join(char *out, const char *root, const char *name);
int library_is_m3u_name(const char *name);
int library_load_m3u(const char *path, Library *out);
/* Write the library back out as a plain .m3u (v1.20, the mirror of
 * library_load_m3u): one absolute path per line under an #EXTM3U
 * header, so the file round-trips through the importer and any
 * other player. Returns the track count written, or -1. */
int library_save_m3u(const char *path, const Library *library);
/* Repeat modes (v1.20) for the finished-track advance. */
enum { NL_REPEAT_OFF=0, NL_REPEAT_ALL=1, NL_REPEAT_ONE=2 };
/* The next queue index when a track ends naturally, or -1 to stop:
 * repeat-one replays cur, repeat-all wraps to 0, off stops at the
 * end of the queue. Pure, so the host tests cover it. */
int queue_advance(int cur, int count, int repeat_mode);
#endif
