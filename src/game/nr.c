#include <stdarg.h>
#include <stdio.h>
#include "PsyX/PsyX_globals.h"
#include "nr.h"

/* Por el log de PsyCross: a la salida (la consola con --console, o donde se redirija) y a "NASCAR Rumble.log" desde
 * PsyX_Initialise (antes, solo a la salida) */
void NR_Log(const char* fmt, ...)
{
    char buf[4000];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    PsyX_Log("%s", buf);
    fflush(stdout);
}

void NR_TagToStr(uint32_t tag, char out[5])
{
    for (int i = 0; i < 4; i++)
    {
        char c = (char)(tag >> (24 - i * 8));
        out[i] = (c >= 32 && c < 127) ? c : '?';
    }
    out[4] = 0;
}
