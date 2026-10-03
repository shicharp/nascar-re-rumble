#include <stdlib.h>
#include "nr.h"
#include "file.h"
#include "exe.h"

static uint8_t* s_exe;
static int      s_exeSize;
static uint32_t s_textAddr, s_textSize;

/* Cabecera PS-X EXE: +0x18 direccion de carga, +0x1C tamano; el codigo y los datos empiezan en 0x800 */
int Exe_Load(void)
{
    if (s_exe)
        return 1;
    int slot = File_OpenSearchPaths("SLUS_010.68");
    int size = slot >= 0 ? File_GetSize(slot) : 0;
    uint8_t* buf = size > 0x800 ? (uint8_t*)malloc((size_t)size) : NULL;
    if (!buf || File_ReadAll(slot, buf, NULL) != size)
    {
        NR_Log("Exe_Load: no se pudo leer SLUS_010.68\n");
        free(buf);
        if (slot >= 0 && !buf)
            File_Close(slot);
        return 0;
    }
    s_exe = buf;
    s_exeSize = size;
    s_textAddr = NR_Read32(s_exe + 0x18);
    s_textSize = NR_Read32(s_exe + 0x1c);
    NR_Log("Exe_Load: %08x + %x\n", s_textAddr, s_textSize);
    return 1;
}

const uint8_t* Exe_Ptr(uint32_t addr)
{
    if (!s_exe && !Exe_Load())
        return NULL;
    uint32_t off = addr - s_textAddr;
    if (addr < s_textAddr || off >= s_textSize || 0x800 + off >= (uint32_t)s_exeSize)
        return NULL;
    return s_exe + 0x800 + off;
}
