/*
 * dashboard.c — OpenGL live dashboard for the Update Server
 * Fully responsive — all layout is relative to the actual window size.
 *
 * Layout proportions:
 *   Header bar    : top 11% of window height
 *   Middle row    : next 54%  (thread panel left 47% | timeline right 53%)
 *   Log feed      : bottom 32%
 */

#include "dashboard.h"
#include <GL/glut.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Globals                                                             */
/* ------------------------------------------------------------------ */

ServerStats     g_stats;
pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

static int         g_pool_size = 8;
static const char *g_log_path  = NULL;
static pthread_t   g_dash_tid;

/* live window size — updated in cb_reshape */
static int g_win_w = 960;
static int g_win_h = 680;

/* ------------------------------------------------------------------ */
/*  Colour palette                                                      */
/* ------------------------------------------------------------------ */

typedef struct { float r, g, b; } Color3;

static const Color3 C_BG       = {0.08f, 0.10f, 0.14f};
static const Color3 C_PANEL    = {0.12f, 0.15f, 0.20f};
static const Color3 C_BORDER   = {0.22f, 0.28f, 0.38f};
static const Color3 C_HEADER   = {0.10f, 0.13f, 0.20f};

static const Color3 C_IDLE     = {0.22f, 0.26f, 0.34f};
static const Color3 C_AUTH     = {0.95f, 0.75f, 0.20f};
static const Color3 C_VERSION  = {0.30f, 0.70f, 0.95f};
static const Color3 C_TRANSFER = {0.35f, 0.90f, 0.55f};
static const Color3 C_DONE     = {0.55f, 0.55f, 0.65f};

static const Color3 C_WHITE    = {1.0f,  1.0f,  1.0f };
static const Color3 C_GREY     = {0.55f, 0.60f, 0.68f};
static const Color3 C_ACCENT   = {0.40f, 0.70f, 1.00f};
static const Color3 C_WARN     = {1.00f, 0.45f, 0.25f};

static void set_color(Color3 c) { glColor3f(c.r, c.g, c.b); }

/* ------------------------------------------------------------------ */
/*  2-D drawing helpers                                                 */
/* ------------------------------------------------------------------ */

static void fill_rect(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
      glVertex2f(x,     y);
      glVertex2f(x+w,   y);
      glVertex2f(x+w,   y+h);
      glVertex2f(x,     y+h);
    glEnd();
}

static void fill_rect_grad(float x, float y, float w, float h,
                            Color3 top, Color3 bot) {
    glBegin(GL_QUADS);
      glColor3f(bot.r, bot.g, bot.b); glVertex2f(x,   y);
      glColor3f(bot.r, bot.g, bot.b); glVertex2f(x+w, y);
      glColor3f(top.r, top.g, top.b); glVertex2f(x+w, y+h);
      glColor3f(top.r, top.g, top.b); glVertex2f(x,   y+h);
    glEnd();
}

static void stroke_rect(float x, float y, float w, float h) {
    glBegin(GL_LINE_LOOP);
      glVertex2f(x,   y);
      glVertex2f(x+w, y);
      glVertex2f(x+w, y+h);
      glVertex2f(x,   y+h);
    glEnd();
}

/* draw string at (x,y) in window coords (bottom-left origin) */
static void draw_str(float x, float y, const char *s, void *font) {
    glRasterPos2f(x, y);
    for (; *s; s++) glutBitmapCharacter(font, *s);
}

/* pixel width of a string for a given GLUT bitmap font */
static int str_width(const char *s, void *font) {
    int w = 0;
    for (; *s; s++) w += glutBitmapWidth(font, *s);
    return w;
}

/* ------------------------------------------------------------------ */
/*  State helpers                                                       */
/* ------------------------------------------------------------------ */

static Color3 thread_color(ThreadState st) {
    switch (st) {
        case THREAD_IDLE:          return C_IDLE;
        case THREAD_AUTH:          return C_AUTH;
        case THREAD_VERSION_CHECK: return C_VERSION;
        case THREAD_TRANSFERRING:  return C_TRANSFER;
        case THREAD_DONE:          return C_DONE;
    }
    return C_IDLE;
}

static const char *thread_state_str(ThreadState st) {
    switch (st) {
        case THREAD_IDLE:          return "IDLE";
        case THREAD_AUTH:          return "AUTH";
        case THREAD_VERSION_CHECK: return "VERSION";
        case THREAD_TRANSFERRING:  return "TRANSFER";
        case THREAD_DONE:          return "DONE";
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/*  Layout — computed fresh every frame from g_win_w / g_win_h         */
/* ------------------------------------------------------------------ */

#define PAD       8.0f   /* outer padding */
#define GAP       6.0f   /* gap between panels */

/* header occupies top 11% */
#define HDR_H     (g_win_h * 0.11f)

/* log feed occupies bottom 30% */
#define LOG_H     (g_win_h * 0.30f)

/* middle row fills the rest */
#define MID_Y     (LOG_H + GAP)
#define MID_H     (g_win_h - HDR_H - LOG_H - GAP * 2)

/* thread panel: left 47% of middle row */
#define TP_X      PAD
#define TP_Y      MID_Y
#define TP_W      ((g_win_w - PAD*2 - GAP) * 0.47f)
#define TP_H      MID_H

/* timeline: right 53% of middle row */
#define TL_X      (TP_X + TP_W + GAP)
#define TL_Y      MID_Y
#define TL_W      (g_win_w - TL_X - PAD)
#define TL_H      MID_H

/* log panel */
#define LG_X      PAD
#define LG_Y      PAD
#define LG_W      (g_win_w - PAD*2)
/* LG_H = LOG_H - PAD (bottom gap) */

/* ------------------------------------------------------------------ */
/*  HEADER                                                              */
/* ------------------------------------------------------------------ */

static void draw_header(ServerStats *s, time_t now) {
    float hx = 0, hy = g_win_h - HDR_H, hw = g_win_w, hh = HDR_H;

    Color3 htop = {0.18f, 0.24f, 0.38f};
    fill_rect_grad(hx, hy, hw, hh, htop, C_HEADER);

    /* title — vertically centred in top half of header */
    float title_y = hy + hh * 0.60f;
    set_color(C_WHITE);
    draw_str(PAD + 4, title_y, "UPDATE SERVER  --  LIVE DASHBOARD",
             GLUT_BITMAP_HELVETICA_18);

    /* uptime — right side */
    long uptime = (long)(now - s->start_time);
    char upstr[48];
    snprintf(upstr, sizeof(upstr), "UPTIME  %02ld:%02ld:%02ld",
             uptime/3600, (uptime%3600)/60, uptime%60);
    set_color(C_GREY);
    float uw = (float)str_width(upstr, GLUT_BITMAP_HELVETICA_12);
    draw_str(hw - uw - PAD*2, title_y, upstr, GLUT_BITMAP_HELVETICA_12);

    /* stat badges — bottom half of header, evenly spaced */
    typedef struct { const char *label; int value; Color3 col; } Badge;
    Badge badges[] = {
        { "ACTIVE",      s->active_connections, C_TRANSFER },
        { "SERVED",      s->total_served,       C_ACCENT   },
        { "UPDATED",     s->updates_sent,       C_VERSION  },
        { "UP-TO-DATE",  s->up_to_date_count,   C_GREY     },
        { "AUTH FAIL",   s->auth_failures,      C_WARN     },
    };
    int nb = 5;
    float badge_h  = hh * 0.38f;
    float badge_y  = hy + 4.0f;
    float total_bw = hw - PAD * 2;
    float bw       = total_bw / nb;

    for (int i = 0; i < nb; i++) {
        float bx = PAD + i * bw;
        float bw2 = bw - 6.0f;

        /* tinted background */
        glColor4f(badges[i].col.r, badges[i].col.g, badges[i].col.b, 0.15f);
        fill_rect(bx, badge_y, bw2, badge_h);

        /* border */
        set_color(badges[i].col);
        glLineWidth(1.2f);
        stroke_rect(bx, badge_y, bw2, badge_h);

        /* label + value */
        char buf[40];
        snprintf(buf, sizeof(buf), "%s: %d", badges[i].label, badges[i].value);
        float tx = bx + (bw2 - str_width(buf, GLUT_BITMAP_HELVETICA_12)) / 2.0f;
        draw_str(tx, badge_y + badge_h * 0.28f, buf, GLUT_BITMAP_HELVETICA_12);
    }
}

/* ------------------------------------------------------------------ */
/*  THREAD POOL PANEL                                                   */
/* ------------------------------------------------------------------ */

static void draw_thread_panel(ServerStats *s) {
    set_color(C_PANEL);
    fill_rect(TP_X, TP_Y, TP_W, TP_H);
    set_color(C_BORDER);
    stroke_rect(TP_X, TP_Y, TP_W, TP_H);

    /* section title */
    float title_y = TP_Y + TP_H - HDR_H * 0.45f;
    set_color(C_ACCENT);
    draw_str(TP_X + PAD, title_y, "THREAD POOL", GLUT_BITMAP_HELVETICA_12);

    int active = 0;
    int n = s->pool_size;
    if (n < 1) n = 1;
    if (n > MAX_THREADS) n = MAX_THREADS;
    for (int i = 0; i < n; i++)
        if (s->threads[i].state != THREAD_IDLE) active++;

    char subtitle[32];
    snprintf(subtitle, sizeof(subtitle), "%d / %d busy", active, n);
    float sw = (float)str_width(subtitle, GLUT_BITMAP_HELVETICA_12);
    set_color(C_GREY);
    draw_str(TP_X + TP_W - sw - PAD, title_y, subtitle, GLUT_BITMAP_HELVETICA_12);

    /* legend row */
    float leg_h   = 12.0f;
    float leg_y   = TP_Y + PAD + 2.0f;
    float leg_x   = TP_X + PAD;
    Color3 lcols[] = {C_IDLE, C_AUTH, C_VERSION, C_TRANSFER, C_DONE};
    const char *llbl[] = {"IDLE","AUTH","VERSION","TRANSFER","DONE"};
    float leg_spacing = (TP_W - PAD*2) / 5.0f;
    for (int i = 0; i < 5; i++) {
        set_color(lcols[i]);
        fill_rect(leg_x + i*leg_spacing, leg_y, leg_h, leg_h);
        set_color(C_GREY);
        draw_str(leg_x + i*leg_spacing + leg_h + 3, leg_y + 1,
                 llbl[i], GLUT_BITMAP_HELVETICA_10);
    }

    /* rows area */
    float rows_top = TP_Y + TP_H - HDR_H * 0.45f - 6.0f;
    float rows_bot = leg_y + leg_h + 4.0f;
    float rows_h   = rows_top - rows_bot;
    float row_h    = rows_h / (float)n;
    float bar_h    = row_h * 0.52f;
    float label_w  = 36.0f;
    float bar_x    = TP_X + label_w + PAD*2;
    float bar_w    = TP_W - label_w - PAD*3;

    for (int i = 0; i < n; i++) {
        ThreadInfo *t    = &s->threads[i];
        /* draw from top downward so T00 is at top */
        float row_top    = rows_top - i * row_h;
        float y_center   = row_top - row_h / 2.0f;

        /* row background */
        set_color(C_HEADER);
        fill_rect(TP_X + 2, y_center - bar_h/2 - 2, TP_W - 4, bar_h + 4);

        /* thread label */
        char lbl[8];
        snprintf(lbl, sizeof(lbl), "T%02d", i);
        set_color(C_GREY);
        draw_str(TP_X + PAD, y_center - 5, lbl, GLUT_BITMAP_HELVETICA_12);

        /* track */
        set_color(C_IDLE);
        fill_rect(bar_x, y_center - bar_h/2, bar_w, bar_h);

        /* fill */
        Color3 col = thread_color(t->state);
        if (t->state == THREAD_IDLE) {
            float pulse = 0.03f * sinf((float)time(NULL)*1.5f + i);
            glColor3f(col.r+pulse, col.g+pulse, col.b+pulse);
        } else {
            set_color(col);
        }
        float fill = (t->state == THREAD_TRANSFERRING) ? t->progress
                   : (t->state == THREAD_IDLE)         ? 0.05f
                   :                                     1.0f;
        fill_rect(bar_x, y_center - bar_h/2, bar_w * fill, bar_h);

        /* state text inside bar */
        set_color(C_WHITE);
        draw_str(bar_x + 4, y_center - 5,
                 thread_state_str(t->state), GLUT_BITMAP_HELVETICA_10);

        /* progress % for transfers */
        if (t->state == THREAD_TRANSFERRING) {
            char pct[8];
            snprintf(pct, sizeof(pct), "%d%%", (int)(t->progress*100));
            float px = bar_x + bar_w * fill + 3;
            if (px + 28 < bar_x + bar_w)
                draw_str(px, y_center - 5, pct, GLUT_BITMAP_HELVETICA_10);
        }

        /* client IP right-aligned */
        if (t->state != THREAD_IDLE && t->client_ip[0]) {
            float ipw = (float)str_width(t->client_ip, GLUT_BITMAP_HELVETICA_10);
            set_color(C_GREY);
            draw_str(bar_x + bar_w - ipw - 2, y_center - 5,
                     t->client_ip, GLUT_BITMAP_HELVETICA_10);
        }

        /* bar border */
        set_color(C_BORDER);
        glLineWidth(0.8f);
        stroke_rect(bar_x, y_center - bar_h/2, bar_w, bar_h);
    }
}

/* ------------------------------------------------------------------ */
/*  CONNECTION TIMELINE                                                 */
/* ------------------------------------------------------------------ */

static void draw_timeline(ServerStats *s) {
    set_color(C_PANEL);
    fill_rect(TL_X, TL_Y, TL_W, TL_H);
    set_color(C_BORDER);
    stroke_rect(TL_X, TL_Y, TL_W, TL_H);

    float title_y = TL_Y + TL_H - HDR_H * 0.45f;
    set_color(C_ACCENT);
    draw_str(TL_X + PAD, title_y,
             "CONNECTIONS / SECOND  (last 60 s)", GLUT_BITMAP_HELVETICA_12);

    float ix = TL_X + 32.0f;
    float iy = TL_Y + PAD + 16.0f;
    float iw = TL_W - 40.0f;
    float ih = TL_H - HDR_H*0.45f - PAD*2 - 16.0f;

    /* find max */
    int max_val = 1;
    for (int i = 0; i < TIMELINE_LEN; i++)
        if (s->timeline[i] > max_val) max_val = s->timeline[i];

    /* grid lines */
    glLineWidth(0.5f);
    set_color(C_BORDER);
    for (int g = 1; g <= 4; g++) {
        float gy = iy + ih * g / 4.0f;
        glBegin(GL_LINES);
          glVertex2f(ix, gy); glVertex2f(ix+iw, gy);
        glEnd();
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", max_val*(4-g)/4);
        set_color(C_GREY);
        draw_str(TL_X + 4, gy - 5, buf, GLUT_BITMAP_HELVETICA_10);
    }

    /* bars */
    float bw = iw / (float)TIMELINE_LEN;
    for (int i = 0; i < TIMELINE_LEN; i++) {
        int idx = (s->timeline_head + i) % TIMELINE_LEN;
        float val = (float)s->timeline[idx];
        float bh  = (val / max_val) * ih;
        float bx  = ix + i * bw;
        float t   = val / (float)max_val;
        glColor3f(0.25f + t*0.10f, 0.55f + t*0.35f, 0.55f + t*0.10f);
        fill_rect(bx+1, iy, bw-2, bh);
    }

    /* axes */
    set_color(C_BORDER);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
      glVertex2f(ix, iy); glVertex2f(ix, iy+ih);
      glVertex2f(ix, iy); glVertex2f(ix+iw, iy);
    glEnd();

    /* x labels */
    const char *xlbl[] = {"-60s","-45s","-30s","-15s","now"};
    float xpos[]       = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
    for (int i = 0; i < 5; i++) {
        set_color(C_GREY);
        float lx = ix + xpos[i]*iw - str_width(xlbl[i],GLUT_BITMAP_HELVETICA_10)/2.0f;
        draw_str(lx, iy - 14, xlbl[i], GLUT_BITMAP_HELVETICA_10);
    }
}

/* ------------------------------------------------------------------ */
/*  LIVE LOG FEED                                                       */
/* ------------------------------------------------------------------ */

static void draw_log_feed(ServerStats *s) {
    float lx = LG_X, ly = LG_Y, lw = LG_W, lh = LOG_H - PAD;

    set_color(C_PANEL);
    fill_rect(lx, ly, lw, lh);
    set_color(C_BORDER);
    stroke_rect(lx, ly, lw, lh);

    float title_y = ly + lh - HDR_H*0.45f;
    set_color(C_ACCENT);
    draw_str(lx + PAD, title_y, "LIVE LOG FEED", GLUT_BITMAP_HELVETICA_12);

    int n = s->log_feed_count;
    if (n > LOG_FEED_LINES) n = LOG_FEED_LINES;

    /* distribute lines evenly inside the feed area */
    float feed_top = title_y - 6.0f;
    float feed_bot = ly + 4.0f;
    float avail    = feed_top - feed_bot;
    float line_h   = (LOG_FEED_LINES > 0) ? avail / LOG_FEED_LINES : 14.0f;

    for (int i = 0; i < n; i++) {
        float line_y = feed_top - (i + 1) * line_h + line_h * 0.25f;

        /* highlight newest */
        if (i == 0) {
            Color3 hl = {0.18f, 0.22f, 0.32f};
            set_color(hl);
            fill_rect(lx+2, line_y - 2, lw-4, line_h);
        }

        const char *line = s->log_feed[i];
        if      (strstr(line, "ERROR"))             set_color(C_WARN);
        else if (strstr(line, "WARN"))              set_color(C_AUTH);
        else if (strstr(line, "Transfer complete")
              || strstr(line, "Update saved"))      set_color(C_TRANSFER);
        else if (strstr(line, "Auth accepted"))     set_color(C_VERSION);
        else                                        set_color(C_GREY);

        /* truncate to fit width */
        char buf[LOG_LINE_LEN];
        strncpy(buf, line, LOG_LINE_LEN-1);
        buf[LOG_LINE_LEN-1] = '\0';
        int len = (int)strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

        /* clip string to panel width */
        void *font = GLUT_BITMAP_HELVETICA_10;
        while (strlen(buf) > 4 &&
               str_width(buf, font) > (int)(lw - PAD*2))
            buf[strlen(buf)-1] = '\0';

        draw_str(lx + PAD, line_y, buf, font);
    }
}

/* ------------------------------------------------------------------ */
/*  GLUT callbacks                                                      */
/* ------------------------------------------------------------------ */

static void cb_display(void) {
    glClear(GL_COLOR_BUFFER_BIT);

    /* reset projection to current window size */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, g_win_w, 0, g_win_h);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    ServerStats snap;
    pthread_mutex_lock(&g_stats_mutex);
    memcpy(&snap, &g_stats, sizeof(snap));
    pthread_mutex_unlock(&g_stats_mutex);

    time_t now = time(NULL);
    draw_header(&snap, now);
    draw_thread_panel(&snap);
    draw_timeline(&snap);
    draw_log_feed(&snap);

    glutSwapBuffers();
}

static void cb_reshape(int w, int h) {
    if (h == 0) h = 1;
    g_win_w = w;
    g_win_h = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void cb_timer(int val) {
    static time_t last_tick = 0;
    time_t now = time(NULL);
    if (now != last_tick) {
        last_tick = now;
        pthread_mutex_lock(&g_stats_mutex);
        g_stats.timeline[g_stats.timeline_head] = g_stats.connections_this_second;
        g_stats.connections_this_second = 0;
        g_stats.timeline_head = (g_stats.timeline_head + 1) % TIMELINE_LEN;
        pthread_mutex_unlock(&g_stats_mutex);
    }
    glutPostRedisplay();
    glutTimerFunc(50, cb_timer, val);
}

/* ------------------------------------------------------------------ */
/*  Dashboard thread                                                    */
/* ------------------------------------------------------------------ */

static void *dashboard_thread_fn(void *arg) {
    (void)arg;
    int   fake_argc = 1;
    char *fake_argv = "server";
    glutInit(&fake_argc, &fake_argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(g_win_w, g_win_h);
    glutCreateWindow("Update Server -- Live Dashboard");

    glClearColor(C_BG.r, C_BG.g, C_BG.b, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glutDisplayFunc(cb_display);
    glutReshapeFunc(cb_reshape);   /* <-- this is what makes it responsive */
    glutTimerFunc(50, cb_timer, 0);
    glutMainLoop();
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void dashboard_init(int pool_size, const char *log_path) {
    pthread_mutex_lock(&g_stats_mutex);
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.pool_size  = pool_size;
    g_stats.start_time = time(NULL);
    pthread_mutex_unlock(&g_stats_mutex);
    g_pool_size = pool_size;
    g_log_path  = log_path;
}

void dashboard_start(void) {
    pthread_create(&g_dash_tid, NULL, dashboard_thread_fn, NULL);
    pthread_detach(g_dash_tid);
}

void dashboard_set_thread(int slot, ThreadState state, float progress,
                          const char *ip, uint32_t total, uint32_t sent) {
    if (slot < 0 || slot >= MAX_THREADS) return;
    pthread_mutex_lock(&g_stats_mutex);
    ThreadInfo *t  = &g_stats.threads[slot];
    t->state       = state;
    t->progress    = progress;
    t->bytes_total = total;
    t->bytes_sent  = sent;
    if (ip) strncpy(t->client_ip, ip, sizeof(t->client_ip)-1);
    pthread_mutex_unlock(&g_stats_mutex);
}

void dashboard_push_log(const char *line) {
    pthread_mutex_lock(&g_stats_mutex);
    for (int i = LOG_FEED_LINES-1; i > 0; i--)
        memcpy(g_stats.log_feed[i], g_stats.log_feed[i-1], LOG_LINE_LEN);
    strncpy(g_stats.log_feed[0], line, LOG_LINE_LEN-1);
    g_stats.log_feed[0][LOG_LINE_LEN-1] = '\0';
    if (g_stats.log_feed_count < LOG_FEED_LINES)
        g_stats.log_feed_count++;
    pthread_mutex_unlock(&g_stats_mutex);
}

void dashboard_on_connect(void) {
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.active_connections++;
    g_stats.connections_this_second++;
    g_stats.total_served++;
    pthread_mutex_unlock(&g_stats_mutex);
}

void dashboard_on_disconnect(int update_was_sent) {
    pthread_mutex_lock(&g_stats_mutex);
    if (g_stats.active_connections > 0)
        g_stats.active_connections--;
    if (update_was_sent)
        g_stats.updates_sent++;
    else
        g_stats.up_to_date_count++;
    pthread_mutex_unlock(&g_stats_mutex);
}