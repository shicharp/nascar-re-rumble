/* Visor de depuracion: carga un fichero de stream entero y muestra sus paginas Cpag desde la VRAM.
 * Flechas izquierda/derecha cambian de pagina, arriba/abajo de 10 en 10. */
#include <stdio.h>
#include "libgte.h"
#include "libgpu.h"
#include "libetc.h"

#include "game/nr.h"
#include "game/mem.h"
#include "game/stream.h"
#include "game/asset.h"
#include "game/cpag.h"
#include "game/gfx.h"
#include "game/pad.h"
#include "debug_viewer.h"

#define PAGE_VRAM_X 512
#define PAGE_VRAM_Y 0
#define CLUT_VRAM_X 320
#define CLUT_VRAM_Y 256

static void DrawPage(CpagPage* p)
{
    const int size = 232, ox = (GFX_SCREEN_W - size) / 2, oy = (GFX_SCREEN_H - size) / 2;
    const int side = p->levels[0].side;

    for (int i = 0; i < p->numRegions || i == 0; i++)
    {
        CpagRegion full = { 0, 0, (uint8_t)(side - 1), (uint8_t)(side - 1), 0 };
        const CpagRegion* r = p->numRegions ? &p->regions[i] : &full;

        POLY_FT4* poly = (POLY_FT4*)Gfx_AllocPrim(sizeof(POLY_FT4));
        if (!poly)
            break;
        setPolyFT4(poly);
        setShadeTex(poly, 1);
        int x0 = ox + r->x0 * size / side, x1 = ox + (r->x1 + 1) * size / side;
        int y0 = oy + r->y0 * size / side, y1 = oy + (r->y1 + 1) * size / side;
        setXY4(poly, x0, y0, x1, y0, x0, y1, x1, y1);
        /* UV de 8 bits: el borde derecho/inferior de una region que llega a 255 no puede ser 256 */
        int u1 = r->x1 < 255 ? r->x1 + 1 : 255, v1 = r->y1 < 255 ? r->y1 + 1 : 255;
        setUV4(poly, r->x0, r->y0, u1, r->y0, r->x0, v1, u1, v1);
        poly->tpage = p->tpage;
        poly->clut = Cpag_RegionClut(p, r);
        Gfx_AddPrim(1, poly);
    }
}

int DebugViewer_Run(const char* file, int page)
{
    Gfx_InitBuffers(200);

    g_AssetViewerMode = 1;
    if (Stream_Open(file) < 0)
        return 1;
    while (Stream_Pump(16) > 0)
        Asset_DispatchAll();
    Stream_Close();
    const StreamStats* st = Stream_GetStats();
    NR_Log("%s: %d bloques, %d assets, %d chunks de audio, %d otros, %d errores\n",
           file, st->blocks, st->assets, st->vagChunks, st->otherChunks, st->errors);
    Asset_LogSummary();
    if (Cpag_Count() == 0)
        return 1;

    int uploaded = -1;
    uint32_t prev = 0;
    for (;;)
    {
        if (page < 0) page = Cpag_Count() - 1;
        if (page >= Cpag_Count()) page = 0;
        CpagPage* p = Cpag_Get(page);
        if (uploaded != page)
        {
            Cpag_Upload(p, PAGE_VRAM_X, PAGE_VRAM_Y, CLUT_VRAM_X, CLUT_VRAM_Y);
            uploaded = page;
        }

        Gfx_BeginFrame(24, 24, 40);
        DrawPage(p);
        FntPrint("PAGINA %d/%d  ID %d  %dBPP %dx%d\n", page + 1, Cpag_Count(), p->id,
                 p->levels[0].bpp, p->levels[0].side, p->levels[0].side);
        FntPrint("%d REGIONES  %d MIPS  %s\n", p->numRegions, p->numLevels, p->section);
        Gfx_Flip(GFX_FLIP_FULL);

        uint32_t pad = Pad_GetButtons(0);
        uint32_t pressed = pad & ~prev;
        prev = pad;
        if (pressed & PADB_RIGHT) page++;
        if (pressed & PADB_LEFT)  page--;
        if (pressed & PADB_UP)    page += 10;
        if (pressed & PADB_DOWN)  page -= 10;
    }
}
