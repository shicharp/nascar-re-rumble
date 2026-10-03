#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "mem.h"
#include "globals.h"
#include "texslot.h"

#define TAG_tReg NR_TAG('t', 'R', 'e', 'g')
#define TAG_PIX4 NR_TAG('P', 'I', 'X', '4')
#define TAG_PDAT NR_TAG('P', 'D', 'A', 'T')
#define TAG_CLUT NR_TAG('C', 'L', 'U', 'T')

static uint16_t s_tpage[0x16];              /* DAT_800b6418 */
static uint8_t* s_regions[TEXSLOT_COUNT];   /* DAT_800b6448: u32 n + n x {y0,x0,y1,x1, u16 clut[4]} */
static int      s_clut16Count;              /* DAT_800af918 */
static int      s_clut256Count;             /* DAT_800af91a */
uint32_t g_EnvMapUV[2];
int      g_FeLoadingSlot;

/* 80066fe0: paginas 0-8 en (0x140 + 64*i, 0) y 0xB-0x13 debajo; 9/10 son de 8 bits en (0x380, 0/256) */
void TexSlot_Init(void)
{
    for (int i = 0; i < 9; i++)
    {
        uint16_t t = (uint16_t)(((0x140 + i * 0x40) & 0x3ff) >> 6);
        s_tpage[i] = t;
        s_tpage[0xb + i] = t | 0x10;
    }
    s_tpage[9] = 0x8e;
    s_tpage[10] = 0x9e;
    s_tpage[0x14] = s_tpage[0x12] | 0x80;   /* DAT_800b6440/42: mips de las paginas 8+ a 8 bits */
    s_tpage[0x15] = s_tpage[0x13] | 0x80;
    if (g_IsFrontend)
    {
        s_tpage[0xb] = 0x9c;   /* tercer coche del garaje: 8 bits en (0x300, 256) */
        s_clut256Count = 3;
    }
    else
        s_clut256Count = 0;
    s_clut16Count = 0;
    for (int i = 0; i < TEXSLOT_COUNT; i++)
    {
        Pool_Free(s_regions[i]);
        s_regions[i] = NULL;
    }
}

/* 800669d4: tpage del nivel de mip 'level' del slot y desplazamiento (x, y) dentro de ella.
 * Los niveles 1-3 se empaquetan en paginas compartidas. */
int TexSlot_GetTPage(int slot, int level, int* x, int* y)
{
    int idx = -1, px = 0, py = 0;
    unsigned car = (unsigned)(slot - 0xe);
    if (car < 6)
    {
        /* coches de la carrera (slots 0xE-0x13) */
        switch (level)
        {
        case 0:
            if (slot == 0xe)
                idx = 0xe;
            break;
        case 1:
            if ((unsigned)(slot - 0xf) < 4)
            {
                idx = 0xf;
                px = ((slot - 0xf) & 1) << 7;
                py = ((slot - 0xf) & 2) << 6;
            }
            else if (slot == 0x13)
            {
                idx = 0x13;
                py = 0x80;
            }
            break;
        case 2:
            idx = 0x13;
            px = (int)(car & 1) * 0x40 + 0x80;
            py = (int)(car & 6) << 5;
            break;
        case 3:
            idx = 0x13;
            px = (int)(car & 1) << 5;
            py = (int)(car & 6) * 0x10 + 0x20;
            break;
        }
    }
    else
    {
        switch (level)
        {
        case 0:
            idx = slot;
            break;
        case 1:
            if (slot < 8)
            {
                px = (slot & 1) << 7;
                py = (slot & 2) << 6;
                idx = slot / 4 + 0x10;
            }
            else
            {
                py = 0x80;
                idx = 0x14;
            }
            break;
        case 2:
            if (slot < 8)
            {
                px = (slot & 3) << 6;
                py = (slot & 4) << 4;
                idx = 0x12;
            }
            else
            {
                px = 0x40;
                py = 0xc0;
                idx = 0x15;
            }
            break;
        case 3:
            if (slot < 8)
            {
                px = (slot & 1) * 0x20 + 0x40;
                py = (slot & 6) << 4;
                idx = 0x13;
            }
            else if (slot == 9)
            {
                px = 0x20;
                idx = 0x13;
            }
            break;
        }
    }
    if (x)
    {
        *x = px;
        *y = py;
    }
    return idx < 0 || idx >= 0x16 ? -1 : s_tpage[idx];
}

/* 8006674c: sube una paleta y devuelve su id de CLUT. Las de 256 colores van en (0, 0xF0 + n);
 * en el garaje, una por hueco de coche. */
static int Clut_Upload(const uint8_t* colors, int count)
{
    int x = 0, y = 0;
    if (count == 0x100)
        y = 0xf0 + (g_IsFrontend ? g_FeLoadingSlot : s_clut256Count++);
    else
    {
        int n = s_clut16Count++;
        if (n < 0x40)
        {
            x = (n & 3) * 0x10 + 0x100;
            y = (n >> 2) + 0xf0;
        }
        else if (n < 0x100)
        {
            x = ((n - 0x40) & 0xf) << 4;
            y = ((n - 0x40) >> 4) + 0xf4;
        }
        else if (n < 0x240)
        {
            x = ((n - 0x100) >> 4) << 4;
            y = ((n - 0x100) & 0xf) + 0x1f0;
        }
        else
        {
            x = ((n - 0x240) & 3) * 0x10 + 0x1c0;
            y = 0x1ff - ((n - 0x240) >> 2);
        }
    }
    RECT16 r;
    setRECT(&r, x, y, count, 1);
    LoadImage(&r, (u_long*)colors);
    DrawSync(0);
    return y << 6 | ((x >> 4) & 0x3f);
}

/* 800668cc: con 16 colores sube la paleta de cada region (si cambia respecto a la anterior) y deja
 * su CLUT en la region; con 256 hay una sola paleta (en la primera region) */
static void Clut_UploadRegions(const uint8_t* colors, int count, int level, uint8_t* regions)
{
    uint32_t n = NR_Read32(regions);
    uint8_t* r = regions + 4;
    if (count == 0x10)
    {
        int last = -1, clut = 0;
        for (uint32_t i = 0; i < n; i++, r += 12)
        {
            int pal = NR_Read16(r + 4 + level * 2);
            if (pal != last)
            {
                clut = Clut_Upload(colors + pal * 0x20, 0x10);
                last = pal;
            }
            r[4 + level * 2] = (uint8_t)clut;
            r[5 + level * 2] = (uint8_t)(clut >> 8);
        }
    }
    else
    {
        int clut = Clut_Upload(colors, count);
        r[4 + level * 2] = (uint8_t)clut;
        r[5 + level * 2] = (uint8_t)(clut >> 8);
    }
}

/* 800666c8 */
static void TexSlot_UploadPixels(const uint8_t* pixels, int tpage, int x, int y, int size, int bpp)
{
    int s = bpp == 4 ? 2 : 1;
    RECT16 r;
    setRECT(&r, (tpage & 0xf) * 0x40 + (x >> s), (tpage & 0x10) * 0x10 + ((tpage >> 2) & 0x200) + y,
            size >> s, size);
    LoadImage(&r, (u_long*)pixels);
    DrawSync(0);
}

/* 80066d00 */
void Cpag_UploadToSlot(const uint8_t* data, int size, int slot)
{
    const uint8_t* pixels[4] = { 0 };
    const uint8_t* cluts[4] = { 0 };
    int bpp[4] = { 0 }, levels = 0;
    const uint8_t* reg = NULL;

    if (slot < 0 || slot >= TEXSLOT_COUNT)
        return;
    for (int off = 0; off + 8 <= size;)
    {
        uint32_t tag = NR_Read32(data + off);
        int len = (int)NR_Read32(data + off + 4);
        if (len < 8)
            break;
        if ((tag == TAG_PDAT || tag == TAG_PIX4) && levels < 4)
        {
            bpp[levels] = tag == TAG_PDAT ? 8 : 4;
            pixels[levels++] = data + off + 8;
        }
        else if (tag == TAG_CLUT && levels > 0)
            cluts[levels - 1] = data + off + 8;
        else if (tag == TAG_tReg)
            reg = data + off;
        off += len;
    }
    if (!reg || !levels)
        return;

    uint32_t n = (NR_Read32(reg + 4) - 8) / 12;
    Pool_Free(s_regions[slot]);
    uint8_t* table = (uint8_t*)Pool_Alloc((int)(n * 12 + 4), 2);
    memcpy(table, &n, 4);
    memcpy(table + 4, reg + 8, n * 12);

    int last = g_IsFrontend ? 0 : levels - 1;   /* el garaje solo sube el nivel 0 */
    for (int l = 0; l <= last; l++)
    {
        int x, y;
        int tp = TexSlot_GetTPage(slot, l, &x, &y);
        if (tp < 0)
            continue;
        if (cluts[l])
            Clut_UploadRegions(cluts[l], 1 << bpp[l], l, table);
        TexSlot_UploadPixels(pixels[l], tp, x, y, 0x100 >> l, bpp[l]);
    }
    s_regions[slot] = table;

    if (slot == 8)
    {
        const uint8_t* r = Cpag_FindRegion(8, 0, 0x80);
        if (r)
            g_EnvMapUV[0] = (uint32_t)NR_Read16(r + 4) << 16 | 0x2d;
        r = Cpag_FindRegion(8, 0x80, 0x80);
        if (r)
            g_EnvMapUV[1] = (uint32_t)NR_Read16(r + 4) << 16 | 0x2d;
    }
}

/* 80066c4c: region (y0,x0,y1,x1 inclusivos) que contiene (u, v). Las paginas de 8 bits de los coches
 * (slots 9, 10 y en el garaje 11) tienen una sola paleta: la primera region. */
const uint8_t* Cpag_FindRegion(int slot, int u, int v)
{
    if (slot < 0 || slot >= TEXSLOT_COUNT || !s_regions[slot])
        return NULL;
    const uint8_t* r = s_regions[slot] + 4;
    if ((unsigned)(slot - 9) < 2 || (g_IsFrontend && slot == 0xb))
        return r;
    uint32_t n = NR_Read32(s_regions[slot]);
    for (uint32_t i = 0; i < n; i++, r += 12)
        if (r[1] <= u && u <= r[3] && r[0] <= v && v <= r[2])
            return r;
    return NULL;
}

/* 800674fc: convierte el slot de la cara (+0xA) en tpage y pone su CLUT (+6); los uv se escalan al
 * nivel de mip y se desplazan a su sitio en la pagina compartida. face[3] = modo de semitransparencia + 1. */
void TexSlot_PatchFace(uint8_t* face, int level)
{
    int slot = NR_Read16(face + 10);
    const uint8_t* r = Cpag_FindRegion(slot, face[4], face[5]);
    int clut = r ? NR_Read16(r + 4 + level * 2) : 0;
    int du = 0, dv = 0;
    int tp = TexSlot_GetTPage(slot, level, &du, &dv);
    if (tp < 0)
        tp = 0;
    static const int us[4] = { 4, 8, 0xc, 0xe };
    for (int i = 0; i < 4; i++)
    {
        face[us[i]] = (uint8_t)(du + (face[us[i]] >> level));
        face[us[i] + 1] = (uint8_t)(dv + (face[us[i] + 1] >> level));
    }
    face[6] = (uint8_t)clut;
    face[7] = (uint8_t)(clut >> 8);
    if (face[3])
        tp |= (face[3] - 1) << 5;
    face[10] = (uint8_t)tp;
    face[11] = (uint8_t)(tp >> 8);
}
