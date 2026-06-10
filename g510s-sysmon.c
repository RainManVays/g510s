/*
 *  g510s-sysmon.c — System monitor screen
 *
 *  LCD layout (160×43 px):
 *    y=1      : CPU [bar x=18..100]  XX%     T: BIG
 *    y=9      : RAM [bar x=18..100]  X.X/XG
 *    y=17     : horizontal separator
 *    y=17..43 : vertical separator x=80
 *    y=19,27,35: LEFT  disk[n]  R XXXX  W XXXX
 *               RIGHT  iface[n] v XXXX  ^ XXXX
 */

#define LOG_MODULE "sysmon"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <pthread.h>
#include <libg15render.h>
#include <gtk/gtk.h>

#include "g510s.h"


#define SM_MAX_CORES  8
#define SM_MAX_DISKS 16
#define SM_MAX_IFACES 3

/* ── CPU ──────────────────────────────────────────────────────────────── */

typedef struct {
    long user, nice, system, idle, iowait, irq, softirq, steal;
} sm_cpu_t;

static sm_cpu_t sm_cpu_prev[SM_MAX_CORES + 1];
static int      sm_cpu_n     = 0;
static int      sm_cpu_init  = 0;
static int      sm_cpu_total = -1;

static int sm_read_cpu(sm_cpu_t *s, int max) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), f) && n <= max) {
        if (strncmp(line, "cpu", 3) != 0) break;
        sm_cpu_t st = {0};
        char name[16];
        int r = sscanf(line, "%15s %ld %ld %ld %ld %ld %ld %ld %ld",
            name, &st.user, &st.nice, &st.system, &st.idle,
            &st.iowait, &st.irq, &st.softirq, &st.steal);
        if (r < 5) break;
        s[n++] = st;
    }
    fclose(f);
    return n - 1;
}

static int sm_cpu_pct(const sm_cpu_t *p, const sm_cpu_t *c) {
    long p_idle = p->idle + p->iowait;
    long c_idle = c->idle + c->iowait;
    long p_tot  = p_idle + p->user + p->nice + p->system + p->irq + p->softirq + p->steal;
    long c_tot  = c_idle + c->user + c->nice + c->system + c->irq + c->softirq + c->steal;
    long dt = c_tot - p_tot;
    if (dt <= 0) return 0;
    int pct = (int)(100L * (dt - (c_idle - p_idle)) / dt);
    return (pct < 0) ? 0 : (pct > 100) ? 100 : pct;
}

/* ── RAM ──────────────────────────────────────────────────────────────── */

static long sm_ram_total_kb = 0;
static long sm_ram_avail_kb = 0;

static void sm_read_ram(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char line[128];
    long total = 0, avail = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line + 9, "%ld", &total);
        else if (strncmp(line, "MemAvailable:", 13) == 0)
            sscanf(line + 13, "%ld", &avail);
        if (total && avail) break;
    }
    fclose(f);
    sm_ram_total_kb = total;
    sm_ram_avail_kb = avail;
}

/* ── Temperature ──────────────────────────────────────────────────────── */

static int sm_temp_c = -1;

static void sm_read_temp(void) {
    DIR *d = opendir("/sys/class/hwmon");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strncmp(de->d_name, "hwmon", 5) != 0) continue;
            char path[512];
            snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp1_input", de->d_name);
            FILE *f = fopen(path, "r");
            if (f) {
                int val = 0;
                if (fscanf(f, "%d", &val) == 1 && val > 0) {
                    sm_temp_c = val / 1000;
                    fclose(f);
                    closedir(d);
                    return;
                }
                fclose(f);
            }
        }
        closedir(d);
    }
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f) {
        int val = 0;
        if (fscanf(f, "%d", &val) == 1) sm_temp_c = val / 1000;
        fclose(f);
    }
}

/* ── Disks ────────────────────────────────────────────────────────────── */

typedef struct {
    char               name[16];
    unsigned long long rd;
    unsigned long long wr;
} sm_disk_t;

static sm_disk_t       sm_disk_prev[SM_MAX_DISKS];
static int             sm_disk_n = 0;
static char            sm_disk_names[SM_MAX_DISKS][16];
static unsigned long long sm_disk_rd_bps[SM_MAX_DISKS];
static unsigned long long sm_disk_wr_bps[SM_MAX_DISKS];

static int is_whole_disk(const char *name) {
    if (strncmp(name, "sd", 2) == 0 && name[2] >= 'a' && name[2] <= 'z' && name[3] == '\0')
        return 1;
    if (strncmp(name, "nvme", 4) == 0 && strchr(name + 4, 'p') == NULL)
        return 1;
    return 0;
}

static int sm_read_disks(sm_disk_t *out, int max) {
    FILE *f = fopen("/proc/diskstats", "r");
    if (!f) return 0;
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), f) && n < max) {
        unsigned int maj, min;
        char name[32];
        unsigned long long rd, wr;
        int r = sscanf(line, "%u %u %31s %*u %*u %llu %*u %*u %*u %llu",
                       &maj, &min, name, &rd, &wr);
        (void)maj; (void)min;
        if (r < 5) continue;
        if (!is_whole_disk(name)) continue;
        strncpy(out[n].name, name, sizeof(out[n].name) - 1);
        out[n].name[sizeof(out[n].name) - 1] = '\0';
        out[n].rd = rd;
        out[n].wr = wr;
        n++;
    }
    fclose(f);
    return n;
}

/* ── Network (up to SM_MAX_IFACES non-loopback interfaces) ────────────── */

static char               sm_net_ifaces[SM_MAX_IFACES][16];
static int                sm_net_n = 0;
static unsigned long long sm_net_rx_prev[SM_MAX_IFACES];
static unsigned long long sm_net_tx_prev[SM_MAX_IFACES];
static unsigned long long sm_net_rx_bps[SM_MAX_IFACES];
static unsigned long long sm_net_tx_bps[SM_MAX_IFACES];

/* Selection state — 1 = show, 0 = hide.  Written from GTK thread, read from
 * update thread. On x86 single-int reads/writes are atomic enough for this. */
static int sm_disk_selected[SM_MAX_DISKS];
static int sm_iface_selected[SM_MAX_IFACES];
static GtkWidget *sm_disk_chk[SM_MAX_DISKS];
static GtkWidget *sm_iface_chk[SM_MAX_IFACES];
static int sysmon_refresh_ms = 1000;
static GtkSpinButton *sysmon_spin_refresh;

static void sm_find_ifaces(void) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return;
    char line[256];
    fgets(line, sizeof(line), f);
    fgets(line, sizeof(line), f);
    sm_net_n = 0;
    while (fgets(line, sizeof(line), f) && sm_net_n < SM_MAX_IFACES) {
        char *p = line;
        while (*p == ' ') p++;
        char iface[16] = {0};
        int i = 0;
        while (*p && *p != ':' && i < 15) iface[i++] = *p++;
        if (strcmp(iface, "lo") == 0) continue;
        strncpy(sm_net_ifaces[sm_net_n], iface, 15);
        sm_net_n++;
    }
    fclose(f);
}

static void sm_read_all_net(unsigned long long *rx, unsigned long long *tx) {
    for (int i = 0; i < sm_net_n; i++) { rx[i] = 0; tx[i] = 0; }
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return;
    char line[256];
    fgets(line, sizeof(line), f);
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ') p++;
        char iface[16] = {0};
        int i = 0;
        while (*p && *p != ':' && i < 15) iface[i++] = *p++;
        for (int j = 0; j < sm_net_n; j++) {
            if (strcmp(iface, sm_net_ifaces[j]) != 0) continue;
            unsigned long long rb, tb;
            if (sscanf(p + 1, "%llu %*u %*u %*u %*u %*u %*u %*u %llu", &rb, &tb) == 2) {
                rx[j] = rb;
                tx[j] = tb;
            }
            break;
        }
    }
    fclose(f);
}

/* ── Format helpers ───────────────────────────────────────────────────── */

/* Always 4 printable chars + NUL: "  0B", " 12K", "840K", "  1M", "  3G" */
static void fmt_rate(unsigned long long bps, char *buf) {
    if (bps < 1000)
        snprintf(buf, 5, "%3lluB", bps);
    else if (bps < 1000000ULL)
        snprintf(buf, 5, "%3lluK", bps / 1000);
    else if (bps < 1000000000ULL)
        snprintf(buf, 5, "%3lluM", bps / 1000000);
    else
        snprintf(buf, 5, "%3lluG", (bps / 1000000000ULL) % 1000);
}

static void fmt_ram(long used_kb, long total_kb, char *buf, int size) {
    double used_gb  = used_kb  / (1024.0 * 1024.0);
    double total_gb = total_kb / (1024.0 * 1024.0);
    if (total_gb < 10.0)
        snprintf(buf, size, "%.1f/%.0fG", used_gb, total_gb);
    else
        snprintf(buf, size, "%.0f/%.0fG", used_gb, total_gb);
}

/* ── Shared timestamp ─────────────────────────────────────────────────── */

static struct timespec sm_last_update = {0, 0};

/* ── Initialization ───────────────────────────────────────────────────── */

void sysmon_screen_init(void) {
    LDEBUG("initializing sysmon screen");
    sm_cpu_t curr[SM_MAX_CORES + 1];
    int cores = sm_read_cpu(curr, SM_MAX_CORES);
    if (cores >= 0) {
        memcpy(sm_cpu_prev, curr, sizeof(sm_cpu_t) * (cores + 1));
        sm_cpu_n    = cores;
        sm_cpu_init = 1;
    }
    sm_disk_n = sm_read_disks(sm_disk_prev, SM_MAX_DISKS);
    for (int i = 0; i < sm_disk_n; i++) {
        strncpy(sm_disk_names[i], sm_disk_prev[i].name, 15);
        sm_disk_names[i][15] = '\0';
        sm_disk_rd_bps[i] = 0;
        sm_disk_wr_bps[i] = 0;
    }
    sm_find_ifaces();
    sm_read_all_net(sm_net_rx_prev, sm_net_tx_prev);
    for (int i = 0; i < sm_net_n; i++) {
        sm_net_rx_bps[i] = 0;
        sm_net_tx_bps[i] = 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &sm_last_update);

    for (int i = 0; i < SM_MAX_DISKS;  i++) { sm_disk_selected[i]  = 1; sm_disk_chk[i]  = NULL; }
    for (int i = 0; i < SM_MAX_IFACES; i++) { sm_iface_selected[i] = 1; sm_iface_chk[i] = NULL; }
}

/* ── Main screen function ─────────────────────────────────────────────── */
/*
 *  LCD pixel layout (160×43), all text MED (6px tall) except temp (LARGE 7px):
 *
 *  y=0      : temperature LARGE  x=130 ("67C" = 23px → x=152)
 *  y=1..6   : "CPU" MED x=2     bar x=18..100    "%3d%%" MED x=103
 *  y=9..14  : "RAM" MED x=2     bar x=18..100    "X.X/XG" MED x=103
 *  y=17     : horizontal line
 *  x=80,y=17..43: vertical line
 *
 *  y=19,27,35  (MED text, 6px tall, 8px row spacing):
 *    LEFT  x=1   disk name (5 ch, ~23px)
 *          x=26  "R"   (4px)
 *          x=32  R-rate (4 ch, 19px)
 *          x=53  "W"   (4px)
 *          x=59  W-rate (4 ch, 19px) → ends x=77
 *    RIGHT x=82  iface (5 ch, ~23px)
 *          x=107 "v"   (4px)
 *          x=113 rx-rate (4 ch, 19px)
 *          x=134 "^"   (4px)
 *          x=140 tx-rate (4 ch, 19px) → ends x=158
 */
void sysmon_screen(lcd_t *lcd) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int new_data = 0;

    long elapsed_ms = (now.tv_sec - sm_last_update.tv_sec) * 1000L +
                      (now.tv_nsec - sm_last_update.tv_nsec) / 1000000L;

    int was_zero = (lcd->ident == 0);
    if (was_zero)
        switch_log("sysmon_screen: elapsed=%ldms refresh_ms=%d sm_cpu_total=%d",
                   elapsed_ms, sysmon_refresh_ms, sm_cpu_total);

    if (elapsed_ms >= sysmon_refresh_ms) {
        long dt_ms = (sm_last_update.tv_sec > 0 || sm_last_update.tv_nsec > 0)
                     ? elapsed_ms : 1000L;
        if (dt_ms <= 0) dt_ms = 1;
        sm_last_update = now;

        /* CPU */
        sm_cpu_t curr[SM_MAX_CORES + 1];
        int cores = sm_read_cpu(curr, SM_MAX_CORES);
        if (cores >= 0) {
            if (!sm_cpu_init) {
                memcpy(sm_cpu_prev, curr, sizeof(sm_cpu_t) * (cores + 1));
                sm_cpu_n    = cores;
                sm_cpu_init = 1;
            } else {
                sm_cpu_total = sm_cpu_pct(&sm_cpu_prev[0], &curr[0]);
                memcpy(sm_cpu_prev, curr, sizeof(sm_cpu_t) * (cores + 1));
                sm_cpu_n = cores;
            }
        }

        /* RAM */
        sm_read_ram();

        /* Temperature */
        sm_read_temp();

        /* Disks */
        sm_disk_t curr_disk[SM_MAX_DISKS];
        int dn = sm_read_disks(curr_disk, SM_MAX_DISKS);
        for (int i = 0; i < dn; i++) {
            unsigned long long rd_bps = 0, wr_bps = 0;
            for (int j = 0; j < sm_disk_n; j++) {
                if (strcmp(curr_disk[i].name, sm_disk_prev[j].name) != 0) continue;
                unsigned long long drd = curr_disk[i].rd - sm_disk_prev[j].rd;
                unsigned long long dwr = curr_disk[i].wr - sm_disk_prev[j].wr;
                rd_bps = (drd * 512 * 1000ULL) / (unsigned long long)dt_ms;
                wr_bps = (dwr * 512 * 1000ULL) / (unsigned long long)dt_ms;
                break;
            }
            strncpy(sm_disk_names[i], curr_disk[i].name, 15);
            sm_disk_names[i][15] = '\0';
            sm_disk_rd_bps[i] = rd_bps;
            sm_disk_wr_bps[i] = wr_bps;
        }
        memcpy(sm_disk_prev, curr_disk, sizeof(sm_disk_t) * dn);
        sm_disk_n = dn;

        /* Network */
        unsigned long long rx[SM_MAX_IFACES], tx[SM_MAX_IFACES];
        sm_read_all_net(rx, tx);
        for (int i = 0; i < sm_net_n; i++) {
            long long drx = (long long)rx[i] - (long long)sm_net_rx_prev[i];
            long long dtx = (long long)tx[i] - (long long)sm_net_tx_prev[i];
            sm_net_rx_bps[i] = (drx > 0) ? (unsigned long long)drx * 1000ULL / (unsigned long long)dt_ms : 0;
            sm_net_tx_bps[i] = (dtx > 0) ? (unsigned long long)dtx * 1000ULL / (unsigned long long)dt_ms : 0;
            sm_net_rx_prev[i] = rx[i];
            sm_net_tx_prev[i] = tx[i];
        }

        new_data = 1;
    }

    if (!new_data && lcd->ident != 0) return;
    if (sm_cpu_total < 0) {
        switch_log("sysmon_screen: EARLY RETURN — no data yet (sm_cpu_total<0)");
        return;
    }

    g15canvas *canvas = malloc(sizeof(g15canvas));
    if (!canvas) return;

    memset(canvas->buffer, 0, G15_BUFFER_LEN);
    canvas->mode_cache   = 0;
    canvas->mode_reverse = 0;
    canvas->mode_xor     = 0;

    char buf[32];

    /* ── CPU row (MED text, bar y=1..6) ───────────────────────────────── */
    g15r_renderString(canvas, (unsigned char *)"CPU", 0, G15_TEXT_MED, 2, 1);
    g15r_drawBar(canvas, 18, 1, 100, 6, G15_COLOR_BLACK, sm_cpu_total, 100, 1);
    snprintf(buf, sizeof(buf), "%3d%%", sm_cpu_total);
    g15r_renderString(canvas, (unsigned char *)buf, 0, G15_TEXT_MED, 103, 1);

    /* Temperature — LARGE for extra prominence */
    if (sm_temp_c > 0) {
        snprintf(buf, sizeof(buf), "%dC", sm_temp_c);
        g15r_renderString(canvas, (unsigned char *)buf, 0, G15_TEXT_LARGE, 130, 0);
    }

    /* ── RAM row (MED text, bar y=9..14) ──────────────────────────────── */
    g15r_renderString(canvas, (unsigned char *)"RAM", 0, G15_TEXT_MED, 2, 9);
    if (sm_ram_total_kb > 0) {
        long used_kb = sm_ram_total_kb - sm_ram_avail_kb;
        int ram_pct = (int)(100L * used_kb / sm_ram_total_kb);
        g15r_drawBar(canvas, 18, 9, 100, 14, G15_COLOR_BLACK, ram_pct, 100, 1);
        fmt_ram(used_kb, sm_ram_total_kb, buf, sizeof(buf));
        g15r_renderString(canvas, (unsigned char *)buf, 0, G15_TEXT_MED, 103, 9);
    }

    /* ── Separators ────────────────────────────────────────────────────── */
    g15r_drawLine(canvas, 0,  17, 159, 17, G15_COLOR_BLACK);
    g15r_drawLine(canvas, 80, 17, 80,  43, G15_COLOR_BLACK);

    /* ── 3 data rows (MED text, 6px tall, 8px spacing) ────────────────── */

    /* Build visible-disk and visible-iface index lists from selection state */
    int vis_disk[SM_MAX_DISKS];
    int vis_disk_n = 0;
    for (int i = 0; i < sm_disk_n; i++)
        if (sm_disk_selected[i]) vis_disk[vis_disk_n++] = i;

    int vis_iface[SM_MAX_IFACES];
    int vis_iface_n = 0;
    for (int i = 0; i < sm_net_n; i++)
        if (sm_iface_selected[i]) vis_iface[vis_iface_n++] = i;

    int raw_off = g510s_data.sysmon_disk_offset;
    int offset  = (vis_disk_n > 0) ? ((raw_off % vis_disk_n) + vis_disk_n) % vis_disk_n : 0;

    for (int row = 0; row < 3; row++) {
        int y = 19 + row * 8;

        /* LEFT: disk (from visible list) */
        int idx = -1;
        if (vis_disk_n > 0) {
            if (vis_disk_n >= 3)
                idx = vis_disk[(offset + row) % vis_disk_n];
            else if (row < vis_disk_n)
                idx = vis_disk[row];
        }

        if (idx >= 0) {
            char dname[6] = {0};
            strncpy(dname, sm_disk_names[idx], 5);
            g15r_renderString(canvas, (unsigned char *)dname, 0, G15_TEXT_MED, 1,  y);
            g15r_renderString(canvas, (unsigned char *)"R",   0, G15_TEXT_MED, 26, y);
            fmt_rate(sm_disk_rd_bps[idx], buf);
            g15r_renderString(canvas, (unsigned char *)buf,   0, G15_TEXT_MED, 32, y);
            g15r_renderString(canvas, (unsigned char *)"W",   0, G15_TEXT_MED, 53, y);
            fmt_rate(sm_disk_wr_bps[idx], buf);
            g15r_renderString(canvas, (unsigned char *)buf,   0, G15_TEXT_MED, 59, y);
        }

        /* RIGHT: network interface (from visible list) */
        if (row < vis_iface_n) {
            int fidx = vis_iface[row];
            char iname[6] = {0};
            strncpy(iname, sm_net_ifaces[fidx], 5);
            g15r_renderString(canvas, (unsigned char *)iname, 0, G15_TEXT_MED, 82,  y);
            g15r_renderString(canvas, (unsigned char *)"v",   0, G15_TEXT_MED, 107, y);
            fmt_rate(sm_net_rx_bps[fidx], buf);
            g15r_renderString(canvas, (unsigned char *)buf,   0, G15_TEXT_MED, 113, y);
            g15r_renderString(canvas, (unsigned char *)"^",   0, G15_TEXT_MED, 134, y);
            fmt_rate(sm_net_tx_bps[fidx], buf);
            g15r_renderString(canvas, (unsigned char *)buf,   0, G15_TEXT_MED, 140, y);
        }
    }

    pthread_mutex_lock(&lcdlist_mutex);
    memcpy(lcd->buf, canvas->buffer, G15_BUFFER_LEN);
    lcd->ident = random();
    pthread_mutex_unlock(&lcdlist_mutex);
    if (was_zero)
        switch_log("sysmon_screen: rendered OK ident=%ld new_data=%d vis_disk=%d vis_iface=%d",
                   lcd->ident, new_data, vis_disk_n, vis_iface_n);

    free(canvas);
}

void sysmon_create_settings(GtkBox *box) {
    /* Refresh rate row */
    GtkBox *row = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
    gtk_box_pack_start(box, GTK_WIDGET(row), FALSE, FALSE, 0);
    gtk_box_pack_start(row,
            GTK_WIDGET(gtk_label_new("Refresh rate (s):")), FALSE, FALSE, 0);
    sysmon_spin_refresh = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 60, 1));
    gtk_spin_button_set_value(sysmon_spin_refresh, sysmon_refresh_ms / 1000.0);
    gtk_box_pack_start(row, GTK_WIDGET(sysmon_spin_refresh), FALSE, FALSE, 0);

    /* Disk selection */
    if (sm_disk_n > 0) {
        gtk_box_pack_start(box,
                GTK_WIDGET(gtk_label_new("Disks:")), FALSE, FALSE, 0);
        for (int i = 0; i < sm_disk_n; i++) {
            sm_disk_chk[i] = gtk_check_button_new_with_label(sm_disk_names[i]);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sm_disk_chk[i]),
                                         sm_disk_selected[i]);
            gtk_box_pack_start(box, sm_disk_chk[i], FALSE, FALSE, 0);
        }
    }

    /* Interface selection */
    if (sm_net_n > 0) {
        gtk_box_pack_start(box,
                GTK_WIDGET(gtk_label_new("Interfaces:")), FALSE, FALSE, 0);
        for (int i = 0; i < sm_net_n; i++) {
            sm_iface_chk[i] = gtk_check_button_new_with_label(sm_net_ifaces[i]);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sm_iface_chk[i]),
                                         sm_iface_selected[i]);
            gtk_box_pack_start(box, sm_iface_chk[i], FALSE, FALSE, 0);
        }
    }
}

void sysmon_save_settings(void) {
    if (sysmon_spin_refresh)
        sysmon_refresh_ms = (int)(gtk_spin_button_get_value(sysmon_spin_refresh) * 1000);
    for (int i = 0; i < sm_disk_n; i++)
        if (sm_disk_chk[i])
            sm_disk_selected[i] = gtk_toggle_button_get_active(
                    GTK_TOGGLE_BUTTON(sm_disk_chk[i]));
    for (int i = 0; i < sm_net_n; i++)
        if (sm_iface_chk[i])
            sm_iface_selected[i] = gtk_toggle_button_get_active(
                    GTK_TOGGLE_BUTTON(sm_iface_chk[i]));
}
