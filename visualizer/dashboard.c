/*
 * dashboard.c — OpenGL live dashboard for the Update Server
 *
 * Visual design based on the card-based dark UI mockup:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  Header: title · port · version badge · status dot      │
 *   ├──────────┬──────────┬────────────┬───────────────────────┤
 *   │ stat     │ stat     │ stat       │ stat                   │
 *   ├──────────────────────┬───────────────────────────────────┤
 *   │  Active Clients      │  Event Log                        │
 *   │  (client cards)      │  (timestamped rows)               │
 *   ├──────────────────────┴───────────────────────────────────┤
 *   │  Client View (focused client detail)                     │
 *   └─────────────────────────────────────────────────────────┘
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

ServerStats g_stats;
pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

static int g_pool_size = 8;
static const char *g_log_path = NULL;
static pthread_t g_dash_tid;
static const char *g_test_state_path = "/tmp/update_test_state.txt";

typedef struct
{
    const char *label;
    const char *target;
} TestScenario;

static const TestScenario g_test_scenarios[] = {
    {"TC1 Outdated", "test-outdated"},
    {"TC2 Up-to-date", "test-uptodate"},
    {"TC3 Multi", "test-multi"},
    {"TC4 Mixed", "test-mixed"},
    {"TC5 Bad auth", "test-badauth"},
    {"TC6 Resume", "test-resume"},
    {"TC7 Large file", "test-largefile"},
    {"TC8 Queue", "test-poolexhaust"},
    {"TC9 Future", "test-future"},
};

static int g_win_w = 1100;
static int g_win_h = 780;

/* ------------------------------------------------------------------ */
/*  Colour Palette  (matches screenshot dark card theme)               */
/* ------------------------------------------------------------------ */

typedef struct
{
    float r, g, b, a;
} Color4;
typedef struct
{
    float r, g, b;
} Color3;

/* Backgrounds */
static const Color3 C_BG = {0.09f, 0.10f, 0.11f};    /* #171819 */
static const Color3 C_CARD = {0.13f, 0.14f, 0.16f};  /* #202428 */
static const Color3 C_CARD2 = {0.10f, 0.12f, 0.14f}; /* inner card */
static const Color3 C_SEP = {0.20f, 0.22f, 0.25f};   /* separator */

/* Text */
static const Color3 C_WHITE = {0.95f, 0.96f, 0.97f};
static const Color3 C_MUTED = {0.50f, 0.54f, 0.60f};
static const Color3 C_DIM = {0.33f, 0.36f, 0.40f};

/* Accent / status */
static const Color3 C_GREEN = {0.27f, 0.80f, 0.44f};  /* running / done */
static const Color3 C_ORANGE = {0.95f, 0.60f, 0.20f}; /* v1.0 / v1.5 / sending */
static const Color3 C_BLUE = {0.35f, 0.65f, 0.95f};   /* CONN badge */
static const Color3 C_LIME = {0.55f, 0.85f, 0.30f};   /* threads used */
static const Color3 C_TEAL = {0.25f, 0.75f, 0.70f};   /* updates sent */
static const Color3 C_WARN = {0.90f, 0.35f, 0.25f};   /* error */

/* Badge background alphas drawn manually */
static void set_color3(Color3 c) { glColor3f(c.r, c.g, c.b); }
static void set_color4(Color3 c, float alpha) { glColor4f(c.r, c.g, c.b, alpha); }

/* ------------------------------------------------------------------ */
/*  Drawing helpers                                                     */
/* ------------------------------------------------------------------ */

static void fill_rect(float x, float y, float w, float h)
{
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void stroke_rect_lw(float x, float y, float w, float h, float lw)
{
    glLineWidth(lw);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

/* Rounded rectangle approximation using GL_POLYGON */
static void fill_rounded(float x, float y, float w, float h, float r)
{
    /* Clamp radius */
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    int segs = 8;
    glBegin(GL_POLYGON);
    for (int c = 0; c < 4; c++)
    {
        float cx = (c == 0 || c == 3) ? x + r : x + w - r;
        float cy = (c == 0 || c == 1) ? y + r : y + h - r;
        float a0 = (float)(c * 90 + 180) * 3.14159f / 180.0f;
        for (int i = 0; i <= segs; i++)
        {
            float a = a0 + i * (3.14159f / 2.0f / segs);
            glVertex2f(cx + r * cosf(a), cy + r * sinf(a));
        }
    }
    glEnd();
}

static void draw_str(float x, float y, const char *s, void *font)
{
    glRasterPos2f(x, y);
    for (; *s; s++)
        glutBitmapCharacter(font, *s);
}

static int str_width(const char *s, void *font)
{
    int w = 0;
    for (; *s; s++)
        w += glutBitmapWidth(font, *s);
    return w;
}

/* Draw a filled pill label with text */
static void draw_pill(float x, float y, float h, const char *text,
                      Color3 text_col, Color3 bg_col, float bg_alpha, void *font)
{
    float pad = h * 0.5f;
    float tw = (float)str_width(text, font);
    float pw = tw + pad * 2.0f;
    set_color4(bg_col, bg_alpha);
    glEnable(GL_BLEND);
    fill_rounded(x, y, pw, h, h * 0.4f);
    set_color4(bg_col, 0.55f);
    /* thin border */
    glLineWidth(0.8f);
    /* skip stroke for cleaner look */
    set_color3(text_col);
    draw_str(x + pad, y + h * 0.25f, text, font);
}

/* Horizontal progress bar with track */
static void draw_progress_bar(float x, float y, float w, float h,
                              float progress, Color3 fill_col)
{
    /* track */
    set_color4(C_SEP, 0.6f);
    glEnable(GL_BLEND);
    fill_rounded(x, y, w, h, h * 0.5f);
    /* fill */
    if (progress > 0.0f)
    {
        float fw = w * progress;
        if (fw < h)
            fw = h;
        set_color3(fill_col);
        fill_rounded(x, y, fw, h, h * 0.5f);
    }
}

/* Divider line */
static void draw_hline(float x, float y, float w)
{
    set_color4(C_SEP, 0.5f);
    glEnable(GL_BLEND);
    glLineWidth(0.6f);
    glBegin(GL_LINES);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glEnd();
}

/* Small dot indicator */
static void draw_dot(float cx, float cy, float r, Color3 col)
{
    set_color3(col);
    int segs = 12;
    glBegin(GL_POLYGON);
    for (int i = 0; i < segs; i++)
    {
        float a = i * 2.0f * 3.14159f / segs;
        glVertex2f(cx + r * cosf(a), cy + r * sinf(a));
    }
    glEnd();
}

/* ------------------------------------------------------------------ */
/*  State helpers                                                       */
/* ------------------------------------------------------------------ */

static Color3 version_pill_color(uint32_t ver, uint32_t latest)
{
    if (ver >= latest)
        return C_GREEN;
    return C_ORANGE;
}

static Color3 status_badge_color(ThreadState st)
{
    switch (st)
    {
    case THREAD_TRANSFERRING:
        return C_ORANGE;
    case THREAD_DONE:
        return C_GREEN;
    case THREAD_AUTH:
        return C_BLUE;
    case THREAD_VERSION_CHECK:
        return C_BLUE;
    default:
        return C_MUTED;
    }
}

static const char *status_badge_str(ThreadState st)
{
    switch (st)
    {
    case THREAD_IDLE:
        return "idle";
    case THREAD_AUTH:
        return "auth";
    case THREAD_VERSION_CHECK:
        return "checking";
    case THREAD_TRANSFERRING:
        return "sending";
    case THREAD_DONE:
        return "done";
    }
    return "idle";
}

/* ------------------------------------------------------------------ */
/*  Layout constants                                                    */
/* ------------------------------------------------------------------ */

#define PAD 12.0f
#define GAP 10.0f
#define R_CARD 8.0f /* card corner radius */

/* Header: top 10% */
#define HDR_H (g_win_h * 0.10f)

/* Stat row: below header, 9% */
#define STAT_Y (g_win_h - HDR_H - g_win_h * 0.09f)
#define STAT_H (g_win_h * 0.09f)

/* Middle section: two panels side by side */
#define MID_TOP (g_win_h * 0.34f)
#define MID_BOT (STAT_Y - GAP)
#define MID_H (MID_BOT - MID_TOP)
#define MID_Y MID_TOP

/* Left panel: Active clients ~47% width */
#define LP_X PAD
#define LP_Y MID_Y
#define LP_W ((g_win_w - PAD * 2 - GAP) * 0.47f)
#define LP_H MID_H

/* Right panel: Event log */
#define RP_X (LP_X + LP_W + GAP)
#define RP_Y MID_Y
#define RP_W (g_win_w - RP_X - PAD)
#define RP_H MID_H

/* Bottom panel: Client View */
#define CV_X PAD
#define CV_Y PAD
#define CV_W (g_win_w - PAD * 2)
#define CV_H (MID_Y - PAD - GAP)

/* ------------------------------------------------------------------ */
/*  Log event ring buffer                                               */
/* ------------------------------------------------------------------ */

#define EV_MAX 32
#define EV_MSG 96

typedef enum
{
    EV_INFO,
    EV_CONN,
    EV_UPDT,
    EV_OK,
    EV_DONE,
    EV_WARN
} EvType;

typedef struct
{
    EvType type;
    char time_str[12]; /* "HH:MM:SS" */
    char msg[EV_MSG];
} LogEvent;

static LogEvent g_events[EV_MAX];
static int g_ev_count = 0;
static int g_ev_head = 0;
static pthread_mutex_t g_ev_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Determine event type from log line */
static EvType classify_event(const char *line)
{
    if (strstr(line, "connected"))
        return EV_CONN;
    if (strstr(line, "sending") ||
        strstr(line, "outdated") ||
        strstr(line, "Transfer"))
        return EV_UPDT;
    if (strstr(line, "up to date") ||
        strstr(line, "up-to-date") ||
        strstr(line, "DONE") ||
        strstr(line, "complete"))
        return EV_DONE;
    if (strstr(line, "Auth accepted") ||
        strstr(line, "OK") ||
        strstr(line, "up to date"))
        return EV_OK;
    if (strstr(line, "ERROR") ||
        strstr(line, "WARN") ||
        strstr(line, "failed"))
        return EV_WARN;
    return EV_INFO;
}

void dashboard_push_log(const char *line)
{
    /* Push into g_stats log feed (existing) */
    pthread_mutex_lock(&g_stats_mutex);
    for (int i = LOG_FEED_LINES - 1; i > 0; i--)
        memcpy(g_stats.log_feed[i], g_stats.log_feed[i - 1], LOG_LINE_LEN);
    strncpy(g_stats.log_feed[0], line, LOG_LINE_LEN - 1);
    g_stats.log_feed[0][LOG_LINE_LEN - 1] = '\0';
    if (g_stats.log_feed_count < LOG_FEED_LINES)
        g_stats.log_feed_count++;
    pthread_mutex_unlock(&g_stats_mutex);

    /* Also push into event ring */
    pthread_mutex_lock(&g_ev_mutex);
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    LogEvent *ev = &g_events[g_ev_head % EV_MAX];
    snprintf(ev->time_str, sizeof(ev->time_str), "%02d:%02d:%02d",
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    ev->type = classify_event(line);
    /* Strip log prefix "[SERVER] " etc for cleaner display */
    const char *msg = line;
    const char *bracket = strstr(line, "] ");
    if (bracket)
        msg = bracket + 2;
    strncpy(ev->msg, msg, EV_MSG - 1);
    ev->msg[EV_MSG - 1] = '\0';
    /* Remove trailing newline */
    int mlen = (int)strlen(ev->msg);
    if (mlen > 0 && ev->msg[mlen - 1] == '\n')
        ev->msg[mlen - 1] = '\0';
    g_ev_head++;
    if (g_ev_count < EV_MAX)
        g_ev_count++;
    pthread_mutex_unlock(&g_ev_mutex);
}

/* ------------------------------------------------------------------ */
/*  Track client versions for display                                   */
/* ------------------------------------------------------------------ */

/* We piggyback on thread state: track last known client version per slot */
static uint32_t g_slot_version[MAX_THREADS];

/* ------------------------------------------------------------------ */
/*  HEADER                                                              */
/* ------------------------------------------------------------------ */

static void refresh_current_test_state(void)
{
    char number[32] = "";
    char name[96] = "";
    char label[160] = "";

    FILE *fp = fopen(g_test_state_path, "r");
    if (fp)
    {
        if (fgets(number, sizeof(number), fp))
        {
            while (number[0] != '\0' && (number[strlen(number) - 1] == '\n' ||
                                         number[strlen(number) - 1] == '\r'))
                number[strlen(number) - 1] = '\0';
        }
        if (fgets(name, sizeof(name), fp))
        {
            while (name[0] != '\0' && (name[strlen(name) - 1] == '\n' ||
                                       name[strlen(name) - 1] == '\r'))
                name[strlen(name) - 1] = '\0';
        }
        fclose(fp);
    }

    if (number[0] && name[0])
        snprintf(label, sizeof(label), "%s — %s", number, name);
    else if (number[0])
        snprintf(label, sizeof(label), "%s", number);
    else if (name[0])
        snprintf(label, sizeof(label), "%s", name);

    pthread_mutex_lock(&g_stats_mutex);
    strncpy(g_stats.current_test_number, number, sizeof(g_stats.current_test_number) - 1);
    g_stats.current_test_number[sizeof(g_stats.current_test_number) - 1] = '\0';
    strncpy(g_stats.current_test_name, name, sizeof(g_stats.current_test_name) - 1);
    g_stats.current_test_name[sizeof(g_stats.current_test_name) - 1] = '\0';
    strncpy(g_stats.current_test_label, label, sizeof(g_stats.current_test_label) - 1);
    g_stats.current_test_label[sizeof(g_stats.current_test_label) - 1] = '\0';
    pthread_mutex_unlock(&g_stats_mutex);
}

static void launch_test_scenario(int index)
{
    if (index < 0 || index >= (int)(sizeof(g_test_scenarios) / sizeof(g_test_scenarios[0])))
        return;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "make %s >/tmp/update_test_run.log 2>&1 &",
             g_test_scenarios[index].target);
    (void)system(cmd);
}

static void get_test_button_rect(int index, float *x, float *y, float *w, float *h)
{
    const float button_h = 18.0f;
    const float button_gap = 6.0f;
    const float button_w = (CV_W - PAD * 2 - GAP * 3) / 4.0f;

    int col = index % 4;
    int row = index / 4;

    *x = CV_X + PAD + col * (button_w + GAP);
    *y = CV_Y + CV_H - 54.0f - row * (button_h + button_gap);
    *w = button_w;
    *h = button_h;
}

static void draw_test_buttons(void)
{
    set_color3(C_CARD);
    fill_rounded(CV_X + PAD, CV_Y + CV_H - 90.0f, CV_W - PAD * 2, 72.0f, R_CARD);

    set_color3(C_WHITE);
    draw_str(CV_X + PAD + 6.0f, CV_Y + CV_H - 72.0f, "Quick test buttons (click to run one scenario)",
             GLUT_BITMAP_HELVETICA_10);

    for (int i = 0; i < (int)(sizeof(g_test_scenarios) / sizeof(g_test_scenarios[0])); i++)
    {
        float bx, by, bw, bh;
        get_test_button_rect(i, &bx, &by, &bw, &bh);

        draw_pill(bx, by, bh, g_test_scenarios[i].label,
                  C_WHITE, C_SEP, 0.70f, GLUT_BITMAP_HELVETICA_10);
    }
}

static void draw_header(ServerStats *s)
{
    float hx = 0, hy = g_win_h - HDR_H, hw = g_win_w, hh = HDR_H;

    /* Dark header background */
    set_color3(C_CARD);
    fill_rect(hx, hy, hw, hh);

    /* Bottom border */
    draw_hline(hx, hy, hw);

    /* Server icon — simple rectangle stack */
    float ix = PAD + 4, iy = hy + hh * 0.5f - 7;
    set_color4(C_MUTED, 0.8f);
    glEnable(GL_BLEND);
    fill_rounded(ix, iy, 18, 6, 2);
    fill_rounded(ix, iy + 8, 18, 6, 2);

    /* Title */
    float ty = hy + hh * 0.62f;
    set_color3(C_WHITE);
    draw_str(ix + 24, ty, "Update server", GLUT_BITMAP_HELVETICA_18);

    /* Port badge */
    char port_str[32] = "port 9090";
    set_color3(C_MUTED);
    draw_str(ix + 24 + str_width("Update server", GLUT_BITMAP_HELVETICA_18) + 10,
             ty, port_str, GLUT_BITMAP_HELVETICA_12);

    /* Version badge */
    float bx = ix + 24 + str_width("Update server", GLUT_BITMAP_HELVETICA_18) + 12 + (float)str_width(port_str, GLUT_BITMAP_HELVETICA_12) + 8;
    draw_pill(bx, hy + hh * 0.45f, 16, "v2.0 latest",
              C_MUTED, C_SEP, 0.5f, GLUT_BITMAP_HELVETICA_10);

    /* Current test badge */
    if (s->current_test_label[0] != '\0')
    {
        float tx = bx + 70.0f;
        draw_pill(tx, hy + hh * 0.45f, 16, s->current_test_label,
                  C_BLUE, C_SEP, 0.45f, GLUT_BITMAP_HELVETICA_10);
    }

    /* Running indicator — right side */
    float rx = hw - PAD - (float)str_width("running", GLUT_BITMAP_HELVETICA_12) - 18;
    draw_dot(rx, hy + hh * 0.52f, 5, C_GREEN);
    set_color3(C_GREEN);
    draw_str(rx + 9, ty - 2, "running", GLUT_BITMAP_HELVETICA_12);
}

/* ------------------------------------------------------------------ */
/*  STAT ROW  (4 cards: connections / updates sent / up to date / threads)  */
/* ------------------------------------------------------------------ */

static void draw_stat_row(ServerStats *s)
{
    float sy = STAT_Y;
    float sh = STAT_H - GAP;
    int n = 4;
    float sw = (g_win_w - PAD * 2 - GAP * (n - 1)) / n;

    int busy_threads = 0;
    for (int i = 0; i < s->pool_size && i < MAX_THREADS; i++)
    {
        if (s->threads[i].state != THREAD_IDLE && s->threads[i].client_ip[0] != '\0')
            busy_threads++;
    }

    typedef struct
    {
        const char *label;
        int value;
        Color3 col;
    } Stat;
    Stat stats[4] = {
        {"connections", s->active_connections, C_WHITE},
        {"updates sent", s->updates_sent, C_ORANGE},
        {"up to date", s->up_to_date_count, C_GREEN},
        {"threads used", busy_threads, C_LIME},
    };

    for (int i = 0; i < n; i++)
    {
        float sx = PAD + i * (sw + GAP);

        /* Card background */
        set_color3(C_CARD);
        fill_rounded(sx, sy, sw, sh, R_CARD);

        /* Label */
        set_color3(C_MUTED);
        draw_str(sx + PAD, sy + sh * 0.82f, stats[i].label, GLUT_BITMAP_HELVETICA_10);

        /* Value — big number */
        char vbuf[32];
        if (i == n - 1)
            snprintf(vbuf, sizeof(vbuf), "%d / %d", busy_threads, s->pool_size);
        else
            snprintf(vbuf, sizeof(vbuf), "%d", stats[i].value);
        set_color3(stats[i].col);
        draw_str(sx + PAD, sy + sh * 0.40f, vbuf, GLUT_BITMAP_HELVETICA_18);
    }
}

/* ------------------------------------------------------------------ */
/*  ACTIVE CLIENTS panel                                                */
/* ------------------------------------------------------------------ */

static void draw_active_clients(ServerStats *s)
{
    /* Panel card */
    set_color3(C_CARD);
    fill_rounded(LP_X, LP_Y, LP_W, LP_H, R_CARD);

    /* Section header */
    float hline_y = LP_Y + LP_H - 30.0f;
    set_color3(C_WHITE);
    draw_str(LP_X + PAD, LP_Y + LP_H - 18.0f, "Active clients", GLUT_BITMAP_HELVETICA_12);
    draw_hline(LP_X + PAD, hline_y, LP_W - PAD * 2);

    /* Count active threads */
    int active_count = 0;
    for (int i = 0; i < s->pool_size && i < MAX_THREADS; i++)
    {
        if (s->threads[i].state != THREAD_IDLE &&
            s->threads[i].client_ip[0] != '\0')
        {
            active_count++;
        }
    }

    if (active_count == 0)
    {
        set_color3(C_MUTED);
        const char *msg = "No active clients";
        float mx = LP_X + (LP_W - str_width(msg, GLUT_BITMAP_HELVETICA_12)) / 2.0f;
        draw_str(mx, LP_Y + LP_H / 2.0f, msg, GLUT_BITMAP_HELVETICA_12);
        return;
    }

    /* Available height for client cards */
    float avail_h = LP_H - 36.0f;
    float card_h = 70.0f;
    float card_gap = 6.0f;
    int max_show = (int)(avail_h / (card_h + card_gap));
    if (max_show < 1)
        max_show = 1;

    int shown = 0;
    for (int i = 0; i < s->pool_size && i < MAX_THREADS && shown < max_show; i++)
    {
        ThreadInfo *t = &s->threads[i];
        if (t->state == THREAD_IDLE || t->client_ip[0] == '\0')
            continue;

        float cy = LP_Y + LP_H - 38.0f - (shown + 1) * (card_h + card_gap);
        if (cy < LP_Y + PAD)
            break;

        float cx = LP_X + PAD;
        float cw = LP_W - PAD * 2;

        /* Inner client card */
        set_color3(C_CARD2);
        fill_rounded(cx, cy, cw, card_h, 6);

        /* IP address */
        set_color3(C_WHITE);
        draw_str(cx + PAD, cy + card_h - 16.0f, t->client_ip, GLUT_BITMAP_HELVETICA_12);

        /* Version pill */
        char ver_buf[16];
        uint32_t cv = g_slot_version[i];
        snprintf(ver_buf, sizeof(ver_buf), "v%u.%u", cv / 10, cv % 10);
        if (cv == 0)
            snprintf(ver_buf, sizeof(ver_buf), "v?.?");
        Color3 vcol = (t->state == THREAD_DONE) ? C_GREEN : C_ORANGE;
        float pill_x = cx + PAD + (float)str_width(t->client_ip, GLUT_BITMAP_HELVETICA_12) + 8;
        draw_pill(pill_x, cy + card_h - 20.0f, 14, ver_buf,
                  vcol, vcol, 0.18f, GLUT_BITMAP_HELVETICA_10);

        /* Status badge — right aligned */
        const char *sts = status_badge_str(t->state);
        Color3 scol = status_badge_color(t->state);
        float sbw = (float)str_width(sts, GLUT_BITMAP_HELVETICA_10) + 14;
        draw_pill(cx + cw - sbw - PAD, cy + card_h - 20.0f, 14, sts,
                  scol, scol, 0.18f, GLUT_BITMAP_HELVETICA_10);

        /* Progress bar */
        float pbx = cx + PAD, pby = cy + card_h * 0.44f;
        float pbw = cw - PAD * 2, pbh = 6.0f;
        float prog = (t->state == THREAD_TRANSFERRING) ? t->progress
                     : (t->state == THREAD_DONE)       ? 1.0f
                                                       : 0.0f;
        Color3 pcol = (t->state == THREAD_DONE) ? C_GREEN : C_ORANGE;
        draw_progress_bar(pbx, pby, pbw, pbh, prog, pcol);

        /* Percent text */
        if (t->state == THREAD_TRANSFERRING || t->state == THREAD_DONE)
        {
            char pct[8];
            if (t->state == THREAD_DONE)
                snprintf(pct, sizeof(pct), "--");
            else
                snprintf(pct, sizeof(pct), "%d%%", (int)(prog * 100));
            set_color3(C_MUTED);
            draw_str(cx + cw - (float)str_width(pct, GLUT_BITMAP_HELVETICA_10) - PAD,
                     pby + 2, pct, GLUT_BITMAP_HELVETICA_10);
        }

        /* Thread + filename */
        char thread_str[64];
        if (t->state == THREAD_TRANSFERRING || t->state == THREAD_DONE)
        {
            snprintf(thread_str, sizeof(thread_str), "thread 0x%04x · update_v2.0.pkg",
                     (unsigned)(i * 0x1000 + 0xf3a));
        }
        else
        {
            snprintf(thread_str, sizeof(thread_str), "thread 0x%04x · %s",
                     (unsigned)(i * 0x1000 + 0xf3a), status_badge_str(t->state));
        }
        set_color3(C_DIM);
        draw_str(cx + PAD, cy + 6.0f, thread_str, GLUT_BITMAP_HELVETICA_10);

        shown++;
    }
}

/* ------------------------------------------------------------------ */
/*  EVENT LOG panel                                                     */
/* ------------------------------------------------------------------ */

static const char *ev_type_str(EvType t)
{
    switch (t)
    {
    case EV_INFO:
        return "INFO";
    case EV_CONN:
        return "CONN";
    case EV_UPDT:
        return "UPDT";
    case EV_OK:
        return "OK";
    case EV_DONE:
        return "DONE";
    case EV_WARN:
        return "WARN";
    }
    return "INFO";
}

static Color3 ev_type_color(EvType t)
{
    switch (t)
    {
    case EV_INFO:
        return C_BLUE;
    case EV_CONN:
        return C_BLUE;
    case EV_UPDT:
        return C_ORANGE;
    case EV_OK:
        return C_GREEN;
    case EV_DONE:
        return C_GREEN;
    case EV_WARN:
        return C_WARN;
    }
    return C_MUTED;
}

static void draw_event_log(void)
{
    /* Panel card */
    set_color3(C_CARD);
    fill_rounded(RP_X, RP_Y, RP_W, RP_H, R_CARD);

    /* Section header */
    float hline_y = RP_Y + RP_H - 30.0f;
    set_color3(C_WHITE);
    draw_str(RP_X + PAD, RP_Y + RP_H - 18.0f, "Event log", GLUT_BITMAP_HELVETICA_12);
    draw_hline(RP_X + PAD, hline_y, RP_W - PAD * 2);

    float avail_h = RP_H - 36.0f;
    float row_h = 22.0f;
    int max_rows = (int)(avail_h / row_h);
    if (max_rows < 1)
        max_rows = 1;

    pthread_mutex_lock(&g_ev_mutex);
    int total = (g_ev_count < max_rows) ? g_ev_count : max_rows;
    for (int i = 0; i < total; i++)
    {
        /* Newest event first: index = (g_ev_head - 1 - i + EV_MAX) % EV_MAX */
        int idx = (g_ev_head - 1 - i + EV_MAX * 2) % EV_MAX;
        LogEvent *ev = &g_events[idx];

        float ry = RP_Y + RP_H - 38.0f - i * row_h;
        if (ry < RP_Y + PAD)
            break;

        float rx = RP_X + PAD;

        /* Timestamp */
        set_color3(C_MUTED);
        draw_str(rx, ry, ev->time_str, GLUT_BITMAP_HELVETICA_10);
        rx += 52.0f;

        /* Type badge */
        Color3 tc = ev_type_color(ev->type);
        draw_pill(rx, ry - 2, 14, ev_type_str(ev->type),
                  tc, tc, 0.18f, GLUT_BITMAP_HELVETICA_10);
        rx += (float)str_width(ev_type_str(ev->type), GLUT_BITMAP_HELVETICA_10) + 22;

        /* Message — clipped to panel width */
        set_color3(C_WHITE);
        char mbuf[EV_MSG];
        strncpy(mbuf, ev->msg, EV_MSG - 1);
        mbuf[EV_MSG - 1] = '\0';
        float max_w = RP_X + RP_W - rx - PAD;
        while (strlen(mbuf) > 4 && (float)str_width(mbuf, GLUT_BITMAP_HELVETICA_10) > max_w)
            mbuf[strlen(mbuf) - 1] = '\0';
        draw_str(rx, ry, mbuf, GLUT_BITMAP_HELVETICA_10);

        /* Separator */
        if (i < total - 1)
            draw_hline(RP_X + PAD, ry - 4, RP_W - PAD * 2);
    }
    pthread_mutex_unlock(&g_ev_mutex);
}

/* ------------------------------------------------------------------ */
/*  CLIENT VIEW panel (focused client detail)                          */
/* ------------------------------------------------------------------ */

static void draw_client_view(ServerStats *s)
{
    draw_test_buttons();

    /* Find the most active (transferring/newest) client */
    int focus = -1;
    for (int i = 0; i < s->pool_size && i < MAX_THREADS; i++)
    {
        if (s->threads[i].state == THREAD_TRANSFERRING)
        {
            focus = i;
            break;
        }
    }
    if (focus < 0)
    {
        for (int i = 0; i < s->pool_size && i < MAX_THREADS; i++)
        {
            if (s->threads[i].state != THREAD_IDLE && s->threads[i].client_ip[0])
            {
                focus = i;
                break;
            }
        }
    }

    /* Panel card */
    set_color3(C_CARD);
    fill_rounded(CV_X, CV_Y, CV_W, CV_H, R_CARD);

    /* Header row */
    char title[64];
    if (focus >= 0 && s->threads[focus].client_ip[0])
        snprintf(title, sizeof(title), "Client view — %s", s->threads[focus].client_ip);
    else
        snprintf(title, sizeof(title), "Client view — no active client");

    float hline_y = CV_Y + CV_H - 26.0f;
    set_color3(C_WHITE);
    draw_str(CV_X + PAD, CV_Y + CV_H - 14.0f, title, GLUT_BITMAP_HELVETICA_12);
    draw_hline(CV_X + PAD, hline_y, CV_W - PAD * 2);

    if (focus < 0)
        return;

    ThreadInfo *t = &s->threads[focus];

    /* 4 detail columns */
    float n = 4.0f;
    float cw = (CV_W - PAD * 2 - GAP * (n - 1)) / n;
    float col_y = CV_Y + CV_H * 0.35f;

    /* Column 1: Step */
    float c1x = CV_X + PAD;
    set_color3(C_MUTED);
    draw_str(c1x, CV_Y + CV_H - 38.0f, "step", GLUT_BITMAP_HELVETICA_10);
    const char *step_line1, *step_line2;
    switch (t->state)
    {
    case THREAD_AUTH:
        step_line1 = "Authenticating";
        step_line2 = "CheckToken() running";
        break;
    case THREAD_VERSION_CHECK:
        step_line1 = "Checking version";
        step_line2 = "VersionCheck() running";
        break;
    case THREAD_TRANSFERRING:
        step_line1 = "Downloading";
        step_line2 = "CheckForUpdate()";
        break;
    case THREAD_DONE:
        step_line1 = "Complete";
        step_line2 = "update saved";
        break;
    default:
        step_line1 = "Idle";
        step_line2 = "waiting";
        break;
    }
    set_color3(C_WHITE);
    draw_str(c1x, col_y + 8, step_line1, GLUT_BITMAP_HELVETICA_12);
    set_color3(C_MUTED);
    draw_str(c1x, col_y - 8, step_line2, GLUT_BITMAP_HELVETICA_10);

    /* Column 2: Current version */
    float c2x = c1x + cw + GAP;
    set_color3(C_MUTED);
    draw_str(c2x, CV_Y + CV_H - 38.0f, "current version", GLUT_BITMAP_HELVETICA_10);
    char cv_str[16];
    uint32_t cv = g_slot_version[focus];
    if (cv == 0)
        snprintf(cv_str, sizeof(cv_str), "v?.?");
    else
        snprintf(cv_str, sizeof(cv_str), "v%u.%u", cv / 10, cv % 10);
    Color3 cv_col = (cv == 0) ? C_MUTED : C_ORANGE;
    set_color3(cv_col);
    draw_str(c2x, col_y + 8, cv_str, GLUT_BITMAP_HELVETICA_12);
    set_color3(C_MUTED);
    draw_str(c2x, col_y - 8, "outdated", GLUT_BITMAP_HELVETICA_10);

    /* Column 3: Target version */
    float c3x = c2x + cw + GAP;
    set_color3(C_MUTED);
    draw_str(c3x, CV_Y + CV_H - 38.0f, "target version", GLUT_BITMAP_HELVETICA_10);
    set_color3(C_GREEN);
    draw_str(c3x, col_y + 8, "v2.0", GLUT_BITMAP_HELVETICA_12);
    set_color3(C_MUTED);
    draw_str(c3x, col_y - 8, "from server", GLUT_BITMAP_HELVETICA_10);

    /* Column 4: Download progress */
    float c4x = c3x + cw + GAP;
    float c4w = CV_X + CV_W - c4x - PAD;
    set_color3(C_MUTED);
    draw_str(c4x, CV_Y + CV_H - 38.0f, "download progress", GLUT_BITMAP_HELVETICA_10);

    float prog = (t->state == THREAD_TRANSFERRING) ? t->progress
                 : (t->state == THREAD_DONE)       ? 1.0f
                                                   : 0.0f;
    Color3 pcol = (t->state == THREAD_DONE) ? C_GREEN : C_ORANGE;

    /* Bar */
    draw_progress_bar(c4x, col_y + 4, c4w * 0.75f, 7, prog, pcol);

    /* Percent */
    char pct[12];
    if (t->state == THREAD_DONE)
        snprintf(pct, sizeof(pct), "100%%");
    else
        snprintf(pct, sizeof(pct), "%d%%", (int)(prog * 100));
    set_color3(C_WHITE);
    draw_str(c4x + c4w * 0.75f + 6, col_y + 5, pct, GLUT_BITMAP_HELVETICA_12);

    /* Filename */
    set_color3(C_MUTED);
    const char *fname = (t->state == THREAD_DONE) ? "update_v2.0.pkg · saved"
                                                  : "update_v2.0.pkg · saving locally";
    draw_str(c4x, col_y - 8, fname, GLUT_BITMAP_HELVETICA_10);
}

/* ------------------------------------------------------------------ */
/*  GLUT callbacks                                                      */
/* ------------------------------------------------------------------ */

static void cb_display(void)
{
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, g_win_w, 0, g_win_h);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    refresh_current_test_state();

    ServerStats snap;
    pthread_mutex_lock(&g_stats_mutex);
    memcpy(&snap, &g_stats, sizeof(snap));
    pthread_mutex_unlock(&g_stats_mutex);

    draw_header(&snap);
    draw_stat_row(&snap);
    draw_active_clients(&snap);
    draw_event_log();
    draw_client_view(&snap);

    glutSwapBuffers();
}

static void cb_reshape(int w, int h)
{
    if (h == 0)
        h = 1;
    g_win_w = w;
    g_win_h = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void cb_mouse(int button, int state, int x, int y)
{
    if (button != GLUT_LEFT_BUTTON || state != GLUT_DOWN)
        return;

    int gl_y = g_win_h - y;
    for (int i = 0; i < (int)(sizeof(g_test_scenarios) / sizeof(g_test_scenarios[0])); i++)
    {
        float bx, by, bw, bh;
        get_test_button_rect(i, &bx, &by, &bw, &bh);

        if (x >= bx && x <= bx + bw && gl_y >= by && gl_y <= by + bh)
        {
            launch_test_scenario(i);
            break;
        }
    }
}

static void cb_timer(int val)
{
    static time_t last_tick = 0;
    time_t now = time(NULL);
    if (now != last_tick)
    {
        last_tick = now;
        pthread_mutex_lock(&g_stats_mutex);
        g_stats.timeline[g_stats.timeline_head] = g_stats.connections_this_second;
        g_stats.connections_this_second = 0;
        g_stats.timeline_head = (g_stats.timeline_head + 1) % TIMELINE_LEN;
        pthread_mutex_unlock(&g_stats_mutex);
    }
    glutPostRedisplay();
    glutTimerFunc(200, cb_timer, val);
}

/* ------------------------------------------------------------------ */
/*  Dashboard thread                                                    */
/* ------------------------------------------------------------------ */

static void *dashboard_thread_fn(void *arg)
{
    (void)arg;
    int fake_argc = 1;
    char *fake_argv = "server";
    glutInit(&fake_argc, &fake_argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(g_win_w, g_win_h);
    glutCreateWindow("Update Server — Live Dashboard");

    glClearColor(C_BG.r, C_BG.g, C_BG.b, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glutDisplayFunc(cb_display);
    glutReshapeFunc(cb_reshape);
    glutMouseFunc(cb_mouse);
    glutTimerFunc(200, cb_timer, 0);
    glutMainLoop();
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void dashboard_init(int pool_size, const char *log_path)
{
    pthread_mutex_lock(&g_stats_mutex);
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.pool_size = pool_size;
    g_stats.start_time = time(NULL);
    pthread_mutex_unlock(&g_stats_mutex);
    refresh_current_test_state();
    memset(g_slot_version, 0, sizeof(g_slot_version));
    g_pool_size = pool_size;
    g_log_path = log_path;
}

void dashboard_start(void)
{
    pthread_create(&g_dash_tid, NULL, dashboard_thread_fn, NULL);
    pthread_detach(g_dash_tid);
}

void dashboard_set_thread(int slot, ThreadState state, float progress,
                          const char *ip, uint32_t total, uint32_t sent)
{
    if (slot < 0 || slot >= MAX_THREADS)
        return;
    pthread_mutex_lock(&g_stats_mutex);
    ThreadInfo *t = &g_stats.threads[slot];
    t->state = state;
    t->progress = progress;
    t->bytes_total = total;
    t->bytes_sent = sent;
    if (ip)
        strncpy(t->client_ip, ip, sizeof(t->client_ip) - 1);
    pthread_mutex_unlock(&g_stats_mutex);
}

/* Extended: also record client version for display */
void dashboard_set_thread_version(int slot, uint32_t client_version)
{
    if (slot < 0 || slot >= MAX_THREADS)
        return;
    g_slot_version[slot] = client_version;
}

void dashboard_on_connect(void)
{
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.active_connections++;
    g_stats.connections_this_second++;
    g_stats.total_served++;
    pthread_mutex_unlock(&g_stats_mutex);
}

void dashboard_on_disconnect(DisconnectReason reason)
{
    pthread_mutex_lock(&g_stats_mutex);
    if (g_stats.active_connections > 0)
        g_stats.active_connections--;

    switch (reason)
    {
    case DISCONNECT_REASON_UPDATE_SENT:
        g_stats.updates_sent++;
        break;
    case DISCONNECT_REASON_UP_TO_DATE:
        g_stats.up_to_date_count++;
        break;
    case DISCONNECT_REASON_AUTH_REJECTED:
        g_stats.auth_failures++;
        break;
    case DISCONNECT_REASON_NONE:
    case DISCONNECT_REASON_ERROR:
    default:
        break;
    }

    pthread_mutex_unlock(&g_stats_mutex);
}
