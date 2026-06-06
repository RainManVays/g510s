/*
 *  This file is part of g510s.
 *
 *  g510s is  free  software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the  Free Software Foundation; either version 3 of the License, or (at your
 *  option)  any later version.
 *
 *  g510s is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with g510s; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1335  USA
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <libg15render.h>

#include "g510s.h"


#define CPU_MAX_CORES 8

typedef struct {
    long user, nice, system, idle, iowait, irq, softirq, steal;
} cpu_stat_t;

static cpu_stat_t cpu_prev[CPU_MAX_CORES + 1];
static int cpu_n_cores = 0;
static int cpu_initialized = 0;

/* Last computed percentages — kept between renders for instant redraw */
static int cpu_last_total = -1;
static int cpu_last_core[CPU_MAX_CORES];
static int cpu_last_show = 0;


static int read_cpu_stats(cpu_stat_t *stats, int maxcores) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    char line[256];
    int count = 0;

    while (fgets(line, sizeof(line), f) && count <= maxcores) {
        if (strncmp(line, "cpu", 3) != 0) break;
        cpu_stat_t s = {0};
        char name[16];
        int n = sscanf(line, "%15s %ld %ld %ld %ld %ld %ld %ld %ld",
            name, &s.user, &s.nice, &s.system, &s.idle,
            &s.iowait, &s.irq, &s.softirq, &s.steal);
        if (n < 5) break;
        stats[count++] = s;
    }
    fclose(f);
    return count - 1;  /* number of individual cores (stats[0] is total) */
}

static int cpu_pct(const cpu_stat_t *prev, const cpu_stat_t *curr) {
    long prev_idle = prev->idle + prev->iowait;
    long curr_idle = curr->idle + curr->iowait;
    long prev_total = prev_idle + prev->user + prev->nice + prev->system +
                      prev->irq + prev->softirq + prev->steal;
    long curr_total = curr_idle + curr->user + curr->nice + curr->system +
                      curr->irq + curr->softirq + curr->steal;
    long dt = curr_total - prev_total;
    if (dt <= 0) return 0;
    int pct = (int)(100L * (dt - (curr_idle - prev_idle)) / dt);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

void cpu_screen_init(void) {
    cpu_stat_t curr[CPU_MAX_CORES + 1];
    int cores = read_cpu_stats(curr, CPU_MAX_CORES);
    if (cores < 0) return;
    memcpy(cpu_prev, curr, sizeof(cpu_stat_t) * (cores + 1));
    cpu_n_cores = cores;
    cpu_initialized = 1;
}

/*
 * LCD layout (160×43 pixels):
 *
 *  y=1..6  : "CPU" label | overall bar (x=20..130) | "XX%" label
 *  y=8     : separator line
 *  y=10..42: per-core bars, 2 columns × 4 rows (up to 8 cores)
 *            each cell 80px wide × 8px tall:
 *            "C0" | bar (x+11..x+62) | "XX%"
 */
void cpu_screen(lcd_t *lcd) {
    static time_t last_data_update = 0;
    time_t now = time(NULL);
    int new_data = 0;

    /* Collect stats once per second */
    if (now != last_data_update) {
        cpu_stat_t curr[CPU_MAX_CORES + 1];
        int cores = read_cpu_stats(curr, CPU_MAX_CORES);
        if (cores >= 0) {
            last_data_update = now;
            if (!cpu_initialized) {
                memcpy(cpu_prev, curr, sizeof(cpu_stat_t) * (cores + 1));
                cpu_n_cores = cores;
                cpu_initialized = 1;
            } else {
                cpu_last_total = cpu_pct(&cpu_prev[0], &curr[0]);
                cpu_last_show = (cores < CPU_MAX_CORES) ? cores : CPU_MAX_CORES;
                for (int i = 0; i < cpu_last_show; i++)
                    cpu_last_core[i] = cpu_pct(&cpu_prev[i + 1], &curr[i + 1]);
                memcpy(cpu_prev, curr, sizeof(cpu_stat_t) * (cores + 1));
                cpu_n_cores = cores;
                new_data = 1;
            }
        }
    }

    /* Render if new data arrived OR screen was invalidated (ident==0 = forced redraw) */
    if (!new_data && lcd->ident != 0) return;
    if (cpu_last_total < 0) return;  /* no data yet — wait for first full collection */

    g15canvas *canvas = malloc(sizeof(g15canvas));
    if (!canvas) return;

    memset(canvas->buffer, 0, G15_BUFFER_LEN);
    canvas->mode_cache = 0;
    canvas->mode_reverse = 0;
    canvas->mode_xor = 0;

    char buf[16];

    /* Overall CPU: label + bar + percent */
    g15r_renderString(canvas, (unsigned char *)"CPU", 0, G15_TEXT_SMALL, 2, 1);
    g15r_drawBar(canvas, 20, 1, 130, 6, G15_COLOR_BLACK, cpu_last_total, 100, 1);
    snprintf(buf, sizeof(buf), "%3d%%", cpu_last_total);
    g15r_renderString(canvas, (unsigned char *)buf, 0, G15_TEXT_SMALL, 133, 1);

    /* Separator */
    g15r_drawLine(canvas, 0, 8, 159, 8, G15_COLOR_BLACK);

    /* Per-core bars */
    for (int i = 0; i < cpu_last_show; i++) {
        int col = i % 2;
        int row = i / 2;
        if (row >= 4) break;

        int x = col * 80;
        int y = 11 + row * 8;

        snprintf(buf, sizeof(buf), "C%d", i);
        g15r_renderString(canvas, (unsigned char *)buf, 0, G15_TEXT_SMALL, x + 1, y);
        g15r_drawBar(canvas, x + 11, y, x + 62, y + 5, G15_COLOR_BLACK, cpu_last_core[i], 100, 1);
        snprintf(buf, sizeof(buf), "%3d%%", cpu_last_core[i]);
        g15r_renderString(canvas, (unsigned char *)buf, 0, G15_TEXT_SMALL, x + 64, y);
    }

    pthread_mutex_lock(&lcdlist_mutex);
    memcpy(lcd->buf, canvas->buffer, G15_BUFFER_LEN);
    lcd->ident = random();
    pthread_mutex_unlock(&lcdlist_mutex);

    free(canvas);
}
