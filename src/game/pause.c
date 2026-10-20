/* Pausa durante la carrera, fin de la carrera y resultados.
 * Original: Race_UpdateState 80027e90 (START pausa; DAT_800af740 cuenta atras tras la meta y pasa al estado
 * 0xD), Pause_Menu 80025ad4 (menus por tablas: lista de opciones DAT_800aa88c + menu * 8 y textos
 * 800aa89c + menu * 0x38; Pause_SetQuitItem 80025a60 cambia la ultima opcion), Pause_UpdateTexts 8002552c,
 * Race_Results 8002896c (estado 0xD), Race_Finalize 800266f0 (tiempos estimados de los que no han terminado y
 * posiciones finales en los registros DAT_800b12ac + id * 0x18).
 * Pendiente: repeticion ("Show Replay" / "Restart Replay"), sonido de los menus (8003f6fc), menu de audio sin
 * efecto hasta portar el sonido, dos jugadores y campeonatos (Quit Series / Continue). */
#include <stdio.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gfx.h"
#include "font.h"
#include "pad.h"
#include "globals.h"
#include "frontend.h"
#include "exe.h"
#include "racecar.h"
#include "carphys_int.h"
#include "hud.h"
#include "ai.h"
#include "pause.h"
#include "snd.h"

int g_RacePaused;              /* DAT_800af710 */
int g_RaceEndTimer;            /* DAT_800af740 */
int g_RaceRestart;             /* DAT_800af691 */
int g_PauseCamPreview;         /* DAT_800af1d0: tics de camara en movimiento tras cambiarla en la pausa */

static int  s_pausePlayer = -1;    /* DAT_800af69c */
static int  s_sel;                 /* DAT_800af6a0 */
static int  s_menu;                /* DAT_800af6a4: 0 principal, 1 audio */
static int  s_resume;              /* DAT_800af6a8 */
static int8_t s_items[2][8];       /* DAT_800aa88c */
static char s_text[2][14][40];     /* textos de 800aa89c (se modifican como en el original) */
static int  s_resSel;              /* DAT_800af73c */
static int  s_resConfirm;          /* DAT_800af73d: 0 no, 1 NO, 2 SI */
static int  s_resTime;             /* DAT_800af70c */
static uint32_t s_prevButtons[2];
static int  s_hold[2][16];         /* estado de cada boton para 80025a10 */

/* 80025a10: 2 suelto, 0 recien pulsado, 3 pulsado, 1 recien soltado */
static int Edge(int player, int bit, uint32_t buttons)
{
    int* st = &s_hold[player & 1][bit & 15];
    if (!(buttons & (1u << (bit + 16))))
    {
        if (*st == 3)
            *st = 1;
        else if (*st != 2)
            *st = 2;
    }
    else if (*st == 2)
        *st = 0;
    else
        *st = 3;
    return *st;
}

#define B_START    3
#define B_UP       4
#define B_RIGHT    5
#define B_DOWN     6
#define B_LEFT     7
#define B_TRIANGLE 12
#define B_CIRCLE   13
#define B_CROSS    14
#define B_SQUARE   15

static const char* ExeStr(uint32_t addr)
{
    const char* s = (const char*)Exe_Ptr(addr);
    return s ? s : "";
}

static void Pause_LoadTexts(void)
{
    const uint8_t* items = Exe_Ptr(0x800aa88c);
    if (items)
        memcpy(s_items, items, sizeof(s_items));
    for (int m = 0; m < 2; m++)
        for (int i = 0; i < 14; i++)
        {
            uint32_t p = (uint32_t)Exe_S32(0x800aa89cu + (uint32_t)(m * 0x38 + i * 4));
            snprintf(s_text[m][i], sizeof(s_text[m][i]), "%s", p ? ExeStr(p) : "");
        }
}

/* sustituye el final "XXXXXX" / "000" de un texto */
static void SetTail(char* s, const char* mark, const char* value)
{
    char* p = strstr(s, mark);
    if (p)
        snprintf(p, 40 - (size_t)(p - s), "%s", value);
}

static void Quit_Texts(void)
{
    /* Race_UpdateState: "Quit Race" (campeonato: "Quit Series") */
    const char* w = g_Settings.champType ? "Series" : "Race";
    for (int i = 10; i <= 12; i++)
    {
        char tmp[40];
        snprintf(tmp, sizeof(tmp), "%s", s_text[0][i]);
        char* p = strstr(tmp, "XXXXXX");
        if (p)
        {
            char rest[40];
            snprintf(rest, sizeof(rest), "%s", p + 6);
            *p = 0;
            snprintf(s_text[0][i], sizeof(s_text[0][i]), "%s%s%s", tmp, w, rest);
        }
    }
}

/* 8002552c */
static void Pause_UpdateTexts(RaceCar* h)
{
    for (int i = 0; i < 4; i++)
    {
        char v[8];
        snprintf(v, sizeof(v), "%03d", ((g_Settings.volume[i] + 1) * 100) >> 7);
        size_t n = strlen(s_text[1][i]);
        if (n >= 3)
            snprintf(s_text[1][i] + n - 3, 4, "%s", v);
    }
    static const char* const modes[3] = { "Mono", "Stereo", "Dolby" };
    char* sm = strchr(s_text[1][4], 0x13);
    if (sm)
        snprintf(sm + 1, 40 - (size_t)(sm + 1 - s_text[1][4]), " %s", modes[g_Settings.audioMode % 3]);
    static const char* const cams[4] = { "Standard", "Follow", "Follow Close", "Bumper" };
    char* sc = strchr(s_text[0][2], 0x13);
    if (sc)
        snprintf(sc + 1, 40 - (size_t)(sc + 1 - s_text[0][2]), " %s", cams[RaceCam_GetViewMode(0) & 3]);
    static const char* const gauge[3] = { "Off", "Standard", "Full" };
    char* sg = strchr(s_text[0][3], 0x13);
    if (sg && h)
        snprintf(sg + 1, 40 - (size_t)(sg + 1 - s_text[0][3]), " %s", gauge[h->ctrl[0x58] % 3]);
}

/* 80025a60 */
static void Pause_SetQuitItem(int a, int b)
{
    int i = 4;
    if (!g_Settings.champType)
    {
        s_items[0][4] = 4;
        i = 5;
    }
    (void)a;
    s_items[0][i] = (int8_t)b;
    s_items[0][i + 1] = -1;
}

void Pause_Init(void)
{
    g_RacePaused = 0;
    g_RaceEndTimer = 0;
    g_RaceRestart = 0;
    s_pausePlayer = -1;
    s_resume = 0;
    s_resTime = -1;
    memset(s_hold, 0, sizeof(s_hold));
    for (int p = 0; p < 2; p++)
        for (int b = 0; b < 16; b++)
            s_hold[p][b] = 2;
    Pause_LoadTexts();
    Quit_Texts();
}

static void Pause_Begin(int player)
{
    g_RacePaused = 1;
    Snd_Pause(0);                 /* 8003fec4 */
    s_pausePlayer = player;
    s_resume = 0;
    s_sel = 0;
    s_menu = 0;
    Pause_UpdateTexts(g_Humans[player]);
    Pause_SetQuitItem(7, 10);
    for (int b = 0; b < 16; b++)
        if (s_hold[player][b] != 2)
            s_hold[player][b] = 3;
}

/* Race_UpdateState 80027e90 (un jugador): fin de carrera y START */
void Race_UpdateState(int dt)
{
    if (g_GameState == GAMESTATE_RUNNING && g_RaceEndTimer > 0 && !g_RacePaused)
    {
        g_RaceEndTimer -= dt;
        if (g_RaceEndTimer < 1)
        {
            g_RaceEndTimer = -1;
            g_GameState = GAMESTATE_RESULTS;
            s_resSel = 0;
            s_resConfirm = 0;
            for (int p = 0; p < 2; p++)
            {
                s_hold[p][B_CROSS] = 3;
                s_hold[p][B_START] = 3;
            }
        }
    }
    for (int p = 0; p < 2; p++)
    {
        if (!g_Humans[p])
            continue;
        uint32_t b = g_PadButtons[p];
        if (g_GameState != GAMESTATE_RESULTS && !g_RacePaused)
        {
            if (Edge(p, B_START, b) == 0)
                Pause_Begin(p);
        }
    }
    if (g_RacePaused && s_pausePlayer < 0 && g_GameState != GAMESTATE_RESULTS)
        g_RacePaused = 0;
}

/* 80025ad4 */
void Pause_Menu(void)
{
    if (s_pausePlayer < 0)
        return;
    int p = s_pausePlayer;
    RaceCar* h = g_Humans[p];
    int w = 0x11a, count = 6;
    if (g_Settings.champType && s_menu == 0)
        count--;
    int x2 = (0x280 - w) >> 1;
    Hud_Rect15(0x422, x2 - 2, 0x86, w + 4, count * 0x1c + 0x2a, 1);
    Hud_Rect15(0x422, x2 - 2, 0x86, w + 4, count * 0x1c + 0x2a, 1);
    Hud_Rect15(0x35ae, x2, 0xa4, w, count * 0x1c + 10, 1);
    int tw, th;
    Font_Measure("Game Paused", &tw, &th);
    Font_DrawString("Game Paused", 0xa0 - tw / 2, 0x44, 9);
    int y = 0x55;
    for (int i = 0; s_items[s_menu][i] >= 0; i++, y += 0xe)
    {
        if (i == s_sel)
            Hud_Rect15(0, (0x280 - w) / 2, (y - 1) * 2, w, 0x1c, 1);
        Font_DrawString(s_text[s_menu][(int)s_items[s_menu][i]], (x2 >> 1) + 6, y, i == s_sel ? 8 : 0x10);
    }
    if (!s_resume)
    {
        uint32_t b = g_PadButtons[p];
        int sq = Edge(p, B_SQUARE, b), cr = Edge(p, B_CROSS, b), ci = Edge(p, B_CIRCLE, b), tr = Edge(p, B_TRIANGLE, b);
        int le = Edge(p, B_LEFT, b), ri = Edge(p, B_RIGHT, b), up = Edge(p, B_UP, b), dn = Edge(p, B_DOWN, b);
        int st = Edge(p, B_START, b);
        (void)sq;
        (void)ci;
        int item = s_items[s_menu][s_sel];
        if (up == 0 || dn == 0 || cr == 0 || ((ri == 0 || le == 0) && s_menu == 0))
            Snd_PlayUi(0x22, h);  /* 8002651c */
        if (up == 0)
        {
            if (--s_sel < 0)
                s_sel = count - 1;
        }
        else if (dn == 0)
        {
            if (++s_sel >= count)
                s_sel = 0;
        }
        else if (cr != 0)
        {
            if (tr != 0 && st != 0)
            {
                if (ri == 3 || le == 3)
                {
                    int d = ri == 3 ? 1 : -1;
                    if (s_menu == 1 && item < 4)
                    {
                        int old = g_Settings.volume[item];
                        int v = old + d;
                        g_Settings.volume[item] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                        Snd_ApplySettings(g_Settings.volume, g_Settings.audioMode);
                        Pause_UpdateTexts(h);
                        /* al cambiar el porcentaje, el sonido 0x22 con ese volumen (8003f6d8) */
                        if (((old + 1) * 100) >> 7 != ((g_Settings.volume[item] + 1) * 100) >> 7)
                            Snd_PlayUiVol(0x22, h, Snd_EffectiveVolume(item));
                    }
                }
                else if (ri == 0 || le == 0)
                {
                    int d = ri == 0 ? 1 : -1;
                    if (s_menu == 1)
                    {
                        if (item == 4)
                        {
                            g_Settings.audioMode = (uint8_t)((g_Settings.audioMode + 3 + d) % 3);
                            Snd_ApplySettings(g_Settings.volume, g_Settings.audioMode);
                            Snd_PlayUi(0x22, h);
                            Pause_UpdateTexts(h);
                        }
                    }
                    else
                        switch (item)
                        {
                        case 2:
                            RaceCam_CycleView(0, d);
                            g_PauseCamPreview = 600;
                            Pause_UpdateTexts(h);
                            break;
                        case 3:
                            if (h)
                                Hud_CycleLevel(h, d);
                            Pause_UpdateTexts(h);
                            break;
                        case 5: s_items[0][4] = 6; break;
                        case 6: s_items[0][4] = 5; break;
                        case 0xb: Pause_SetQuitItem(7, 0xc); break;
                        case 0xc: Pause_SetQuitItem(7, 0xb); break;
                        }
                }
            }
            else if (s_menu == 1)
            {
                s_menu = 0;
                s_sel = 0;
            }
            else
                s_resume = 1;
        }
        else if (s_menu == 1)
        {
            if (item == 5)
            {
                s_menu = 0;
                s_sel = 0;
            }
        }
        else
            switch (item)
            {
            case 0: s_resume = 1; break;
            case 1: s_menu = 1; s_sel = 0; break;
            case 4: s_items[0][4] = 5; break;
            case 5: s_items[0][4] = 4; break;
            case 6:
                g_RaceRestart = 1;
                g_GameState = 5;
                s_items[0][4] = 4;
                break;
            case 10: Pause_SetQuitItem(7, 0xb); break;
            case 0xb: Pause_SetQuitItem(7, 10); break;
            case 0xc:
                g_GameState = 5;
                Pause_SetQuitItem(7, 10);
                break;
            }
    }
    /* ayuda: "Edit: ^ $" y "Select: * Exit: ~" (iconos de botones de la fuente) */
    const char* l1 = ExeStr(0x80010ad4);
    const char* l2 = ExeStr(Pad_GetType(p) == 2 ? 0x80010ae0 : 0x80010af8);
    int w1, w2, hh;
    Font_Measure(l1, &w1, &hh);
    Font_Measure(l2, &w2, &hh);
    int wm = w1 > w2 ? w1 : w2;
    Hud_Rect15(0x422, (0xa0 - wm / 2) * 2 - 10, (0xba - 0xb) * 2, wm * 2 + 0x14, 0x2e, 1);
    Font_DrawString(l1, 0xa0 - w1 / 2, 0xba - 0xb, 9);
    Font_DrawString(l2, 0xa0 - w2 / 2, 0xba, 9);
    if (s_resume && !(g_PadButtons[p] & (PADB_CROSS | PADB_TRIANGLE | PADB_START)))
    {
        s_resume = 0;
        s_sel = -1;
        s_pausePlayer = -1;
        g_RacePaused = 0;
        Snd_Pause(1);             /* 8003fe7c(1) */
    }
}

/* 800266f0: tiempos de los que no han terminado (por la diferencia con el primero) y posiciones finales */
void Race_Finalize(void)
{
    for (int i = 0; i < g_NumCars; i++)
    {
        uint8_t* c = g_CarPhys[i];
        RaceCar* e = (RaceCar*)ENT(c);
        if (I32(c, 0x330) == 0 && e->type != 1)
            I32(c, 0x330) = g_RaceTime - 900 + Hud_GapTime(c, g_CarPhys[0]);
        int slot = e->id - 1000;
        if (slot >= 0 && slot < 6 && g_CarRecords[slot].startPos == 0)
        {
            g_CarRecords[slot].startPos = (uint8_t)(i + 1);
            g_CarRecords[slot].a = (I32(c, 0x330) + 1) / 3;
        }
    }
    Race_UpdatePositions(0);
    for (int i = 0; i < g_NumCars; i++)
    {
        int slot = ((RaceCar*)ENT(g_CarPhys[i]))->id - 1000;
        if (slot >= 0 && slot < 6)
            g_CarRecords[slot].startPos = (uint8_t)(i + 1);
    }
}

static const char* Car_Name(RaceCar* e)
{
    const GridCar* g = &g_RaceGrid.car[e->id - 1000];
    return e->type == 1 ? g_Settings.playerName[g->player & 1] : g_Drivers[g->driver].name;
}

int Pause_ResultsTime(void)
{
    return s_resTime;
}

/* 8002896c: resultados (estado 0xD) con Restart Race / Show Replay / Exit Race */
void Race_Results(int dt)
{
    int n = g_NumCars;
    int h = n * 10 + 0x40;
    int top = -((h + 0x18) >> 1);
    int y0 = top + 0x90;
    Hud_Rect15(2, 0x38, (top + 0x76) * 2, 0x210, (h + 0x1c) * 2, 1);
    Hud_Rect15(0x294b, 0x3c, y0 * 2, 0x208, h << 1, 1);
    char s[96];
    snprintf(s, sizeof(s), "Race Results for %s", g_TrackName);
    int w, hh;
    Font_Measure(s, &w, &hh);
    Font_DrawString(s, 0xa0 - w / 2, top + 0x78, 9);
    static const char* const cls[4] = { "Kid Play", "Rookie", "Pro", "Elite" };
    static const char* const pu[4] = { "None", "Some", "More", "Mayhem" };
    snprintf(s, sizeof(s), "Class: %s     Power-Ups: %s", cls[g_GameInfo->racingClass & 3], pu[g_GameInfo->powerUps & 3]);
    Font_Measure(s, &w, &hh);
    Font_DrawString(s, 0xa0 - w / 2, top + 0x83, 0xd294);
    Font_DrawString("Total", 0xae, top + 0x92, 0xeb5f);
    Font_DrawString("Best Lap", 0xe8, top + 0x92, 0xeb5f);
    int allDone = 1;
    for (int i = 0; i < n; i++)
    {
        uint8_t* c = g_CarPhys[i];
        RaceCar* e = (RaceCar*)ENT(c);
        int yy = y0 + 0xe + i * 10;
        Hud_Rect15(Hud_TeamColor(e->team), 0x42, (yy + 4) * 2, 4, 4, 0);
        snprintf(s, sizeof(s), "%d. %s (%d)", i + 1, Car_Name(e), e->id - 999);
        int col = e->type == 1 ? 6 : 8;
        Font_DrawString(s, 0x26, yy, col);
        int slot = e->id - 1000;
        CarRecord* r = slot >= 0 && slot < 6 ? &g_CarRecords[slot] : NULL;
        int t = I32(c, 0x330);
        if (t == 0)
        {
            snprintf(s, sizeof(s), "Racing...");
            allDone = 0;
        }
        else
        {
            if (r && r->startPos == 0)
            {
                r->startPos = (uint8_t)(i + 1);
                r->a = (t + 1) / 3;
            }
            unsigned v = r ? (unsigned)r->a : (unsigned)((t + 1) / 3);
            snprintf(s, sizeof(s), "%02u:%02u.%02u", v / 6000, v / 100 - (v / 6000) * 60, v % 100);
        }
        Font_DrawString(s, 0xae, yy, col);
        if (I32(c, 0x334) > 0 && r)
        {
            unsigned v = (unsigned)r->b;
            snprintf(s, sizeof(s), "%02u:%02u.%02u", v / 6000, v / 100 - (v / 6000) * 60, v % 100);
            Font_DrawString(s, 0xe8, yy, col);
        }
    }
    if (allDone && !g_RacePaused)
    {
        if (s_resTime < 0)
            s_resTime = 0;
        s_resTime += dt;
        if (s_resTime > 17999)
            g_RacePaused = 1;
    }
    int yb = y0 + h;
    Hud_Rect15(1, 0x3c, (yb - 0x2a) * 2, 0x208, 4, 1);
    Hud_Rect15(0x1084, 0x158, (yb - 0x28) * 2, 2, 0x50, 1);
    Font_DrawString(ExeStr(0x80010ad4), 0xc0, yb - 0x20, 0xe);
    Font_DrawString(ExeStr(Pad_GetType(0) == 2 ? 0x80010d00 : 0x80010d0c), 0xcb, yb - 0x15, 0xe);
    static const char* const opts[3] = { "Restart Race", "Show Replay", "Exit Race" };
    for (int i = 0; i < 3; i++)
    {
        char o[48];
        snprintf(o, sizeof(o), "%s", opts[i]);
        int yy = yb - 0x25 + i * 0xb;
        if (i == s_resSel)
        {
            if (s_resConfirm == 1)
                strcat(o, ": \x13NO\x12/YES ?");
            else if (s_resConfirm == 2)
                strcat(o, ": \x12NO/\x13YES \x12?");
            Hud_Rect15(0, 0x3c, yy * 2, 0x11c, 0x18, 1);
        }
        Font_DrawString(o, 0x26, yy, i == s_resSel ? 0xeb5f : 0x10);
    }
    for (int p = 0; p < 2; p++)
    {
        if (!g_Humans[p])
            continue;
        uint32_t b = g_PadButtons[p];
        int cr = Edge(p, B_CROSS, b), le = Edge(p, B_LEFT, b), ri = Edge(p, B_RIGHT, b);
        int up = Edge(p, B_UP, b), dn = Edge(p, B_DOWN, b);
        if (cr == 0 || le == 0 || ri == 0 || up == 0 || dn == 0)
            Snd_PlayUi(0x22, g_Humans[p]);   /* 80029950 */
        if (cr == 0)
        {
            if (s_resConfirm == 1)
                s_resConfirm = 0;
            else if (s_resConfirm == 0)
                s_resConfirm = 1;
            else if (s_resConfirm == 2)
            {
                if (s_resSel == 1)
                    s_resConfirm = 0;   /* repeticion: pendiente */
                else
                {
                    Race_Finalize();
                    g_GameState = 0xb;
                    if (s_resSel == 0)
                        g_RaceRestart = 1;
                }
            }
        }
        else if (le == 0 || ri == 0)
        {
            if (s_resConfirm != 0)
                s_resConfirm = 3 - s_resConfirm;
        }
        else if (up == 0 || dn == 0)
        {
            s_resConfirm = 0;
            if (up == 0)
                s_resSel = s_resSel == 0 ? 2 : s_resSel - 1;
            else
                s_resSel = s_resSel == 2 ? 0 : s_resSel + 1;
        }
    }
}
