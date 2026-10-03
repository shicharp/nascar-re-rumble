/* Paginas de textura Cpag.
 * Original: Cpag_OnLoaded 8006710c (slot = id - 1; coches id >= 10000 -> slot 0xE + indice),
 * Cpag_UploadToSlot 80066d00 (sube PIX4/PDAT + CLUT a VRAM y guarda la tabla tReg del slot),
 * Cpag_FindRegion 80066c4c (CLUT que corresponde a un (u, v)).
 * Formato: subchunks tReg (regiones u8 y0,x0,y1,x1 + indice de paleta), PIX4/PDAT, CLUT; varias
 * parejas PIX/CLUT = mipmaps. */
#ifndef NR_CPAG_H
#define NR_CPAG_H

#include "stream.h"

typedef struct
{
    uint8_t  y0, x0, y1, x1;    /* inclusivos, Y primero (ver NOTAS.md) */
    uint16_t pal;
} CpagRegion;

typedef struct
{
    int             bpp;        /* 4 u 8 */
    int             side;       /* 256, 128, 64, 32 */
    const uint8_t*  pixels;
    const uint16_t* colors;     /* BGR555 de PS1 */
    int             numColors;
} CpagLevel;

typedef struct
{
    int        id;
    char       section[17];
    int        numRegions;
    CpagRegion regions[64];
    int        numLevels;
    CpagLevel  levels[4];
    NrAsset*   asset;           /* duenyo de los datos */

    /* Ubicacion en VRAM tras Cpag_Upload */
    int      uploaded;
    int      vramX, vramY, clutX, clutY;
    uint16_t tpage;
} CpagPage;

int       Cpag_OnLoaded(NrAsset* a);
int       Cpag_Count(void);
CpagPage* Cpag_Get(int index);
void      Cpag_FreeAll(void);

/* Sube el nivel 0 a VRAM: pixeles en (vramX, vramY), paletas en (clutX, clutY) una por fila */
void     Cpag_Upload(CpagPage* p, int vramX, int vramY, int clutX, int clutY);
uint16_t Cpag_RegionClut(const CpagPage* p, const CpagRegion* r);

#endif
