#include <string.h>
#include "nr.h"
#include "mem.h"
#include "globals.h"
#include "texslot.h"
#include "frontend.h"
#include "model.h"

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

void Asset_ClearRegistry(void)
{
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
    /* TODO carrera: id < 20000 se renumera segun el coche (DAT_800af6cc) y se registra */
    Asset_Register(a);
    return 1;
}
