#ifndef NL_SETTINGS_LAYOUT_H
#define NL_SETTINGS_LAYOUT_H
/* Settings screen geometry, 1280x720 logical canvas (ui.c scales all
 * coordinates to the real surface). Factored out of main.c so the host
 * test (tests/test_settings_layout.c) asserts on the exact numbers the
 * console draws with.
 *
 * One uniform scheme for all four rows:
 *   - every row's tile sits on a fixed pitch (SET_PITCH) from SET_TOP;
 *   - a row's label sits inside its tile (SET_LABEL_DY);
 *   - description lines start SET_DESC_DY below the tile top and step
 *     by SET_LINE;
 *   - the volume row's bar sits SET_BAR_DY below its tile top, with
 *     its hint SET_BAR_H + 8 below the bar top.
 *
 * In 1.15 the rows were hand-placed at pitches of 152/90/90 while the
 * updates row's content was 98 tall, so its second description line
 * ("MP3 playback | ...", y=460, baseline 478 with descenders to ~482)
 * overlapped the reset row's tile (top 470, focus glow 467). With the
 * pitch at 110 the tallest row content (updates: 40 + 14 + 26 + 18 =
 * 98) clears the next row's glow by ~5 px, and the reset row's last
 * trailing line (notice, bottom ~596) clears the footer panel
 * (SET_FOOTER_Y) by ~12 px. */
#define SET_X        266
#define SET_W        960
#define SET_TEXT_X   280
#define SET_TOP      138
#define SET_PITCH    110
#define SET_TILE_H   40
#define SET_LABEL_DY 4
#define SET_DESC_DY  54
#define SET_LINE     26
#define SET_BAR_DY   50
#define SET_BAR_H    12
#define SET_BAR_W    660
#define SET_FOOTER_Y 608
#define SET_ROW_Y(i)  (SET_TOP + (i) * SET_PITCH)
#define SET_BAR_Y(i)  (SET_ROW_Y(i) + SET_BAR_DY)
#define SET_HINT_Y(i) (SET_BAR_Y(i) + SET_BAR_H + 8)
#define SET_DESC_Y(i) (SET_ROW_Y(i) + SET_DESC_DY)
#endif
