/* Pantalla 1200: eleccion de piloto y coche (carrusel de miniaturas, clase, compañero de equipo y
 * rivales) y lo que necesita: atlas de miniaturas del Crle, tabla de pilotos y coches del garaje, que
 * llegan por secciones del FE.TRK (libreia Cpag al slot 9 + hueco y modelo Cobj) y se dibujan en 3D.
 * La sombra y los reflejos del coche los pone Render_DrawList (render.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "mem.h"
#include "gfx.h"
#include "pad.h"
#include "font.h"
#include "globals.h"
#include "frontend.h"
#include "fe_internal.h"
#include "asset.h"
#include "model.h"
#include "render.h"
#include "texslot.h"

/* 800aa5ec: nombre, dorsal, equipo (coche), y dos valores de pilotaje */
const DriverInfo g_Drivers[NUM_DRIVERS] = {
    { "Steve Park",            1,  0, 30, 20 },   /*  0 */
    { "Rusty Wallace",         2,  1, 40, 30 },   /*  1 */
    { "Dale Earnhardt",        3,  2, 45, 60 },   /*  2 */
    { "Bobby Hamilton",        4,  3, 30, 10 },   /*  3 */
    { "Terry Labonte",         5,  4, 40, 30 },   /*  4 */
    { "Mark Martin",           6,  5, 52, 20 },   /*  5 */
    { "Michael Waltrip",       7,  6, 25, 30 },   /*  6 */
    { "Dale Earnhardt Jr.",    8,  7, 35, 30 },   /*  7 */
    { "Ricky Rudd",           10,  8, 25, 20 },   /*  8 */
    { "Jeremy Mayfield",      12,  9, 35, 20 },   /*  9 */
    { "Kevin LePage",         16, 10, 25, 10 },   /* 10 */
    { "Bobby Labonte",        18, 11, 51, 30 },   /* 11 */
    { "Tony Stewart",         20, 12, 51, 20 },   /* 12 */
    { "Ward Burton",          22, 13, 45, 20 },   /* 13 */
    { "Jeff Gordon",          24, 14, 52, 20 },   /* 14 */
    { "Wally Dallenbach",     25, 15, 40, 10 },   /* 15 */
    { "Johnny Benson",        26, 16, 30, 10 },   /* 16 */
    { "Kenny Irwin Jr.",      28, 17, 30, 10 },   /* 17 */
    { "Mike Skinner",         31, 18, 40, 10 },   /* 18 */
    { "Ken Schrader",         33, 19, 35, 10 },   /* 19 */
    { "Ernie Irvan",          36, 20, 25, 10 },   /* 20 */
    { "Sterling Marlin",      40, 21, 35, 10 },   /* 21 */
    { "Joe Nemechek",         42, 22, 25, 20 },   /* 22 */
    { "John Andretti",        43, 23, 35, 10 },   /* 23 */
    { "Kyle Petty",           44, 24, 25, 10 },   /* 24 */
    { "Adam Petty",           45, 25, 20, 10 },   /* 25 */
    { "Kenny Wallace",        55, 26, 32, 20 },   /* 26 */
    { "Geoff Bodine",         60, 27, 30, 20 },   /* 27 */
    { "Darrell Waltrip",      66, 28, 25, 30 },   /* 28 */
    { "Dale Jarrett",         88, 29, 54, 20 },   /* 29 */
    { "Bill Elliott",         94, 30, 30, 20 },   /* 30 */
    { "Chad Little",          97, 31, 20, 10 },   /* 31 */
    { "Jeff Burton",          99, 32, 48, 20 },   /* 32 */
    { "Mike Wallace",          2, 37, 30, 10 },   /* 33 */
    { "Rick Carelli",          6, 38, 50, 20 },   /* 34 */
    { "Ron Hornaday",         16, 34, 45, 10 },   /* 35 */
    { "Jack Sprague",         24, 33, 40, 10 },   /* 36 */
    { "Scott Hansen",         52, 39, 48, 30 },   /* 37 */
    { "Stacy Compton",        86, 36, 30, 10 },   /* 38 */
    { "Alan Kulwicki",         7, 40, 57, 20 },   /* 39 */
    { "Cale Yarborough",      11, 43, 62, 30 },   /* 40 */
    { "David Pearson",        17, 45, 61, 20 },   /* 41 */
    { "Bobby Allison",        22, 42, 58, 30 },   /* 42 */
    { "Davey Allison",        28, 44, 59, 30 },   /* 43 */
    { "Richard Petty",        43, 46, 63, 40 },   /* 44 */
    { "Benny Parsons",        72, 41, 60, 20 },   /* 45 */
    { "EA Sports Car",       153, 56, 58, 45 },   /* 46 */
    { "Loader",              198, 48, 10, 50 },   /* 47 */
    { "Sprint",              209, 47, 35, 10 },   /* 48 */
    { "Jet Car",             299, 49, 25, 10 },   /* 49 */
    { "Tow Truck",           357, 50, 20, 60 },   /* 50 */
    { "Chicken Truck",       455, 51, 20, 20 },   /* 51 */
    { "RV",                  528, 52, 20, 30 },   /* 52 */
    { "Golf Cart",           574, 53, 30, 40 },   /* 53 */
    { "Hot Rod",             609, 54, 40, 45 },   /* 54 */
    { "Road Captain",        707, 55, 55, 45 },   /* 55 */
};

/* ---------------------------------------------------------------- atlas de miniaturas */

/* 8009307c: RLE por bits (MSB primero, palabras de 32 bits). Cabecera de 4 bits: n = (h & 7) + 1;
 * con el bit 3 un valor repetido n veces, sin el n valores literales. Las filas se recorren en
 * zigzag (la primera de izquierda a derecha, la siguiente al reves...). */
static void Rle_Decode(const uint8_t* src, uint8_t* dst, int w, int h, int bits)
{
    uint32_t cur, next;
    memcpy(&cur, src, 4);
    src += 4;
    int avail = 32;
    int header = 1, count = 0, run = 0;
    int written = 0, rowEnd = w, step = 1;
    uint8_t* out = dst;

    while (written < w * h)
    {
        int n = header ? 4 : bits;
        uint32_t v;
        if (avail < n)
        {
            v = avail ? cur >> (32 - n) : 0;
            memcpy(&next, src, 4);
            src += 4;
            int rem = n - avail;
            v |= next >> (32 - rem);
            cur = rem < 32 ? next << rem : 0;
            avail = 32 - rem;
        }
        else
        {
            v = cur >> (32 - n);
            cur = n < 32 ? cur << n : 0;
            avail -= n;
        }

        if (header)
        {
            count = (int)(v & 7) + 1;
            run = (v & 8) != 0;
            header = 0;
            continue;
        }
        do
        {
            if (written == rowEnd)
            {
                rowEnd = written + w;
                out += step > 0 ? w - 1 : w + 1;
                step = -step;
            }
            written++;
            *out = (uint8_t)v;
            out += step;
            count--;
        } while (run && count > 0 && written < w * h);
        if (count < 1)
            header = 1;
    }
}

/* 8008e2ec: decodifica la imagen 'image' del atlas en (x, y) de VRAM (8 bits) y su paleta en
 * (0x200, 0xF0 + slot) con el color 0 sustituido por (r, g, b). Devuelve el id de CLUT.
 * Atlas: u16 numero de imagenes, u8 bits por pixel, paletas en +0xC (1 << bits colores cada una),
 * despues 4 bytes por imagen (u16 desplazamiento en palabras, u8 ancho-1, u8 alto-1) y los datos. */
int Fe_DecodeThumb(int r, int g, int b, const uint8_t* atlas, int image, int slot, int x, int y)
{
    int count = NR_Read16(atlas);
    int bits = atlas[2] & 0x1f;
    const uint8_t* palettes = atlas + 0xc;
    const uint8_t* entries = palettes + (count << bits) * 2;
    const uint8_t* pixels = entries + count * 4;
    if (image < 0 || image >= count)
        return 0;
    const uint8_t* e = entries + image * 4;
    int w = e[2] + 1, h = e[3] + 1;

    uint8_t* buf = (uint8_t*)Pool_Alloc(w * h, 1);
    Rle_Decode(pixels + NR_Read16(e) * 4, buf, w, h, bits);
    RECT16 rc;
    setRECT(&rc, x, y, w >> 1, h);
    LoadImage(&rc, (u_long*)buf);
    DrawSync(0);
    Pool_Free(buf);

    uint16_t pal[256];
    memcpy(pal, palettes + (image << bits) * 2, (size_t)(1 << bits) * 2);
    pal[0] = (uint16_t)(b << 10 | g << 5 | r);
    setRECT(&rc, 0x200, 0xf0 + slot, 1 << bits, 1);
    LoadImage(&rc, (u_long*)pal);
    DrawSync(0);
    return getClut(0x200, 0xf0 + slot);
}

/* 8008e0c0: miniatura de 64x20 del coche en el hueco 'slot' */
static void Fe_LoadCarThumb(int image, int slot)
{
    FrontendState* fe = g_Frontend;
    if (!fe->atlas || slot < 0 || slot >= 8)
        return;
    fe->thumbClut[slot] = Fe_DecodeThumb(0x10, 0x11, 0x11, fe->atlas, image, slot, 0x200, slot * 0x14);
}

static int CarImage(int driver, int car)
{
    return g_Frontend->carInfo[driver][car][2];
}

/* 8008b9d4: miniatura del carrusel, recortada a la ventana del panel si 'clip' */
static void Fe_DrawCarThumb(int slot, int x, int y, int depth, int clip)
{
    FrontendState* fe = g_Frontend;
    if (slot < 0)
        return;
    int lo, hi;
    if (fe->numPanels == 1)
    {
        lo = 0x1b;
        hi = 0xe3;
    }
    else
    {
        lo = 0x6a;
        hi = 0x123;
    }
    int x1 = x + 0x40, lineX0 = x;
    if (clip)
    {
        if (x1 < lo || x > hi)
            return;
        lineX0 = lo;
        if (x >= lo)
        {
            lineX0 = x;
            if (x1 > hi)
                x1 = hi;
        }
    }

    DR_TPAGE* tp = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    SPRT* s = (SPRT*)Gfx_AllocPrim(sizeof(SPRT));
    if (!tp || !s)
        return;
    setDrawTPage(tp, 1, 1, getTPage(1, 0, 0x200, 0));   /* 0xE1000688: 8 bits en (512, 0), con dither */
    Gfx_AddPrimDepth(depth, tp);

    setSprt(s);
    setRGB0(s, 0x77, 0x77, 0x77);
    s->clut = (u_short)fe->thumbClut[slot];
    int u = 0, w = 0x40, sx = x;
    if (clip && x + 0x40 > hi)
        w = hi - x;
    else if (clip && x < lo)
    {
        u = lo - x;
        w = 0x40 - u;
        sx = lo;
    }
    setXY0(s, sx, y);
    setUV0(s, u, slot * 0x14);
    setWH(s, w, 0x14);
    Gfx_AddPrimDepth(depth, s);

    Fe_DrawLine(0x28888880, lineX0, y + 0x14, x1, y + 0x14, 2);
}

/* 8008bc34: miniaturas first..last del carrusel del jugador p */
static void Fe_DrawCarousel(int p, int first, int last, int scrolled)
{
    FrontendState* fe = g_Frontend;
    int x, y;
    if (fe->numPanels == 1)
    {
        x = first * 0x44 + 0x1b;
        y = 0x1b;
    }
    else
    {
        x = first * 0x44 + 0x62;
        y = p * 0x6a + 0x19;
    }
    if (scrolled)
        x += fe->scrollX[p];
    for (int i = first; i <= last; i++, x += 0x44)
        Fe_DrawCarThumb(fe->carousel[p][i].slot, x, y, 0, 1);
}

/* ---------------------------------------------------------------- coches del garaje */

/* 80090674: el coche (piloto, clase) se puede elegir */
int Fe_CarAvailable(int driver, int car)
{
    if (car > g_Settings.maxClassUnlocked)
        return 0;
    if (driver < 0x27)
        return 1;
    if (driver < 0x2e)
    {
        int v = PROGRESS(0x800b70c5u + driver * 3 + car);       /* copa de la sede ganada en esa clase */
        if (v == 2)
            return 1;
        if (g_Frontend->screen == 1200)
            return 0;
        return v != 0;
    }
    if (driver > 0x37)
        return 0;
    return PROGRESS(0x800b7121u + driver) == 1;
}

/* 80023e88: empieza a cargar por streaming la seccion del coche (una a la vez; rscE la cierra) */
static int Car_StartLoad(int section)
{
    return Stream_StartSection(section, 0);
}

/* id del Cobj cargado (10000 + (equipo+1)*100 + clase*10 + 1) -> equipo*3 + clase */
static int CarIdIndex(int id)
{
    unsigned u = (unsigned)(id - 10000);
    return (int)(u / 10 - (u / 100) * 7 - 3);
}

/* el panel p deja de usar los huecos distintos de 'keep' (los libera si nadie mas los usa) */
static void ReleaseOtherSlots(int p, int keep)
{
    FrontendState* fe = g_Frontend;
    for (int i = 0; i < 3; i++)
    {
        CarSlot* s = &fe->carSlots[i];
        if (i != keep && s->users[p] && (s->inUse || s->state))
        {
            s->users[p] = 0;
            if (!s->users[0] && !s->users[1])
            {
                s->inUse = 0;
                s->state = 0;
            }
        }
    }
}

/* 800923cc: el panel p quiere mostrar (driver, car): reutiliza un coche ya cargado o pide la carga
 * en el hueco libre usado hace mas tiempo */
void Fe_RequestCar(int p, int driver, int car)
{
    FrontendState* fe = g_Frontend;
    if (!Fe_CarAvailable(fe->driver[p], fe->car[p]))
        fe->modelAnim[p] = -1;

    int want = g_Drivers[driver].team * 3 + car, found = -1;
    for (int i = 0; i < 3; i++)
    {
        CarSlot* s = &fe->carSlots[i];
        if (s->state == 0 && s->data && CarIdIndex(((NrAsset*)s->data)->id) == want)
        {
            found = i;
            break;
        }
    }

    if (found < 0)
    {
        for (int i = 0; i < 3; i++)
        {
            CarSlot* s = &fe->carSlots[i];
            if (s->state != 0 && !s->inUse && s->users[p])
            {
                s->driver = (uint8_t)driver;   /* ya pedido por este panel: cambia de coche */
                s->car = (uint8_t)car;
                s->state = 1;
                return;
            }
        }
        int freeSlots[3], numFree = 0;
        for (int i = 0; i < 3; i++)
            if (fe->carSlots[i].state == 0 && !fe->carSlots[i].inUse)
                freeSlots[numFree++] = i;
        if (numFree == 0)
        {
            fe->pendingDriver = (uint8_t)driver;
            fe->pendingCar = (uint8_t)car;
            fe->pendingPlayer = (char)p;
            fe->pendingLoad = 1;
        }
        else
        {
            int best = 0;
            for (int i = 1; i < numFree; i++)
                if (fe->carSlots[freeSlots[i]].lastUsed < fe->carSlots[freeSlots[best]].lastUsed)
                    best = i;
            CarSlot* s = &fe->carSlots[freeSlots[best]];
            s->users[0] = s->users[1] = 0;
            s->users[p] = 1;
            s->driver = (uint8_t)driver;
            s->car = (uint8_t)car;
            s->state = 1;
        }
        fe->modelAnim[p] = -1;
        return;
    }

    ReleaseOtherSlots(p, found);
    CarSlot* s = &fe->carSlots[found];
    fe->carModel[p] = s->model;
    s->users[p] = 1;
    s->inUse = 1;
    s->lastUsed = g_MenuFrame;
    if (Fe_CarAvailable(fe->driver[p], fe->car[p]) == 1)
        fe->modelAnim[p] = fe->modelAnim[p] != -1 ? 1 : -2;
}

static int s_fadeBusy;   /* DAT_800af388 */

int Fe_IsFading(void)
{
    return s_fadeBusy;
}

static int GarageScreen(void)
{
    short s = g_Frontend ? g_Frontend->screen : 0;
    return s == 1200 || s == 1700 || s == 1800;
}

/* 80092938 */
int Fe_CarReceiving(void)
{
    if (!GarageScreen())
        return 0;
    for (int i = 0; i < 3; i++)
        if (g_Frontend->carSlots[i].receiving)
            return 1;
    return 0;
}

/* 80092894: al llegar la libreia, el hueco que esta cargando pasa a recibir la seccion */
int Fe_MarkReceiving(void)
{
    int found = 0;
    if (!GarageScreen())
        return 0;
    for (int i = 0; i < 3; i++)
    {
        CarSlot* s = &g_Frontend->carSlots[i];
        if (s->state == 2 && !s->inUse && !found)
        {
            s->receiving = 1;
            found = 1;
        }
        else
            s->receiving = 0;
    }
    return found;
}

/* 80092998: el modelo del coche ha llegado; pasa a su hueco y a los paneles que lo esperaban */
int Fe_OnCarModel(NrAsset* a, int id)
{
    FrontendState* fe = g_Frontend;
    int found = -1;
    if (!a || id < 30000)
        return 0;
    if (GarageScreen() && a->obj)
    {
        for (int k = 0; k < 3; k++)
        {
            CarSlot* s = &fe->carSlots[k];
            if (!s->receiving)
            {
                if (s->state == 2)
                    s->state = 0;
                continue;
            }
            found = k;
            if (s->data)
                Asset_Free((NrAsset*)s->data);
            s->data = a;
            ModelHdr* m = (ModelHdr*)a->obj;
            unsigned u = (unsigned)(a->id - 10000);
            m->tag = (u / 100 - 1) << 4 | (u / 10 - (u / 100) * 10);   /* equipo << 4 | clase */
            GfxObj* o = (GfxObj*)s->model;
            Obj_Init(o, m);
            o->flags |= 0x80;
            for (int p = 0; p < g_Settings.numPlayers; p++)
            {
                if (!s->users[p])
                    continue;
                fe->carModel[p] = o;
                ReleaseOtherSlots(p, k);
                if (Fe_CarAvailable(fe->driver[p], fe->car[p]) == 1)
                    fe->modelAnim[p] = fe->modelAnim[p] == -1 ? -2 : 1;
            }
            s->inUse = 1;
            s->state = 0;
            s->lastUsed = g_MenuFrame;
            break;
        }
    }
    if (fe->pendingLoad)
    {
        fe->pendingLoad = 0;
        Fe_RequestCar(fe->pendingPlayer, fe->pendingDriver, fe->pendingCar);
    }
    return found >= 0;
}

/* 80090a88(1): vacia los huecos de coches */
void Fe_ResetCars(void)
{
    FrontendState* fe = g_Frontend;
    for (int i = 0; i < 3; i++)
    {
        CarSlot* s = &fe->carSlots[i];
        if (s->data)
        {
            Asset_Free((NrAsset*)s->data);
            s->data = NULL;
        }
        s->state = 0;
        s->inUse = 0;
        s->receiving = 0;
        s->users[0] = s->users[1] = 0;
    }
    for (int p = 0; p < 2; p++)
    {
        fe->modelOffset[p] = 15000;
        fe->modelAnim[p] = 0;
        fe->carModel[p] = NULL;
    }
}

/* 80090a88(0): lanza la siguiente carga pendiente y mueve los coches que entran o salen */
void Fe_UpdateCars(void)
{
    FrontendState* fe = g_Frontend;
    s_fadeBusy = fe->fadeDone != 0;
    int loading = 0;
    for (int i = 0; i < 3; i++)
        if (fe->carSlots[i].state == 2 && !fe->carSlots[i].inUse)
            loading = 1;
    if (!loading)
    {
        for (int i = 0; i < 3; i++)
        {
            CarSlot* s = &fe->carSlots[i];
            if (s->state != 0 && !s->inUse)
            {
                int prev = g_FeLoadingSlot;
                g_FeLoadingSlot = i;
                if (Car_StartLoad(fe->carInfo[s->driver][s->car][1]))
                    s->state = 2;
                else
                    g_FeLoadingSlot = prev;
                break;
            }
        }
    }
    for (int p = 0; p < 2; p++)
    {
        if (!fe->modelAnim[p])
            continue;
        if (fe->modelAnim[p] > 0 && fe->modelOffset[p] == 15000)
            Fe_PlaySound(0x53);
        fe->modelOffset[p] += fe->modelAnim[p] < 1 ? g_FrameDelta * 0x140 : -g_FrameDelta * 0xa0;
        if (fe->modelOffset[p] < 0)
        {
            fe->modelOffset[p] = 0;
            fe->modelAnim[p] = 0;
        }
        else if (fe->modelOffset[p] > 15000)
        {
            fe->modelOffset[p] = 15000;
            fe->modelAnim[p] = fe->modelAnim[p] == -2 ? 1 : 0;
        }
    }
}

/* ---------------------------------------------------------------- coche 3D */

static GfxObj* NewGarageObject(int cobjId)
{
    NrAsset* a = Asset_Find(ASSET_Cobj, cobjId);
    GfxObj* o = (GfxObj*)Pool_Alloc(sizeof(GfxObj), 2);
    Obj_Init(o, a ? (ModelHdr*)a->obj : NULL);
    return o;
}

/* parte de 80079814: ruedas (Cobj 0, 1, 2; cuatro de cada), huecos de coches y candados (Cobj 3, 4) */
void Fe_CreateGarageObjects(void)
{
    FrontendState* fe = g_Frontend;
    for (int k = 0; k < 4; k++)
    {
        fe->garageObjA[k] = NewGarageObject(0);
        fe->garageObjB[k] = NewGarageObject(1);
        fe->garageObjC[k] = NewGarageObject(2);
    }
    for (int i = 0; i < 3; i++)
    {
        CarSlot* s = &fe->carSlots[i];
        memset(s, 0, sizeof(*s));
        s->model = Pool_Alloc(sizeof(GfxObj), 2);
        Obj_Init((GfxObj*)s->model, NULL);
    }
    fe->carModel[0] = fe->carModel[1] = NULL;
    for (int i = 0; i < 2; i++)
    {
        GfxObj* o = NewGarageObject(3 + i);
        o->flags |= 0x80;
        fe->lockModel[i] = o;
    }
    for (int i = 0; i < 12; i++)
    {
        GfxObj* o = NewGarageObject(5 + i);
        o->flags |= 0x80;
        fe->trophies[i] = o;
    }
}

void Fe_FreeGarageObjects(void)
{
    FrontendState* fe = g_Frontend;
    if (!fe)
        return;
    for (int k = 0; k < 4; k++)
    {
        Pool_Free(fe->garageObjA[k]);
        Pool_Free(fe->garageObjB[k]);
        Pool_Free(fe->garageObjC[k]);
        fe->garageObjA[k] = fe->garageObjB[k] = fe->garageObjC[k] = NULL;
    }
    for (int i = 0; i < 3; i++)
    {
        if (fe->carSlots[i].data)
            Asset_Free((NrAsset*)fe->carSlots[i].data);
        Pool_Free(fe->carSlots[i].model);
        memset(&fe->carSlots[i], 0, sizeof(fe->carSlots[i]));
    }
    for (int i = 0; i < 12; i++)
    {
        Pool_Free(fe->trophies[i]);
        fe->trophies[i] = NULL;
    }
    for (int i = 0; i < 2; i++)
    {
        Pool_Free(fe->lockModel[i]);
        fe->lockModel[i] = NULL;
        fe->carModel[i] = NULL;
    }
}

/* 8008c9a4: foco en el suelo bajo el coche (abanico de 64 triangulos aditivos) */
static void Fe_DrawFloorGlow(void)
{
    DR_TPAGE* tp = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    if (!tp)
        return;
    setDrawTPage(tp, 1, 1, getTPage(0, 1, 0, 0));   /* 0xE1000620: mezcla 1 (suma) */
    Gfx_AddPrimDepth(0xa, tp);

    static const int ident[9] = { 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x10000 };
    static const int pos[3] = { 0, -0x109, 0x1e14 };
    Render_LoadObjectMatrix16(ident, pos);
    SVECTOR v = { 0, 0, 0, 0 };
    int cx, cy, px, py;
    Render_ProjectPoint(&v, &cx, &cy);
    v.vz = 0x40;
    Render_ProjectPoint(&v, &px, &py);
    for (int i = 0; i < 0x41; i++)
    {
        int s, c, x, y;
        Math_SinCos(i << 6, &s, &c);
        v.vx = (short)(s * 3 >> 10);
        v.vz = (short)(c * 3 >> 10);
        Render_ProjectPoint(&v, &x, &y);
        POLY_G3* p = (POLY_G3*)Gfx_AllocPrim(sizeof(POLY_G3));
        if (!p)
            return;
        setPolyG3(p);
        setSemiTrans(p, 1);
        setRGB0(p, 0x5a, 0x5a, 0x64);
        setRGB1(p, 0, 0, 0);
        setRGB2(p, 0, 0, 0);
        setXY3(p, cx, cy, px, py, x, y);
        Gfx_AddPrimDepth(0xa, p);
        px = x;
        py = y;
    }
}

static void NegateY(int* rot)
{
    rot[3] = -rot[3];
    rot[4] = -rot[4];
    rot[5] = -rot[5];
}

/* 8008bd58: coche del panel p girando, centrado en (x, y) de pantalla, a 'dist' de la camara. Con
 * 'reflection' dibuja antes su reflejo invertido bajo el suelo y el foco, y lo vuelca (Gfx_Flip(0)). */
void Fe_DrawCarModel(int p, int x, int y, int dist, int reflection)
{
    FrontendState* fe = g_Frontend;
    GfxObj* car = (GfxObj*)fe->carModel[p];
    if (!car || !car->model)
        return;

    int lift = fe->modelOffset[p] / 0xdac;
    if (lift > 8)
        lift = 8;
    if (lift < -8)
        lift = -8;
    SetGeomOffset(x, y - lift);
    int floorY = 0;
    if (!reflection)
        car->flags &= ~0x10;

    /* juego de ruedas: carrito de golf (equipo 0x35) con ruedas A/B; Elite C; el resto B */
    uint32_t ident = car->model->tag;
    void** front = fe->garageObjB;
    void** rear = fe->garageObjB;
    if ((ident >> 4) == 0x35 && (ident & 0xf) < 2)
    {
        front = fe->garageObjA;
        rear = (ident & 0xf) == 0 ? fe->garageObjA : fe->garageObjB;
    }
    else if ((ident >> 4) != 0x35 && (ident & 0xf) == 2)
        front = rear = fe->garageObjC;

    car->shadowY = 0;
    car->pos[0] = 0;
    car->pos[1] = 0;
    car->pos[2] = 0x1d4c;
    int eye[3] = { 0, 0x9c4, -fe->modelOffset[p] };
    Camera_LookAt(eye, car->pos);

    for (;;)
    {
        int angle = fe->modelAngle & 0xfff;
        Math_RotMatrix(angle, 0, 0, car->rot);
        if (reflection)
        {
            int hp[3];
            Obj_GetLocalPoint(car, 0, hp);
            GfxObj* w0 = (GfxObj*)front[0];
            int wheelBottom = w0 && w0->model ? *(int16_t*)(w0->model->bbox + 0x10) : 0;
            NegateY(car->rot);
            floorY = wheelBottom - hp[1] + 0x40;
            car->flags &= ~0x10;
        }
        car->pos[0] = 0;
        car->pos[1] = floorY * -2;
        car->pos[2] = dist;

        Render_ClearList();
        for (int i = 0; i < 4; i++)
        {
            GfxObj* w = (GfxObj*)(i < 2 ? front : rear)[i];
            if (!w)
                continue;
            Math_RotMatrix(angle + ((i & 1) ? 0x800 : 0), 0, 0, w->rot);
            if (reflection)
                NegateY(w->rot);
            Obj_GetPoint(car, i, w->pos);
            for (int a = 0; a < 3; a++)
                w->pos[a] += (int)(((int64_t)w->rot[3 + a] * -0x40) >> 16);
            Render_Add(w);
        }
        Render_Add(car);
        Light* l = car->light;
        l->bk[0] = l->bk[1] = l->bk[2] = reflection ? 0x40 : 0x600;
        if (!reflection)
            break;

        static const int under[3] = { 0, -0x1000000, -0x800000 };
        Light_SetSun(under, 0x40, 0x40, 0x40);
        Render_DrawList(1);
        reflection = 0;
        SetGeomOffset(x, y);
        Fe_DrawFloorGlow();
        Gfx_FlushPartial();
        SetGeomOffset(x, y - lift);
        car->flags |= 0x10;
        car->shadowY = (short)(-floorY >> 4);
        floorY = 0;
    }
    static const int sun[3] = { 0x2000000, 0x1000000, 0x2000000 };
    Light_SetSun(sun, 0x80, 0x80, 0x80);
    Render_DrawList(0);
    car->flags |= 0x10;
    SetGeomOffset(160, 120);
}

/* 8008de60: modelo pequeño girando (3/4 = candados) en (x, y) de pantalla */
void Fe_DrawSpinningModel(int x, int y, int dist, int model, int speed)
{
    FrontendState* fe = g_Frontend;
    GfxObj* o;
    switch (model)
    {
    case 3: o = (GfxObj*)fe->lockModel[0]; break;
    case 4: o = (GfxObj*)fe->lockModel[1]; break;
    default:
        if (model < 5 || model > 16)
            return;
        o = (GfxObj*)fe->trophies[model - 5];   /* copas (fe+0xA48) */
        break;
    }
    if (!o || !o->model)
        return;
    int a = (g_Ticks * speed) % 0x1000;
    SetGeomOffset(x, y);
    Math_RotMatrix(a, 0, 0, o->rot);
    o->pos[0] = 0;
    o->pos[1] = 0;
    o->pos[2] = dist;
    int eye[3] = { 0, model >= 11 ? -10 : 0x9c4, 500 };
    Camera_LookAt(eye, o->pos);
    Render_ClearList();
    Render_Add(o);
    Render_DrawList(0);
    SetGeomOffset(160, 120);
}

/* ---------------------------------------------------------------- seleccion */

/* 8008aee4: con (-1, -1) guarda la eleccion actual del panel p; si no, pone piloto y clase,
 * rellena el carrusel (anterior, actual, siguiente) y pide el coche */
static void Fe_SetDriver(int p, int driver, int car)
{
    FrontendState* fe = g_Frontend;
    if (driver == -1 && car == -1)
    {
        if (fe->stage[p] == 0)
        {
            g_Settings.lastDriver[p] = fe->driver[p];
            g_Settings.lastCar[p] = fe->car[p];
        }
        else if (fe->stage[p] == -1)
        {
            g_Settings.mateDriver[p] = fe->driver[p];
            g_Settings.mateCar[p] = fe->car[p];
        }
        else if ((uint8_t)(fe->stage[0] - 1) < 5)
        {
            g_Settings.oppDriver[(int)fe->stage[0]] = fe->driver[0];
            g_Settings.oppCar[(int)fe->stage[0]] = fe->car[0];
        }
        return;
    }
    if (fe->scrollDir[p] != 0)
        return;
    fe->driver[p] = (uint8_t)driver;
    fe->car[p] = (uint8_t)car;
    CarouselEntry* c = fe->carousel[p];
    c[0].driver = (uint8_t)(driver == 0 ? 0x37 : driver - 1);
    c[1].driver = (uint8_t)driver;
    c[2].driver = (uint8_t)(driver == 0x37 ? 0 : driver + 1);
    c[3].driver = 0;
    for (int i = 0; i < 4; i++)
    {
        c[i].car = (uint8_t)car;
        if (fe->firstFrame)
            c[i].slot = (int8_t)(i + p * 4);
        Fe_LoadCarThumb(CarImage(c[i].driver, c[i].car), c[i].slot);
    }
    Fe_RequestCar(p, driver, car);
}

static int NextDriver2(int d) { return d > 0x35 ? d - 0x36 : d + 2; }
static int PrevDriver2(int d) { return d < 2 ? d + 0x36 : d - 2; }

/* el carrusel avanza: la miniatura nueva entra por la derecha */
static void Carousel_ShiftLeft(int p)
{
    CarouselEntry* c = g_Frontend->carousel[p];
    int8_t slot = c[0].slot;
    c[0] = c[1];
    c[1] = c[2];
    c[2] = c[3];
    c[3].slot = slot;
}

/* el carrusel retrocede: la nueva (piloto - 2) entra por la izquierda */
static void Carousel_ShiftRight(int p)
{
    FrontendState* fe = g_Frontend;
    CarouselEntry* c = fe->carousel[p];
    int8_t slot = c[3].slot;
    c[3] = c[2];
    c[2] = c[1];
    c[1] = c[0];
    c[0].slot = slot;
    c[0].car = fe->car[p];
    c[0].driver = (uint8_t)PrevDriver2(fe->driver[p]);
    Fe_LoadCarThumb(CarImage(c[0].driver, c[0].car), c[0].slot);
}

/* animacion del carrusel del panel p y su dibujo (parte de 80081a9c) */
static void Fe_ScrollCarousel(int p)
{
    FrontendState* fe = g_Frontend;
    CarouselEntry* c = fe->carousel[p];
    int dir = fe->scrollDir[p];

    if (dir == 0)
    {
        Fe_DrawCarousel(p, 0, 2, 0);
        return;
    }
    if (dir < 0)
    {
        if (fe->scrollX[p] > -0x44)
        {
            fe->scrollX[p] = (short)(fe->scrollX[p] + dir);
            if (fe->scrollX[p] >= -0x43)
            {
                Fe_DrawCarousel(p, 0, 3, 1);
                return;
            }
            if (!Fe_PadPressed(PADB_RIGHT, p, 0))
            {
                fe->scrollX[p] = -0x44;
                Fe_DrawCarousel(p, 0, 3, 1);
                return;
            }
            fe->scrollAgain[p] = 1;
        }
        Carousel_ShiftLeft(p);
        fe->driver[p] = c[1].driver;
        if (!fe->scrollAgain[p])
            Fe_RequestCar(p, c[1].driver, fe->car[p]);
        if (fe->scrollAgain[p])
        {
            fe->scrollX[p] = (short)(fe->scrollX[p] + 0x44);
            c[3].car = fe->car[p];
            c[3].driver = (uint8_t)NextDriver2(fe->driver[p]);
            Fe_LoadCarThumb(CarImage(c[3].driver, c[3].car), c[3].slot);
            Fe_DrawCarousel(p, 0, 3, 1);
            return;
        }
    }
    else
    {
        if (fe->scrollX[p] < 0)
        {
            fe->scrollX[p] = (short)(fe->scrollX[p] + dir);
            if (fe->scrollX[p] < 0)
            {
                Fe_DrawCarousel(p, 0, 3, 1);
                return;
            }
            if (!Fe_PadPressed(PADB_LEFT, p, 0))
            {
                fe->scrollX[p] = 0;
                Fe_DrawCarousel(p, 0, 3, 1);
                return;
            }
            fe->scrollAgain[p] = 1;
        }
        fe->driver[p] = c[1].driver;
        if (!fe->scrollAgain[p])
            Fe_RequestCar(p, c[1].driver, fe->car[p]);
        if (fe->scrollAgain[p])
        {
            fe->scrollX[p] = (short)(fe->scrollX[p] - 0x44);
            Carousel_ShiftRight(p);
            Fe_DrawCarousel(p, 0, 3, 1);
            return;
        }
    }
    Fe_DrawCarousel(p, 0, 2, 0);
    fe->scrollDir[p] = 0;
}

/* 8008dc1c: marco (x, y, w, h) con una flecha que baja 'down', va a la izquierda 'left', baja 'down2'
 * y apunta a la derecha 'right' (une la miniatura elegida con la clase) */
void Fe_DrawCallout(int x, int y, int w, int h, int down, int left, int down2, int right)
{
    int r, g, b;
    Fe_ItemColor(&r, &g, &b, 2);
    r <<= 3; g <<= 3; b <<= 3;
    int y1 = y + h;
    Fe_DrawFrame(r, g, b, x, y, x + w, y1);
    if (!down)
        return;
    uint32_t c = Rgb(r, g, b);
    int mx = x + w / 2, yd = y1 + down;
    Fe_DrawLine(c, mx, y1, mx, yd, 1);
    Fe_DrawLine(c, mx - 1, yd - 1, mx, yd - 1, 1);
    int lx = mx - left;
    Fe_DrawLine(c, lx + 1, yd, mx, yd, 1);
    Fe_DrawLine(c, lx + 1, yd + 1, lx + 2, yd + 1, 1);
    int yb = yd + down2;
    Fe_DrawLine(c, lx, yd + 1, lx, yb, 1);
    int ax = lx + right;
    Fe_DrawLine(c, lx + 1, yb, ax, yb, 1);
    Fe_DrawLine(c, lx + 1, yb - 1, lx + 2, yb - 1, 1);
    Fe_DrawLine(c, ax, yb - 3, ax, yb + 3, 1);
    Fe_DrawLine(c, ax + 1, yb - 2, ax + 1, yb + 2, 1);
    Fe_DrawLine(c, ax + 2, yb - 1, ax + 2, yb + 1, 1);
}

/* 8008efec: texto en color RGB555 directo; align 1 centrado, 2 a la derecha */
int Fe_DrawTextRgb(int r, int g, int b, const char* text, int x, int y, int align)
{
    int w, h;
    Font_Measure(text, &w, &h);
    if (align == 1)
        x -= w / 2;
    else if (align == 2)
        x -= w;
    Font_DrawString(text, x, y, (unsigned)(r << 10 | g << 5 | b | 0x8000));
    return w;
}

const char* HelpText(int i)
{
    return Fe_GetString(3000 + i);   /* DAT_800b6e78[i] */
}

/* primer hueco de la parrilla para los rivales y compañeros (detras de los jugadores) */
static int OpponentBase(void)
{
    return g_Frontend->teamMode ? g_Settings.numPlayers * 2 : g_Settings.numPlayers;
}

static void DrawPanel1P(int r, int g, int b)
{
    FrontendState* fe = g_Frontend;
    char buf[64];
    int k = fe->car[0] * 0x18, w, h;
    int d = fe->carousel[0][1].driver;

    Font_Select(3);
    snprintf(buf, sizeof(buf), "%i", g_Drivers[d].number);
    Font_Measure(buf, &w, &h);
    Font_DrawString(buf, 0xae - w, 0x36, 8);
    Font_Select(2);
    Font_DrawString(g_Drivers[d].name, 0xb4, 0x3c, 8);
    Fe_DrawCallout(0x5f, 0x1a, 0x3f, 0x17, 0xc, 0x74, k + 0x10, 8);
    Fe_DrawFrame(r / 2 + 0x52, g / 2 + 0x52, b / 2 + 0x5c, 0x14, k + 0x44, 0x58, k + 0x54);

    if (fe->stage[0] != 100)
    {
        int o = fe->modelOffset[0];
        Fe_DrawCarModel(0, 200 - o / 300, 0x90 - o / 0x226, 0x1e14, g_FadeDir == 0);
        if (!Fe_CarAvailable(fe->driver[0], fe->car[0]) && o == 15000)
            Fe_DrawSpinningModel(0xfa, 0xa0, 30000, 3, 9);
    }

    int help = -1;
    if (fe->stage[0] == 0)
    {
        Fe_DrawBox(0x5a, 10, 10, 0x112, 0xcd, 0x12d, 0xdc, 1);
        Fe_DrawFrame(0x8c, 0x8c, 0x96, 0x112, 0xcd, 0x12d, 0xdc);
        Font_Select(4);
        Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, Fe_GetString(g_Settings.transmission[0] < 3 ? 0xb54 : 0xb55), 0x120, 0xcd, 1);
        help = 0x11;
    }
    else if (fe->stage[0] != 100)
        help = 0x12;
    if (help != -1)
        Fe_DrawHelp(0, HelpText(help), 0x21, 0xd5, 0x23, help == 0x12 ? 300 : 0x109, 1);

    if (fe->stage[0] == -1 && g_FadeDir != -1)
    {
        Fe_DrawBox(0x19, 0x41, 0, 0x16, 0xae, 0x52, 0xbd, 0);
        Fe_DrawFrame(r, g, b, 0x15, 0xae, 0x52, 0xbd);
        Font_Select(1);
        Font_DrawString(Fe_GetString(0xb56), 0x1a, 0xaa, 0x11);          /* Teammate */
    }
    else if ((uint8_t)(fe->stage[0] - 1) < 5)
    {
        Fe_DrawBox(0x19, 0x41, 0, 0x16, 0xae, 0x5b, 0xbd, 0);
        Fe_DrawFrame(r, g, b, 0x15, 0xae, 0x5b, 0xbd);
        Font_Select(1);
        snprintf(buf, sizeof(buf), "%s %ld", Fe_GetString(0xb57), (long)fe->stage[0]);   /* Opponent N */
        Font_DrawString(buf, 0x19, 0xaa, 0x11);
        Fe_DrawHelp(0, HelpText(0x13), 0x16, 0xbf, 0, 0, 0);             /* Skip Rest */
    }
}

static void DrawPanels2P(int r, int g, int b)
{
    FrontendState* fe = g_Frontend;
    char buf[64];
    for (int i = 0; i < 2; i++)
    {
        int off = i * 0x6a, w, h;
        int d = fe->carousel[i][1].driver;
        Font_Select(3);
        snprintf(buf, sizeof(buf), "%i", g_Drivers[d].number);
        Font_Measure(buf, &w, &h);
        Font_DrawString(buf, 0x58, 0x30 + off, 8);
        Font_Select(1);
        Font_DrawString(g_Drivers[d].name, w + 0x5f, 0x33 + off, 8);
        int k = fe->car[i] * 0x13 + off;
        Fe_DrawCallout(0xa6, 0x18 + off, 0x3f, 0x17, 0, 0, 0, 0);
        Fe_DrawFrame(r, g, b, 0xe, k + 0x14, 0x4b, k + 0x23);

        int help = -1;
        if (fe->stage[i] == 0)
        {
            Fe_DrawBox(0x5a, 10, 10, 0x118, 0x68 + off, 0x130, 0x72 + off, 1);
            Fe_DrawFrame(0x82, 0x82, 0x8c, 0x118, 0x68 + off, 0x130, 0x72 + off);
            Font_Select(0);
            Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, Fe_GetString(g_Settings.transmission[i] < 3 ? 0xb54 : 0xb55),
                           0x124, 0x68 + off, 1);
            help = 0x11;
        }
        else if (fe->stage[i] != 100)
            help = 0x12;
        if (help != -1)
            Fe_DrawHelp(i, HelpText(help), 0x21, 0x6c + off, 0xf, help == 0x12 ? 300 : 0x112, 3);

        if (fe->stage[i] == -1)
        {
            Fe_DrawBox(0x19, 0x41, 0, 0xf, 0x56 + off, 0x4b, 0x65 + off, 0);
            Fe_DrawFrame(r, g, b, 0xe, 0x56 + off, 0x4b, 0x65 + off);
            Font_Select(1);
            Font_DrawString(Fe_GetString(0xb56), 0x13, 0x52 + off, 0x11);
        }
        if (fe->stage[i] == 100 || fe->nextScreen == 1200)
        {
            Font_Select(1);
            Font_DrawString(Fe_GetString(0xb58), 200, 0x4c + off, 9);       /* Ready... */
            Fe_DrawFrame(r / 2 + 0x1e, g / 2 + 0x1e, b / 2 + 0x1e, 0xc3, 0x51 + off, 0xf5, 0x5e + off);
        }
        else
        {
            int o = fe->modelOffset[i];
            Fe_DrawCarModel(i, 0xeb - o / 300, 0x80 + off - o / 0x28a, 0x2e18, 0);
            if (!Fe_CarAvailable(fe->driver[i], fe->car[i]) && o == 15000)
                Fe_DrawSpinningModel(0x108, 0x54 + off, 46000, 3, 10);
        }
    }
}

/* X: guarda el coche en la parrilla y pasa al compañero, a los rivales o a la pantalla 1300 */
static void Confirm(int p)
{
    FrontendState* fe = g_Frontend;
    int np = g_Settings.numPlayers;
    if (Fe_CarAvailable(fe->driver[p], fe->car[p]) != 1)
    {
        Fe_PlaySound(0x4c);
        return;
    }
    if (fe->stage[p] != 100)
        Fe_PlaySound(0x48);

    int st = fe->stage[p];
    GridCar* e;
    if (st == 0)
    {
        e = &g_RaceGrid.car[p];
        e->player = (uint8_t)p;
        e->driver = fe->driver[p];
        e->car = fe->car[p];
        e->team = (uint8_t)p;
    }
    else if (st == -1)
    {
        e = &g_RaceGrid.car[p + np];
        e->player = 0xff;
        e->driver = fe->driver[p];
        e->car = fe->car[p];
        e->team = (uint8_t)p;
    }
    else if ((uint8_t)(st - 1) < 5)
    {
        int idx = OpponentBase() + st - 1;
        e = &g_RaceGrid.car[idx];
        e->player = 0xff;
        e->driver = fe->driver[p];
        e->car = fe->car[p];
        e->team = (uint8_t)(idx + 1);
        if (st == fe->numOpponents)
            fe->stage[p] = 100;
        else
        {
            fe->stage[0]++;
            Fe_SetDriver(0, g_Settings.oppDriver[(int)fe->stage[0]], g_Settings.oppCar[(int)fe->stage[0]]);
        }
    }

    if (fe->stage[p] == 0)
    {
        if (fe->teamMode)
        {
            fe->stage[p] = -1;
            Fe_SetDriver(p, g_Settings.mateDriver[p], g_Settings.mateCar[p]);
        }
        else
            fe->stage[p] = 100;
    }
    else if (fe->stage[p] == -1)
        fe->stage[p] = 100;

    if (fe->selectMode != 1)
    {
        if (fe->stage[0] != 100 || fe->stage[1] != 100)
            return;
        if (fe->pickOpponents && fe->numOpponents)
        {
            fe->stage[0] = 1;
            fe->selectMode = 1;
            if (np == 1)
                Fe_SetDriver(0, g_Settings.oppDriver[1], g_Settings.oppCar[1]);
            else
                fe->nextScreen = 1200;   /* vuelve a entrar con un solo panel para los rivales */
        }
    }
    if (fe->stage[0] == 100 && fe->stage[1] == 100)
        fe->nextScreen = 1300;
}

/* triangulo: deshace el ultimo paso */
static void Back(int p)
{
    FrontendState* fe = g_Frontend;
    int st = fe->stage[p];
    Fe_PlaySound(st == 0 ? 0x48 : 0x5c);
    if (st == 0)
    {
        fe->nextScreen = 200;
        return;
    }
    if (st == -1 || (st == 100 && !fe->teamMode))
    {
        fe->stage[p] = 0;
        Fe_SetDriver(p, g_Settings.lastDriver[p], g_Settings.lastCar[p]);
        return;
    }
    if (st == 100)
    {
        fe->stage[p] = -1;
        Fe_SetDriver(p, g_Settings.mateDriver[p], g_Settings.mateCar[p]);
        return;
    }
    if (fe->selectMode != 1)
        return;
    if (fe->stage[0] < 2)
    {
        if (g_Settings.numPlayers == 2)
        {
            fe->stage[0] = fe->stage[1] = fe->teamMode ? -1 : 0;
            fe->selectMode = 0;
            fe->nextScreen = 1200;
        }
        else
        {
            if (!fe->teamMode)
            {
                fe->stage[0] = 0;
                Fe_SetDriver(0, g_Settings.lastDriver[0], g_Settings.lastCar[0]);
            }
            else
            {
                fe->stage[0] = -1;
                Fe_SetDriver(0, g_Settings.mateDriver[0], g_Settings.mateCar[0]);
            }
            fe->selectMode = 0;
        }
    }
    else
    {
        fe->stage[0]--;
        g_RaceGrid.car[OpponentBase() + fe->stage[p] - 1].team = 100;
        Fe_SetDriver(0, g_Settings.oppDriver[(int)fe->stage[0]], g_Settings.oppCar[(int)fe->stage[0]]);
    }
}

static void HandleInput(int p)
{
    FrontendState* fe = g_Frontend;
    CarouselEntry* c = fe->carousel[p];
    int port = p;
    if (p == 0 && fe->numPanels == 1 && g_Settings.numPlayers == 2)
        port = -1;

    if (fe->stage[p] != 100)
    {
        Fe_SetDriver(p, -1, -1);
        if (Fe_PadPressed(PADB_RIGHT, port, 0))
        {
            if (fe->scrollDir[p] == 0)
            {
                fe->scrollDir[p] = -8;
                fe->scrollX[p] = 0;
                c[3].car = fe->car[p];
                c[3].driver = (uint8_t)NextDriver2(fe->driver[p]);
                Fe_LoadCarThumb(CarImage(c[3].driver, c[3].car), c[3].slot);
                Fe_PlaySound(0x47);
            }
            fe->scrollAgain[p] = 0;
        }
        else if (Fe_PadPressed(PADB_LEFT, port, 0))
        {
            if (fe->scrollDir[p] == 0)
            {
                fe->scrollDir[p] = 8;
                fe->scrollX[p] = -0x44;
                Carousel_ShiftRight(p);
                Fe_PlaySound(0x47);
            }
            fe->scrollAgain[p] = 0;
        }
        else
        {
            int d = 0;
            if (Fe_PadPressed(PADB_UP, p, 1))
                d = -1;
            else if (Fe_PadPressed(PADB_DOWN, p, 1))
                d = 1;
            int car = Fe_Clamp(fe->car[p] + d, 0, 2);
            if (d && car != fe->car[p])
            {
                fe->modelAnim[p] = 0;
                fe->car[p] = (uint8_t)car;
                for (int i = 0; i < 4; i++)
                {
                    c[i].car = (uint8_t)car;
                    Fe_LoadCarThumb(CarImage(c[i].driver, car), c[i].slot);
                }
                fe->cursor = fe->prevCursor == 0;
                Fe_RequestCar(p, c[1].driver, car);
            }
        }
    }

    int idle = fe->scrollDir[p] == 0 && fe->stage[p] != 100;
    if (Fe_PadPressed(PADB_CROSS, port, 1) && fe->scrollDir[p] == 0)
        Confirm(p);
    else if (Fe_PadPressed(PADB_TRIANGLE, port, 1))
        Back(p);
    else if (Fe_PadPressed(PADB_SQUARE, port, 1))
    {
        int st = fe->stage[p];
        if ((uint8_t)(st - 1) < 5)
        {
            /* el resto de rivales los elige el juego */
            for (int i = st; i <= fe->numOpponents; i++)
            {
                GridCar* e = &g_RaceGrid.car[OpponentBase() + i - 1];
                e->player = 0xff;
                e->team = 100;
            }
            fe->stage[p] = 100;
            fe->nextScreen = 1300;
        }
        else if (st == 0)
        {
            g_Settings.transmission[p] = (uint8_t)(g_Settings.transmission[p] + (g_Settings.transmission[p] > 2 ? -3 : 3));
            Fe_PlaySound(0x48);
        }
    }
    else if (Fe_PadPressed(PADB_R1, port, 1))
    {
        if (idle)
        {
            /* siguiente grupo: Winston Cup, camionetas/Busch, leyendas, especiales */
            int d = fe->driver[p];
            int nd = d < 0x21 ? 0x21 : d < 0x27 ? 0x27 : d < 0x2e ? 0x2e : 0;
            Fe_SetDriver(p, nd, fe->car[p]);
            Fe_PlaySound(0x50);
        }
    }
    else if (Fe_PadPressed(PADB_L1, port, 1))
    {
        if (idle)
        {
            int d = fe->driver[p];
            int nd = d < 0x21 ? 0x2e : d < 0x27 ? 0 : d < 0x2e ? 0x21 : 0x27;
            Fe_SetDriver(p, nd, fe->car[p]);
            Fe_PlaySound(0x4f);
        }
    }
}

/* 80081a9c */
void Fe_Screen1200(void)
{
    FrontendState* fe = g_Frontend;
    int np = g_Settings.numPlayers;

    if (fe->firstFrame)
    {
        Fe_LoadAtlas();
        Fe_ResetCars();
        for (int p = 0; p < np; p++)
        {
            fe->scrollDir[p] = 0;
            fe->scrollX[p] = 0;
            fe->scrollAgain[p] = 0;
            fe->carModel[p] = NULL;
        }
        if (fe->prevScreen == 1200)
        {
            for (int p = 0; p < np; p++)
            {
                int st = fe->stage[p];
                if (st == 0)
                    Fe_SetDriver(p, g_Settings.lastDriver[p], g_Settings.lastCar[p]);
                else if (st == -1)
                    Fe_SetDriver(p, g_Settings.mateDriver[p], g_Settings.mateCar[p]);
                else if ((uint8_t)(st - 1) < 5)
                    Fe_SetDriver(p, g_Settings.oppDriver[st], g_Settings.oppCar[st]);
            }
        }
        else
        {
            fe->stage[0] = 0;
            fe->stage[1] = np == 1 ? 100 : 0;
            fe->selectMode = 0;
            for (int i = 0; i < 6; i++)
                g_RaceGrid.car[i].team = 100;
            for (int p = 0; p < np; p++)
                Fe_SetDriver(p, g_Settings.lastDriver[p], g_Settings.lastCar[p]);
        }
        if (fe->selectMode == 0)
            Fe_SetBackground(np == 1 ? 1 : 2);
        else if (fe->selectMode == 1)
            Fe_SetBackground(1);
        fe->numPanels = fe->bgIndex == 1 ? 1 : 2;
    }

    Fe_UpdateCars();
    int r, g, b;
    Fe_ItemColor(&r, &g, &b, 2);
    r <<= 3; g <<= 3; b <<= 3;
    Fe_ReadPads();
    Fe_DrawBackground(8000);

    for (int p = 0; p < fe->numPanels; p++)
        Fe_ScrollCarousel(p);
    if (fe->numPanels == 1)
        DrawPanel1P(r, g, b);
    else
        DrawPanels2P(r, g, b);
    for (int p = 0; p < fe->numPanels; p++)
        HandleInput(p);

    fe->modelAngle += g_FrameDelta * 2;
    Fe_PadRepeatReset();
    if (fe->fadeDone)
        Fe_FreeAtlas();
    for (int p = 0; p < fe->numPanels; p++)
    {
        if (fe->scrollDir[p] > 0)
            fe->scrollDir[p] = (char)g_FrameDelta;
        else if (fe->scrollDir[p] < 0)
            fe->scrollDir[p] = (char)-g_FrameDelta;
    }
}
