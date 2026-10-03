/* Port de NASCAR Rumble (PS1) sobre PsyCross.
 *
 * Primer hito: arranque de PsyCross, carga por streaming de FE.TRK con el sistema de chunks del juego,
 * despacho de assets y visor de las paginas de textura Cpag subidas a la VRAM emulada.
 * Controles: izquierda/derecha cambian de pagina, arriba/abajo saltan 10, Escape sale.
 *
 * Uso: nascar.exe [--disc <carpeta del disco extraido>] [--file <fichero de stream>]
 *                 [--page N] [--shot salida.bmp]   (--shot: guarda un frame y sale)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "libgte.h"
#include "libgpu.h"
#include "libetc.h"
#include "libpad.h"
#include "PsyX/PsyX_public.h"

#include "game/nr.h"
#include "game/mem.h"
#include "game/file.h"
#include "game/stream.h"
#include "game/asset.h"
#include "game/cpag.h"
#include "game/gfx.h"

#define PAGE_VRAM_X 512
#define PAGE_VRAM_Y 0
#define CLUT_VRAM_X 320
#define CLUT_VRAM_Y 256

static int DirExists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFDIR);
}

/* Busca la carpeta del disco extraido subiendo desde el directorio de trabajo */
static const char* FindDiscRoot(void)
{
    static const char* candidates[] = { "disc", "../disc", "../../disc", "../../../disc", "../../../../disc" };
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++)
    {
        char probe[256];
        snprintf(probe, sizeof(probe), "%s/CW", candidates[i]);
        if (DirExists(probe))
            return candidates[i];
    }
    return NULL;
}

/* Carga completa de un fichero de stream (el juego lo hace por bloques mientras corre) */
static void LoadStream(const char* name)
{
    if (Stream_Open(name) < 0)
        return;
    while (Stream_Pump(16) > 0)
        Asset_DispatchAll();
    Stream_Close();

    const StreamStats* st = Stream_GetStats();
    NR_Log("%s: %d bloques, %d assets, %d chunks de audio, %d otros, %d errores\n",
           name, st->blocks, st->assets, st->vagChunks, st->otherChunks, st->errors);
    Asset_LogSummary();
    NR_Log("Memoria en uso: %d KB (pico %d KB)\n", Mem_GetUsed() / 1024, Mem_GetPeak() / 1024);
}

/* Dibuja una pagina a 240x240 centrada, region a region con su paleta */
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

int main(int argc, char** argv)
{
    const char* disc = NULL;
    const char* file = "CW\\FEnd\\Fe.trk";   /* Frontend_Load: Level_GetDataDir() + "Fe.trk" */
    const char* shot = NULL;
    int page = 0;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
    }

    PsyX_Initialise("NASCAR Rumble", 960, 720, 0);

    if (!disc)
        disc = FindDiscRoot();
    if (!disc)
    {
        NR_Log("No encuentro el disco extraido; usa --disc <carpeta>\n");
        PsyX_Shutdown();
        return 1;
    }
    File_SetRoot(disc);
    NR_Log("Disco: %s\n", disc);

    Gfx_Init();
    Gfx_InitBuffers(0x5818 + 200 * 0x68);   /* tamano que pide main() con Gfx_InitBuffers(200, ...) */
    static unsigned char padBuf[2][34];
    PadInitDirect(padBuf[0], padBuf[1]);
    PadStartCom();

    LoadStream(file);
    if (Cpag_Count() == 0)
    {
        NR_Log("No hay paginas Cpag en %s\n", file);
        PsyX_Shutdown();
        return 1;
    }

    int uploaded = -1;
    u_int prevPad = 0;
    for (int frame = 0;; frame++)
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
        Gfx_Flip();

        if (shot && frame == 3)
        {
            Gfx_SaveScreenshot(shot);
            NR_Log("Captura guardada en %s\n", shot);
            break;
        }

        /* buffer del mando PS1: [0] estado (0 = ok), [2..3] botones activos a nivel bajo */
        u_int pad = padBuf[0][0] == 0 ? (~((padBuf[0][2] << 8) | padBuf[0][3]) & 0xffff) : 0;
        u_int pressed = pad & ~prevPad;
        prevPad = pad;
        if (pressed & PADLright) page++;
        if (pressed & PADLleft)  page--;
        if (pressed & PADLup)    page += 10;
        if (pressed & PADLdown)  page -= 10;
    }

    Cpag_FreeAll();
    Gfx_FreeBuffers();
    PsyX_Shutdown();
    return 0;
}
