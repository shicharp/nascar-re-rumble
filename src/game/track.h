/* Pista (Ctrk): carga y dibujo de los segmentos.
 * Original: Track_Parse 80062088 (estructura DAT_800af8d8), tablas de texturas por mip FUN_80067218,
 * Track_DrawView 800644c0 (visibilidad 800637bc / 80063928 / 80063a34, distancias de LOD 800641b8),
 * Track_DrawSegments 8006327c, Track_TransformSegVerts 80063470, Track_DrawSegFaces 80062de0 y la
 * subdivision de las caras cercanas 80062970 (vertices 800626b8, primitiva 80062888).
 * Coordenadas de la pista = las del mundo >> 4. */
#ifndef NR_TRACK_H
#define NR_TRACK_H

#include <stdint.h>

/* Cabecera de un TSEG (los punteros del original en +0x00/+0x04/+0x28/+0x40 van en TrackSeg) */
typedef struct
{
    uint32_t tag, size;     /* +0x00 */
    int32_t  origin[3];     /* +0x08 */
    int16_t  half[3];       /* +0x14 semiextension de la caja */
    int16_t  radius;        /* +0x1A */
    uint16_t curveEnd;      /* +0x1C ultimo punto de TCRV */
    uint16_t curveStart;    /* +0x1E primero */
    uint8_t  numRows;       /* +0x20 */
    uint8_t  numVerts;      /* +0x21 */
    uint8_t  numFaces;      /* +0x22 */
    uint8_t  numC;          /* +0x23 registros de 8 bytes */
    uint8_t  numD;          /* +0x24 objetos/carteles de 4 bytes */
    uint8_t  visRange;      /* +0x25 segmentos visibles: (atras << 4) | adelante */
    uint8_t  horizon;       /* +0x26 */
    uint8_t  viewMask;      /* +0x27 */
} TrackSegHdr;

typedef struct
{
    TrackSegHdr*    hdr;
    const uint32_t* verts;  /* seg[0] */
    const uint8_t*  faces;  /* seg[1]: 10 bytes por cara */
    const uint8_t*  d;      /* seg[10] */
    const uint8_t*  rest;   /* seg[0x10] */
} TrackSeg;

typedef struct
{
    uint32_t*       colors;     /* [0] TCOL convertido a palabras RGB */
    uint8_t*        tex[4];     /* [1..4] TTEX por nivel de mip: {uv0, clut, uv1, tpage, uv2, uv3} */
    const uint8_t*  curve;      /* [5] TCRV */
    uint8_t*        horizon;    /* [6] THOR */
    uint8_t*        tigr;       /* [7] TIGR */
    int             numSegs;    /* +0x20 */
    int             numTex;     /* +0x22 */
    int             bbMin[3];   /* [9..11] */
    int             bbMax[3];   /* [12..14] */
    int             numCurve;   /* puntos de TCRV (10 bytes) */
    TrackSeg*       segs;       /* [0xF..] */
} Track;

extern Track* g_Track;          /* DAT_800af8d8 */

void Track_Parse(uint8_t* data, int size);          /* 80062088 */
void Track_Free(void);

/* Punto i de la linea central en coordenadas del mundo */
void Track_GetCurvePoint(int i, int* out);
/* Solo en el port (el original lo lleva la camara, +0x6C): segmento que contiene la posicion del mundo */
int  Track_FindSegment(const int* pos);

void Track_DrawView(int view, int curSeg, int mirrorCam);   /* 800644c0: visibilidad de los segmentos */
void Track_DrawSegments(int view, int mirror);              /* 8006327c */

#endif
