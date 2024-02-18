static const char *font[] = { "Liberation Sans:size=12" };

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
#define BORDER_WIDTH 3

/* Reshape will use this width and/or height (pixels) if trying to resize to
 * less than it, thus preventing resizing to dumb window sizes. */
#define MIN_WINDOW_SIZE 20

/* Program to spawn on "new". */
#define TERMINAL "alacritty"

/* This sets the actual menu length. 'm' is usually a quite wide character
 * even for variable fonts so you don't need much to get your desired
 * length. */
#define MENU_LENGTH "mmmmmmm"

/* There are two keybinds and both need the modmask. */
#define MODMASK (Mod4Mask)

#define FULLSCREEN_KEY XK_f
#define RESHAPE_KEY XK_r

/* These are the only needed keybinds because:
 * 1- Fullscreen is very desirable nowadays and you need a keybind for that
 * (it's impossible to access the menu when a window is fullscreen)
 * 2- Sometimes windows may accidentally hide the entire root and you need
 * to reshape them to access the menu. */
