/*
 * dashboard.c — OpenGL live dashboard for the Update Server
 *
 * Layout (800 x 600 window):
 *
 *  ┌────────────────────────────────────────────────────────────────┐
 *  │  HEADER BAR  — title, uptime, server stats (connections etc.)  │
 *  ├──────────────────────────┬─────────────────────────────────────┤
 *  │  THREAD POOL PANEL       │  CONNECTION TIMELINE                │
 *  │  one row per worker      │  bar-chart: last 60 s               │
 *  │  colour = state          │                                     │
 *  │  fill    = progress      │                                     │
 *  ├──────────────────────────┴─────────────────────────────────────┤
 *  │  LIVE LOG FEED  — last 10 lines, newest on top                 │
 *  └────────────────────────────────────────────────────────────────┘
 *
 * Compile deps:  -lGL -lGLU -lglut -lpthread
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

static int         g_pool_size  = 8;
static const char *g_log_path   = NULL;
static pthread_t   g_dash_tid;

/* window dimensions */
#define WIN_W  900
#define WIN_H  620

/* ------------------------------------------------------------------ */
/*  Colour palette                                                      */
/* ------------------------------------------------------------------ */

typedef struct { float r, g, b; } Color3;

static const Color3 C_BG         = {0.08f, 0.10f, 0.14f};  /* near-black */
static const Color3 C_PANEL      = {0.12f, 0.15f, 0.20f};  /* dark blue-grey */
static const Color3 C_BORDER     = {0.22f, 0.28f, 0.38f};
static const Color3 C_HEADER     = {0.10f, 0.13f, 0.20f};

static const Color3 C_IDLE       = {0.22f, 0.26f, 0.34f};
static const Color3 C_AUTH       = {0.95f, 0.75f, 0.20f};  /* amber       */
static const Color3 C_VERSION    = {0.30f, 0.70f, 0.95f};  /* sky blue    */
static const Color3 C_TRANSFER   = {0.35f, 0.90f, 0.55f};  /* green       */
static const Color3 C_DONE       = {0.55f, 0.55f, 0.65f};  /* grey-purple */

static const Color3 C_WHITE      = {1.0f, 1.0f, 1.0f};
static const Color3 C_GREY       = {0.55f, 0.60f, 0.68f};
static const Color3 C_ACCENT     = {0.40f, 0.70f, 1.00f};  /* bright blue */
static const Color3 C_WARN       = {1.00f, 0.45f, 0.25f};  /* orange-red  */

static void set_color(Color3 c) { glColor3f(c.r, c.g, c.b); }

/* ------------------------------------------------------------------ */
/*  2-D drawing helpers                                                 */
/* ------------------------------------------------------------------ */

static void fill_rect(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
      glVertex2f(x,     y);
      glVertex2f(x + w, y);
      glVertex2f(x + w, y + h);
      glVertex2f(x,     y + h);
    glEnd();
}

/* vertical gradient rect */
static void fill_rect_grad(float x, float y, float w, float h,
                            Color3 top, Color3 bot) {
    glBegin(GL_QUADS);
      glColor3f(bot.r, bot.g, bot.b); glVertex2f(x,     y);
      glColor3f(bot.r, bot.g, bot.b); glVertex2f(x + w, y);
      glColor3f(top.r, top.g, top.b); glVertex2f(x + w, y + h);
      glColor3f(top.r, top.g, top.b); glVertex2f(x,     y + h);
    glEnd();
}

static void stroke_rect(float x, float y, float w, float h) {
    glBegin(GL_LINE_LOOP);
      glVertex2f(x,     y);
      glVertex2f(x + w, y);
      glVertex2f(x + w, y + h);
      glVertex2f(x,     y + h);
    glEnd();
}

static void draw_string(float x, float y, const char *s, void *font) {
    glRasterPos2f(x, y);
    for (; *s; s++)
        glutBitmapCharacter(font, *s);
}

/* draw a filled rounded rectangle via GL_POLYGON (approximated) */
static void fill_rounded_rect(float x, float y, float w, float h, float r, int segs) {
    glBegin(GL_POLYGON);
    /* corners: BL, BR, TR, TL */
    float cx[4] = {x+r,   x+w-r, x+w-r, x+r  };
    float cy[4] = {y+r,   y+r,   y+h-r, y+h-r};
    float a0[4] = {(float)M_PI, (float)(3*M_PI/2), 0.0f, (float)(M_PI/2)};
    for (int c = 0; c < 4; c++) {
        for (int s = 0; s <= segs; s++) {
            float a = a0[c] + (float)M_PI/2 * s / segs;
            glVertex2f(cx[c] + r * cosf(a), cy[c] + r * sinf(a));
        }
    }
    glEnd();
}

/* ------------------------------------------------------------------ */
/*  State-to-colour mapping                                             */
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
/*  Section: HEADER BAR                                                 */
/* ------------------------------------------------------------------ */

static void draw_header(ServerStats *s, time_t now) {
    /* background gradient */
    Color3 hdr_top = {0.18f, 0.24f, 0.38f};
    Color3 hdr_bot = C_HEADER;
    fill_rect_grad(0, WIN_H - 64, WIN_W, 64, hdr_top, hdr_bot);

    /* title */
    set_color(C_WHITE);
    draw_string(14, WIN_H - 24, "UPDATE SERVER  —  LIVE DASHBOARD",
                GLUT_BITMAP_HELVETICA_18);

    /* uptime */
    long uptime = (long)(now - s->start_time);
    char upstr[64];
    snprintf(upstr, sizeof(upstr), "UPTIME  %02ld:%02ld:%02ld",
             uptime/3600, (uptime%3600)/60, uptime%60);
    set_color(C_GREY);
    draw_string(WIN_W - 180, WIN_H - 24, upstr, GLUT_BITMAP_HELVETICA_12);

    /* stat badges */
    typedef struct { const char *label; int value; Color3 col; } Badge;
    Badge badges[] = {
        { "ACTIVE",   s->active_connections, C_TRANSFER },
        { "SERVED",   s->total_served,       C_ACCENT   },
        { "UPDATED",  s->updates_sent,       C_VERSION  },
        { "UP-TO-DATE", s->up_to_date_count, C_GREY     },
        { "AUTH FAIL",s->auth_failures,      C_WARN     },
    };
    int nb = 5;
    float bw = 130.0f, bh = 26.0f, bx = 14.0f, by = WIN_H - 58.0f;
    for (int i = 0; i < nb; i++) {
        /* badge background */
        set_color(badges[i].col);
        glColor4f(badges[i].col.r, badges[i].col.g, badges[i].col.b, 0.18f);
        fill_rounded_rect(bx, by, bw - 6, bh, 4.0f, 6);

        /* border */
        set_color(badges[i].col);
        glLineWidth(1.2f);
        stroke_rect(bx, by, bw - 6, bh);

        /* text */
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %d", badges[i].label, badges[i].value);
        set_color(badges[i].col);
        draw_string(bx + 6, by + 8, buf, GLUT_BITMAP_HELVETICA_12);

        bx += bw;
    }
}

/* ------------------------------------------------------------------ */
/*  Section: THREAD POOL PANEL (left)                                   */
/* ------------------------------------------------------------------ */

#define TPANEL_X   10.0f
#define TPANEL_Y  200.0f
#define TPANEL_W  420.0f
#define TPANEL_H  350.0f

static void draw_thread_panel(ServerStats *s) {
    /* panel background */
    set_color(C_PANEL);
    fill_rect(TPANEL_X, TPANEL_Y, TPANEL_W, TPANEL_H);
    set_color(C_BORDER);
    stroke_rect(TPANEL_X, TPANEL_Y, TPANEL_W, TPANEL_H);

    /* section title */
    set_color(C_ACCENT);
    draw_string(TPANEL_X + 10, TPANEL_Y + TPANEL_H - 20,
                "THREAD POOL", GLUT_BITMAP_HELVETICA_12);

    /* active worker count subtitle */
    int active = 0;
    for (int i = 0; i < s->pool_size; i++)
        if (s->threads[i].state != THREAD_IDLE) active++;
    char subtitle[32];
    snprintf(subtitle, sizeof(subtitle), "%d / %d busy", active, s->pool_size);
    set_color(C_GREY);
    draw_string(TPANEL_X + TPANEL_W - 90, TPANEL_Y + TPANEL_H - 20,
                subtitle, GLUT_BITMAP_HELVETICA_12);

    int n = s->pool_size;
    if (n < 1) n = 1;
    if (n > MAX_THREADS) n = MAX_THREADS;

    float row_h   = (TPANEL_H - 36.0f) / n;
    float bar_h   = row_h * 0.55f;
    float bar_pad = row_h * 0.22f;
    float bar_x   = TPANEL_X + 70.0f;
    float bar_w   = TPANEL_W - 80.0f;

    for (int i = 0; i < n; i++) {
        ThreadInfo *t  = &s->threads[i];
        float y_center = TPANEL_Y + TPANEL_H - 36.0f - (i + 0.5f) * row_h;

        /* row background */
        set_color(C_HEADER);
        fill_rect(TPANEL_X + 2, y_center - bar_h / 2 - bar_pad,
                  TPANEL_W - 4, bar_h + bar_pad * 2);

        /* thread label */
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "T%02d", i);
        set_color(C_GREY);
        draw_string(TPANEL_X + 10, y_center - 5, lbl, GLUT_BITMAP_HELVETICA_12);

        /* track (background bar) */
        set_color(C_IDLE);
        fill_rect(bar_x, y_center - bar_h / 2, bar_w, bar_h);

        /* filled portion */
        Color3 col = thread_color(t->state);

        if (t->state == THREAD_IDLE) {
            /* subtle idle pulse using time */
            float pulse = 0.04f + 0.02f * sinf((float)time(NULL) * 1.5f + i);
            glColor3f(col.r + pulse, col.g + pulse, col.b + pulse);
        } else {
            set_color(col);
        }

        float fill = (t->state == THREAD_TRANSFERRING) ? t->progress : 1.0f;
        if (t->state == THREAD_IDLE) fill = 0.06f;
        fill_rect(bar_x, y_center - bar_h / 2, bar_w * fill, bar_h);

        /* progress % text for transfers */
        if (t->state == THREAD_TRANSFERRING) {
            char pct[16];
            snprintf(pct, sizeof(pct), "%d%%", (int)(t->progress * 100));
            set_color(C_WHITE);
            draw_string(bar_x + bar_w * fill + 4, y_center - 5,
                        pct, GLUT_BITMAP_HELVETICA_10);
        }

        /* state label */
        set_color(C_WHITE);
        draw_string(bar_x + 4, y_center - 5,
                    thread_state_str(t->state), GLUT_BITMAP_HELVETICA_10);

        /* client IP (greyed, right-aligned area) */
        if (t->state != THREAD_IDLE && t->client_ip[0]) {
            set_color(C_GREY);
            draw_string(bar_x + bar_w - 80, y_center - 5,
                        t->client_ip, GLUT_BITMAP_HELVETICA_10);
        }

        /* bar border */
        set_color(C_BORDER);
        glLineWidth(0.8f);
        stroke_rect(bar_x, y_center - bar_h / 2, bar_w, bar_h);
    }

    /* legend */
    Color3 legend_cols[] = {C_IDLE, C_AUTH, C_VERSION, C_TRANSFER, C_DONE};
    const char *legend_lbl[] = {"IDLE", "AUTH", "VERSION", "TRANSFER", "DONE"};
    float lx = TPANEL_X + 10.0f;
    float ly = TPANEL_Y + 6.0f;
    for (int i = 0; i < 5; i++) {
        set_color(legend_cols[i]);
        fill_rect(lx, ly, 10, 10);
        set_color(C_GREY);
        draw_string(lx + 13, ly + 1, legend_lbl[i], GLUT_BITMAP_HELVETICA_10);
        lx += 72.0f;
    }
}

/* ------------------------------------------------------------------ */
/*  Section: CONNECTION TIMELINE (right)                                */
/* ------------------------------------------------------------------ */

#define TLINE_X  445.0f
#define TLINE_Y  200.0f
#define TLINE_W  445.0f
#define TLINE_H  350.0f

static void draw_timeline(ServerStats *s) {
    set_color(C_PANEL);
    fill_rect(TLINE_X, TLINE_Y, TLINE_W, TLINE_H);
    set_color(C_BORDER);
    stroke_rect(TLINE_X, TLINE_Y, TLINE_W, TLINE_H);

    set_color(C_ACCENT);
    draw_string(TLINE_X + 10, TLINE_Y + TLINE_H - 20,
                "CONNECTIONS / SECOND  (last 60 s)", GLUT_BITMAP_HELVETICA_12);

    float inner_x = TLINE_X + 40.0f;
    float inner_y = TLINE_Y + 20.0f;
    float inner_w = TLINE_W - 50.0f;
    float inner_h = TLINE_H - 50.0f;

    /* find max for Y-scale */
    int max_val = 1;
    for (int i = 0; i < TIMELINE_LEN; i++)
        if (s->timeline[i] > max_val) max_val = s->timeline[i];

    /* grid lines */
    glLineWidth(0.5f);
    set_color(C_BORDER);
    for (int g = 1; g <= 4; g++) {
        float gy = inner_y + inner_h * g / 4.0f;
        glBegin(GL_LINES);
          glVertex2f(inner_x, gy);
          glVertex2f(inner_x + inner_w, gy);
        glEnd();
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", max_val * (4 - g) / 4);
        set_color(C_GREY);
        draw_string(TLINE_X + 4, gy - 5, buf, GLUT_BITMAP_HELVETICA_10);
    }

    /* bars */
    float bar_w = inner_w / (float)TIMELINE_LEN;
    for (int i = 0; i < TIMELINE_LEN; i++) {
        /* oldest is at (head), newest at (head-1) */
        int idx = (s->timeline_head + i) % TIMELINE_LEN;
        float val = (float)s->timeline[idx];
        float bh  = (val / max_val) * inner_h;
        float bx  = inner_x + i * bar_w;

        /* colour: interpolate from dark-blue to bright-green by height */
        float t = val / (float)max_val;
        glColor3f(0.25f + t * 0.10f, 0.60f + t * 0.30f, 0.55f + t * 0.10f);
        fill_rect(bx + 1, inner_y, bar_w - 2, bh);

        /* most recent bar highlight */
        if (i == TIMELINE_LEN - 1) {
            glColor4f(1.0f, 1.0f, 1.0f, 0.3f);
            stroke_rect(bx + 1, inner_y, bar_w - 2, bh);
        }
    }

    /* axis */
    set_color(C_BORDER);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
      glVertex2f(inner_x, inner_y);
      glVertex2f(inner_x, inner_y + inner_h);
      glVertex2f(inner_x, inner_y);
      glVertex2f(inner_x + inner_w, inner_y);
    glEnd();

    /* x labels: -60s, -45s, -30s, -15s, now */
    const char *x_lbl[] = {"-60s", "-45s", "-30s", "-15s", "now"};
    float x_pos[] = {0, 0.25f, 0.5f, 0.75f, 1.0f};
    for (int i = 0; i < 5; i++) {
        set_color(C_GREY);
        draw_string(inner_x + x_pos[i] * inner_w - 10,
                    inner_y - 14, x_lbl[i], GLUT_BITMAP_HELVETICA_10);
    }
}

/* ------------------------------------------------------------------ */
/*  Section: LIVE LOG FEED (bottom)                                     */
/* ------------------------------------------------------------------ */

#define LOG_X   10.0f
#define LOG_Y   10.0f
#define LOG_W   (WIN_W - 20.0f)
#define LOG_H   182.0f

static void draw_log_feed(ServerStats *s) {
    set_color(C_PANEL);
    fill_rect(LOG_X, LOG_Y, LOG_W, LOG_H);
    set_color(C_BORDER);
    stroke_rect(LOG_X, LOG_Y, LOG_W, LOG_H);

    set_color(C_ACCENT);
    draw_string(LOG_X + 10, LOG_Y + LOG_H - 20,
                "LIVE LOG FEED", GLUT_BITMAP_HELVETICA_12);

    int n = s->log_feed_count;
    if (n > LOG_FEED_LINES) n = LOG_FEED_LINES;

    float line_h = (LOG_H - 30.0f) / LOG_FEED_LINES;
    for (int i = 0; i < n; i++) {
        float ly = LOG_Y + LOG_H - 32.0f - i * line_h;

        /* newest line gets a subtle highlight */
        if (i == 0) {
            Color3 hl = {0.18f, 0.22f, 0.32f};
            set_color(hl);
            fill_rect(LOG_X + 2, ly - 2, LOG_W - 4, line_h);
        }

        /* colour by log level in the line */
        const char *line = s->log_feed[i];
        if (strstr(line, "ERROR"))
            set_color(C_WARN);
        else if (strstr(line, "WARN"))
            set_color(C_AUTH);   /* amber */
        else if (strstr(line, "Transfer complete") || strstr(line, "Update saved"))
            set_color(C_TRANSFER);
        else if (strstr(line, "Auth accepted"))
            set_color(C_VERSION);
        else
            set_color(C_GREY);

        /* truncate to fit */
        char buf[128];
        strncpy(buf, line, 127);
        buf[127] = '\0';
        /* trim newline */
        int len = (int)strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

        draw_string(LOG_X + 10, ly + 2, buf, GLUT_BITMAP_HELVETICA_10);
    }
}

/* ------------------------------------------------------------------ */
/*  GLUT callbacks                                                       */
/* ------------------------------------------------------------------ */

static void cb_display(void) {
    glClear(GL_COLOR_BUFFER_BIT);

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
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void cb_timer(int val) {
    /* advance timeline once per second */
    static time_t last_tick = 0;
    time_t now = time(NULL);
    if (now != last_tick) {
        last_tick = now;
        pthread_mutex_lock(&g_stats_mutex);
        g_stats.timeline[g_stats.timeline_head] = g_stats.active_connections;
        g_stats.timeline_head = (g_stats.timeline_head + 1) % TIMELINE_LEN;
        pthread_mutex_unlock(&g_stats_mutex);
    }

    glutPostRedisplay();
    glutTimerFunc(50, cb_timer, val);   /* ~20 fps */
}

/* ------------------------------------------------------------------ */
/*  Dashboard thread entry point                                         */
/* ------------------------------------------------------------------ */

static void *dashboard_thread_fn(void *arg) {
    (void)arg;

    int   fake_argc = 1;
    char *fake_argv = "server";
    glutInit(&fake_argc, &fake_argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("Update Server — Live Dashboard");

    glClearColor(C_BG.r, C_BG.g, C_BG.b, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);

    /* set up 2-D projection */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, WIN_W, 0, WIN_H);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glutDisplayFunc(cb_display);
    glutReshapeFunc(cb_reshape);
    glutTimerFunc(50, cb_timer, 0);

    glutMainLoop();   /* blocks forever — dashboard runs until window is closed */
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                           */
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
    /* detach — the window lives until the process exits */
    pthread_detach(g_dash_tid);
}

void dashboard_set_thread(int slot, ThreadState state, float progress,
                          const char *ip, uint32_t total, uint32_t sent) {
    if (slot < 0 || slot >= MAX_THREADS) return;
    pthread_mutex_lock(&g_stats_mutex);
    ThreadInfo *t = &g_stats.threads[slot];
    t->state       = state;
    t->progress    = progress;
    t->bytes_total = total;
    t->bytes_sent  = sent;
    if (ip) strncpy(t->client_ip, ip, sizeof(t->client_ip) - 1);
    pthread_mutex_unlock(&g_stats_mutex);
}

void dashboard_push_log(const char *line) {
    pthread_mutex_lock(&g_stats_mutex);
    /* shift entries down */
    for (int i = LOG_FEED_LINES - 1; i > 0; i--)
        memcpy(g_stats.log_feed[i], g_stats.log_feed[i-1], LOG_LINE_LEN);
    strncpy(g_stats.log_feed[0], line, LOG_LINE_LEN - 1);
    g_stats.log_feed[0][LOG_LINE_LEN - 1] = '\0';
    if (g_stats.log_feed_count < LOG_FEED_LINES)
        g_stats.log_feed_count++;
    pthread_mutex_unlock(&g_stats_mutex);
}

void dashboard_on_connect(void) {
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.active_connections++;
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
