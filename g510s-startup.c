/*
 *  Startup animation for g510s LCD (160×43 pixels).
 *
 *  startup_animation() spawns a background thread and returns immediately
 *  so main init continues in parallel.  The animation thread sets
 *  startup_anim_done=1 when finished; update_function() waits for that flag
 *  before rendering normal screens.
 *
 *  All drawing uses libg15render primitives — no text functions.
 */

#define LOG_MODULE "startup"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <libg15.h>
#include <libg15render.h>

#include "g510s.h"
#include "g510s-startup.h"


/* ── 5×7 pixel font ──────────────────────────────────────────────────────────
 * One entry per needed glyph.  Each row byte: bit 4 = leftmost pixel.
 */
static const struct { char ch; unsigned char rows[7]; } FONT5X7[] = {
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'0', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
};
#define FONT_N ((int)(sizeof(FONT5X7) / sizeof(FONT5X7[0])))

/* ── glyph rendering ─────────────────────────────────────────────────────── */

#define SCALE 2
#define CW    (5 * SCALE)   /* 10 px per glyph */
#define CH    (7 * SCALE)   /* 14 px per glyph */
#define CSP   SCALE         /*  2 px inter-char gap */
#define CSTEP (CW + CSP)    /* 12 px per character slot */

static void draw_glyph(g15canvas *c, const unsigned char rows[7], int x, int y)
{
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            if (rows[row] & (0x10 >> col))
                for (int sy = 0; sy < SCALE; sy++)
                    for (int sx = 0; sx < SCALE; sx++)
                        g15r_setPixel(c, x + col*SCALE + sx, y + row*SCALE + sy, 1);
}

static void draw_char(g15canvas *c, char ch, int x, int y)
{
    for (int i = 0; i < FONT_N; i++)
        if (FONT5X7[i].ch == ch) { draw_glyph(c, FONT5X7[i].rows, x, y); return; }
}

/* ── logo layout ─────────────────────────────────────────────────────────────
 *  "LOGITECH"  8 chars → w=94  x1=33  y1=5   cy1=12
 *  "G510"      4 chars → w=46  x2=57  y2=23  cy2=30
 */
#define ROW1      "LOGITECH"
#define ROW1_N    8
#define ROW1_X    33
#define ROW1_Y    5
#define ROW1_CY   (ROW1_Y + CH/2)   /* 12 */

#define ROW2      "G510"
#define ROW2_N    4
#define ROW2_X    57
#define ROW2_Y    23
#define ROW2_CY   (ROW2_Y + CH/2)   /* 30 */

/* draw one row; eaten[i] != 0 → skip that char */
static void draw_row(g15canvas *c,
                     const char *str, int x_start, int y,
                     const int *eaten, int n)
{
    for (int i = 0; i < n; i++)
        if (!eaten[i])
            draw_char(c, str[i], x_start + i * CSTEP, y);
}

static void draw_logo(g15canvas *c)
{
    int eaten_none[8] = {0};
    draw_row(c, ROW1, ROW1_X, ROW1_Y, eaten_none, ROW1_N);
    draw_row(c, ROW2, ROW2_X, ROW2_Y, eaten_none, ROW2_N);
}


/* ── Pac-Man (eating animation, r=10) ────────────────────────────────────────
 *  dir= 1 → mouth faces right
 *  dir=-1 → mouth faces left
 *  Mouth is open when (frame/3) is odd.
 */
#define PAC_R     10
#define PAC_SPEED 35     /* px per frame; 35 → ~5 frames per 180px row, no overlap → no ghosting */
#define FRAME_US  42000  /* 42 ms × 2 sleeps/frame = 83 ms/frame ≈ 12 fps */

static void draw_pacman(g15canvas *c, int cx, int cy, int dir, int frame)
{
    int r = PAC_R;
    g15r_drawCircle(c, cx, cy, r, 1, 1);

    if ((frame / 3) & 1)   /* mouth open on odd 3-frame groups */
        for (int dy = -r; dy <= r; dy++)
            for (int dx = 0; dx <= r; dx++)
                if (dx*dx + dy*dy <= r*r && dy*dy <= dx*dx)
                    g15r_setPixel(c, cx + dir * dx, cy + dy, 0);
}


/* ── Ghost ───────────────────────────────────────────────────────────────────
 *  cx=80, r=9
 *  dome circle at (80,17) r=9 → top y=8
 *  body box (71,17)..(89,28)
 *  3 triangular teeth at y=28..30
 *  white eyes at (75,17) and (85,17) r=3, 2×2 black pupils
 */
static void draw_ghost(g15canvas *c)
{
    int cx = G15_LCD_WIDTH / 2;
    int r  = 9, dome_cy = 17, body_bot = 28;

    g15r_drawCircle(c, cx, dome_cy, r, 1, 1);
    g15r_pixelBox(c, cx-r, dome_cy, cx+r, body_bot, 1, 1, 1);

    /* teeth: margin(1) tooth(5) gap(1) tooth(5) gap(1) tooth(5) margin(1) = 19 */
    static const int tx[3] = {72, 78, 84};
    for (int t = 0; t < 3; t++)
        for (int dy = 0; dy < 3; dy++)
            for (int px = tx[t]+dy; px <= tx[t]+4-dy; px++)
                g15r_setPixel(c, px, body_bot+dy, 1);

    /* eyes */
    int ey[2] = {75, 85};
    for (int e = 0; e < 2; e++) {
        g15r_drawCircle(c, ey[e], dome_cy, 3, 1, 0);
        g15r_setPixel(c, ey[e],   dome_cy,   1);
        g15r_setPixel(c, ey[e]+1, dome_cy,   1);
        g15r_setPixel(c, ey[e],   dome_cy-1, 1);
        g15r_setPixel(c, ey[e]+1, dome_cy-1, 1);
    }
}


/* ── LCD write helper ────────────────────────────────────────────────────── *
 * Copy frame into the shared buffer and raise anim_frame_ready.
 * update_function polls the flag and does the actual writePixmapToLCD under
 * libg15_mutex — this avoids competing with key_function for libusb_mutex
 * (which it holds almost continuously via getPressedKeys tight-loop).
 */
static void lcd_show(g15canvas *c)
{
    memcpy(anim_lcd_buf, c->buffer, sizeof(anim_lcd_buf));
    anim_frame_ready = 1;
    usleep(FRAME_US);
}


/* ── eating animation ────────────────────────────────────────────────────── */

static void eat_animation(g15canvas *c)
{
    int eaten1[ROW1_N] = {0};
    int eaten2[ROW2_N] = {0};

    /* ── Row 1: Pac-Man left → right ── */
    int frame = 0;
    for (int px = -PAC_R; px <= G15_LCD_WIDTH + PAC_R && !leaving; px += PAC_SPEED, frame++) {
        /* eat chars whose center Pac-Man centre has passed */
        for (int i = 0; i < ROW1_N; i++) {
            int cx_ch = ROW1_X + i * CSTEP + CW/2;
            if (px >= cx_ch) eaten1[i] = 1;
        }

        g15r_initCanvas(c);
        draw_row(c, ROW1, ROW1_X, ROW1_Y, eaten1, ROW1_N);
        draw_row(c, ROW2, ROW2_X, ROW2_Y, eaten2, ROW2_N);
        draw_pacman(c, px, ROW1_CY, 1, frame);
        lcd_show(c);
        usleep(FRAME_US);
    }

    /* brief pause — Pac-Man "going around the back of the screen" */
    for (int p = 0; p < 5 && !leaving; p++) {
        g15r_initCanvas(c);
        draw_row(c, ROW2, ROW2_X, ROW2_Y, eaten2, ROW2_N);
        lcd_show(c);
        usleep(FRAME_US);
    }

    /* ── Row 2: Pac-Man right → left ── */
    frame = 0;
    for (int px = G15_LCD_WIDTH + PAC_R; px >= -PAC_R && !leaving; px -= PAC_SPEED, frame++) {
        for (int i = 0; i < ROW2_N; i++) {
            int cx_ch = ROW2_X + i * CSTEP + CW/2;
            if (px <= cx_ch) eaten2[i] = 1;
        }

        g15r_initCanvas(c);
        draw_row(c, ROW2, ROW2_X, ROW2_Y, eaten2, ROW2_N);
        draw_pacman(c, px, ROW2_CY, -1, frame);
        lcd_show(c);
        usleep(FRAME_US);
    }
}


/* ── background animation thread ────────────────────────────────────────── */

static void *anim_thread(void *arg)
{
    (void)arg;
    g15canvas *c = malloc(sizeof(g15canvas));
    if (!c) {
        LERROR("anim_thread: malloc failed");
        startup_anim_done = 1;
        return NULL;
    }

    LINFO("animation thread start");

    /* logo static */
    g15r_initCanvas(c);
    draw_logo(c);
    lcd_show(c);
    usleep(700000);

    /* eating */
    if (!leaving)
        eat_animation(c);

    /* ghost */
    if (!leaving) {
        g15r_initCanvas(c);
        draw_ghost(c);
        lcd_show(c);
        usleep(500000);
    }

    free(c);
    LINFO("animation thread done");
    startup_anim_done = 1;
    return NULL;
}


/* ── public entry point ──────────────────────────────────────────────────── */

void startup_animation(void)
{
    int r1 = setLEDs(G15_LED_M1);
    int r2 = setG510LEDColor(255, 255, 255);
    LINFO("startup backlight: setLEDs=%d setG510LEDColor=%d", r1, r2);

    pthread_t tid;
    if (pthread_create(&tid, NULL, anim_thread, NULL) != 0) {
        LERROR("failed to create animation thread");
        startup_anim_done = 1;
        return;
    }
    pthread_detach(tid);
}
