#include <string.h>
#include "nr.h"
#include "mem.h"
#include "globals.h"
#include "texslot.h"
#include "frontend.h"
#include "model.h"
#include "race.h"
#include "powerup.h"

static_assert(sizeof(void*) == 4, "los modelos guardan punteros de 32 bits (compilar en Win32)");

#define TAG_5DGI 0x35444749u
#define TAG_3DRF 0x33445246u
#define TAG_3DRL 0x3344524cu
#define TAG_3DBB 0x33444242u
#define TAG_3DHS 0x33444853u
#define TAG_3DSD 0x33445344u
#define TAG_3DTL 0x3344544cu
#define TAG_AnmD 0x416e6d44u
#define TAG_4DTA 0x34445441u
#define TAG_4DNL 0x34444e4cu
#define TAG_4DQL 0x3444514cu
#define TAG_4DVL 0x3444564cu

#define MAX_CHUNK_INDEX 256

/* DAT_800af538...800af564 */
static uint32_t* s_hdr;
static uint32_t* s_faces;
static uint32_t* s_shadow;
static uint32_t* s_bbox;
static uint32_t* s_anim;
static uint32_t* s_points;
static uint32_t* s_animTex;
static uint32_t* s_vertTable[MAX_CHUNK_INDEX];   /* DAT_800af554 */
static uint32_t* s_normTable[MAX_CHUNK_INDEX];   /* DAT_800af558 */
static uint32_t* s_rlTable[MAX_CHUNK_INDEX];     /* DAT_800af55c */
static uint32_t* s_refTable[MAX_CHUNK_INDEX];    /* DAT_800af560 */
static uint32_t* s_animTexList;                  /* DAT_800af564 */

static uint32_t Ptr(const void* p)
{
    return (uint32_t)(uintptr_t)p;
}

/* 80012c08: recorre los subchunks [tag][tamano][indice] y apunta cada uno */
static void Model_ParseChunks(uint8_t* data, int size)
{
    s_anim = s_points = s_animTex = s_shadow = NULL;
    for (int off = 0; off + 12 <= size;)
    {
        uint32_t* c = (uint32_t*)(data + off);
        uint32_t tag = c[0], len = c[1], idx = c[2] % MAX_CHUNK_INDEX;
        uint32_t* body = c + 3;
        if (len < 12)
            break;
        switch (tag)
        {
        case TAG_3DTL:
        case TAG_AnmD:
            c[2] = (len - 0xc) >> 4;
            s_anim = c + 2;
            break;
        case TAG_3DRF: s_refTable[idx] = body; break;
        case TAG_3DRL: s_rlTable[idx] = body; break;
        case TAG_3DBB: s_bbox = c + 2; break;
        case TAG_3DHS: s_points = c + 2; break;
        case TAG_3DSD: s_shadow = c; break;
        case TAG_4DTA: s_animTex = c; break;
        case TAG_4DNL: s_normTable[idx] = body; break;
        case TAG_4DQL: s_faces = body; break;
        case TAG_5DGI: s_hdr = c; break;
        case TAG_4DVL: s_vertTable[idx] = body; break;
        }
        off += (int)len;
    }
}

/* 80012de0: los indices de una lista 3DRF ([tag][n][indices]) pasan a punteros */
static void Model_ResolveRefList(uint32_t* ref)
{
    uint32_t** table;
    switch (ref[0])
    {
    case TAG_4DNL: table = s_normTable; break;
    case TAG_3DRL: table = s_rlTable; break;
    case TAG_4DVL: table = s_vertTable; break;
    default: return;
    }
    for (uint32_t i = 0; i < ref[1]; i++)
        ref[2 + i] = Ptr(table[ref[2 + i] % MAX_CHUNK_INDEX]);
}

static uint32_t* RefList(uint32_t index)
{
    uint32_t* ref = s_refTable[index % MAX_CHUNK_INDEX];
    if (!ref)
        return NULL;
    Model_ResolveRefList(ref);
    return ref + 1;
}

/* 80012f14: el tipo de cara pasa a codigo de primitiva de la GPU (FT3/FT4/GT3/GT4 + semitransparente)
 * y su slot de pagina a tpage/CLUT reales. En el garaje la pagina 0xF (libreia) es la del hueco. */
static void Model_PatchFaces(ModelHdr* m, int id)
{
    uint32_t n = m->faces[0];
    uint8_t* f = (uint8_t*)(m->faces + 1);
    int level = (m->flags >> 22) & 3;
    for (uint32_t i = 0; i < n; i++, f += 24)
    {
        uint8_t t = f[0], code = 0;
        int lit = 0;
        switch ((t & 0x70) >> 4)
        {
        case 0: case 1: code = 0x24; break;
        case 2: case 3: code = 0x24; lit = 1; break;
        case 4: case 5: code = 0x34; lit = 1; break;
        }
        if ((t & 7) == 4)
            code += 8;
        if (t & 8)
            code |= 2;
        if (lit)
            f[2] |= 2;
        f[0] = code;
        int slot = NR_Read16(f + 10);
        if (g_IsFrontend && id >= 30000 && slot == 0xf)
            slot = id - 0x7527;
        if (id < 0 && slot == 0xf)
            slot = -id;
        f[10] = (uint8_t)slot;
        f[11] = (uint8_t)(slot >> 8);
        TexSlot_PatchFace(f, level);
    }
}

/* 8001315c */
ModelHdr* Model_Register(uint8_t* data, int size, int id)
{
    s_hdr = s_faces = s_bbox = NULL;
    Model_ParseChunks(data, size);
    if (!s_hdr || !s_faces || !s_bbox)
        return NULL;
    ModelHdr* m = (ModelHdr*)s_hdr;

    if (id < 0 && id != -0xe && ((m->flags >> 22) & 3) == 0)
        m->flags = (m->flags & 0xff3fffff) | 0x400000;
    m->bbox = (uint8_t*)s_bbox;
    s_bbox[1] = (m->flags & 0xffff) << 4;
    m->faces = s_faces;
    m->normals = RefList(Ptr(m->normals));
    m->verts = RefList(Ptr(m->verts));
    m->rl = RefList(Ptr(m->rl));

    if (!s_animTex)
    {
        m->animTex = NULL;
        m->flags &= ~0x1000000u;
    }
    else
    {
        uint32_t* a = s_animTex;
        m->animTex = a;
        m->flags |= 0x1000000;
        a[0] = Ptr(s_animTexList);
        s_animTexList = a;
        a[1] = Ptr(m->faces + 1);
        uint8_t* e = (uint8_t*)(a + 3);
        for (int i = 0; i < (int)a[2]; i++, e += 16)
        {
            if (e[1] & 2)
                (*(int16_t*)(e + 6))++;
            e[0] = (uint8_t)((m->flags >> 22) & 3);
        }
    }

    Model_PatchFaces(m, id);
    m->shadow = (uint8_t*)s_shadow;
    if (s_shadow)
        s_shadow[1] = Ptr((uint8_t*)s_shadow + ((uint8_t*)s_shadow)[8] * 8 + 0xc);
    m->anim = s_anim;
    m->points = s_points;
    return m;
}

/* ---------------------------------------------------------------- registro de assets */

static NrAsset* s_registry;   /* DAT_800af5b0 (el original los enlaza por +0xC/+0x10) */

void Asset_Register(NrAsset* a)
{
    a->next = NULL;
    NrAsset** p = &s_registry;
    while (*p)
        p = &(*p)->next;
    *p = a;
}

NrAsset* Asset_Find(uint32_t type, int id)
{
    for (NrAsset* a = s_registry; a; a = a->next)
        if (a->type == type && a->id == id)
            return a;
    return NULL;
}

/* 8001364c (Race_UpdateTime): texturas animadas 4DTA. Cada modelo con 4DTA esta en la lista DAT_800af564:
 * +4 sus caras, +8 n entradas y entradas de 16 bytes: +0 desplazamiento (nivel de mip de la pagina), +1 bits
 * (1 vaiven), +2 paso en U, +3 fotograma, +4 ultimo fotograma, +5 sentido, +6 cara (bytes desde +4), +8 tics por
 * fotograma, +0xA cuenta, +0xC las U de sus 4 vertices. Al cambiar de fotograma, U = base + fotograma * paso >> mip.
 * La tabla de 36 entradas (la del tornado) solo se mueve si hay alguno en la pista. */
void Model_AnimateTextures(int dt)
{
    for (uint32_t* a = s_animTexList; a; a = (uint32_t*)(uintptr_t)a[0])
    {
        int n = (int)a[2];
        if (n == 0x24 && g_TornadoCount == 0)
            continue;
        uint8_t* e = (uint8_t*)(a + 3);
        for (int i = 0; i < n; i++, e += 16)
        {
            uint16_t period = *(uint16_t*)(e + 8);
            *(uint16_t*)(e + 0xa) = (uint16_t)(*(uint16_t*)(e + 0xa) + dt);
            if (*(uint16_t*)(e + 0xa) <= period)
                continue;
            int f = 0;
            while (period < *(uint16_t*)(e + 0xa))
            {
                *(uint16_t*)(e + 0xa) = (uint16_t)(*(uint16_t*)(e + 0xa) - period);
                f = e[3] + (int8_t)e[5];
                if (!(e[1] & 1))
                {
                    if (f < 0)
                        f = e[4];
                    else if (f > e[4])
                        f = 0;
                }
                else if (f < 0 || f > e[4])
                {
                    f = f < 0 ? 1 : e[4] - 1;
                    e[5] = (uint8_t)-(int8_t)e[5];
                }
                e[3] = (uint8_t)f;
                if (period == 0)
                    break;   /* (solo en el port: el original no sale con un periodo 0) */
            }
            uint8_t* face = (uint8_t*)(uintptr_t)a[1] + *(uint16_t*)(e + 6);
            uint8_t c = (uint8_t)((f * e[2]) >> (e[0] & 31));
            face[4] = (uint8_t)(e[0xc] + c);
            face[8] = (uint8_t)(e[0xd] + c);
            face[0xc] = (uint8_t)(e[0xe] + c);
            face[0xe] = (uint8_t)(e[0xf] + c);
        }
    }
}

/* Sombras cuadradas (Pool_Alloc(0x30, 2) en el original: se van con la carrera) */
#define SHADOW_QUADS 64
static uint32_t s_shadowQuads[SHADOW_QUADS][12];
static int      s_numShadowQuads;

/* 8001309c: sombra 3DSD de un rectangulo plano con la planta de la caja del modelo (bbox +8: centro x, y, z, ?,
 * medio ancho x, ?, medio largo z): 4 vertices y una cara plana (bit 0x80) */
uint8_t* Model_MakeShadowQuad(const ModelHdr* m)
{
    if (!m || !m->bbox || s_numShadowQuads >= SHADOW_QUADS)
        return NULL;
    uint8_t* q = (uint8_t*)s_shadowQuads[s_numShadowQuads++];
    memset(q, 0, 0x30);
    const int16_t* b = (const int16_t*)(m->bbox + 8);
    int16_t x0 = (int16_t)(b[0] - b[3]), x1 = (int16_t)(b[0] + b[3]);
    int16_t z0 = (int16_t)(b[2] - b[5]), z1 = (int16_t)(b[2] + b[5]);
    const int16_t v[4][2] = { { x0, z0 }, { x0, z1 }, { x1, z1 }, { x1, z0 } };
    q[8] = 4;    /* vertices */
    q[9] = 1;    /* caras */
    *(uint32_t*)(q + 4) = (uint32_t)(uintptr_t)(q + 0x2c);
    for (int k = 0; k < 4; k++)
    {
        int16_t* s = (int16_t*)(q + 0xc + k * 8);
        s[0] = v[k][0];
        s[2] = v[k][1];
    }
    q[0x2c] = 0x80;
    q[0x2d] = 1;
    q[0x2e] = 2;
    q[0x2f] = 3;
    return q;
}

void Asset_ClearRegistry(void)
{
    s_numShadowQuads = 0;
    while (s_registry)
    {
        NrAsset* n = s_registry->next;
        Asset_Free(s_registry);
        s_registry = n;
    }
    s_animTexList = NULL;
}

/* 80013374. En el garaje los coches (id >= 10000) llegan por secciones al hueco que se esta cargando
 * y se registran con id 30000 + hueco (su libreia esta en el slot 9 + hueco). */
int Cobj_OnLoaded(NrAsset* a)
{
    if (a->id < 10000)
    {
        if (Asset_Find(a->type, a->id))
            return 0;   /* FUN_80018898: ya registrado */
        a->obj = Model_Register(a->data, a->size, a->id);
        if (!a->obj)
            NR_Log("Cobj %d: no se pudo registrar\n", a->id);
        Asset_Register(a);
        return 1;
    }
    if (g_IsFrontend)
    {
        if (!Fe_IsFading() && Fe_CarReceiving())
        {
            int k = g_FeLoadingSlot;
            a->obj = Model_Register(a->data, a->size, k + 30000);
            return Fe_OnCarModel(a, k + 30000);
        }
        return 0;
    }
    /* carrera: los modelos de coche pasan a 10000 + hueco de la parrilla*10 + LOD (se registran con su
     * libreia en Cars_RegisterModels al llegar la pista); 20001-20009 son comunes */
    if (a->id < 20000)
        a->id = a->id % 10 + g_CarLoadOrder[g_CarsLoaded] * 10 + 10000;
    Asset_Register(a);
    return 1;
}
