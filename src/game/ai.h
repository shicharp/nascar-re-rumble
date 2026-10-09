/* IA de los coches (Ai_UpdatePath 80030928, Ai_UpdateTarget 8002de7c, Ai_Drive 8002f4ac), vueltas
 * (Car_UpdateLap 8003014c) y posiciones (Race_UpdatePositions 8002fc9c). */
#ifndef NR_AI_H
#define NR_AI_H

#include <stdint.h>
#include "racecar.h"

void Ai_Reset(void);
void Ai_InitCar(RaceCar* e);             /* AiCar_Create 8002cfbc (antes de la fisica) */
void Ai_InitPhys(RaceCar* e);            /* AiCar_Create 8002cfbc (despues) */
void Ai_UpdateAll(int dt);               /* ruta, objetivo, conduccion y mandos (80030cac) */
void Ai_UpdateLaps(void);                /* Car_UpdateLap de la IA y de los humanos */
void Car_UpdateLap(uint8_t* phys, int curve);   /* 8003014c */
void Race_UpdatePositions(int dt);       /* 8002fc9c */
int  Car_TrackDeltaPublic(uint8_t* p1, uint8_t* p2);   /* 8002f094: puntos de TCRV de ventaja de p1 */
void Ai_Recover(RaceCar* e);             /* 8002ea48: vuelve a la red (o a la pista) */

#endif
