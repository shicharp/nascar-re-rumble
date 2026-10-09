/* Interno del sonido: estructuras y funciones que comparten snd.c y snd_car.c (motores). Ver snd.h. */
#ifndef NR_SND_INT_H
#define NR_SND_INT_H

#include <stdint.h>
#include "stream.h"
#include "racecar.h"

#define NUM_DEFS     0x6e
#define NUM_IDS      0x8c
#define NUM_REFS     0x96
#define NUM_VOICES   24
#define NUM_SOURCES  0x1e
#define NUM_BANKS    10
#define MAX_PATCHES  6

/* ---------------------------------------------------------------- estructuras */

typedef struct
{
    uint8_t prog;           /* programa del banco */
    uint8_t used;
    uint8_t slot;           /* hueco de snd_lib */
    uint8_t bank;           /* id del banco (Cvkh) */
} PatchRef;                 /* DAT_800b3be0 (0x96 x 4) */

typedef struct SndVoice SndVoice;

typedef struct
{
    PatchRef* patch[MAX_PATCHES];  /* +0x00 */
    uint8_t   count;               /* +0x18 */
    uint8_t   rr;                  /* +0x19 ultimo patch usado */
    uint8_t   active;              /* +0x1A voces sonando */
    uint8_t   prio;                /* +0x1B 1..10 */
    uint8_t   maxVoices;           /* +0x1C (0 = definicion libre) */
    uint8_t   b1d;                 /* +0x1D */
    uint8_t   key;                 /* +0x1E clave del Cshd */
    uint8_t   pad;
    SndVoice* voices;              /* +0x20 ordenadas por prioridad */
} SndDef;                          /* DAT_800b3e38 (0x6E x 0x24) */

typedef struct SndSource SndSource;

struct SndVoice
{
    int        def;         /* +0x00 0xFF = sin definicion */
    int8_t     vol;         /* +0x04 */
    int8_t     volReq;      /* +0x05 */
    int8_t     pan;         /* +0x06 */
    uint8_t    list;        /* +0x07 0, 1 (ordenada) o 2 */
    int        pitch;       /* +0x08 */
    int        doppler;     /* +0x0C 16.16 (0 = sin) */
    int        prio;        /* +0x10 */
    int8_t     lib;         /* +0x14 voz de snd_lib */
    int8_t     reverb;      /* +0x15 */
    int        serial;      /* +0x18 0 = libre */
    PatchRef*  patch;       /* +0x1C */
    int        basePitch;   /* +0x20 */
    SndSource* src;         /* +0x24 */
    SndVoice*  next;        /* +0x28 */
    SndVoice*  prev;        /* +0x2C */
    SndVoice*  dnext;       /* +0x30 en la lista de su definicion */
    SndVoice*  dprev;       /* +0x34 */
};

/* Resultado de 8003de88: volumen, panoramica, 1 = panoramica ya calculada, reverberacion, vectores y distancias
 * a cada oyente y el ultimo angulo por vista */
typedef struct
{
    int8_t vol, pan, done, reverb;
    int    d[2][2];
    int    dist[2];
    int8_t angle[2];
} SndPan;

struct SndSource
{
    int        handle;      /* +0x00 entidad (-1 libre) */
    int        prio;        /* +0x04 */
    int16_t    def[2];      /* +0x08 por vista */
    int8_t     kind[2];     /* +0x0C id de sonido por vista */
    int8_t     lastKind[2]; /* +0x0E */
    uint8_t    index;       /* +0x10 */
    int8_t     patchSel;    /* +0x11 */
    int        x, z;        /* +0x14 */
    SndPan     pan;         /* +0x1C */
    int        vel[3];      /* +0x3C direccion de la entidad */
    int        speed;       /* +0x48 (-1 = sin Doppler) */
    int        volScale;    /* +0x4C 16.16 */
    int        pitch;       /* +0x50 */
    SndVoice*  voice[2];    /* +0x54 */
    SndSource* next;        /* +0x5C */
    SndSource* prev;        /* +0x60 */
};

typedef struct
{
    int     pos[3];         /* +0x00 */
    int     vel[3];         /* +0x0C */
    int     heading;        /* +0x18 rumbo >> 4 (8 bits) */
    int8_t  reverb;         /* +0x1C */
    int16_t curve;          /* +0x1E */
} SndListener;              /* DAT_800b3b10 + vista * 0x20 */

typedef struct
{
    int8_t   lib;           /* hueco de snd_lib (-1 libre) */
    int8_t   bank;          /* id del banco */
    NrAsset* hdr;
} BankEntry;                /* DAT_800b0948 (10 x 0x10) */

typedef struct
{
    SndVoice* v;
    int       serial;
} SndHandle;                /* 8003fca4 / 8003fccc / 8003fcf8 */

extern SndListener s_lis[2];
extern int         s_raceActive;
extern unsigned    s_tick;

int       Snd_LogOn(void);                                                      /* NR_SNDLOG */
int       Def_FromId(int id);                                                   /* 8003d89c */
SndVoice* Voice_Alloc(int vol, int cls, int list, int def);                     /* 8003f550 */
void      Voice_Free(SndVoice* v);                                              /* 8003f0c8 */
void      Voice_StopLib(SndVoice* v);                                           /* 80023910 */
void      Voice_Set(SndVoice* v, int pan, int vol, int pitch, int reverb);      /* 800400a0 */
void      Voice_Fade(SndVoice* v);                                              /* 800415fc */
int       Def_Play(int def, SndVoice* v, int pan, int vol, int pitch, int sel, int reverb);   /* 8003ff20 */
int       Snd_Listen(int x, int z, SndPan* o, const RaceCar* ent, int kind);    /* 8003de88 */
void      Snd_PanFinish(SndPan* o);                                             /* 8003e784 */
int       Snd_Doppler(int* lvel, const int* lpos, const int* sdir, int sspeed, const int* spos);   /* 80041948 */
SndVoice* Snd_PlayDef(int x, int z, int def, const RaceCar* ent, int list, int scale, int kind, int sel,
                      int volOverride);                                         /* 8003f898 */

/* motores (snd_car.c) */
int  Engine_BanksForCar(int type);        /* 80041e34 */
void Engine_UpdateAll(int dt);            /* 80040c58 */
void Engine_StopAll(void);                /* 8004170c */
int  Engine_NextMainBank(void);           /* 8003d128: bancos 2..7 (ids 4 motor y 5 bocina) */
int  Engine_NextExtraBank(void);          /* 8003d160: bancos 8..9 (id 3, motor sin gas de los humanos) */
int  Engine_OnCeng(NrAsset* a);           /* parte de 80022908 */

#endif
