#include "libetc.h"
#include "libpad.h"
#include "PsyX/PsyX_public.h"
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "pad.h"

uint32_t g_PadButtons[2];
uint32_t g_PadBinds[2][14];                 /* DAT_800b0570 */
static uint8_t s_vibration[2] = { 1, 1 };   /* DAT_800b04ef + puerto * 0x28 */

#define MAX_SCRIPT 256
static struct { int frame, len; uint32_t mask; } s_script[MAX_SCRIPT];   /* --press "N:boton[*frames]" */
static int s_scriptCount;

void Pad_SetScript(const char* script)
{
    static const struct { const char* name; uint32_t mask; } names[] = {
        { "up", PADB_UP }, { "down", PADB_DOWN }, { "left", PADB_LEFT }, { "right", PADB_RIGHT },
        { "cross", PADB_CROSS }, { "circle", PADB_CIRCLE }, { "triangle", PADB_TRIANGLE },
        { "square", PADB_SQUARE }, { "start", PADB_START }, { "select", PADB_SELECT },
        { "l1", PADB_L1 }, { "r1", PADB_R1 }, { "l2", PADB_L2 }, { "r2", PADB_R2 } };
    const char* p = script;
    while (p && *p && s_scriptCount < MAX_SCRIPT)
    {
        int frame = atoi(p);
        const char* c = strchr(p, ':');
        if (!c)
            break;
        c++;
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
            if (!strncmp(c, names[i].name, strlen(names[i].name)))
            {
                s_script[s_scriptCount].frame = frame;
                s_script[s_scriptCount].mask = names[i].mask;
                s_script[s_scriptCount].len = c[strlen(names[i].name)] == '*' ? atoi(c + strlen(names[i].name) + 1) : 3;
                s_scriptCount++;
            }
        p = strchr(c, ',');
        if (p)
            p++;
    }
}

static uint32_t ScriptButtons(void)
{
    uint32_t m = 0;
    int f = Gfx_GetFlipCount();
    for (int i = 0; i < s_scriptCount; i++)
        if (f >= s_script[i].frame && f < s_script[i].frame + s_script[i].len)
            m |= s_script[i].mask;
    return m;
}
static unsigned char s_padBuf[2][34];

void Pad_Init(void)
{
    Pad_ResetBinds(0);
    Pad_ResetBinds(1);
    PadInitDirect(s_padBuf[0], s_padBuf[1]);
    PadStartCom();
}

int Pad_GetType(int port)
{
    if (port == 0 && s_scriptCount)
        return 4;
    /* PsyCross marca el puerto vacio con id 0xFF pero deja el estado a 0 (en la PS1 el estado seria 0xFF) */
    if (port < 0 || port > 1 || s_padBuf[port][0] != 0 || s_padBuf[port][1] == 0xff)
        return 0;
    return s_padBuf[port][1] >> 4;
}

/* 8001dac0. El original anade bits sinteticos para neGcon (tipo 2) y analogico (tipo 7): pendiente. */
uint32_t Pad_GetButtons(int port)
{
    if (port == 0 && s_scriptCount)
        return ScriptButtons();
    if (port < 0 || port > 1 || s_padBuf[port][0] != 0)
        return 0;
    uint32_t word = s_padBuf[port][0] | s_padBuf[port][1] << 8 | s_padBuf[port][2] << 16 | (uint32_t)s_padBuf[port][3] << 24;
    return ~word & 0xffff0000u;
}

/* 8001d840: como Pad_GetType, pero -1 para los tipos que el juego no admite (ni 2, ni 4, ni 7) */
int Pad_Kind(int port)
{
    int t = Pad_GetType(port);
    return t == 0 || t == 2 || t == 4 || t == 7 ? t : -1;
}

/* 8001cedc */
void Pad_ResetBinds(int player)
{
    static const uint32_t def[14] = { PADB_UP, PADB_DOWN, PADB_LEFT, PADB_RIGHT, PADB_SQUARE, PADB_CROSS, PADB_CIRCLE,
                                      PADB_TRIANGLE, PADB_R1, PADB_L1, PADB_L2, PADB_R2, PADB_SELECT, PADB_START };
    if (player >= 0 && player < 2)
        memcpy(g_PadBinds[player], def, sizeof(def));
}

/* 8001d448 */
int Pad_HasVibration(int port)
{
    return Pad_GetType(port & 1) == 7 && !(port == 0 && s_scriptCount);
}

/* 8001d520 */
void Pad_SetVibration(int port, int on)
{
    port = port < 0 ? 0 : port > 1 ? 1 : port;
    s_vibration[port] = (uint8_t)(on != 0);
    if (!on)
    {
        unsigned char act[2] = { 0, 0 };
        PadSetAct(port << 4, act, 2);
    }
}

/* 8001d2c8 / 8001d308: el motor pequeño (el original lo deja 300 tics; SDL lo para solo a los 200 ms) */
void Pad_TestVibration(int port)
{
    port = port < 0 ? 0 : port > 1 ? 1 : port;
    if (!s_vibration[port])
        return;
    unsigned char act[2] = { 1, 0 };
    PadSetAct(port << 4, act, 2);
}

/* 8005fc54 */
void Pad_ReadAll(void)
{
    /* PsyCross solo copia el teclado y los mandos SDL al buffer del mando en PsyX_UpdateInput, que no se
     * llama por si solo fuera de su modo de depuracion. Teclado por defecto: flechas, C = cruz, X = cuadrado,
     * V = circulo, Z = triangulo, Intro = START, espacio = SELECT, Mayus / Ctrl izq. = L1 / L2, der. = R1 / R2. */
    PsyX_UpdateInput();
    for (int i = 0; i < 2; i++)
        g_PadButtons[i] = Pad_GetButtons(i);
}
