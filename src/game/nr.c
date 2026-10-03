#include <stdarg.h>
#include <stdio.h>
#include "nr.h"

void NR_Log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
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
