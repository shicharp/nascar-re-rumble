#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "mem.h"
#include "gfx.h"
#include "gte.h"
#include "render.h"
#include "texslot.h"
#include "track.h"

Track* g_Track;

#define TAG_TCOL 0x54434f4cu
#define TAG_TCRV 0x54435256u
#define TAG_THOR 0x54484f52u
#define TAG_TIGR 0x54494752u
#define TAG_TSEG 0x54534547u
#define TAG_TSUN 0x5453554eu
#define TAG_TTEX 0x54544558u
#define TAG_TTXA 0x54545841u

#define MAX_SEGS 188    /* DAT_800b62a0: 0x2F palabras de banderas por vista */

/* Vertice transformado (20 bytes en DAT_800af908 / en la pila de la subdivision) */
typedef struct
{
    int16_t  vx, vy, vz;    /* +0x00 relativo a la camara (antes de rotar) */
    uint16_t uv;            /* +0x06 (solo al subdividir) */
    uint32_t sxy;           /* +0x08 */
    int16_t  sz;            /* +0x0C */
    uint16_t flags;         /* +0x0E ver abajo */
    uint32_t color;         /* +0x10 (solo al subdividir) */
#if USE_PGXP
    VERTTYPE px, py;        /* solo en el port: proyeccion precisa de la GTE emulada */
#endif
} TVert;

/* Banderas de vertice. Lejos: 0x20 no valido, 0x01 valido, 0x40 x en pantalla, 0x80 y en pantalla.
 * Cerca: 0x100/0x200 fuera por la izquierda/derecha, 0x800/0x400 por arriba/abajo, 0x40/0x80 dentro en
 * x/y, 0x01 lejano, 0x10/0x18/0x1c/0x1e cuantas veces hay que subdividir, 0x20 demasiado cerca. */

static TVert   s_verts[256];            /* DAT_800af908 */
static uint8_t s_segFlags[2][MAX_SEGS]; /* DAT_800b62a0: 0x80 visible, 0x40 cerca, 0-3 nivel de mip */
static uint8_t s_viewStart[2], s_viewCount[2], s_viewHorizon[2];   /* DAT_800b6358/59/5a */
static uint16_t s_lodDist[3];           /* DAT_800af8f8/fa/fc */

/* Estado de la cara que se subdivide (scratchpad 1f80000c-11) */
static uint32_t s_subTex;               /* clut | semitransparente (0x8000) | tpage << 16 */
static int      s_subMirror;            /* 1f800010 */
static int      s_subBias;              /* 1f800011: 0xFF, o 0xBF con el bit 0x2000 */

/* ---------------------------------------------------------------- carga */

/* FUN_80067218: TTEX (ya reordenado a {uv0, modo, uv1, slot, uv2, uv3}) -> 4 tablas por nivel de mip con
 * tpage y CLUT reales. La del nivel 0 se escribe encima del propio TTEX. */
static void Track_BuildTexTables(int n, uint8_t* src, uint8_t* l1, uint8_t* l2, uint8_t* l3)
{
    uint8_t* out[4] = { src, l1, l2, l3 };
    for (; n > 0; n--)
    {
        uint8_t u0 = src[0], v0 = src[1], u1 = src[4], v1 = src[5];
        uint8_t u2 = src[8], v2 = src[9], u3 = src[10], v3 = src[11];
        int semi = (int16_t)NR_Read16(src + 2);
        int slot = NR_Read16(src + 6);
        const uint8_t* region = Cpag_FindRegion(slot, u0, v0);
        for (int lv = 0; lv < 4; lv++)
        {
            int du = 0, dv = 0;
            int tp = TexSlot_GetTPage(slot, lv, &du, &dv);
            if (tp < 0)
                tp = 0;
            uint8_t* o = out[lv];
            o[0] = (uint8_t)(du + (u0 >> lv));
            o[1] = (uint8_t)(dv + (v0 >> lv));
            o[4] = (uint8_t)(du + (u1 >> lv));
            o[5] = (uint8_t)(dv + (v1 >> lv));
            o[8] = (uint8_t)(du + (u2 >> lv));
            o[9] = (uint8_t)(dv + (v2 >> lv));
            o[10] = (uint8_t)(du + (u3 >> lv));
            o[11] = (uint8_t)(dv + (v3 >> lv));
            uint16_t clut = region ? NR_Read16(region + 4 + lv * 2) : 0;
            uint16_t tpage = (uint16_t)tp;
            if (semi != 0)
                tpage |= (uint16_t)((semi - 1) * 0x20);
            o[2] = (uint8_t)clut;
            o[3] = (uint8_t)(clut >> 8);
            o[6] = (uint8_t)tpage;
            o[7] = (uint8_t)(tpage >> 8);
            out[lv] += 12;
        }
        src = out[0];
    }
}

void Track_Free(void)
{
    if (!g_Track)
        return;
    free(g_Track->segs);
    free(g_Track->tex[1]);
    free(g_Track->colors);
    free(g_Track);
    g_Track = NULL;
}

/* 800673f8: tpage/CLUT reales de las caras del horizonte (+0x0A: byte alto = texturizada, bajo = modo
 * semitransparente + 1; +0x0E: slot) y los colores c2/c3 al orden de la primitiva */
static void Track_PatchHorizonFaces(int n, uint8_t* f)
{
    for (; n > 0; n--, f += 20)
    {
        uint16_t info = NR_Read16(f + 10);
        uint16_t clut = 0, tpage = 0;
        if (info >> 8)
        {
            int slot = NR_Read16(f + 14);
            const uint8_t* r = Cpag_FindRegion(slot, f[8], f[9]);
            clut = r ? NR_Read16(r + 4) : 0;
            int du, dv, tp = TexSlot_GetTPage(slot, 0, &du, &dv);
            tpage = (uint16_t)(tp < 0 ? 0 : tp);
            if (info & 0xff)
            {
                tpage |= (uint16_t)(((info & 0xff) - 1) * 0x20);
                clut |= 0x8000;
            }
        }
        f[10] = (uint8_t)clut;
        f[11] = (uint8_t)(clut >> 8);
        f[14] = (uint8_t)tpage;
        f[15] = (uint8_t)(tpage >> 8);
        uint8_t t = f[6];
        f[6] = f[7];
        f[7] = t;
    }
}

/* 80062004: las caras con los 4 colores iguales se dibujan planas (bit 0x800 de +0x0E) */
static int Track_MarkFlatFaces(int n, uint8_t* f)
{
    int count = 0;
    for (; n > 0; n--, f += 20)
        if (f[4] == f[5] && f[4] == f[6] && f[4] == f[7])
        {
            f[15] |= 0x08;
            count++;
        }
    return count;
}

/* 80062088. Pendiente: TTXA (texturas animadas FUN_80061dfc) y la tabla de TCRV DAT_800af8e8. */
void Track_Parse(uint8_t* data, int size)
{
    Track_Free();
    Track* t = (Track*)calloc(1, sizeof(Track));
    g_Track = t;
    t->bbMin[0] = t->bbMin[1] = t->bbMin[2] = 0x40000000;
    t->bbMax[0] = t->bbMax[1] = t->bbMax[2] = -0x40000000;
    int maxSegs = 0;
    uint8_t* ttex = NULL;
    for (uint8_t* c = data; c + 8 <= data + size;)
    {
        uint32_t tag = NR_Read32(c), len = NR_Read32(c + 4);
        if (len < 8)
            break;
        switch (tag)
        {
        case TAG_TCRV:
            maxSegs = NR_Read16(c + 8);
            t->curve = c + 0xc;
            t->numCurve = (int)(len - 0xc) / 10;
            t->segs = (TrackSeg*)calloc((size_t)maxSegs, sizeof(TrackSeg));
            break;
        case TAG_TCOL:
        {
            int n = (int)(len - 8) >> 2;
            t->colors = (uint32_t*)malloc((size_t)n * 4);
            for (int i = 0; i < n; i++)
            {
                const uint8_t* p = c + 8 + i * 4;   /* b, g, r -> palabra RGB de la GPU */
                t->colors[i] = (uint32_t)p[2] | (uint32_t)p[1] << 8 | (uint32_t)p[0] << 16;
            }
            break;
        }
        case TAG_TTEX:
        {
            t->numTex = (int)(len - 8) / 12;
            ttex = c + 8;
            /* {uv0 uv1 uv2 uv3, u32 pagina} -> {uv0, pagina >> 16, uv1, pagina & 0xFFFF, uv2, uv3} */
            for (int i = 0; i < t->numTex; i++)
            {
                uint8_t* e = ttex + i * 12;
                uint8_t tmp[12];
                memcpy(tmp, e, 12);
                e[2] = tmp[10]; e[3] = tmp[11];
                e[4] = tmp[2];  e[5] = tmp[3];
                e[6] = tmp[8];  e[7] = tmp[9];
                e[8] = tmp[4];  e[9] = tmp[5];
                e[10] = tmp[6]; e[11] = tmp[7];
            }
            uint8_t* mips = (uint8_t*)malloc((size_t)t->numTex * 36 + 1);
            t->tex[0] = ttex;
            t->tex[1] = mips;
            t->tex[2] = mips + t->numTex * 12;
            t->tex[3] = mips + t->numTex * 24;
            Track_BuildTexTables(t->numTex, ttex, t->tex[1], t->tex[2], t->tex[3]);
            break;
        }
        case TAG_TSEG:
        {
            if (!t->segs || t->numSegs >= maxSegs || t->numSegs >= MAX_SEGS)
                break;
            TrackSegHdr* h = (TrackSegHdr*)c;
            TrackSeg* s = &t->segs[t->numSegs++];
            s->hdr = h;
            s->d = c + 0x4c + h->numC * 8;
            s->verts = (const uint32_t*)(s->d + h->numD * 4);
            s->faces = (const uint8_t*)(s->verts + h->numVerts);
            s->rest = s->faces + h->numFaces * 10;
            for (int a = 0; a < 3; a++)
            {
                if (h->origin[a] < t->bbMin[a])
                    t->bbMin[a] = h->origin[a];
                if (h->origin[a] + h->half[a] * 2 > t->bbMax[a])
                    t->bbMax[a] = h->origin[a] + h->half[a] * 2;
            }
            break;
        }
        case TAG_TSUN:
        {
            int pos[3] = { (int16_t)NR_Read16(c + 8) << 7, (int16_t)NR_Read16(c + 10) << 7,
                           (int16_t)NR_Read16(c + 12) << 7 };
            Light_SetSun(pos, c[0x10], c[0x11], c[0x12]);   /* c[0x13]: destello (pendiente) */
            break;
        }
        case TAG_THOR:
            t->horizon = c;
            if (t->numHorizons < TRACK_MAX_HORIZONS)
            {
                TrackHorizon* h = &t->horizons[t->numHorizons++];
                h->hdr = c;
                h->numVerts = c[0x1c];
                h->numFaces = c[0x1d];
                h->bit = c[0x1e];
                h->verts = c + 0x20;
                h->faces = h->verts + h->numVerts * 8;
                Track_PatchHorizonFaces(h->numFaces, (uint8_t*)h->faces);
                Track_MarkFlatFaces(h->numFaces, (uint8_t*)h->faces);
            }
            break;
        case TAG_TIGR:
            t->tigr = c;
            break;
        case TAG_TTXA:
            break;              /* TODO: FUN_80061dfc */
        }
        c += len;
    }
    NR_Log("Track_Parse: %d segmentos, %d texturas, %d puntos de TCRV, %d horizontes\n", t->numSegs, t->numTex,
           t->numCurve, t->numHorizons);
}

void Track_GetCurvePoint(int i, int* out)
{
    const Track* t = g_Track;
    if (!t || !t->curve || t->numCurve <= 0)
    {
        out[0] = out[1] = out[2] = 0;
        return;
    }
    i %= t->numCurve;
    if (i < 0)
        i += t->numCurve;
    const uint8_t* p = t->curve + i * 10;
    out[0] = NR_Read16(p) << 5;      /* u16 * 2 en unidades de la pista */
    out[1] = NR_Read16(p + 2) << 5;
    out[2] = NR_Read16(p + 4) << 5;
}

int Track_FindSegment(const int* pos)
{
    const Track* t = g_Track;
    int best = 0;
    int64_t bestD = INT64_MAX;
    for (int i = 0; t && i < t->numSegs; i++)
    {
        const TrackSegHdr* h = t->segs[i].hdr;
        int64_t d = 0;
        for (int a = 0; a < 3; a++)
        {
            int64_t e = (int64_t)(h->origin[a] + h->half[a]) - (pos[a] >> 4);
            d += e * e;
        }
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

/* ---------------------------------------------------------------- visibilidad */

/* 800641b8 */
static void Track_SetLodDistances(int small)
{
    unsigned d = (unsigned)Render_GetProjection()->h * 4;
    if (small)
    {
        int a = (int)d - ((int)d >> 3);
        d = (unsigned)(a - (a / 16));
    }
    s_lodDist[0] = (uint16_t)d;
    s_lodDist[1] = (uint16_t)((s_lodDist[0] * 5) >> 1);
    s_lodDist[2] = (uint16_t)((s_lodDist[1] * 5) >> 1);
}

/* 80063a34 */
static int Track_SphereInFrustum(const int16_t* c, int r)
{
    if (c[2] * 0x10 + r <= 0x600)
        return 0;
    const RenderProj* pr = Render_GetProjection();
    int ax = c[0] * 0x10;
    if (ax < 0)
        ax = -ax;
    int z8 = c[2] * 0x80;
    if ((ax << pr->sx0) + (ax << pr->sx1) > z8 + pr->rx * r)
        return 0;
    int ay = c[1] * 0x10;
    if (ay < 0)
        ay = -ay;
    return (ay << pr->sy0) + (ay << pr->sy1) <= z8 + pr->ry * r;
}

/* 80063928: centro de la caja respecto a la camara; dist = z - radio */
static int Track_SegVisible(const TrackSegHdr* h, int* dist)
{
    int d[3];
    for (int a = 0; a < 3; a++)
    {
        d[a] = h->half[a] + h->origin[a] - (g_Camera.pos[a] >> 4);
        if (abs(d[a]) >= 0x7f00)
            return 0;
    }
    /* 800638e4 (la rotacion de la camara ya esta cargada) */
    Gte_SetV0(d[0], d[1], d[2]);
    doCOP2(GTE_OP_RTV0);
    int16_t c[3] = { (int16_t)MFC2_S(9), (int16_t)-MFC2_S(10), (int16_t)MFC2_S(11) };
    *dist = c[2] - h->radius;
    return Track_SphereInFrustum(c, h->radius << 4);
}

/* 800637bc */
static void Track_MarkVisible(int start, int count, uint8_t* flags, uint8_t mask)
{
    const Track* t = g_Track;
    while (count > 0)
    {
        count--;
        int i = start + count;
        if (i >= t->numSegs)
            i -= t->numSegs;
        const TrackSegHdr* h = t->segs[i].hdr;
        int dist;
        if (!(mask & h->viewMask) || !Track_SegVisible(h, &dist))
            continue;
        uint8_t f = dist < 0x200 ? 0xc0 : 0x80;
        uint8_t lod;
        if (dist > s_lodDist[1])
            lod = dist <= s_lodDist[2] ? 2 : 3;
        else
            lod = dist > s_lodDist[0];
        flags[i] = f | lod;
    }
}

/* 800644c0. Port: la camara es g_Camera (preset ya aplicado) y curSeg el segmento en el que esta. */
void Track_DrawView(int view, int curSeg, int mirrorCam)
{
    const Track* t = g_Track;
    if (!t || t->numSegs == 0)
        return;
    Render_LoadCameraRotation();
    Track_SetLodDistances(mirrorCam);   /* TODO: tambien con pantalla partida (DAT_800af708) */
    memset(s_segFlags[view], 0, sizeof(s_segFlags[view]));
    const TrackSegHdr* h = t->segs[curSeg].hdr;
    int extra = (mirrorCam ? -4 : 1) + ((Render_GetProjection()->h - 0x78) >> 9);
    int ahead = (h->visRange & 0xf) + extra;
    if (ahead < 4)
        ahead = 4;
    int behind = (h->visRange >> 4) + extra;
    if (behind < 4)
        behind = 4;
    int start = curSeg - behind;
    int count = behind + ahead + 1;
    s_viewHorizon[view] = h->horizon;
    if (start < 0)
        start += t->numSegs;
    if (count > t->numSegs)
        count = t->numSegs;
    Track_MarkVisible(start, count, s_segFlags[view], h->viewMask);
    s_viewStart[view] = (uint8_t)start;
    s_viewCount[view] = (uint8_t)count;
}

/* ---------------------------------------------------------------- vertices */

static void StorePrecise(TVert* v)
{
#if USE_PGXP
    v->px = g_FP_SXYZ2.x;
    v->py = g_FP_SXYZ2.y;
#else
    (void)v;
#endif
}

/* Banderas de un vertice cercano a partir de la RTPS recien hecha (comun a 80063470 y 800626b8) */
static uint16_t NearFlags(TVert* v, int x, int y, int z)
{
    const RenderProj* pr = Render_GetProjection();
    uint32_t flag = CFC2(31);
    int ir1 = (int16_t)MFC2_S(9), ir2 = (int16_t)MFC2_S(10);
    int sz = MFC2_S(19);
    if (flag & 0x20000)
    {
        /* division desbordada: se repite a escala 16 para tener una SXY usable */
        Gte_SetV0(x << 4, y << 4, z << 4);
        doCOP2(GTE_OP_RTPS);
    }
    v->sz = (int16_t)sz;
    uint16_t f = 0;
    int ex = (ir1 << pr->sx1) + (ir1 << pr->sx0);
    int ey = (ir2 << pr->sy1) + (ir2 << pr->sy0);
    int z8 = sz * 8;
    if (z8 < ex)
        f = 0x200;
    if (ex < -z8)
        f |= 0x100;
    if (ey < -z8)
        f |= 0x800;
    if (z8 < ey)
        f |= 0x400;
    flag = CFC2(31);
    uint32_t sxy = MFC2(14);
    int lv = (((z8 >> 3) << pr->lod[1]) + ((z8 >> 3) << pr->lod[0])) >> 5;
    if ((flag & 0xffffe000) || lv < 4)
        return f | 0x20;
    uint16_t sub;
    if (lv < 0x100)
        sub = lv < 0x40 ? 0x1e : lv < 0x80 ? 0x1c : 0x18;
    else
        sub = lv < 0x200 ? 0x10 : 1;
    v->sxy = sxy;
    StorePrecise(v);
    f |= sub;
    if (!(f & 0x300))
        f |= 0x40;
    if (!(f & 0xc00))
        f |= 0x80;
    return f;
}

/* 80063470 */
static void Track_TransformSegVerts(const TrackSeg* s, int nearMode)
{
    const TrackSegHdr* h = s->hdr;
    int tx = h->origin[0] - (g_Camera.pos[0] >> 4);
    int ty = h->origin[1] - (g_Camera.pos[1] >> 4);
    int tz = h->origin[2] - (g_Camera.pos[2] >> 4);
    TVert* v = s_verts;
    for (int i = 0; i < h->numVerts; i++, v++)
    {
        uint32_t p = s->verts[i];
        int x = (int16_t)(tx + ((p >> 19) & 0x1ffc));
        int y = (int16_t)(ty + (p & 0x3ff) * 4);
        int z = (int16_t)(tz + ((p >> 8) & 0x1ffc));
        Gte_SetV0(x, y, z);
        doCOP2(GTE_OP_RTPS);
        if (!nearMode)
        {
            uint32_t sxy = MFC2(14);
            int sz = MFC2_S(19);
            uint32_t flag = CFC2(31);
            v->sz = (int16_t)sz;
            uint16_t f = 0x20;
            if (!(flag & 0xffffe000) && sz > 3)
            {
                f = 1;
                v->sxy = sxy;
                StorePrecise(v);
                if ((unsigned)(int16_t)sxy < 2u * GFX_SCREEN_W / 2)
                    f = 0x41;
                if ((unsigned)((int32_t)sxy >> 16) < 2u * GFX_SCREEN_H / 2)
                    f |= 0x80;
            }
            v->flags = f;
        }
        else
        {
            v->vx = (int16_t)x;
            v->vy = (int16_t)y;
            v->vz = (int16_t)z;
            v->flags = NearFlags(v, x, y, z);
        }
    }
}

/* 800626b8: proyecta n vertices intermedios de la subdivision */
static void Track_TransformSubVerts(TVert* v, int n)
{
    for (; n > 0; n--, v++)
    {
        Gte_SetV0(v->vx, v->vy, v->vz);
        doCOP2(GTE_OP_RTPS);
        v->flags = NearFlags(v, v->vx, v->vy, v->vz);
    }
}

/* ---------------------------------------------------------------- primitivas */

#define SX(v) ((int16_t)(v))
#define SY(v) ((int16_t)((v) >> 16))
#define RGBW(p, n, c) ((p)->r##n = (uint8_t)(c), (p)->g##n = (uint8_t)((c) >> 8), (p)->b##n = (uint8_t)((c) >> 16))
#if USE_PGXP
#define PRECISE(p, n, v) ((p)->x##n = (v)->px, (p)->y##n = (v)->py)
#else
#define PRECISE(p, n, v) ((void)0)
#endif
#define PGXP_NONE 0xffff

static int NClip(uint32_t a, uint32_t b, uint32_t c)
{
    int x0 = SX(a), y0 = SY(a), x1 = SX(b), y1 = SY(b), x2 = SX(c), y2 = SY(c);
    return x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1;
}

/* 80062888: GT4 de la subdivision (a, b, d, c en la primitiva) */
static void Track_EmitSubQuad(const TVert* a, const TVert* b, const TVert* c, const TVert* d)
{
    POLY_GT4* p = (POLY_GT4*)Gfx_AllocPrim(sizeof(POLY_GT4));
    if (!p)
        return;
    setPolyGT4(p);
    setXY4(p, SX(a->sxy), SY(a->sxy), SX(b->sxy), SY(b->sxy), SX(d->sxy), SY(d->sxy), SX(c->sxy), SY(c->sxy));
    PRECISE(p, 0, a);
    PRECISE(p, 1, b);
    PRECISE(p, 2, d);
    PRECISE(p, 3, c);
    setUV4(p, a->uv & 0xff, a->uv >> 8, b->uv & 0xff, b->uv >> 8, d->uv & 0xff, d->uv >> 8, c->uv & 0xff, c->uv >> 8);
    p->clut = (u_short)(s_subTex & 0x7fff);
    p->tpage = (u_short)(s_subTex >> 16);
    RGBW(p, 0, a->color);
    RGBW(p, 1, b->color);
    RGBW(p, 2, d->color);
    RGBW(p, 3, c->color);
    if (s_subTex & 0x8000)
        setSemiTrans(p, 1);
    int depth = s_subBias + ((a->sz + b->sz + c->sz + d->sz) >> 2);
    Gfx_AddPrimDepthPgxp(depth, p, PGXP_NONE);
}

static uint32_t AvgColor(uint32_t a, uint32_t b)
{
    return ((a >> 1) & 0x7f7f7f7f) + ((b >> 1) & 0x7f7f7f7f);
}

static uint16_t AvgUV(uint16_t a, uint16_t b)
{
    return (uint16_t)(((a >> 1) & 0x7f7f) + ((b >> 1) & 0x7f7f));
}

static void Midpoint(TVert* m, const TVert* a, const TVert* b)
{
    m->vx = (int16_t)((a->vx + b->vx) >> 1);
    m->vy = (int16_t)((a->vy + b->vy) >> 1);
    m->vz = (int16_t)((a->vz + b->vz) >> 1);
    m->uv = AvgUV(a->uv, b->uv);
    m->color = AvgColor(a->color, b->color);
}

/* Relleno de la grieta entre una arista partida y la vecina sin partir (80062888 con un triangulo) */
static void CrackFill(const TVert* a, const TVert* m, const TVert* b)
{
    uint16_t f = a->flags | b->flags | m->flags;
    if (!(f & 0x22) && (f & 0xc0) == 0xc0)
        Track_EmitSubQuad(a, m, b, b);
}

/* 80062970: cuadrilatero p1..p4 en orden de contorno; se parte en 4 hasta 'level' veces mientras algun
 * vertice este demasiado cerca. Devuelve 1 si se dibujo sin partir. */
static int Track_Subdivide(TVert* p1, TVert* p2, TVert* p3, TVert* p4, int level)
{
    if (p1->flags & p2->flags & p3->flags & p4->flags & 0xf00)
        return 0;
    uint16_t any = p1->flags | p2->flags | p3->flags | p4->flags;
    if (any & 0x3e)
    {
        if (any & 0x20)
        {
            if (!(any & 0x1f) || level == 0)
                return 0;
        }
        if (level != 0 && ((any & 0x20) || (any & (1 << level))))
        {
            TVert m[5];     /* m01, m12, m23, m30, centro */
            Midpoint(&m[0], p1, p2);
            Midpoint(&m[1], p2, p3);
            Midpoint(&m[2], p3, p4);
            Midpoint(&m[3], p4, p1);
            m[4].vx = (int16_t)((m[0].vx + m[2].vx) >> 1);
            m[4].vy = (int16_t)((m[0].vy + m[2].vy) >> 1);
            m[4].vz = (int16_t)((m[0].vz + m[2].vz) >> 1);
            uint16_t uA = (uint16_t)(((p1->uv >> 1) & 0x7f7f) + ((p2->uv >> 1) & 0x7f7f));
            uint16_t uB = (uint16_t)(((p3->uv >> 1) & 0x7f7f) + ((p4->uv >> 1) & 0x7f7f));
            m[4].uv = AvgUV(uA, uB);
            m[4].color = AvgColor(m[0].color, m[2].color);
            Track_TransformSubVerts(m, 5);
            int r = Track_Subdivide(p1, &m[0], &m[4], &m[3], level - 1);
            r |= Track_Subdivide(&m[0], p2, &m[1], &m[4], level - 1) << 1;
            r |= Track_Subdivide(&m[4], &m[1], p3, &m[2], level - 1) << 2;
            r |= Track_Subdivide(&m[3], &m[4], &m[2], p4, level - 1) << 3;
            if (!r)
                return 0;
            if (r & 3)
                CrackFill(p1, &m[0], p2);
            if (r & 6)
                CrackFill(p2, &m[1], p3);
            if (r & 0xc)
                CrackFill(p3, &m[2], p4);
            if (r & 9)
                CrackFill(p4, &m[3], p1);
            return 0;
        }
    }
    int nc = NClip(p1->sxy, p2->sxy, p4->sxy);
    if (s_subMirror)
        nc = -nc;
    if (nc < 1)
    {
        nc = NClip(p3->sxy, p2->sxy, p4->sxy);
        if (s_subMirror)
            nc = -nc;
        if (nc >= 0)
            return 0;
    }
    Track_EmitSubQuad(p1, p2, p3, p4);
    return 1;
}

/* 80062de0 */
static void Track_DrawSegFaces(const TrackSeg* s, int nearMode, int lod, int mirror)
{
    const Track* t = g_Track;
    const uint32_t* col = t->colors;
    const uint8_t* texTable = t->tex[lod];
    if (!col || !texTable)
        return;
    s_subMirror = mirror;
    const uint8_t* f = s->faces;
    for (int fi = 0; fi < s->hdr->numFaces; fi++, f += 10)
    {
        TVert* a = &s_verts[f[0]];
        TVert* b = &s_verts[f[1]];
        TVert* c = &s_verts[f[2]];
        TVert* d = &s_verts[f[3]];
        uint16_t any = a->flags | b->flags | c->flags | d->flags;
        uint16_t all = a->flags & b->flags & c->flags & d->flags;
        uint16_t tex = NR_Read16(f + 8);
        int tri = f[2] == f[3];
        const uint8_t* tt = texTable + (tex & 0x7ff) * 12;

        if (nearMode && (any & 0x3e))
        {
            if (!(any & 0x1f) || (all & 0xf00))
                continue;
            s_subBias = (uint8_t)~((tex >> 7) & 0x40);
            s_subTex = NR_Read16(tt + 2) | (tex & 0x8000) | (uint32_t)NR_Read16(tt + 6) << 16;
            a->uv = NR_Read16(tt);
            b->uv = NR_Read16(tt + 4);
            c->uv = NR_Read16(tt + 8);
            a->color = col[f[4]];
            b->color = col[f[5]];
            c->color = col[f[6]];
            if (!tri)
            {
                d->uv = NR_Read16(tt + 10);
                d->color = col[f[7]];
            }
            Track_Subdivide(a, b, c, d, 4);
            continue;
        }
        if (any & 0x20)
            continue;
        if (!nearMode)
        {
            if ((any & 0xc0) != 0xc0)
                continue;
        }
        else if (all & 0xf00)
            continue;

        int nc = NClip(a->sxy, b->sxy, d->sxy);
        if (mirror)
            nc = -nc;
        if (nc < 1)
        {
            if (tri)
                continue;
            nc = NClip(c->sxy, b->sxy, d->sxy);
            if (mirror)
                nc = -nc;
            if (nc >= 0)
                continue;
        }
        int depth = ((((tex >> 7) & 0x40) ^ 0xff) * 4 + a->sz + b->sz + c->sz + d->sz) >> 2;
        int semi = tex >> 15;
        u_short clut = NR_Read16(tt + 2), tpage = NR_Read16(tt + 6);
        if (!(tex & 0x800))
        {
            if (tri)
            {
                POLY_GT3* p = (POLY_GT3*)Gfx_AllocPrim(sizeof(POLY_GT3));
                if (!p)
                    return;
                setPolyGT3(p);
                setXY3(p, SX(a->sxy), SY(a->sxy), SX(b->sxy), SY(b->sxy), SX(d->sxy), SY(d->sxy));
                PRECISE(p, 0, a);
                PRECISE(p, 1, b);
                PRECISE(p, 2, d);
                setUV3(p, tt[0], tt[1], tt[4], tt[5], tt[10], tt[11]);
                p->clut = clut;
                p->tpage = tpage;
                RGBW(p, 0, col[f[4]]);
                RGBW(p, 1, col[f[5]]);
                RGBW(p, 2, col[f[7]]);
                setSemiTrans(p, semi);
                Gfx_AddPrimDepthPgxp(depth, p, PGXP_NONE);
            }
            else
            {
                POLY_GT4* p = (POLY_GT4*)Gfx_AllocPrim(sizeof(POLY_GT4));
                if (!p)
                    return;
                setPolyGT4(p);
                setXY4(p, SX(a->sxy), SY(a->sxy), SX(b->sxy), SY(b->sxy), SX(d->sxy), SY(d->sxy),
                       SX(c->sxy), SY(c->sxy));
                PRECISE(p, 0, a);
                PRECISE(p, 1, b);
                PRECISE(p, 2, d);
                PRECISE(p, 3, c);
                setUV4(p, tt[0], tt[1], tt[4], tt[5], tt[10], tt[11], tt[8], tt[9]);
                p->clut = clut;
                p->tpage = tpage;
                RGBW(p, 0, col[f[4]]);
                RGBW(p, 1, col[f[5]]);
                RGBW(p, 2, col[f[7]]);
                RGBW(p, 3, col[f[6]]);
                setSemiTrans(p, semi);
                Gfx_AddPrimDepthPgxp(depth, p, PGXP_NONE);
            }
        }
        else
        {
            uint32_t c0 = col[f[4]];
            if (tri)
            {
                POLY_FT3* p = (POLY_FT3*)Gfx_AllocPrim(sizeof(POLY_FT3));
                if (!p)
                    return;
                setPolyFT3(p);
                setXY3(p, SX(a->sxy), SY(a->sxy), SX(b->sxy), SY(b->sxy), SX(d->sxy), SY(d->sxy));
                PRECISE(p, 0, a);
                PRECISE(p, 1, b);
                PRECISE(p, 2, d);
                setUV3(p, tt[0], tt[1], tt[4], tt[5], tt[10], tt[11]);
                p->clut = clut;
                p->tpage = tpage;
                RGBW(p, 0, c0);
                setSemiTrans(p, semi);
                Gfx_AddPrimDepthPgxp(depth, p, PGXP_NONE);
            }
            else
            {
                POLY_FT4* p = (POLY_FT4*)Gfx_AllocPrim(sizeof(POLY_FT4));
                if (!p)
                    return;
                setPolyFT4(p);
                setXY4(p, SX(a->sxy), SY(a->sxy), SX(b->sxy), SY(b->sxy), SX(d->sxy), SY(d->sxy),
                       SX(c->sxy), SY(c->sxy));
                PRECISE(p, 0, a);
                PRECISE(p, 1, b);
                PRECISE(p, 2, d);
                PRECISE(p, 3, c);
                setUV4(p, tt[0], tt[1], tt[4], tt[5], tt[10], tt[11], tt[8], tt[9]);
                p->clut = clut;
                p->tpage = tpage;
                RGBW(p, 0, c0);
                setSemiTrans(p, semi);
                Gfx_AddPrimDepthPgxp(depth, p, PGXP_NONE);
            }
        }
    }
}

/* 8006327c. Pendiente: los objetos D de cada segmento (carteles, DAT_800b5500 / DAT_800af854). */
void Track_DrawSegments(int view, int mirror)
{
    const Track* t = g_Track;
    if (!t || t->numSegs == 0)
        return;
    Render_LoadCameraRotation();
    int start = s_viewStart[view], count = s_viewCount[view];
    while (count > 0)
    {
        count--;
        int i = start + count;
        if (i >= t->numSegs)
            i -= t->numSegs;
        uint8_t fl = s_segFlags[view][i];
        if (!fl)
            continue;
        Track_TransformSegVerts(&t->segs[i], fl & 0x40);
        Track_DrawSegFaces(&t->segs[i], fl & 0x40, fl & 3, mirror);
    }
}

/* ---------------------------------------------------------------- horizonte */

typedef struct
{
    uint32_t sxy;
    int16_t  sz;
    uint8_t  flags;     /* DAT_1f80000c: 0x20 no valido, 0x10 fuera por arriba/abajo */
#if USE_PGXP
    VERTTYPE px, py;
#endif
} HVert;

static HVert s_hverts[256];

/* 80064134: centro de la capa respecto a la camara (escala mundo >> 7) en el frustum */
static int Track_HorizonVisible(const TrackHorizon* h)
{
    int16_t c[3];
    for (int a = 0; a < 3; a++)
        c[a] = (int16_t)((int16_t)NR_Read32(h->hdr + 0xc + a * 4) - (int16_t)(g_Camera.pos[a] >> 7));
    Gte_SetV0(c[0], c[1], c[2]);
    doCOP2(GTE_OP_RTV0);
    int16_t r[3] = { (int16_t)MFC2_S(9), (int16_t)-MFC2_S(10), (int16_t)MFC2_S(11) };
    return Track_SphereInFrustum(r, (int)NR_Read32(h->hdr + 0x18) << 4);
}

/* 80063af4 */
static void Track_TransformHorizon(const TrackHorizon* h)
{
    int cx = g_Camera.pos[0] >> 7, cy = g_Camera.pos[1] >> 7, cz = g_Camera.pos[2] >> 7;
    for (int i = 0; i < h->numVerts; i++)
    {
        const uint8_t* p = h->verts + i * 8;
        HVert* v = &s_hverts[i];
        Gte_SetV0(NR_Read16(p) - cx, NR_Read16(p + 2) - cy, (int16_t)NR_Read16(p + 4) - cz);
        doCOP2(GTE_OP_RTPS);
        uint8_t f = 0x20;
        if (!(CFC2(31) & 0xffffe000))
        {
            v->sxy = MFC2(14);
            v->sz = (int16_t)MFC2(19);
#if USE_PGXP
            v->px = g_FP_SXYZ2.x;
            v->py = g_FP_SXYZ2.y;
#endif
            /* (en el original la comprobacion de x nunca marca nada) */
            f = (v->sxy >> 16) < 2u * GFX_SCREEN_H / 2 ? 0 : 0x10;
        }
        v->flags = f;
    }
}

/* Primitivas del horizonte: G (sin textura), FT (plana) o GT; colores ya en el orden de la primitiva */
#define HOR_UV3(p) setUV3(p, w2 & 0xff, (w2 >> 8) & 0xff, w3 & 0xff, (w3 >> 8) & 0xff, (w4 >> 16) & 0xff, w4 >> 24)
#define HOR_UV4(p) setUV4(p, w2 & 0xff, (w2 >> 8) & 0xff, w3 & 0xff, (w3 >> 8) & 0xff, (w4 >> 16) & 0xff, w4 >> 24, \
                          w4 & 0xff, (w4 >> 8) & 0xff)
#define HOR_XY3(p) (setXY3(p, SX(a->sxy), SY(a->sxy), SX(b->sxy), SY(b->sxy), SX(d->sxy), SY(d->sxy)), \
                    PRECISE(p, 0, a), PRECISE(p, 1, b), PRECISE(p, 2, d))
#define HOR_XY4(p) (setXY4(p, SX(a->sxy), SY(a->sxy), SX(b->sxy), SY(b->sxy), SX(d->sxy), SY(d->sxy), \
                           SX(c->sxy), SY(c->sxy)), \
                    PRECISE(p, 0, a), PRECISE(p, 1, b), PRECISE(p, 2, d), PRECISE(p, 3, c))

/* 80063c08 */
static void Track_DrawFaceList(const TrackHorizon* h, int mirror)
{
    const uint32_t* col = g_Track->colors;
    const uint8_t* f = h->faces;
    for (int fi = 0; fi < h->numFaces; fi++, f += 20)
    {
        const HVert* a = &s_hverts[f[0]];
        const HVert* b = &s_hverts[f[1]];
        const HVert* c = &s_hverts[f[2]];
        const HVert* d = &s_hverts[f[3]];
        if ((a->flags | b->flags | c->flags | d->flags) & 0x20)
            continue;
        if (a->flags & b->flags & c->flags & d->flags & 0x1e)
            continue;
        int nc = NClip(a->sxy, b->sxy, d->sxy);
        if (mirror)
            nc = -nc;
        if (nc < 1)
            continue;   /* (el original solo repite la prueba con los triangulos, donde siempre da 0) */
        int tri = f[2] == f[3];
        uint32_t w2 = NR_Read32(f + 8), w3 = NR_Read32(f + 12), w4 = NR_Read32(f + 16);
        int depth = ((a->sz + b->sz + c->sz + d->sz) >> 2) + 0x8000;
        uint32_t c0 = col[f[4]], c1 = col[f[5]], c2 = col[f[6]], c3 = col[f[7]];
        int semi = (int)(w2 >> 31);
        u_short clut = (u_short)((w2 >> 16) & 0x7fff);
        void* prim;
        if (!(w2 >> 16))
        {
            if (tri)
            {
                POLY_G3* p = (POLY_G3*)(prim = Gfx_AllocPrim(sizeof(POLY_G3)));
                if (!p)
                    return;
                setPolyG3(p);
                HOR_XY3(p);
                RGBW(p, 0, c0);
                RGBW(p, 1, c1);
                RGBW(p, 2, c2);
            }
            else
            {
                POLY_G4* p = (POLY_G4*)(prim = Gfx_AllocPrim(sizeof(POLY_G4)));
                if (!p)
                    return;
                setPolyG4(p);
                HOR_XY4(p);
                RGBW(p, 0, c0);
                RGBW(p, 1, c1);
                RGBW(p, 2, c2);
                RGBW(p, 3, c3);
            }
        }
        else if (w3 & 0x8000000)
        {
            u_short tpage = (u_short)((w3 >> 16) & 0xf7ff);
            if (tri)
            {
                POLY_FT3* p = (POLY_FT3*)(prim = Gfx_AllocPrim(sizeof(POLY_FT3)));
                if (!p)
                    return;
                setPolyFT3(p);
                HOR_XY3(p);
                HOR_UV3(p);
                p->clut = clut;
                p->tpage = tpage;
                RGBW(p, 0, c0);
                setSemiTrans(p, semi);
            }
            else
            {
                POLY_FT4* p = (POLY_FT4*)(prim = Gfx_AllocPrim(sizeof(POLY_FT4)));
                if (!p)
                    return;
                setPolyFT4(p);
                HOR_XY4(p);
                HOR_UV4(p);
                p->clut = clut;
                p->tpage = tpage;
                RGBW(p, 0, c0);
                setSemiTrans(p, semi);
            }
        }
        else
        {
            u_short tpage = (u_short)(w3 >> 16);
            if (tri)
            {
                POLY_GT3* p = (POLY_GT3*)(prim = Gfx_AllocPrim(sizeof(POLY_GT3)));
                if (!p)
                    return;
                setPolyGT3(p);
                HOR_XY3(p);
                HOR_UV3(p);
                p->clut = clut;
                p->tpage = tpage;
                RGBW(p, 0, c0);
                RGBW(p, 1, c1);
                RGBW(p, 2, c2);
                setSemiTrans(p, semi);
            }
            else
            {
                POLY_GT4* p = (POLY_GT4*)(prim = Gfx_AllocPrim(sizeof(POLY_GT4)));
                if (!p)
                    return;
                setPolyGT4(p);
                HOR_XY4(p);
                HOR_UV4(p);
                p->clut = clut;
                p->tpage = tpage;
                RGBW(p, 0, c0);
                RGBW(p, 1, c1);
                RGBW(p, 2, c2);
                RGBW(p, 3, c3);
                setSemiTrans(p, semi);
            }
        }
        Gfx_AddPrimDepthPgxp(depth, prim, PGXP_NONE);
    }
}

/* 80064078: capas cuyo bit esta en la mascara del segmento de la camara; las de radio > 0x3FF siempre */
void Track_DrawHorizon(int view, int mirror)
{
    const Track* t = g_Track;
    if (!t || !t->colors)
        return;
    Render_LoadCameraRotation();
    int mask = s_viewHorizon[view];
    for (int i = t->numHorizons - 1; i >= 0; i--)
    {
        const TrackHorizon* h = &t->horizons[i];
        if (!((mask >> h->bit) & 1))
            continue;
        if ((int)NR_Read32(h->hdr + 0x18) <= 0x3ff && !Track_HorizonVisible(h))
            continue;
        Track_TransformHorizon(h);
        Track_DrawFaceList(h, mirror);
    }
}
