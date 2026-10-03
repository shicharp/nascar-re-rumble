#include <stdio.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "mem.h"
#include "file.h"
#include "mdec.h"
#include "gfx.h"
#include "globals.h"
#include "frontend.h"
#include "game.h"
#include "lsc.h"

/* FUN_80095d70: el original prepara el MDEC (DecDCTin/out) y espera; aqui se decodifica por software */
int Lsc_DecodeToVram(const unsigned char* img, int size, int x, int y)
{
    static uint16_t pixels[320 * 256];
    int w, h;
    if (!Mdec_DecodeImage(img, size, pixels, 320 * 256, &w, &h))
        return 0;
    RECT16 r;
    setRECT(&r, x, y, w, h);
    LoadImage(&r, (u_long*)pixels);
    DrawSync(0);
    return 1;
}

static int LoadLscFile(const char* name)
{
    void* buf;
    int size;
    if (File_LoadAlloc(name, &buf, &size) < 0 || !buf)
        return 0;
    const unsigned char* d = (const unsigned char*)buf;
    int second = (int)NR_Read32(d);
    int ok = Lsc_DecodeToVram(d + 8, second, LSC_VRAM_X, 0) &&
             Lsc_DecodeToVram(d + second + 8, size - second - 8, LSC_VRAM_X, 0x100);
    Pool_Free(buf);
    return ok;
}

/* 80090ef8 */
void Lsc_LoadToVram(const char* name, int show)
{
    if (!LoadLscFile(name))
        return;
    if (show)
    {
        Gfx_BeginFrame(0, 0, 0);
        Fe_DrawBackground(0);   /* FUN_8008b79c(0) */
        Gfx_Flip(GFX_FLIP_FULL);
    }
}

/* 800921a0 (rama del front-end; en carrera elige Help%ld.lsc o la pantalla de la pista) */
const char* LoadScreen_GetName(void)
{
    static char name[64];
    snprintf(name, sizeof(name), "%sFeld.lsc", Level_GetDataDir());
    return name;
}

/* 80024c58 */
int LoadScreen_Show(void)
{
    if (!LoadLscFile(LoadScreen_GetName()))
        return 0;
    /* FUN_80024d10(0): pinta el texto de consejos (pendiente) */
    Gfx_BeginFrame(0, 0, 0);
    Fe_DrawBackground(0);
    Gfx_Flip(GFX_FLIP_FULL);
    return 1;
}
