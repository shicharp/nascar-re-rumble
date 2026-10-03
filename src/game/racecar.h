/* Coches de la carrera: entidades creadas en la parrilla y su camara de persecucion.
 * Original: entidad de coche (0xE4/0xE8 bytes, manejadores HumanCar_Handler 80060190 / AiCar_Handler 80030f84;
 * +0x10 TrackPos, +0x1C id = 1000 + hueco, +0x22 tipo 1 humano / 0x11 IA, +0x30 GfxObj con la matriz en +0x34
 * y la posicion en +0x58, +0xCC velocidad, +0xE0 controlador -> fisica del coche (0x4A4 bytes),
 * +0xE4 siguiente IA), Race_CreateCars 8003132c, salida tACT tipo 0x55 800317a0, camaras RaceCam_Init
 * 8005d4c8 / 8005f034 (posicion deseada 8005e8b0, muelle 8005dc98), presets 800ab408.
 * Pendiente: la fisica real (80056c6c y los controles 80059870 / 800598dc / 8005967c), la IA (80030928,
 * 8002de7c), colisiones entre coches (8002f4ac) y paredes de la camara (8005d6b8). */
#ifndef NR_RACECAR_H
#define NR_RACECAR_H

#include <stdint.h>
#include "render.h"
#include "stream.h"
#include "track.h"

typedef struct RaceCar
{
    int      slot;          /* hueco de la parrilla (+0x1C = 1000 + slot) */
    uint8_t  type;          /* +0x22: 1 humano, 0x11 IA */
    int      player;        /* numero de jugador o -1 */
    TrackPos tpos;          /* +0x10 */
    GfxObj   obj;           /* +0x30 */
    GfxObj   wheels[4];
    int      speed;         /* +0xCC (provisional: 1/256 de unidad por tic) */
    int      yaw;           /* provisional hasta la fisica */
    int      lap;
} RaceCar;

#define RACE_MAX_CARS 8

extern RaceCar g_Cars[RACE_MAX_CARS];     /* DAT_800b12c0 */
extern int     g_NumCars;                 /* DAT_800af7cc */
extern int     g_RaceTime;                /* DAT_800af748: tics (1/300 s) desde el final de la carga */
extern int     g_StartPos[3];             /* DAT_800b12b0 */
extern int     g_StartCurve;              /* DAT_800af7b8 */
extern int     g_HaveStart;

void Race_SetStart(const int* pos);       /* 800317a0 (con la posicion del tACT tipo 0x55) */
int  Cact_OnLoaded(NrAsset* a);           /* 8004942c (solo la salida, de momento) */
void Race_CreateCars(void);               /* 8003132c */
void Race_UpdateCars(int dt);             /* provisional: conduccion cinematica del jugador */
void Race_AddCarsToDrawList(void);

void RaceCam_Init(void);                  /* 8005d4c8 */
void RaceCam_Update(int view, int dt);    /* 8005f034 */
int  RaceCam_GetSegment(int view);        /* camara +0x6C (0xFF si no hay suelo bajo ella) */

#endif
