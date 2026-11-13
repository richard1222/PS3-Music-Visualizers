#ifndef NEON_LIBRARY_H
#define NEON_LIBRARY_H
#define NL_PATH 512
#define NL_TRACKS 512
#define NL_ENTRIES 256
typedef struct { char path[NL_PATH]; char name[256]; } Track;
typedef struct { Track tracks[NL_TRACKS]; int count, truncated, errors; } Library;
typedef struct { char name[256]; int directory; } Entry;
int library_scan(Library *library, const char *root);
int library_browse(const char *root, Entry *entries, int capacity);
int library_browse_bmp(const char *root, Entry *entries, int capacity);
int library_join(char *out, const char *root, const char *name);
#endif
