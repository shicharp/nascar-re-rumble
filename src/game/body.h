/* Objetos sueltos: cuerpos con la fisica de los coches para los actores golpeados (ver body.c) */
#ifndef NR_BODY_H
#define NR_BODY_H

#include <stdint.h>

struct Actor;
struct RaceCar;

uint8_t* Body_Ensure(struct Actor* a, int mass);           /* 8005cd50 */
void Body_Free(struct Actor* a);                           /* 8004de8c */
int  Body_Step(uint8_t* b, int dt);                        /* 8005610c: 1 = dormido */
void Body_Fling(uint8_t* b, int speed);                    /* 8005bebc */
int  Body_CarHitsActor(struct RaceCar* car, struct Actor* a);   /* 8005cdb8 (lado del coche) */
int  Body_Count(void);                                     /* DAT_800af310 */
/* 8005c05c: choque de dos cuerpos; car1 / car2: el cuerpo es la fisica de un coche */
int  Body_CollidePair(uint8_t* b1, int car1, uint8_t* b2, int car2);

#endif
