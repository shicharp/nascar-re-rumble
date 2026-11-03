/* Fuentes Cfnt.
 * Formato: "FNTP", +0xA u16 nº de glifos, +0xC flags, +0x12/+0x13 alturas, +0x14 tabla de glifos,
 * +0x1C bitmap ('@' = 4 bpp, +4 ancho, +6 alto, pixeles en +0x10).
 * Glifo de 11 bytes: [0] caracter, [2] ancho, [3] alto, [4] u, [6] v, [8] avance, [9] dx, [10] dy (con signo).
 * Original: Font_Init 800446b8, Font_BuildPalette 80043f88, Font_Load 800441a4 (Cfnt 80044690),
 * Font_Upload 800443cc, Font_Select 8004463c (+800445f4), Font_GetGlyph 80044158, Font_DrawChar 800447b8,
 * Font_DrawString 80044944, Font_DrawClipped 80044a44, Font_DrawWrapped 80044d24, Font_Measure 80045140.
 * Colores: codigos 10..31 dentro del texto (o el parametro) indexan la tabla 800aa570; con el bit 15
 * activo el color va directo (RGB555 con R en los bits altos). */
#ifndef NR_FONT_H
#define NR_FONT_H

#include <stdint.h>
#include "stream.h"

#define FONT_MAX 6

typedef struct
{
    short    bpp;          /* +0x00 */
    short    hdr12;        /* +0x02 */
    short    hdr13;        /* +0x04 */
    short    maxH;         /* +0x06 alto de linea */
    short    maxW;         /* +0x08 */
    short    avgW;         /* +0x0A */
    short    numGlyphs;    /* +0x0C */
    short    rowBytes;     /* +0x0E */
    const uint8_t* glyphs; /* +0x10 */
    const uint8_t* bitmap; /* +0x14 */
    uint32_t flags;        /* +0x18 */
    int      param;        /* +0x1C */
    uint8_t  map[256];     /* +0x20 caracter -> glifo */
} FontInfo;

void Font_Init(void);
int  Font_OnCfnt(NrAsset* a);
int  Font_Select(int index);
int  Font_Count(void);

const uint8_t* Font_GetGlyph(int c);
void Font_AdjustAdvance(int c, int delta);
int  Font_DrawChar(int c, int x, int y, unsigned color);
void Font_DrawString(const char* s, int x, int y, unsigned color);
void Font_DrawStringDepth(const char* s, int x, int y, unsigned color, int depth);   /* port */
void Font_DrawClipped(const char* s, int x, int y, int clipX0, int clipX1, unsigned color);
int  Font_DrawWrapped(int x0, int y0, int x1, int y1, const char* s, unsigned color);
void Font_Measure(const char* s, int* w, int* h);

/* Solo en el port: en la carga de la carrera las fuentes irian encima de la pantalla de carga (que aqui se
 * redibuja desde VRAM cada frame); se suben al terminar */
void Font_SetDeferUpload(int defer);
void Font_FlushUploads(void);

#endif
