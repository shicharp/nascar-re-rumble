/* Vuelta de una carrera al front-end (80090fb4): records de pista, copas y desbloqueos, y la pantalla de records de
 * la pista (1400, 800855a0).
 *
 * Records de pista (g_SaveExtra.trackRecs, DAT_800b7198): por sede 0x120 bytes y por pista 0x60, cinco puestos de
 * 0x10 bytes {nombre[10], vuelta rapida u16 (+0xA), tiempo total u32 (+0xC)} y en +0x50 el record de la vuelta
 * rapida con el mismo formato. Los tiempos van en centesimas (CarRecord.a / .b); 9000000 = "<Time ?>".
 * El tiempo total solo cuenta en carreras de 4 vueltas (DAT_800b7116 == 4).
 *
 * Despues de una carrera (pantalla 2000 con g_GameInfo->nextMode == 0) se meten los records de cada jugador;
 * si hay alguno, la pantalla 1100 pide el nombre (texto 2002 record, 2003 vuelta rapida, 2004 los dos) y la 1400
 * los ensena. En una copa se va directamente a la 1400 y despues a la clasificacion (1500); al terminar la copa,
 * los tres primeros pasan a la 1700 (trofeo y records de copa). Despues, 80091ae0 mira que se ha desbloqueado
 * (1800) y se vuelve al menu principal (o a la 600 la primera vez que se gana todo en Elite).
 *
 * Records de copa (g_SaveExtra.cupRecs, DAT_800b7998): por sede 0x150 y por clase 0x70, cuatro puestos de 0x1C
 * {tiempo u32, tiempo del modo 4 u32, nombre[9] (+8), puesto (+0x11), nombre del modo 4 (+0x12), su puesto (+0x1B)};
 * por equipos (cupRecs2, DAT_800b8418): por sede 0x120 y clase 0x60, cuatro de 0x18 {tiempo, nombre (+4), puesto
 * (+0xD), compañero (+0xE)}. Sedes 0-5, 6 Winston Cup, 7 copa EA. Puesto 0 = copa ganada. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "nr.h"
#include "mem.h"
#include "gfx.h"
#include "asset.h"
#include "render.h"
#include "exe.h"
#include "font.h"
#include "globals.h"
#include "frontend.h"
#include "fe_internal.h"
#include "pad.h"
#include "savegame.h"

#define P(a) PROGRESS(a)

short g_FeSaveReturn;            /* DAT_800b7118 */

/* fe +0xA07 / +0xA08: los que suben al podio al terminar una copa (pantalla 1700) */
typedef struct
{
    uint8_t  team;           /* +0x00 copa por equipos: dos pilotos */
    char     name[9];        /* +0x01 nombre (0xDE + piloto + 1 = el de un piloto de la IA, 8008f0c8) */
    uint8_t  driver, car;    /* +0x0A */
    char     name2[9];       /* +0x0C compañero */
    uint8_t  driver2, car2;  /* +0x15 */
    uint32_t time;           /* +0x18 tiempo de la copa */
    uint8_t  pos;            /* +0x1C puesto (0..2) */
    int8_t   player;         /* +0x1D mando (-1 = IA) */
} CupWinner;

static CupWinner s_winners[3];
static uint8_t   s_numWinners;
static int8_t    s_teamCars[3][2];   /* fe +0x9E9: coches (hueco de la parrilla) de cada equipo */
static int8_t    s_teamPos[3];       /* fe +0x9EB: puesto de cada equipo */
static uint8_t*  s_unlock;           /* fe +0xA78: lo desbloqueado (0x24 bytes, ver Fe_CheckUnlocks) */
int8_t           g_FeUnlockShow;     /* fe +0xA84: clase desbloqueada; -1 = todo ganado en Elite (pantalla 600) */
static int       s_kidTime;          /* fe +0xA8C: tiempo de una copa de Rookie bajo el limite de 800abe64 */

static uint32_t Rd32(const uint8_t* p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static uint16_t Rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static void Wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void Wr16(uint8_t* p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static uint8_t* TrackRecs(int venue, int track)
{
    return g_SaveExtra.trackRecs + venue * 0x120 + track * 0x60;
}

/* primer puesto de las dos tablas de copa de una sede y clase ganado por un jugador (+0x11 / +0xD = puesto 0) */
static int CupWon(int venue, int cls)
{
    const uint8_t* r = g_SaveExtra.cupRecs + venue * 0x150 + cls * 0x70;
    const uint8_t* u = g_SaveExtra.cupRecs2 + venue * 0x120 + cls * 0x60;
    return (Rd32(r) != 0 && r[0x11] == 0) || (Rd32(u) != 0 && u[0xd] == 0);
}

/* 8008aafc: hueco de la parrilla del mando p; con p < 0, el equipo del mando ~p (pantalla 1500) */
static int GridFind(int p)
{
    if (p < 0)
    {
        for (int t = 0; t < 3; t++)
            if ((int8_t)g_RaceGrid.car[s_teamCars[t][0]].player == ~p ||
                (int8_t)g_RaceGrid.car[s_teamCars[t][1]].player == ~p)
                return t;
        return 0;
    }
    for (int i = 0; i < g_RaceGrid.count; i++)
        if ((int8_t)g_RaceGrid.car[i].player == p)
            return i;
    return 0;
}

/* 8008f0c8: nombre de un coche de la parrilla para las tablas de copa */
static void GridName(char* dst, const GridCar* c)
{
    if (c->player == 0xff)
    {
        dst[0] = (char)0xde;
        dst[1] = (char)(c->driver + 1);
        dst[2] = 0;
    }
    else
        snprintf(dst, 9, "%s", c->player < 2 ? g_Settings.playerName[c->player] : Fe_GetString(0xa2b));
}

/* 8008ee98: tiempo mm:ss.cc (o "<Time ?>"); con color negativo solo mide. Devuelve el ancho. */
int Fe_DrawTime(int r, int g, int b, int x, int y, int t)
{
    char s[16];
    if (t == 9000000)
        snprintf(s, sizeof(s), "<Time ?>");
    else
        snprintf(s, sizeof(s), "%02d:%02d.%02d", t / 100 / 60, t / 100 % 60, t % 100);
    if (r >= 0 && g >= 0 && b >= 0)
        Font_DrawString(s, x, y, (unsigned)((r << 10 | g << 5 | b) & 0xffff) | 0x8000);
    int w, h;
    Font_Measure(s, &w, &h);
    return w;
}

/* 80091ae0: desbloqueos por la carrera o la copa (solo fuera de Kid Play). s_unlock: [0] clase nueva, [1] nuevo nivel
 * de DAT_800b7136, [2..4] pistas sueltas (DAT_800b7137..7139), [5 + sede * 3 + clase] estado de la copa de una sede,
 * [0x1A..0x23] especiales (DAT_800b714f..7158). Si hay algo, se pregunta si guardar al volver al menu. */
static int Fe_CheckUnlocks(void)
{
    GameSettings* gs = &g_Settings;
    int changed = 0;
    if (gs->racingClass == 0)
        return 0;
    free(s_unlock);
    s_unlock = (uint8_t*)calloc(1, 0x24);
    g_FeUnlockShow = 0;
    int c = gs->racingClass - 1;
    if (s_kidTime == 0 || gs->maxClassUnlocked != 0)
        s_kidTime = 0;
    else
    {
        /* copa de Rookie con buen tiempo sin Pro: Pro, el segundo nivel de sedes y las pistas sueltas */
        gs->maxClassUnlocked = 1;
        s_unlock[0] = 1;
        g_FeUnlockShow = 1;
        if (P(0x800b7136) == 0)
        {
            s_unlock[1] = 3;
            P(0x800b7136) = 2;
        }
        else if (P(0x800b7136) == 1)
        {
            P(0x800b7136) = 2;
            s_unlock[1] = 2;
        }
        for (int k = 0; k < 3; k++)
            if (P(0x800b7137 + k) == 0)
            {
                P(0x800b7137 + k) = 1;
                s_unlock[2 + k] = 1;
            }
        changed = 1;
    }
    /* las 6 copas de sede ganadas en la clase maxima: la siguiente clase (o, en Elite, la pantalla 600) */
    int v = 0;
    while (v < 6 && CupWon(v, c))
        v++;
    if (v == 6 && c == gs->maxClassUnlocked)
    {
        if (c < 2)
        {
            gs->maxClassUnlocked = gs->racingClass;
            s_unlock[0] = gs->racingClass;
            changed = 1;
            g_FeUnlockShow = (int8_t)gs->maxClassUnlocked;
        }
        else if (!g_SaveExtra.b711c)
        {
            g_FeUnlockShow = -1;
            g_SaveExtra.b711c = 1;
        }
    }
    int won = 0;
    for (v = 0; v < 6; v++)
        if (CupWon(v, c))
            won++;
    if (won / 2 < 3 && P(0x800b7136) < won / 2)
    {
        changed = 1;
        P(0x800b7136)++;
        s_unlock[1] = P(0x800b7136);
    }
    int k = (won + 1) / 2;
    if (k >= 1 && P(0x800b7136 + k) == 0)
    {
        changed = 1;
        P(0x800b7136 + k) = 1;
        s_unlock[k + 1] = 1;
    }
    /* especiales recogidos en la carrera (g_GameInfo +0x4C, bits de DAT_800abeec) */
    for (int i = 0; i < 10; i++)
    {
        if (!(g_GameInfo->unlockMask & 1 << i))
            continue;
        int idx = Exe_U8(0x800abeecu + i);
        if (g_Settings.progress[idx] == 0)
        {
            changed = 1;
            g_Settings.progress[idx] = 1;
            s_unlock[idx - 0x14] = 1;
        }
    }
    /* copa EA (sede 7) ganada: el especial DAT_800b714f */
    if (CupWon(7, c) && P(0x800b714f) == 0)
    {
        changed = 1;
        P(0x800b714f) = 1;
        s_unlock[0x1a] = 1;
    }
    /* estado de la copa jugada: 1 ganada, 2 ganada tambien la segunda tabla (+4 / +0x1B) */
    if (gs->champType && (gs->curVenue < 6 || gs->champType == 2))
    {
        int cv = gs->champType == 2 ? 6 : gs->curVenue;
        const uint8_t* r = g_SaveExtra.cupRecs + cv * 0x150 + c * 0x70;
        int st = CupWon(cv, c);
        if (Rd32(r + 4) != 0 && r[0x1b] == 0)
            st++;
        uint8_t* p = &P(0x800b713a + cv * 3);
        if (p[c] < st)
        {
            if (st == 2)
                for (int j = 0; j < c; j++)
                {
                    p[j] = 2;
                    s_unlock[cv * 3 + j + 5] = 2;
                }
            changed = 1;
            p[c] = (uint8_t)st;
            s_unlock[cv * 3 + c + 5] = (uint8_t)st;
        }
    }
    if (!changed)
    {
        free(s_unlock);
        s_unlock = NULL;
    }
    else
        g_FeSaveReturn = 1;
    return changed;
}

/* records de pista de los jugadores al acabar una carrera (parte de 80090fb4 en la pantalla 2000).
 * pos[i]: puesto de la tabla (o -1), pos[2 + i]: vuelta rapida. */
static void InsertTrackRecords(int* pos)
{
    GameSettings* gs = &g_Settings;
    uint8_t* t = TrackRecs(gs->venue, gs->track);
    for (int i = 0; i < gs->numPlayers && i < 2; i++)
    {
        const CarRecord* r = &g_CarRecords[GridFind(i)];
        if (r->startPos == 0)
            continue;   /* no ha terminado */
        if (gs->laps == 4)
            for (int k = 4; k >= 0; k--)
            {
                uint32_t rec = Rd32(t + k * 0x10 + 0xc);
                if (rec == 0 || (uint32_t)r->a < rec)
                    pos[i] = k;
            }
        unsigned lap = Rd16(t + 0x5a);
        if (lap == 0 || (uint32_t)r->b < lap)
            pos[2 + i] = 1;
        if (pos[i] >= 0)
        {
            for (int k = 4; k > pos[i]; k--)
                memcpy(t + k * 0x10, t + (k - 1) * 0x10, 0x10);
            uint8_t* e = t + pos[i] * 0x10;
            snprintf((char*)e, 10, "%s", gs->playerName[i]);
            Wr32(e + 0xc, (uint32_t)r->a);
            Wr16(e + 0xa, (unsigned)r->b);
        }
        if (pos[2 + i])
        {
            snprintf((char*)t + 0x50, 10, "%s", gs->playerName[i]);
            Wr32(t + 0x5c, (uint32_t)r->a);
            Wr16(t + 0x5a, (unsigned)r->b);
            pos[i] += 100;
        }
        g_SaveExtra.recordPos[i] = (int8_t)pos[i];
    }
}

/* fin de una copa (parte de 80090fb4 en la pantalla 1500): los tres primeros, para la pantalla 1700 */
static void CupPodium(void)
{
    GameSettings* gs = &g_Settings;
    s_numWinners = 0;
    for (int i = 0; i < gs->numPlayers && i < 2; i++)
    {
        CupWinner* w = &s_winners[i];
        if (!gs->teamRace)
        {
            int who = i;
            if (gs->numPlayers != 1 && g_CarRecords[GridFind(0)].startPos >= g_CarRecords[GridFind(1)].startPos)
                who = i == 0;   /* el mejor clasificado primero */
            int slot = GridFind(who);
            const CarRecord* r = &g_CarRecords[slot];
            if (gs->racingClass == 1 && gs->champType == 1 &&
                (uint32_t)r->d < (uint32_t)Exe_S32(0x800abe64u + gs->curVenue * 4))
                s_kidTime = r->d;
            if (r->startPos > 2)
                break;
            const GridCar* c = &g_RaceGrid.car[slot];
            w->team = 0;
            GridName(w->name, c);
            w->driver = c->driver;
            w->car = c->car;
            w->pos = r->startPos;
            w->time = (uint32_t)r->d;
            w->player = (int8_t)c->player;
        }
        else
        {
            int who = 0;
            if (gs->numPlayers != 1)
            {
                if (i != 0 && s_teamPos[GridFind(-1)] == s_teamPos[GridFind(-2)])
                    break;   /* los dos jugadores en el mismo equipo */
                who = s_teamPos[GridFind(-1)] < s_teamPos[GridFind(-2)] ? i != 0 : i == 0;
            }
            int slot = GridFind(who), mate = 0;
            while (mate < 6 && !(mate != slot && g_RaceGrid.car[mate].team == g_RaceGrid.car[slot].team))
                mate++;
            const GridCar* c = &g_RaceGrid.car[slot];
            const GridCar* m = &g_RaceGrid.car[mate < 6 ? mate : 0];
            w->team = 1;
            GridName(w->name, c);
            w->driver = c->driver;
            w->car = c->car;
            GridName(w->name2, m);
            w->driver2 = m->driver;
            w->car2 = m->car;
            w->pos = (uint8_t)s_teamPos[GridFind(~(int)(int8_t)c->player)];
            w->time = (uint32_t)(g_CarRecords[slot].d + g_CarRecords[mate < 6 ? mate : 0].d);
            w->player = m->player == 0xff ? (int8_t)c->player : -1;
        }
        s_numWinners++;
    }
}

/* 80090fb4: lo que sigue a la pantalla actual al volver de una carrera (2000), de los records (1400), de la
 * clasificacion de la copa (1500), del podio (1700) o de los desbloqueos (1800). */
int Fe_AfterRace(void)
{
    FrontendState* fe = g_Frontend;
    GameSettings* gs = &g_Settings;
    switch (fe->screen)
    {
    case 1500:
        if (gs->champType)
        {
            gs->venue = gs->savedVenue;
            gs->track = gs->savedTrack;
        }
        if (gs->raceIndex < gs->cupRaces)
        {
            Fe_StartRace(0);   /* siguiente carrera de la copa */
            return fe->screen;
        }
        CupPodium();
        if (s_numWinners)
        {
            g_FeSaveReturn = 1;
            return 1700;
        }
        break;
    case 2000:
        g_FeUnlockShow = 0;   /* (el original los pone a 0 con el estado del front-end) */
        s_kidTime = 0;
        if (g_RaceExit != 0)
            break;   /* carrera abandonada */
        if (gs->champType)
        {
            gs->savedVenue = gs->venue;
            gs->savedTrack = gs->track;
            gs->venue = gs->curVenue;
            gs->track = gs->curTrack;
        }
        {
            int pos[4] = { -1, -1, 0, 0 };
            g_SaveExtra.recordPos[0] = g_SaveExtra.recordPos[1] = -1;
            InsertTrackRecords(pos);
            if (g_SaveExtra.recordPos[0] >= 0 || g_SaveExtra.recordPos[1] >= 0)
            {
                g_FeSaveReturn = 1;
                fe->portPopup = (char)((g_SaveExtra.recordPos[0] >= 0) + (g_SaveExtra.recordPos[1] >= 0));
                /* (el original mira el byte +0x14 del registro del jugador 1, que nunca vale -1) */
                int first = g_CarRecords[GridFind(0)].taken == 0xff;
                fe->namePlayer[0] = (uint8_t)first;
                fe->namePlayer[1] = (uint8_t)!first;
                if (gs->champType)
                    return 1400;
                /* (el original pone el texto solo al primer recuadro) */
                for (int i = 0; i < fe->portPopup; i++)
                {
                    int p = fe->namePlayer[0];
                    fe->champReady[p] = (char)(pos[p] >= 100 ? 4 : pos[p] == 99 ? 3 : 2);
                }
                return 1100;
            }
        }
        if (gs->champType)
            return 1500;
        break;
    case 1400:
        if (gs->champType)
            return 1500;
        break;
    case 1700:
        break;
    case 1800:
        goto menu;
    default:
        return fe->screen;
    }
    if (Fe_CheckUnlocks())
        return 1800;
menu:
    if (gs->numPlayers == 1)
        fe->raceMenu1P[gs->raceMenuCursor].active = 1;
    else if (gs->numPlayers == 2)
        fe->raceMenu2P[gs->raceMenuCursor].active = 1;
    return g_FeUnlockShow == -1 ? 600 : 100;
}

/* ---------------------------------------------------------------- pantalla 1400: records de la pista */

static short  s_slide;        /* fe +0x99E */
static int8_t s_hiRank[2];    /* fe +0x9E6 / +0x9E7: puesto del record nuevo de cada jugador */
static int8_t s_hiLap;        /* fe +0x9E8: jugador con la vuelta rapida nueva */

/* 800855a0: los cinco mejores tiempos y la vuelta rapida de la pista (desde la 1300 con cuadrado o despues de
 * la carrera); los records nuevos en amarillo. Al entrar copia otra vez el nombre de cada jugador (puede haberlo
 * cambiado en la 1100). */
void Fe_Screen1400(void)
{
    FrontendState* fe = g_Frontend;
    GameSettings* gs = &g_Settings;
    int port = gs->numPlayers == 2 ? -1 : 0;
    uint8_t* t = TrackRecs(gs->venue, gs->track);
    if (fe->firstFrame)
    {
        Fe_SetBackground(5);
        fe->popup = -1;
        s_hiRank[0] = s_hiRank[1] = s_hiLap = -1;
        for (int p = 0; p < 2; p++)
        {
            int8_t* rp = &g_SaveExtra.recordPos[p];
            if (*rp > 98)
            {
                snprintf((char*)t + 0x50, 10, "%s", gs->playerName[p]);
                *rp -= 100;
                s_hiLap = (int8_t)p;
            }
            if (*rp >= 0)
            {
                snprintf((char*)t + *rp * 0x10, 10, "%s", gs->playerName[p]);
                s_hiRank[p] = *rp;
            }
        }
        s_slide = 6;
    }
    Fe_DrawBackground(0x800);
    Fe_DrawHelp(0, HelpText(fe->prevScreen == 1300 ? 3 : 9), 0x21, 0xd5, 0x23, 0x109, 0);

    /* sede (fuente 2) y pista (fuente 1) centradas juntas */
    const char* track = fe->venues[gs->venue][gs->track + 1].name;
    const char* venue = fe->venues[gs->venue][0].name;
    int wt, wv, h;
    Font_Select(1);
    Font_Measure(track, &wt, &h);
    Font_Select(2);
    Font_Measure(venue, &wv, &h);
    int half = (wt + 0xc + wv) / 2;
    Font_DrawString(venue, 0x9b - half, 0x10, 8);
    Font_Select(1);
    Font_DrawString(track, wv - half + 0xa7, 0x14, 8);

    int limit = s_slide + 0x36;
    for (int i = 0; i < 5 && i * 0x11 + 0x57 < limit; i++)
    {
        const uint8_t* e = t + i * 0x10;
        int b = (i == s_hiRank[0] || i == s_hiRank[1]) ? 0 : 0x1f;
        Font_Select(4);
        if (Rd32(e + 0xc) == 0)
        {
            Font_Select(0);
            Font_DrawString("----", 0x4b, i * 0x11 + 0x4d, 8);   /* DAT_800af47c */
            Font_DrawString("----", 0xa7, i * 0x11 + 0x4d, 8);
            Font_DrawString("----", 0xed, i * 0x11 + 0x4d, 8);
        }
        else
        {
            Fe_DrawTextRgb(0x1e, 0x1e, b, (const char*)e, 0x3e, i * 0x11 + 0x4b, 0);
            Font_Select(0);
            Fe_DrawTime(0x1e, 0x1e, b, 0x9c, i * 0x11 + 0x4e, Rd16(e + 0xa));
            Fe_DrawTime(0x1e, 0x1e, b, 0xe2, i * 0x11 + 0x4e, (int)Rd32(e + 0xc));
        }
    }
    if (limit > 0xc6)
    {
        int b = s_hiLap >= 0 ? 0 : 0x1f;
        Font_Select(4);
        if (Rd32(t + 0x5c) == 0)
        {
            Font_Select(0);
            Font_DrawString("----", 0x4b, 0xbc, 8);
            Font_DrawString("----", 0xa7, 0xbc, 8);
            Font_DrawString("----", 0xed, 0xbc, 8);
        }
        else
        {
            Fe_DrawTextRgb(0x1e, 0x1e, b, (const char*)t + 0x50, 0x3e, 0xba, 0);
            Font_Select(0);
            Fe_DrawTime(0x1e, 0x1e, b, 0x9c, 0xbd, Rd16(t + 0x5a));
            Fe_DrawTime(0x1e, 0x1e, b, 0xe2, 0xbd, (int)Rd32(t + 0x5c));
        }
    }

    Fe_ReadPads();
    if (Fe_PadPressed(PADB_CROSS, port, 1))
    {
        if (fe->prevScreen != 1300)
            fe->nextScreen = (short)Fe_AfterRace();
    }
    else if (Fe_PadPressed(PADB_TRIANGLE, port, 1) && fe->prevScreen == 1300)
        fe->nextScreen = 1300;
    Fe_PadRepeatReset();
    if (s_slide < 0x92)
    {
        s_slide = (short)(s_slide + g_FrameDelta);
        if (s_slide > 0x92)
            s_slide = 0x92;
    }
}

/* ---------------------------------------------------------------- pantalla 1500: clasificacion de la copa */

static short s_cupView;   /* fe +0x91A: 0 clasificacion de la copa, 1 resultado de la carrera */

static const char* CarName(const GridCar* c)
{
    return c->player == 0xff ? g_Drivers[c->driver].name : g_Settings.playerName[c->player & 1];
}

/* color de una fila: el equipo del jugador 1 amarillo, el del 2 naranja, el resto blanco */
static void RowColor(int k, int* r, int* g, int* b)
{
    *r = 0x1c;
    *b = 0;
    if (g_RaceGrid.car[k].team == g_RaceGrid.car[GridFind(0)].team)
        *g = 0x1c;
    else if (g_Settings.numPlayers == 2 && g_RaceGrid.car[k].team == g_RaceGrid.car[GridFind(1)].team)
        *g = 0xe;
    else
        *g = *b = 0x1c;
}

static void DrawNumber(int n, int x, int y, int r, int g, int b)   /* 80045098 */
{
    char s[16];
    snprintf(s, sizeof(s), "%d", n);
    Font_DrawString(s, x, y, (unsigned)(r << 10 | g << 5 | b) | 0x8000);
}

/* registros y parrilla en el orden de CarRecord.startPos (+ base: 1 = puesto de llegada 1..n, 0 = clasificacion) */
static void SortByPos(int base)
{
    int n = g_RaceGrid.count < 6 ? g_RaceGrid.count : 6;
    CarRecord recs[8];
    GridCar cars[8];
    memset(recs, 0, sizeof(recs));
    memset(cars, 0, sizeof(cars));
    for (int i = 0; i < n; i++)
    {
        int p = (g_CarRecords[i].startPos - base) & 7;
        recs[p] = g_CarRecords[i];
        cars[p] = g_RaceGrid.car[i];
    }
    for (int i = 0; i < n; i++)
    {
        g_CarRecords[i] = recs[i];
        g_RaceGrid.car[i] = cars[i];
    }
}

/* primer frame de 80085d90: puntos de la carrera (DAT_800af468: 10, 8, 6, 4, 2, 1) y tiempo acumulado, orden de
 * llegada y puesto en la copa (por puntos, despues por tiempo); por equipos, la suma de los dos */
static void CupUpdateStandings(void)
{
    int n = g_RaceGrid.count < 6 ? g_RaceGrid.count : 6;
    CarRecord* r = g_CarRecords;
    for (int i = 0; i < n; i++)
    {
        r[i].points = (uint8_t)(r[i].points + Exe_U8(0x800af467u + r[i].startPos));
        r[i].d += r[i].a;
    }
    SortByPos(1);
    if (!g_Settings.teamRace)
    {
        for (int i = 0; i < n; i++)
        {
            int rank = n - 1;
            for (int j = 0; j < n; j++)
            {
                if (j == i)
                    continue;
                uint8_t pi = (uint8_t)r[i].points, pj = (uint8_t)r[j].points;
                if (pj < pi || (pi == pj && ((uint32_t)r[i].d < (uint32_t)r[j].d || (r[i].d == r[j].d && i < j))))
                    rank--;
            }
            r[i].startPos = (uint8_t)rank;
        }
        return;
    }
    for (int t = 0; t < 3; t++)
    {
        int m = 0;
        for (int k = 0; k < 6; k++)
            if (g_RaceGrid.car[k].team == t && m < 2)
                s_teamCars[t][m++] = (int8_t)k;
    }
    for (int t = 0; t < 3; t++)
    {
        int rank = 2;
        const CarRecord *a = &r[s_teamCars[t][0]], *b = &r[s_teamCars[t][1]];
        unsigned pt = (uint8_t)a->points + (uint8_t)b->points;
        uint32_t tt = (uint32_t)a->d + (uint32_t)b->d;
        for (int u = 0; u < 3; u++)
        {
            if (u == t)
                continue;
            const CarRecord *c = &r[s_teamCars[u][0]], *d = &r[s_teamCars[u][1]];
            unsigned pu = (uint8_t)c->points + (uint8_t)d->points;
            uint32_t tu = (uint32_t)c->d + (uint32_t)d->d;
            if (pu < pt || (pu == pt && (tt < tu || (tt == tu && t < u))))
                rank--;
        }
        s_teamPos[t] = (int8_t)rank;
    }
    for (int t = 0; t < 3; t++)
    {
        const CarRecord *a = &r[s_teamCars[t][0]], *b = &r[s_teamCars[t][1]];
        int second = (uint8_t)a->points < (uint8_t)b->points ||
                     ((uint8_t)a->points == (uint8_t)b->points && (uint32_t)b->d < (uint32_t)a->d);
        r[s_teamCars[t][second]].startPos = (uint8_t)(s_teamPos[t] * 2);
        r[s_teamCars[t][!second]].startPos = (uint8_t)(s_teamPos[t] * 2 + 1);
    }
}

/* titulo con la sede (fuente 2) y la pista (fuente 1) centradas juntas */
static void DrawTrackTitle(int y)
{
    FrontendState* fe = g_Frontend;
    const char* track = fe->venues[g_Settings.venue][g_Settings.track + 1].name;
    const char* venue = fe->venues[g_Settings.venue][0].name;
    int wt, wv, h;
    Font_Select(1);
    Font_Measure(track, &wt, &h);
    Font_Select(2);
    Font_Measure(venue, &wv, &h);
    int half = (wt + 0xc + wv) / 2;
    Font_DrawString(venue, 0x9b - half, y, 8);
    Font_Select(1);
    Font_DrawString(track, wv - half + 0xa7, y + 4, 8);
}

/* 80085d90: despues de cada carrera de una copa. Arriba / abajo cambia entre la clasificacion (puntos y tiempo
 * acumulados; por equipos, de dos en dos con la suma) y el resultado de la carrera; X sigue (siguiente carrera o
 * podio, 80090fb4) y triangulo pregunta si abandonar la copa (0xA28). */
void Fe_Screen1500(void)
{
    FrontendState* fe = g_Frontend;
    GameSettings* gs = &g_Settings;
    int port = gs->numPlayers == 2 ? -1 : 0;
    int n = g_RaceGrid.count < 6 ? g_RaceGrid.count : 6;
    if (fe->firstFrame)
    {
        Fe_SetBackground(8);
        Fe_DecodeGmdc(10, 0x380, 0);      /* titulos "Standings" / "Race Results" */
        Fe_DecodeGmdc(0xb, 0x380, 0x20);
        Fe_DecodeGmdc(8, 0x300, 0x100);   /* tabla normal y por equipos */
        Fe_DecodeGmdc(9, 0x300, 0x180);
        Fe_DecodeGmdc(0xc, 0x380, 0x40);
        fe->popup = 0;
        s_cupView = 0;
        CupUpdateStandings();
        s_slide = 6;
    }
    Fe_DrawBackground(0x800);
    Fe_DrawHelp(0, HelpText(10), 0x21, 0xd5, 0x23, 300, 0);
    DrawTrackTitle(0x11);
    Fe_DrawVramRect(0x380, s_cupView * 0x20, 99, 0x1f, 0x27, 0x33, 0, 0);
    Fe_DrawVramRect(0x380, 0x40, 0xf, 0x1a, 0x91, 0x35, 0, 0);
    int ir, ig, ib;
    Fe_ItemColor(&ir, &ig, &ib, 1);
    Fe_DrawFrame(ir * -2 + 0x96, ig * -2 + 0x96, ib * -2 + 0xa0, 0x90, 0x35, 0xa0, 0x4e);
    Fe_DrawVramRect(0x300, (gs->teamRace && s_cupView == 0) * 0x80 + 0x100, 0xf3, 0x7b, 0x26, 0x56, 0, 0);
    int limit = s_slide + 0x2d;
    if (s_cupView == 0)
    {
        unsigned sumP = 0;
        uint32_t sumT = 0;
        for (int rank = 0; rank < n; rank++)
        {
            int k = 0;
            while (k < n && g_CarRecords[k].startPos != rank)
                k++;
            if (k == n)
                k = 0;
            int y = gs->teamRace ? rank / 2 * 0x22 + (rank % 2 == 0 ? 0x6b : 0x77) : rank * 0x11 + 0x6a;
            if (limit <= y + 9)
                break;
            int r, g, b;
            RowColor(k, &r, &g, &b);
            const CarRecord* rec = &g_CarRecords[k];
            Font_Select(4);
            Fe_DrawTextRgb(r, g, b, CarName(&g_RaceGrid.car[k]), 0x3b, y - 3, 0);
            Font_Select(0);
            if (!gs->teamRace)
            {
                Fe_DrawTime(r, g, b, 0xb3, y, rec->d);
                DrawNumber((uint8_t)rec->points, (uint8_t)rec->points < 10 ? 0xf8 : 0xf5, y, r, g, b);
            }
            else if (rank % 2)
            {
                int yy = rank / 2 * 0x22 + 0x70;
                sumT += (uint32_t)rec->d;
                sumP += (uint8_t)rec->points;
                Fe_DrawTime(r, g, b, 0xb3, yy, (int)sumT);
                Font_Select(4);
                DrawNumber((int)sumP, sumP < 10 ? 0xf8 : 0xf4, yy - 3, r, g, b);
            }
            else
            {
                sumT = (uint32_t)rec->d;
                sumP = (uint8_t)rec->points;
            }
        }
    }
    else
        for (int i = 0; i < n && i * 0x11 + 0x73 < limit; i++)
        {
            int r, g, b;
            RowColor(i, &r, &g, &b);
            Font_Select(4);
            Fe_DrawTextRgb(r, g, b, CarName(&g_RaceGrid.car[i]), 0x3b, i * 0x11 + 0x67, 0);
            Font_Select(0);
            Fe_DrawTime(r, g, b, 0xb3, i * 0x11 + 0x6a, g_CarRecords[i].a);
            int pts = Exe_U8(0x800af468u + i);
            DrawNumber(pts, pts < 10 ? 0xf8 : 0xf5, i * 0x11 + 0x6a, r, g, b);
        }

    Fe_ReadPads();
    if (fe->popup == 0)
    {
        if (Fe_PadPressed(PADB_CROSS, port, 1))
            fe->nextScreen = 0;   /* (al terminar el fundido se cambia por lo que siga) */
        else if (Fe_PadPressed(PADB_TRIANGLE, port, 1))
        {
            fe->popup = 1;
            gs->yesNo = 0;
        }
        else if (Fe_PadPressed(PADB_UP, port, 1) || Fe_PadPressed(PADB_DOWN, port, 1))
        {
            s_cupView = s_cupView == 0;
            fe->cursor = fe->prevCursor == 0;   /* (para el sonido de mover el cursor) */
        }
    }
    else
    {
        int res = Fe_MsgBox(0, 0x4f, Fe_GetString(0xa28), &gs->yesNo, port);
        if (res != -1)
        {
            if (res != 0)
            {
                /* abandonar la copa */
                if (!Fe_CheckUnlocks())
                {
                    fe->nextScreen = 100;
                    if (g_FeSaveReturn)
                        fe->marquee = 1;
                }
                else
                    fe->nextScreen = 1800;
            }
            fe->popup = 0;
        }
    }
    Fe_PadRepeatReset();
    if (s_slide < 0x9e)
    {
        s_slide = (short)(s_slide + g_FrameDelta);
        if (s_slide > 0x9e)
            s_slide = 0x9e;
    }
    if (fe->fadeDone && fe->nextScreen != 100 && fe->nextScreen != 1800 && fe->nextScreen != 800)
    {
        if (gs->champType && gs->raceIndex < gs->cupRaces)
            SortByPos(0);   /* la parrilla de la siguiente carrera sale de la clasificacion */
        fe->nextScreen = (short)Fe_AfterRace();
    }
}

/* ---------------------------------------------------------------- pantalla 1700: podio de la copa */

/* inserta al del podio en una tabla de copa de 4 puestos (por puesto en la copa y despues por tiempo).
 * stride: tamaño de cada puesto; tOff / pOff: tiempo y puesto; names / nameOff: nombres que se copian */
static void CupInsert(uint8_t* tab, int stride, int tOff, int pOff, const CupWinner* w, int whole,
                      int nameOff, int name2Off)
{
    int k = 0;
    for (; k < 4; k++)
    {
        const uint8_t* e = tab + k * stride;
        uint32_t t = Rd32(e + tOff);
        if (t == 0 || w->pos < e[pOff] || (w->time < t && w->pos == e[pOff]))
            break;
    }
    if (k >= 4)
        return;
    for (int j = 3; j > k; j--)
    {
        uint8_t* d = tab + j * stride;
        const uint8_t* s = tab + (j - 1) * stride;
        if (whole)
            memcpy(d, s, stride);
        else
        {
            memcpy(d + nameOff, s + nameOff, 9);
            d[pOff] = s[pOff];
            memcpy(d + tOff, s + tOff, 4);
        }
    }
    uint8_t* e = tab + k * stride;
    memcpy(e + nameOff, w->name, 9);
    if (name2Off >= 0)
        memcpy(e + name2Off, w->name2, 9);
    e[pOff] = w->pos;
    Wr32(e + tOff, w->time);
}

/* nombre de una tabla de copa (8008f15c): 0xDE + piloto + 1 = el de un piloto de la IA */
static const char* CupName(const char* s)
{
    return (uint8_t)s[0] == 0xde ? g_Drivers[(uint8_t)s[1] - 1].name : s;
}

/* 80087130: uno por uno, de abajo arriba, los del podio con su coche (dos por equipos), el puesto, el tiempo y la
 * copa girando; al entrar cada uno pasa a las tablas de la copa (DAT_800b7998: +0 tiempo, +8 nombre, +0x11 puesto;
 * en el modo 4, +4 / +0x12 / +0x1B; por equipos DAT_800b8418). */
void Fe_Screen1700(void)
{
    FrontendState* fe = g_Frontend;
    GameSettings* gs = &g_Settings;
    if (fe->firstFrame)
    {
        Fe_SetBackground(9);
        fe->popup = gs->numPlayers;   /* se restaura al salir */
        if (s_numWinners == 0)
            fe->nextScreen = 200;
        else
        {
            int c = gs->racingClass ? gs->racingClass - 1 : 0;
            Fe_ResetCars();
            if (gs->champType == 2)
                gs->curVenue = 6;
            else if (gs->champType == 3)
                gs->curVenue = 7;
            gs->numPlayers = 2;
            const CupWinner* w = &s_winners[--s_numWinners];
            if (!w->team)
                gs->numPlayers = 1;
            fe->driver[0] = w->driver;
            fe->car[0] = w->car;
            Fe_RequestCar(0, w->driver, w->car);
            if (w->team)
            {
                fe->driver[1] = w->driver2;
                fe->car[1] = w->car2;
                Fe_RequestCar(1, w->driver2, w->car2);
            }
            uint8_t* recs = g_SaveExtra.cupRecs + gs->curVenue * 0x150 + c * 0x70;
            if (gs->gameMode == 4)
                CupInsert(recs, 0x1c, 4, 0x1b, w, 0, 0x12, -1);
            else if (!w->team)
                CupInsert(recs, 0x1c, 0, 0x11, w, 0, 8, -1);
            else
                CupInsert(g_SaveExtra.cupRecs2 + gs->curVenue * 0x120 + c * 0x60, 0x18, 0, 0xd, w, 1, 4, 0xe);
            s_slide = 0x92;
            fe->previewZoom = 25000;
        }
    }
    const CupWinner* w = &s_winners[s_numWinners];
    Fe_UpdateCars();
    Fe_DrawBackground(0x800);
    Fe_DrawHelp(w->player, HelpText(9), 0x21, 0xd5, 0x23, 0x109, 0);
    Fe_DrawBox(0x46, 0x46, 0x46, s_slide + 0xb4, 0x31, 0x140, 0x46, 1);
    Fe_DrawFrame(100, 100, 100, s_slide + 0xb4, 0x31, 0x14a, 0x46);
    Font_Select(2);
    Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, fe->venues[gs->venue][0].name, s_slide + 0xb7, 0x2e, 0);
    Fe_DrawBox(0x46, 0x46, 0x46, s_slide + 200, 0x4e, 0x14a, 99, 1);
    Fe_DrawFrame(100, 100, 100, s_slide + 200, 0x4e, 0x14a, 99);
    Font_Select(2);
    Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, Fe_GetString(0xa2d + w->pos), s_slide + 0xcb, 0x4b, 0);   /* DAT_800b7058 */
    Font_Select(4);
    int x1 = w->team ? 0x6e : 0x5a, y = w->team ? 0x89 : 0x8e;
    Fe_DrawBox(0x46, 0x46, 0x46, -0x1e, y, x1 - s_slide, y + 0x10, 1);
    Fe_DrawFrame(100, 100, 100, -0x1e, y, x1 - s_slide, y + 0x10);
    Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, CupName(w->name), x1 - (s_slide + 4), y, 2);
    int y2 = y;
    if (w->team)
    {
        y2 = y + 0x17;
        x1 += 10;
        Fe_DrawBox(0x46, 0x46, 0x46, -0x32, y2, x1 - s_slide, y + 0x27, 1);
        Fe_DrawFrame(100, 100, 100, -0x32, y2, x1 - s_slide, y + 0x27);
        Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, CupName(w->name2), x1 - (s_slide + 4), y2, 2);
    }
    int y3 = y2 + 0x17;
    x1 += 10;
    Fe_DrawBox(0x46, 0x46, 0x46, -0x1e, y3, x1 - s_slide, y2 + 0x27, 1);
    Fe_DrawFrame(100, 100, 100, -0x1e, y3, x1 - s_slide, y2 + 0x27);
    Fe_DrawTime(0x1f, 0x1f, 0x1f, x1 - (s_slide + 0x34), y3, (int)w->time);
    fe->modelAngle = 0x990;
    if (w->team)
    {
        Fe_DrawCarModel(0, 0xa0, 0x96, 0x1e14, 0);
        Fe_DrawCarModel(1, 0xe6, 0xa0, 0x19c8, 0);
    }
    else
        Fe_DrawCarModel(0, 200, 0x90, 0x1a2c, 0);
    if ((g_FadeDir == 0 || g_FadeLevel == 0x100) && fe->nextScreen == -1)
    {
        Gfx_FlushPartial();   /* la copa por encima de los coches */
        Fe_DrawSpinningModel(0x34, 0x54, fe->previewZoom + 11000, w->pos + (gs->gameMode == 4 ? 8 : 5), 1);
    }
    Fe_ReadPads();
    if (Fe_PadPressed(PADB_CROSS, w->player, 1))
        fe->nextScreen = (short)(s_numWinners != 0 ? 1700 : Fe_AfterRace());
    Fe_PadRepeatReset();
    if (g_FadeDir == 0 && fe->previewZoom > 0)
    {
        fe->previewZoom -= g_FrameDelta * 400;
        if (fe->previewZoom < 0)
            fe->previewZoom = 0;
    }
    if (s_slide > 0)
    {
        s_slide = (short)(s_slide - g_FrameDelta);
        if (s_slide < 0)
            s_slide = 0;
    }
    if (fe->fadeDone && fe->nextScreen != 1700)
        gs->numPlayers = (uint8_t)fe->popup;
}

/* ---------------------------------------------------------------- pantalla 1800: desbloqueos */

static short s_page = -1;       /* fe +0xA80: ultima pagina ensenada (0..7) */
static short s_pageArg;         /* fe +0xA82 */
static char  s_pageMsg[0x80];   /* fe +0xA7C */
static int   s_allCups;         /* fe +0xA88: todas las copas de sede ganadas en la clase: la copa EA */
static TrackPreview* s_previews[3];   /* fe +0xA90 */

/* elige la siguiente pagina de lo desbloqueado (s_unlock) y cuenta las que quedan (incluida esa) */
static int NextUnlockPage(void)
{
    FrontendState* fe = g_Frontend;
    GameSettings* gs = &g_Settings;
    int c = gs->racingClass ? gs->racingClass - 1 : 0;
    for (int i = 0; i < 3; i++)
        if (s_previews[i])
        {
            Pool_Free(s_previews[i]);
            s_previews[i] = NULL;
        }
    int count = 0, sel = -1;
    if (s_kidTime != 0 && s_page < 0)
    {
        s_page = 0;   /* tiempo de la copa de Rookie */
        snprintf(s_pageMsg, sizeof(s_pageMsg), "%s", Fe_GetString(0xa95));
        sel = 0;
        count++;
    }
    if (s_unlock[0] && s_page < 1)
    {
        if (sel == -1)
        {
            s_page = 1;   /* clase nueva */
            s_pageArg = s_unlock[0];
            snprintf(s_pageMsg, sizeof(s_pageMsg), "%s", Fe_GetString(0xa8d));
            sel = 1;
        }
        count++;
    }
    if (s_unlock[1] && s_page < 3)
    {
        if (sel == -1)
        {
            /* sede nueva (4 o 5; con 3, las dos) */
            if (s_unlock[1] == 3)
            {
                s_page = 2;
                s_unlock[1] = 2;
                s_pageArg = 4;
                count++;
            }
            else
            {
                s_page = 3;
                s_pageArg = (short)(s_unlock[1] - 1 + 4);
            }
            sel = s_page;
            snprintf(s_pageMsg, sizeof(s_pageMsg), "%s%s%s", Fe_GetString(0xa8f), fe->venues[s_pageArg][0].name,
                     Fe_GetString(0xa90));
        }
        count++;
    }
    int first = -1;
    for (int k = 0; k < 3; k++)
        if (s_unlock[2 + k] == 1)
        {
            if (first == -1)
                first = k;
            count++;
        }
    if (first != -1 && sel == -1)
    {
        s_page = 4;   /* pista suelta */
        s_pageArg = (short)first;
        snprintf(s_pageMsg, sizeof(s_pageMsg), "%s", Fe_GetString(0xa91));
        sel = 4;
        s_unlock[2 + first] = 0;
    }
    int v = 0;
    while (v < 7 && s_unlock[5 + v * 3 + c] == 0)
        v++;
    if (v < 7 && s_page < 5)
    {
        if (sel == -1)
        {
            sel = 5;   /* copa ganada: el piloto de la sede */
            s_page = 5;
            s_pageArg = (short)v;
            if (s_unlock[5 + v * 3 + c] == 1)
            {
                snprintf(s_pageMsg, sizeof(s_pageMsg), "%s", Fe_GetString(0xa92));
                int all = 0;
                while (all < 6 && P(0x800b713a + all * 3 + c) != 0)
                    all++;
                if (all == 6 && v < 6)
                    s_allCups = 1;
            }
            else
                snprintf(s_pageMsg, sizeof(s_pageMsg), "%s", Fe_GetString(0xa93));
        }
        count++;
    }
    if (s_allCups && s_page < 6)
    {
        if (sel == -1)
        {
            s_page = 6;   /* la copa EA de la clase */
            snprintf(s_pageMsg, sizeof(s_pageMsg), "%s%s %s.", Fe_GetString(0xa97), Fe_GetString(0x7b + c),
                     fe->venues[8][0].name);
            sel = 6;
        }
        count++;
    }
    for (int k = 0; k < 10; k++)
        if (s_unlock[0x1a + k] == 1)
        {
            if (sel == -1)
            {
                s_page = 7;   /* especial: piloto leyenda */
                s_pageArg = (short)k;
                snprintf(s_pageMsg, sizeof(s_pageMsg), "%s", Fe_GetString(0xa94));
                sel = 7;
            }
            count++;
        }
    return count;
}

/* las tres pistas de una sede (o una) girando, con su nombre debajo */
static void DrawPreview(TrackPreview* tp, int z, int x, int y, const char* name, int ty)
{
    FrontendState* fe = g_Frontend;
    if (!tp)
        return;
    tp->pos[0] = tp->pos[1] = 0;
    tp->pos[2] = z;
    int eye[3] = { 0, 50000, 0 };
    Camera_LookAt(eye, tp->pos);
    Math_RotMatrix(fe->modelAngle & 0xfff, 0, 0, tp->rot);
    SetGeomOffset(x, y);
    TrackPreview_Draw(tp, 0x7f3f3f, &fe->previewLight);
    Fe_DrawTextBox(0, name, x, ty);
}

/* 80087fa8: una pagina por cada cosa desbloqueada (texto 0xA8C de titulo): tiempo de Rookie, clase nueva (coche),
 * sede nueva (sus tres pistas), pista suelta, piloto de la sede ganada, la copa EA y los pilotos leyenda. Con mas
 * de una, X vuelve a entrar en la 1800 con la siguiente. */
void Fe_Screen1800(void)
{
    FrontendState* fe = g_Frontend;
    GameSettings* gs = &g_Settings;
    int port = gs->numPlayers == 2 ? -1 : 0;
    int c = gs->racingClass ? gs->racingClass - 1 : 0;
    if (!s_unlock)
    {
        fe->nextScreen = (short)Fe_AfterRace();
        return;
    }
    if (fe->firstFrame)
    {
        Fe_SetBackground(4);
        Fe_ResetCars();
        fe->previewLight.vx = 0;
        fe->previewLight.vy = (short)0xf000;
        fe->previewLight.vz = 0x1000;
        {
            double l = sqrt(2.0);
            fe->previewLight.vy = (short)lround(-4096 / l);
            fe->previewLight.vz = (short)lround(4096 / l);
        }
        fe->modelAngle = 0;
        fe->popup = 0;
        if (fe->prevScreen != 1800)
        {
            s_page = -1;
            fe->portPopup = (char)NextUnlockPage();
        }
    }
    Fe_DrawBackground(20000);
    Fe_DrawHelp(0, HelpText(9), 0x21, 0xd5, 0x23, 0x109, 0);
    Fe_UpdateCars();
    Fe_DrawBox(0x20, 0, 0, 0x27, 0x32, 0x114, 0x50, 0);
    Fe_DrawFrame(0x94, 0x94, 0xa0, 0x26, 0x32, 0x114, 0x50);
    Font_Select(2);
    Fe_DrawTextRgb(0x1c, 0x1c, 0x1c, Fe_GetString(0xa8c), 0x9d, 0x14, 1);
    Font_Select(0);
    Fe_DrawTextRgb(0x1d, 0x1e, 0, s_pageMsg, 0x9d, 0x3b, 1);
    char text[96];
    switch (s_page)
    {
    case 0:
    {
        Fe_DrawTextBox(0, Fe_GetString(0xa96), 0xa0, 0x73);
        Font_Select(3);
        int w = Fe_DrawTime(-1, -1, -1, 0, 0, s_kidTime);
        Fe_DrawTime(0x1c, 0x1c, 0x1c, 0x9c - w / 2, 0x84, s_kidTime);
        break;
    }
    case 1:
        if (fe->firstFrame)
        {
            fe->car[0] = (uint8_t)s_pageArg;
            Fe_RequestCar(0, fe->driver[0], s_pageArg);
        }
        else
        {
            Fe_DrawCarModel(0, 0xa8, 0x9b, 8000, 0);
            snprintf(text, sizeof(text), "%s %s", Fe_GetString(0x7c + c), Fe_GetString(0xa8e));
            Fe_DrawTextBox(0, text, 0xa0, 0xb6);
        }
        break;
    case 2:
    case 3:
    {
        static const short offs[3][2] = { { 0, -0x46 }, { -0x41, -0xf }, { 0x41, -0xf } };
        if (fe->firstFrame)
            for (int i = 0; i < 3; i++)
                s_previews[i] = TrackPreview_Create(Asset_Find(ASSET_TkFE, fe->venues[s_pageArg][i + 1].tkfe), 600, 700);
        for (int i = 0; i < 3; i++)
            DrawPreview(s_previews[i], 110000, offs[i][0] + 0xa0, offs[i][1] + 200, fe->venues[s_pageArg][i + 1].name,
                        offs[i][1] + 200);
        SetGeomOffset(0xa0, 0x8c);
        break;
    }
    case 4:
        if (fe->firstFrame)
            s_previews[0] = TrackPreview_Create(Asset_Find(ASSET_TkFE, fe->venues[6][s_pageArg + 1].tkfe), 600, 700);
        Font_Select(0);
        DrawPreview(s_previews[0], 78000, 0xa0, 0x8c, fe->venues[6][s_pageArg + 1].name, 0xb6);
        SetGeomOffset(0xa0, 0x8c);
        break;
    case 5:
        if (fe->firstFrame)
        {
            fe->driver[0] = (uint8_t)(s_pageArg + 0x27);
            fe->car[0] = (uint8_t)c;
            Fe_RequestCar(0, s_pageArg + 0x27, c);
        }
        else
        {
            Fe_DrawCarModel(0, 0xa6, 0x9b, 8000, 0);
            Fe_DrawTextBox(0, g_Drivers[s_pageArg + 0x27].name, 0xa0, 0xb6);
        }
        break;
    case 6:
        if (fe->firstFrame)
            Fe_LoadVenueThumb(fe->venues[8][0].image, 0);
        else
        {
            Fe_DrawVenueThumb(0, 0x83, 0x87, 0, 1);
            int r, g, b;
            Fe_ItemColor(&r, &g, &b, 1);
            Fe_DrawFrame(r * 3 + 0x32, g * 3 + 0x32, b * 3 + 0x3c, 0x81, 0x86, 0xc1, 0x9f);
            Font_Select(2);
            Fe_DrawTextRgb(0x1c, 0x1c, 0x1c, fe->venues[8][0].name, 0x9e, 0x6e, 1);
        }
        break;
    case 7:
        if (fe->firstFrame)
        {
            fe->driver[0] = (uint8_t)(s_pageArg + 0x2e);
            fe->car[0] = (uint8_t)c;
            Fe_RequestCar(0, s_pageArg + 0x2e, c);
        }
        else if (!fe->fadeDone)
        {
            Fe_DrawCarModel(0, 0xa6, 0xa1, 8000, 0);
            Fe_DrawTextBox(0, g_Drivers[s_pageArg + 0x2e].name, 0xa0, 0xb6);
        }
        else
            s_unlock[0x1a + s_pageArg] = 0;
        break;
    default:
        break;
    }
    Fe_ReadPads();
    if (Fe_PadPressed(PADB_CROSS, port, 1))
        fe->nextScreen = (uint8_t)fe->portPopup < 2 ? (short)Fe_AfterRace() : 0;
    Fe_PadRepeatReset();
    if (!fe->fadeDone)
    {
        fe->modelAngle += g_FrameDelta * 2;
        return;
    }
    if ((uint8_t)fe->portPopup >= 2)
    {
        /* quedan mas: la siguiente pagina */
        fe->portPopup = (char)NextUnlockPage();
        fe->nextScreen = 1800;
        return;
    }
    /* LAB_80088658: fin */
    for (int i = 0; i < 3; i++)
        if (s_previews[i])
        {
            Pool_Free(s_previews[i]);
            s_previews[i] = NULL;
        }
    free(s_unlock);
    s_unlock = NULL;
    if (g_FeUnlockShow > 0)
    {
        /* clase nueva: los coches de los jugadores pasan a ella y el menu principal marca "Racing Class" */
        for (int i = 0; i < gs->numPlayers && i < 2; i++)
            gs->lastCar[i] = gs->maxClassUnlocked;
        for (int i = 0; i < 7; i++)
            fe->mainMenu[i].active = 0;
        fe->mainMenu[2].active = 1;
    }
    s_kidTime = 0;
}

/* ---------------------------------------------------------------- pantalla 900: Hall of Champs */

/* tabla de copa de la sede del carrusel (7 y 8 = Winston Cup y EA: tablas 6 y 7; la 6 del carrusel no existe) */
static int HallTable(void)
{
    int v = g_SaveExtra.b7995;
    return v > 6 ? v - 1 : v;
}

/* 8008c5c0: ganador de una clase en la 900: clase (y "  " + 0xA2A en la tabla del modo 4), nombre (por equipos
 * los dos) y, si hay, la copa girando y el tiempo; el elegido con el color del cursor */
static void DrawHallEntry(int x, int y, int cls, int pos, int time, const char* name, const char* name2, int sel)
{
    int r = 0, g = 0, b = 0;
    if (sel)
        Fe_ItemColor(&r, &g, &b, 2);
    Font_Select(0);
    char s[48];
    snprintf(s, sizeof(s), "%s%s%s", Fe_GetString(0x7e + cls), pos > 2 ? "  " : "", pos > 2 ? Fe_GetString(0xa2a) : "");
    if (sel)
        Font_DrawString(s, x + 1, y + 1, 0);
    Font_DrawString(s, x, y, sel ? 9 : 8);
    Font_Select(1);
    int nx = x, ny = time == 0 ? y + 8 : y + 0xf;
    if (name2 && time != 0)
    {
        Font_DrawString(name, x + 0x3c, y - 2, 8);
        name = CupName(name2);
        nx = x + 0x3c;
        ny = y + 0xb;
    }
    Font_DrawString(name, nx, ny, 8);
    if (time == 0)
    {
        Fe_DrawBox(0x3c, 0x3c, 0x3c, x - 0x28, y + 1, x - 4, y + 0x20, 0);
        Fe_DrawFrame(r * 2 + 100, g * 2 + 100, b * 2 + 100, x - 0x29, y + 1, x - 4, y + 0x20);
    }
    else
    {
        Fe_DrawBox(0x10, 0x10, 0x3f, x - 0x28, y + 1, x - 4, y + 0x20, 0);
        Fe_DrawFrame(r * 2 + 0x3c, g * 2 + 0x3c, b * 2 + 0x3c, x - 0x29, y + 1, x - 4, y + 0x20);
        Fe_DrawSpinningModel(x - 0x16, y + 0x11, 26000, pos + 0xb, 2);
        Font_Select(0);
        Fe_DrawTime(0x1d, 0x1d, 3, x, y + 10, time);
    }
}

static void HallThumb(int i)
{
    FrontendState* fe = g_Frontend;
    Fe_LoadVenueThumb(fe->venues[fe->vcar[i].venue][fe->vcar[i].k].image, fe->vcar[i].slot);
}

static void HallDrawCarousel(int n, int scrolled)
{
    FrontendState* fe = g_Frontend;
    for (int i = 0; i < n; i++)
        Fe_DrawVenueThumb(fe->vcar[i].slot, i * 0x44 + (scrolled ? fe->vScrollX : 0) + 0x3a, 0x25, 0, 1);
}

/* el carrusel avanza (entra por la derecha la sede + 2) */
static void HallShiftLeft(void)
{
    VenueCarousel* c = g_Frontend->vcar;
    int8_t slot = c[0].slot;
    c[0] = c[1];
    c[1] = c[2];
    c[2] = c[3];
    c[3].slot = slot;
}

/* el carrusel retrocede (entra por la izquierda la sede - 2) */
static void HallShiftRight(void)
{
    VenueCarousel* c = g_Frontend->vcar;
    int v = g_SaveExtra.b7995;
    int8_t slot = c[3].slot;
    c[3] = c[2];
    c[2] = c[1];
    c[1] = c[0];
    c[0].slot = slot;
    c[0].k = 0;
    c[0].venue = (uint8_t)(v < 2 ? v + 7 : Fe_WrapVenue(v - 2));
    HallThumb(0);
}

/* 8007ddb4: carrusel de sedes (izquierda / derecha, DAT_800b7995) y, en tres filas, el ganador de la copa de cada
 * clase (arriba / abajo elige, DAT_800b7996); individual o por equipos segun el ajuste del menu principal. X ensena
 * los cuatro primeros (1600), triangulo vuelve al menu. */
void Fe_Screen900(void)
{
    FrontendState* fe = g_Frontend;
    GameSettings* gs = &g_Settings;
    SaveExtra* x = &g_SaveExtra;
    VenueCarousel* c = fe->vcar;
    if (fe->firstFrame)
    {
        Fe_SetBackground(7);
        gs->champType = 1;   /* (Fe_WrapVenue: las 9 sedes sin la 6) */
        gs->gameMode = 0;
        c[0].venue = (uint8_t)Fe_WrapVenue(x->b7995 - 1);
        c[1].venue = x->b7995;
        c[2].venue = (uint8_t)Fe_WrapVenue(x->b7995 + 1);
        c[3].venue = 0;
        for (int i = 0; i < 4; i++)
        {
            c[i].k = 0;
            c[i].slot = (int8_t)i;
            HallThumb(i);
        }
        fe->vScrollDir = 0;
        fe->vScrollAgain = 0;
        fe->vScrollX = 0;
        fe->firstFrame = 0;
    }
    Fe_ReadPads();
    Fe_DrawBackground(0x1000);
    Fe_DrawHelp(0, HelpText(11), 0x21, 0xd5, 0x23, 300, 0);

    int dir = fe->vScrollDir;
    if (dir < 0)
    {
        int shift = fe->vScrollX < -0x43;
        if (!shift)
        {
            fe->vScrollX = (short)(fe->vScrollX + dir);
            if (fe->vScrollX < -0x43)
            {
                if (Fe_PadPressed(PADB_RIGHT, 0, 0))
                {
                    fe->vScrollAgain = 1;
                    shift = 1;
                }
                else
                    fe->vScrollX = -0x44;
            }
        }
        if (!shift)
            HallDrawCarousel(4, 1);
        else
        {
            HallShiftLeft();
            x->b7995 = c[1].venue;
            if (!fe->vScrollAgain)
            {
                HallDrawCarousel(3, 0);
                fe->vScrollDir = 0;
            }
            else
            {
                c[3].k = 0;
                fe->vScrollX = (short)(fe->vScrollX + 0x44);
                c[3].venue = (uint8_t)(x->b7995 < 7 ? Fe_WrapVenue(x->b7995 + 2) : x->b7995 - 7);
                HallThumb(3);
                HallDrawCarousel(4, 1);
            }
        }
    }
    else if (dir == 0)
        HallDrawCarousel(3, 0);
    else
    {
        int shift = fe->vScrollX >= 0;
        if (!shift)
        {
            fe->vScrollX = (short)(fe->vScrollX + dir);
            if (fe->vScrollX >= 0)
            {
                if (Fe_PadPressed(PADB_LEFT, 0, 0))
                {
                    fe->vScrollAgain = 1;
                    shift = 1;
                }
                else
                    fe->vScrollX = 0;
            }
        }
        if (!shift)
            HallDrawCarousel(4, 1);
        else
        {
            x->b7995 = c[1].venue;
            if (!fe->vScrollAgain)
            {
                HallDrawCarousel(3, 0);
                fe->vScrollDir = 0;
            }
            else
            {
                fe->vScrollX = (short)(fe->vScrollX - 0x44);
                HallShiftRight();
                HallDrawCarousel(4, 1);
            }
        }
    }

    int team = gs->hallType == 1;
    int xo = team ? 0x16 : 0, xr = team ? 0x1f : 0;
    for (int i = 0; i < 2; i++)
        Fe_DrawLine(0x28787878, xo + 0x1b, 0x86 + i * 0x28, 0x131 - xr, 0x86 + i * 0x28, 1);
    int yy = (2 - x->b7996) * 0x28;
    Fe_DrawCallout(0x7e, 0x24, 0x3f, 0x19, 10, 0x92 - xo, yy + 0x2b, 0xc);
    Fe_DrawBox(0x44, 0x44, 0x44, xo + 0x1d, yy + 0x62, 0x131 - xr, yy + 0x82, 1);
    Font_Select(2);
    Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, fe->venues[x->b7995][0].name, 0x9e, 0x45, 1);
    Font_Select(4);
    Fe_DrawTextRgb(0x1c, 0x1c, 10, Fe_GetString(0x90 + gs->hallType), 0xf2, 0x12, 1);   /* DAT_800b7000 */
    int t = HallTable();
    for (int cls = 0; cls < 3; cls++)
    {
        int y = (2 - cls) * 0x28 + 0x61;
        int sel = x->b7996 == cls;
        if (!team)
        {
            const uint8_t* e = x->cupRecs + t * 0x150 + cls * 0x70;
            DrawHallEntry(0x45, y, cls, e[0x11], (int)Rd32(e), (const char*)e + 8, NULL, sel);
            if (t < 7)
                DrawHallEntry(0xc9, y, cls, e[0x1b] + 3, (int)Rd32(e + 4), (const char*)e + 0x12, NULL, sel);
        }
        else
        {
            const uint8_t* u = x->cupRecs2 + t * 0x120 + cls * 0x60;
            DrawHallEntry(xo + 0x45, y, cls, u[0xd], (int)Rd32(u), (const char*)u + 4, (const char*)u + 0xe, sel);
        }
    }

    if (Fe_PadPressed(PADB_RIGHT, -1, 0))
    {
        if (fe->vScrollDir == 0)
        {
            fe->vScrollDir = -7;
            fe->vScrollX = 0;
            c[3].k = 0;
            c[3].venue = (uint8_t)(x->b7995 < 7 ? Fe_WrapVenue(x->b7995 + 2) : x->b7995 - 7);
            HallThumb(3);
            fe->valueDir = 1;
        }
        fe->vScrollAgain = 0;
    }
    else if (Fe_PadPressed(PADB_LEFT, -1, 0))
    {
        if (fe->vScrollDir == 0)
        {
            fe->vScrollDir = 7;
            fe->vScrollX = -0x44;
            HallShiftRight();
            fe->valueDir = 1;
        }
        fe->vScrollAgain = 0;
    }
    else if (Fe_PadPressed(PADB_CROSS, -1, 1))
        fe->nextScreen = 1600;
    else if (Fe_PadPressed(PADB_TRIANGLE, -1, 1))
        fe->nextScreen = 100;
    else
    {
        int d = Fe_PadPressed(PADB_UP, -1, 1) ? 1 : Fe_PadPressed(PADB_DOWN, -1, 1) ? -1 : 0;
        if (d)
        {
            x->b7996 = (uint8_t)Fe_Clamp(x->b7996 + d, 0, 2);
            fe->cursor = fe->prevCursor == 0;   /* (para el sonido de mover el cursor) */
        }
    }
    Fe_PadRepeatReset();
    if (fe->vScrollDir > 0)
        fe->vScrollDir = (char)g_FrameDelta;
    else if (fe->vScrollDir < 0)
        fe->vScrollDir = (char)-g_FrameDelta;
    fe->modelAngle += g_FrameDelta;
}

/* ---------------------------------------------------------------- pantalla 1600: records de una copa */

static int8_t s_hallTable;   /* DAT_800b8d18: 0 copa, 1 tabla del modo 4 (arriba / abajo) */

/* 8007ea64: los cuatro primeros de la copa de la sede y clase elegidas en la 900 (puesto, nombre y tiempo) y la
 * copa del primero girando; por equipos, los dos nombres. */
void Fe_Screen1600(void)
{
    FrontendState* fe = g_Frontend;
    GameSettings* gs = &g_Settings;
    SaveExtra* x = &g_SaveExtra;
    int ea = x->b7995 == 8;
    int cls = x->b7996;
    if (fe->firstFrame)
    {
        Fe_SetBackground(ea ? 0xb : gs->hallType + 10);
        Fe_DecodeGmdc(0, 0x300, 0x100);
        Fe_DecodeGmdc(0xc, 0x300, 0x120);
        Fe_DecodeGmdc(6, 0x300, 0x140);
        s_hallTable = 0;
    }
    Fe_DrawBackground(0x1000);
    Fe_DrawHelp(0, HelpText(gs->hallType == 0 && !ea ? 22 + s_hallTable : 3), 0x21, 0xd5, 0x23, 300, 0);
    int t = HallTable();
    char title[64];
    snprintf(title, sizeof(title), "%s%s", fe->venues[x->b7995][0].name, Fe_GetString(0xa29));
    Font_Select(2);
    Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, title, 0x22, 0x17, 0);
    Fe_DrawBox(0x48, 0x48, 0x48, 0xf8, 0x16, 0x130, 0x29, 1);
    Fe_DrawFrame(0x8c, 0x8c, 0x96, 0xf8, 0x16, 0x130, 0x29);
    Font_Select(1);
    Fe_DrawTextRgb(0x1b, 0x1b, 2, Fe_GetString(0x7b + cls), 0x113, 0x14, 1);   /* DAT_800b6fcc */
    int best = -1, mx = 0xff, my = 99;
    if (gs->hallType == 0)
    {
        if (!ea)
        {
            int r, g, b;
            Fe_ItemColor(&r, &g, &b, 1);
            if (s_hallTable)
                Fe_DrawVramRect(0x300, 0x100, 0x40, 0x20, 0x22, 0x3f, 0, 0);
            Fe_DrawVramRect(0x300, 0x120, 0xf, 0x1a, 0x68, 0x41, 0, 0);
            Fe_DrawFrame(r * -2 + 0x96, g * -2 + 0x96, b * -2 + 0xa0, 0x67, 0x41, 0x77, 0x5a);
        }
        else
            Fe_DrawVramRect(0x300, 0x140, 0x28, 0xe, 0x2f, 0x44, 0, 0);
        const uint8_t* tab = x->cupRecs + t * 0x150 + cls * 0x70;
        for (int i = 0; i < 4; i++)
        {
            int y = ea ? i * 0x1e + 0x55 : i * 0x15 + 0x78;
            const uint8_t* e = tab + i * 0x1c;
            int pos = -1, time = 0;
            const char* name = "";
            if (s_hallTable == 0 && Rd32(e) != 0)
            {
                name = (const char*)e + 8;
                pos = e[0x11];
                time = (int)Rd32(e);
            }
            else if (s_hallTable == 1 && Rd32(e + 4) != 0)
            {
                name = (const char*)e + 0x12;
                pos = e[0x1b];
                time = (int)Rd32(e + 4);
            }
            if (pos < 0)
            {
                Font_Select(0);
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, "----", 0x47, y + 4, 1);
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, "----", 0x87, y + 4, 1);
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, "----", 0xb6, y + 4, 1);
            }
            else
            {
                Font_Select(4);
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, name, 0x35, y + 1, 0);
                Font_Select(0);
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, Fe_GetString(0xa2d + pos), 0x87, y + 4, 1);   /* DAT_800b7058 */
                Fe_DrawTime(0x1f, 0x1f, 0x1f, 0xa5, y + 4, time);
            }
        }
        if (s_hallTable == 0 || ea)
        {
            if (Rd32(tab) != 0)
                best = tab[0x11];
        }
        else if (Rd32(tab + 4) != 0)
            best = tab[0x1b] + 3;
        if (!ea)
        {
            mx = 0xfe;
            my = 0x89;
        }
    }
    else
    {
        Font_Select(0);
        const uint8_t* tab = x->cupRecs2 + t * 0x120 + cls * 0x60;
        for (int i = 0; i < 4; i++)
        {
            const uint8_t* u = tab + i * 0x18;
            if (Rd32(u) == 0)
            {
                Font_Select(0);
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, "----", 0x42, 0x5d + i * 0x1e, 1);
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, "----", 0x88, 0x5d + i * 0x1e, 1);
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, "----", 0xb8, 0x5d + i * 0x1e, 1);
            }
            else
            {
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, (const char*)u + 4, 0x30, 0x53 + i * 0x1e, 0);
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, CupName((const char*)u + 0xe), 0x30, 0x5f + i * 0x1e, 0);
                Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, Fe_GetString(0xa2d + u[0xd]), 0x88, 0x59 + i * 0x1e, 1);
                Fe_DrawTime(0x1f, 0x1f, 0x1f, 0xa7, 0x59 + i * 0x1e, (int)Rd32(u));
            }
        }
        if (Rd32(tab) != 0)
            best = tab[0xd];
    }
    if (best >= 0)
        Fe_DrawSpinningModel(mx, my, 0x34bc, best + 5, 2);
    Fe_ReadPads();
    if (Fe_PadPressed(PADB_TRIANGLE, -1, 1))
        fe->nextScreen = 900;
    else if (gs->hallType == 0 && !ea && (Fe_PadPressed(PADB_UP, 0, 1) || Fe_PadPressed(PADB_DOWN, 0, 1)))
    {
        s_hallTable = s_hallTable == 0;
        fe->cursor = fe->prevCursor == 0;
    }
    Fe_PadRepeatReset();
    fe->modelAngle += g_FrameDelta;
}
