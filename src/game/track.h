/* Pista (Ctrk): carga y dibujo de los segmentos.
 * Original: Track_Parse 80062088 (estructura DAT_800af8d8), tablas de texturas por mip FUN_80067218,
 * Track_DrawView 800644c0 (visibilidad 800637bc / 80063928 / 80063a34, distancias de LOD 800641b8),
 * Track_DrawSegments 8006327c, Track_TransformSegVerts 80063470, Track_DrawSegFaces 80062de0 y la
 * subdivision de las caras cercanas 80062970 (vertices 800626b8, primitiva 80062888) y el horizonte THOR
 * (texturas 800673f8, caras planas 80062004, dibujo 80064078 / 80064134 / 80063af4 / 80063c08), las texturas
 * animadas TTXA (80061dfc / 80061e94) y los halos de luz de los segmentos (objetos D, ver sprite.h).
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

/* Capa de horizonte THOR (en el original una lista enlazada por +0x00, la ultima primero) */
typedef struct
{
    const uint8_t*  hdr;        /* +0x0C/+0x10/+0x14 centro (mundo >> 7), +0x18 radio, +0x1E bit de la mascara */
    const uint8_t*  verts;      /* +0x04 tras el parser: 8 bytes {u16 x, u16 y, s16 z, pad} (mundo >> 7) */
    const uint8_t*  faces;      /* +0x08: 20 bytes {v0-v3, c0-c3, uv0|clut, uv1|tpage, uv3|uv2} */
    int             numVerts;   /* +0x1C */
    int             numFaces;   /* +0x1D */
    int             bit;        /* +0x1E */
} TrackHorizon;

#define TRACK_MAX_HORIZONS 16

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
    TrackHorizon    horizons[TRACK_MAX_HORIZONS];   /* [6] */
    int             numHorizons;
    uint8_t*        anims;      /* DAT_800af904: TTXA, 0x1C bytes por textura animada */
    int             numAnims;
} Track;

extern Track* g_Track;          /* DAT_800af8d8 */

/* Posicion sobre la pista (4 bytes que el juego guarda en cada objeto, p. ej. entidad +0x10, camara +0x6C) */
typedef struct
{
    uint8_t seg;                /* 0xFF = desconocido */
    uint8_t face;
    int16_t curve;              /* punto de TCRV */
} TrackPos;

#define TRACK_NO_SURFACE (-0x40000000)

int  Track_FindSurface(TrackPos* tp, const int* pos, int withCurve);   /* 80064acc (sin la normal) */
int  Track_GroundHeight(TrackPos* tp, const int* pos, int mode);       /* 80065488 */

void Track_Parse(uint8_t* data, int size);          /* 80062088 */
void Track_Free(void);

/* Punto i de la linea central en coordenadas del mundo */
void Track_GetCurvePoint(int i, int* out);
/* Solo en el port (el original lo lleva la camara, +0x6C): segmento que contiene la posicion del mundo */
int  Track_FindSegment(const int* pos);
/* Solo en el port: segmento cuyo tramo de TCRV (+0x1E..+0x1C) esta mas cerca del punto i */
int  Track_FindSegmentByCurve(int i);

void Track_DrawView(int view, int curSeg, int mirrorCam);   /* 800644c0: visibilidad de los segmentos */
void Track_DrawSegments(int view, int mirror);              /* 8006327c */
void Track_DrawHorizon(int view, int mirror);               /* 80064078 (antes de los segmentos) */
void Track_Animate(int dt);                                 /* 80061e94: texturas animadas (TTXA) */

#endif
