/* Modelos Cobj y registro de assets.
 * Original: Cobj_OnLoaded 80013374, Model_Register 8001315c, Model_ParseChunks 80012c08,
 * Model_ResolveRefList 80012de0, Model_PatchFaces 80012f14; lista de assets registrados DAT_800af5b0
 * (Asset_Register 8001860c, Asset_Find 80018788, Asset_IsRegistered 80018640).
 * Como el original, Model_Register enlaza los subchunks en el propio buffer del asset (punteros de 32
 * bits: el port es Win32). */
#ifndef NR_MODEL_H
#define NR_MODEL_H

#include <stdint.h>
#include "stream.h"

/* Cabecera 5DGI tras Model_Register */
typedef struct
{
    uint32_t  tag;          /* +0x00 'IGD5'; en el garaje, equipo << 4 | clase */
    uint32_t* animTex;      /* +0x04 chunk 4DTA (texturas animadas) */
    uint8_t   pad08[3];
    int8_t    envCount;     /* +0x0B pasadas de reflejo */
    uint32_t  flags;        /* +0x0C bits 22-23 nivel de mip, 24 4DTA, 25 puntos 3DHS, 30 sombra */
    uint32_t* anim;         /* +0x10 3DTL / AnmD */
    uint8_t*  shadow;       /* +0x14 3DSD */
    uint32_t  unk18;
    uint32_t* verts;        /* +0x1C [n][4DVL* por frame]; cada 4DVL = [n][SVECTOR...] */
    uint32_t* normals;      /* +0x20 igual con 4DNL */
    uint32_t* faces;        /* +0x24 [n][caras de 24 bytes] */
    uint32_t* rl;           /* +0x28 3DRL */
    uint8_t   hardpoint[12];/* +0x2C vertice de cada punto (ruedas...) */
    uint8_t*  bbox;         /* +0x38 3DBB (+8 + frame*16: centro y radio) */
    uint32_t* points;       /* +0x3C 3DHS */
} ModelHdr;

static_assert(sizeof(ModelHdr) == 0x40, "ModelHdr debe medir 0x40");

ModelHdr* Model_Register(uint8_t* data, int size, int id);   /* 8001315c */

/* Vertices (SVECTOR) y numero de vertices del frame f */
static inline const uint8_t* Model_FrameVerts(const ModelHdr* m, int f, int* count)
{
    const uint32_t* v = (const uint32_t*)(uintptr_t)m->verts[1 + f];
    *count = (int)v[0];
    return (const uint8_t*)(v + 1);
}

/* Assets que se quedan en memoria (modelos sueltos, tablas...) */
void     Asset_Register(NrAsset* a);              /* 8001860c */
NrAsset* Asset_Find(uint32_t type, int id);       /* 80018788 */
void     Asset_ClearRegistry(void);

int Cobj_OnLoaded(NrAsset* a);                    /* 80013374 */
uint8_t* Model_MakeShadowQuad(const ModelHdr* m);
void Model_AnimateTextures(int dt);                /* 8001364c */  /* 8001309c: sombra cuadrada para los modelos sin 3DSD */

#endif
