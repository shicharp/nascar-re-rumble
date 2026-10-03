#include "libetc.h"
#include "libpad.h"
#include "pad.h"

uint32_t g_PadButtons[2];
static unsigned char s_padBuf[2][34];

void Pad_Init(void)
{
    PadInitDirect(s_padBuf[0], s_padBuf[1]);
    PadStartCom();
}

/* 8001dac0. El original anade bits sinteticos para neGcon (tipo 2) y analogico (tipo 7): pendiente. */
uint32_t Pad_GetButtons(int port)
{
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
