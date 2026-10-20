/* Marcador de la carrera (Race_DrawHud 80043520, un jugador).
 * Nivel del marcador (controlador del humano +0x58; ajuste DAT_800b7112, 1 por defecto, cambia con SELECT
 * largo o con L1 en automatico, 8005efec): 0 solo posicion y vuelta, 1 + mapa (8004274c) y cuentarrevoluciones
 * con marcha y velocidad (800430e4), 2 + tiempos de vuelta y diferencia con el coche de delante / detras
 * (8002cd84). Graficos de la pagina de texturas 8 (Cpag): mapa en (0,0) y esfera en (0x80,0).
 * Iconos de poderes 80042544. Pendiente: dos jugadores, repeticion y el marcador sin
 * humanos (8004304c). */
#include <stdio.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gfx.h"
#include "font.h"
#include "globals.h"
#include "texslot.h"
#include "render.h"
#include "track.h"
#include "vecmath.h"
#include "frontend.h"
#include "exe.h"
#include "racecar.h"
#include "carphys_int.h"
#include "ai.h"
#include "powerup.h"
#include "cpag.h"
#include "snd.h"
#include "hud.h"

static uint8_t s_hudSetting[2] = { 1, 1 };   /* DAT_800b7112 */

/* pantalla ancha: lo de la izquierda y la derecha del marcador se pega a los bordes */
#define HUD_L(x) ((x) - g_WideMargin)
#define HUD_R(x) ((x) + g_WideMargin)
static int     s_needle = 0x5a0;              /* DAT_800af280: angulo de la aguja */
static int     s_needleVel;                   /* DAT_800af284 */

uint16_t Hud_TeamColor(int team)
{
    return Exe_U16(0x800aab38u + (uint32_t)team * 2);
}

static uint8_t* Hud_Phys(RaceCar* e)
{
    return e ? e->phys : NULL;
}

void Hud_Init(void)
{
    s_needle = 0x5a0;
    s_needleVel = 0;
}

int Hud_GetSetting(int player)
{
    return s_hudSetting[player & 1];
}

/* 8005efec */
void Hud_CycleLevel(RaceCar* e, int dir)
{
    uint8_t* ctrl = e->ctrl;
    int v = ctrl[0x58] + dir;
    if (v > 2)
        v = 0;
    if (v < 0)
        v = 2;
    ctrl[0x58] = (uint8_t)v;
    s_hudSetting[ctrl[0x59] & 1] = (uint8_t)v;
}

/* 80042330: aguja del cuentarrevoluciones con un muelle hacia las rpm del coche que sigue la camara */
void Hud_Update(int dt)
{
    RaceCar* e = RaceCam_GetTarget(0);
    uint8_t* c = Hud_Phys(e);
    if (!c || g_NumHumans >= 2)
        return;
    int target = (((((I32(c, 0x3ec) * 0x3c) >> 8) * 0xaaa) / 9000) + 0x5a0) & 0xfff;
    s_needleVel += ((((target - s_needle) * 0x400 - s_needleVel * 0x100) >> 4) * dt) >> 8;
    s_needle += (s_needleVel * dt) >> 8;
    if (s_needle < 0x5a0)
    {
        s_needle = 0x5a0;
        s_needleVel = 0;
    }
}

/* ---------------------------------------------------------------- primitivas 2D */

/* 8004242c: sprite de la pagina 8 (u, v, w, h) en (x, y) con la paleta de la region */
static void Hud_Sprite(const uint8_t* region, int u, int v, int w, int h, int semi, int x, int y)
{
    int du = 0, dv = 0;
    int tp = TexSlot_GetTPage(8, 0, &du, &dv);
    if (tp < 0 || !region)
        return;
    DR_TPAGE* t = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    SPRT* s = (SPRT*)Gfx_AllocPrim(sizeof(SPRT));
    if (!t || !s)
        return;
    setDrawTPage(t, 1, 1, tp & 0x9ff);
    Gfx_AddPrimDepth(0, t);
    setSprt(s);
    setSemiTrans(s, semi);
    setRGB0(s, 0x80, 0x80, 0x80);
    setUV0(s, u + du, v + dv);
    setWH(s, w, h);
    setXY0(s, x, y);
    s->clut = NR_Read16(region + 4);
    Gfx_AddPrimDepth(0, s);
}

/* 8001ab58: rectangulo de color 15 bits; coordenadas en medios pixeles */
void Hud_Rect15(unsigned color, int x2, int y2, int w2, int h2, int semi)
{
    if (semi)
    {
        DR_TPAGE* t = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
        if (t)
        {
            setDrawTPage(t, 1, 1, 0);
            Gfx_AddPrimDepth(0, t);
        }
    }
    TILE* r = (TILE*)Gfx_AllocPrim(sizeof(TILE));
    if (!r)
        return;
    setTile(r);
    setSemiTrans(r, semi);
    setRGB0(r, (color >> 7) & 0xf8, (color >> 2) & 0xf8, (color & 0x1f) << 3);
    setXY0(r, x2 >> 1, y2 >> 1);
    setWH(r, w2 >> 1, h2 >> 1);
    Gfx_AddPrimDepth(0, r);
}

/* ---------------------------------------------------------------- partes */

/* 8004274c: mapa del circuito (region (0,0) de la pagina 8) con un punto por coche */
static void Hud_Map(int view, int* w, int* h, int* y)
{
    const uint8_t* r = Cpag_FindRegion(8, 0, 0);
    int drawn = 0;
    if (r)
    {
        *w = (r[3] - r[1]) + 1;
        *h = (r[2] - r[0]) + 1;
        if (*w < 0x80 && *h < 0x80)
        {
            *y = 0xdc - *h;
            Hud_Sprite(r, 0, 0, *w, *h, 1, HUD_L(8), *y);
            drawn = 1;
        }
    }
    if (!drawn)
    {
        *w = 0x40;
        *h = 0x30;
        *y = 0xac;
    }
    RaceCar* target = RaceCam_GetTarget(view);
    for (int i = 0; i < g_NumCars; i++)
    {
        RaceCar* e = (RaceCar*)ENT(g_CarPhys[i]);
        int px = (((e->obj.pos[2] * 3) >> 16) + 6 - g_WideMargin) * 2;
        int py = (((e->obj.pos[0] * 3) >> 16) + *y - 2) * 2;
        Hud_Rect15(e == target ? 0x7fff : 1, px - 2, py - 2, 0xc, 0xc, 1);
        Hud_Rect15(Hud_TeamColor(e->team), px, py, 8, 8, 0);
    }
}

/* 800430e4: esfera del cuentarrevoluciones con la aguja, la marcha y la velocidad */
static void Hud_Gauge(uint8_t* c, int y)
{
    RaceCar* e = (RaceCar*)ENT(c);
    const uint8_t* r = Cpag_FindRegion(8, 0x80, 0);
    if (!r)
        return;
    Hud_Sprite(r, 0x80, 0, 0x40, 0x40, 1, HUD_R(0xf3), y - 0x21);
    int sn, cs;
    Math_SinCos(s_needle, &sn, &cs);
    LINE_F2* l = (LINE_F2*)Gfx_AllocPrim(sizeof(LINE_F2));
    if (l)
    {
        setLineF2(l);
        setRGB0(l, 0xf8, 0x20, 0x20);
        setXY2(l, HUD_R(0x114), y, HUD_R(0x114) + (int16_t)((uint32_t)(cs * 0x1a) >> 16), y + (int16_t)((uint32_t)(sn * 0x1a) >> 16));
        Gfx_AddPrimDepth(0, l);
    }
    Font_Select(2);
    char s[16];
    int g = I8(c, 0x3f4);
    s[0] = g == 0 ? 'R' : g < 0 ? 'N' : (char)('0' + g);
    s[1] = 0;
    Font_DrawString(s, HUD_R(0x101), y + 0x10, 8);
    snprintf(s, sizeof(s), "%03d", e->speed);
    Font_DrawString(s, HUD_R(e->speed > 99 ? 0x113 : 0x112), y + 0xf, 8);
}

static void Fmt_Time(char* out, size_t n, int t)
{
    snprintf(out, n, "%02d:%02d.%02d", t / 18000, t / 300 - (t / 18000) * 60, (t % 300) / 3);
}

/* 8002cd84: diferencia en tiempo (decimas * 30) con otro coche segun la distancia por la pista y la
 * velocidad media del coche */
int Hud_GapTime(uint8_t* me, uint8_t* other)
{
    int d = Abs_(Car_TrackDeltaPublic(me, other));
    int k = d << 2;
    if (d < 1)
    {
        d = 1;
        k = 4;
    }
    int v = (k + d) * 0x10000;
    int avg = 0x14;
    if (g_RaceTime != 0)
    {
        avg = (Div_(I32(me, 0x12c), g_RaceTime) * 300) >> 8;
        if (avg < 0x14)
            avg = 0x14;
    }
    int den = (avg * 4000) >> 1;
    int q = den ? (int)(((uint64_t)(uint32_t)v * (0x80000000u / (uint32_t)den)) >> 16) : 0;
    return (q >> 0xf) * 0x1e;
}

/* 80042544: icono de poder (pagina 8, 20 x 20 en (i * 0x14, 0x54), del 6 en adelante en la fila 0x68). El que se
 * lleva (slot 0) a la izquierda; los activos (slot 1..4) en columna a la derecha, con una barra del tiempo que
 * les queda (fraccion de la duracion de 800aa90c, 18 pixeles). */
static void Hud_PowerIcon(int icon, int slot, int ticks, int dx, int y)
{
    int x;
    if (slot == 0)
    {
        y += 0xe;
        x = HUD_L(9);
    }
    else
    {
        y += slot * 0x16 + 10;
        x = HUD_R(dx + 0x11a);
    }
    int i = icon > 5 ? icon - 6 : icon, v = icon < 6 ? 0x54 : 0x68;
    Hud_Sprite(Cpag_FindRegion(8, i * 0x14, v), i * 0x14, v, 0x14, 0x14, 0, x, y);
    int dur = Power_Duration(icon);
    if (ticks > 0 && dur > 0)
    {
        int fill = ticks * 0x12 / dur + 1;
        if (fill > 0x12)
            fill = 0x12;
        Hud_Rect15(0, (x + 0x15) * 2, y * 2, 8, 0x28, 0);
        Hud_Rect15(0x7fe0, (x + 0x16) * 2, (y - (fill - 0x13)) * 2, 4, fill * 2, 0);
    }
}

/* 800429d0 (parte): aviso para usar el poder, aviso de clima y CONTRA DIRECCION si se va hacia atras por la pista
 * mas de 300 tics */
static void Hud_Messages(int view, int y, int level)
{
    RaceCar* h = g_Humans[view];
    if (!h)
        return;
    uint8_t* ctrl = h->ctrl;
    uint8_t* c = h->phys;
    int off = level == 2 || g_GameInfo->isRestart ? 0x13 : 0;
    /* con el retrovisor (R1, ctrl +0x40 == 3) los mensajes bajan por debajo de el */
    int mir = !g_SplitScreen && I32(c, 0x330) == 0 && I32(ctrl, 0x40) == 3 ? (off ? 0xc : 0x1f) : 0;
    Font_Select(4);
    /* sin usar el poder 18000 tics (1 minuto): "To Use" alternando con "Press R2" (DAT_800af714 & 0x100, reloj de
     * tics de 1/300 s; aqui el tiempo de carrera). Con la asignacion de botones por defecto el poder va en R2. */
    if (I32(c, 0x330) == 0 && I32(ctrl, 0x68) > 18000)
    {
        int blink = g_RaceTime & 0x100;
        Font_DrawString(blink ? "Press" : "To", HUD_L(0x1e), y + 0xd, blink ? 9 : 1);
        Font_DrawString(blink ? "R2" : "Use", HUD_L(0x1e), y + 0x17, blink ? 9 : 1);
    }
    /* clima que se acerca por detras (ctrl +0x60, lo rellena 800323f0); al parpadear, si va deprisa, "Reduce Speed" */
    int alert = Power_AlertKind(h);
    if (alert >= 0 && I32(c, 0x330) == 0)
    {
        static const char* const names[3] = { "Storm Alert!", "Twister Alert!", "Freeze Alert!" };
        int blink = g_RaceTime & 0x100;
        const char* s = blink && h->speed >= 0x50 ? "Reduce Speed" : names[alert > 1 ? 2 : alert];
        int w, hh;
        Font_Measure(s, &w, &hh);
        Font_DrawString(s, 0xa0 - w / 2, y + off + mir, blink ? 9 : 1);
    }
    if (I32(c, 0x330) == 0 && g_Track && g_Track->numCurve > 0 && h->tpos.curve >= 0)
    {
        int n = g_Track->numCurve, a[3], b[3];
        Track_GetCurvePoint(h->tpos.curve, a);
        Track_GetCurvePoint(h->tpos.curve + 1 >= n ? 0 : h->tpos.curve + 1, b);
        int d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        int l = Vec_ApproxLen3(d);
        Vec_Scale(l < 2 ? 0 : (int)(0x80000000u / (unsigned)(l >> 1)), d);
        int diff = ((Math_Atan2(d[0], d[2]) & 0xfff) - I16(c, 0x432)) & 0xfff;
        if (diff > 0x800)
            diff -= 0x1000;
        I32(ctrl, 4) = Abs_(diff) < 0x601 ? -1 : I32(ctrl, 4) + g_FrameDelta;
        if (g_GameInfo->racingClass == 0 && I32(ctrl, 4) > 899)
        {
            Car_Reset(h, 0);
            I32(ctrl, 4) = -1;
        }
        if (I32(ctrl, 4) > 299)
        {
            int w, hh;
            Font_Measure("WRONG WAY", &w, &hh);
            Font_DrawString("WRONG WAY", 0xa0 - w / 2, y + off + mir + 10, 9);
            Speech_Say(0x2c, 8, h, 1);
        }
    }
    Font_Select(1);
}

/* 80043520 */
void Hud_Draw(int view)
{
    RaceCar* t = RaceCam_GetTarget(view);
    if (!t || (t->type != 1 && t->type != 0x11) || g_NumHumans == 0)
        return;
    int level = g_Humans[view] ? g_Humans[view]->ctrl[0x58] : 0;
    int mapW = 0, mapH = 0, mapY = 0xdc;
    if (level)
        Hud_Map(view, &mapW, &mapH, &mapY);
    int ty = 0x16;
    if (t->type == 1)
        Hud_Messages(view, ty, level);
    if (g_RaceTime < 0x4b0)
    {
        static const char* const txt[4] = { "3", "2", "1", "GO!!!" };
        static const int col[4] = { 5, 9, 6, 6 };
        int k = g_RaceTime < 300 ? 0 : g_RaceTime < 600 ? 1 : g_RaceTime < 900 ? 2 : 3;
        Font_Select(3);
        int w, h;
        Font_Measure(txt[k], &w, &h);
        Font_DrawString(txt[k], 0xa0 - w / 2, 0x2a, col[k]);
        Font_Select(1);
    }
    uint8_t* c = t->phys;
    if (!c)
        return;
    int pos = U8(c, 0x31a);
    uint8_t* ahead = pos == 0 || pos >= g_NumCars ? NULL : g_CarPhys[pos - 1];
    uint8_t* behind = pos < g_NumCars - 1 ? g_CarPhys[pos + 1] : NULL;
    if (I32(c, 0x330) != 0)
        return;
    int mate = -1;
    for (int i = 0; i < g_NumCars; i++)
        if (g_CarPhys[i] != c && ((RaceCar*)ENT(g_CarPhys[i]))->team == t->team)
        {
            mate = i;
            break;
        }
    int off = mate >= 0 ? 10 : 0;
    char s[64];
    Font_Select(5);
    s[0] = (char)('1' + pos);
    s[1] = 0;
    Font_DrawString(s, HUD_L(mapW + 0xb), (mapY + mapH) - (off + 0x1b), 8);
    Font_Select(0);
    snprintf(s, sizeof(s), "/%d", g_NumCars);
    Font_DrawString(s, HUD_L(mapW + 0x19), (mapY + mapH) - (off + 0x14), 8);
    if (mate >= 0)
    {
        snprintf(s, sizeof(s), "%d/%d", mate + 1, g_NumCars);
        Font_DrawString(s, HUD_L(mapW + 0xd), mapY + mapH - 0x14, Hud_TeamColor(t->team) | 0x8000);
    }
    Font_Select(1);
    if (level)
        Hud_Gauge(c, mapY + mapH - 0x1f);
    Font_Select(0);
    snprintf(s, sizeof(s), "lap %d/%d  ", I32(c, 0x328) + 1, g_Settings.laps);
    Font_DrawString(s, HUD_L(10), ty, 8);
    if (level > 1)
    {
        Font_DrawString("lap:", HUD_R(0xce), ty, 8);
        int lt = g_RaceTime - I32(c, 0x32c);
        if (lt < 0)
            lt = 0;
        Fmt_Time(s, sizeof(s), lt);
        Font_DrawString(s, HUD_R(0xf6), ty, 8);
        int slot = t->id - 1000;
        int best = slot >= 0 && slot < 6 ? g_CarRecords[slot].b * 3 : 0;
        if (t->type == 1 && I32(c, 0x328) + 1 >= g_Settings.laps)
            best = 0;
        if (best > 0)
        {
            Font_DrawString("best:", HUD_R(0xce), ty + 10, 8);
            Fmt_Time(s, sizeof(s), best);
        }
        else
        {
            Font_DrawString("total:", HUD_R(0xce), ty + 10, 8);
            int tt = g_RaceTime - 900;
            Fmt_Time(s, sizeof(s), tt < 0 ? 0 : tt);
        }
        Font_DrawString(s, HUD_R(0xf6), ty + 10, 8);
    }
    Font_Select(1);
    if (I8(c, 0x338) >= 0)
    {
        int held = I8(c, 0x338);
        Hud_PowerIcon(Exe_U8(0x800aa90fu + (uint32_t)held * 4), 0, held == POWER_MYSTERY ? I16(c, 0x33e) : 0, 0, ty);
    }
    for (int i = 0; i < U8(c, 0x339); i++)
        Hud_PowerIcon(Exe_U8(0x800aa90fu + (uint32_t)I8(c, 0x340 + i * 4) * 4), i + 1, I16(c, 0x342 + i * 4), 0, ty);
    if (g_RaceTime < 0x4b1 || level != 2)
        return;
    if (ahead && I32(ahead, 0x330) != 0)
        ahead = NULL;
    if (!ahead && !behind)
        return;
    uint8_t* other = ahead ? ahead : behind;
    RaceCar* oe = (RaceCar*)ENT(other);
    int gap = Hud_GapTime(c, other);
    const GridCar* g = &g_RaceGrid.car[oe->id - 1000];
    const char* name = oe->type == 1 ? g_Settings.playerName[g->player & 1] : g_Drivers[g->driver].name;
    Font_Select(0);
    if (gap < 0x7405)
        snprintf(s, sizeof(s), "%c%02d.%01d %s", ahead ? '-' : '+', gap / 300, gap / 0x1e - (gap / 300) * 10, name);
    else
        snprintf(s, sizeof(s), "(Far) %s", name);
    Font_DrawString(s, HUD_L(mapW + 10), mapY + mapH - 10, ahead ? 8 : 6);
    Font_Select(1);
}
