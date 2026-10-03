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
#include "sprite.h"
#include "vecmath.h"
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

/* TTXA (0x1C bytes): +1 banderas (1 ida y vuelta, 2 anima v en vez de u), +2 paso en texeles por fotograma,
 * +3 fotograma actual, +4 ultimo fotograma, +5 sentido (+1/-1), +6 desplazamiento en bytes de la entrada en las
 * tablas de TTEX, +8 periodo (1/300 s), +0xA acumulador, +0xC + 4 * n: u (o v) base de las 4 esquinas en el
 * nivel de mip n. */

/* 80061dfc: bases de los niveles 1-3 (la del 0 viene en el fichero) */
static void Track_InitAnims(Track* t)
{
    for (int i = 0; i < t->numAnims; i++)
    {
        uint8_t* e = t->anims + i * 0x1c;
        int off = NR_Read16(e + 6);
        if (e[1] & 2)
        {
            off++;
            e[6] = (uint8_t)off;
            e[7] = (uint8_t)(off >> 8);
        }
        for (int lv = 1; lv < 4; lv++)
        {
            const uint8_t* p = t->tex[lv] + off;
            uint8_t* b = e + 0xc + lv * 4;
            b[0] = p[0];
            b[1] = p[4];
            b[2] = p[8];
            b[3] = p[10];
        }
    }
}

/* 80061e94 */
void Track_Animate(int dt)
{
    Track* t = g_Track;
    if (!t || !t->anims)
        return;
    for (int i = 0; i < t->numAnims; i++)
    {
        uint8_t* e = t->anims + i * 0x1c;
        uint16_t period = NR_Read16(e + 8);
        uint16_t acc = (uint16_t)(NR_Read16(e + 10) + dt);
        int frame = e[3];
        int stepped = 0;
        while (period < acc && period != 0)
        {
            stepped = 1;
            frame = e[3] + (int8_t)e[5];
            acc = (uint16_t)(acc - period);
            if (!(e[1] & 1))
            {
                if (frame < 0)
                    frame = e[4];
                else if (frame > e[4])
                    frame = 0;
            }
            else if (frame < 0)
            {
                frame = 1;
                e[5] = (uint8_t)-(int8_t)e[5];
            }
            else if (frame > e[4])
            {
                frame = e[4] - 1;
                e[5] = (uint8_t)-(int8_t)e[5];
            }
            e[3] = (uint8_t)frame;
        }
        e[10] = (uint8_t)acc;
        e[11] = (uint8_t)(acc >> 8);
        if (!stepped)
            continue;
        int off = NR_Read16(e + 6);
        int c = frame * e[2];
        for (int lv = 0; lv < 4; lv++, c >>= 1)
        {
            uint8_t* p = t->tex[lv] + off;
            const uint8_t* b = e + 0xc + lv * 4;
            p[0] = (uint8_t)(b[0] + c);
            p[4] = (uint8_t)(b[1] + c);
            p[8] = (uint8_t)(b[2] + c);
            p[10] = (uint8_t)(b[3] + c);
        }
    }
}

/* 80062088. Pendiente: la tabla de TCRV DAT_800af8e8. */
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
            t->anims = c + 8;
            t->numAnims = (int)(len - 8) / 0x1c;
            break;
        }
        c += len;
    }
    if (t->anims && t->tex[3])
        Track_InitAnims(t);
    else
        t->numAnims = 0;
    Track_ResetNormalCache();
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

int Track_FindSegmentByCurve(int i)
{
    const Track* t = g_Track;
    if (!t || t->numCurve <= 0)
        return 0;
    int n = t->numCurve, best = 0, bestD = 0x7fffffff;
    i %= n;
    if (i < 0)
        i += n;
    for (int s = 0; s < t->numSegs; s++)
    {
        const TrackSegHdr* h = t->segs[s].hdr;
        int len = ((int)h->curveEnd - h->curveStart + n) % n;
        int mid = (h->curveStart + len / 2) % n;
        int d = abs(i - mid);
        if (d > n / 2)
            d = n - d;
        if (d < bestD)
        {
            bestD = d;
            best = s;
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

/* 8006327c: caras de los segmentos visibles y sus objetos D (halos de luz: {forma, color TCOL, tamano,
 * vertice}) a la lista de sprites */
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
        const TrackSegHdr* h = t->segs[i].hdr;
        const uint8_t* d = t->segs[i].d;
        for (int k = 0; k < h->numD; k++, d += 4)
        {
            const TVert* v = &s_verts[d[3]];
            if ((v->flags & 0x20) || ((v->flags & 0x300) && v->sz <= 0x7f))
                continue;
#if USE_PGXP
            Sprite_Add(d[0], t->colors[d[1]], 0, v->sxy, v->sz, d[2] << 6, v->px, v->py);
#else
            Sprite_Add(d[0], t->colors[d[1]], 0, v->sxy, v->sz, d[2] << 6, (float)(int16_t)v->sxy,
                       (float)(int16_t)(v->sxy >> 16));
#endif
        }
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

/* ---------------------------------------------------------------- colision con el suelo */

/* Producto exterior de la GTE (OP, sf = 0) solo con la componente que usa el juego:
 * MAC1 = R22 * IR3 - R33 * IR2, con registros de 16 bits */
static int Gte_OpMac1(int r22, int r33, int ir2, int ir3)
{
    return (int16_t)r22 * (int16_t)ir3 - (int16_t)r33 * (int16_t)ir2;
}

#define VX(v) ((int)(((v) >> 19) & 0x1ffc))
#define VZ(v) ((int)(((v) >> 8) & 0x1ffc))

/* 800647f4: (lx, lz) relativos al origen del segmento dentro de la cara (cuadrilatero v0 v1 v2 v3 en orden de
 * contorno). 1 = triangulo (v0, v1, v3), 2 = (v1, v2, v3), 0 = fuera. */
static int Track_FaceContains(const uint8_t* f, const uint32_t* verts, int lx, int lz)
{
    if (lx < 0 || lz < 0 || lx >= 0x2000 || lz >= 0x2000)
        return 0;
    uint32_t a = verts[f[1]], b = verts[f[3]];
    int ax = VX(a), az = VZ(a), bx = VX(b), bz = VZ(b);
    int r22 = lz - az, r33 = lx - ax;
    if (Gte_OpMac1(r22, r33, bz - az, bx - ax) < 1)
    {
        uint32_t c = verts[f[0]];
        int cx = VX(c), cz = VZ(c);
        if (Gte_OpMac1(lz - cz, lx - cx, az - cz, ax - cx) > 0)
            return 0;
        return Gte_OpMac1(lz - bz, lx - bx, cz - bz, cx - bx) < 1 ? 1 : 0;
    }
    if (f[3] == f[2])
        return 0;
    uint32_t d = verts[f[2]];
    int dx = VX(d), dz = VZ(d);
    if (Gte_OpMac1(r22, r33, dz - az, dx - ax) > 0)
        return 0;
    return Gte_OpMac1(lz - dz, lx - dx, bz - dz, bx - dx) < 1 ? 2 : 0;
}

/* 80064998: altura (mundo, relativa al origen del segmento * 16) del plano del triangulo en (lx, lz) y, si se
 * pide, su normal sin normalizar (e1 x e2 con el producto exterior de la GTE) */
static int Track_PlaneHeight(uint32_t p1, uint32_t p2, uint32_t p3, int lx, int lz, int* normal)
{
    int x1 = VX(p1), z1 = VZ(p1), y1 = (int)(p1 & 0x3ff);
    int e1x = VX(p2) - x1, e2x = VX(p3) - x1;
    int e1z = VZ(p2) - z1, e2z = VZ(p3) - z1;
    int det = Gte_OpMac1(e2x, e2z, e1x, e1z);
    int e1y = (int)(p2 & 0x3ff) * 4 - y1 * 4;
    int e2y = (int)(p3 & 0x3ff) * 4 - y1 * 4;
    int h = y1 << 6;
    if (det != 0)
    {
        int m1 = (int16_t)e2x * (int16_t)(lz - z1) - (int16_t)(lx - x1) * (int16_t)e2z;
        int m2 = (int16_t)(lx - x1) * (int16_t)e1z - (int16_t)e1x * (int16_t)(lz - z1);
        int32_t num = (int32_t)(uint32_t)(((int64_t)m1 * e1y + (int64_t)m2 * e2y) * 0x10);
        h += num / det;
    }
    if (normal)
    {
        normal[0] = (int16_t)e2z * (int16_t)e1y - (int16_t)e2y * (int16_t)e1z;
        normal[1] = det;
        normal[2] = (int16_t)e2y * (int16_t)e1x - (int16_t)e2x * (int16_t)e1y;
    }
    return h;
}

static int Track_FaceHeight(const TrackSeg* s, const uint8_t* f, int tri, int lx, int lz, int* normal)
{
    const uint32_t* v = s->verts;
    int h = tri == 1 ? Track_PlaneHeight(v[f[0]], v[f[1]], v[f[3]], lx, lz, normal)
                     : Track_PlaneHeight(v[f[1]], v[f[2]], v[f[3]], lx, lz, normal);
    return h + s->hdr->origin[1] * 0x10;
}

/* Cache de normales (DAT_800af90c, 0x1858 bytes, iniciada en 80061cd0): 24 bloques de 32 triangulos.
 * +0 ultimo bloque usado, +4 bloque de cada grupo de 32 claves (0xFF ninguno), +0x5C8 mascaras de validos,
 * +0x628 grupo dueno de cada bloque, +0x658 normales (3 x s16 >> 2). Clave = cara << 1 | triangulo | seg << 8. */
static uint8_t s_normCache[0x1858];

void Track_ResetNormalCache(void)
{
    s_normCache[0] = 0xff;
    for (int i = 0; i < 0x5c1; i++)
        s_normCache[4 + i] = 0xff;
    for (int k = 0; k < 24; k++)
    {
        s_normCache[0x628 + k * 2] = (uint8_t)0x5c1;
        s_normCache[0x629 + k * 2] = (uint8_t)(0x5c1 >> 8);
    }
}

/* 80065194 */
static int NormCache_Get(uint32_t key, int* n)
{
    if ((key >> 5) >= 0x5c1)
        return 0;
    int slot = (int8_t)s_normCache[(key >> 5) + 4];
    if (slot < 0 || !(NR_Read32(s_normCache + 0x5c8 + slot * 4) & (1u << (key & 31))))
        return 0;
    const uint8_t* p = s_normCache + slot * 0xc0 + 0x658 + (key & 31) * 6;
    n[0] = (int16_t)NR_Read16(p) << 2;
    n[1] = (int16_t)NR_Read16(p + 2) << 2;
    n[2] = (int16_t)NR_Read16(p + 4) << 2;
    return 1;
}

static void Put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void Put16(uint8_t* p, int v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* 800650b0 */
static void NormCache_Put(uint32_t key, const int* n)
{
    if ((key >> 5) >= 0x5c1)
        return;
    uint8_t* e = &s_normCache[(key >> 5) + 4];
    int slot = (int8_t)*e;
    uint32_t mask;
    if (slot < 0)
    {
        slot = (int8_t)s_normCache[0] + 1;
        if (slot > 0x17)
            slot = 0;
        int owner = (int16_t)NR_Read16(s_normCache + 0x628 + slot * 2);
        s_normCache[0] = (uint8_t)slot;
        if (owner + 4 < (int)sizeof(s_normCache))
            s_normCache[owner + 4] = 0xff;
        Put16(s_normCache + 0x628 + slot * 2, (int)(key >> 5));
        *e = (uint8_t)slot;
        mask = 0;
    }
    else
        mask = NR_Read32(s_normCache + 0x5c8 + slot * 4);
    Put32(s_normCache + 0x5c8 + slot * 4, mask | 1u << (key & 31));
    uint8_t* p = s_normCache + (key & 31) * 6 + slot * 0xc0 + 0x658;
    Put16(p, n[0] >> 2);
    Put16(p + 2, n[1] >> 2);
    Put16(p + 4, n[2] >> 2);
}

/* 80064acc. tp = pista del ultimo resultado (seg 0xFF = desconocido), pos = (x, y, z) del mundo; devuelve la
 * altura del suelo (mundo) o TRACK_NO_SURFACE y, si normal != NULL, la normal 16.16 (cacheada). Con la rejilla
 * TIGR (celdas de 0x8000, subceldas de 0x2000) se elige, entre las caras bajo el punto, la mas cercana por
 * debajo de y. withCurve: tambien el punto de TCRV. */
int Track_FindSurface(TrackPos* tp, const int* pos, int* normal, int withCurve)
{
    const Track* t = g_Track;
    int* calc = NULL;           /* local_30: normal a calcular y cachear */
    uint32_t key = 0;           /* local_34 */
    int x = pos[0], y = pos[1], z = pos[2];
    if (!t || !t->segs)
        goto none;
    if (tp->seg != 0xff && tp->seg < t->numSegs)
    {
        const TrackSeg* s = &t->segs[tp->seg];
        if (tp->face < s->hdr->numFaces)
        {
            const uint8_t* f = s->faces + tp->face * 10;
            int lx = (x >> 4) - s->hdr->origin[0], lz = (z >> 4) - s->hdr->origin[2];
            int tri = Track_FaceContains(f, s->verts, lx, lz);
            if (tri)
            {
                if (normal)
                {
                    key = (uint32_t)tp->face << 1 | (tri & 1) | (uint32_t)tp->seg << 8;
                    if (!NormCache_Get(key, normal))
                        calc = normal;
                }
                int h = Track_FaceHeight(s, f, tri, lx, lz, calc);
                if (calc)
                {
                    Vec_Normalize(calc);
                    NormCache_Put(key, calc);
                }
                return h;
            }
        }
    }

    {
        const uint8_t* g = t->tigr;
        if (!g)
            goto none;
        int w = g[8], hgt = g[9];
        int nOffs = NR_Read16(g + 10), split = NR_Read16(g + 12);
        const uint8_t* grid = g + 0x10 + nOffs * 2;
        const uint8_t* blocks = grid + w * hgt;
        int gx = x, gz = z;
        if (gx < 0 || (gx >> 15) >= hgt)
            gx = 0;
        if (gz < 0 || (gz >> 15) >= w)
            gz = 0;
        int cell = (gx >> 15) * w + (gz >> 15);
        int ci = grid[cell];
        if (ci == 0xff)
            goto none;
        int oi = cell >= split ? ci + 0xff : ci;
        const uint8_t* block = blocks + NR_Read16(g + 0x10 + oi * 2);
        int start = block[((gx >> 11) & 0xc) + ((gz >> 13) & 3)];
        const uint8_t* list = block + 0x10;
        if (start == 0xff)
            goto none;
        int curSeg = -1, left = -1, more = 1;
        int bestDy = 0, bestH = 0, bestSeg = 0, bestFace = 0;
        const TrackSeg* s = NULL;
        int lx = 0, lz = 0;
        const uint8_t* p = list + start;
        int found = 0, h = 0, face = 0;
        do
        {
            int b = *p++;
            if (left < 0)
            {
                if (b & 0x80)
                    more = 0;
                int segNum = list[(b >> 4) & 7];
                left = b & 0xf;
                if (segNum != curSeg && segNum < t->numSegs)
                {
                    s = &t->segs[segNum];
                    lx = (x >> 4) - s->hdr->origin[0];
                    lz = (z >> 4) - s->hdr->origin[2];
                    curSeg = segNum;
                }
                b = *p++;
            }
            face = b & 0x7f;
            if (s && face < s->hdr->numFaces)
            {
                const uint8_t* f = s->faces + face * 10;
                int tri = Track_FaceContains(f, s->verts, lx, lz);
                if (tri)
                {
                    calc = NULL;
                    if (normal)
                    {
                        key = (uint32_t)face << 1 | (tri & 1) | (uint32_t)curSeg << 8;
                        if (!NormCache_Get(key, normal))
                            calc = normal;
                    }
                    h = Track_FaceHeight(s, f, tri, lx, lz, calc);
                    int dy = y - h;
                    if (bestDy == 0)
                    {
                        bestDy = dy;
                        bestH = h;
                        bestSeg = curSeg;
                        bestFace = face;
                        if ((unsigned)(dy + 0x1ff) < 0x11ff)
                        {
                            found = 1;  /* esta misma */
                            break;
                        }
                    }
                    else if ((unsigned)(bestDy - dy + 0x40) > 0x80)
                    {
                        if (bestDy < 1)
                            found = bestDy <= dy ? 1 : 2;
                        else
                            found = (dy < -0x200 || bestDy < dy) ? 2 : 1;
                        break;
                    }
                }
            }
            left--;
        } while (left >= 0 || more);
        if (!found && bestDy != 0)
            found = 2;
        if (!found)
            goto none;
        if (found == 2)
        {
            curSeg = bestSeg;
            face = bestFace;
            h = bestH;
        }
        tp->seg = (uint8_t)curSeg;
        tp->face = (uint8_t)face;
        if (withCurve)
        {
            /* fila de la cara -> punto de TCRV (busqueda binaria en las filas de +0x2C) */
            const TrackSegHdr* sh = t->segs[curSeg].hdr;
            const uint8_t* rows = (const uint8_t*)sh + 0x2c;
            unsigned hi = sh->numRows, mid = hi >> 1, lo = 0, prev = 0;
            if (mid != 0)
            {
                do
                {
                    lo = mid;
                    if (face < rows[mid])
                    {
                        lo = prev;
                        hi = mid;
                    }
                    mid = (lo + hi) / 2;
                    prev = lo;
                } while (mid != lo);
            }
            int c = (int)lo + sh->curveStart;
            if (t->numCurve > 0 && c >= t->numCurve)
                c -= t->numCurve;
            tp->curve = (int16_t)c;
        }
        if (calc)
        {
            Vec_Normalize(calc);
            NormCache_Put(key, calc);
        }
        return h;
    }
none:
    if (normal)
    {
        normal[0] = 0;
        normal[1] = 0x10000;
        normal[2] = 0;
    }
    return TRACK_NO_SURFACE;
}

/* 80065488: altura del suelo bajo pos (y se ignora: se busca desde y = -1). mode 0 = la encontrada,
 * 1 = media con la de encima, 2 = la de encima (desde la primera + 0x8000). */
int Track_GroundHeight(TrackPos* tp, const int* pos, int mode)
{
    if (mode == 3 || !tp)
        return 0;
    int p[3] = { pos[0], -1, pos[2] };
    int h = Track_FindSurface(tp, p, NULL, 1);
    if (h == TRACK_NO_SURFACE)
        return 0;
    if (mode == -1 || mode == 1 || mode == 2)
    {
        p[1] = h + 0x8000;
        tp->seg = 0xff;
        tp->curve = -1;
        int h2 = Track_FindSurface(tp, p, NULL, 1);
        return mode == 1 ? (h + h2) / 2 : h2;
    }
    return h;
}

/* 80065ee8: tipo de superficie de la cara (tabla de 4 en TSEG +0x44, indice en los bits 6-7 del segundo byte
 * de la cara en el bloque que sigue a las caras); 0xF sin cara. face: la cara (10 bytes) o NULL. */
int Track_SurfaceType(const TrackPos* tp, const uint8_t** face)
{
    const Track* t = g_Track;
    if (!t || tp->seg == 0xff || tp->seg >= t->numSegs)
    {
        if (face)
            *face = NULL;
        return 0xf;
    }
    const TrackSeg* s = &t->segs[tp->seg];
    if (face)
        *face = s->faces + tp->face * 10;
    return ((const uint8_t*)s->hdr)[(s->rest[tp->face * 2 + 1] >> 6) + 0x44];
}

/* 80065610: punto de TCRV mas cercano (TCRV +6 de cada punto = indice ordenado por x, busqueda binaria por x y
 * luego hacia los dos lados mientras la distancia en x no supere la mejor) */
int Track_NearestCurvePoint(const int* pos)
{
    const Track* t = g_Track;
    if (!t || !t->curve || t->numCurve < 2)
        return 0;
    const uint8_t* c = t->curve;
    int n = t->numCurve;
#define CX(i) ((int)NR_Read16(c + (i) * 10))
#define CY(i) ((int)NR_Read16(c + (i) * 10 + 2))
#define CZ(i) ((int)NR_Read16(c + (i) * 10 + 4))
#define CS(i) ((int)(int16_t)NR_Read16(c + (i) * 10 + 6))
    int px = pos[0] >> 5, py = pos[1] >> 5, pz = pos[2] >> 5;
    int lo = 0, hi = n - 1, sum = n - 1, mid = 0;
    if (n - 2 > 0)
    {
        do
        {
            mid = sum / 2;
            if (px < CX(CS(mid)))
            {
                hi = mid;
                mid = lo;
            }
            sum = mid + hi;
            lo = mid;
        } while (mid < hi - 1);
    }
    int best = CS(mid);
    int dy = py >= 0 ? CY(best) - py : 0;
    int dz = CZ(best) - pz;
    int bestD = (CX(best) - px) * (CX(best) - px) + dy * dy + dz * dz;
    int step = 1;
    unsigned done = 0, k = (unsigned)mid + 1;
    for (;;)
    {
        if (k < (unsigned)n)
        {
            int i = CS(k);
            if (py >= 0)
                dy = CY(i) - py;
            int dx2 = (CX(i) - px) * (CX(i) - px);
            int dz2 = CZ(i) - pz;
            if (bestD <= dx2)
                goto blocked;
            dx2 += dy * dy + dz2 * dz2;
            if (dx2 < bestD)
            {
                bestD = dx2;
                best = i;
            }
            if (done == 0)
                goto flip;
            step = (step >> 31) * 2 + 1;
        }
        else
        {
        blocked:
            done |= 1u << ((unsigned)step >> 31);
        flip:
            step = ~step + (step >> 31) * -2;
        }
        k += (unsigned)step;
        if (done == 3)
            return best;
    }
#undef CX
#undef CY
#undef CZ
#undef CS
}

/* 80065844: el segmento pos0 -> pos1 (mundo) cruza un muro de la pista (aristas marcadas en el bloque que sigue a
 * las caras: 2 bits por arista, 3 = muro alto, 1-2 = bordillo de 0x200 >> n). Devuelve 0 o 0x100 | tipo del muro
 * (TSEG +0x48), con la normal horizontal del muro (16.16) y sus dos extremos (mundo). */
int Track_RayWall(const int* p0, const int* p1, int* normal, int* edge)
{
    const Track* t = g_Track;
    if (!t || !t->tigr)
        return 0;
    const uint8_t* g = t->tigr;
    int w = g[8], hgt = g[9];
    int nOffs = NR_Read16(g + 10), split = NR_Read16(g + 12);
    const uint8_t* grid = g + 0x10 + nOffs * 2;
    const uint8_t* blocks = grid + w * hgt;
    int x0 = p0[0], z0 = p0[2], x1 = p1[0], z1 = p1[2];
    if (x0 < 0 || (x0 >> 15) >= hgt || z0 < 0 || (z0 >> 15) >= w || x1 < 0 || (x1 >> 15) >= hgt || z1 < 0 ||
        (z1 >> 15) >= w)
        return 0;
    int dcx = (x1 >> 13) - (x0 >> 13), dcz = (z1 >> 13) - (z0 >> 13);
    int pass = 2;
    int cx = x0, cz = z0;
    const TrackSeg* s = NULL;
    for (;;)
    {
        int cell = (cx >> 15) * w + (cz >> 15);
        int ci = grid[cell];
        if (ci != 0xff)
        {
            int oi = cell >= split ? ci + 0xff : ci;
            const uint8_t* block = blocks + NR_Read16(g + 0x10 + oi * 2);
            int start = block[((cx >> 11) & 0xc) + ((cz >> 13) & 3)];
            if (start != 0xff)
            {
                const uint8_t* list = block + 0x10;
                const uint8_t* p = list + start;
                int left = -1, more = 1, curSeg = -1;
                int l0x = 0, l0z = 0, l1x = 0, l1z = 0;
                do
                {
                    int b = *p++;
                    if (left < 0)
                    {
                        if (b & 0x80)
                            more = 0;
                        int segNum = list[(b >> 4) & 7];
                        if (segNum != curSeg && segNum < t->numSegs)
                        {
                            s = &t->segs[segNum];
                            l0x = (x0 >> 4) - s->hdr->origin[0];
                            l1x = (x1 >> 4) - s->hdr->origin[0];
                            l0z = (z0 >> 4) - s->hdr->origin[2];
                            l1z = (z1 >> 4) - s->hdr->origin[2];
                            curSeg = segNum;
                        }
                        left = b & 0xf;
                        b = *p++;
                    }
                    if ((b & 0x80) && s)
                    {
                        int face = b & 0x7f;
                        const uint8_t* f = s->faces + face * 10;
                        int nv = f[2] == f[3] ? 3 : 4;
                        unsigned prevV = (unsigned)nv - 1;
                        for (unsigned e = 0; (int)e < nv; prevV = e, e++)
                        {
                            int kind = (s->rest[face * 2] >> ((prevV & 0xf) << 1)) & 3;
                            if (!kind)
                                continue;
                            uint32_t va = s->verts[f[prevV]], vb = s->verts[f[e]];
                            int ay = (int)(va & 0x3ff) * 4, az = VZ(va), ax = VX(va);
                            int by = (int)(vb & 0x3ff) * 4, bz = VZ(vb), bx = VX(vb);
                            /* el rayo cruza la arista y la arista cruza el rayo (OP de la GTE) */
                            int m1 = Gte_OpMac1(l0z - az, bz - az, l0x - ax, bx - ax);
                            int m2 = (int16_t)(bz - az) * (int16_t)(ax - l1x) - (int16_t)(az - l1z) * (int16_t)(bx - ax);
                            /* los extremos del rayo al mismo lado de la arista, o los de la arista al mismo
                             * lado del rayo: no hay cruce */
                            int noHit;
                            if (m1 != 0 && m2 != 0 && (m1 ^ m2) >= 0)
                                noHit = 1;
                            else
                            {
                                m1 = Gte_OpMac1(az - l0z, l1z - l0z, ax - l0x, l1x - l0x);
                                m2 = (int16_t)(l1z - l0z) * (int16_t)(l0x - bx) - (int16_t)(l0z - bz) * (int16_t)(l1x - l0x);
                                noHit = m1 != 0 && m2 != 0 && (m1 ^ m2) >= 0;
                            }
                            if (noHit)
                                continue;
                            if (kind != 3)
                            {
                                int d = ((by + ay) >> 1) - (((p0[1] + p1[1]) >> 5) - s->hdr->origin[1]);
                                if (abs(d) > (0x200 >> kind))
                                    continue;
                            }
                            if (normal)
                            {
                                normal[0] = bz - az;
                                normal[1] = 0;
                                normal[2] = ax - bx;
                                Vec_Normalize(normal);
                            }
                            if (edge)
                            {
                                edge[0] = (s->hdr->origin[0] + ax) * 0x10;
                                edge[3] = (s->hdr->origin[0] + bx) * 0x10;
                                edge[1] = (s->hdr->origin[1] + ay) * 0x10;
                                edge[4] = (s->hdr->origin[1] + by) * 0x10;
                                edge[2] = (s->hdr->origin[2] + az) * 0x10;
                                edge[5] = (s->hdr->origin[2] + bz) * 0x10;
                            }
                            return ((const uint8_t*)s->hdr)[((s->rest[face * 2 + 1] >> 4) & 3) + 0x48] | 0x100;
                        }
                    }
                    left--;
                } while (left >= 0 || more);
            }
        }
        if (pass == 0)
            return 0;
        if (pass == 2)
        {
            if (dcx == 0)
            {
                if (dcz == 0)
                    return 0;
                pass = 0;
                cx = x1;
                cz = z1;
                continue;
            }
            if (dcz == 0)
            {
                pass = 0;
                cx = x1;
                cz = z1;
                continue;
            }
            pass = 1;
            if ((dcx ^ dcz ^ ((x1 - x0) * (((z0 >> 13) + dcz) * 0x2000 - z0) -
                              (z1 - z0) * (((x0 >> 13) + dcx) * 0x2000 - x0))) < 1)
            {
                cz = z0 + dcz;
                cx = x0;
            }
            else
            {
                cz = z0;
                cx = x0 + dcx;
            }
            continue;
        }
        pass = 0;
        cx = x1;
        cz = z1;
    }
}
