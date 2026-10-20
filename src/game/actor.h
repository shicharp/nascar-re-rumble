/* Actores de la pista (assets Cact) y entidades que no son coches.
 * Original: Cact_OnLoaded 8004942c (tACT tipo 0x54 FUN_80072208, 0x55 salida Race_SetStart 800317a0, resto
 * entidades con el manejador de 8002ca3c), Entity_Create 800491ec (mensajes: 1 tamano, 2 crear, 3 dibujar,
 * 5 destruir, 6 actualizar, 10 preguntar si choca, 0xB choque), Entity_DefaultHandler 8002c894, creacion
 * comun 8002c2a0, actualizacion 80048dec (mensaje 6), visibles 80061a0c + Actors_DrawView 80061b28
 * (mensaje 3), choques Cell_CollideSelf 80061608 / Entity_TestPair 800613b4, animacion de modelos
 * 8003c95c / 8003c8ac / 8003cb08 y Obj_SetFrame 80013aa4.
 * La entidad conserva la disposicion del original (cabecera de 0xE0 bytes comun con los coches; despues, los
 * datos de cada tipo) porque el codigo portado accede a ella por desplazamientos.
 * Tipos creados: 0x06 objetos que giran (800339b4), 0x0B objetos de la pista (8003421c) y 0x16 cajas de poder
 * (80031854), 0x0E disparadores (80034894) y 0x05 vehiculos que siguen una red (80034e6c, vehicle.c); los
 * poderes crean 0xD (objetos que se sueltan) y 8 (clima) en powerup.c. El 0x54 son las camaras de TV (tvcam.c).
 * Los generadores Csac (800605f0) crean entidades durante la carrera. Los objetos sueltos (cuerpos con la fisica
 * de los coches al golpearlos) estan en body.c. */
#ifndef NR_ACTOR_H
#define NR_ACTOR_H

#include <stddef.h>
#include <stdint.h>
#include "render.h"
#include "stream.h"
#include "track.h"

struct Actor;
typedef int (*ActorFn)(int msg, struct Actor* a, intptr_t p1, intptr_t p2);

/* Estado de la animacion de un modelo (0x14 bytes; secuencias de 16 bytes en el 3DTL/AnmD del modelo:
 * +0 siguiente, +1 tipo (0 quieta, 3 por parametro), +2 bucle (0 ida, 1 ida y vuelta, 2 repetir), +3 bits
 * (0x20 ritmo al azar, 0x40 siguiente al azar), +4 / +6 fotogramas inicial y final, +8 / +9 siguientes,
 * +0xA evento, +0xC duracion de un fotograma) */
typedef struct
{
    const uint8_t* seqs;    /* +0x00 */
    const uint8_t* cur;     /* +0x04 */
    int      time;          /* +0x08 */
    int16_t  frame;         /* +0x0C */
    int16_t  target;        /* +0x0E */
    int8_t   dir;           /* +0x10 */
    uint8_t  seq;           /* +0x11 */
    uint8_t  count;         /* +0x12 */
    uint8_t  flags;         /* +0x13 1 cambio pendiente, 2 parada */
} AnimState;

static_assert(sizeof(AnimState) == 0x14, "AnimState debe medir 0x14");

typedef struct Actor
{
    struct Actor* next;     /* +0x00 */
    struct Actor* cellNext; /* +0x04 lista de su celda de TCRV */
    ActorFn  handler;       /* +0x08 */
    int      handle;        /* +0x0C numero de entidad (> 0 viva) */
    TrackPos tpos;          /* +0x10 */
    uint8_t  cmds[8];       /* +0x14 ordenes del guion {orden, argumento} x 4 */
    uint16_t id;            /* +0x1C */
    int16_t  unk1e;         /* +0x1E */
    int16_t  hp;            /* +0x20 */
    uint8_t  type;          /* +0x22 */
    uint8_t  unk23;
    uint8_t  team;          /* +0x24 */
    uint8_t  visible;       /* +0x25 */
    uint8_t  unk26, unk27;
    uint8_t  objClass;      /* +0x28 clase de objeto (DAT_800b1b38) */
    uint8_t  pad29[3];
    uint32_t flags2c;       /* +0x2C 0x10000 siempre visible, 0x20 no se dibuja, 0x200 / 0x2000 choca */
    GfxObj   obj;           /* +0x30 */
    ModelHdr* model2;       /* +0xBC */
    int      vel[3];        /* +0xC0 */
    int      speed;         /* +0xCC */
    void*    script[3];     /* +0xD0 */
    void*    body;          /* +0xDC */
    uint8_t  ext[0x80];     /* +0xE0 datos de cada tipo */
    /* solo en el port */
    const uint8_t* def;     /* tACT (+8: id, tipo, x, y, z...) */
    const uint32_t* res;    /* aRSL resuelta: [n] y {tipo, NrAsset*} */
} Actor;

static_assert(offsetof(Actor, obj) == 0x30, "Actor.obj en +0x30");
static_assert(offsetof(Actor, model2) == 0xbc, "Actor.model2 en +0xBC");
static_assert(offsetof(Actor, ext) == 0xe0, "Actor.ext en +0xE0");

/* Campos de cada tipo por desplazamiento (como en el original) */
#define A8(a, o)   (*(uint8_t*)((uint8_t*)(a) + (o)))
#define AS8(a, o)  (*(int8_t*)((uint8_t*)(a) + (o)))
#define A16(a, o)  (*(int16_t*)((uint8_t*)(a) + (o)))
#define AU16(a, o) (*(uint16_t*)((uint8_t*)(a) + (o)))
#define A32(a, o)  (*(int32_t*)((uint8_t*)(a) + (o)))

/* Parametros de creacion (Entity_Create 800491ec): manejador, aRSL, tACT y asset */
typedef struct
{
    ActorFn handler;
    const uint32_t* res;
    const uint8_t* def;
    NrAsset* asset;
} ActorDef;

extern Actor* g_Actors;     /* DAT_800af87c */

Actor* Actor_Create(const ActorDef* d);                 /* Entity_Create 800491ec */
void   Actor_Destroy(Actor* a);                         /* 80049688 (marca; se libera en Actors_Free) */
int    Actor_DefaultHandler(int msg, Actor* a, intptr_t p1, intptr_t p2);   /* 8002c894 */
ModelHdr* Actor_ResModel(const Actor* a, int i);        /* modelo i de la aRSL */
void   Actor_RemoveCmd(Actor* a, int i);                /* 8002c1a8: quita la orden i de la cola */
void   Actor_FindTrackPos(Actor* a);                    /* 8002c1e8 */
uint8_t* Ent_ByHandle(int handle);                     /* solo en el port: actor o coche vivo con ese numero */
void   Actor_UpdateCenter(Actor* a);                    /* 80015928 + 80016784: centro de la caja (+0x64) */

int  Cact_OnLoaded(NrAsset* a);                         /* 8004942c */
void Actors_LogSummary(void);
void Actors_Free(void);                                 /* 80048d5c */
int  Ent_NewHandle(void);                               /* ++DAT_800af870 */
void Actors_Update(int dt);                             /* 80048dec(6, dt) */
void Actors_PostUpdate(void);                           /* 80048e84: mensaje 4 a las de vida < 0 */
int  Actors_OnScriptPool(NrAsset* as);                  /* 80049628: el RPNS de la pista */
void Actors_Activate(void);                             /* 80049b74: sonidos de ambiente y guion del cercano */
void Actors_RunOrders(void);                            /* 80048f78: ordenes genericas de los guiones */
int  Actor_PushCmd(Actor* a, int cmd, int arg);         /* 80049788 (llena: a la cola de reintento) */
void Actors_RetryOrders(int dt);                        /* 800497f4 */

/* Mensaje 0xE: dano directo (orden 0x3C) */
typedef struct
{
    uint32_t id;
    int16_t  dmg;
    uint8_t  kind;
    uint8_t  pad;
    uint32_t extra;
} DamageMsg;
void Actors_CmdById(int id, int cmd, int arg);          /* 800498e4 */
void Actors_CmdByGroup(int group, int cmd, int arg);    /* 800499b4 (+0x23) */
void Actors_CmdByTeam(int team, int cmd, int arg);      /* 80049a40 (+0x24) */
void Actors_AddToDrawList(int view);                    /* 80061a0c + 80061b28 */

/* Generadores Csac (crean entidades de un tACT cada cierto tiempo, ver actor.c) */
int  Csac_OnLoaded(NrAsset* as);                        /* 800605f0 */
void Csacs_Update(int dt);                              /* 80060a60 */
void Csacs_CmdById(int id, int cmd, int arg);           /* 8006094c */
void Csacs_CmdByGroup(int group, int cmd, int arg);     /* 800609c0 */
void Actors_CollideCars(void);                          /* coches contra actores en 80061808 */

/* Animacion de modelos */
void Anim_Init(const uint32_t* anim, AnimState* s, int forward, int seq);   /* 8003c95c */
const uint8_t* Anim_SetSeq(AnimState* s, int seq, int mode, int flags);     /* 8003c8ac */
uint32_t Anim_Step(Actor* a, AnimState* s, int dt, uint32_t pick);          /* 8003cb08 */
void Obj_SetFrame(GfxObj* o, int frame);                                    /* 80013aa4 */

#endif
