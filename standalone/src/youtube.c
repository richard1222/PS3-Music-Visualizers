// youtube extractor - InnerTube player request + parse.
//
// POSTs the player endpoint as the VISIONOS (Apple Vision Pro) client, which returns
// direct (unciphered) urls and - unlike plain ANDROID/WEB - is not behind the
// GVS PoToken wall, so its adaptive (fragmented) urls above 360p don't 403.
// Those streams are fragmented mp4 (moof); the demuxer handles them.
//
// Fragile by nature: Google keeps closing these token-exempt clients. ANDROID_VR
// (Oculus) was capped to ~60 seconds of media on 2026-08-26; VISIONOS replaced it
// and serves full-length urls. Expect this to need swapping again in future.

#include "youtube.h"
#include "yt_net.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ---- Neon Lounge port shims ------------------------------------------------
 * The upstream file targets mohasi's simple-lib-core (fetchHttp, vfs,
 * string-utilities) and TEE's app services (settings, account, subtitles).
 * Neon Lounge replaces those with the small implementations below:
 * fetchHttp/fetchHttpCapturing come from yt_net.c (libcurl + mbedTLS, the
 * app's existing update-checker stack); the file helpers are plain stdio;
 * the content region is fixed to "US" for v1; no signed-in account exists,
 * so account feeds are not ported; every subtitle language is "shown"
 * (captions are parsed into StreamInfo but not displayed in v1). */

static void yt_log_line(const char *level, const char *fmt, va_list ap)
{
   char line[1024];
   vsnprintf(line, sizeof line, fmt, ap);
   yt_log_sink(level, line);
}
static void logInfo(const char *fmt, ...)  { va_list ap; va_start(ap, fmt); yt_log_line("info", fmt, ap); va_end(ap); }
static void logWarn(const char *fmt, ...)  { va_list ap; va_start(ap, fmt); yt_log_line("warn", fmt, ap); va_end(ap); }
static void logError(const char *fmt, ...) { va_list ap; va_start(ap, fmt); yt_log_line("error", fmt, ap); va_end(ap); }

/* from mohasi's string-utilities.h (Apache-2.0) - only what this file uses */
static inline char toLowerChar(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static inline int hexDigit(char c)
{
   if (c >= '0' && c <= '9') return c - '0';
   if (c >= 'a' && c <= 'f') return c - 'a' + 10;
   if (c >= 'A' && c <= 'F') return c - 'A' + 10;
   return -1;
}
static inline int urlDecode(const char *src, char *dst, int cap)
{
   int o = 0;
   while (*src && *src != ' ' && *src != '?' && *src != '\r' && o < cap - 1) {
     char c = *src++;
     if (c == '%') {
       if (!src[0] || !src[1]) return -1;
       int hi = hexDigit(*src); if (hi < 0) return -1; src++;
       int lo = hexDigit(*src); if (lo < 0) return -1; src++;
       dst[o++] = (char)((hi << 4) | lo);
     } else if (c == '+') {
       dst[o++] = ' ';
     } else {
       dst[o++] = c;
     }
   }
   dst[o] = '\0';
   return o;
}
static inline void strCopy(char *dst, int cap, const char *src)
{
   int i = 0;
   while (i < cap - 1 && src[i]) { dst[i] = src[i]; i++; }
   dst[i] = 0;
}

/* vfs replacements (upstream semantics: readFile returns bytes read or -1,
 * deleteFile is idempotent) */
static int readFile(const char *path, char *buf, int cap)
{
   FILE *f = fopen(path, "rb");
   if (!f) return -1;
   int n = (int)fread(buf, 1, cap - 1, f);
   fclose(f);
   buf[n] = 0;
   return n;
}
static int deleteFile(const char *path) { remove(path); return 0; }

/* app services */
static const char *getAppDataDir(void) { return yt_data_dir(); }
static const char *getContentRegionCode(void) { return "US"; }
static const char *getSubtitleLanguage(void) { return ""; }
static int isSubtitleLanguageShown(const char *code) { (void)code; return 1; }
/* channel avatars are a TEE UI extra; Neon Lounge v1 does not keep them */
static int keepChannelAvatar(const char *channelId, const char *from, const char *end)
{ (void)channelId; (void)from; (void)end; return 0; }

#define PLAYER_URL "https://www.youtube.com/youtubei/v1/player?prettyPrint=false"
#define SEARCH_URL "https://www.youtube.com/youtubei/v1/search?prettyPrint=false"
#define BROWSE_URL "https://www.youtube.com/youtubei/v1/browse?prettyPrint=false"
#define RESP_CAP   (2 * 1024 * 1024)   // player JSON can pass 1 MB; a truncated response loses formats (and their urls)

// two clients, each for what it does best:
//  - VISIONOS (playback), see below. WEB/plain ANDROID are PoToken-gated for
//    adaptive (their googlevideo urls 403 above 360p - only progressive itag 18
//    is token-exempt); TVHTML5 hit the bot wall; iOS gave only fragmented adaptive.
//  - VISIONOS (Apple Vision Pro) is currently exempt from the GVS PO-token wall and
//    returns direct (un-ciphered) adaptive urls that serve full-length, so it plays
//    above 360p past the ~60s cap that now hits ANDROID_VR (the client it replaced).
//  - WEB (search): search isn't PoToken-gated, and WEB reliably returns the
//    videoRenderer result structure we parse.
#define PLAYER_VERSION "1.02"
#define WEB_VERSION    "2.20250701.00.00"

// visitor session endpoint: mints an anonymous visitorData that clears the
// logged-out LOGIN_REQUIRED ("confirm you're not a bot") wall (see extract()).
// the key below is youtube's own public web key, shipped in every youtube.com page. not a credential.
#define VISITOR_URL "https://www.youtube.com/youtubei/v1/visitor_id?key=AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8&prettyPrint=false"

// the VISIONOS client object, without the wrapping braces so visitorData can
// be appended before the player request closes it. Its gl stays "US" whatever the content region: the player
// answered alike for every gl probed, and this is the one request that decides whether anything plays.
#define PLAYER_CLIENT \
   "\"clientName\":\"VISIONOS\",\"clientVersion\":\"" PLAYER_VERSION "\"," \
   "\"deviceMake\":\"Apple\",\"deviceModel\":\"RealityDevice17,1\"," \
   "\"osName\":\"visionOS\",\"osVersion\":\"26.5.23O471\",\"hl\":\"en\",\"gl\":\"US\""
// The WEB client of every list request: browse, search and their next pages. hl stays "en" - the parsers key on
// English text (" views", " ago", " watching", "Members only"). gl is the content region (settings.h): every
// format built on this takes it as its FIRST argument, and a request reads getContentRegionCode() once and passes
// that one value. The root object is left open for each body's own fields.
#define WEB_CONTEXT_FMT \
   "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"" WEB_VERSION "\",\"hl\":\"en\",\"gl\":\"%s\"}}"

static const char *PLAYER_UA =
   "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15";
static const char *WEB_UA =
   "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36";

// player body: the client + the minted visitorData, then the video id. two %s: visitorData, videoId.
static const char *PLAYER_BODY_FMT =
   "{\"context\":{\"client\":{" PLAYER_CLIENT ",\"visitorData\":\"%s\"}},\"videoId\":\"%s\",\"contentCheckOk\":true,\"racyCheckOk\":true}";
// (Test 18) the client asked when VISIONOS says UNPLAYABLE: YouTube plays content "made for kids" - Peppa Pig Deutsch,
// PAW Patrol Deutschland, 24/7 streams the German Live search finds - for no client the app used ("This video is not
// available" for VISIONOS, ANDROID_VR, TV, web and embedded alike), but for IOS, with direct urls that googlevideo
// serves live segments for (X-Head-Seqnum, sq) as it does VISIONOS's (probed 2026-11-12). Asked only then.
#define IOS_VERSION "20.10.4"
static const char *IOS_UA = "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)";
static const char *PLAYER_BODY_IOS_FMT =
   "{\"context\":{\"client\":{\"clientName\":\"IOS\",\"clientVersion\":\"" IOS_VERSION "\",\"deviceMake\":\"Apple\","
   "\"deviceModel\":\"iPhone16,2\",\"osName\":\"iPhone\",\"osVersion\":\"18.3.2.22D82\",\"hl\":\"en\",\"gl\":\"US\","
   "\"visitorData\":\"%s\"}},\"videoId\":\"%s\",\"contentCheckOk\":true,\"racyCheckOk\":true}";
// The visitor-session mint keeps its own fixed client, byte for byte what it always sent: the session it returns
// records the IP's country whatever gl says, and it is minted once and then reused for months.
static const char *VISITOR_BODY =
   "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"" WEB_VERSION "\",\"hl\":\"en\",\"gl\":\"US\"}}}";
// search params combine the "Videos" filter (drops the Shorts shelf, channels and playlists) with a sort
// order. index by SortOrder. canonical values from youtube's own filter menu.
static const char *SEARCH_SORT_PARAMS[SORT_COUNT] = {
   "EgIQAQ==",   // SORT_RELEVANCE (youtube's default)
   "CAMSAhAB",   // SORT_VIEWS (popularity / view count, highest first)
};
// The list bodies are string literals, not const char pointers, so -Wall's format check sees every snprintf
// that uses one: a call that forgets the region is a build warning, not a garbage pointer read at run time. The
// check cannot see the ORDER of the arguments (each is a char *), so every call passes gl straight after the
// format, where a grep for the format name followed by gl can count them.
#define SEARCH_BODY_FMT       WEB_CONTEXT_FMT ",\"query\":\"%s\",\"params\":\"%s\"}"      // gl, escaped query, params
// trending feed. YouTube retired the aggregated Trending page in 2025 (FEtrending now redirects to an empty
// home feed); NewPipe/FreeTube/Invidious replaced it with per-category channel-tab browses that still work
// logged-out. see TRENDING_FEEDS for the ids.
#define TRENDING_BODY_FMT     WEB_CONTEXT_FMT ",\"browseId\":\"%s\",\"params\":\"%s\"}"   // gl, browseId, params
#define CONTINUATION_BODY_FMT WEB_CONTEXT_FMT ",\"continuation\":\"%s\"}"                 // gl, token: any feed's next page

static int hex4(const char *p)
{
   int value = 0;
   for (int i = 0; i < 4; i++) {
      char c = p[i], digit;
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
      else return -1;
      value = value * 16 + digit;
   }
   return value;
}

// copy the json string value of "key" found within [from,end), unescaping the
// usual escapes plus \uXXXX (ascii only - urls and titles are ascii; higher
// code points are dropped). returns 0 if the key was not found, 1 if it was, and 2 if its value did not fit
// `cap` and was cut (readField marks such a text with "...").
static int jsonString(const char *from, const char *end, const char *key, char *out, int cap)
{
   char pat[48];
   int patLen = snprintf(pat, sizeof pat, "\"%s\":\"", key);
   const char *p = from ? strstr(from, pat) : NULL;
   if (!p || p >= end) { if (cap) out[0] = 0; return 0; }
   p += patLen;

   int i = 0;
   while (p < end && *p && *p != '"' && i < cap - 1) {
      if (*p != '\\') { out[i++] = *p++; continue; }
      if (++p >= end) break;                       // consume backslash
      char c = *p++;
      switch (c) {
         case 'n': out[i++] = '\n'; break;
         case 't': out[i++] = '\t'; break;
         case 'r': out[i++] = '\r'; break;
         case 'b': out[i++] = '\b'; break;
         case 'f': out[i++] = '\f'; break;
         case 'u': {
            if (p + 4 > end) { p = end; break; }
            int value = hex4(p);
            p += 4;
            if (value >= 0 && value <= 0x7F) out[i++] = (char)value;   // ascii only
            break;
         }
         default: out[i++] = c; break;              // \/ \\ \" and any other: keep literal
      }
   }
   // The cap cut the value short: never end on half a UTF-8 character. A Cyrillic, Greek or Japanese title is
   // 2-3 bytes a character, and a cut one sends the whole label to FONT_POP with a stray letter at its end.
   // Escapes only ever add ASCII, so they cannot leave a lead byte behind.
   int cut = i == cap - 1 && p < end && *p && *p != '"';
   if (cut && ((unsigned char)*p & 0xC0) == 0x80) {
      while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80) i--;   // the cut character's continuation bytes
      if (i > 0 && (unsigned char)out[i - 1] >= 0xC0) i--;               // and its lead byte
   }
   out[i] = 0;
   return cut ? 2 : 1;
}

// A text the viewer reads that its field's size cut short ends in "..." - "The Colin Cowherd Podcast and The Herd
// with Col" read as if the channel were called that. The dots replace the end, at a whole character.
static void markCut(char *out, int cap)
{
   if (cap < 4) return;
   int i = (int)strlen(out);
   if (i > cap - 4) i = cap - 4;                                     // room for "..." and the terminator
   while (i > 0 && ((unsigned char)out[i] & 0xC0) == 0x80) i--;      // never inside a character
   memcpy(out + i, "...", 4);
}

// read the (possibly quoted) integer value of "key" within [from,end). 1 if found.
static int jsonInt(const char *from, const char *end, const char *key, int *out)
{
   char pat[48];
   int patLen = snprintf(pat, sizeof pat, "\"%s\":", key);
   const char *p = from ? strstr(from, pat) : NULL;
   if (!p || p >= end) return 0;
   p += patLen;
   if (*p == '"') p++;      // some numeric fields are quoted (e.g. lengthSeconds)
   *out = atoi(p);
   return 1;
}


static int isIdChar(char c)
{
   return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// copy the 11-char youtube video id from a url (watch?v=, youtu.be/, /shorts/,
// /embed/, /live/) or a bare id into out. returns 1 only if a full 11-char id was found.
static int extractVideoId(const char *input, char *out, int cap)
{
   static const char *markers[] = { "v=", "youtu.be/", "/shorts/", "/embed/", "/live/" };
   const char *id = NULL;
   for (unsigned i = 0; i < sizeof markers / sizeof markers[0]; i++) {
      const char *hit = strstr(input, markers[i]);
      if (hit) { id = hit + strlen(markers[i]); break; }
   }
   if (!id) id = ((int)strlen(input) == 11) ? input : NULL;   // otherwise a bare id
   if (!id) return 0;

   int length = 0;
   while (length < 11 && length < cap - 1 && isIdChar(id[length])) { out[length] = id[length]; length++; }
   out[length] = 0;
   return length == 11;
}

// a bounded strstr: the first needle that starts inside [from, end), or NULL. jsonString/jsonInt run on to the
// end of the response when a key is missing; the audio-track fields below are optional, so each probe stays
// inside its own format object instead of scanning a megabyte for every stream that lacks them.

static const char *findIn(const char *from, const char *end, const char *needle)
{
   int length = (int)strlen(needle);
   for (const char *p = from; p + length <= end; p++)
      if (*p == *needle && memcmp(p, needle, length) == 0) return p;
   return NULL;
}

// case-insensitive substring test; word must be given in lower case
static int containsWord(const char *text, const char *word)
{
   int length = (int)strlen(word);
   for (const char *p = text; *p; p++) {
      int i = 0;
      while (i < length && p[i] && toLowerChar(p[i]) == word[i]) i++;
      if (i == length) return 1;
   }
   return 0;
}

// one key of the url's xtags parameter. An audio stream of a video with several tracks carries them as
// "xtags=acont%3Ddubbed-auto%3Alang%3Dde-DE": acont says what the track is (original, dubbed, dubbed-auto,
// descriptive), lang its language. 1 if found.
static int xtagValue(const char *url, const char *key, char *out, int cap)
{
   out[0] = 0;
   const char *x = strstr(url, "xtags=");
   if (!x) return 0;
   x += 6;
   char encoded[192], tags[192];
   int i = 0;
   while (x[i] && x[i] != '&' && i < (int)sizeof encoded - 1) { encoded[i] = x[i]; i++; }
   encoded[i] = 0;
   if (urlDecode(encoded, tags, sizeof tags) < 0) return 0;

   int keyLength = (int)strlen(key);
   for (const char *p = tags; *p; ) {   // "acont=dubbed-auto:lang=de-DE"
      if (strncmp(p, key, keyLength) == 0 && p[keyLength] == '=') {
         p += keyLength + 1;
         for (i = 0; *p && *p != ':' && i < cap - 1; i++) out[i] = *p++;
         out[i] = 0;
         return 1;
      }
      const char *colon = strchr(p, ':');
      if (!colon) break;
      p = colon + 1;
   }
   return 0;
}

// how good one AAC stream of a track is: itag 140 (AAC-LC, 128k) over the others (139 is 48k HE-AAC), and
// YouTube's normal mix over the dynamic-range-compressed copy it serves beside it
static int audioStreamScore(int itag, int isDrc)
{
   return (itag == 140 ? 2 : 0) + (isDrc ? 0 : 1);
}

// An AAC or (V3.0.1) Opus audio stream that belongs to one of several audio tracks goes into out->audioTracks
// rather than out->formats: one entry per track, holding that track's best stream of each codec. Opus was left
// out until V3.0.1, so 1440p (VP9, whose sound is Opus) played whichever track YouTube listed first - an AI dub
// (Arabic first, the original last, on MrBeast's videos) - and Square had no tracks to offer. The track is named by the reply's
// audioTrack object (id, displayName, audioIsDefault) where it has one, and by the url's xtags otherwise -
// whichever the client returns, the same video can be told apart from its dubs. Returns 1 when the stream
// was such a track stream (taken, or dropped as unplayable), 0 for an ordinary single-track audio stream.
static int collectAudioTrack(const char *object, const char *objectEnd, const StreamFormat *format, StreamInfo *out)
{
   char id[24] = "", name[64] = "", acont[24], lang[16];
   int isDefault = 0;
   const char *block = findIn(object, objectEnd, "\"audioTrack\":{");
   if (block) {
      const char *blockEnd = findIn(block, objectEnd, "}");
      if (!blockEnd) blockEnd = objectEnd;
      if (findIn(block, blockEnd, "\"id\":\""))          jsonString(block, blockEnd, "id", id, sizeof id);
      if (findIn(block, blockEnd, "\"displayName\":\"")) jsonString(block, blockEnd, "displayName", name, sizeof name);
      isDefault = findIn(block, blockEnd, "\"audioIsDefault\":true") != NULL;
   }
   xtagValue(format->url, "acont", acont, sizeof acont);
   xtagValue(format->url, "lang", lang, sizeof lang);
   if (!id[0]) {
      if (!lang[0] && !acont[0]) return 0;   // nothing marks it as one of several: an ordinary audio stream
      snprintf(id, sizeof id, "%.11s.%.11s", lang, acont);
   }
   if (!name[0]) snprintf(name, sizeof name, "%s%s%s", lang[0] ? lang : "Audio", acont[0] ? " " : "", acont);

   // displayName is in the request's language (hl=en): "English (United States) original",
   // "German (auto-dubbed)", "English descriptive". acont says the same without depending on hl.
   int rank;
   if (containsWord(name, "descriptive") || strcmp(acont, "descriptive") == 0)    rank = AUDIO_RANK_DESCRIPTIVE;
   else if (containsWord(name, "original") || strcmp(acont, "original") == 0)     rank = AUDIO_RANK_ORIGINAL;
   else if (containsWord(name, "dubbed") || strncmp(acont, "dubbed", 6) == 0)     rank = AUDIO_RANK_DUBBED;
   else if (isDefault)                                                           rank = AUDIO_RANK_DEFAULT;
   else                                                                          rank = AUDIO_RANK_OTHER;

   if (format->needsCipher || !format->url[0]) return 1;   // one of the tracks' streams, but not playable

   AudioTrack *track = NULL;
   for (int i = 0; i < out->audioTrackCount; i++)
      if (strcmp(out->audioTracks[i].id, id) == 0) { track = &out->audioTracks[i]; break; }
   if (!track) {
      if (out->audioTrackCount >= MAX_AUDIO_TRACKS) return 1;
      track = &out->audioTracks[out->audioTrackCount++];
      memset(track, 0, sizeof *track);
      strCopy(track->id, sizeof track->id, id);
      strCopy(track->name, sizeof track->name, name);
      track->rank = rank;
   }
   if (format->isOpus) {   // itag 251 only (parseFormatArray): YouTube's normal mix over its compressed copy
      if (!track->opusItag || (track->opusIsDrc && !format->isDrc)) {
         track->opusItag = format->itag;
         track->opusIsDrc = format->isDrc;
         strCopy(track->opusUrl, sizeof track->opusUrl, format->url);
      }
   } else if (!track->itag || audioStreamScore(format->itag, format->isDrc) > audioStreamScore(track->itag, track->isDrc)) {
      track->itag = format->itag;
      track->isDrc = format->isDrc;
      strCopy(track->url, sizeof track->url, format->url);
   }
   return 1;
}

// one stream of the chosen track, into formats[] as an ordinary audio stream
static void addTrackStream(StreamInfo *out, int itag, int isDrc, int isOpus, const char *url)
{
   if (out->formatCount >= MAX_STREAM_FORMATS) { logError("[yt] no room for the audio stream\n"); return; }
   StreamFormat *format = &out->formats[out->formatCount++];
   memset(format, 0, sizeof *format);
   format->itag = itag;
   format->hasAudio = 1;
   format->isDrc = isDrc;
   format->isOpus = isOpus;
   strCopy(format->container, sizeof format->container, isOpus ? "webm" : "mp4");
   strCopy(format->url, sizeof format->url, url);
}

// Several audio tracks: sort them best first - stable, so equal ranks keep YouTube's order - and put the best
// one's streams into formats[], where pickBestAudio and pickOpusAudio find them as the only audio on offer.
static void finishAudioTracks(StreamInfo *out)
{
   for (int i = 1; i < out->audioTrackCount; i++) {
      if (out->audioTracks[i - 1].rank >= out->audioTracks[i].rank) continue;   // already in place
      AudioTrack moving = out->audioTracks[i];   // ~4 KB of stack; the resolve workers have 64 KB
      int j = i;
      while (j > 0 && out->audioTracks[j - 1].rank < moving.rank) { out->audioTracks[j] = out->audioTracks[j - 1]; j--; }
      out->audioTracks[j] = moving;
   }
   if (out->audioTrackCount == 0) return;

   // the best track's AAC and Opus streams - the first track that has one, should the best lack it
   for (int i = 0; i < out->audioTrackCount; i++) {
      const AudioTrack *track = &out->audioTracks[i];
      if (track->url[0]) { addTrackStream(out, track->itag, track->isDrc, 0, track->url); break; }
   }
   for (int i = 0; i < out->audioTrackCount; i++) {
      const AudioTrack *track = &out->audioTracks[i];
      if (track->opusUrl[0]) { addTrackStream(out, track->opusItag, track->opusIsDrc, 1, track->opusUrl); break; }
   }
}

// parse one formats array (region [arrayStart, regionEnd)) into out->formats.
// progressive entries are muxed (video+audio); adaptive entries are video-only
// or audio-only, classified from the mimeType.
static void parseFormatArray(const char *arrayStart, const char *regionEnd, int progressive, StreamInfo *out)
{
   const char *scan = arrayStart;
   while (out->formatCount < MAX_STREAM_FORMATS) {
      const char *object = strstr(scan, "\"itag\":");
      if (!object || object >= regionEnd) break;
      const char *next = strstr(object + 7, "\"itag\":");
      const char *objectEnd = (next && next < regionEnd) ? next : regionEnd;

      StreamFormat *format = &out->formats[out->formatCount];
      memset(format, 0, sizeof *format);
      jsonInt(object, objectEnd, "itag", &format->itag);
      jsonInt(object, objectEnd, "width", &format->width);
      jsonInt(object, objectEnd, "height", &format->height);
      jsonInt(object, objectEnd, "fps", &format->fps);
      format->needsCipher = !jsonString(object, objectEnd, "url", format->url, sizeof format->url);

      // mimeType is like: video/mp4; codecs="avc1.640028". only mark a stream playable when its codec is
      // one we can actually decode - H.264 (avc1/avc3) video, AAC (mp4a) audio. newer itags carry AV1/VP9
      // video or Opus audio in an mp4/webm container that our demuxer+decoder can't play (itag 787 = AV1),
      // and picking one of those over a real avc1 stream made the video fail to open.
      char mimeType[64] = "";
      jsonString(object, objectEnd, "mimeType", mimeType, sizeof mimeType);
      strCopy(format->container, sizeof format->container, strstr(mimeType, "webm") ? "webm" : "mp4");
      int isAvc = strstr(mimeType, "avc1") != NULL || strstr(mimeType, "avc3") != NULL;
      int isAac = strstr(mimeType, "mp4a") != NULL;
      // (V3.0.0) VP9 in WebM above 1080p, up to 2560 x 1440: YouTube's 1440p (itag 308 at 60 fps, 271 at 30), which
      // the TEE VP9 building block decodes on the SPEs. Profile 0 only - codecs="vp9" or "vp09.00..." - the HDR
      // ones (vp09.02, 10 bit) it refuses. Lower VP9 stays out: H.264 covers it, and every format kept takes an
      // array slot the AAC audio (listed after all the video) needs.
      int isVp9 = strstr(mimeType, "webm") && (strstr(mimeType, "\"vp9\"") || strstr(mimeType, "vp09.00.")) &&
                  format->height > 1080 && format->height <= 1440 && format->width <= 2560;
      // (Test 14) VP9 1080p - 303 at 60 fps, 248 at 30 - is kept after all, beside 1440p: the stream the player changes
      // to where 1440p is too heavy for the decoder (the VP9 session's measurement: 86 - 97 pictures a second in 1080p)
      int isVp9Low = strstr(mimeType, "webm") && (strstr(mimeType, "\"vp9\"") || strstr(mimeType, "vp09.00.")) &&
                     (format->itag == 303 || format->itag == 248) && format->height <= 1080 && format->width <= 1920;
      if (progressive)                              format->hasVideo = format->hasAudio = 1;
      // (V3.0.0) Opus in WebM, itag 251 only (the best of YouTube's three): VP9's sound, decoded on the PPU
      else if (strncmp(mimeType, "audio/", 6) == 0) {
         int isOpus = strstr(mimeType, "webm") && strstr(mimeType, "opus") && format->itag == 251;
         format->hasAudio = isAac || isOpus;
         format->isOpus = isOpus && !isAac;
      }
      else {
         format->hasVideo = isAvc || isVp9;
         format->isVp9 = isVp9 && !isAvc;
         if (!format->hasVideo && isVp9Low) format->isVp9 = format->vp9Fallback = 1;   // (Test 14) beside, not offered
      }

      // an audio stream of a video with several audio tracks (other languages, AI dubs) is filed by track
      // instead - see collectAudioTrack; the chosen track's streams join formats[] once the array is read.
      // (V3.0.1) Opus too: two dozen dubs' Opus streams used to fill formats[] here, the original's last of all
      int tracked = 0;
      if (format->hasAudio && !format->hasVideo) {
         char drc[4];
         format->isDrc = findIn(object, objectEnd, "\"isDrc\":true") != NULL ||
                         (xtagValue(format->url, "drc", drc, sizeof drc) && drc[0] == '1');
         tracked = collectAudioTrack(object, objectEnd, format, out);
      }

      // keep only formats we can decode (H.264 video, VP9 1440p / AAC audio). dropping AV1/Opus frees array slots
      // so the AAC audio - which youtube lists AFTER all the video formats - isn't pushed past the cap.
      if ((format->hasVideo || format->hasAudio || format->vp9Fallback) && !tracked) out->formatCount++;
      scan = objectEnd;
   }
}

// (V3.0.3) An auto-dubbed video has a speech-recognition track for every dub besides the original's ("a.de" for the
// German dub, "a.en" for the English original): transcripts of a machine's translation, two dozen of them on some
// videos, sorted by name - so the original's could fall past MAX_CAPTION_TRACKS, and a translation was made from the
// first, a dub's. The caption list's audio tracks say which is the original: its id ends in ".4" ("en-US.4"; a dub's
// in ".10"). Its language into `original` ("en"); 1 when the video has other audio tracks besides.
static int findOriginalAudioLanguage(const char *audio, const char *end, char *original, int cap)
{
   int others = 0;
   original[0] = 0;
   for (const char *id = findIn(audio, end, "\"audioTrackId\":\""); id; id = findIn(id + 1, end, "\"audioTrackId\":\"")) {
      const char *value = id + 16;
      const char *quote = findIn(value, end, "\"");
      if (!quote) break;
      int length = (int)(quote - value);
      if (length >= 3 && value[length - 2] == '.' && value[length - 1] == '4') {
         int letters = 0;
         while (letters < length - 2 && letters < cap - 1 && value[letters] != '-' && value[letters] != '.') {
            original[letters] = value[letters];
            letters++;
         }
         original[letters] = 0;
      } else {
         others = 1;
      }
   }
   return others;
}

// whether two language codes name the same language: "en" and "en-US" do
static int sameLanguage(const char *code, const char *language)
{
   size_t length = strlen(language);
   return length > 0 && strncmp(code, language, length) == 0 && (code[length] == 0 || code[length] == '-');
}

// (V3.0.3) the most caption tracks one look at a video takes in - more than any video seen had (26 on a MrBeast
// video); the rest would be left out
#define CAPTION_SCAN_MAX 64

// one caption track's entry in the reply into `track`; 0 when it lacks its url or language
static int copyCaptionTrack(const char *entry, const char *entryEnd, CaptionTrack *track)
{
   if (!jsonString(entry, entryEnd, "baseUrl", track->url, sizeof track->url) ||
       !jsonString(entry, entryEnd, "languageCode", track->languageCode, sizeof track->languageCode))
      return 0;
   if (!jsonString(entry, entryEnd, "text", track->name, sizeof track->name) &&
       !jsonString(entry, entryEnd, "simpleText", track->name, sizeof track->name))
      strCopy(track->name, sizeof track->name, track->languageCode);
   track->translatable  = findIn(entry, entryEnd, "\"isTranslatable\":true") != NULL;
   track->autoGenerated = findIn(entry, entryEnd, "\"kind\":\"asr\"") != NULL;
   return 1;
}

// parse the caption track list into out->captions. the list ends where the renderer's next field
// starts (entries hold nested arrays, so a plain ']' scan would cut early); each entry leads with
// "baseUrl". the display name sits in the entry's name block as "text" (runs) or "simpleText".
// (V3.0.3) YouTube lists the tracks by name, and the first MAX_CAPTION_TRACKS of them used to be kept before the
// player dropped the languages its fonts do not draw: on a video with two dozen languages Arabic, Bangla and two
// Chinese took half the places, and Polish, Russian, Spanish, Portuguese, Turkish and Italian - written by people -
// were never offered. Now every track is looked at first. Those the fonts cannot draw and a dub's transcripts are
// left out, and the places go to the subtitle language's tracks first (getSubtitleLanguage, the region's by
// default), then the original's (the language of the audio track marked ".4", else of the speech recognition),
// then the rest in YouTube's order - in each, a written track before speech recognition.
static void parseCaptionTracks(const char *resp, const char *end, StreamInfo *out)
{
   const char *captions = strstr(resp, "\"captionTracks\"");
   if (!captions) return;
   const char *listEnd = strstr(captions, "\"audioTracks\"");
   if (!listEnd) listEnd = strstr(captions, "\"translationLanguages\"");
   if (!listEnd) listEnd = end;

   // (V3.0.3) a dubbed video's speech-recognition tracks: only the original's is kept
   char original[12] = "";
   int dubbed = 0;
   if (listEnd < end && strncmp(listEnd, "\"audioTracks\"", 13) == 0) {
      const char *audioEnd = strstr(listEnd, "\"translationLanguages\"");
      dubbed = findOriginalAudioLanguage(listEnd, audioEnd ? audioEnd : end, original, sizeof original);
   }

   // every track's place in the reply, its language and whether it is speech recognition
   struct { const char *entry, *entryEnd; char code[12]; int asr, rank; } found[CAPTION_SCAN_MAX];
   int foundCount = 0, total = 0, noFont = 0, transcripts = 0;
   for (const char *scan = captions; ; ) {
      const char *entry = strstr(scan, "\"baseUrl\"");
      if (!entry || entry >= listEnd) break;
      const char *next = strstr(entry + 1, "\"baseUrl\"");
      const char *entryEnd = (next && next < listEnd) ? next : listEnd;
      scan = entryEnd;
      char code[12];
      if (!jsonString(entry, entryEnd, "languageCode", code, sizeof code)) continue;
      total++;
      int asr = findIn(entry, entryEnd, "\"kind\":\"asr\"") != NULL;
      if (!isSubtitleLanguageShown(code)) { noFont++; continue; }
      if (dubbed && original[0] && asr && !sameLanguage(code, original)) { transcripts++; continue; }
      if (foundCount == CAPTION_SCAN_MAX) continue;
      found[foundCount].entry = entry;
      found[foundCount].entryEnd = entryEnd;
      strCopy(found[foundCount].code, sizeof found[foundCount].code, code);
      found[foundCount].asr = asr;
      foundCount++;
   }
   if (!original[0])   // no audio track says which is the original: speech recognition is in the video's language
      for (int i = 0; i < foundCount; i++)
         if (found[i].asr) { strCopy(original, sizeof original, found[i].code); break; }

   const char *want = getSubtitleLanguage();
   for (int i = 0; i < foundCount; i++) {
      int group = (want[0] && sameLanguage(found[i].code, want)) ? 0 : (original[0] && sameLanguage(found[i].code, original)) ? 1 : 2;
      found[i].rank = group * 2 + found[i].asr;
   }
   char order[96] = "";
   for (int rank = 0; rank < 6; rank++)
      for (int i = 0; i < foundCount && out->captionCount < MAX_CAPTION_TRACKS; i++) {
         if (found[i].rank != rank || !copyCaptionTrack(found[i].entry, found[i].entryEnd, &out->captions[out->captionCount]))
            continue;
         out->captionCount++;
         size_t used = strlen(order);
         snprintf(order + used, sizeof order - used, "%s%s%s", used ? " " : "", found[i].code, found[i].asr ? "*" : "");
      }
   if (total)
      logInfo("[yt] %d of %d caption track(s): %s (* speech recognition; left out: %d without a font, %d dub transcript(s))\n",
              out->captionCount, total, order, noFont, transcripts);

   // (V3.0.0, issue #4) YouTube's translation: its list of languages, and the one set for subtitles in it. Each entry is
   // {"languageCode":"cs","languageName":{"runs":[{"text":"Czech"}]}} (or simpleText).
   const char *languages = strstr(captions, "\"translationLanguages\"");
   if (want[0] && languages && out->captionCount > 0) {
      char needle[40];
      snprintf(needle, sizeof needle, "\"languageCode\":\"%s\"", want);
      const char *hit = findIn(languages, end, needle);
      if (hit) {
         const char *following = findIn(hit + 1, end, "\"languageCode\"");
         const char *hitEnd = following ? following : end;
         strCopy(out->translateCode, sizeof out->translateCode, want);
         if (!jsonString(hit, hitEnd, "text", out->translateName, sizeof out->translateName) &&
             !jsonString(hit, hitEnd, "simpleText", out->translateName, sizeof out->translateName))
            strCopy(out->translateName, sizeof out->translateName, want);
         logInfo("[yt] subtitles can be translated into %s (%s)\n", out->translateName, want);
      } else {
         logInfo("[yt] subtitles: no translation into %s offered\n", want);
      }
   }
}

// parse both stream arrays. iOS returns only adaptiveFormats (separate video and
// audio); WEB/TVHTML5 would also carry a progressive "formats" (muxed) array.
static void parseFormats(const char *resp, const char *end, StreamInfo *out)
{
   const char *streaming = strstr(resp, "\"streamingData\"");
   if (!streaming) return;
   const char *progressive = strstr(streaming, "\"formats\"");
   const char *adaptive    = strstr(streaming, "\"adaptiveFormats\"");

   if (progressive)
      parseFormatArray(progressive, (adaptive && adaptive > progressive) ? adaptive : end, 1, out);
   if (adaptive)
      parseFormatArray(adaptive, end, 0, out);
   finishAudioTracks(out);
}

// The anonymous visitor session. Without it a logged-out player request gets LOGIN_REQUIRED. Minting
// a fresh one every launch is itself a bot signal that re-triggers the wall, so it persists to disk:
// we reuse a saved session and only mint (and save) when there's none, or a saved one goes stale.
//
// The file lives in the directory migrateAppData settled on (see app-paths.h). It used to be built from
// YO_DATA_DIR, which pointed it at /dev_hdd0/yo-player even on a console that refuses that folder: there
// nothing was ever saved, and every launch minted a fresh session - the very thing this cache exists to avoid.
static void visitorCachePath(char *out, int cap)
{
   snprintf(out, cap, "%s/visitor.txt", getAppDataDir());
}

static char visitorData[192];
static int  visitorFromDisk;   // the current session was loaded from the cache (vs minted this launch)

static int loadVisitorData(void)
{
   char path[128];
   visitorCachePath(path, sizeof path);
   return readFile(path, visitorData, sizeof visitorData) > 0 && visitorData[0] != 0;
}

static void saveVisitorData(void)
{
   char path[128], tmp[160];
   visitorCachePath(path, sizeof path);
   /* temp file + rename, so a power cut mid-write can never leave a
    * half-written session behind (PCStream certstore discipline) */
   snprintf(tmp, sizeof tmp, "%s.tmp", path);
   FILE *f = fopen(tmp, "wb");
   if (!f) return;
   fwrite(visitorData, 1, strlen(visitorData), f);
   fclose(f);
   if (rename(tmp, path) != 0) { remove(path); rename(tmp, path); }
}

// a saved session that hit LOGIN_REQUIRED is stale: forget it so the next play mints a fresh one once.
static void dropVisitorData(void)
{
   char path[128];
   visitorCachePath(path, sizeof path);
   deleteFile(path);
   visitorData[0] = 0;
   visitorFromDisk = 0;
}

static int ensureVisitorData(void)
{
   if (visitorData[0]) return 0;
   if (loadVisitorData()) { visitorFromDisk = 1; return 0; }   // reuse the persisted session

   HttpHeader headers[] = {
      { "Content-Type", "application/json" },
      { "User-Agent", WEB_UA },
      { "X-YouTube-Client-Name", "1" },
      { "X-YouTube-Client-Version", WEB_VERSION },
   };
   char *resp = malloc(RESP_CAP);
   if (!resp) return -1;

   int respLen = 0, status = 0;
   int rc = fetchHttp("POST", VISITOR_URL, headers, 4, VISITOR_BODY, (int)strlen(VISITOR_BODY), resp, RESP_CAP, &respLen, &status);
   int ok = (rc == 0 && status == 200 && jsonString(resp, resp + respLen, "visitorData", visitorData, sizeof visitorData));
   free(resp);

   if (!ok) { logError("[yt] visitor_id failed rc=%d status=%d\n", rc, status); return -1; }
   urlDecode(visitorData, visitorData, sizeof visitorData);   // endpoint returns it percent-encoded (%3D)
   visitorFromDisk = 0;
   logInfo("[yt] visitorData minted (%d chars)\n", (int)strlen(visitorData));
   return 0;
}

// XML text nodes escape '&' as "&amp;"; the BaseURL query string needs it back to fetch.
static void unescapeAmp(char *text)
{
   char *write = text;
   for (char *read = text; *read; ) {
      if (strncmp(read, "&amp;", 5) == 0) { *write++ = '&'; read += 5; }
      else *write++ = *read++;
   }
   *write = 0;
}

// copy the first `attr="..."` (or `<tag>...` with close="<") value found at/after text into out; empty if absent.
static void extractBetween(const char *text, const char *open, const char *close, char *out, int cap)
{
   out[0] = 0;
   const char *start = strstr(text, open);
   if (!start) return;
   start += strlen(open);
   const char *stop = strstr(start, close);
   if (!stop) return;
   int length = (int)(stop - start);
   if (length >= cap) length = cap - 1;
   memcpy(out, start, length);
   out[length] = 0;
}

// a live stream's itag urls are a rolling DVR window whose byte-0 init rolls off, so play by DASH segments
// instead: parse the manifest and, for each adaptiveFormat, replace its url with that Representation's
// segment base ("<base>sq/<n>/...") and record the available sq range (window start .. live edge). kind is
// "dashseg" for a live stream and "dashvod" for one that just ended, whose last segment is its end (video-source.c).
static void parseLiveManifest(const char *resp, const char *end, StreamInfo *out, const char *kind)
{
   char manifestUrl[1400];
   if (!jsonString(resp, end, "dashManifestUrl", manifestUrl, sizeof manifestUrl)) return;

   int cap = 4 * 1024 * 1024;   // live DVR manifests enumerate every segment and can pass 1 MB
   char *mpd = (char *)malloc(cap);
   if (!mpd) return;
   HttpHeader headers[] = { { "User-Agent", PLAYER_UA }, { "Accept-Encoding", "identity" } };
   int length = 0, status = 0;
   if (fetchHttp("GET", manifestUrl, headers, 2, NULL, 0, mpd, cap, &length, &status) != 0 || status != 200) {
      logError("[yt] live manifest fetch status=%d len=%d\n", status, length);
      free(mpd);
      return;
   }
   if (length >= cap - 1) logWarn("[yt] live manifest truncated at %d bytes\n", length);

   for (int i = 0; i < out->formatCount; i++) {
      StreamFormat *format = &out->formats[i];
      char idTag[24];
      snprintf(idTag, sizeof idTag, "id=\"%d\"", format->itag);
      const char *rep = strstr(mpd, idTag);
      if (!rep) continue;
      const char *repEnd = strstr(rep, "</Representation>");
      if (!repEnd) repEnd = mpd + length;

      char base[MAX_STREAM_URL];
      extractBetween(rep, "<BaseURL>", "</BaseURL>", base, sizeof base);
      if (!base[0] || strstr(rep, "<BaseURL>") > repEnd) continue;   // this rep has no own BaseURL
      unescapeAmp(base);

      // first + last media="sq/<n>/..." within this representation = the window start and the live edge
      long startSq = -1, edgeSq = -1;
      for (const char *media = rep; (media = strstr(media, "media=\"sq/")) != NULL && media < repEnd; media++) {
         long number = 0;
         for (const char *digit = media + 10; *digit >= '0' && *digit <= '9'; digit++) number = number * 10 + (*digit - '0');
         if (startSq < 0) startSq = number;
         edgeSq = number;
      }
      if (edgeSq < 0) continue;

      format->isLiveSegmented = 1;
      format->liveStartSq = startSq;
      format->liveEdgeSq = edgeSq;
      // the player opens this through the live segment source (see video-source / live-source)
      snprintf(format->url, sizeof format->url, "%s|%ld|%ld|%s", kind, startSq, edgeSq, base);
      logInfo("[yt] live itag %d: sq %ld..%ld baseTail=%s\n", format->itag, startSq, edgeSq,
              (int)strlen(base) > 40 ? base + strlen(base) - 40 : base);
   }
   free(mpd);
}

// The token-exempt clients (VISIONOS, and ANDROID_VR before it) answer live videos WITHOUT a
// dashManifestUrl, so parseLiveManifest has nothing to read and the player would open a rolling
// live url as if it were a static file - a few seconds of video, then "couldn't open stream".
//
// The manifest is not actually needed: googlevideo serves those same urls segment-wise via
// "&sq=<n>" and reports the current live edge in the X-Head-Seqnum response header. Probe the
// first format for that header and build the same dashseg descriptors parseLiveManifest would
// have produced, in the query form ("<url>&sq=") that live-source appends the number to. A stream that just
// ended answers the same way, with its last segment as the head.
static void parseLiveFromSeqnum(StreamInfo *out, const char *kind)
{
   if (out->formatCount <= 0 || !out->formats[0].url[0]) return;

   char seqnum[40] = "";
   // Only the head is wanted: a one-byte buffer reads none of the body. On a stream that had just ended the
   // server sent the head at once and then held the 1-byte body until the connection broke, ~7.6 s later
   // (TEE's console, 2026-11-07, Test11) - and the failed read made the probe throw away the head it had.
   char body[1];
   HttpHeader headers[] = { { "User-Agent", PLAYER_UA }, { "Range", "bytes=0-0" } };
   // findHeader lowercases only the response side, so the name has to be lowercase here
   HttpHeaderCapture capture = { "x-head-seqnum", seqnum, sizeof seqnum };
   int length = 0, status = 0;
   int rc = fetchHttpCapturing("GET", out->formats[0].url, headers, 2, NULL, 0,
                               body, sizeof body, &length, &status, &capture);
   if (rc != 0 && !seqnum[0]) {   // the head is taken before the body is read: it counts whenever it came
      logError("[yt] live seqnum probe failed (status=%d)\n", status);
      return;
   }

   long edgeSq = 0;
   for (const char *digit = seqnum; *digit >= '0' && *digit <= '9'; digit++)
      edgeSq = edgeSq * 10 + (*digit - '0');
   if (edgeSq <= 0) {
      logError("[yt] live probe: no X-Head-Seqnum (status=%d)\n", status);
      return;
   }

   int rewritten = 0;
   for (int i = 0; i < out->formatCount; i++) {
      StreamFormat *format = &out->formats[i];
      if (!format->url[0]) continue;

      char rebuilt[MAX_STREAM_URL];
      // "&sq=" is 4 chars; snprintf truncates rather than overflows, so drop anything that
      // would not survive the round trip instead of handing the player a cut-off url.
      if (snprintf(rebuilt, sizeof rebuilt, "%s|0|%ld|%s&sq=", kind, edgeSq, format->url)
          >= (int)sizeof rebuilt) continue;

      strCopy(format->url, sizeof format->url, rebuilt);
      format->isLiveSegmented = 1;
      format->liveStartSq = 0;
      format->liveEdgeSq = edgeSq;
      rewritten++;
   }
   logInfo("[yt] live via X-Head-Seqnum: edge sq %ld, %d/%d formats\n",
           edgeSq, rewritten, out->formatCount);
}

/* (Test 15) upstream quiets per-thread resolve logs for its background
 * resolver; Neon Lounge v1 resolves one video at a time, on user request. */
static int isExtractQuiet(void) { return 0; }

static int extract(const char *input, StreamInfo *out)
{
   memset(out, 0, sizeof *out);
   int quiet = isExtractQuiet();

   char videoId[16];
   if (!extractVideoId(input, videoId, sizeof videoId)) return -1;
   if (ensureVisitorData() != 0) return -1;

   char body[1024];
   HttpHeader headers[] = {
      { "Content-Type", "application/json" },
      { "User-Agent", PLAYER_UA },
      { "X-YouTube-Client-Name", "101" },
      { "X-YouTube-Client-Version", PLAYER_VERSION },
      { "X-Goog-Visitor-Id", visitorData },
      { "Origin", "https://www.youtube.com" },
      { "Accept-Encoding", "identity" },
   };

   char *resp = malloc(RESP_CAP);
   if (!resp) return -1;

   int respLen = 0, status = 0;
   const char *end = resp;
   // VISIONOS first; (Test 18) IOS once more when VISIONOS calls the video UNPLAYABLE and gives no streams
   for (int asIos = 0; asIos < 2; asIos++) {
      int bodyLen = snprintf(body, sizeof body, asIos ? PLAYER_BODY_IOS_FMT : PLAYER_BODY_FMT, visitorData, videoId);
      if (asIos) {
         headers[1].value = IOS_UA;
         headers[2].value = "5";
         headers[3].value = IOS_VERSION;
      }
      int rc = fetchHttp("POST", PLAYER_URL, headers, 7, body, bodyLen, resp, RESP_CAP, &respLen, &status);
      if (rc < 0)        { logError("[yt] player POST failed rc=%d\n", rc); free(resp); return rc; }
      if (status != 200) { logError("[yt] player POST status=%d respLen=%d\n", status, respLen); free(resp); return -status; }
      end = resp + respLen;
      if (asIos) break;
      const char *play = strstr(resp, "\"playabilityStatus\"");
      char playStatus[32] = "";
      jsonString(play ? play : resp, end, "status", playStatus, sizeof playStatus);
      if (strcmp(playStatus, "UNPLAYABLE") != 0 || strstr(resp, "\"streamingData\"")) break;
      logInfo("[yt] %s: UNPLAYABLE for VISIONOS (made for kids?) - asking as IOS\n", videoId);
   }

   // metadata
   const char *details = strstr(resp, "\"videoDetails\"");
   jsonString(details ? details : resp, end, "title", out->title, sizeof out->title);
   jsonString(details ? details : resp, end, "shortDescription", out->description, sizeof out->description);
   char lenText[16];
   if (jsonString(details ? details : resp, end, "lengthSeconds", lenText, sizeof lenText))
      out->durationSeconds = atoi(lenText);

   parseCaptionTracks(resp, end, out);
   parseFormats(resp, end, out);

   // Diagnostic: the whole ladder this client was offered. stream-select only ever reports the
   // one format it picked, which cannot tell you WHY - e.g. whether a 1080p30 rendition existed
   // at all, or whether the 1080p60 was simply the only 1080p on offer.
   for (int i = 0; i < out->formatCount && !quiet; i++) {
      const StreamFormat *f = &out->formats[i];
      logInfo("[yt] fmt itag=%d %dx%d %dfps %s vid=%d aud=%d cipher=%d url=%d drc=%d\n",
              f->itag, f->width, f->height, f->fps, f->container,
              f->hasVideo, f->hasAudio, f->needsCipher, f->url[0] ? 1 : 0, f->isDrc);
   }
   // and every audio track, best first: the first one is what plays
   for (int i = 0; i < out->audioTrackCount && !quiet; i++) {
      const AudioTrack *t = &out->audioTracks[i];
      logInfo("[yt] audio track %d: %s [%s] rank=%d itag=%d%s opus=%d%s\n", i, t->name, t->id, t->rank, t->itag,
              t->isDrc ? " drc" : "", t->opusItag, t->opusIsDrc ? " drc" : "");
   }

   // a currently-live stream (isLive:true) is a rolling DVR window, so the init/moov at byte 0 may have
   // rolled off - it needs the DASH manifest's explicit init + segment template rather than a static open.
   {
      int liveNow      = strstr(resp, "\"isLive\":true") != NULL;
      // A stream that just ended: for a while YouTube serves its recording only as the live segments, sq 0 to the
      // last one (204 past it), until it has made a video of it - opened as a file it has no moov ("couldn't open
      // stream" on TEE's console, 2026-11-07). It plays like a live stream whose edge stays put and ends there.
      int postLive     = !liveNow && strstr(resp, "\"isPostLiveDvr\":true") != NULL;
      const char *dash = strstr(resp, "\"dashManifestUrl\"");
      out->isLive = liveNow;
      out->isPostLive = postLive;
      if (liveNow || postLive) {
         logInfo("[yt] %s (dash=%d formats=%d dur=%d)\n", liveNow ? "live stream" : "live stream that just ended",
                 dash ? 1 : 0, out->formatCount, out->durationSeconds);
         // the rewind: the streamer's DVR switch, and every adaptive format's segment length and window (the
         // same for all of a stream's formats). Seen 2026-11-07: DVR on for 2 of 6 live streams, 5 s segments
         // (2 s for low latency), a 12 h window; googlevideo still served segments 14 h back with DVR off.
         out->liveDvr = strstr(resp, "\"isLiveDvrEnabled\":true") != NULL;
         const char *streaming = strstr(resp, "\"streamingData\"");
         jsonInt(streaming, end, "targetDurationSec", &out->liveSegmentSec);
         jsonInt(streaming, end, "maxDvrDurationSec", &out->liveWindowSec);
         logInfo("[yt] live rewind %s, %d s segments, window %d s\n", out->liveDvr ? "on" : "off",
                 out->liveSegmentSec, out->liveWindowSec);
         const char *kind = postLive ? "dashvod" : "dashseg";
         parseLiveManifest(resp, end, out, kind);
         int segmented = 0;
         for (int i = 0; i < out->formatCount; i++)
            if (out->formats[i].isLiveSegmented) { segmented = 1; break; }
         // no dashManifestUrl in the reply (every token-exempt client): fall back to the edge probe
         if (!segmented) parseLiveFromSeqnum(out, kind);
      }
   }

   if (out->formatCount == 0) {
      char statusText[32] = "";
      const char *play = strstr(resp, "\"playabilityStatus\"");
      jsonString(play ? play : resp, end, "status", statusText, sizeof statusText);
      logError("[yt] no formats, respLen=%d playability=%s\n", respLen, statusText);
      if (visitorFromDisk && strcmp(statusText, "LOGIN_REQUIRED") == 0) dropVisitorData();   // stale session
   } else {
      if (!quiet) logInfo("[yt] resolved %d formats\n", out->formatCount);
      if (!visitorFromDisk) { saveVisitorData(); visitorFromDisk = 1; }   // persist a session that works
   }

   free(resp);
   return 0;
}

// copy src into out as a json string body value, escaping " and \ (and dropping
// control chars). the query comes from the on-screen keyboard.
static void jsonEscape(const char *src, char *out, int cap)
{
   int i = 0;
   for (; *src && i < cap - 2; src++) {
      unsigned char c = (unsigned char)*src;
      if (c == '"' || c == '\\') { out[i++] = '\\'; out[i++] = (char)c; }
      else if (c >= 0x20)        { out[i++] = (char)c; }
   }
   out[i] = 0;
}

// pull a nested string from a videoRenderer sub-object: find `container` (e.g. "\"ownerText\""), then read
// `key` (e.g. "text" for a runs array, "simpleText" for a plain value) within the block. Missing = left empty.
static void readField(const char *block, const char *end, const char *container, const char *key, char *out, int cap)
{
   const char *pos = strstr(block, container);
   // every field read here is text on screen (title, channel, duration, date): a cut one says so
   if (pos && pos < end && jsonString(pos, end, key, out, cap) == 2) markCut(out, cap);
}

// the next feed item at or after `from`. search uses "videoRenderer", trending shelves "gridVideoRenderer",
// and channel pages the newer "lockupViewModel". returns the nearest of the three (NULL if none).
static const char *nextItemBlock(const char *from)
{
   static const char *keys[] = { "\"videoRenderer\"", "\"gridVideoRenderer\"", "\"lockupViewModel\"" };
   const char *best = NULL;
   for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
      const char *hit = strstr(from, keys[i]);
      if (hit && (!best || hit < best)) best = hit;
   }
   return best;
}

static int isDuplicate(const SearchResults *out, const char *videoId)
{
   for (int i = 0; i < out->count; i++) if (strcmp(out->items[i].videoId, videoId) == 0) return 1;
   return 0;
}

// copy the first "content":"..." value within [from,end) whose text ends with `suffix` (e.g. " views",
// " ago"). lockupViewModel metadata rows are plain content strings, not keyed by field.
static void readContentEndingWith(const char *from, const char *end, const char *suffix, char *out, int cap)
{
   int suffixLen = (int)strlen(suffix);
   const char *p = from;
   while ((p = strstr(p, "\"content\":\"")) != NULL && p + 11 <= end) {
      p += 11;                                       // past "content":"
      const char *close = memchr(p, '"', end - p);
      if (!close) break;
      int length = (int)(close - p);
      if (length >= suffixLen && length < cap && strncmp(close - suffixLen, suffix, suffixLen) == 0) {
         memcpy(out, p, length); out[length] = 0; return;
      }
      p = close + 1;
   }
}

// the owning channel's UC id: the first "browseId":"UC..." in the item block (the video owner for a
// videoRenderer, the author for a lockup). left empty if absent.
static void readChannelId(const char *block, const char *end, char *out, int cap)
{
   const char *p = strstr(block, "\"browseId\":\"UC");
   if (p && p < end) jsonString(p, end, "browseId", out, cap);
   else if (cap) out[0] = 0;
}

// One past the closing brace of the JSON object whose '{' is at `open`, or `end` when it does not close before
// it. Strings are skipped whole, escapes included, so a brace inside a title cannot end the object early.
static const char *objectEnd(const char *open, const char *end)
{
   int depth = 0, inString = 0;
   for (const char *p = open; p < end; p++) {
      char c = *p;
      if (inString) {
         if (c == '\\') p++;             // the escaped character, whatever it is
         else if (c == '"') inString = 0;
      }
      else if (c == '"') inString = 1;
      else if (c == '{') depth++;
      else if (c == '}' && --depth == 0) return p + 1;
   }
   return end;
}

// The view count, read only inside the item's own "viewCountText" object. A video gives it as one simpleText
// ("6,100 views"); a live stream gives it as runs ("52,106" + " watching"), which used to leave the field empty
// - or the scan ran on past the field and took a later simpleText such as the overlay's "PREMIERE".
static void readViewCount(const char *block, const char *end, char *out, int cap)
{
   out[0] = 0;
   const char *field = findIn(block, end, "\"viewCountText\":{");   // a null count has no object at all
   if (!field) return;
   const char *fieldEnd = objectEnd(field + 16, end);              // 16 = strlen("\"viewCountText\":")
   if (findIn(field, fieldEnd, "\"simpleText\":\"")) { jsonString(field, fieldEnd, "simpleText", out, cap); return; }

   int length = 0;
   for (const char *run = field; (run = findIn(run, fieldEnd, "\"text\":\"")) != NULL; run += 8) {
      char part[24];
      jsonString(run, fieldEnd, "text", part, sizeof part);
      int partLength = (int)strlen(part), room = cap - 1 - length, cut = partLength > room;
      if (cut) {   // keep what fits, and never half a character
         partLength = room;
         while (partLength > 0 && ((unsigned char)part[partLength] & 0xC0) == 0x80) partLength--;
      }
      memcpy(out + length, part, partLength);
      length += partLength;
      out[length] = 0;
      if (cut) break;
   }
}

// fill one result from a videoRenderer (search) or gridVideoRenderer (trending) block. 1 if it has a title.
static int parseRendererItem(const char *block, const char *end, SearchResult *item)
{
   if (!jsonString(block, end, "videoId", item->videoId, sizeof item->videoId) || !item->videoId[0]) return 0;
   readChannelId(block, end, item->channelId, sizeof item->channelId);
   // (Test 15) the channel's picture, where the item carries one (search, Live): for the cards, the hero and the list
   const char *avatar = findIn(block, end, "\"channelThumbnailSupportedRenderers\":{");
   if (avatar) keepChannelAvatar(item->channelId, avatar, end);
   readField(block, end, "\"title\"", "text", item->title, sizeof item->title);
   if (!item->title[0])   // gridVideoRenderer (trending) uses a plain simpleText title
      readField(block, end, "\"title\"", "simpleText", item->title, sizeof item->title);
   readField(block, end, "\"lengthText\"",        "simpleText", item->duration,  sizeof item->duration);
   // A trending item (gridVideoRenderer) has no lengthText: its duration is only the thumbnail's time overlay, so
   // the Trending cards showed none. It is taken from there when the overlay is a plain duration ("style":
   // "DEFAULT"), never from a LIVE or UPCOMING one, whose text is a word rather than a time.
   if (!item->duration[0]) {
      static const char OVERLAY[] = "\"thumbnailOverlayTimeStatusRenderer\":{";
      const char *overlay = findIn(block, end, OVERLAY);
      if (overlay) {
         const char *open  = overlay + sizeof OVERLAY - 2;   // its '{'
         const char *close = objectEnd(open, end);
         if (findIn(open, close, "\"style\":\"DEFAULT\""))
            jsonString(open, close, "simpleText", item->duration, sizeof item->duration);
      }
   }
   readField(block, end, "\"ownerText\"",         "text",       item->author,    sizeof item->author);
   if (!item->author[0])   // trending items carry the channel in shortBylineText instead
      readField(block, end, "\"shortBylineText\"", "text", item->author, sizeof item->author);
   readViewCount(block, end, item->views, sizeof item->views);
   readField(block, end, "\"publishedTimeText\"", "simpleText", item->published, sizeof item->published);
   // browse surfaces mark a live stream with the time-status overlay, search and the Gaming live tab only with
   // a badge. Bounded to the item: an unbounded search ran on through the rest of the reply for every video.
   item->isLive = findIn(block, end, "\"style\":\"LIVE\"") != NULL ||
                  findIn(block, end, "\"style\":\"BADGE_STYLE_TYPE_LIVE_NOW\"") != NULL;
   return item->title[0] != 0;
}

// fill one result from a lockupViewModel VIDEO block (youtube's newer feed-item format, seen on channel
// pages and other surfaces). 1 if it's a playable video with a title; albums/playlists (other contentTypes) are skipped.
static int parseLockupItem(const char *block, const char *end, SearchResult *item)
{
   const char *videoType = strstr(block, "\"contentType\":\"LOCKUP_CONTENT_TYPE_VIDEO\"");
   if (!videoType || videoType >= end) return 0;
   if (!jsonString(block, end, "contentId", item->videoId, sizeof item->videoId) || !item->videoId[0]) return 0;

   readChannelId(block, end, item->channelId, sizeof item->channelId);
   const char *avatar = findIn(block, end, "\"avatarViewModel\":{");   // (Test 15) its channel's picture
   if (avatar) keepChannelAvatar(item->channelId, avatar, end);
   readField(block, end, "\"lockupMetadataViewModel\"", "content", item->title, sizeof item->title);
   readField(block, end, "\"metadataParts\"",           "content", item->author, sizeof item->author);
   // on a channel's own page the first metadata part is the view count, not the channel name - drop it so
   // the caller can substitute the real channel name (the channel is implied on that page).
   if (strstr(item->author, " views") || strstr(item->author, " watching")) item->author[0] = 0;
   readField(block, end, "\"thumbnailBadgeViewModel\"", "text",    item->duration, sizeof item->duration);
   readContentEndingWith(block, end, " views", item->views, sizeof item->views);
   readContentEndingWith(block, end, " ago",   item->published, sizeof item->published);
   item->isLive = 0;
   return item->title[0] != 0;
}

// scan a WEB innertube response for feed items (search, trending shelves and channel pages use different
// renderers, dispatched per block) and fill out->items. dedupes by videoId - youtube repeats a
// video across shelves.
static void parseVideoResults(const char *resp, int respLen, SearchResults *out)
{
   const char *end = resp + respLen;
   const char *scan = resp;
   while (out->count < MAX_SEARCH_RESULTS) {
      const char *block = nextItemBlock(scan);
      if (!block) break;
      const char *next = nextItemBlock(block + 1);
      const char *blockEnd = (next && next < end) ? next : end;

      SearchResult *item = &out->items[out->count];
      memset(item, 0, sizeof *item);
      int ok = (strncmp(block + 1, "lockupViewModel", 15) == 0)
             ? parseLockupItem(block, blockEnd, item)
             : parseRendererItem(block, blockEnd, item);
      // drop members-only videos: they resolve to UNPLAYABLE for an anonymous session (no stream, no author)
      const char *membersBadge = strstr(block, "Members only");
      if (membersBadge && membersBadge < blockEnd) ok = 0;
      if (ok && !isDuplicate(out, item->videoId)) out->count++;
      scan = blockEnd;
   }

   // the "load more" token for the next page. anchor on the OBJECT ("...":{) not the bare name - youtube's
   // response carries a schema list of all renderer names ("...","...") and the bare string matches that
   // first, yielding a garbage token. the real continuationItemRenderer holds continuationCommand > token.
   const char *more = strstr(resp, "\"continuationItemRenderer\":{");
   if (more && more < end) jsonString(more, end, "token", out->continuation, sizeof out->continuation);

   // channel feeds carry the channel's own title in metadata; used to label videos on a channel page that
   // omit their author (the channel is implied). absent in search/trending responses (left empty). anchor on
   // the OBJECT ("...":{) - the bare name appears first in youtube's schema list, and the title after it is a
   // tab ("Home"), not the channel name.
   const char *channelMeta = strstr(resp, "\"channelMetadataRenderer\":{");
   if (channelMeta && channelMeta < end) {
      jsonString(channelMeta, end, "title", out->channelName, sizeof out->channelName);
      // (V3.0.0) and its picture, for the channel's tile on Subscriptions: the renderer's "avatar", by its "externalId"
      char channelId[32];
      const char *metaEnd = objectEnd(channelMeta + sizeof "\"channelMetadataRenderer\":{" - 2, end);
      if (metaEnd && jsonString(channelMeta, metaEnd, "externalId", channelId, sizeof channelId)) {
         const char *avatar = findIn(channelMeta, metaEnd, "\"avatar\":{");
         if (avatar) keepChannelAvatar(channelId, avatar, metaEnd);
      }
   }

   // channel Videos feeds sort by Latest, Popular or Oldest, each with a continuation token the screen re-browses
   // with. YouTube used to send them as three chips (chipViewModel with the text, the token after it); it now sends
   // ONE drop-down chip, "Latest", whose sheet lists the three as listItemViewModel entries, each with its token -
   // so only Latest was found, and Popular and Oldest loaded the plain first page: the Latest list again (seen in
   // RPCS3, Nov 2026; the sheet's tokens answer with their own order, no session needed). The sheet is read first,
   // from the drop-down chip on, and the old chips stay as the fallback. Absent in search/trending (left empty).
   static const char *SORT_CHIP_LABELS[3] = { "Latest", "Popular", "Oldest" };
   const char *sheet = strstr(resp, "\"CHIP_VIEW_MODEL_DISPLAY_TYPE_DROP_DOWN\"");
   for (int chip = 0; chip < 3; chip++) {
      char pat[64];
      const char *found = NULL;
      if (sheet && sheet < end) {
         snprintf(pat, sizeof pat, "\"listItemViewModel\":{\"title\":{\"content\":\"%s\"", SORT_CHIP_LABELS[chip]);
         found = strstr(sheet, pat);
      }
      if (!found || found >= end) {
         snprintf(pat, sizeof pat, "\"chipViewModel\":{\"text\":\"%s\"", SORT_CHIP_LABELS[chip]);
         found = strstr(resp, pat);
      }
      if (found && found < end) jsonString(found, end, "token", out->sortTokens[chip], MAX_SORT_TOKEN);
   }
}

// YouTube names the country it built a page 1 for in its top bar ("countryCode":"DE", near the end of the reply)
// and names none for the United States. A code it does not take gets HTTP 200, an empty page and no country,
// which in the grid looks exactly like an empty list - so the log says which region YouTube really used.
// Continuation replies have no top bar and are not checked. One line whenever the answer changes, not one per
// page. The list workers run one at a time; were two ever to overlap, a log line would repeat or go missing.
static void logRegionEcho(const char *resp, const char *end, const char *sent, int itemCount)
{
   static char loggedSent[4], loggedUsed[8];
   static int  loggedSuspect = -1;

   char named[8] = "";
   const char *badge = strstr(resp, "\"countryCode\":\"");
   if (badge && badge < end) jsonString(badge, end, "countryCode", named, sizeof named);
   int isUs = strcmp(sent, "US") == 0;
   const char *used = named[0] ? named : isUs ? "US" : "none";   // no badge is the normal answer for the US
   int mismatch = named[0] && strcmp(named, sent) != 0;
   int suspect = !named[0] && !isUs && itemCount == 0;

   if (strcmp(loggedSent, sent) == 0 && strcmp(loggedUsed, used) == 0 && loggedSuspect == suspect) return;
   strCopy(loggedSent, sizeof loggedSent, sent);
   strCopy(loggedUsed, sizeof loggedUsed, used);
   loggedSuspect = suspect;
   if (mismatch)     logWarn("[yt] region: sent %s, YouTube used %s\n", sent, used);
   else if (suspect) logWarn("[yt] region: sent %s, YouTube used none and sent no items - it may not take this code\n", sent);
   else              logInfo("[yt] region: sent %s, YouTube used %s\n", sent, used);
}

// a character of a session as YouTube hands it out: base64, url-safe or not
static int isSessionChar(char c)
{
   return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '-' || c == '_' || c == '+' || c == '/' || c == '=';
}

// A whole session, and nothing else. It goes out again as a header line, so a malformed reply must never be able
// to smuggle a CR or LF into the next request.
static int isSessionToken(const char *text)
{
   int length = 0;
   while (isSessionChar(text[length])) length++;
   return length > 0 && text[length] == 0;
}

// The reply's own session: its first "visitorData", which is responseContext's (it leads every reply), percent-
// decoded as ensureVisitorData does. "" when there is none, or nothing that passes for one whole.
static void captureSession(const char *resp, const char *end, char *out, int cap)
{
   char raw[128];
   out[0] = 0;
   if (!jsonString(resp, end, "visitorData", raw, sizeof raw) || (int)strlen(raw) >= (int)sizeof raw - 1) return;
   // as it comes, percent-encoded ("%3D"): nothing but session characters and '%', so that urlDecode - which stops
   // at a blank, '?' or CR - reads it whole instead of leaving a cut-off start that would pass the check below
   for (const char *c = raw; *c; c++) if (!isSessionChar(*c) && *c != '%') return;
   if (urlDecode(raw, out, cap) < 0 || (int)strlen(out) >= cap - 1 || !isSessionToken(out)) out[0] = 0;
}

// What a WEB list request needs beyond its body. Search, Trending and channel pages use none of it (postAndParse);
// the Live page uses the session parts and the append.
typedef struct {
   const char *pageOneGl;      // the region a page-1 body was built with, checked against the reply (logRegionEcho);
                               // NULL for a continuation, whose reply names no country
   const char *visitorId;      // sent as X-Goog-Visitor-Id when set: a session that passed isSessionToken
   char       *replyVisitor;   // receives the reply's own session (captureSession); NULL = not wanted
   int         replyVisitorCap;
   int         append;         // add the reply's items to out's (deduped by videoId) instead of starting afresh. out
                               // is then left alone unless an answer comes, whose next-page token replaces out's
} WebRequest;

// POST a WEB innertube endpoint with a prebuilt json body, then parse videoRenderers from the response.
static int postAndParseWith(const char *url, const char *body, int bodyLen, const WebRequest *request, SearchResults *out)
{
   if (!request->append) memset(out, 0, sizeof *out);
   if (request->replyVisitor) request->replyVisitor[0] = 0;
   char *resp = malloc(RESP_CAP);
   if (!resp) return -1;

   HttpHeader headers[7] = {   // WEB_UA is a pointer, so these can't be a file-scope const array
      { "Content-Type", "application/json" },
      { "User-Agent", WEB_UA },
      { "X-YouTube-Client-Name", "1" },
      { "X-YouTube-Client-Version", WEB_VERSION },
      { "Origin", "https://www.youtube.com" },
      { "Accept-Encoding", "identity" },
   };
   int headerCount = 6;
   if (request->visitorId && request->visitorId[0]) {   // last, as it was sent when the echo was probed
      headers[6].name = "X-Goog-Visitor-Id";
      headers[6].value = request->visitorId;
      headerCount = 7;
   }

   int respLen = 0, status = 0;
   int rc = fetchHttp("POST", url, headers, headerCount, body, bodyLen, resp, RESP_CAP, &respLen, &status);
   if (rc < 0)        { free(resp); return rc; }
   if (status != 200) { free(resp); return -status; }

   int before = out->count;
   if (request->append) out->continuation[0] = 0;   // the next page is the one this reply names, or none
   parseVideoResults(resp, respLen, out);
   if (request->pageOneGl) logRegionEcho(resp, resp + respLen, request->pageOneGl, out->count - before);
   if (request->replyVisitor) captureSession(resp, resp + respLen, request->replyVisitor, request->replyVisitorCap);
   free(resp);
   return 0;
}

// pageOneGl as in WebRequest.
static int postAndParse(const char *url, const char *body, int bodyLen, const char *pageOneGl, SearchResults *out)
{
   WebRequest request = { pageOneGl, NULL, NULL, 0, 0 };
   return postAndParseWith(url, body, bodyLen, &request, out);
}

// snprintf answers with the length it WOULD have written, and the body is sent bodyLen bytes long: one that did
// not fit is refused rather than sent cut short (or read past its buffer).
static int refuseBody(int bodyLen, SearchResults *out)
{
   logError("[yt] request body of %d bytes does not fit its buffer, not sent\n", bodyLen);
   memset(out, 0, sizeof *out);
   return -1;
}

static int search(const char *query, SortOrder sort, const char *continuation, SearchResults *out)
{
   const char *gl = getContentRegionCode();   // read once: the whole request uses this one value
   char body[MAX_CONTINUATION + 512];
   int bodyLen;
   if (continuation) bodyLen = snprintf(body, sizeof body, CONTINUATION_BODY_FMT, gl, continuation);
   else {
      char escaped[256];
      jsonEscape(query, escaped, sizeof escaped);
      if (sort < 0 || sort >= SORT_COUNT) sort = SORT_RELEVANCE;
      bodyLen = snprintf(body, sizeof body, SEARCH_BODY_FMT, gl, escaped, SEARCH_SORT_PARAMS[sort]);
   }
   if (bodyLen < 0 || bodyLen >= (int)sizeof body) return refuseBody(bodyLen, out);
   return postAndParse(SEARCH_URL, body, bodyLen, continuation ? NULL : gl, out);
}

// the surviving per-category channel-tab feeds, each a real feed logged-out. order matches TrendingCategory.
static const struct { const char *browseId, *params; } TRENDING_FEEDS[TREND_COUNT] = {
   { "UCOpNcN46UbXVtpKMrmU4Abg", "Egh0cmVuZGluZw%3D%3D" },              // TREND_GAMING
   { "UCEgdi0XIXXZ-qJOFPf4JSKw", "EglzcG9ydHN0YWK4AQCSAwDyBgQKAjIA" },  // TREND_SPORTS
   { "FEpodcasts_destination",   "qgcCCAM%3D" },                        // TREND_PODCASTS
};

static int fetchTrending(TrendingCategory category, const char *continuation, SearchResults *out)
{
   if (category < 0 || category >= TREND_COUNT) return -1;
   const char *gl = getContentRegionCode();
   char body[MAX_CONTINUATION + 512];
   int bodyLen;
   if (continuation) bodyLen = snprintf(body, sizeof body, CONTINUATION_BODY_FMT, gl, continuation);
   else bodyLen = snprintf(body, sizeof body, TRENDING_BODY_FMT, gl, TRENDING_FEEDS[category].browseId, TRENDING_FEEDS[category].params);
   if (bodyLen < 0 || bodyLen >= (int)sizeof body) return refuseBody(bodyLen, out);
   return postAndParse(BROWSE_URL, body, bodyLen, continuation ? NULL : gl, out);
}

/* ---- Neon Lounge public API (youtube.h) ---------------------------------- */

static char yt_dir[256] = "/dev_hdd0/game/SVIZ00001/USRDIR";

void yt_set_data_dir(const char *dir)
{
   if (!dir) return;
   strCopy(yt_dir, sizeof yt_dir, dir);
   int n = (int)strlen(yt_dir);
   while (n > 1 && yt_dir[n - 1] == '/') yt_dir[--n] = 0;   /* visitorCachePath adds "/visitor.txt" */
}

const char *yt_data_dir(void) { return yt_dir; }

int yt_search(const char *query, SearchResults *out)
{
   return search(query, SORT_RELEVANCE, NULL, out);
}

int yt_trending(int category, SearchResults *out)
{
   return fetchTrending((TrendingCategory)category, NULL, out);
}

int yt_extract(const char *input, StreamInfo *out)
{
   return extract(input, out);
}

/* the Settings escape hatch: forget the saved anonymous session, on disk
 * and in memory, so the next resolve mints a fresh one */
void yt_reset_visitor(void)
{
   dropVisitorData();
}

int yt_visitor_persisted(void)
{
   char path[320];
   snprintf(path, sizeof path, "%s/visitor.txt", yt_dir);
   FILE *f = fopen(path, "rb");
   if (!f) return 0;
   fclose(f);
   return 1;
}

/* ---- host-test exports: the pure parse halves of search/extract --------- */

int yt_parse_results(const char *resp, int respLen, SearchResults *out)
{
   memset(out, 0, sizeof *out);
   parseVideoResults(resp, respLen, out);
   return out->count;
}

/* the parse half of extract(): metadata, captions and the format ladder.
 * Live-manifest/seqnum fetching is part of extract() proper and is not
 * repeated here; isLive/isPostLive are still reported. */
int yt_parse_player(const char *resp, int respLen, StreamInfo *out)
{
   memset(out, 0, sizeof *out);
   const char *end = resp + respLen;
   const char *details = strstr(resp, "\"videoDetails\"");
   jsonString(details ? details : resp, end, "title", out->title, sizeof out->title);
   jsonString(details ? details : resp, end, "shortDescription", out->description, sizeof out->description);
   char lenText[16];
   if (jsonString(details ? details : resp, end, "lengthSeconds", lenText, sizeof lenText))
      out->durationSeconds = atoi(lenText);
   parseCaptionTracks(resp, end, out);
   parseFormats(resp, end, out);
   out->isLive = strstr(resp, "\"isLive\":true") != NULL;
   out->isPostLive = !out->isLive && strstr(resp, "\"isPostLiveDvr\":true") != NULL;
   return out->formatCount > 0 ? 0 : -1;
}

/* visitor lifecycle, exercised directly by the host tests */
int yt_visitor_store(const char *data)
{
   if (!data || !*data) return -1;
   strCopy(visitorData, sizeof visitorData, data);
   saveVisitorData();
   visitorFromDisk = 1;
   return 0;
}

int yt_visitor_load(char *out, int cap)
{
   if (!loadVisitorData()) return 0;
   if (out && cap) strCopy(out, cap, visitorData);
   return 1;
}
