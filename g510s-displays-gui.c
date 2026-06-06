/*
 *  g510s-displays-gui.c — GUI for the "Displays" settings tab.
 *
 *  Public API (declared in g510s.h in phase 2):
 *    displays_gui_init(GtkBuilder*)  — call once after gtk_builder_connect_signals
 *    displays_gui_save()             — call from the global Save handler
 */

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>

#include "g510s.h"
#include "g510s-display-registry.h"

/* LCD hardware dimensions for the preview drawing area. */
#define LCD_W  160
#define LCD_H   43
#define PREVIEW_SCALE  3   /* each pixel drawn as 3×3 screen pixels */

/* ── Module-level state ───────────────────────────────────────────── */
static int              s_cur         = 0;     /* current index (sorted order) */
static int              s_busy        = 0;     /* guard against re-entrant signals */
static gboolean         s_initialized = FALSE; /* lazy init guard */
static GtkBox          *s_root        = NULL;  /* box_displays from glade */

static GtkDrawingArea  *s_preview_area;
static GtkLabel        *s_name_label;
static GtkCheckButton  *s_chk_enabled;
static GtkCheckButton  *s_chk_startup;
static GtkStack        *s_settings_stack;

/* ── Internal helpers ─────────────────────────────────────────────── */

/* Update all widgets to reflect the display at s_cur. */
static void refresh_view(void)
{
    int n = display_registry_count();
    if (n == 0) return;

    display_entry_t *d = display_registry_get(s_cur);
    if (!d) return;

    s_busy = 1;

    /* Navigation label */
    char label[80];
    snprintf(label, sizeof(label), "<b>%s</b>   %d / %d",
             d->name, s_cur + 1, n);
    gtk_label_set_markup(s_name_label, label);

    /* Enabled / Startup checks */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s_chk_enabled), d->enabled);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s_chk_startup), d->is_startup);

    /* Settings stack page */
    char page_id[16];
    snprintf(page_id, sizeof(page_id), "%d", d->id);
    gtk_stack_set_visible_child_name(s_settings_stack, page_id);

    s_busy = 0;

    /* Redraw preview (triggers on_preview_draw) */
    gtk_widget_queue_draw(GTK_WIDGET(s_preview_area));
}

/* ── Cairo preview callback ───────────────────────────────────────── */

static gboolean on_preview_draw(GtkWidget *w, cairo_t *cr, gpointer unused)
{
    (void)w; (void)unused;

    display_entry_t *d = display_registry_get(s_cur);

    if (!d || !d->render_preview) {
        /* No preview available: fill with background colour. */
        cairo_set_source_rgb(cr, 0.08, 0.17, 0.08);
        cairo_paint(cr);
        return FALSE;
    }

    /* Render a one-shot snapshot into a temporary lcd_t. */
    lcd_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.max_x = LCD_W;
    tmp.max_y = LCD_H;
    d->render_preview(&tmp);

    /* Paint each pixel scaled up by PREVIEW_SCALE. */
    for (int y = 0; y < LCD_H; y++) {
        for (int x = 0; x < LCD_W; x++) {
            unsigned int poff = (unsigned int)(y * LCD_W + x);
            int byte = (int)(poff / 8);
            int bit  = 7 - (int)(poff % 8);
            int on   = (tmp.buf[byte] >> bit) & 1;

            if (on)
                cairo_set_source_rgb(cr, 0.78, 0.84, 0.00);  /* lit pixel  */
            else
                cairo_set_source_rgb(cr, 0.08, 0.17, 0.08);  /* dark pixel */

            cairo_rectangle(cr,
                            x * PREVIEW_SCALE, y * PREVIEW_SCALE,
                            PREVIEW_SCALE,     PREVIEW_SCALE);
            cairo_fill(cr);
        }
    }
    return FALSE;
}

/* ── Signal handlers ──────────────────────────────────────────────── */

static void on_btn_prev(GtkButton *btn, gpointer unused)
{
    (void)btn; (void)unused;
    int n = display_registry_count();
    s_cur = (s_cur - 1 + n) % n;
    refresh_view();
}

static void on_btn_next(GtkButton *btn, gpointer unused)
{
    (void)btn; (void)unused;
    s_cur = (s_cur + 1) % display_registry_count();
    refresh_view();
}

static void on_chk_enabled(GtkToggleButton *tb, gpointer unused)
{
    (void)unused;
    if (s_busy) return;
    display_entry_t *d = display_registry_get(s_cur);
    if (d) d->enabled = gtk_toggle_button_get_active(tb);
}

static void on_chk_startup(GtkToggleButton *tb, gpointer unused)
{
    (void)unused;
    if (s_busy) return;
    if (!gtk_toggle_button_get_active(tb)) return;  /* ignore unchecks */

    /* Clear startup on all others, set on current. */
    for (int i = 0; i < display_registry_count(); i++) {
        display_entry_t *e = display_registry_get(i);
        if (e) e->is_startup = (i == s_cur);
    }
}

static void on_btn_up(GtkButton *btn, gpointer unused)
{
    (void)btn; (void)unused;
    display_registry_move_up(s_cur);
    if (s_cur > 0) s_cur--;
    refresh_view();
}

static void on_btn_down(GtkButton *btn, gpointer unused)
{
    (void)btn; (void)unused;
    display_registry_move_down(s_cur);
    if (s_cur < display_registry_count() - 1) s_cur++;
    refresh_view();
}

/* ── Public API ───────────────────────────────────────────────────── */

/*
 * displays_gui_save — flush in-memory state to disk.
 * Call this from the global Save (on_menusave_activate) handler.
 */
void displays_gui_save(void)
{
    for (int i = 0; i < display_registry_count(); i++) {
        display_entry_t *e = display_registry_get(i);
        if (e && e->save_settings) e->save_settings();
    }
    display_registry_save();
}

/* ── Lazy initializer ─────────────────────────────────────────────── */

/* Called the first time the Displays tab becomes visible. */
static void displays_gui_do_init(void)
{
    if (s_initialized || !s_root) return;
    s_initialized = TRUE;

    GtkBox *root = s_root;

    /* ── Top-level horizontal split ─────────────────────────────── */
    GtkBox *hbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12));
    gtk_widget_set_margin_start (GTK_WIDGET(hbox), 10);
    gtk_widget_set_margin_end   (GTK_WIDGET(hbox), 10);
    gtk_widget_set_margin_top   (GTK_WIDGET(hbox), 10);
    gtk_widget_set_margin_bottom(GTK_WIDGET(hbox), 10);
    gtk_box_pack_start(root, GTK_WIDGET(hbox), TRUE, TRUE, 0);

    /* ── LEFT column: preview + navigation ──────────────────────── */
    GtkBox *vbox_left = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));
    gtk_box_pack_start(hbox, GTK_WIDGET(vbox_left), FALSE, FALSE, 0);

    /* Preview frame */
    GtkFrame *frm_preview = GTK_FRAME(gtk_frame_new("LCD Preview"));
    gtk_box_pack_start(vbox_left, GTK_WIDGET(frm_preview), FALSE, FALSE, 0);

    GtkDrawingArea *da = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_widget_set_size_request(GTK_WIDGET(da),
                                LCD_W * PREVIEW_SCALE,
                                LCD_H * PREVIEW_SCALE);
    gtk_container_add(GTK_CONTAINER(frm_preview), GTK_WIDGET(da));
    g_signal_connect(da, "draw", G_CALLBACK(on_preview_draw), NULL);
    s_preview_area = da;

    /* Navigation row: [◀]  name  [▶] */
    GtkBox *hbox_nav = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    gtk_widget_set_halign(GTK_WIDGET(hbox_nav), GTK_ALIGN_CENTER);
    gtk_box_pack_start(vbox_left, GTK_WIDGET(hbox_nav), FALSE, FALSE, 0);

    GtkButton *btn_prev = GTK_BUTTON(gtk_button_new_with_label("◀"));
    GtkLabel  *lbl_name = GTK_LABEL(gtk_label_new(""));
    GtkButton *btn_next = GTK_BUTTON(gtk_button_new_with_label("▶"));

    gtk_widget_set_size_request(GTK_WIDGET(lbl_name), LCD_W * PREVIEW_SCALE, -1);
    gtk_label_set_justify(lbl_name, GTK_JUSTIFY_CENTER);

    gtk_box_pack_start(hbox_nav, GTK_WIDGET(btn_prev), FALSE, FALSE, 0);
    gtk_box_pack_start(hbox_nav, GTK_WIDGET(lbl_name), TRUE,  FALSE, 0);
    gtk_box_pack_start(hbox_nav, GTK_WIDGET(btn_next), FALSE, FALSE, 0);

    g_signal_connect(btn_prev, "clicked", G_CALLBACK(on_btn_prev), NULL);
    g_signal_connect(btn_next, "clicked", G_CALLBACK(on_btn_next), NULL);
    s_name_label = lbl_name;

    /* ── RIGHT column: settings ──────────────────────────────────── */
    GtkBox *vbox_right = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));
    gtk_box_pack_start(hbox, GTK_WIDGET(vbox_right), TRUE, TRUE, 0);

    GtkFrame *frm_cfg = GTK_FRAME(gtk_frame_new("Settings"));
    gtk_box_pack_start(vbox_right, GTK_WIDGET(frm_cfg), TRUE, TRUE, 0);

    GtkBox *vbox_cfg = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 8));
    gtk_widget_set_margin_start (GTK_WIDGET(vbox_cfg), 10);
    gtk_widget_set_margin_end   (GTK_WIDGET(vbox_cfg), 10);
    gtk_widget_set_margin_top   (GTK_WIDGET(vbox_cfg), 10);
    gtk_widget_set_margin_bottom(GTK_WIDGET(vbox_cfg), 10);
    gtk_container_add(GTK_CONTAINER(frm_cfg), GTK_WIDGET(vbox_cfg));

    /* Row: Enabled  Startup */
    GtkBox *hbox_flags = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20));
    gtk_box_pack_start(vbox_cfg, GTK_WIDGET(hbox_flags), FALSE, FALSE, 0);

    GtkCheckButton *chk_en = GTK_CHECK_BUTTON(
            gtk_check_button_new_with_label("Enabled"));
    GtkCheckButton *chk_st = GTK_CHECK_BUTTON(
            gtk_check_button_new_with_label("Show on startup"));
    gtk_box_pack_start(hbox_flags, GTK_WIDGET(chk_en), FALSE, FALSE, 0);
    gtk_box_pack_start(hbox_flags, GTK_WIDGET(chk_st), FALSE, FALSE, 0);
    g_signal_connect(chk_en, "toggled", G_CALLBACK(on_chk_enabled), NULL);
    g_signal_connect(chk_st, "toggled", G_CALLBACK(on_chk_startup), NULL);
    s_chk_enabled = chk_en;
    s_chk_startup = chk_st;

    /* Row: Order in rotation  [↑] [↓] */
    GtkBox *hbox_order = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
    gtk_box_pack_start(vbox_cfg, GTK_WIDGET(hbox_order), FALSE, FALSE, 0);

    GtkLabel  *lbl_order = GTK_LABEL(gtk_label_new("Order in rotation:"));
    GtkButton *btn_up    = GTK_BUTTON(gtk_button_new_with_label("↑"));
    GtkButton *btn_dn    = GTK_BUTTON(gtk_button_new_with_label("↓"));
    gtk_box_pack_start(hbox_order, GTK_WIDGET(lbl_order), FALSE, FALSE, 0);
    gtk_box_pack_start(hbox_order, GTK_WIDGET(btn_up),    FALSE, FALSE, 0);
    gtk_box_pack_start(hbox_order, GTK_WIDGET(btn_dn),    FALSE, FALSE, 0);
    g_signal_connect(btn_up, "clicked", G_CALLBACK(on_btn_up), NULL);
    g_signal_connect(btn_dn, "clicked", G_CALLBACK(on_btn_down), NULL);

    /* Separator */
    gtk_box_pack_start(vbox_cfg,
            GTK_WIDGET(gtk_separator_new(GTK_ORIENTATION_HORIZONTAL)),
            FALSE, FALSE, 0);

    /* Display-specific settings stack */
    GtkStack *stack = GTK_STACK(gtk_stack_new());
    gtk_box_pack_start(vbox_cfg, GTK_WIDGET(stack), TRUE, TRUE, 0);
    s_settings_stack = stack;

    /* One stack page per display */
    int n = display_registry_count();
    for (int i = 0; i < n; i++) {
        display_entry_t *d = display_registry_get(i);
        if (!d) continue;

        char page_id[16];
        snprintf(page_id, sizeof(page_id), "%d", d->id);

        GtkBox *page = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
        if (d->create_settings) {
            d->create_settings(page);
        } else {
            GtkLabel *lbl = GTK_LABEL(
                    gtk_label_new("No additional settings for this display."));
            gtk_label_set_xalign(lbl, 0.0f);
            gtk_widget_set_sensitive(GTK_WIDGET(lbl), FALSE);
            gtk_box_pack_start(page, GTK_WIDGET(lbl), FALSE, FALSE, 0);
        }
        gtk_stack_add_named(stack, GTK_WIDGET(page), page_id);
    }

    gtk_widget_show_all(GTK_WIDGET(root));

    /* Show the startup display first */
    s_cur = display_registry_startup_idx();
    refresh_view();
}

static void on_displays_map(GtkWidget *w, gpointer unused)
{
    (void)w; (void)unused;
    displays_gui_do_init();
}

/*
 * displays_gui_init — store the root widget and arm the lazy trigger.
 * No widgets are built here; building is deferred to the first time the
 * Displays tab becomes visible (on_displays_map → displays_gui_do_init).
 */
void displays_gui_init(GtkBuilder *builder)
{
    s_root = GTK_BOX(gtk_builder_get_object(builder, "box_displays"));
    if (!s_root) {
        printf("G510s: displays_gui_init: box_displays not found\n");
        return;
    }
    g_signal_connect(s_root, "map", G_CALLBACK(on_displays_map), NULL);
}
