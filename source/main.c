// Gerald the Frog - a tiny Tamagotchi-style pet for the Nintendo 3DS
// Pixel art is drawn straight from the string maps below (no image files needed).
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>

#define SAVE_DIR  "sdmc:/3ds/gerald"
#define SAVE_FILE SAVE_DIR "/save.bin"
#define SAVE_MAGIC 0x47524C44u  /* "GRLD" */

/* ------------------------------------------------------------ pixel art */
#define FROG_ROWS 13
static const char *FROG[FROG_ROWS] = {
    "................",
    "..ggg......ggg..",
    ".gwwwg....gwwwg.",
    ".gwkkg....gwkkg.",
    ".gggggggggggggg.",
    "gggggggggggggggg",
    "grrggggggggggrrg",
    "ggggkkkkkkkkgggg",
    "ggllllllllllllgg",
    "ggllllllllllllgg",
    ".ggllllllllllgg.",
    "..gggggggggggg..",
    "dddd........dddd",
};
static const char *HEART[6] = {
    ".rr.rr.", "rrrrrrr", "rrrrrrr", ".rrrrr.", "..rrr..", "...r...",
};

static u32 pal(char c)
{
    switch (c) {
    case 'g': return C2D_Color32(88, 200, 70, 255);
    case 'd': return C2D_Color32(40, 120, 50, 255);
    case 'l': return C2D_Color32(200, 240, 150, 255);
    case 'k': return C2D_Color32(20, 20, 30, 255);
    case 'w': return C2D_Color32(255, 255, 255, 255);
    case 'r': return C2D_Color32(230, 90, 110, 255);
    case 'b': return C2D_Color32(120, 80, 40, 255);
    }
    return 0;
}

static void drawSprite(const char *const *rows, int n, float x, float y, float s)
{
    for (int r = 0; r < n; r++)
        for (int c = 0; rows[r][c]; c++)
            if (rows[r][c] != '.')
                C2D_DrawRectSolid(x + c * s, y + r * s, 0.4f, s, s, pal(rows[r][c]));
}

/* ------------------------------------------------------------ audio */
enum { S_RIBBIT, S_EAT, S_HAPPY, S_CLICK, S_SLEEP, S_CLEAN, S_COUNT };
typedef struct { const char *file; s16 *data; ndspWaveBuf wb; } Snd;
static Snd snd[S_COUNT] = {
    {"romfs:/ribbit.pcm"}, {"romfs:/eat.pcm"},   {"romfs:/happy.pcm"},
    {"romfs:/click.pcm"},  {"romfs:/sleep.pcm"}, {"romfs:/clean.pcm"},
};
static bool audioOk = false;

static void audioInit(void)
{
    if (R_FAILED(ndspInit())) return;  /* no dspfirm.cdc -> silent mode */
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int i = 0; i < S_COUNT; i++) {
        FILE *f = fopen(snd[i].file, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        snd[i].data = linearAlloc(size);
        if (!snd[i].data) { fclose(f); continue; }
        fread(snd[i].data, 1, size, f);
        fclose(f);
        DSP_FlushDataCache(snd[i].data, size);

        float mix[12] = {0};
        mix[0] = mix[1] = 0.8f;
        ndspChnReset(i);
        ndspChnSetInterp(i, NDSP_INTERP_LINEAR);
        ndspChnSetRate(i, 22050.0f);
        ndspChnSetFormat(i, NDSP_FORMAT_MONO_PCM16);
        ndspChnSetMix(i, mix);

        memset(&snd[i].wb, 0, sizeof(ndspWaveBuf));
        snd[i].wb.data_vaddr = snd[i].data;
        snd[i].wb.nsamples = size / 2;
        snd[i].wb.looping = false;
        snd[i].wb.status = NDSP_WBUF_DONE;
    }
    audioOk = true;
}

static void play(int i)
{
    if (!audioOk || !snd[i].data) return;
    if (snd[i].wb.status == NDSP_WBUF_QUEUED || snd[i].wb.status == NDSP_WBUF_PLAYING) return;
    ndspChnWaveBufAdd(i, &snd[i].wb);
}

static void audioExit(void)
{
    if (!audioOk) return;
    ndspExit();
    for (int i = 0; i < S_COUNT; i++)
        if (snd[i].data) linearFree(snd[i].data);
}

/* ------------------------------------------------------------ game state */
typedef struct {
    u32 magic;
    float hunger, happy, clean, energy;
    u32 savedAt;
    u32 age;  /* seconds */
} Save;

typedef enum { A_NONE, A_EAT, A_PLAY, A_CLEAN } Anim;

static Save g = { SAVE_MAGIC, 80, 80, 80, 80, 0, 0 };
static bool sleeping = false;
static Anim anim = A_NONE;
static float animT = 0, msgT = 0, ribbitTimer = 12, saveTimer = 0, clock_s = 0, ageAcc = 0;
static char msg[48] = "";

static float clampf(float v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

static void say(const char *s) { snprintf(msg, sizeof msg, "%s", s); msgT = 2.5f; }

static void saveGame(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);
    g.savedAt = (u32)time(NULL);
    FILE *f = fopen(SAVE_FILE, "wb");
    if (!f) return;
    fwrite(&g, sizeof g, 1, f);
    fclose(f);
}

static void loadGame(void)
{
    FILE *f = fopen(SAVE_FILE, "rb");
    if (!f) return;
    Save s;
    if (fread(&s, sizeof s, 1, f) == 1 && s.magic == SAVE_MAGIC) {
        g = s;
        /* gentle offline decay: capped at 6h and never below 15 so Gerald never "dies" */
        float el = (float)((u32)time(NULL) - g.savedAt);
        if (el < 0) el = 0;
        if (el > 21600) el = 21600;
        el *= 0.3f;
        g.hunger = fmaxf(15, clampf(g.hunger - 0.12f * el));
        g.happy  = fmaxf(15, clampf(g.happy  - 0.08f * el));
        g.clean  = fmaxf(15, clampf(g.clean  - 0.06f * el));
    }
    fclose(f);
}

static void tick(float dt)
{
    float m = sleeping ? 0.4f : 1.0f;
    g.hunger -= 0.12f * m * dt;
    g.happy  -= 0.08f * m * dt;
    g.clean  -= 0.06f * dt;
    if (g.hunger < 20) g.happy -= 0.05f * dt;
    if (sleeping) {
        g.energy += 2.0f * dt;
        if (g.energy >= 100) { sleeping = false; say("Gerald woke up!"); play(S_RIBBIT); }
    } else {
        g.energy -= 0.05f * dt;
        if (g.energy <= 0) { sleeping = true; say("Gerald fell asleep..."); play(S_SLEEP); }
    }
    g.hunger = clampf(g.hunger); g.happy = clampf(g.happy);
    g.clean = clampf(g.clean);   g.energy = clampf(g.energy);

    ageAcc += dt;
    while (ageAcc >= 1.0f) { g.age++; ageAcc -= 1.0f; }

    if (animT > 0) { animT -= dt; if (animT <= 0) anim = A_NONE; }
    if (msgT > 0) msgT -= dt;
    ribbitTimer -= dt;
    if (ribbitTimer <= 0) { if (!sleeping) play(S_RIBBIT); ribbitTimer = 15 + rand() % 20; }
    saveTimer += dt;
    if (saveTimer > 30) { saveTimer = 0; saveGame(); }
}

static void doAction(int a)  /* 0 feed, 1 play, 2 clean, 3 sleep */
{
    if (a == 3) {
        sleeping = !sleeping;
        anim = A_NONE;
        play(sleeping ? S_SLEEP : S_RIBBIT);
        say(sleeping ? "Nighty night, Gerald" : "Good morning!");
        return;
    }
    if (sleeping) { say("Shh... Gerald is asleep"); play(S_CLICK); return; }
    if (anim != A_NONE) return;
    switch (a) {
    case 0:
        if (g.hunger > 95) { say("Gerald is full!"); play(S_CLICK); return; }
        g.hunger = clampf(g.hunger + 25); anim = A_EAT; animT = 1.6f; play(S_EAT); say("Nom nom! A tasty fly");
        break;
    case 1:
        if (g.energy < 15) { say("Gerald is too tired"); play(S_CLICK); return; }
        g.happy = clampf(g.happy + 25); g.energy = clampf(g.energy - 8); g.hunger = clampf(g.hunger - 4);
        anim = A_PLAY; animT = 2.0f; play(S_HAPPY); say("Hop hop hop!");
        break;
    case 2:
        g.clean = clampf(g.clean + 35); anim = A_CLEAN; animT = 1.8f; play(S_CLEAN); say("Squeaky clean!");
        break;
    }
}

/* ------------------------------------------------------------ drawing */
static C2D_TextBuf tb;

static void text(const char *s, float x, float y, float sc, u32 col)
{
    C2D_Text t;
    C2D_TextParse(&t, tb, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.8f, sc, sc, col);
}

static void textCentered(const char *s, float cx, float y, float sc, u32 col)
{
    C2D_Text t;
    float w, h;
    C2D_TextParse(&t, tb, s);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, sc, sc, &w, &h);
    C2D_DrawText(&t, C2D_WithColor, cx - w / 2, y, 0.8f, sc, sc, col);
}

static const char *status(void)
{
    if (sleeping) return "Zzz...";
    if (g.hunger < 25) return "Gerald is hungry!";
    if (g.clean < 25) return "Gerald is dirty...";
    if (g.happy < 25) return "Gerald is bored";
    if (g.energy < 20) return "Gerald is sleepy";
    return "Gerald is happy";
}

static char frame[FROG_ROWS][17];
static const char *fp[FROG_ROWS];

static void buildFrog(void)
{
    float score = (g.hunger + g.happy + g.clean) / 3.0f;
    bool blink = fmodf(clock_s, 4.0f) < 0.15f;
    for (int i = 0; i < FROG_ROWS; i++) { strcpy(frame[i], FROG[i]); fp[i] = frame[i]; }

    if (sleeping || blink) {
        strcpy(frame[2], ".ggggg....ggggg.");
        strcpy(frame[3], ".gkkkg....gkkkg.");
    }
    if (anim == A_EAT) {
        if (((int)(animT * 5)) % 2) {
            strcpy(frame[7], "ggggrrrrrrrrgggg");
            strcpy(frame[8], "gggrrrrrrrrrrggg");
        }
    } else if (anim == A_PLAY || (score > 66 && !sleeping)) {
        strcpy(frame[7], "gkggggggggggggkg");
        strcpy(frame[8], "ggkkkkkkkkkkkkgg");
    } else if (score < 35 && !sleeping) {
        strcpy(frame[7], "gggkkkkkkkkkkggg");
        strcpy(frame[8], "ggkgglllllllgkgg");
    }
    if (g.clean < 50) { frame[5][3] = 'b'; frame[9][11] = 'b'; }
    if (g.clean < 25) { frame[6][8] = 'b'; frame[10][4] = 'b'; frame[4][9] = 'b'; }
}

static void drawTop(void)
{
    const float S = 8;
    /* sky, water, lily pad */
    C2D_DrawRectangle(0, 0, 0, 400, 150, C2D_Color32(120, 200, 235, 255), C2D_Color32(120, 200, 235, 255),
                      C2D_Color32(190, 235, 250, 255), C2D_Color32(190, 235, 250, 255));
    C2D_DrawRectSolid(0, 150, 0, 400, 90, C2D_Color32(50, 110, 190, 255));
    for (int i = 0; i < 6; i++) {  /* water sparkles */
        float x = fmodf(i * 73 + clock_s * 10, 400);
        C2D_DrawRectSolid(x, 170 + (i % 3) * 22, 0.1f, 14, 2, C2D_Color32(120, 170, 230, 255));
    }
    C2D_DrawRectSolid(100, 170, 0.2f, 200, 14, C2D_Color32(60, 150, 70, 255));
    C2D_DrawRectSolid(112, 164, 0.2f, 176, 6, C2D_Color32(60, 150, 70, 255));
    C2D_DrawRectSolid(112, 184, 0.2f, 176, 6, C2D_Color32(40, 120, 50, 255));
    /* clouds */
    float cx = fmodf(clock_s * 6, 480) - 60;
    C2D_DrawRectSolid(cx, 30, 0.1f, 50, 12, C2D_Color32(255, 255, 255, 255));
    C2D_DrawRectSolid(cx + 10, 22, 0.1f, 30, 10, C2D_Color32(255, 255, 255, 255));

    /* Gerald */
    buildFrog();
    float fx = (400 - 16 * S) / 2, fy = 170 - FROG_ROWS * S;
    float bob = (((int)(clock_s * 2)) % 2) ? S / 2 : 0;
    if (sleeping) bob = (((int)(clock_s)) % 2) ? S / 2 : 0;
    float hop = (anim == A_PLAY) ? -fabsf(sinf(animT * 8)) * 36 : 0;
    drawSprite(fp, FROG_ROWS, fx, fy + bob + hop, S);

    /* animation extras */
    if (anim == A_EAT) {
        float p = 1.0f - animT / 1.6f;               /* 0..1 */
        float x = 330 - p * 130, y = 90 + sinf(p * 14) * 18 + p * 30;
        if (p < 0.85f) {
            C2D_DrawRectSolid(x, y, 0.5f, 6, 6, pal('k'));
            C2D_DrawRectSolid(x - 3, y - 4, 0.5f, 5, 4, C2D_Color32(255, 255, 255, 200));
            C2D_DrawRectSolid(x + 4, y - 4, 0.5f, 5, 4, C2D_Color32(255, 255, 255, 200));
        }
    } else if (anim == A_PLAY) {
        for (int i = 0; i < 3; i++) {
            float t = fmodf(animT * 1.5f + i * 0.33f, 1.0f);
            drawSprite(HEART, 6, 150 + i * 45, 110 - t * 70, 3);
        }
    } else if (anim == A_CLEAN) {
        for (int i = 0; i < 9; i++) {
            float x = 130 + i * 17 + sinf(clock_s * 4 + i) * 6;
            float y = 180 - fmodf(animT * 70 + i * 23, 110);
            C2D_DrawCircleSolid(x, y, 0.6f, 7 + (i % 3) * 2, C2D_Color32(200, 235, 255, 200));
            C2D_DrawCircleSolid(x - 1, y - 1, 0.6f, 4 + (i % 3) * 2, C2D_Color32(235, 250, 255, 220));
        }
    }
    if (sleeping) {
        C2D_DrawRectSolid(0, 0, 0.7f, 400, 240, C2D_Color32(0, 0, 50, 110));
        for (int i = 0; i < 3; i++) {
            float t = fmodf(clock_s * 0.6f + i * 0.33f, 1.0f);
            text("Z", 270 + t * 30 + i * 8, 120 - t * 60, 0.6f + i * 0.2f, C2D_Color32(255, 255, 255, (u8)(255 * (1 - t))));
        }
    }

    /* HUD */
    char buf[64];
    text("Gerald", 8, 4, 0.7f, C2D_Color32(20, 40, 80, 255));
    u32 d = g.age / 86400, h = (g.age / 3600) % 24, mi = (g.age / 60) % 60;
    snprintf(buf, sizeof buf, "Age: %lud %luh %lum", (unsigned long)d, (unsigned long)h, (unsigned long)mi);
    text(buf, 8, 26, 0.45f, C2D_Color32(20, 40, 80, 255));
    textCentered(msgT > 0 ? msg : status(), 200, 206, 0.6f, C2D_Color32(255, 255, 255, 255));
}

static const char *LABEL[4] = { "FEED", "PLAY", "CLEAN", "SLEEP" };
static const char *KEYL[4]  = { "(A)", "(B)", "(X)", "(Y)" };
#define BTN_Y 164
#define BTN_W 72
#define BTN_H 64

static void drawBar(const char *name, float v, float y)
{
    u32 col = v > 50 ? C2D_Color32(110, 220, 90, 255) : (v > 25 ? C2D_Color32(240, 200, 60, 255) : C2D_Color32(230, 80, 70, 255));
    text(name, 14, y - 2, 0.5f, C2D_Color32(255, 255, 255, 255));
    C2D_DrawRectSolid(90, y, 0, 200, 14, C2D_Color32(20, 50, 30, 255));
    C2D_DrawRectSolid(92, y + 2, 0, 196 * v / 100.0f, 10, col);
}

static void drawBottom(int pressed)
{
    C2D_DrawRectSolid(0, 0, 0, 320, 240, C2D_Color32(36, 80, 52, 255));
    text("Gerald's needs", 80, 6, 0.6f, C2D_Color32(255, 240, 150, 255));
    drawBar("Hunger", g.hunger, 40);
    drawBar("Fun",    g.happy,  66);
    drawBar("Clean",  g.clean,  92);
    drawBar("Energy", g.energy, 118);
    for (int i = 0; i < 4; i++) {
        float x = 4 + i * 80;
        u32 c = (pressed == i) ? C2D_Color32(250, 220, 90, 255) : C2D_Color32(88, 200, 70, 255);
        C2D_DrawRectSolid(x, BTN_Y + 4, 0.1f, BTN_W, BTN_H, C2D_Color32(20, 50, 30, 255));
        C2D_DrawRectSolid(x, BTN_Y, 0.2f, BTN_W, BTN_H, c);
        C2D_DrawRectSolid(x + 4, BTN_Y + 4, 0.3f, BTN_W - 8, BTN_H - 8, C2D_Color32(40, 120, 50, 255));
        textCentered(LABEL[i], x + BTN_W / 2, BTN_Y + 14, 0.55f, C2D_Color32(255, 255, 255, 255));
        textCentered(KEYL[i], x + BTN_W / 2, BTN_Y + 36, 0.45f, C2D_Color32(200, 240, 150, 255));
    }
    text("START: save & exit", 92, 226, 0.4f, C2D_Color32(200, 240, 150, 255));
}

/* ------------------------------------------------------------ main */
int main(void)
{
    romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    tb = C2D_TextBufNew(4096);
    srand((unsigned)time(NULL));

    audioInit();
    loadGame();
    play(S_RIBBIT);

    u64 last = osGetTime();
    int pressed = -1;
    float pressT = 0;

    while (aptMainLoop()) {
        u64 now = osGetTime();
        float dt = (now - last) / 1000.0f;
        last = now;
        if (dt > 0.25f) dt = 0.25f;
        clock_s += dt;

        hidScanInput();
        u32 kd = hidKeysDown();
        if (kd & KEY_START) break;

        int act = -1;
        if (kd & KEY_A) act = 0;
        if (kd & KEY_B) act = 1;
        if (kd & KEY_X) act = 2;
        if (kd & KEY_Y) act = 3;
        if (kd & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            for (int i = 0; i < 4; i++) {
                float x = 4 + i * 80;
                if (tp.px >= x && tp.px < x + BTN_W && tp.py >= BTN_Y && tp.py < BTN_Y + BTN_H) act = i;
            }
        }
        if (act >= 0) {
            pressed = act; pressT = 0.15f;
            doAction(act);
        }
        if (pressT > 0) { pressT -= dt; if (pressT <= 0) pressed = -1; }

        tick(dt);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(tb);
        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(top);
        drawTop();
        C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(bot);
        drawBottom(pressed);
        C3D_FrameEnd(0);
    }

    saveGame();
    audioExit();
    C2D_TextBufDelete(tb);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    return 0;
}
