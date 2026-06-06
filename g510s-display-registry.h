/*
 *  g510s-display-registry.h — LCD display descriptor and registry API.
 *
 *  Adding a new display:
 *    1. Define render/settings callbacks in your g510s-<name>.c file.
 *    2. Add one line to the registry[] array in g510s-display-registry.c.
 *    3. Add a DISP_* constant below.
 */

#pragma once

#include <gtk/gtk.h>

/* Forward declaration — full definition is in g510s.h */
struct lcd_s;
typedef struct lcd_s lcd_t;

/* ── Display IDs ──────────────────────────────────────────────────── */
#define DISP_CLOCK   0
#define DISP_CPU     1
#define DISP_SYSMON  2
#define DISP_CLAUDE  3

#define DISPLAY_MAX  16

/* ── Per-display descriptor ───────────────────────────────────────── */
typedef struct {
    int         id;
    const char *name;

    /* State — persisted to ~/.g510s/displays.conf */
    int  enabled;     /* 1 = appears in the LCD rotation              */
    int  order;       /* position in rotation, 0-based                */
    int  is_startup;  /* 1 = shown on launch (only one can be set)    */

    /*
     * Callbacks — set to NULL if not applicable.
     *
     * render_preview : fill lcd->buf with a representative snapshot
     *                  (may use live data; called once on navigation).
     * create_settings: populate 'box' with settings widgets.
     *                  Widgets must stay alive until save_settings().
     * save_settings  : read widget values and apply / store them.
     */
    void (*render_preview)(lcd_t *lcd);
    void (*create_settings)(GtkBox *box);
    void (*save_settings)(void);
} display_entry_t;

/* ── Registry API ─────────────────────────────────────────────────── */

/* Call once at startup, after init_data(). Loads saved state. */
void display_registry_init(void);

/* Number of registered displays. */
int  display_registry_count(void);

/* Return entry at sorted position idx (0 = first in rotation). */
display_entry_t *display_registry_get(int idx);

/* Return entry by its DISP_* id, or NULL. */
display_entry_t *display_registry_by_id(int id);

/* Return the sorted position of the startup display (0 if none set). */
int  display_registry_startup_idx(void);

/* Swap entry at idx with the one at idx-1 / idx+1. */
void display_registry_move_up(int idx);
void display_registry_move_down(int idx);

/* Persist state to ~/.g510s/displays.conf. */
void display_registry_save(void);

/* Load state from ~/.g510s/displays.conf (called by init). */
void display_registry_load(void);

/*
 * Given a DISP_* id, return the DISP_* id of the next enabled display
 * in rotation order.  Falls back to current_id if nothing else is enabled.
 */
int display_registry_next_id(int current_id);
