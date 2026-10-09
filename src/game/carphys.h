/* Fisica de los coches (modulo 8004c000-8005a000 del original).
 * Physics_StepAll 80056c6c: dos sub-pasos por frame; por coche: orientacion por la velocidad angular
 * (8004cdb4), esquinas de la caja (80055010) contra el suelo (8004fdd4 / 8004fb74) y los muros (8004fb10 /
 * 8004f650), ruedas sobre la pista (80065230), suspension y fuerzas de los neumaticos (80052dbc), en el aire
 * (800524b4), choques (80050210, 80055748), integracion (80054a14, 80055f44).
 * Creacion: 8004d44c + 8004e830 (parametros 8004dc1c / 8004e1b4 / 8004da38, marchas 8004db30 / 8004d75c).
 * Controles: freno 80059870, acelerador 800598dc, direccion 8005967c; mando del jugador 8005f928 + 8004cae8. */
#ifndef NR_CARPHYS_H
#define NR_CARPHYS_H

#include <stdint.h>
#include "racecar.h"

uint8_t* Car_CreatePhys(RaceCar* e, GfxObj* wheels, const void* desc);   /* 8004d44c + 8004e830 */
void     Physics_StepAll(int dt);                                       /* 80056c6c */
void     Physics_Init(void);                                            /* tablas (8001836c / 800182ec) */
void     Physics_PlaceWheelsAll(int view);                              /* 8004f54c + 8004ef54 */

/* Mando: 8005fc54 + 8001cf74 + 8005f928 (bits de accion en ctrl + 8) y el control del coche humano 8004cae8 */
void     Human_ReadPad(RaceCar* e, int port);
void     Human_UpdateButtons(RaceCar* e, int dt);   /* 8005ffbc: R2, bocina y tiempo con un poder */
void     Human_Control(RaceCar* e);
void     Human_Buttons(RaceCar* e);     /* 8005f708: SELECT (corto camara, largo marcador), L1/L2 en automatico */

#endif
