/* Marcador de la carrera (Race_DrawHud 80043520) */
#ifndef NR_HUD_H
#define NR_HUD_H

#include "racecar.h"

void Hud_Init(void);
void Hud_Update(int dt);                 /* 80042330: aguja del cuentarrevoluciones */
void Hud_Draw(int view);                 /* 80043520 */
int  Hud_GetSetting(int player);         /* DAT_800b7112 */
void Hud_CycleLevel(RaceCar* e, int dir);   /* 8005efec */
void Hud_Rect15(unsigned color, int x2, int y2, int w2, int h2, int semi);   /* 8001ab58 (medios pixeles) */
uint16_t Hud_TeamColor(int team);        /* DAT_800aab38 */
int  Hud_GapTime(uint8_t* me, uint8_t* other);   /* 8002cd84: diferencia en tics */

#endif
