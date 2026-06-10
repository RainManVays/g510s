/*
 *  g510s-display-registry.c — LCD display registry.
 *
 *  To add a display: add one DISPLAY_ENTRY() line to registry[] and
 *  implement the three callbacks in the corresponding .c file.
 */

#define LOG_MODULE "registry"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g510s.h"
#include "g510s-display-registry.h"

/* Forward declarations for per-display settings callbacks */
void cpu_create_settings(GtkBox *box);
void cpu_save_settings(void);
void sysmon_create_settings(GtkBox *box);
void sysmon_save_settings(void);

/* ── Registry table ───────────────────────────────────────────────
 *
 *  Fields: id, name, enabled, order, is_startup,
 *          render_preview, create_settings, save_settings
 */
#define DISPLAY_ENTRY(id, name, rfn, cfn, sfn) \
    { (id), (name), 1, (id), 0, (rfn), (cfn), (sfn) }

static display_entry_t registry[DISPLAY_MAX] = {
    DISPLAY_ENTRY(DISP_CLOCK,  "Clock",  digital_clock,  NULL,                  NULL),
    DISPLAY_ENTRY(DISP_CPU,    "CPU",    cpu_screen,     cpu_create_settings,   cpu_save_settings),
    DISPLAY_ENTRY(DISP_SYSMON, "Sysmon", sysmon_screen,  sysmon_create_settings,sysmon_save_settings),
    DISPLAY_ENTRY(DISP_CLAUDE, "Claude", claude_screen,  NULL,                  NULL),
};
static int n_displays = 4;

/* Mark the first entry as startup (overridden by load if file exists). */
static void set_default_startup(void) {
    if (n_displays > 0) registry[0].is_startup = 1;
}

/* ── Init ─────────────────────────────────────────────────────────── */
void display_registry_init(void)
{
    set_default_startup();
    display_registry_load();
}

/* ── Accessors ────────────────────────────────────────────────────── */
int display_registry_count(void) { return n_displays; }

display_entry_t *display_registry_get(int idx)
{
    if (idx < 0 || idx >= n_displays) return NULL;
    for (int i = 0; i < n_displays; i++) {
        if (registry[i].order == idx) return &registry[i];
    }
    return NULL;
}

display_entry_t *display_registry_by_id(int id)
{
    for (int i = 0; i < n_displays; i++) {
        if (registry[i].id == id) return &registry[i];
    }
    return NULL;
}

int display_registry_startup_idx(void)
{
    for (int i = 0; i < n_displays; i++) {
        if (registry[i].is_startup) return registry[i].order;
    }
    return 0;
}

/* ── Reordering ───────────────────────────────────────────────────── */
void display_registry_move_up(int idx)
{
    if (idx <= 0 || idx >= n_displays) return;
    display_entry_t *a = display_registry_get(idx);
    display_entry_t *b = display_registry_get(idx - 1);
    if (a && b) { a->order = idx - 1; b->order = idx; }
}

void display_registry_move_down(int idx)
{
    if (idx < 0 || idx >= n_displays - 1) return;
    display_entry_t *a = display_registry_get(idx);
    display_entry_t *b = display_registry_get(idx + 1);
    if (a && b) { a->order = idx + 1; b->order = idx; }
}

/* ── Rotation helper ──────────────────────────────────────────────── */
int display_registry_next_id(int current_id)
{
    display_entry_t *cur = display_registry_by_id(current_id);
    int cur_order = cur ? cur->order : 0;
    switch_log("next_id: cur_id=%d cur_order=%d n_displays=%d",
               current_id, cur_order, n_displays);
    for (int i = 1; i <= n_displays; i++) {
        int candidate_order = (cur_order + i) % n_displays;
        display_entry_t *d = display_registry_get(candidate_order);
        switch_log("  candidate order=%d → %s id=%d enabled=%d",
                   candidate_order,
                   d ? d->name : "(null)",
                   d ? d->id : -1,
                   d ? d->enabled : -1);
        if (d && d->enabled) return d->id;
    }
    switch_log("  no enabled display found, staying at id=%d", current_id);
    return current_id;  /* no other enabled display */
}

/* ── Persistence ──────────────────────────────────────────────────── */
static int config_path(char *buf, size_t size)
{
    const char *home = getenv("HOME");
    if (!home) return -1;
    snprintf(buf, size, "%s/.g510s/displays.conf", home);
    return 0;
}

void display_registry_save(void)
{
    char path[512];
    if (config_path(path, sizeof(path)) < 0) return;

    FILE *f = fopen(path, "w");
    if (!f) { LERROR("could not write %s", path); return; }

    fprintf(f, "# id enabled order startup\n");
    for (int i = 0; i < n_displays; i++) {
        fprintf(f, "%d %d %d %d\n",
                registry[i].id,
                registry[i].enabled,
                registry[i].order,
                registry[i].is_startup);
    }
    fclose(f);
}

void display_registry_load(void)
{
    char path[512];
    if (config_path(path, sizeof(path)) < 0) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* first run — keep defaults */

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int id, enabled, order, startup;
        if (sscanf(line, "%d %d %d %d", &id, &enabled, &order, &startup) != 4)
            continue;
        display_entry_t *d = display_registry_by_id(id);
        if (d) {
            d->enabled    = enabled;
            d->order      = order;
            d->is_startup = startup;
        }
    }
    fclose(f);
}
