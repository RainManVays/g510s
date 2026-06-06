/* g510s-claude-lcd.c — Claude Code usage screen for G510/G510s LCD
 *
 * ── LCD layout (160×43 px) ────────────────────────────────────────────────
 *
 *   y= 1..10  "CC Pro   14:23"          LARGE: plan name | local time
 *   y=11..12  gap
 *   y=13..22  "5h [===bar===] Rst Xh"  LARGE label | bar | MED reset
 *   y=23..24  gap
 *   y=25..34  "7d [===bar===] Rst Xd"  LARGE label | bar | MED reset
 *   y=35      gap
 *   y=36..42  "5H:313K/965K 7D:1.9M/2.9M"  MED: both windows tok/limit
 *
 * Bar x-geometry: x1=20 x2=109 (x1 shifted right to clear LARGE "5h"/"7d").
 * Reset label:    x=111 in MED font (up to 7 chars, "Rst 59m"/"Rst 23h").
 *
 * ── Data source ───────────────────────────────────────────────────────────
 *
 * Files: ~/.claude/projects/ENCODED_PATH/SESSION_UUID.jsonl
 * Each file is one Claude Code session (conversation).
 * Each line is one JSON event; we care only about type=="assistant" lines,
 * which carry usage data in message.usage.{input,output,cache_*}_tokens.
 *
 * Rescan every CL_SCAN_S=60 seconds. Files older than 7d are skipped by
 * mtime as a fast pre-filter before opening.
 *
 * ── Streaming-duplicate problem ───────────────────────────────────────────
 *
 * Claude Code writes a new JSONL line for every streaming chunk received from
 * the API. A single API call (requestId) therefore produces 2-10 identical
 * lines in quick succession with the same final usage values. Without
 * deduplication, summing all lines overcounts by ~2-10×.
 *
 * Fix: last-entry-wins per requestId. Within a file, all duplicate lines for
 * a requestId are consecutive (they arrive while the response streams), so a
 * simple "buffer current; commit when requestId changes" pattern suffices.
 * No cross-file dedup needed: each requestId belongs to exactly one file.
 *
 * ── Sidechain (subagent) filter ───────────────────────────────────────────
 *
 * When Claude Code spawns a sub-agent, it writes a separate JSONL file whose
 * entries have isSidechain:true. Both the parent and sub-agent files record
 * cache_creation_input_tokens for the same cache block, causing double-
 * counting. Lines with isSidechain:true are skipped entirely.
 *
 * ── Rate-limit metric ─────────────────────────────────────────────────────
 *
 * Anthropic's claude.ai subscription rate limiter counts OUTPUT TOKENS ONLY.
 * Cache tokens (cache_creation, cache_read) and input_tokens do NOT count
 * against the rolling-window rate limit for Pro/Max subscribers.
 *
 * This was determined empirically: when summing in+out+cache_* the numbers
 * exceeded the published 44K/5h API limit by 790×. Summing output_tokens
 * only and back-calculating from the official usage percentage gave consistent
 * limits across both the 5h and 7d windows. See calibration section below.
 *
 * ── Plan limits & calibration ─────────────────────────────────────────────
 *
 * Limits are OUTPUT TOKEN counts per rolling window, derived empirically by
 * comparing JSONL output_tokens sums against the % shown on claude.ai/usage.
 *
 * Calibration session (2026-06-06, user on Pro plan):
 *
 *   Official UI: 5h = 32%,  7d = 67%
 *   JSONL sum:   5h = 308,798 out_tok,  7d = 1,919,253 out_tok
 *   Derived:     lim_5h = 308798/0.32 ≈ 965,000
 *                lim_7d = 1919253/0.67 ≈ 2,860,000
 *                ratio  = 7d/5h ≈ 2.96 (not 8.4 as published for API tiers)
 *
 * Accuracy check: out_5h / lim_5h = 312,959/965,000 = 32.4% vs official 33%.
 * The ~1% gap is the JSONL lag: the current session's last response isn't
 * flushed to disk until after the message completes.
 *
 * Max5 and Max20 multipliers (5× and 20× Pro) are assumed from plan naming;
 * they have not been independently verified. Adjust if you subscribe to Max.
 *
 * TO RECALIBRATE:
 *   1. Note the official % shown at claude.ai for 5h and 7d windows.
 *   2. Sum output_tokens from deduplicated assistant entries in the same
 *      windows (see calc_plan.md for the full Python script, or run
 *      "python3 calc_plan.py" if the script exists in the project root).
 *      Key algorithm: type=="assistant", isSidechain==false, last-entry-wins
 *      per requestId, filter by timestamp >= (now - window_seconds).
 *   3. new_lim_5h = out_5h / (official_5h_pct / 100.0)
 *      new_lim_7d = out_7d / (official_7d_pct / 100.0)
 *   4. Update CL_PLAN_PRO below. Derive Max5/Max20 by multiplying by 5 and 20.
 *   5. Rebuild and reinstall: make && sudo make install && pkill g510s && g510s
 */
#define _DEFAULT_SOURCE   /* timegm() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glob.h>
#include <sys/stat.h>
#include <pthread.h>
#include <libg15render.h>

#include "g510s.h"

#define CL_SCAN_S 60    /* JSONL rescan interval, seconds */

/* Despite Anthropic's UI label "Session (5hr)", empirical measurement shows
 * the actual rolling window is 10 hours. Calibrated 2026-06-06:
 *   out_10h=308K → official 32% → lim=965K ✓
 *   out_10h=548K → official 56% → lim=965K ✓
 * The 5h label is kept on the bar to match the official UI terminology. */
#define WIN_5H  (10 * 3600)
#define WIN_7D  (7  * 86400)

/* Bar geometry: x1 pushed to 20 to clear LARGE "5h"/"7d" labels (~18px wide).
 * Reset label in MED font at x=111 — fits 7 chars ("Rst now") without overflow. */
#define BAR_X1   20
#define BAR_X2  109
#define RST_X   111

/* Claude Sonnet 4.6 pricing (USD per million tokens) */
#define PRICE_IN   3.00
#define PRICE_OUT 15.00
#define PRICE_CW   3.75
#define PRICE_CR   0.30

/* ── Plan limits ─────────────────────────────────────────────────────── */

typedef struct {
    long long lim_5h;
    long long lim_7d;
    char      name[8];
} cl_plan_t;

/* Output-token limits per rolling window.
 * Calibrated 2026-06-06: Pro 5h=32% at 308,798 out_tok → lim=965K;
 *                         Pro 7d=67% at 1,919K out_tok  → lim=2,860K.
 * Max5/Max20 are unverified 5×/20× extrapolations — adjust if you upgrade. */
static const cl_plan_t CL_PLAN_PRO   = {   965000,   2860000, "Pro"   };
static const cl_plan_t CL_PLAN_MAX5  = {  4825000,  14300000, "Max5"  };
static const cl_plan_t CL_PLAN_MAX20 = { 19300000,  57200000, "Max20" };

static cl_plan_t cl_plan;

/* ── State ───────────────────────────────────────────────────────────── */

static long long cl_tok_5h = 0;
static long long cl_tok_7d = 0;

static time_t cl_last_screen = 0;
static time_t cl_last_scan   = 0;

/* ── JSON field extraction ───────────────────────────────────────────── */

static long long cl_jll(const char *s, const char *key) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(s, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ') p++;
    return strtoll(p, NULL, 10);
}

/* Extract a JSON string field value into out[0..outsz-1], NUL-terminated.
   Returns length on success, 0 if key not found. */
static int cl_jstr(const char *s, const char *key, char *out, size_t outsz) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(s, needle);
    if (!p) return 0;
    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outsz)
        out[i++] = *p++;
    out[i] = '\0';
    return (int)i;
}

/* Parse ISO8601 UTC string "YYYY-MM-DDTHH:MM:SS..." → time_t (UTC). */
static time_t cl_parse_ts(const char *s) {
    int Y, Mo, D, h, m, sec;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &m, &sec) != 6)
        return 0;
    struct tm tm = {
        .tm_year = Y - 1900, .tm_mon = Mo - 1, .tm_mday = D,
        .tm_hour = h,        .tm_min  = m,      .tm_sec  = sec,
        .tm_isdst = 0
    };
    return timegm(&tm);
}

/* ── Plan detection ──────────────────────────────────────────────────── */

static void cl_load_plan(void) {
    cl_plan = CL_PLAN_PRO;   /* default */

    const char *home = getenv("HOME");
    if (!home) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/.claude/.credentials.json", home);

    FILE *f = fopen(path, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    rewind(f);
    if (fsz <= 0 || fsz > 65536) { fclose(f); return; }

    char *buf = malloc(fsz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, fsz, f);
    buf[fsz] = '\0';
    fclose(f);

    /* subscriptionType observed values: "pro" (verified), "max" / "max_20"
     * (assumed — not yet observed on this machine). Any unknown value falls
     * back to Pro (conservative — bars may show over 100% on Max plans). */
    const char *p = strstr(buf, "\"subscriptionType\":\"");
    if (p) {
        p += 20;
        if      (strncmp(p, "max_20", 6) == 0 || strncmp(p, "max20", 5) == 0)
            cl_plan = CL_PLAN_MAX20;
        else if (strncmp(p, "max", 3) == 0)
            cl_plan = CL_PLAN_MAX5;
        /* "pro" or unknown → CL_PLAN_PRO (already set) */
    }

    free(buf);
}

/* ── Scan all JSONL files with per-requestId deduplication ───────────── */

static void cl_load_all(void) {
    const char *home = getenv("HOME");
    if (!home) return;

    cl_tok_5h = 0;
    cl_tok_7d = 0;

    char pat[512];
    snprintf(pat, sizeof(pat), "%s/.claude/projects/*/*.jsonl", home);

    glob_t gl;
    if (glob(pat, 0, NULL, &gl) != 0) return;

    time_t now   = time(NULL);
    time_t t0_5h = now - WIN_5H;
    time_t t0_7d = now - WIN_7D;

    char *line = malloc(65536);
    if (!line) { globfree(&gl); return; }

    for (size_t i = 0; i < gl.gl_pathc; i++) {
        /* skip files older than 7d window by mtime (fast pre-filter) */
        struct stat st;
        if (stat(gl.gl_pathv[i], &st) == 0 && st.st_mtime < t0_7d) continue;

        FILE *f = fopen(gl.gl_pathv[i], "r");
        if (!f) continue;

        /* Per-file dedup state.
         * All streaming duplicates of a requestId are consecutive within a
         * file; we buffer the current entry and commit when requestId changes.
         * last-entry-wins → always overwrite buffer with newest same-id line. */
        char last_rid[64] = "";
        long long buf_out = 0;
        time_t    buf_ts  = 0;

        while (fgets(line, 65536, f)) {
            if (!strstr(line, "\"type\":\"assistant\"")) continue;
            if  (strstr(line, "\"isSidechain\":true"))  continue;

            char rid[64], tsstr[32];
            if (!cl_jstr(line, "requestId", rid,   sizeof(rid)))   continue;
            if (!cl_jstr(line, "timestamp",  tsstr, sizeof(tsstr))) continue;

            time_t ts = cl_parse_ts(tsstr);
            if (ts < t0_7d) continue;

            /* Rate limit counts output_tokens only (see file header). */
            long long out = cl_jll(line, "output_tokens");

            if (strcmp(rid, last_rid) != 0) {
                /* New requestId → commit buffered previous (last-entry-wins). */
                if (last_rid[0]) {
                    if (buf_ts >= t0_5h) cl_tok_5h += buf_out;
                    cl_tok_7d += buf_out;
                }
                strncpy(last_rid, rid, sizeof(last_rid) - 1);
                last_rid[sizeof(last_rid) - 1] = '\0';
            }
            buf_out = out;
            buf_ts  = ts;
        }

        /* commit final buffered entry */
        if (last_rid[0]) {
            if (buf_ts >= t0_5h) cl_tok_5h += buf_out;
            if (buf_ts >= t0_7d) cl_tok_7d += buf_out;
        }

        fclose(f);
    }

    free(line);
    globfree(&gl);
}

/* ── Init ────────────────────────────────────────────────────────────── */

void claude_screen_init(void) {
    cl_load_plan();
    cl_load_all();
    cl_last_scan = cl_last_screen = time(NULL);
}

/* Called from update_function BEFORE libg15_mutex to avoid holding the mutex
 * during slow file I/O (cl_load_all reads 40+ JSONL files, takes ~500ms).
 * Holding libg15_mutex during I/O blocked getPressedKeys() in key_function,
 * creating a race condition in libusb that manifested as segfault on L1. */
void claude_maybe_scan(void) {
    time_t now = time(NULL);
    if (now - cl_last_scan >= CL_SCAN_S) {
        cl_load_all();
        cl_last_scan = now;
    }
}

/* Format a token count compactly: 313000→"313K", 1900000→"1.9M", 42→"42". */
static void cl_fmt_tok(char *out, size_t sz, long long n) {
    if      (n >= 1000000) snprintf(out, sz, "%.1fM", n / 1e6);
    else if (n >= 1000)    snprintf(out, sz, "%lldK", n / 1000);
    else                   snprintf(out, sz, "%lld",  n);
}

/* ── Main screen function ─────────────────────────────────────────────── */

/* LCD layout (160×43px):
 *
 *  y= 1..10  MED  "CC Pro   14:23"       header: plan + time
 *  y=11..19  LARGE "5h" | bar(20..109)   (no gap after header)
 *  y=20..22  gap
 *  y=23..31  LARGE "7d" | bar(20..109)
 *  y=32..33  gap
 *  y=34..40  LARGE "5H:313K/965K 7D:1.9M/2.9M"
 *  y=41..42  gap  (moved from after header to after summary)
 *
 * Reset labels removed — Anthropic uses a fixed-interval timer, not a
 * sliding window, so displaying oldest-entry-based reset time was misleading.
 */
void claude_screen(lcd_t *lcd) {
    time_t now = time(NULL);

    /* Scan is done by claude_maybe_scan() outside libg15_mutex — don't call
     * cl_load_all() here. */
    if (now == cl_last_screen && lcd->ident != 0) return;
    cl_last_screen = now;

    g15canvas *canvas = malloc(sizeof(g15canvas));
    if (!canvas) return;

    memset(canvas->buffer, 0, G15_BUFFER_LEN);
    canvas->mode_cache   = 0;
    canvas->mode_reverse = 0;
    canvas->mode_xor     = 0;

    /* bar percentages clamped to 0-100 */
    int pct_5h = (cl_plan.lim_5h > 0) ? (int)(cl_tok_5h * 100 / cl_plan.lim_5h) : 0;
    int pct_7d = (cl_plan.lim_7d > 0) ? (int)(cl_tok_7d * 100 / cl_plan.lim_7d) : 0;
    if (pct_5h > 100) pct_5h = 100;
    if (pct_7d > 100) pct_7d = 100;

    /* ── y=1: header ────────────────────────────────────────────────── */
    {
        char tstr[8], hdr[24];
        struct tm *lt = localtime(&now);
        strftime(tstr, sizeof(tstr), "%H:%M", lt);
        snprintf(hdr, sizeof(hdr), "CC %-5s  %s", cl_plan.name, tstr);
        g15r_renderString(canvas, (unsigned char *)hdr, 0, G15_TEXT_MED, 1, 1);
    }

    /* ── y=11: Session (5h) row ─────────────────────────────────────── */
    g15r_renderString(canvas, (unsigned char *)"5h", 0, G15_TEXT_LARGE, 1,      11);
    g15r_drawBar(     canvas, BAR_X1, 11, BAR_X2, 19, G15_COLOR_BLACK, pct_5h, 100, 1);

    /* ── y=23: Weekly (7d) row ──────────────────────────────────────── */
    g15r_renderString(canvas, (unsigned char *)"7d", 0, G15_TEXT_LARGE, 1,      23);
    g15r_drawBar(     canvas, BAR_X1, 23, BAR_X2, 31, G15_COLOR_BLACK, pct_7d, 100, 1);

    /* ── y=34: summary — both windows ──────────────────────────────── */
    {
        char t5[7], l5[7], t7[7], l7[7], summary[40];
        cl_fmt_tok(t5, sizeof(t5), cl_tok_5h);
        cl_fmt_tok(l5, sizeof(l5), cl_plan.lim_5h);
        cl_fmt_tok(t7, sizeof(t7), cl_tok_7d);
        cl_fmt_tok(l7, sizeof(l7), cl_plan.lim_7d);
        snprintf(summary, sizeof(summary), "5H:%s/%s 7D:%s/%s", t5, l5, t7, l7);
        g15r_renderString(canvas, (unsigned char *)summary, 0, G15_TEXT_LARGE, 1, 34);
    }

    pthread_mutex_lock(&lcdlist_mutex);
    memcpy(lcd->buf, canvas->buffer, G15_BUFFER_LEN);
    lcd->ident = random();
    pthread_mutex_unlock(&lcdlist_mutex);

    free(canvas);
}
