/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm1cs_session end to end: the session over posix_tcp against Move Remote's simulator (moveremote-sim), presses
 * injected through its test FIFO, the actions, LEDs and screen checked, pings answered, and a reconnect after the
 * socket is cut. tests/move_e2e.sh starts the simulator and runs this.
 *   session_e2e --port N --dir DIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "moveremote/proto.h"
#include "rfm1cs/posix_tcp.h"
#include "rfm1cs/session.h"

static int g_fail, g_checks;
#define CHECK(c, ...)                                                                                               \
    do {                                                                                                            \
        g_checks++;                                                                                                 \
        if (c)                                                                                                      \
            printf("  ok    ");                                                                                     \
        else                                                                                                        \
            printf("  FAIL  "), g_fail++;                                                                           \
        printf(__VA_ARGS__);                                                                                        \
        putchar('\n');                                                                                              \
    } while (0)

static rfm1cs_session_t S;
static int g_fd = -1, g_port, g_opens, g_lost;
static rfm_move_act_t g_acts[512];
static int g_nact;

static uint32_t ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)((uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u);
}
static void on_event(void *ctx, const rfm1cs_event_t *ev)
{
    (void)ctx;
    switch (ev->type) {
    case RFM1CS_EV_OPEN: g_opens++; g_fd = rfm1cs_tcp_connect("127.0.0.1", g_port); break;
    case RFM1CS_EV_CLOSE: rfm1cs_tcp_close(g_fd); g_fd = -1; break;
    case RFM1CS_EV_SEND:
        if (g_fd >= 0)
            rfm1cs_tcp_write(g_fd, ev->data, (int)ev->len);
        break;
    case RFM1CS_EV_LOST: g_lost++; break;
    case RFM1CS_EV_ACTION:
        if (g_nact < 512)
            g_acts[g_nact++] = *ev->act;
        break;
    default: break;
    }
}
/* the host's loop for a while: the connect, the bytes, the timers */
static void run(int ms_max, int want_acts)
{
    const uint32_t until = ms() + (uint32_t)ms_max;
    uint8_t buf[4096];
    while ((int32_t)(ms() - until) < 0 && (want_acts < 0 || g_nact < want_acts)) {
        if (g_fd >= 0 && S.st.state == RFM1CS_CONNECTING && !S.opened) {
            const int r = rfm1cs_tcp_wait(g_fd, 5);
            if (r > 0)
                rfm1cs_session_opened(&S, ms());
            else if (r < 0)
                rfm1cs_session_closed(&S, ms(), 0);
        } else if (g_fd >= 0) {
            const int n = rfm1cs_tcp_read(g_fd, buf, sizeof buf, 5);
            if (n > 0)
                rfm1cs_session_rx(&S, ms(), buf, (uint32_t)n);
            else if (n < 0)
                rfm1cs_session_closed(&S, ms(), 0);
        } else {
            usleep(5000);
        }
        rfm1cs_session_tick(&S, ms());
    }
}
static int find(uint8_t kind, int index, int down, int value)
{
    int i;
    for (i = 0; i < g_nact; i++)
        if (g_acts[i].kind == kind && g_acts[i].index == index && g_acts[i].down == down && g_acts[i].value == value)
            return 1;
    return 0;
}
static void inject(const char *fifo, const char *lines)
{
    FILE *f = fopen(fifo, "w");
    if (f)
        fputs(lines, f), fclose(f);
}
static int state_has(const char *dir, const char *what)
{
    char path[512], line[512];
    FILE *f;
    int ok = 0;
    snprintf(path, sizeof path, "%s/mr-state", dir);
    if (!(f = fopen(path, "r")))
        return 0;
    if (fgets(line, sizeof line, f))
        ok = strstr(line, what) != NULL;
    fclose(f);
    return ok;
}
/* the simulator's screen shot is the frame the session sent last */
static int shot_matches(const char *dir, const char *fifo)
{
    char path[512], line[256];
    int tries, x, y, ok;
    for (tries = 0; tries < 8; tries++) {
        FILE *f = NULL;
        int t;
        snprintf(path, sizeof path, "%s/mr-screen.pbm", dir);
        unlink(path);
        inject(fifo, "shot\n");
        for (t = 0; t < 50 && !(f = fopen(path, "r")); t++)
            run(20, -1);
        if (!f)
            continue;
        ok = fgets(line, sizeof line, f) && !strncmp(line, "P1", 2) && fgets(line, sizeof line, f);
        for (y = 0; y < 64 && ok; y++) {
            if (!fgets(line, sizeof line, f) || strlen(line) < 128) {
                ok = 0;
                break;
            }
            for (x = 0; x < 128; x++)
                ok &= (line[x] == '1') == ((S.shown[(y >> 3) * 128 + x] >> (y & 7)) & 1);
        }
        fclose(f);
        if (ok)
            return 1;
        run(50, -1);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = NULL;
    char fifo[512];
    uint8_t levels[RFM_MOVE_NLEVELS];
    int i, tries;
    uint32_t pings;
    for (i = 1; i + 1 < argc; i += 2)
        if (!strcmp(argv[i], "--port"))
            g_port = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--dir"))
            dir = argv[i + 1];
    if (!g_port || !dir) {
        fprintf(stderr, "usage: session_e2e --port N --dir DIR\n");
        return 2;
    }
    snprintf(fifo, sizeof fifo, "%s/mr-cmd", dir);
    printf("session_e2e\n");
    rfm1cs_session_init(&S, NULL, 0, "rfm1cs session e2e", on_event, NULL);
    rfm1cs_session_start(&S, ms());
    for (tries = 0; tries < 300 && S.st.state != RFM1CS_CONNECTED; tries++)
        run(10, -1);
    CHECK(S.st.state == RFM1CS_CONNECTED && (S.st.caps & MR_CAP_SIMULATOR) && S.st.via == MR_VIA_LOCAL,
          "connected over TCP to the simulator (%s, via %d)", S.st.name, S.st.via);
    CHECK(S.st.led_writes == RFM_MOVE_NLEDS && S.st.screens == 1, "every LED painted (%u), one screen", S.st.led_writes);

    /* FM-1 mode: pad-2-1 = key 0; Play; the volume knob = MASTER */
    inject(fifo, "note 84 100\nnote 84 0\ncc 85 127\ncc 85 0\ncc 79 2\n");
    run(3000, 5);
    CHECK(find(RFM_MOVE_ACT_KEY, 0, 1, 0) && find(RFM_MOVE_ACT_KEY, 0, 0, 0), "pad-2-1: key 0 down and up");
    CHECK(find(RFM_MOVE_ACT_BTN, RFM_MOVE_B_PLAY, 1, 0) && find(RFM_MOVE_ACT_MASTER, 0, 0, 768 + 32), "Play, MASTER 800");
    memset(levels, 0, sizeof levels);
    levels[3 * 4 + 3] = 255;
    rfm1cs_session_panel(&S, levels, 255);
    run(300, -1);
    CHECK(shot_matches(dir, fifo), "the simulator shows the session's screen, pixel for pixel");
    for (tries = 0; tries < 160 && !(state_has(dir, "led_pending=0 ") && state_has(dir, "drew=1")); tries++)
        run(25, -1);
    CHECK(state_has(dir, "led_pending=0 ") && state_has(dir, "host=rfm1cs session e2e"), "every LED write taken, our host");

    /* pings answered for 2.5 s; STATUS keeps it alive */
    pings = S.st.pings;
    run(2500, -1);
    CHECK(S.st.pings >= pings + 2 && S.st.pongs >= 2 && S.st.state == RFM1CS_CONNECTED && S.st.battery >= 0,
          "pings %u, pongs %u, battery %d: alive", S.st.pings, S.st.pongs, S.st.battery);

    /* the socket cut under it: lost, let go, back in about a second */
    rfm1cs_tcp_close(g_fd);
    g_fd = -1;
    rfm1cs_session_closed(&S, ms(), 0);
    CHECK(g_lost == 1 && S.st.state == RFM1CS_RETRYING && S.st.retry_ms == 1000, "cut: lost, retrying in 1 s");
    for (tries = 0; tries < 400 && S.st.state != RFM1CS_CONNECTED; tries++)
        run(10, -1);
    CHECK(S.st.state == RFM1CS_CONNECTED && S.st.connects == 2 && g_opens == 2, "reconnected (%u connects)", S.st.connects);
    g_nact = 0;
    inject(fifo, "cc 42 127\ncc 42 0\nnote 68 90\nnote 68 0\n");
    run(3000, 3);
    CHECK(S.st.mode == RFM_MOVE_MODE_MIDI && find(RFM_MOVE_ACT_SETTINGS, 0, 0, 0), "Track 2: MIDI mode");
    rfm1cs_session_stop(&S);
    CHECK(S.st.state == RFM1CS_OFF && g_fd < 0, "stopped: closed");
    printf("session_e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
