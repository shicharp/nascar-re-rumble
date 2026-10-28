/* Carga de la carrera y bucle de carrera.
 * Original: Race_Load 80024284, coche siguiente FUN_80023ef4, progreso FUN_80024f08, Ctrk_OnLoaded 80062680,
 * Cars_RegisterModels 800134f0, Race_MainLoop 800299a8. */
#ifndef NR_RACE_H
#define NR_RACE_H

#include <stdint.h>
#include "stream.h"

extern uint8_t g_CarLoadOrder[8];   /* DAT_800af6cc: huecos de la parrilla ordenados por modelo */
extern int     g_CarsLoaded;        /* DAT_800b0dbc: secciones de coche terminadas (rscE) */

void Race_Load(const char* path);   /* 80024284 */
int  Race_NextCar(void);            /* 80023ef4: pide la seccion del siguiente coche; 0 si no quedan */
void Race_DrawProgress(void);       /* 80024f08 (solo la barra) */
int  Ctrk_OnLoaded(NrAsset* a);     /* 80062680 + Cars_RegisterModels 800134f0 */
void Race_MainLoop(void);           /* 800299a8: de momento, visor de los coches cargados */
void Race_StateChecksum(uint32_t out[3]);   /* solo en el port: coches, actores y aleatorios (desincronizaciones) */

#endif
