/* Poderes: cajas de la pista (tACT 0x16) y efectos sobre los coches.
 * Original: Pickup_Handler 8003203c (crear 80031854, girar 80031f04, recoger Pickup_Collect 800319c4),
 * Car_UpdatePowerUps 8002ec00 (usar el que se lleva y descontar los activos), Car_AddEffect 8002f268,
 * duraciones 800aa90c ({u16 tics, 0, icono} x 11; la del misterioso, 800aa92c, es su caducidad), turbo
 * 80050130 / 800501b8, onda expansiva 8005b2a0 / 8005b068, velocidad 8004ffec.
 * Campos de la fisica del coche: +0x338 poder en la mano (-1 ninguno), +0x339 numero de efectos activos,
 * +0x33A mascara de activos, +0x33C espera de la IA para usarlo, +0x33E caducidad del misterioso, +0x340 + i * 4
 * {tipo, 0, u16 tics} x 4.
 * Poderes (por lo que hacen; los bits de +0x33A los lee la fisica): 0 y 4 proyectiles (80033468; con el
 * misterioso caen sobre uno mismo: 0 acelerador -75 %, 4 -25 % y ruedas planas), 1 helada (sin direccion),
 * 5 tormenta y 7 tornado (80033274, entidades de clima), 2 escudo (anula los malos), 3 turbo (otra tabla de
 * marchas), 6 ruedas grandes, 9 embestida (invulnerable, +25 %), 10 onda expansiva, 8 misterioso (uno al
 * azar al usarlo). */
#ifndef NR_POWERUP_H
#define NR_POWERUP_H

#include <stdint.h>

struct Actor;

#define POWER_COUNT    11
#define POWER_MYSTERY  8

int  Pickup_Handler(int msg, struct Actor* a, intptr_t p1, intptr_t p2);   /* 8003203c */
void Pickup_Reset(void);                                 /* modelos globales DAT_800af7d0 / 7c8 / 7c0 */

int  Power_Duration(int type);                           /* 800aa90c */
int  Car_AddEffect(uint8_t* c, int type, int ticks);     /* 8002f268 */
void Car_UpdatePowerUps(uint8_t* c, int dt, int use);    /* 8002ec00 */

#endif
