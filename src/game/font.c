#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "mem.h"
#include "gfx.h"
#include "globals.h"
#include "font.h"

static_assert(sizeof(FontInfo) == 0x120, "FontInfo debe medir 0x120");

static FontInfo* s_fonts[FONT_MAX];   /* DAT_800b54a0 */
static uint16_t  s_tpage[FONT_MAX];   /* DAT_800b5480 */
static uint8_t   s_vOffset[FONT_MAX]; /* DAT_800b5490 (u16, se usa el byte bajo) */
static int       s_numFonts;          /* DAT_800af2f8 */
static int       s_numPalettes;       /* DAT_800af2f4 */
static FontInfo* s_cur;               /* DAT_800af2f0 */
static int       s_curIndex;          /* DAT_800af848 */
static uint16_t  s_clut;              /* DAT_800af840: CLUT del texto actual (0 = sin fuentes) */
static uint16_t  s_clutAlt;           /* DAT_800af842: paleta 1 (flags & 4) */
static uint16_t  s_clutStd;           /* DAT_800af844: paleta 0 */
static int       s_nextV;             /* DAT_800af846 */
static int       s_deferUpload;       /* solo en el port: subir los bitmaps al terminar la carga */
static int       s_numUploaded;
static uint16_t  s_palettes[2][16];   /* DAT_800b54c0 / DAT_800b54e0 */

/* 800aa570: colores para los codigos 10..31 (RGB555 con R en los bits altos) */
static const uint16_t s_colors[22] = {
    0x0000, 0x7e10, 0x2bea, 0x421f, 0x4210, 0x7c00, 0x03e0, 0x211f, 0x7fff, 0x7fe0, 0x7c1f,
    0x03ff, 0x6200, 0x5014, 0x0294, 0x3638, 0x331f, 0x7fff, 0x7fff, 0x7fff, 0x0000, 0x0000 };

/* 80043f88: rampa de 15 tonos hacia (c10, c5, c0); entrada 0 transparente */
static void Font_BuildPalette(int mode, int pal, int c10, int c5, int c0)
{
    if (pal < 0 || pal > 1)
        return;
    uint16_t* p = s_palettes[pal];
    p[0] = 0;
    for (int i = 2; i < 17; i++)
    {
        uint16_t stp, v;
        if (mode == 0)
        {
            stp = i < 9 ? 0x8000 : 0;
            /* el original acumula 2c, 3c... 16c y divide entre 16 */
            v = (uint16_t)(((c0 * i) >> 4) | (((c10 * i) >> 4) << 10) | (((c5 * i) >> 4) << 5));
        }
        else
        {
            int k = i;
            stp = 0x8000;
            if (k > 8)
            {
                stp = 0;
                k -= 8;
            }
            v = (uint16_t)(((c0 * k) >> 3) | (((c10 * k) >> 3) << 10) | (((c5 * k) >> 3) << 5));
        }
        p[i - 1] = stp | v;
    }
}

/* 800446b8 */
void Font_Init(void)
{
    for (int i = 0; i < FONT_MAX; i++)
    {
        if (s_fonts[i])
            Pool_Free(s_fonts[i]);
        s_fonts[i] = NULL;
        s_tpage[i] = 0;
    }
    s_nextV = 1;
    s_numUploaded = 0;
    s_clut = 0;
    s_cur = NULL;
    s_numPalettes = 0;
    s_numFonts = 0;
    Font_BuildPalette(0, s_numPalettes++, 0x1f, 0x1f, 0x1f);
    Font_BuildPalette(1, s_numPalettes++, 0x1f, 0x1f, 0x1f);
}

/* 800445f4 */
static void Font_SelectClut(void)
{
    s_clut = (s_cur->flags & 4) ? s_clutAlt : s_clutStd;
}

/* 8004463c */
int Font_Select(int index)
{
    if (index >= s_numFonts)
        return 0;
    s_cur = s_fonts[index];
    s_curIndex = index;
    Font_SelectClut();
    return 1;
}

void Font_SetDeferUpload(int defer)
{
    s_deferUpload = defer;
}

static void Font_Upload(int index);

void Font_FlushUploads(void)
{
    while (s_numUploaded < s_numFonts)
        Font_Upload(s_numUploaded++);
}

int Font_Count(void)
{
    return s_numFonts;
}

/* 800443cc: los bitmaps se apilan en VRAM; en el front-end en x = 0x2C0, y = 0x100 + offset */
static void Font_Upload(int index)
{
    const int x = g_IsFrontend ? 0x2c0 : 0x1c0;
    const int clutY = g_IsFrontend ? 0x100 : 0x1a8;
    const uint8_t* bmp = s_fonts[index]->bitmap;
    int w = (int16_t)NR_Read16(bmp + 4), h = (int16_t)NR_Read16(bmp + 6);
    if (s_nextV + h >= 0x100)
        return;

    s_vOffset[index] = (uint8_t)s_nextV;
    RECT16 r;
    setRECT(&r, x, 0x100 + s_nextV, w / 4, h);
    LoadImage(&r, (u_long*)(bmp + 0x10));
    DrawSync(0);
    s_tpage[index] = getTPage(0, 0, x, 0x100 + s_nextV);
    s_nextV += h;

    if (index == 0)
    {
        setRECT(&r, x, clutY, 16, 1);
        LoadImage(&r, (u_long*)s_palettes[0]);
        DrawSync(0);
        s_clutStd = getClut(x, clutY);
        setRECT(&r, x, clutY - 1, 16, 1);
        LoadImage(&r, (u_long*)s_palettes[1]);
        DrawSync(0);
        s_clutAlt = getClut(x, clutY - 1);
    }
}

/* 800441a4 */
static void Font_Load(const uint8_t* d, int param)
{
    if (s_numFonts >= FONT_MAX)
        return;
    FontInfo* f = (FontInfo*)Pool_Alloc(sizeof(FontInfo), 2);
    memset(f, 0, sizeof(*f));
    s_fonts[s_numFonts] = f;

    f->param = param;
    f->flags = NR_Read32(d + 0xc);
    f->hdr12 = (int8_t)d[0x12];
    f->hdr13 = (int8_t)d[0x13];
    f->numGlyphs = (short)NR_Read16(d + 0xa);
    f->bitmap = d + NR_Read32(d + 0x1c);
    f->glyphs = d + NR_Read32(d + 0x14);
    int sum = 0;
    for (int i = 0; i < f->numGlyphs; i++)
    {
        const uint8_t* g = f->glyphs + i * 11;
        f->map[g[0]] = (uint8_t)i;
        sum += g[2];
        if (f->maxW < g[2]) f->maxW = g[2];
        if (f->maxH < g[3]) f->maxH = g[3];
    }
    f->avgW = (short)(f->numGlyphs ? sum / f->numGlyphs : 0);
    f->bpp = (f->bitmap[0] == 'D') ? 1 : 4;          /* 8004417c */
    f->rowBytes = (short)((((int16_t)NR_Read16(f->bitmap + 4) * f->bpp + 0x1f) & ~0x1f) >> 3);

    s_numFonts++;
    if (!s_deferUpload)
        Font_FlushUploads();
    if (s_numFonts == 1)
        Font_Select(0);
}

/* 80044690. El asset se queda en memoria: la fuente apunta a sus glifos. */
int Font_OnCfnt(NrAsset* a)
{
    Font_Load(a->data, 0);
    return 1;
}

/* 80044158 */
const uint8_t* Font_GetGlyph(int c)
{
    return s_cur->glyphs + s_cur->map[c & 0xff] * 11;
}

/* Avance (+8 del glifo) del caracter c en la fuente actual: el menu principal (8007b2ac) estrecha la r un pixel
 * mientras pinta sus opciones. Los glifos estan en el asset, como en el original. */
void Font_AdjustAdvance(int c, int delta)
{
    if (!s_cur)
        return;
    uint8_t* g = (uint8_t*)(uintptr_t)Font_GetGlyph(c);
    g[8] = (uint8_t)(g[8] + delta);
}

static void ColorToRgb(unsigned color, uint8_t* r, uint8_t* g, uint8_t* b)
{
    unsigned c = (color & 0x8000) ? (color & 0x7fff) : s_colors[(color & 0xffff) < 22 ? (color & 0xffff) : 0];
    *r = (uint8_t)((c >> 8) & 0x7c);
    *g = (uint8_t)((c >> 3) & 0x7c);
    *b = (uint8_t)((c & 0x1f) << 2);
}

static void AddTextTPage(void)
{
    DR_TPAGE* tp = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    if (!tp)
        return;
    setDrawTPage(tp, 1, 1, s_tpage[s_curIndex] & 0x9ff);
    Gfx_AddPrimDepth(0, tp);
}

/* 800447b8: devuelve el avance */
int Font_DrawChar(int c, int x, int y, unsigned color)
{
    const uint8_t* g = Font_GetGlyph(c);
    SPRT* s = (SPRT*)Gfx_AllocPrim(sizeof(SPRT));
    if (s)
    {
        setSprt(s);
        setSemiTrans(s, 1);
        ColorToRgb(color, &s->r0, &s->g0, &s->b0);
        setUV0(s, g[4], g[6] + s_vOffset[s_curIndex]);
        setWH(s, g[2], g[3]);
        setXY0(s, x + (int8_t)g[9], y + (int8_t)g[10]);
        s->clut = s_clut;
        Gfx_AddPrimDepth(0, s);
    }
    return (int8_t)g[8];
}

/* 80044944 */
void Font_DrawString(const char* str, int x, int y, unsigned color)
{
    if (!s_clut || !str)
        return;
    AddTextTPage();
    for (const uint8_t* p = (const uint8_t*)str; *p; p++)
    {
        if (*p >= 10 && *p < 32)
            color = *p - 10;
        else
            x += Font_DrawChar(*p, x, y, color & 0xffff);
    }
}

/* 80044a44: como Font_DrawString pero recortando a [clipX0, clipX1] */
void Font_DrawClipped(const char* str, int x, int y, int clipX0, int clipX1, unsigned color)
{
    if (!s_clut || !str)
        return;
    AddTextTPage();
    for (const uint8_t* p = (const uint8_t*)str; *p; p++)
    {
        if (*p >= 10 && *p < 32)
        {
            color = *p - 10;
            continue;
        }
        const uint8_t* g = Font_GetGlyph(*p);
        int gx = x + (int8_t)g[9];
        if (gx > clipX1)
            return;
        if (gx + g[2] >= clipX0)
        {
            SPRT* s = (SPRT*)Gfx_AllocPrim(sizeof(SPRT));
            if (s)
            {
                setSprt(s);
                setSemiTrans(s, 1);
                ColorToRgb(color, &s->r0, &s->g0, &s->b0);
                int skip = gx < clipX0 ? clipX0 - gx : 0;
                int w = g[2] - skip;
                if (gx + g[2] > clipX1)
                    w -= gx + g[2] - clipX1;
                setUV0(s, g[4] + skip, g[6] + s_vOffset[s_curIndex]);
                setWH(s, w, g[3]);
                setXY0(s, gx + skip, y + (int8_t)g[10]);
                s->clut = s_clut;
                Gfx_AddPrimDepth(0, s);
            }
        }
        x += (int8_t)g[8];
    }
}

/* 80044d24: texto centrado con ajuste de linea en [x0, x1] desde y0 hasta y1. Devuelve el alto usado.
 * Con un color >= 100 (sin bit 15) solo mide. */
int Font_DrawWrapped(int x0, int y0, int x1, int y1, const char* str, unsigned color)
{
    if (!s_clut || !str)
        return 0;
    const int draw = (color & 0x8000) || (color & 0xffff) < 100;
    if (draw)
        AddTextTPage();
    const int lineH = s_cur->maxH;
    const int len = (int)strlen(str);
    int start = 0, end = 0, y = y0;
    if (len <= 0)
        return 0;

    for (; y + lineH <= y1; y += lineH)
    {
        /* cuantos caracteres caben */
        int i = start, w = x0;
        while (i < len && w < x1)
            w += (int8_t)Font_GetGlyph((uint8_t)str[i++])[8];
        if (w < x1)
            end = i;                                    /* cabe todo lo que queda */
        else if (i < len && str[i] == ' ')
            end = i + 1;
        else
        {
            int j = i - 1;
            while (j >= start && str[j] != ' ')
                j--;
            end = j >= start ? j + 1 : i - 1;           /* corta en el ultimo espacio */
        }

        int lw = 0;
        for (int k = start; k < end; k++)
            lw += (int8_t)Font_GetGlyph((uint8_t)str[k])[8];
        int x = x0 + (x1 - x0) / 2 - lw / 2;
        if (draw)
            for (int k = start; k < end; k++)
            {
                uint8_t c = (uint8_t)str[k];
                if (c >= 10 && c < 32)
                    color = c - 10;
                else
                    x += Font_DrawChar(c, x, y, color & 0xffff);
            }
        if (end >= len)
            break;
        start = end;
    }
    return end ? y + lineH - y0 : 0;
}

/* 80045140 */
void Font_Measure(const char* str, int* w, int* h)
{
    int width = 0, height = 0;
    if (s_cur && str)
    {
        for (const uint8_t* p = (const uint8_t*)str; *p; p++)
            if (*p >= 0x20)
                width += (int8_t)Font_GetGlyph(*p)[8];
        height = s_cur->maxH;
    }
    *w = width < 1 ? 0 : width;
    *h = height;
}
