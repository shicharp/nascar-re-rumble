/* Lista de destellos/halos 2D de la carrera (luces de la pista, efectos): formas de puntos y poligonos en
 * degradado dibujadas con mezcla aditiva a la profundidad de su punto.
 * Original: lista DAT_800b5500 (0x30 entradas de 0x18 bytes, contador DAT_800af854, vaciada en
 * Race_DrawView 8006422c), dibujo FUN_80045268, formas 800aafa4 (puntos 800aab44, poligonos 800aad34).
 * La pista anade los objetos D de sus segmentos en Track_DrawSegments 8006327c. */
#ifndef NR_SPRITE_H
#define NR_SPRITE_H

#include <stdint.h>

#define SPRITE_MAX 0x30

void Sprite_Clear(void);
/* colorA = centro (RGB de GPU), colorB = borde; sxy/sz de pantalla (sz 0 = sin escala por distancia) */
void Sprite_Add(int type, uint32_t colorA, uint32_t colorB, uint32_t sxy, int sz, int size, float px, float py);
void Sprite_DrawAll(void);                                  /* 80045268 */
/* 800457e0: manchas del destello del sol; (x, y) esquina de su cuadrado de 5x5, intensity y bright en 16.16 */
void Sprite_AddLensFlare(int x, int y, int intensity, int type, int bright);

#endif
