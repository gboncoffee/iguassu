static const char *font[] = { "Go Mono:size=18" };

/* DRW colors, so hex strings. */
static const char *menu_color[2] = { "#000000", "#eaffea" };
static const char *menu_color_f[2] = { "#eaffea", "#448844" };

/* Hex numbers (RGB). */
#define BORDER_COLOR 0x52aaad
#define MENU_BORDER_COLOR 0x88cb88
#define SWIPE_BORDER_COLOR 0x88cb88
#define SWIPE_BACKGROUND 0xffffff
/* Just comment out this def if you don't want background setting (so you can
 * set a background with other application). */
#define BACKGROUND 0x757373
/* Probably doesn't matter, just put the same as the DRW. */
#define MENU_BACKGROUND_COLOR 0xeaffea
/* Pixels. */
#define BORDER_WIDTH 2

/* Reshape will use this width and/or height (pixels) if trying to resize to
 * less than it, thus preventing resizing to dumb window sizes. */
#define MIN_WINDOW_SIZE 10

/* Program to spawn on "new". */
#define TERMINAL "9term.rc"
