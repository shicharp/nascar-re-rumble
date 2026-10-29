/* Pantalla 1300: eleccion de sede y pista (carrusel de sedes, lista de pistas, trazado 3D girando) y
 * montaje de la carrera (parrilla, rivales, fichero .trk) antes de salir del front-end.
 * En campeonato, la mejor copa ganada en la sede (tablas DAT_800b7998 / DAT_800b8418) gira en la esquina de la
 * ventana. Los botones de cada jugador los pone Pad_SetupControls (8008b160, fe_pads.c). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "mem.h"
#include "gfx.h"
#include "gte.h"
#include "pad.h"
#include "font.h"
#include "globals.h"
#include "asset.h"
#include "model.h"
#include "render.h"
#include "frontend.h"
#include "fe_internal.h"
#include "savegame.h"
#include "net.h"

#define GTE_OP_NCCS 0x0108041B

/* 800abdd4: codigo de sede (directorio CW\Loc<codigo>) y ficheros de sus pistas */
static const char* const s_trackFiles[9][4] = {
    { "GR", "GR1.trk", "GR2.trk", "GR3.trk" },
    { "MC", "MC1.trk", "MC2.trk", "MC3.trk" },
    { "SE", "SE1.trk", "SE2.trk", "SE3.trk" },
    { "BB", "BB1.trk", "BB2.trk", "BB3.trk" },
    { "BL", "BL1.trk", "BL2.trk", "BL3.trk" },
    { "MG", "MG1.trk", "MG2.trk", "MG3.trk" },
    { "JT", "JT1.trk", "JT2.trk", "JT3.trk" },
    { "WC", "-", "-", "-" },
    { "EA", "-", "-", "-" },
};

const char* Fe_VenueCode(int venue)
{
    return venue >= 0 && venue < 9 ? s_trackFiles[venue][0] : "BB";
}

/* parte de 80079814 (8007a478): nombres (textos 5000 + sede*10 + k), trazados TkFE y miniaturas */
void Fe_InitVenues(void)
{
    FrontendState* fe = g_Frontend;
    for (int v = 0; v < 9; v++)
    {
        for (int k = 0; k < 4; k++)
        {
            VenueEntry* e = &fe->venues[v][k];
            e->name = Fe_GetString(5000 + v * 10 + k);
            e->file = k ? s_trackFiles[v][k] : NULL;
            e->venue = (short)v;
            e->unk6 = -1;
            e->tkfe = k ? (short)(v < 7 ? 1000 + v * 3 + k - 1 : 1000) : -1;
            e->image = (short)(v * 4 + k);
        }
    }
}

static int ClassIndex(void)
{
    return g_Settings.racingClass ? g_Settings.racingClass - 1 : 0;
}

/* 80090718: la pista (sede, pista) se puede elegir */
static int Fe_TrackAvailable(int venue, int track)
{
    int cls = ClassIndex();
    if (g_Settings.gameMode == 4)
    {
        if (venue <= 5 && PROGRESS(0x800b713au + venue * 3 + cls) != 0)
            return 1;
        if (venue != 7)
            return 0;
        return PROGRESS(0x800b714cu + cls) != 0;
    }
    if (venue <= 3)
        return 1;
    if (venue < 6)
        return venue - 4 < PROGRESS(0x800b7136u);           /* sedes extra desbloqueadas */
    if (venue == 6)
        return !g_Settings.champType && PROGRESS(0x800b7137u + track) == 1;
    if (venue == 7)
        return 1;
    if (venue != 8)
        return 0;
    for (int k = 0; k < 6; k++)                               /* EA: todas las copas ganadas */
        if (PROGRESS(0x800b713au + cls + 3 * k) == 0)
            return 0;
    return 1;
}

static int NumVenues(void)
{
    if (!g_Settings.champType)
        return 7;
    return g_Settings.gameMode == 4 ? 8 : 9;
}

/* 80090860: sede v dando la vuelta; en campeonato se salta Jacksonville Trail (6) */
int Fe_WrapVenue(int v)
{
    int cur = g_Settings.venue;
    int n = NumVenues();
    if (v < 0)
    {
        cur += n;
        v += n;
    }
    else if (v >= n)
    {
        cur -= n;
        v -= n;
    }
    if (!g_Settings.champType)
        return v;
    int skip = 0;
    if (v < cur)
    {
        for (int i = v; i <= cur; i++)
            if (i == 6)
                skip++;
        v -= skip;
    }
    else if (v > cur)
    {
        for (int i = cur; i <= v; i++)
            if (i == 6)
                skip++;
        v += skip;
    }
    if (v >= n)
        v -= n;
    return v;
}

/* 8008e13c: miniatura de 64x32 en (0x200, hueco*32), los huecos 7+ una pagina mas abajo */
void Fe_LoadVenueThumb(int image, int slot)
{
    Fe_DecodeLmdc(image, 0x200, slot * 0x20 + (slot > 6 ? 0x20 : 0));
}

static int VenueImage(const VenueCarousel* c)
{
    return g_Frontend->venues[c->venue][c->k].image;
}

/* 8008c308: miniatura de sede. mode 1 = carrusel (64x24 recortada a la ventana), 0 = con marco,
 * -1 = sin marco (64x30) */
void Fe_DrawVenueThumb(int slot, int x, int y, int depth, int mode)
{
    if (slot < 0)
        return;
    int lo = 0, hi = 0x140;
    if (g_Frontend->screen == 1300)
    {
        lo = 0x1b;
        hi = 0xe3;
    }
    else if (g_Frontend->screen == 900)
    {
        lo = 0x3a;
        hi = 0x102;
    }
    DR_TPAGE* tp = (DR_TPAGE*)Gfx_AllocPrim(sizeof(DR_TPAGE));
    SPRT* s = (SPRT*)Gfx_AllocPrim(sizeof(SPRT));
    if (!tp || !s)
        return;
    setDrawTPage(tp, 1, 1, getTPage(2, 0, 0x200, slot < 7 ? 0 : 0x100));   /* 0xE1000708/718 */
    Gfx_AddPrimDepth(depth, tp);
    setSprt(s);
    setRGB0(s, 0x80, 0x80, 0x80);
    int v = (slot * 0x20 + (slot > 6 ? 0x20 : 0)) & 0xff;
    int u = 0, w = 0x40, h = 0x1e, sx = x;
    if (hi < x + 0x40 && mode == 1)
    {
        w = x > hi ? 0 : 0x40 - (x - (hi - 0x40));
        h = 0x18;
    }
    else if (x < lo && mode == 1)
    {
        u = lo - x;
        w = x + 0x40 < lo ? 0 : 0x40 - (lo - x);
        h = 0x18;
        sx = lo;
    }
    else
    {
        h = mode == 1 ? 0x18 : 0x1e;
        if (mode == 0)
            Fe_DrawFrame(0x46, 0x46, 0x46, x - 1, y - 1, x + 0x40, y + 0x1e);
    }
    setXY0(s, sx, y);
    setUV0(s, u, v);
    setWH(s, w, h);
    Gfx_AddPrimDepth(depth, s);
}

/* 8008b80c: fondo con una ventana (x0, y0)-(x1, y1) que muestra el panel oscuro (Gmdc 7 en (0x300,0x100)).
 * El fondo va delante (profundidad 1) y el panel detras: lo que se dibuje en medio (el trazado 3D)
 * solo se ve por la ventana. */
static void Fe_DrawWindowBg(int x0, int y0, int x1, int y1, int depth)
{
    if (x0 < 0xa0)
    {
        Fe_DrawVramRect(0x140, 0, 0xa0, y0, 0, 0, 1, 0);
        Fe_DrawVramRect(0x140, y0, x0, y1 - y0, 0, y0, 1, 0);
        Fe_DrawVramRect(0x140, y1, 0xa0, 0xf0 - y1, 0, y1, 1, 0);
        Fe_DrawVramRect(0x300, 0x100, 0xa2 - x0, y1 - y0, x0 - 1, y0, depth, 0);
    }
    if (x1 > 0xa0)
    {
        Fe_DrawVramRect(0x140, 0x100, 0xa0, y0, 0xa0, 0, 1, 0);
        Fe_DrawVramRect(x1 + 0xa0, y0 + 0x100, 0x140 - x1, y1 - y0, x1, y0, 1, 0);
        Fe_DrawVramRect(0x140, y1 + 0x100, 0xa0, 0xf0 - y1, 0xa0, y1, 1, 0);
        Fe_DrawVramRect(0x39f - x0, 0x100, x1 - 0xa0, y1 - y0, 0xa0, y0, depth, 0);
    }
}

/* ---------------------------------------------------------------- trazado 3D (TkFE) */

static_assert(sizeof(TrackPreview) == 0x6c, "TrackPreview debe medir 0x6C");

static void Normalize16(double* v)
{
    double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0)
        for (int i = 0; i < 3; i++)
            v[i] /= l;
}

/* 8001893c: cinta de anchura w a la altura h sobre los puntos (todo a escala 1/8) */
TrackPreview* TrackPreview_Create(const NrAsset* a, int w, int h)
{
    if (!a)
        return NULL;
    const uint8_t* d = a->data;
    int n = (int)NR_Read32(d + 4);
    if (n <= 0)
        return NULL;
    TrackPreview* tp = (TrackPreview*)Pool_Alloc(n * 0x18 + (int)sizeof(TrackPreview), 1);
    memset(tp, 0, sizeof(*tp));
    tp->verts = (int16_t*)(tp + 1);
    tp->normals = tp->verts + n * 8;
    tp->count = (short)n;
    tp->start = (short)NR_Read32(d + 8);
    int startK = n - tp->start;
    const int16_t* p = (const int16_t*)(d + 0xc);
    int ax = p[0], ay = p[1], az = p[2];
    for (int k = n, i = 0; k > 0; k--, i++)
    {
        const int16_t* q = k == 1 ? p : p + (i + 1) * 3;
        double dir[3] = { (double)(q[0] - ax), (double)(q[1] - ay), (double)(q[2] - az) };
        Normalize16(dir);
        /* lado = cross(arriba, dir), normal = cross(lado, dir) */
        double side[3] = { dir[2], 0, -dir[0] };
        Normalize16(side);
        double nrm[3] = { side[1] * dir[2] - side[2] * dir[1], side[2] * dir[0] - side[0] * dir[2],
                          side[0] * dir[1] - side[1] * dir[0] };
        int16_t* nv = tp->normals + i * 4;
        nv[0] = (int16_t)lround(nrm[0] * 4096);
        nv[1] = (int16_t)lround(nrm[1] * 4096);
        nv[2] = (int16_t)lround(nrm[2] * 4096);
        int sx = (int)lround(side[0] * (w / 2)), sy = (int)lround(side[1] * (w / 2)), sz = (int)lround(side[2] * (w / 2));
        int16_t* vv = tp->verts + i * 8;
        vv[0] = (int16_t)((ax - sx) >> 3);
        vv[1] = (int16_t)((ay - sy + h) >> 3);
        vv[2] = (int16_t)((az - sz) >> 3);
        vv[4] = (int16_t)((ax + sx) >> 3);
        vv[5] = (int16_t)((ay + sy + h) >> 3);
        vv[6] = (int16_t)((az + sz) >> 3);
        if (k == startK)
        {
            sx = sx * 0x6e >> 6;
            sy = sy * 0x6e >> 6;
            sz = sz * 0x6e >> 6;
            int16_t(*sv)[4] = tp->startVerts;
            sv[0][0] = (int16_t)((ax - sx) >> 3);
            sv[0][1] = (int16_t)((ay - sy + h) >> 3);
            sv[0][2] = (int16_t)((az - sz) >> 3);
            sv[1][0] = (int16_t)((ax + sx) >> 3);
            sv[1][1] = (int16_t)((ay + sy + h) >> 3);
            sv[1][2] = (int16_t)((az + sz) >> 3);
            sv[2][0] = sv[0][0]; sv[2][1] = (int16_t)(sv[0][1] + 0x8c); sv[2][2] = sv[0][2];
            sv[3][0] = sv[1][0]; sv[3][1] = (int16_t)(sv[1][1] + 0x8c); sv[3][2] = sv[1][2];
            sv[4][0] = vv[0]; sv[4][1] = (int16_t)(vv[1] + 100); sv[4][2] = vv[2];
            sv[5][0] = vv[4]; sv[5][1] = (int16_t)(vv[5] + 100); sv[5][2] = vv[6];
        }
        ax = q[0];
        ay = q[1];
        az = q[2];
    }
    return tp;
}

/* 8001016c: proyecta n vertices (stride 8 bytes); flags 0xC0 = dentro de la pantalla, 1 = mal */
static void ProjectVerts(const int16_t* v, int n, uint32_t* sxy, int* sz, uint8_t* fl)
{
    for (int i = 0; i < n; i++, v += 4)
    {
        Gte_SetV0(v[0], v[1], v[2]);
        doCOP2(GTE_OP_RTPS);
        uint32_t flag = CFC2(31);
        uint32_t xy = MFC2(14);
        int z = (int)MFC2(19);
        sxy[i] = xy;
        sz[i] = z;
        if (flag & 0x1c66000)
            fl[i] = 1;
        else
            fl[i] = (uint8_t)((z < 0x20) | ((xy & 0xffff) < 320u) << 6 | ((xy >> 16) < 240u) << 7);
    }
}

static uint32_t LightNormal(const int16_t* n)
{
    MTC2((uint32_t)(n[0] & 0xffff) | (uint32_t)n[1] << 16, 0);
    MTC2((uint32_t)n[2], 1);
    doCOP2(GTE_OP_NCCS);
    return MFC2(22) & 0xffffff;
}

static void EmitG4(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t ca, uint32_t cc, int depth)
{
    POLY_G4* p = (POLY_G4*)Gfx_AllocPrim(sizeof(POLY_G4));
    if (!p)
        return;
    setPolyG4(p);
    setXY4(p, (int16_t)a, (int16_t)(a >> 16), (int16_t)b, (int16_t)(b >> 16),
           (int16_t)c, (int16_t)(c >> 16), (int16_t)d, (int16_t)(d >> 16));
    setRGB0(p, ca & 0xff, (ca >> 8) & 0xff, (ca >> 16) & 0xff);
    setRGB1(p, ca & 0xff, (ca >> 8) & 0xff, (ca >> 16) & 0xff);
    setRGB2(p, cc & 0xff, (cc >> 8) & 0xff, (cc >> 16) & 0xff);
    setRGB3(p, cc & 0xff, (cc >> 8) & 0xff, (cc >> 16) & 0xff);
    Gfx_AddPrimDepth(depth, p);
}

/* 80018ef0: cinta iluminada (NCCS con el color de la pista) y su sombra plana en y = 0 */
void TrackPreview_Draw(TrackPreview* tp, uint32_t color, const SVECTOR* light)
{
    MTC2(color | 0x38000000u, 6);
    CTC2(0x20 << 4, 13);
    CTC2(0x20 << 4, 14);
    CTC2(0x20 << 4, 15);
    CTC2(0x1000, 16);                       /* DAT_800aa4a8: la luz 1 aporta a los tres canales */
    CTC2(0x1000u << 16, 17);
    CTC2(0, 18);
    CTC2(0x1000, 19);
    CTC2(0, 20);
    /* 8001010c: direccion de la luz al espacio del trazado */
    Gte_SetRot(tp->rot[0] >> 4, tp->rot[1] >> 4, tp->rot[2] >> 4,
               -tp->rot[3] >> 4, -tp->rot[4] >> 4, -tp->rot[5] >> 4,
               tp->rot[6] >> 4, tp->rot[7] >> 4, tp->rot[8] >> 4);
    Gte_SetV0(light->vx, light->vy, light->vz);
    doCOP2(GTE_OP_RTV0);
    int lx = MFC2_S(9), ly = -MFC2_S(10), lz = MFC2_S(11);
    CTC2((uint32_t)(lx & 0xffff) | (uint32_t)ly << 16, 8);
    CTC2((uint32_t)(lz & 0xffff), 9);
    CTC2(0, 10);
    CTC2(0, 11);
    CTC2(0, 12);

    Render_LoadObjectMatrix16(tp->rot, tp->pos);

    uint32_t prevXY[10], curXY[10];
    int prevZ[10], curZ[10];
    uint8_t prevF[10], curF[10];
    int n = tp->count;
    const int16_t* last = tp->verts + (n - 1) * 8;
    ProjectVerts(last, 2, prevXY, prevZ, prevF);
    int16_t flat[8] = { last[0], 0, last[2], 0, last[4], 0, last[6], 0 };
    ProjectVerts(flat, 2, prevXY + 2, prevZ + 2, prevF + 2);
    uint32_t prevCol = LightNormal(tp->normals + (n - 1) * 4);

    for (int i = 0, k = n; i < n; i++, k--)
    {
        const int16_t* v = tp->verts + i * 8;
        ProjectVerts(v, 2, curXY, curZ, curF);
        uint32_t col = LightNormal(tp->normals + i * 4);
        int16_t f2[8] = { v[0], 0, v[2], 0, v[4], 0, v[6], 0 };
        ProjectVerts(f2, 2, curXY + 2, curZ + 2, curF + 2);
        if (((prevF[0] | prevF[1] | curF[0] | curF[1]) & 0xc1) == 0xc0)
            EmitG4(curXY[0], curXY[1], prevXY[0], prevXY[1], col, prevCol,
                   (curZ[0] + curZ[1] + prevZ[0] + prevZ[1]) >> 2);
        if (((prevF[2] | prevF[3] | curF[2] | curF[3]) & 0xc1) == 0xc0)
            EmitG4(curXY[2], curXY[3], prevXY[2], prevXY[3], 0x0f0f0f, 0x0f0f0f,
                   (curZ[2] + curZ[3] + prevZ[2] + prevZ[3]) >> 2);
        if (k == n - tp->start)
        {
            /* 80018dac: linea de salida, 3 cuadros (tabla 800aa49c) */
            static const uint8_t quads[3][4] = { { 0, 4, 6, 8 }, { 8, 6, 7, 9 }, { 1, 9, 7, 5 } };
            ProjectVerts(&tp->startVerts[0][0], 6, curXY + 4, curZ + 4, curF + 4);
            for (int q = 0; q < 3; q++)
            {
                const uint8_t* ix = quads[q];
                if (((curF[ix[0]] | curF[ix[1]] | curF[ix[2]] | curF[ix[3]]) & 0xc1) != 0xc0)
                    continue;
                POLY_F4* p = (POLY_F4*)Gfx_AllocPrim(sizeof(POLY_F4));
                if (!p)
                    break;
                setPolyF4(p);
                uint32_t a = curXY[ix[0]], b = curXY[ix[1]], c = curXY[ix[3]], d = curXY[ix[2]];
                setXY4(p, (int16_t)a, (int16_t)(a >> 16), (int16_t)b, (int16_t)(b >> 16),
                       (int16_t)c, (int16_t)(c >> 16), (int16_t)d, (int16_t)(d >> 16));
                setRGB0(p, 0x8f, 0x8f, 0);
                Gfx_AddPrimDepth((curZ[ix[0]] + curZ[ix[1]] + curZ[ix[2]] + curZ[ix[3]]) >> 2, p);
            }
        }
        memcpy(prevXY, curXY, sizeof(prevXY[0]) * 4);
        memcpy(prevZ, curZ, sizeof(prevZ[0]) * 4);
        memcpy(prevF, curF, 4);
        prevCol = col;
    }
}

static void Fe_LoadPreview(int height)
{
    FrontendState* fe = g_Frontend;
    if (fe->preview)
    {
        Pool_Free(fe->preview);
        fe->preview = NULL;
    }
    int id = g_Settings.venue == 8 ? fe->venues[0][g_Settings.racingClass].tkfe
                                   : fe->venues[g_Settings.venue][g_Settings.track + 1].tkfe;
    fe->preview = TrackPreview_Create(Asset_Find(ASSET_TkFE, id), 600, height);
}

/* ---------------------------------------------------------------- pantalla 1300 */

/* lista de pistas del panel: las 3 de la sede, o una por sede en la copa EA */
static int Fe_FillTrackList(void)
{
    FrontendState* fe = g_Frontend;
    int last = 6;
    if (g_Settings.venue == 8)
    {
        last = 9;
        for (int i = 4; i < 10; i++)
        {
            fe->vcar[i].venue = (uint8_t)(i - 4);
            fe->vcar[i].k = g_Settings.racingClass;
        }
    }
    else
        for (int i = 4; i < 7; i++)
        {
            fe->vcar[i].venue = fe->vcar[1].venue;
            fe->vcar[i].k = (uint8_t)(i - 3);
        }
    return last;
}

static void DrawCarousel(int n, int scrolled)
{
    FrontendState* fe = g_Frontend;
    for (int i = 0; i < n; i++)
        Fe_DrawVenueThumb(fe->vcar[i].slot, i * 0x44 + (scrolled ? fe->vScrollX : 0) + 0x1b, 0x1a, 0, 1);
}

/* el carrusel avanza: la sede de la derecha pasa al centro */
static void VenueShiftLeft(void)
{
    VenueCarousel* c = g_Frontend->vcar;
    int8_t slot = c[0].slot;
    c[0] = c[1];
    c[1] = c[2];
    c[2] = c[3];
    c[3].slot = slot;
    g_Settings.venue = c[1].venue;
}

/* el carrusel retrocede: entra por la izquierda la sede - 2 */
static void VenueShiftRight(void)
{
    FrontendState* fe = g_Frontend;
    VenueCarousel* c = fe->vcar;
    int8_t slot = c[3].slot;
    c[3] = c[2];
    c[2] = c[1];
    c[1] = c[0];
    c[0].slot = slot;
    c[0].k = 0;
    c[0].venue = (uint8_t)Fe_WrapVenue(g_Settings.venue - 2);
    Fe_LoadVenueThumb(VenueImage(&c[0]), c[0].slot);
}

/* al pararse el carrusel: lista de pistas y trazado de la nueva sede */
static void VenueSettle(void)
{
    FrontendState* fe = g_Frontend;
    int last = Fe_FillTrackList();
    DrawSync(0);
    for (int i = 4; i <= last; i++)
        Fe_LoadVenueThumb(VenueImage(&fe->vcar[i]), fe->vcar[i].slot);
    Fe_LoadPreview(700);
    if (Fe_TrackAvailable(g_Settings.venue, g_Settings.track) == 1)
        fe->previewAnim = -2;
}

static void Fe_ScrollVenues(void)
{
    FrontendState* fe = g_Frontend;
    VenueCarousel* c = fe->vcar;
    int dir = fe->vScrollDir;
    if (dir == 0)
    {
        DrawCarousel(3, 0);
        return;
    }
    if (dir < 0)
    {
        if (fe->vScrollX >= -0x43)
        {
            fe->vScrollX = (short)(fe->vScrollX + dir);
            if (fe->vScrollX >= -0x43)
            {
                DrawCarousel(4, 1);
                return;
            }
            if (!Fe_PadPressed(PADB_RIGHT, 0, 0))
            {
                fe->vScrollX = -0x44;
                DrawCarousel(4, 1);
                return;
            }
            fe->vScrollAgain = 1;
        }
        VenueShiftLeft();
        if (!fe->vScrollAgain)
        {
            VenueSettle();
            DrawCarousel(3, 0);
            fe->vScrollDir = 0;
            return;
        }
        c[3].k = 0;
        fe->vScrollX = (short)(fe->vScrollX + 0x44);
        c[3].venue = (uint8_t)Fe_WrapVenue(g_Settings.venue + 2);
        Fe_LoadVenueThumb(VenueImage(&c[3]), c[3].slot);
        DrawCarousel(4, 1);
        return;
    }
    if (fe->vScrollX < 0)
    {
        fe->vScrollX = (short)(fe->vScrollX + dir);
        if (fe->vScrollX < 0)
        {
            DrawCarousel(4, 1);
            return;
        }
        if (!Fe_PadPressed(PADB_LEFT, 0, 0))
        {
            fe->vScrollX = 0;
            DrawCarousel(4, 1);
            return;
        }
        fe->vScrollAgain = 1;
    }
    g_Settings.venue = c[1].venue;
    if (!fe->vScrollAgain)
    {
        VenueSettle();
        DrawCarousel(3, 0);
        fe->vScrollDir = 0;
        return;
    }
    fe->vScrollX = (short)(fe->vScrollX - 0x44);
    VenueShiftRight();
    DrawCarousel(4, 1);
}

/* 800838f8 */
void Fe_Screen1300(void)
{
    FrontendState* fe = g_Frontend;
    GameSettings* gs = &g_Settings;
    int port = gs->numPlayers == 2 ? -1 : 0;

    if (fe->firstFrame)
    {
        Fe_SetBackground(3);
        Fe_DecodeGmdc(7, 0x300, 0x100);
        uint8_t oldTrack = gs->track;
        uint8_t m = gs->gameMode;
        gs->teamRace = m == 1 || m == 3 || m == 8 || m == 9 || m == 11 || m == 12;
        gs->champType = m > 1 && m != 5 && m != 6 && m != 7 && m != 8 && m != 9 && m != 13 && m != 14;
        int n = NumVenues();
        if (gs->champType)
        {
            if (gs->venue == 6)
                gs->venue = 5;
            gs->track = 0;
            gs->savedTrack = oldTrack;
        }
        if (gs->venue >= n)
            gs->venue = (uint8_t)(n - 1);
        VenueCarousel* c = fe->vcar;
        c[0].venue = (uint8_t)Fe_WrapVenue(gs->venue - 1);
        c[1].venue = gs->venue;
        c[2].venue = (uint8_t)Fe_WrapVenue(gs->venue + 1);
        c[3].venue = 0;
        for (int i = 0; i < 4; i++)
            c[i].k = 0;
        int last = Fe_FillTrackList();
        for (int i = 0; i < 10; i++)
        {
            c[i].slot = (int8_t)i;
            if (i <= last)
                Fe_LoadVenueThumb(VenueImage(&c[i]), i);
        }
        Fe_LoadPreview(700);
        fe->previewLight.vx = 0;
        fe->previewLight.vy = -0x1000;
        fe->previewLight.vz = 0x1000;
        {
            double l = sqrt(2.0);
            fe->previewLight.vy = (short)lround(-4096 / l);
            fe->previewLight.vz = (short)lround(4096 / l);
        }
        fe->vScrollDir = 0;
        fe->vScrollAgain = 0;
        fe->vScrollX = 0;
        fe->previewOffset = 15000;
        fe->previewAnim = Fe_TrackAvailable(gs->venue, gs->track) == 1 ? 1 : 0;
        fe->previewZoom = 0;
        fe->previewDX = fe->previewDY = 0;
        fe->popup = 0;
        fe->portPopup = 0;
    }

    Fe_ReadPads();
    int r, g, b;
    Fe_ItemColor(&r, &g, &b, 2);
    r <<= 3; g <<= 3; b <<= 3;
    Fe_DrawHelp(0, HelpText(gs->champType ? 0x14 : 0x15), 0x21, 0xd5, 0x23, 300, 0);
    Fe_ScrollVenues();

    /* panel: ventana, nombre de la sede y lista de pistas */
    int rx0, ry0, rx1, ry1, nameX, nameY, items;
    int ix[6], iy[6], islot[6];
    const char* iname[6];
    int prevX = 0, prevY = 0, lockX = 0, lockY = 0;
    VenueCarousel* c = fe->vcar;
    int curVenue = c[1].venue;
    if (!gs->champType || gs->venue != 8 || (fe->vScrollDir && !fe->popup))
    {
        if (!gs->champType)
        {
            Fe_DrawCallout(0x5f, 0x19, 0x3f, 0x19, 0xd, 0x74, gs->track * 0x30 + 0x20, 0xd);
            rx0 = 0x73; ry0 = 0x61; rx1 = 0x122; ry1 = 0xcb; nameX = 200; nameY = 0x42;
            for (int k = 0; k < 3; k++)
            {
                ix[k] = 0x1a;
                iy[k] = 0x50 + k * 0x30;
            }
        }
        else
        {
            Fe_DrawCallout(0x5f, 0x19, 0x3f, 0x19, 0, 0, 0, 0);
            rx0 = 0x19; ry0 = 0x60; rx1 = 200; ry1 = 0xca; nameX = 0x6e; nameY = 0x41;
            for (int k = 0; k < 3; k++)
            {
                ix[k] = 0xe2;
                iy[k] = 0x4c + k * 0x30;
            }
            fe->popup = 0;
            prevX = lockX = -0x5a;
            prevY = lockY = -1;
        }
        items = 3;
        for (int k = 0; k < 3; k++)
        {
            int v = curVenue != 8 ? curVenue : 0;
            iname[k] = fe->venues[v][k + 1].name;
            islot[k] = c[4 + k].slot;
        }
    }
    else
    {
        /* copa EA: las seis sedes en dos columnas */
        rx0 = 0x5a; ry0 = 0x60; rx1 = 0xe1; ry1 = 0xca; nameX = 0x98; nameY = 0x41;
        lockX = -0x5f;
        lockY = -1;
        prevX = (fe->previewOffset - 15000) / 400 - 7;
        prevY = -1;
        items = 6;
        int cls = gs->racingClass;
        for (int k = 0; k < 3; k++)
        {
            ix[k] = 0xd;
            iy[k] = 0x4c + k * 0x30;
            ix[k + 3] = 0xee;
            iy[k + 3] = iy[k];
            iname[k] = fe->venues[k][cls].name;
            iname[k + 3] = fe->venues[k + 3][cls].name;
            islot[k] = c[4 + k].slot;
            islot[k + 3] = c[7 + k].slot;
        }
    }
    Fe_DrawWindowBg(rx0, ry0, rx1, ry1, 50000);
    Fe_DrawFrame(0x96, 0x96, 0xa0, rx0, ry0, rx1, ry1);
    Font_Select(2);
    Fe_DrawTextRgb(0x1f, 0x1f, 0x1f, fe->venues[curVenue][0].name, nameX, nameY, 1);
    if (gs->champType)
    {
        /* la mejor copa ganada en esta sede y clase, girando en la esquina: la de las tablas de copa individual
         * (puesto +0x11) o por equipos (+0xD), la mejor de las dos; en el modo 4 la de su tabla (+0x1B, copas 8..10) */
        int t = curVenue > 6 ? curVenue - 1 : curVenue;
        int cls = gs->racingClass ? gs->racingClass - 1 : 0;
        const uint8_t* e = g_SaveExtra.cupRecs + t * 0x150 + cls * 0x70;
        const uint8_t* u = g_SaveExtra.cupRecs2 + t * 0x120 + cls * 0x60;
        int model = -1;
        if (gs->gameMode == 4)
        {
            if (NR_Read32(e + 4) != 0)
                model = e[0x1b] + 8;
        }
        else if (NR_Read32(e) != 0 || NR_Read32(u) != 0)
            model = (NR_Read32(u) == 0 || (NR_Read32(e) != 0 && e[0x11] <= u[0xd]) ? e[0x11] : u[0xd]) + 5;
        if (model >= 0)
            Fe_DrawSpinningModel(rx1 - 0x11, ry0 + 0xf, 29000, model, 2);
    }

    Font_Select(0);
    for (int k = 0; k < items; k++)
    {
        int mode = -1;
        if (!gs->champType)
            mode = gs->track == k ? -1 : 0;
        Fe_DrawVenueThumb(islot[k], ix[k], iy[k], 0, mode);
        if (mode == -1)
            Fe_DrawFrame(r / 2 + 0x52, g / 2 + 0x52, b / 2 + 0x5c, ix[k] - 1, iy[k] - 1, ix[k] + 0x40, iy[k] + 0x1e);
        Fe_DrawTextRgb(7, 7, 7, iname[k], ix[k] + 0x1e, iy[k] - 0xd, 1);
        Fe_DrawTextRgb(0x1f, mode ? 0x1c : 0x1f, mode == 0 ? 0x1f : 0, iname[k], ix[k] + 0x1f, iy[k] - 0xc, 1);
    }

    int off = fe->previewOffset;
    if (gs->venue == 7)
    {
        /* Wild Card: tres interrogantes girando */
        Fe_DrawSpinningModel(0x3b - off / 300, 0x8c - off / 700, off * -2 + 0x1c6b0, 4, 4);
        Fe_DrawSpinningModel(0xa7 - off / 0x8c, 0xa0 - off / 0x1ae, off * -4 + 0x1d650, 4, -5);
        Fe_DrawSpinningModel(0x75 - off / 0x6e, 0x96 - off / 300, 0x7594 - off, 4, 3);
    }
    else if (fe->preview)
    {
        TrackPreview* tp = (TrackPreview*)fe->preview;
        tp->pos[0] = 0;
        tp->pos[1] = 0;
        tp->pos[2] = off * 8 + fe->previewZoom + 60000;
        int eye[3] = { 0, off * 2 + 30000, 0 };
        Camera_LookAt(eye, tp->pos);
        tp->pos[0] = fe->previewDX + 8000 - off * 10 / 0x15;
        tp->pos[1] = off * 10 / 0x15 + fe->previewDY - 8000;
        Math_RotMatrix(fe->modelAngle & 0xfff, 0, 0, tp->rot);
        SetGeomOffset(prevX + 0xa0, prevY + 0x78);
        TrackPreview_Draw(tp, 0x7f3f3f, &fe->previewLight);
        SetGeomOffset(0xa0, 0x78);
    }
    if (off == 15000 && !Fe_TrackAvailable(gs->venue, gs->track))
        Fe_DrawSpinningModel(lockX + 0xff, lockY + 0x9e, 32000, 3, 9);

    /* mando */
    if (Fe_PadPressed(PADB_RIGHT, port, 0))
    {
        if (fe->vScrollDir == 0)
        {
            fe->vScrollDir = -7;
            fe->vScrollX = 0;
            c[3].k = 0;
            c[3].venue = (uint8_t)Fe_WrapVenue(gs->venue + 2);
            fe->previewAnim = -1;
            fe->valueDir = 1;
            if (gs->venue == 8)
                fe->popup = 1;
            Fe_LoadVenueThumb(VenueImage(&c[3]), c[3].slot);
        }
        fe->vScrollAgain = 0;
    }
    else if (Fe_PadPressed(PADB_LEFT, port, 0))
    {
        if (fe->vScrollDir == 0)
        {
            fe->vScrollDir = 7;
            fe->vScrollX = -0x44;
            VenueShiftRight();
            fe->previewAnim = -1;
            fe->valueDir = 1;
            if (gs->venue == 8)
                fe->popup = 1;
        }
        fe->vScrollAgain = 0;
    }
    else
    {
        int d = 0;
        if (Fe_PadPressed(PADB_UP, port, 1) && !gs->champType)
            d = gs->track != 0 ? -1 : 0;
        else if (Fe_PadPressed(PADB_DOWN, port, 1) && !gs->champType)
            d = gs->track < 2 ? 1 : 0;
        else if (Fe_PadPressed(PADB_SQUARE, port, 1))
        {
            if (!gs->champType)
            {
                g_Venue = gs->venue;
                g_SaveExtra.recordPos[0] = g_SaveExtra.recordPos[1] = -1;
                fe->nextScreen = 1400;   /* records de la pista */
            }
        }
        else if (Fe_PadPressed(PADB_CROSS, port, 1))
        {
            if (!Fe_TrackAvailable(gs->venue, gs->track) || fe->vScrollDir)
            {
                if (!fe->vScrollDir)
                    Fe_PlaySound(0x4c);
            }
            else if (gs->champType)
                fe->nextScreen = 1100;   /* nombre del piloto antes del campeonato */
            else
            {
                Fe_PlaySound(0x48);
                fe->nextScreen = 0;
                fe->portPopup = 1;       /* al terminar el fundido: Fe_StartRace */
            }
        }
        else if (Fe_PadPressed(PADB_TRIANGLE, port, 1))
        {
            g_Venue = gs->venue;
            if (gs->champType)
                gs->track = gs->savedTrack;
            fe->nextScreen = 1200;
        }
        if (d)
        {
            gs->track = (uint8_t)(gs->track + d);
            if (!Fe_TrackAvailable(gs->venue, gs->track))
                fe->previewAnim = -1;
            else if (fe->previewOffset > 0 && fe->previewAnim < 1)
                fe->previewAnim = -2;
            Fe_LoadPreview(0x200);
            fe->cursor = fe->prevCursor == 0;
        }
    }
    Fe_PadRepeatReset();

    /* entrada/salida del trazado y zoom */
    if (fe->previewAnim)
    {
        if (fe->previewAnim > 0 && fe->previewOffset == 15000)
            Fe_PlaySound(0x53);
        fe->previewOffset = (short)(fe->previewOffset + (fe->previewAnim < 1 ? g_FrameDelta * 0x140 : -g_FrameDelta * 0xa0));
        if (fe->previewOffset < 0)
        {
            fe->previewOffset = 0;
            fe->previewAnim = 0;
        }
        else if (fe->previewOffset >= 15001)
        {
            fe->previewOffset = 15000;
            fe->previewAnim = fe->previewAnim == -2 ? 1 : 0;
        }
    }
    if (fe->previewOffset == 0)
    {
        fe->previewZoom += g_FrameDelta * 0x14;
        if (fe->previewZoom > 0x57e4)
            fe->previewZoom = 0x57e4;
    }
    else
        fe->previewZoom /= 3;
    fe->previewDX = (short)(fe->previewZoom / 8);
    fe->previewDY = (short)-(fe->previewZoom / 0x7d);
    if (fe->vScrollDir > 0)
        fe->vScrollDir = (char)g_FrameDelta;
    else if (fe->vScrollDir < 0)
        fe->vScrollDir = (char)-g_FrameDelta;
    fe->modelAngle += g_FrameDelta;

    if (fe->fadeDone)
    {
        if (fe->portPopup)
            Fe_StartRace(0);
        else if (fe->nextScreen == 1100)
        {
            gs->champType = gs->venue == 7 ? 2 : (gs->venue == 8 ? 3 : 1);
            gs->raceIndex = 0;
            gs->track = gs->savedTrack;
            for (int p = 0; p < gs->numPlayers; p++)
                fe->champReady[p] = 1;
        }
    }
}

/* ---------------------------------------------------------------- montaje de la carrera */

/* 8008abb8: grupo del piloto (limite superior de su bloque) */
static int DriverGroup(int d)
{
    return d < 0x21 ? 0x21 : d < 0x27 ? 0x27 : d < 0x2e ? 0x2e : 0x38;
}

/* 8008aafc(p >= 0): hueco de la parrilla del mando p */
static int GridFindPlayer(int p)
{
    for (int i = 0; i < g_RaceGrid.count; i++)
        if ((int8_t)g_RaceGrid.car[i].player == p)
            return i;
    return 0;
}

/* Solo en el port: en una partida en red, el anfitrion pone detras de sus jugadores a los de las demas maquinas
 * (GridCar.player = numero de humano, en el orden de Net_HostRacers), cada uno con su piloto si esta libre y
 * disponible en la clase (si no, uno al azar). Devuelve el numero de humanos. */
static int Fe_AddNetHumans(int np, int cls)
{
    int machines[NET_MAX_HUMANS];
    int n = Net_HostRacers(machines);
    for (int i = 0; i < n && np < 8; i++, np++)
    {
        const NetPlayer* p = Net_Player(machines[i]);
        GridCar* e = &g_RaceGrid.car[np];
        memset(e, 0, sizeof(*e));
        e->player = (uint8_t)np;
        e->car = (uint8_t)cls;
        e->cls = g_Settings.racingClass;
        e->team = (uint8_t)np;
        int d = p ? p->driver : 0;
        for (int tries = 0; tries < 500; tries++)
        {
            int dup = 0;
            for (int j = 0; j < np; j++)
                if (g_RaceGrid.car[j].driver == d)
                    dup = 1;
            if (!dup && d < 0x21 && Fe_CarAvailable(d, cls))
                break;
            d = rand() % 0x21;
        }
        e->driver = (uint8_t)d;
    }
    return np;
}

/* 8008927c: completa la parrilla (rivales al azar, niveles, rival destacado, orden de salida), elige el
 * fichero de la pista y sale del front-end (estado 0xB). Con demo (Frontend_Update tras 30 s sin tocar nada): 6
 * coches de la IA a 1 vuelta en una sede y pista al azar, sin rival destacado; la camara sigue al ultimo de la
 * parrilla (que lleva la marca de rival). */
void Fe_StartRace(int demo)
{
    GameSettings* gs = &g_Settings;
    RaceGrid* grid = &g_RaceGrid;
    unsigned seed = (unsigned)rand() & 0xffff;
    int cls = ClassIndex();
    int np = gs->numPlayers;
    int npLocal = np;   /* port: los de esta maquina (np cuenta tambien los de la red) */

    if (!demo)
    {
        if (!gs->playerNameSet[0])
            snprintf(gs->playerName[0], sizeof(gs->playerName[0]), "%s", Fe_GetString(4000));
        if (!gs->playerNameSet[1])
            snprintf(gs->playerName[1], sizeof(gs->playerName[1]), "%s", Fe_GetString(0xfa1));
    }
    else
    {
        /* la sede y la pista elegidas se guardan (Frontend_InitScreens las devuelve al volver); sedes entre las 4
         * primeras y las extra desbloqueadas (DAT_800b7136). Poderes "Some" salvo en la tercera seguida (ninguno).
         * Port: el original elegia ademas las sedes de una copa si estaba elegida (raceIndex 0); aqui no hace falta,
         * se vuelven a elegir al empezarla. */
        gs->demo = 1;
        gs->laps = 1;
        grid->count = 6;
        grid->car[0].team = 100;   /* todos los huecos libres: los 6 al azar */
        gs->teamRace = 0;
        gs->champType = 0;
        gs->gameMode = 0;
        g_SplitScreen = 0;
        gs->curVenue = gs->venue;
        gs->curTrack = gs->track;
        gs->venue = (uint8_t)(rand() % (PROGRESS(0x800b7136u) + 4));
        gs->track = (uint8_t)(rand() % 3);
        g_GameInfo->powerUps = g_DemoCount != 3;
        np = npLocal = 0;
    }

    if (gs->champType)
    {
        if (gs->raceIndex == 0 && !gs->demo)
        {
            if (gs->champType == 2)
            {
                /* Wild Card: tres sedes al azar distintas, una pista de cada */
                gs->cupRaces = 3;
                int was4 = gs->gameMode == 4;
                if (was4)
                    gs->gameMode = 0;
                for (int i = 0; i < 3; i++)
                {
                    gs->cupTrack[i] = (uint8_t)i;
                    for (;;)
                    {
                        int v = rand() % 6;
                        if (!Fe_TrackAvailable(v, i))
                            continue;
                        int dup = 0;
                        for (int j = 0; j < i; j++)
                            if (gs->cupVenue[j] == v)
                                dup = 1;
                        if (dup)
                            continue;
                        gs->cupVenue[i] = (uint8_t)v;
                        break;
                    }
                }
                if (was4)
                    gs->gameMode = 4;
            }
            else if (gs->champType == 3)
            {
                gs->cupRaces = 6;
                for (int i = 0; i < 6; i++)
                {
                    gs->cupVenue[i] = (uint8_t)i;
                    gs->cupTrack[i] = (uint8_t)cls;
                }
            }
            else
            {
                gs->cupRaces = 3;
                for (int i = 0; i < 3; i++)
                {
                    gs->cupTrack[i] = (uint8_t)i;
                    gs->cupVenue[i] = gs->venue;
                }
            }
        }
        if (gs->raceIndex != 0)
            goto pickTrack;
    }

    /* numero de coches y vueltas segun el modo */
    if (!demo)
    {
        gs->laps = (uint8_t)gs->raceOpts1P[2];
        switch (gs->gameMode)
        {
        case 0: grid->count = (uint8_t)gs->raceOpts1P[0]; break;
        case 1: grid->count = (uint8_t)(gs->raceOpts1P[1] * 2); break;
        case 5: grid->count = 1; gs->laps = 4; break;
        case 6: grid->count = 2; gs->laps = 1; break;
        case 7: grid->count = (uint8_t)gs->raceOpts2P[0]; gs->laps = (uint8_t)gs->raceOpts2P[3]; break;
        case 8: gs->laps = (uint8_t)gs->raceOpts2P[3]; grid->count = (uint8_t)(gs->raceOpts2P[1] * 2); break;
        case 9: gs->laps = (uint8_t)gs->raceOpts2P[3]; grid->count = (uint8_t)(gs->raceOpts2P[2] * 2); break;
        case 13: grid->count = 2; gs->laps = 4; break;
        case 14: grid->count = 1; gs->laps = 1; break;
        default: grid->count = 6; gs->laps = 4; break;
        }
    }
    if (!demo && !gs->champType && Net_IsHost() && npLocal == 1)   /* (en red, un jugador por maquina) */
        np = Fe_AddNetHumans(np, cls);
    {
        const char* nc = getenv("NR_CARS");   /* solo en el port (pruebas): numero de coches (hasta 8) */
        if (nc && !demo && atoi(nc) > 0)
            grid->count = (uint8_t)(atoi(nc) < 8 ? atoi(nc) : 8);
    }
    if (grid->count < np)
        grid->count = (uint8_t)np;
    if (grid->count > 8)
        grid->count = 8;
    for (int i = np; i < grid->count; i++)
        grid->car[i].cls = gs->racingClass;

    {
        int s = np;
        while (s < grid->count && grid->car[s].team != 100)
            s++;   /* rivales y compañeros elegidos en la pantalla 1200 */
        int truckSlot = -1, legendSlot = -1;
        if (gs->champType)
        {
            truckSlot = np + rand() % (grid->count - np);
            if (gs->gameMode == 4)
                do
                    legendSlot = np + rand() % (grid->count - np);
                while (legendSlot == truckSlot);
        }
        for (; s < grid->count; s++)
        {
            GridCar* e = &grid->car[s];
            e->car = (uint8_t)cls;
            for (;;)
            {
                int d;
                if (s == legendSlot)
                {
                    legendSlot = 0;
                    d = gs->champType == 2 ? 0x2d : gs->cupVenue[gs->raceIndex] + 0x27;
                }
                else if (s == truckSlot)
                {
                    truckSlot = 0;
                    d = rand() % 6 + 0x21;
                }
                else if (!gs->champType)
                    d = (gs->gameMode == 6 || gs->gameMode == 14) ? rand() % 0x2e : rand() % 0x38;
                else
                    d = rand() % 0x21;
                e->driver = (uint8_t)d;
                if (!Fe_CarAvailable(d, e->car))
                    continue;
                int dup = 0;
                for (int j = 0; j < s; j++)
                    if (grid->car[j].driver == d)
                        dup = 1;
                if (dup)
                    continue;
                int grp = DriverGroup(d);
                if (grp != 0x21 && s > np)
                {
                    int same = 0;
                    for (int j = np; j < s; j++)
                        if (DriverGroup(grid->car[j].driver) == grp)
                            same = 1;
                    if (same)
                        continue;   /* como mucho uno de cada grupo especial */
                }
                break;
            }
            e->player = 0xff;
            e->team = (uint8_t)s;
        }
    }

    /* un tercio de la IA baja un nivel (no el mejor piloto) */
    if (gs->racingClass && !gs->teamRace)
    {
        int best = 0, bestSkill = 0;
        for (int i = np; i < grid->count; i++)
            if (grid->car[i].player == 0xff && bestSkill < g_Drivers[grid->car[i].driver].stat7)
            {
                best = i;
                bestSkill = g_Drivers[grid->car[i].driver].stat7;
            }
        /* port: en red puede haber pocos coches de la IA (o ninguno): como mucho uno por coche de la IA, y sin
         * dividir por cero (el original siempre tenia al menos 4) */
        int lower = grid->count / 3;
        if (lower > grid->count - np)
            lower = grid->count - np;
        for (int k = 0; k < lower; k++)
        {
            int u;
            int guard = 0;
            do
                u = np + rand() % (grid->count - np);
            while (((DriverGroup(grid->car[u].driver) == 0x2e && gs->gameMode == 4) ||
                    (gs->gameMode != 4 && u == best) || grid->car[u].cls != gs->racingClass) && ++guard < 1000);
            if (guard < 1000)   /* (port: sin candidato no baja a nadie; con la IA del original nunca pasa) */
                grid->car[u].cls--;
        }
    }

    /* orden de salida */
    if (!gs->champType)
    {
        for (int i = 0; i < grid->count; i++)
            g_CarRecords[i].taken = 0;
        for (int i = 0; i < grid->count; i++)
        {
            unsigned u;
            do
                u = ((unsigned)rand() + seed++) % grid->count;
            while (g_CarRecords[u].taken);
            g_CarRecords[i].startPos = (uint8_t)u;
            g_CarRecords[u].taken = 1;
        }
    }
    else
    {
        for (int i = 0; i < 8; i++)
        {
            g_CarRecords[i].points = 0;
            g_CarRecords[i].d = 0;
        }
        for (int i = 0; i < grid->count; i++)
            g_CarRecords[i].startPos = (uint8_t)(grid->count - i - 1);
    }

    int teamMode = 1;
    if (gs->teamRace)
    {
        if (gs->gameMode == 12 || gs->gameMode == 9)
        {
            static const uint8_t teams[6] = { 0, 1, 0, 1, 2, 2 };
            for (int i = 0; i < 6; i++)
                grid->car[i].team = teams[i];
        }
        else
        {
            for (int i = 0; i < grid->count / 2; i++)
            {
                grid->car[i * 2].team = (uint8_t)i;
                grid->car[i * 2 + 1].team = (uint8_t)i;
            }
            if (gs->gameMode == 8 || gs->gameMode == 11)
                teamMode = 2;
        }
    }
    for (int i = 0; i < grid->count; i++)
        grid->car[i].flag = 0;
    if (gs->gameMode == 6)
        grid->car[1].flag = 1;
    else if (gs->gameMode != 4 && !demo)
    {
        /* rival destacado: el mejor de la IA de la clase que no sea del equipo de los jugadores */
        int other = np == 2 ? grid->car[1].team : -1;
        int rival = -1, bestSkill = 0;
        for (int i = 0; i < grid->count; i++)
        {
            GridCar* e = &grid->car[i];
            if (e->player != 0xff || e->cls != gs->racingClass || e->team == grid->car[0].team || e->team == other)
                continue;
            if (bestSkill < g_Drivers[e->driver].stat7)
            {
                rival = i;
                bestSkill = g_Drivers[e->driver].stat7;
            }
        }
        if (rival != -1)
        {
            grid->car[rival].flag = 1;
            if (gs->champType && teamMode > 1)
                for (int j = 1; j < grid->count; j++)
                    if (j != rival && grid->car[j].team == grid->car[rival].team)
                        grid->car[j].flag = 1;
        }
    }

    /* la parrilla y los registros pasan al orden de salida */
    {
        GridCar cars[8];
        CarRecord recs[8];
        memset(recs, 0, sizeof(recs));
        for (int i = 0; i < grid->count; i++)
        {
            int pos = g_CarRecords[i].startPos;
            cars[pos] = grid->car[i];
            recs[pos] = g_CarRecords[i];
        }
        for (int i = 0; i < grid->count; i++)
        {
            grid->car[i] = cars[i];
            g_CarRecords[i] = recs[i];
        }
    }
    {
        int pn = 1;
        for (int i = 0; i < grid->count; i++)
        {
            GridCar* e = &grid->car[i];
            e->model = (uint8_t)(e->car + g_Drivers[e->driver].team * 3);
            if (e->player == 0)
                e->playerNum = 0;
            else
            {
                e->playerNum = (uint8_t)pn++;
                if (gs->gameMode == 4 && (unsigned)(e->driver - 0x27) < 7)
                    e->flag = 1;
            }
        }
    }
    if (demo)
    {
        /* el ultimo de la parrilla: numero 0, marca de rival y la clase de la carrera */
        GridCar* e = &grid->car[grid->count - 1];
        e->playerNum = 0;
        e->flag = 1;
        e->cls = gs->racingClass;
        g_GameInfo->unlockMask = 0;
    }
    else
    {
        g_PlayerTeam[0] = grid->car[GridFindPlayer(0)].team;
        if (np == 2)
            g_PlayerTeam[1] = grid->car[GridFindPlayer(1)].team;
        {
            static const uint8_t specials[10] = { 47, 48, 49, 50, 51, 52, 53, 54, 55, 46 };   /* 800abeec */
            uint32_t mask = 0;
            for (int k = 0; k < 10; k++)
                if (PROGRESS(0x800b7121u + specials[k]) == 1)
                    mask |= 1u << k;
            g_GameInfo->unlockMask = (int)mask;
        }
    }

pickTrack:
    for (int i = 0; i < 8; i++)
    {
        g_CarRecords[i].b = 0;
        g_CarRecords[i].a = 0;
        g_CarRecords[i].startPos = 0;
    }
    g_GameInfo->unk5c = 0;
    g_GameInfo->numPlayers = np;
    g_GameInfo->racingClass = 1;
    if (!demo)
    {
        /* 8008a370: el tipo de cada mando y los botones del esquema de cada jugador (pantalla 1000) */
        Pad_SetupControls(-1, 0, 0);
        for (int p = 0; p < npLocal; p++)
            Pad_SetupControls(p, (int8_t)g_SaveExtra.padKind[p], gs->transmission[p]);
        g_GameInfo->powerUps = (gs->gameMode == 5 || gs->gameMode == 13) ? 0 : gs->powerUps;
        g_SplitScreen = npLocal == 2;
    }

    int venue, track;
    if (!gs->champType)
    {
        venue = gs->venue;
        track = gs->track;
    }
    else
    {
        gs->curVenue = gs->cupVenue[gs->raceIndex];
        gs->curTrack = gs->cupTrack[gs->raceIndex];
        gs->raceIndex++;
        venue = gs->curVenue;
        track = gs->curTrack;
    }
    g_Venue = (short)venue;
    snprintf(g_SelectedTrack, sizeof(g_SelectedTrack), "%s", s_trackFiles[venue][track + 1]);
    snprintf(g_TrackName, sizeof(g_TrackName), "%s", Fe_GetString(5000 + venue * 10 + track + 1));
    NR_Log("Carrera: %s (%s, sede %s), %d coches, %d vueltas\n", g_TrackName, g_SelectedTrack,
           s_trackFiles[venue][0], grid->count, gs->laps);
    for (int i = 0; i < grid->count; i++)
        NR_Log("  %d: %-20s clase %d nivel %d equipo %d %s%s\n", i + 1, g_Drivers[grid->car[i].driver].name,
               grid->car[i].car, grid->car[i].cls, grid->car[i].team,
               grid->car[i].player == 0xff ? "IA" : "JUGADOR", grid->car[i].flag ? " (rival)" : "");
    if (np > npLocal)
        Net_HostStartRace();   /* port: manda la carrera a las demas maquinas */
    g_GameState = 0xb;
}

/* Solo en el port: cliente de una partida en red. Pone la carrera que ha mandado el anfitrion (Net_Setup: ajustes,
 * parrilla en el orden de salida) y sale del front-end como Fe_StartRace. Los ajustes de esta maquina se recuperan
 * al acabar (Net_RaceEnd). */
void Fe_NetStartRace(void)
{
    GameSettings* gs = &g_Settings;
    const NetRaceSetup* s = Net_Setup();
    gs->venue = s->venue;
    gs->track = s->track;
    gs->laps = s->laps;
    gs->racingClass = s->racingClass;
    gs->teamRace = s->teamRace;
    gs->gameMode = s->gameMode;
    gs->champType = 0;
    gs->demo = 0;
    gs->numPlayers = 1;
    g_RaceGrid.count = s->gridCount;
    memcpy(g_RaceGrid.car, s->grid, sizeof(g_RaceGrid.car));
    memset(g_CarRecords, 0, sizeof(g_CarRecords));
    g_GameInfo->unk5c = 0;
    g_GameInfo->numPlayers = s->numHumans;
    g_GameInfo->racingClass = 1;
    g_GameInfo->powerUps = s->powerUps;
    g_GameInfo->unlockMask = (int)s->unlockMask;
    for (int i = 0; i < g_RaceGrid.count; i++)
        if (g_RaceGrid.car[i].player != 0xff && Net_HumanView(g_RaceGrid.car[i].player) == 0)
            g_PlayerTeam[0] = g_RaceGrid.car[i].team;
    Pad_SetupControls(-1, 0, 0);
    Pad_SetupControls(0, (int8_t)g_SaveExtra.padKind[0], gs->transmission[0]);
    g_SplitScreen = 0;
    int venue = s->venue, track = s->track;
    g_Venue = (short)venue;
    snprintf(g_SelectedTrack, sizeof(g_SelectedTrack), "%s", s_trackFiles[venue][track + 1]);
    snprintf(g_TrackName, sizeof(g_TrackName), "%s", Fe_GetString(5000 + venue * 10 + track + 1));
    NR_Log("Carrera en red: %s (%s), %d coches, %d vueltas, %d humanos\n", g_TrackName, g_SelectedTrack,
           g_RaceGrid.count, gs->laps, s->numHumans);
    g_GameState = 0xb;
}
