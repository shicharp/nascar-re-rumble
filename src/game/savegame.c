/* Partida guardada (ver savegame.h).
 * Bloque del fichero: +0 cabecera de la PS1 ('SC', 0x13 = icono de 3 fotogramas, 1 bloque, +4 titulo SJIS, +0x60
 * paleta y +0x80 fotogramas del icono, del asset McIc del front-end) y desde +0x200 los datos del juego:
 *   +0x200 version 0x10, +0x202 / +0x20D los dos nombres (11 bytes: 9 del nombre, puesto, ultimo), +0x218 DAT_800b711c,
 *   +0x219 progreso DAT_800b7135..7158 (0x24: clase maxima, desbloqueos, copas), +0x23D clase, poderes, salon,
 *   +0x240 marcador de cada jugador (715f / 7160), +0x242 DAT_800b7161, +0x243 nombre / tarjeta / borrar,
 *   +0x246 carrera (7162) y campeonato (7164) por jugadores, +0x24A / +0x24E opciones de carrera 1P / 2P,
 *   +0x253 piloto, coche, compañero y su coche de cada jugador, +0x25B / +0x260 los 5 rivales y sus coches,
 *   +0x265 sede y pista, +0x268 campeonatos (7 x 0x120), +0xA48 volumenes (4) y sonido, +0xA4D mando, +0xA4F
 *   cambio, +0xA51 DAT_800b7981 de cada jugador, +0xA65 / +0xA66 DAT_800b7995 / 7996, +0xA68 records
 *   (8 x 0x150), +0x14E8 copas (8 x 0x120), +0x1DE8 suma de i * byte y +0x1DEA suma de bytes de +0x200..+0x1DE7. */
#include <string.h>
#include "nr.h"
#include "exe.h"
#include "globals.h"
#include "frontend.h"
#include "model.h"
#include "snd.h"
#include "memcard.h"
#include "savegame.h"

#define SAVE_NAME  SAVE_FILE_NAME
#define SAVE_BYTES 0x1dec

SaveExtra g_SaveExtra;

static uint8_t  s_buf[0x1e6c];      /* DAT_800b8d2c */
static uint16_t s_title[14];        /* DAT_800b8d30: "NASCAR RUMBLE" en SJIS */
SaveState g_SaveState;

/* 8001fe7c: caracter ASCII a SJIS de ancho completo (tablas del ejecutable en 800aa59c / 800aa5a8) */
static uint16_t Save_Sjis(uint8_t c)
{
    int k = 0, set = 0;
    if ((uint8_t)(c - 0x20) < 0x10)
        k = 1;
    else if ((uint8_t)(c - 0x30) > 9)
    {
        if ((uint8_t)(c - 0x3a) < 7)
            k = 0xb;
        else if ((uint8_t)(c - 0x41) < 0x1a)
            set = 1;
        else if ((uint8_t)(c - 0x5b) < 6)
            k = 0x25;
        else if ((uint8_t)(c - 0x61) < 0x1a)
            set = 2;
        else if ((uint8_t)(c - 0x7b) < 4)
            k = 0x3f;
        else
            return 0;
    }
    unsigned v;
    if (k == 0)
        v = Exe_U16(0x800aa59cu + set * 4) + c - Exe_U16(0x800aa59eu + set * 4);
    else
        v = Exe_U16(0x800aa5a8u + (c - (k + 0x1f)) * 2);
    return (uint16_t)((v & 0xff) << 8 | (v >> 8 & 0xff));
}

/* 8009347c + 80093748 */
void Save_Init(void)
{
    g_SaveState.last = g_SaveState.prev = -43;
    g_SaveState.mode = 0;
    g_SaveState.exists = 0;
    g_SaveState.res = g_SaveState.shown = g_SaveState.shownPrev = 0;
    memset(s_title, 0, sizeof(s_title));
    const char* t = "NASCAR RUMBLE";
    for (int i = 0; t[i] && i < 13; i++)
        s_title[i] = Save_Sjis((uint8_t)t[i]);
}

/* 80093540 */
void Save_Start(void)
{
    Card_Init(0);
    g_SaveState.on = 1;
}

/* 80093568 */
void Save_Stop(void)
{
    Card_Stop();
    g_SaveState.on = 0;
}

static unsigned Save_Sums(uint16_t* sum)
{
    unsigned w = 0;
    uint16_t s = 0;
    for (int i = 0; i < 0x1be8; i++)
    {
        uint8_t b = s_buf[0x200 + i];
        w += (unsigned)i * b;
        s = (uint16_t)(s + b);
    }
    *sum = s;
    return w & 0xffff;
}

/* 800939fc */
static void Save_Pack(void)
{
    uint8_t* d = s_buf;
    GameSettings* g = &g_Settings;
    SaveExtra* x = &g_SaveExtra;
    d[0x200] = 0x10;
    d[0x201] = 0;
    for (int p = 0; p < 2; p++)
    {
        uint8_t* r = d + 0x202 + p * 0xb;
        memcpy(r, g->playerName[p], 9);
        r[9] = g->playerNameSet[p];
        r[10] = x->nameTail[p];
    }
    d[0x218] = x->b711c;
    PROGRESS(0x800b7135) = g->maxClassUnlocked;
    memcpy(d + 0x219, &PROGRESS(0x800b7135), 0x24);
    d[0x23d] = g->racingClass;
    d[0x23e] = g->powerUps;
    d[0x23f] = g->hallType;
    d[0x240] = x->hudPref[0];
    d[0x241] = x->hudPref[1];
    d[0x242] = g->singleRace[0];
    d[0x243] = g->setNamePlayers;
    d[0x244] = g->loadSaveMode;
    d[0x245] = g->clearRecords;
    for (int i = 0; i < 2; i++)
    {
        d[0x246 + i] = g->singleRace[1 + i];
        d[0x248 + i] = g->championship[1 + i];
    }
    memcpy(d + 0x24a, g->raceOpts1P, 4);
    memcpy(d + 0x24e, g->raceOpts2P, 5);
    for (int i = 0; i < 2; i++)
    {
        d[0x253 + i] = g->lastDriver[i];
        d[0x255 + i] = g->lastCar[i];
        d[0x257 + i] = g->mateDriver[i];
        d[0x259 + i] = g->mateCar[i];
    }
    for (int i = 0; i < 5; i++)
    {
        d[0x25b + i] = g->oppDriver[1 + i];
        d[0x260 + i] = g->oppCar[1 + i];
    }
    d[0x265] = g->venue;
    d[0x266] = g->track;
    memcpy(d + 0x268, x->trackRecs, sizeof(x->trackRecs));
    memcpy(d + 0xa48, g->volume, 4);
    d[0xa4c] = g->audioMode;
    for (int i = 0; i < 2; i++)
    {
        d[0xa4d + i] = x->pad797d[i];
        d[0xa4f + i] = g->transmission[i];
        d[0xa51 + i] = x->b7981[i];
    }
    d[0xa65] = x->b7995;
    d[0xa66] = x->b7996;
    memcpy(d + 0xa68, x->cupRecs, sizeof(x->cupRecs));
    memcpy(d + 0x14e8, x->cupRecs2, sizeof(x->cupRecs2));
    d[0x1de8] = d[0x1de9] = d[0x1dea] = d[0x1deb] = 0;
    uint16_t sum;
    unsigned w = Save_Sums(&sum);
    d[0x1de8] = (uint8_t)w;
    d[0x1de9] = (uint8_t)(w >> 8);
    d[0x1dea] = (uint8_t)sum;
    d[0x1deb] = (uint8_t)(sum >> 8);
}

/* 80093f6c: solo si la version y las dos sumas cuadran (si no, error de lectura) */
static void Save_Unpack(void)
{
    uint8_t* d = s_buf;
    uint16_t sum;
    unsigned w = Save_Sums(&sum);
    if ((d[0x200] | d[0x201] << 8) != 0x10 || (d[0x1de8] | d[0x1de9] << 8) != w ||
        (uint16_t)(d[0x1dea] | d[0x1deb] << 8) != sum)
    {
        g_SaveState.res = -4;
        NR_Log("[tarjeta] partida con version o sumas incorrectas\n");
        return;
    }
    GameSettings* g = &g_Settings;
    SaveExtra* x = &g_SaveExtra;
    for (int p = 0; p < 2; p++)
    {
        const uint8_t* r = d + 0x202 + p * 0xb;
        memset(g->playerName[p], 0, sizeof(g->playerName[p]));
        memcpy(g->playerName[p], r, 9);
        g->playerNameSet[p] = r[9];
        x->nameTail[p] = r[10];
    }
    x->b711c = d[0x218];
    memcpy(&PROGRESS(0x800b7135), d + 0x219, 0x24);
    g->maxClassUnlocked = PROGRESS(0x800b7135);
    if (g_GameInfo)
        g_GameInfo->unlockMask = 0;
    g->racingClass = d[0x23d];
    g->powerUps = d[0x23e];
    g->hallType = d[0x23f];
    g->setNamePlayers = d[0x243];
    g->loadSaveMode = d[0x244];
    g->clearRecords = d[0x245];
    x->hudPref[0] = d[0x240];
    x->hudPref[1] = d[0x241];
    g->singleRace[0] = d[0x242];
    for (int i = 0; i < 2; i++)
    {
        g->singleRace[1 + i] = d[0x246 + i];
        g->championship[1 + i] = d[0x248 + i];
    }
    memcpy(g->raceOpts1P, d + 0x24a, 4);
    memcpy(g->raceOpts2P, d + 0x24e, 5);
    for (int i = 0; i < 2; i++)
    {
        g->lastDriver[i] = d[0x253 + i];
        g->lastCar[i] = d[0x255 + i];
        g->mateDriver[i] = d[0x257 + i];
        g->mateCar[i] = d[0x259 + i];
    }
    for (int i = 0; i < 5; i++)
    {
        g->oppDriver[1 + i] = d[0x25b + i];
        g->oppCar[1 + i] = d[0x260 + i];
    }
    g->venue = d[0x265];
    g->track = d[0x266];
    memcpy(x->trackRecs, d + 0x268, sizeof(x->trackRecs));
    memcpy(g->volume, d + 0xa48, 4);
    g->audioMode = d[0xa4c];
    Snd_ApplySettings(g->volume, g->audioMode);
    for (int i = 0; i < 2; i++)
    {
        x->pad797d[i] = d[0xa4d + i];
        g->transmission[i] = d[0xa4f + i];
        x->b7981[i] = d[0xa51 + i];
    }
    x->b7995 = d[0xa65];
    x->b7996 = d[0xa66];
    memcpy(x->cupRecs, d + 0xa68, sizeof(x->cupRecs));
    memcpy(x->cupRecs2, d + 0x14e8, sizeof(x->cupRecs2));
    NR_Log("[tarjeta] partida cargada (clase maxima %d)\n", g->maxClassUnlocked);
}

/* 80093938: lee el fichero (4 intentos); g_SaveState.res 1 si la lectura ha empezado, -4 si no */
void Save_Read(void)
{
    memset(s_buf, 0, 0x200);
    s_buf[0] = 'S';
    s_buf[1] = 'C';
    s_buf[2] = 0x13;
    s_buf[3] = 1;
    memcpy(s_buf + 4, s_title, sizeof(s_title));
    int tries = 4;
    while (tries && !Card_ReadFile(SAVE_NAME, s_buf, SAVE_BYTES))
        tries--;
    g_SaveState.res = tries ? 1 : -4;
}

/* 800937f8: cabecera con el icono del front-end, datos y escritura (4 intentos); g_SaveState.res 2 o -3 */
void Save_Write(void)
{
    memset(s_buf, 0, 0x200);
    s_buf[0] = 'S';
    s_buf[1] = 'C';
    s_buf[2] = 0x13;
    s_buf[3] = 1;
    memcpy(s_buf + 4, s_title, sizeof(s_title));
    NrAsset* icon = Asset_Find(NR_TAG('M', 'c', 'I', 'c'), 0);   /* front-end +0x1DC */
    if (icon && icon->size >= 0x1a0)
    {
        memcpy(s_buf + 0x60, icon->data, 0x20);
        memcpy(s_buf + 0x80, icon->data + 0x20, 3 * 0x80);
    }
    Save_Pack();
    int tries = 4;
    while (tries && !Card_WriteFile(SAVE_NAME, s_buf, SAVE_BYTES))
        tries--;
    g_SaveState.res = tries ? 2 : -3;
}

/* 8009358c: sigue los cambios de estado de la tarjeta. Al quedar lista la primera vez pide leer (g_SaveState.res -1); al
 * terminar una lectura desempaqueta (-6) y al terminar una escritura marca -5. */
int Save_Poll(void)
{
    int st = Card_Status();
    if (st != g_SaveState.last)
    {
        g_SaveState.prev = g_SaveState.last;
        int next = st;
        switch (st)
        {
        case 0:
            if (g_SaveState.mode == -2)
            {
                g_SaveState.mode = 1;
                g_SaveState.last = st;
                Save_Write();
                goto done;
            }
            if (g_SaveState.mode == 0)
            {
                g_SaveState.mode = 1;
                g_SaveState.res = -1;
            }
            else if (g_SaveState.last == -2)
            {
                g_SaveState.mode = 0;
                g_SaveState.last = st;
                goto done;
            }
            if (g_SaveState.last == -5)
            {
                if (g_SaveState.res == 2)
                    g_SaveState.exists = 1;
                g_SaveState.res = -5;
            }
            else
            {
                int wasReading = g_SaveState.last == -4;
                g_SaveState.last = st;
                if (!wasReading)
                    goto done;
                g_SaveState.res = -6;
                Save_Unpack();
                next = g_SaveState.last;
            }
            break;
        case -6:
        case -5:
        case -4:
            break;
        default:
            if (g_SaveState.mode == -2)
                g_SaveState.res = -10;
            else if (g_SaveState.res == 2)
                g_SaveState.res = -3;
            else if (g_SaveState.res == 1)
                g_SaveState.res = -4;
            g_SaveState.mode = 0;
            break;
        case -2:
            g_SaveState.res = 0;
            g_SaveState.mode = 0;
            break;
        case -1:
            g_SaveState.mode = -1;
            g_Settings.yesNo = 0;   /* DAT_800b8d1a */
            break;
        }
        g_SaveState.last = next;
    }
done:
    if (g_SaveState.res != g_SaveState.shown)
    {
        g_SaveState.shownPrev = g_SaveState.shown;
        g_SaveState.shown = g_SaveState.res;
    }
    return st;
}

/* 800931d8: 0 o -1 borra los campeonatos; 1 o -1 los records y las copas (el primero de cada tabla con el nombre
 * por defecto 0xA2C, los demas con el caracter DAT_800af364) */
void Save_ClearRecords(int which)
{
    SaveExtra* x = &g_SaveExtra;
    if ((unsigned)(which + 1) < 2)
        for (int v = 0; v < 7; v++)
            for (int c = 0; c < 3; c++)
                for (int e = 0; e < 6; e++)
                {
                    uint8_t* r = x->trackRecs + v * 0x120 + c * 0x60 + e * 0x10;
                    r[0] = 0;
                    r[0xa] = r[0xb] = 0;
                    memset(r + 0xc, 0, 4);
                }
    if (which == 1 || which == -1)
    {
        const char* def = Fe_GetString(0xa2c);
        uint8_t blank = Exe_U8(0x800af364u);
        for (int c = 0; c < 3; c++)
            for (int v = 0; v < 8; v++)
                for (int e = 0; e < 4; e++)
                {
                    uint8_t* r = x->cupRecs + v * 0x150 + c * 0x70 + e * 0x1c;
                    uint8_t* u = x->cupRecs2 + v * 0x120 + c * 0x60 + e * 0x18;
                    memset(r, 0, 8);
                    r[0x11] = 0;
                    r[0x1b] = 0;
                    memset(u, 0, 4);
                    u[0xd] = 0;
                    if (e == 0)
                    {
                        strncpy((char*)r + 8, def, 9);
                        strncpy((char*)r + 0x12, def, 9);
                        strncpy((char*)u + 4, def, 9);
                        strncpy((char*)u + 0xe, def, 9);
                    }
                    else
                    {
                        r[8] = blank;
                        r[0x12] = blank;
                        u[4] = blank;
                        u[0xe] = blank;
                    }
                }
    }
}

/* Frontend_InitScreens 80079814 (primer arranque, sin partida): los campos que no estan en g_Settings */
void Save_ResetExtra(void)
{
    SaveExtra* x = &g_SaveExtra;
    x->b711c = 0;
    for (int p = 0; p < 2; p++)
    {
        x->hudPref[p] = 1;      /* DAT_800b715f / 7160 */
        x->pad797d[p] = 0xff;
        x->b7981[p] = 1;
        x->nameTail[p] = 0;
    }
    x->b7995 = x->b7996 = 0;
    Save_ClearRecords(-1);
}

/* Legal_Show 80090d98 (con 0): mientras dura la pantalla legal mira la tarjeta y, si hay partida, la carga
 * (g_BootState = 2: el front-end no pone los ajustes por defecto). Al acabar, Save_BootEnd. */
static int s_boot;

void Save_BootLoad(void)
{
    if (!s_boot)
    {
        g_SaveState.shown = g_SaveState.shownPrev = 0;
        g_SaveState.res = 0;
        Save_Init();
        Save_Start();
        s_boot = 1;
    }
    Save_Poll();
    if (g_SaveState.mode == 1)
    {
        if (g_SaveState.res == -6)
        {
            g_BootState = 2;
            g_SaveState.mode = 0;
        }
        else if (g_SaveState.res == -1)
            Save_Read();
    }
}

/* fin de Legal_Show (80093568 + 800934f8): el port deja saltar la pantalla, asi que acaba antes la lectura */
void Save_BootEnd(void)
{
    for (int i = 0; i < 8 && s_boot; i++)
        Save_BootLoad();
    Save_Stop();
    s_boot = 0;
}
