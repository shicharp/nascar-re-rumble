/* Interno de los efectos visuales (fx.c y un fichero por tipo de efecto).
 * Las plantillas son los registros del Csfx copiados tal cual (se leen por desplazamiento como en el original;
 * el juego cambia algunos campos en marcha, 800774a4 / 800751cc). Mapa de campos en NOTAS.md. */
#ifndef NR_FX_INT_H
#define NR_FX_INT_H

#include <stdint.h>
#include "fx.h"
#include "render.h"
#include "racecar.h"

#define T8(t, o)   (*(uint8_t*)((uint8_t*)(t) + (o)))
#define TS8(t, o)  (*(int8_t*)((uint8_t*)(t) + (o)))
#define T16(t, o)  (*(int16_t*)((uint8_t*)(t) + (o)))
#define TU16(t, o) (*(uint16_t*)((uint8_t*)(t) + (o)))
#define T32(t, o)  (*(int32_t*)((uint8_t*)(t) + (o)))

/* Campos comunes de las plantillas */
#define TP_TYPE    0x04   /* u8 0x57..0x5E */
#define TP_POS     0x08   /* 3 x int (efectos fijos en la pista, bandera 1) */
#define TP_OFSYAW  0x18   /* s16 (< 0: al azar) */
#define TP_OFSPIT  0x1a
#define TP_OFSDIST 0x1c   /* s16 desplazamiento (8.8) */
#define TP_LP      0x1e   /* s8 punto local del modelo */
#define TP_CLASS   0x14
#define TP_NEXT    0x15   /* clase siguiente de la cadena */
#define TP_SND     0x16
#define TP_TIME    0x20   /* int 16.16 s: vida de las particulas / temporizador +0xC */
#define TP_LIFE    0x24   /* int 16.16 s: vida de la instancia */
#define TP_REPEAT  0x28   /* s16: repeticiones (-1 ninguna, 0 sin fin) */
#define TP_INTERV  0x2a   /* s16: intervalo entre emisiones */
#define TP_COUNT   0x2c   /* s16 */
#define TP_MSG9    0x2e
#define TP_FLAGS   0x32   /* u16: 1 fijo en la pista, 2 no muere al vaciarse, 4 con la rotacion del objeto,
                           * 0x40, 0x80 vertice al azar, 0x100 marco local, 0x400 visible si lo es el objeto,
                           * 0x800, 0x1000, 0x4000 sin distancia, 0x8000 menos particulas de lejos */

typedef struct FxInst FxInst;

/* Parametros de creacion (0x4C bytes en la pila de los llamadores) */
typedef struct FxSpawn
{
    int     scale;       /* +0x00 (bandera 1) */
    int     vel[3];      /* +0x04 (bandera 2) */
    int     pos[3];      /* +0x10 (bandera 4) */
    int     rot[9];      /* +0x1C (bandera 8) */
    int     unk40;
    GfxObj* obj;         /* +0x44 (bandera 0x40) */
    int8_t  lp;          /* +0x48 punto local (bandera 0x20) */
    uint8_t flags;       /* +0x49 */
} FxSpawn;

/* Aviso al dueno del ancla (+0x48: {dato, funcion}): 1 cada frame (lista 6), 2 al morir una instancia */
typedef struct FxCallback
{
    void* user;
    void (*fn)(int msg, FxInst* in, struct FxCallback* cb);
} FxCallback;

typedef struct FxPersist FxPersist;

/* Ancla (0x4C bytes, lista DAT_800af23c): entidad o marco fijo al que se pegan las instancias */
typedef struct FxAnchor
{
    struct FxAnchor* next;   /* +0x00 */
    void*    ent;            /* +0x04 entidad (cabecera comun: +0x0C handle, +0x30 GfxObj) */
    int      entHandle;      /* +0x08 (-1 si la entidad no tenia) */
    int      rot[9];         /* +0x04 en el original (solapado con la entidad si el ancla es fija) */
    int      pos[3];         /* +0x28 */
    GfxObj*  obj;            /* +0x34 */
    uint8_t  isStatic;       /* +0x38 */
    uint8_t  flags;          /* +0x39 1 desligada, 2 copia propia del objeto, 4 sin copia, 8 sin vivas */
    int16_t  count;          /* +0x3A instancias */
    int16_t  scale;          /* +0x3C */
    int16_t  live;           /* +0x3E instancias sin la bandera de clase 4 */
    uint8_t  unk42;
    FxPersist* owner;        /* +0x44 */
    FxCallback* cb;          /* +0x48 */
} FxAnchor;

typedef int (*FxHandler)(int msg, FxInst* in, intptr_t a, const FxSpawn* sp);

/* Cabecera comun de las instancias (0x64 bytes) */
struct FxInst
{
    FxInst*  next;           /* +0x00 */
    int16_t  life;           /* +0x04 tics hasta vencer (< 0 sin fin) */
    uint8_t  nextCls;        /* +0x06 */
    int8_t   repeats;        /* +0x07 */
    int16_t  prims;          /* +0x08 / +0x0A presupuesto de primitivas */
    int16_t  prims2;
    int16_t  killTimer;      /* +0x0C */
    int8_t   lp;             /* +0x0E */
    uint8_t  state;          /* +0x0F 1 muerta, 2 vencida, 4 emitir, 8 suspendida */
    uint8_t  cflags;         /* +0x10 banderas de la clase | 2 banco 1 */
    uint8_t  snd;            /* +0x11 */
    int16_t  scale;          /* +0x12 */
    int16_t  interval;       /* +0x14 */
    int16_t  delay;          /* +0x16 */
    int16_t  counter;        /* +0x18 */
    uint16_t tflags;         /* +0x1A copia de TP_FLAGS */
    FxAnchor* anchor;        /* +0x1C */
    uint8_t* tmpl;           /* +0x20 */
    FxHandler handler;       /* +0x24 */
    int      origin[3];      /* +0x28 */
    int      frame[12];      /* +0x34 rotacion (columnas) + posicion (+0x58) */
};

/* Registro persistente (0x20 bytes, lista DAT_800af234): efectos que se encienden al acercarse */
struct FxPersist
{
    FxPersist* next;
    uint8_t  cls;            /* +0x04 */
    int      bank;           /* +0x08 */
    void*    ent;            /* +0x10 */
    int      entHandle;      /* +0x14 */
    FxSpawn* spawn;          /* +0x18 copia */
    uint8_t  flags;          /* +0x1C 1 encendido */
};

/* fx.c */
uint8_t* Fx_Template(int bank, int cls);
int      Fx_ListOf(int type);
void     Fx_InitInst(FxInst* in, uint8_t* t);                                  /* 80035b18 */
int      Fx_BaseHandler(int msg, FxInst* in);                                 /* 80036170 */
int      Fx_MiscHandler(int msg, FxInst* in);                                 /* 800362f4 */
void     Fx_SetupFrame(uint8_t* t, FxInst* parent, FxAnchor* an, const FxSpawn* sp, int* frame,
                       int16_t* scale, int8_t* lp);                            /* 8003742c */
const int* Fx_WorldFrame(GfxObj* obj, unsigned tflags, int lp, int doRot, FxAnchor* an, const int* frame);   /* 800372b4 */
const int* Fx_BaseFrame(FxInst* in);                                          /* 80037280 */
void     Fx_Focus(int view, int* out);                                        /* 80035914 */
void     Fx_RotI(const int* m, const int* in, int* out, int n);               /* 8001006c con la matriz m */
void     Fx_RotS(const int* m, const int16_t* in, int16_t* out);              /* 8001010c con la matriz m */
void     Fx_ProjectVerts(const int16_t* v, int n, uint32_t* sxy, int* sz, uint8_t* fl);   /* 800688c8 */
void     Fx_LoadMatrix(const int* m);                                         /* 8001025c */
extern int g_FxParticles;                                                     /* DAT_800af7f0 */

/* fx_psys.c: sistemas de particulas (tipo 0x5E) */
FxInst*  Psys_Create(uint8_t* t, GfxObj* obj);                                /* 8003c08c */
void     Psys_UpdateAll(FxInst* list, int dt);                                /* 8003b77c */
void     Psys_DrawAll(FxInst* list, const int* focus, int view);              /* 8003be24 */
void     Psys_InitSprites(void);                                              /* 8003bd60 */
void     Psys_SetSpeed(FxInst* in, int min, int max);                        /* 800751cc (+0x80 / +0x82) */

/* fx_ring.c: anillos y cupulas (tipo 0x5C) */
typedef struct FxDome FxDome;
FxInst*  Ring_Create(uint8_t* t);                                             /* 8006a578 */
void     Ring_UpdateAll(FxInst* list, int dt);                                /* 8006939c */
void     Ring_DrawAll(FxInst* list, const int* focus);                        /* 8006a394 */
FxDome*  Dome_Create(int step, int mode);                                     /* 80068400 */
void     Dome_Free(FxDome* d);                                                /* 80068654 */
void     Dome_SetScale(FxDome* d, int x, int y, int z);                       /* +0x19..+0x1B */
void     Dome_Draw(FxDome* d, const int* w, int r, const uint8_t* c0, const uint8_t* c1);   /* 80068a18 */

/* fx_marks.c: marcas de las ruedas y su humo */
void     Fx_QueueWheel(uint8_t* c, int k);                                    /* 80075130 (tipo 3) */
void     Fx_WheelSmoke(uint8_t* c, int k);                                    /* 80075330 */
void     Marks_Draw(uint8_t* c, int view);                                    /* 80076a1c */

#endif
