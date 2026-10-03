#include <stdlib.h>
#include "mem.h"

/* DAT_80010028 (uso actual) y DAT_8001002c (pico) en el original */
static int s_used;
static int s_peak;

/* 8001cb84: reserva size + 4 y guarda el tamano delante del bloque */
void* Mem_Alloc(int size, int tag)
{
    (void)tag;
    if (size == 0)
        return NULL;

    int* p = (int*)malloc((size_t)size + sizeof(int));
    if (!p)
        return NULL;

    s_used += size + (int)sizeof(int);
    if (s_used > s_peak)
        s_peak = s_used;

    *p = size;
    return p + 1;
}

/* 8001cc04 */
void Mem_Free(void* ptr)
{
    if (!ptr)
        return;
    int* p = (int*)ptr - 1;
    s_used -= *p + (int)sizeof(int);
    free(p);
}

/* 80017154: el original elige pool por modo (0 heap, 1/4 desde arriba, 2 desde abajo, 3 pools fijos) */
void* Pool_Alloc(int size, int mode)
{
    (void)mode;
    return Mem_Alloc(size, 0);
}

/* 8001745c */
void Pool_Free(void* p)
{
    Mem_Free(p);
}

int Mem_GetUsed(void) { return s_used; }
int Mem_GetPeak(void) { return s_peak; }
