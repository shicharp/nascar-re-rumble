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
#include "savegame.h"
#include "lsc.h"

uint8_t g_LoadHelp;   /* DAT_800b7117: la proxima carga de carrera pone la ayuda de los mandos */

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
    int ok = Lsc_DecodeToVram(d + 8, size - 8, LSC_VRAM_X, 0) &&
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

/* 800921a0: front-end "Feld.lsc"; carrera "<sede><pista>{a,b,c}.lsc" (letra al azar). La primera carrera tras
 * arrancar o cargar la partida (DAT_800b7117), con un jugador, mando analogico o digital (0 / 1: Help1 / Help2), el
 * transmision 0 (DAT_800b797f) y fuera de la demostracion, pone en su lugar la ayuda de los mandos
 * (CW\HelpScrn\Help%ld.lsc). */
const char* LoadScreen_GetName(void)
{
    static char name[64];
    if (g_IsFrontend)
        snprintf(name, sizeof(name), "%sFeld.lsc", Level_GetDataDir());
    else if (g_Settings.numPlayers == 1 && g_LoadHelp && g_Settings.transmission[0] == 0 &&
             g_SaveExtra.padKind[0] < 2 && !g_Settings.demo)
    {
        snprintf(name, sizeof(name), "%s%sHelp%d.lsc", "CW\\", "HelpScrn\\", g_SaveExtra.padKind[0] + 1);
        g_LoadHelp = 0;
    }
    else
    {
        g_LoadHelp = 0;
        int track = g_Settings.champType ? g_Settings.curTrack : g_Settings.track;
        snprintf(name, sizeof(name), "%s%s%d%c.lsc", Level_GetDataDir(), Fe_VenueCode(g_Venue), track + 1,
                 "abc"[g_Ticks % 3]);
    }
    return name;
}

/* 80024c58 */
int LoadScreen_Show(void)
{
    if (!LoadLscFile(LoadScreen_GetName()))
        return 0;
    /* 80024d10(0): la imagen en dos sprites de 160 x 240 (tpages 0x705 / 0x715) = Fe_DrawBackground(0) */
    Gfx_BeginFrame(0, 0, 0);
    Fe_DrawBackground(0);
    Gfx_Flip(GFX_FLIP_FULL);
    return 1;
}
