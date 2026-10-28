#include "libetc.h"
#include "libpad.h"
#include "PsyX/PsyX_public.h"
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "globals.h"
#include "pause.h"
#include "pad.h"
#include "padmap.h"

uint32_t g_PadButtons[2];
uint32_t g_PadBinds[2][14];                 /* DAT_800b0570 */
static uint8_t s_vibration[2] = { 1, 1 };   /* DAT_800b04ef + puerto * 0x28 */

#define MAX_SCRIPT 256
/* --press "N:[2]boton[*frames]"; el stick izquierdo con "N:[2]lx=valor[*frames]" / "ly=valor" (axis 0 / 1); con
 * "rN" el frame cuenta desde el principio de la carrera (Pad_SetRaceFrame) */
static struct { int frame, len, port, axis, value, race; uint32_t mask; } s_script[MAX_SCRIPT];
static int s_raceFrame = -1;
static int s_scriptCount;
static int s_scriptPorts;    /* mandos con pulsaciones simuladas (bit 0 siempre; bit 1 si hay alguna "2boton") */
static int s_scriptAnalog;   /* mandos simulados con stick: son analogicos (tipo 7) */

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
        int race = *p == 'r';
        int frame = atoi(p + race);
        const char* c = strchr(p, ':');
        if (!c)
            break;
        c++;
        int port = 0;
        if (*c == '2')
        {
            port = 1;   /* "2cross": el mando 2 */
            c++;
        }
        s_scriptPorts |= 1 | 1 << port;
        if ((c[0] == 'l' && (c[1] == 'x' || c[1] == 'y') && c[2] == '=') && s_scriptCount < MAX_SCRIPT)
        {
            const char* star = strchr(c, '*');
            const char* comma = strchr(c, ',');
            s_script[s_scriptCount].frame = frame;
            s_script[s_scriptCount].race = race;
            s_script[s_scriptCount].port = port;
            s_script[s_scriptCount].axis = c[1] == 'y';
            s_script[s_scriptCount].value = atoi(c + 3);
            s_script[s_scriptCount].mask = 0;
            s_script[s_scriptCount].len = star && (!comma || star < comma) ? atoi(star + 1) : 3;
            s_scriptCount++;
            s_scriptAnalog |= 1 << port;
        }
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
            if (!strncmp(c, names[i].name, strlen(names[i].name)))
            {
                s_script[s_scriptCount].frame = frame;
                s_script[s_scriptCount].race = race;
                s_script[s_scriptCount].port = port;
                s_script[s_scriptCount].axis = -1;
                s_script[s_scriptCount].mask = names[i].mask;
                s_script[s_scriptCount].len = c[strlen(names[i].name)] == '*' ? atoi(c + strlen(names[i].name) + 1) : 3;
                s_scriptCount++;
            }
        p = strchr(c, ',');
        if (p)
            p++;
    }
}

void Pad_SetRaceFrame(int frame)
{
    s_raceFrame = frame;
}

/* la entrada i esta activa en este frame (de la carrera con "r", si no del programa) */
static int ScriptActive(int i)
{
    int f = s_script[i].race ? s_raceFrame : Gfx_GetFlipCount();
    if (s_script[i].race && f < 0)
        return 0;
    return f >= s_script[i].frame && f < s_script[i].frame + s_script[i].len;
}

static uint32_t ScriptButtons(int port)
{
    uint32_t m = 0;
    for (int i = 0; i < s_scriptCount; i++)
        if (s_script[i].port == port && ScriptActive(i))
            m |= s_script[i].mask;
    return m;
}

/* eje del stick simulado: el ultimo valor activo, o el centro */
static int ScriptStick(int port, int axis)
{
    int v = 0x80;
    for (int i = 0; i < s_scriptCount; i++)
        if (s_script[i].port == port && s_script[i].axis == axis && ScriptActive(i))
            v = s_script[i].value;
    return v;
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
    if (port >= 0 && port < 2 && (s_scriptPorts >> port & 1))
        return (s_scriptAnalog >> port & 1) ? 7 : 4;
    /* PsyCross marca el puerto vacio con id 0xFF pero deja el estado a 0 (en la PS1 el estado seria 0xFF) */
    if (port < 0 || port > 1 || s_padBuf[port][0] != 0 || s_padBuf[port][1] == 0xff)
        return 0;
    return s_padBuf[port][1] >> 4;
}

/* 8001d10c(.., 0): el stick izquierdo (bytes 6 y 7 del buffer: 0 izquierda / arriba, 0x80 centro, 0xFF derecha /
 * abajo). Port: tambien con el mando en digital, porque PsyCross rellena los ejes del mando SDL en los dos modos (sin
 * mando quedan en 127, dentro de la zona muerta): asi el stick vale sin pasar el mando a analogico (SELECT + START).
 * 0 si no hay mando o es uno simulado sin stick. */
int Pad_GetStick(int port, int* x, int* y)
{
    int kind = Pad_Kind(port);
    if (kind != 7 && kind != 4)
        return 0;
    if (s_scriptPorts >> port & 1)
    {
        if (!(s_scriptAnalog >> port & 1))
            return 0;
        *x = ScriptStick(port, 0);
        *y = ScriptStick(port, 1);
    }
    else
    {
        *x = s_padBuf[port][6];
        *y = s_padBuf[port][7];
    }
    return 1;
}

/* 8001dac0. Con el mando analogico (tipo 7; en el port, cualquier mando con stick), en el front-end, en la pausa y en
 * los resultados el stick izquierdo hace de cruceta (pasado un tercio del recorrido). El neGcon (tipo 2: I, II y L como cruz, cuadrado y L1 segun su
 * calibracion) no esta: PsyCross solo emula el DualShock. */
uint32_t Pad_GetButtons(int port)
{
    uint32_t m;
    if (port >= 0 && port < 2 && (s_scriptPorts >> port & 1))
        m = ScriptButtons(port);
    else
    {
        if (port < 0 || port > 1 || s_padBuf[port][0] != 0)
            return 0;
        uint32_t word = s_padBuf[port][0] | s_padBuf[port][1] << 8 | s_padBuf[port][2] << 16 |
                        (uint32_t)s_padBuf[port][3] << 24;
        m = ~word & 0xffff0000u;
    }
    int x, y;
    if ((g_IsFrontend || g_RacePaused || g_GameState == GAMESTATE_RESULTS) && Pad_GetStick(port, &x, &y))
    {
        if (x > 0xaa)
            m |= PADB_RIGHT;
        else if (x < 0x56)
            m |= PADB_LEFT;
        if (y >= 0xab)
            m |= PADB_DOWN;
        else if (y <= 0x55)
            m |= PADB_UP;
    }
    return m;
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
    return Pad_GetType(port & 1) == 7 && !(s_scriptPorts >> (port & 1) & 1);
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
     * V = circulo, Z = triangulo, Intro = START, espacio = SELECT, Mayus / Ctrl izq. = L1 / L2, der. = R1 / R2.
     * Las teclas y botones cambiados en la pantalla de mandos solo valen conduciendo (padmap.h). */
    PadMap_Apply(!g_IsFrontend && !g_RacePaused && g_GameState == GAMESTATE_RUNNING && g_GameInfo &&
                 !g_GameInfo->isReplay);
    PsyX_UpdateInput();
    for (int i = 0; i < 2; i++)
        g_PadButtons[i] = Pad_GetButtons(i);
}
