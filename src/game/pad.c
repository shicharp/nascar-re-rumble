#include "libetc.h"
#include "libpad.h"
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "pad.h"

uint32_t g_PadButtons[2];

#define MAX_SCRIPT 64
static struct { int frame; uint32_t mask; } s_script[MAX_SCRIPT];
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
        if (f >= s_script[i].frame && f < s_script[i].frame + 3)
            m |= s_script[i].mask;
    return m;
}
static unsigned char s_padBuf[2][34];

void Pad_Init(void)
{
    PadInitDirect(s_padBuf[0], s_padBuf[1]);
    PadStartCom();
}

int Pad_GetType(int port)
{
    if (port == 0 && s_scriptCount)
        return 4;
    if (port < 0 || port > 1 || s_padBuf[port][0] != 0)
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

/* 8005fc54 */
void Pad_ReadAll(void)
{
    for (int i = 0; i < 2; i++)
        g_PadButtons[i] = Pad_GetButtons(i);
}
