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
 * (1800) y se vuelve al menu principal (o a la 600 la primera vez que se gana todo en Elite). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nr.h"
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
static int8_t    s_unlockShow;       /* fe +0xA84: clase desbloqueada; -1 = todo ganado en Elite (pantalla 600) */
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
    s_unlockShow = 0;
    int c = gs->racingClass - 1;
    if (s_kidTime == 0 || gs->maxClassUnlocked != 0)
        s_kidTime = 0;
    else
    {
        /* copa de Rookie con buen tiempo sin Pro: Pro, el segundo nivel de sedes y las pistas sueltas */
        gs->maxClassUnlocked = 1;
        s_unlock[0] = 1;
        s_unlockShow = 1;
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
            s_unlockShow = (int8_t)gs->maxClassUnlocked;
        }
        else if (!g_SaveExtra.b711c)
        {
            s_unlockShow = -1;
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
    /* Winston Cup (sede 7) ganada: el especial DAT_800b714f */
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
        s_unlockShow = 0;   /* (el original los pone a 0 con el estado del front-end) */
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
    return s_unlockShow == -1 ? 600 : 100;
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
