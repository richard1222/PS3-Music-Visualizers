#ifndef NL_EQ_H
#define NL_EQ_H
#include <stddef.h>
#include <stdint.h>
/* Five-band equalizer for Neon Lounge's single shared PCM path.
 *
 * Every source (MP3 via minimp3, PC Audio, YouTube via FAAD2) funnels
 * through main.c's SDL audio callback, which calls eq_process() on the
 * decoded interleaved S16 samples after the playback-volume gain. One
 * hook therefore equalizes the whole app.
 *
 * Bands: 100 Hz low shelf, 400 Hz / 1 kHz / 4 kHz peaking (Q = 1),
 * 12 kHz high shelf, RBJ audio EQ cookbook coefficients, direct
 * form II transposed. Gains are whole dB in [EQ_GAIN_MIN, EQ_GAIN_MAX]
 * so the settings UI can step them one dB at a time. When any band is
 * boosted, an automatic headroom preamp of -max_boost dB is applied so
 * loud passages do not clip; cuts never change overall level. A fully
 * flat EQ is a bit-exact pass-through (the filters are skipped).
 *
 * Threading: gains are written by the UI thread (eq_set_gain /
 * eq_apply_preset) and read by the audio thread (eq_process), the same
 * lock-free pattern main.c already uses for its plain-int rate and
 * channels state. Coefficient rebuilds happen inside eq_process when
 * it notices the gains or the sample rate changed; filter state is
 * kept across rebuilds so retuning never clicks from a state reset.
 * No SDL dependency: this file is host-testable (tests/test_eq.c). */
#define EQ_BANDS 5
#define EQ_GAIN_MIN (-12)
#define EQ_GAIN_MAX 12
#define EQ_PRESET_COUNT 6
#define EQ_PRESET_CUSTOM (-1)
/* v1.18: three user preset slots ("Custom 1".."Custom 3"). A slot is
 * captured from the live gains with eq_save_user_preset(), applied
 * with eq_apply_user_preset(), and persisted by main.c in eq.txt v2
 * (one "slot <n> g0 g1 g2 g3 g4" line per set slot, after the legacy
 * first line - see eq_format_user_slot / eq_parse_user_slot). The
 * unified preset cycle in the UI runs built-ins 0..5 then user slots
 * as indices EQ_PRESET_COUNT..EQ_PRESET_TOTAL-1. */
#define EQ_USER_PRESET_COUNT 3
#define EQ_PRESET_TOTAL (EQ_PRESET_COUNT + EQ_USER_PRESET_COUNT)
extern const int eq_band_freq[EQ_BANDS];
extern const char *const eq_band_name[EQ_BANDS];
extern const char *const eq_preset_name[EQ_PRESET_COUNT];
extern const char *const eq_user_preset_name[EQ_USER_PRESET_COUNT];
void eq_init(void);
void eq_set_gain(int band,int db);
int eq_gain(int band);
void eq_apply_preset(int preset);
int eq_match_preset(void);
int eq_match_user_preset(void);
void eq_apply_user_preset(int slot);
void eq_save_user_preset(int slot);
void eq_user_preset_set(int slot,const int gains_in[EQ_BANDS]);
int eq_user_preset_is_set(int slot);
int eq_user_preset_last(void);
int eq_format_user_slot(char *buf,size_t size,int slot);
int eq_parse_user_slot(const char *line,int *slot_out,int gains_out[EQ_BANDS]);
int eq_is_flat(void);
void eq_process(int16_t *samples,size_t count,int channels,int sample_rate);
#endif
