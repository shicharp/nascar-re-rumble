/* Camaras de TV: las del tACT tipo 0x54 de cada pista (zonas con una camara fija o que sigue al coche) y 8
 * virtuales pegadas al coche (orbita, grua, rueda, lateral...). Se usan al cruzar la meta (orbita alrededor del
 * coche y luego cambios de camara), en las repeticiones y en la demostracion sin humanos.
 * Original: lista DAT_800af938 (0x48 bytes por camara), crear Emitter_Create 80071f50 (desde Emitter_OnLoaded
 * 80072208, Cact 0x54), virtuales 80072278 y tabla 80071e4c (DAT_800af928 / DAT_800af92c, desde Level_Run),
 * liberar RaceCam_ResetPresets 80072348, camara activa por vista DAT_800af930, modo TV DAT_800af8c8. */
#ifndef NR_TVCAM_H
#define NR_TVCAM_H

#include <stdint.h>
#include "track.h"

void TvCam_Free(void);                          /* 80072254 / 80072348 */
void TvCam_AddFromDef(const uint8_t* def);      /* 80071f50 (tACT tipo 0x54) */
void TvCam_PostLoad(void);                      /* 80072320 (Level_Run) */
int  TvCam_IsActive(int view);                  /* DAT_800af8c8 */
int  TvCam_CurrentType(int view);               /* (DAT_800af930 +0x1B) */
int  TvCam_Enter(int view);                     /* RaceCam_CycleView 8005ed10 (con 0): 1 si hay camara */
void TvCam_Leave(int view);
void TvCam_SelectIndex(int view, int idx);      /* 80074574 (7 = orbita de la llegada) */
/* RaceCam_Update 8005f034 en modo TV (80074118 + 80074264); deja la camara en g_Camera y su punto de pista */
void TvCam_Update(int view, int dt, TrackPos* camPos);

#endif
