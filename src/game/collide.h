/* Choques entre coches (8005c05c y su fase amplia 80061808 / 800613b4) */
#ifndef NR_COLLIDE_H
#define NR_COLLIDE_H

#include <stdint.h>
#include "render.h"

void Race_CollideCars(void);
/* 800613b4: esferas, cilindros (+0x74 / +0x76) y cajas del modelo de dos entidades (fa / fb = sus +0x2C) */
int  Ent_Near(const GfxObj* a, uint32_t fa, const GfxObj* b, uint32_t fb);

#endif
