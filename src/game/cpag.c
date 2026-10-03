#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "mem.h"
#include "cpag.h"

#define TAG_tReg NR_TAG('t', 'R', 'e', 'g')
#define TAG_PIX4 NR_TAG('P', 'I', 'X', '4')
#define TAG_PDAT NR_TAG('P', 'D', 'A', 'T')
#define TAG_CLUT NR_TAG('C', 'L', 'U', 'T')

#define MAX_PAGES 512

static CpagPage* s_pages[MAX_PAGES];
static int       s_numPages;

static int ParsePage(CpagPage* p, const uint8_t* d, int len)
{
    int off = 0;
    CpagLevel* pending = NULL;

    while (off + 8 <= len)
    {
        uint32_t tag  = NR_Read32(d + off);
        int      size = (int)NR_Read32(d + off + 4);
        const uint8_t* body = d + off + 8;
        int bodyLen = size - 8;
        if (size < 8 || off + size > len)
            return 0;

        if (tag == TAG_tReg)
        {
            int n = bodyLen / 12;
            if (n > 64)
                n = 64;
            for (int i = 0; i < n; i++)
            {
                const uint8_t* r = body + i * 12;
                p->regions[i].y0 = r[0];
                p->regions[i].x0 = r[1];
                p->regions[i].y1 = r[2];
                p->regions[i].x1 = r[3];
                p->regions[i].pal = NR_Read16(r + 4);
            }
            p->numRegions = n;
        }
        else if ((tag == TAG_PIX4 || tag == TAG_PDAT) && p->numLevels < 4)
        {
            pending = &p->levels[p->numLevels++];
            pending->bpp = (tag == TAG_PIX4) ? 4 : 8;
            int pixels = bodyLen * 8 / pending->bpp;
            int side = 1;
            while (side * side < pixels)
                side <<= 1;
            pending->side = side;
            pending->pixels = body;
        }
        else if (tag == TAG_CLUT && pending)
        {
            pending->colors = (const uint16_t*)body;
            pending->numColors = bodyLen / 2;
            pending = NULL;
        }
        off += size;
    }
    return p->numLevels > 0;
}

int Cpag_OnLoaded(NrAsset* a)
{
    if (s_numPages == MAX_PAGES)
        return 0;

    CpagPage* p = (CpagPage*)Mem_Alloc(sizeof(CpagPage), 0);
    memset(p, 0, sizeof(*p));
    p->id = a->id;
    memcpy(p->section, a->section, sizeof(p->section));
    if (!ParsePage(p, a->data, a->size))
    {
        NR_Log("Cpag %d: formato no reconocido\n", a->id);
        Mem_Free(p);
        return 0;
    }
    p->asset = a;
    s_pages[s_numPages++] = p;
    return 1;
}

int Cpag_Count(void)
{
    return s_numPages;
}

CpagPage* Cpag_Get(int index)
{
    return (index >= 0 && index < s_numPages) ? s_pages[index] : NULL;
}

void Cpag_FreeAll(void)
{
    for (int i = 0; i < s_numPages; i++)
    {
        Asset_Free(s_pages[i]->asset);
        Mem_Free(s_pages[i]);
    }
    s_numPages = 0;
}

void Cpag_Upload(CpagPage* p, int vramX, int vramY, int clutX, int clutY)
{
    const CpagLevel* l = &p->levels[0];
    RECT16 r;

    setRECT(&r, vramX, vramY, l->side * l->bpp / 16, l->side);
    LoadImage(&r, (u_long*)l->pixels);

    if (l->colors)
    {
        if (l->bpp == 4)
            setRECT(&r, clutX, clutY, 16, l->numColors / 16);
        else
            setRECT(&r, clutX, clutY, 256, l->numColors >= 256 ? l->numColors / 256 : 1);
        LoadImage(&r, (u_long*)l->colors);
    }
    DrawSync(0);

    p->uploaded = 1;
    p->vramX = vramX;
    p->vramY = vramY;
    p->clutX = clutX;
    p->clutY = clutY;
    p->tpage = getTPage(l->bpp == 4 ? 0 : 1, 0, vramX, vramY);
}

uint16_t Cpag_RegionClut(const CpagPage* p, const CpagRegion* r)
{
    const CpagLevel* l = &p->levels[0];
    if (l->bpp == 8)
        return getClut(p->clutX, p->clutY);
    int numPal = l->numColors / 16;
    int pal = (r && r->pal < numPal) ? r->pal : 0;
    return getClut(p->clutX, p->clutY + pal);
}
