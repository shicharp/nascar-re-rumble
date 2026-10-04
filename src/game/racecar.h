/* Coches de la carrera: entidades creadas en la parrilla y su camara de persecucion.
 * La entidad del coche conserva la disposicion del original (0xE8 bytes) porque la fisica portada accede a
 * ella por desplazamientos, igual que el codigo de la PS1 (el port es de 32 bits).
 * Original: HumanCar_Handler 80060190 / HumanCar_Create 8005fca4, AiCar_Handler 80030f84 / AiCar_Create
 * 8002cfbc, Race_CreateCars 8003132c, salida tACT tipo 0x55 Race_SetStart 800317a0, camaras RaceCam_Init
 * 8005d4c8 / RaceCam_Update 8005f034 (RaceCam_Desired 8005e8b0, RaceCam_Spring 8005dc98), presets 800ab408.
 * Fisica en carphys*.c (Physics_StepAll 80056c6c), IA, vueltas y posiciones en ai.c, choques en
 * collide.c. Pendiente: poderes y paredes de la camara (RaceCam_AvoidWalls 8005d6b8). */
#ifndef NR_RACECAR_H
#define NR_RACECAR_H

#include <stddef.h>
#include <stdint.h>
#include "render.h"
#include "stream.h"
#include "track.h"

typedef struct RaceCar
{
    struct RaceCar* next;   /* +0x00 lista de entidades */
    uint32_t unk04;
    void*    handler;       /* +0x08 */
    int      handle;        /* +0x0C numero de entidad (sonido) */
    TrackPos tpos;          /* +0x10 */
    uint8_t  pad14[8];
    uint16_t id;            /* +0x1C 1000 + hueco */
    int16_t  unk1e;
    int16_t  unk20;         /* +0x20 */
    uint8_t  type;          /* +0x22 1 humano, 0x11 IA */
    uint8_t  unk23;
    uint8_t  team;          /* +0x24 */
    uint8_t  visBits;       /* +0x25 */
    uint8_t  unk26, unk27;
    uint8_t  objUnder;      /* +0x28 actor bajo el coche (DAT_800b1b38) */
    uint8_t  pad29[3];
    uint32_t flags2c;       /* +0x2C */
    GfxObj   obj;           /* +0x30 (matriz +0x34, posicion +0x58, sombra +0x7C) */
    uint32_t unkbc;
    int      dir[3];        /* +0xC0 direccion de la velocidad */
    int      speed;         /* +0xCC */
    uint8_t  padd0[0xc];
    uint8_t* phys2;         /* +0xDC fisica (puntero de vuelta) */
    uint8_t* ctrl;          /* +0xE0 controlador (humano 0x2C4 bytes, IA 0x2A8) */
    struct RaceCar* nextAi; /* +0xE4 */
    /* solo en el port */
    uint8_t* phys;          /* fisica del coche (0x6A8 bytes): humano ctrl[0], IA ctrl[0x40] */
    int      slot;
    int      player;        /* numero de jugador o -1 */
    int      lap;
} RaceCar;

static_assert(offsetof(RaceCar, obj) == 0x30, "RaceCar.obj en +0x30");
static_assert(offsetof(RaceCar, speed) == 0xcc, "RaceCar.speed en +0xCC");
static_assert(offsetof(RaceCar, ctrl) == 0xe0, "RaceCar.ctrl en +0xE0");
static_assert(offsetof(RaceCar, phys) == 0xe8, "RaceCar: los campos del port despues de 0xE8");

#define RACE_MAX_CARS 8

extern RaceCar  g_Cars[RACE_MAX_CARS];
extern uint8_t* g_CarPhys[RACE_MAX_CARS]; /* DAT_800b12c0: fisica de cada coche (por hueco) */
extern int      g_NumCars;                /* DAT_800af7cc */
extern int      g_NumHumans;              /* DAT_800af6e8 */
extern RaceCar* g_Humans[2];              /* DAT_800af730 */
extern int      g_RaceTime;               /* DAT_800af748: tics (1/300 s) desde el final de la carga */
extern int      g_PrevRaceTime;           /* DAT_800af718 */
extern int      g_StartPos[3];            /* DAT_800b12b0 */
extern int      g_StartCurve;             /* DAT_800af7b8 */
extern int      g_HaveStart;

void Race_SetStart(const int* pos);       /* 800317a0 (con la posicion del tACT tipo 0x55) */
int  Cact_OnLoaded(NrAsset* a);           /* 8004942c (solo la salida, de momento) */
void Race_CreateCars(void);               /* 8003132c */
void Race_FreeCars(void);
void Race_UpdateCars(int dt);             /* parte de Race_UpdateSim 80030cac */
void Race_AddCarsToDrawList(void);

void RaceCam_Init(void);                  /* 8005d4c8 */
void RaceCam_Update(int view, int dt);    /* 8005f034 */
int  RaceCam_GetSegment(int view);        /* camara +0x6C (0xFF si no hay suelo bajo ella) */

#endif
