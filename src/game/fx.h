/* Efectos visuales de la carrera: chispas, humo, tierra, destellos... (plantillas del Csfx de GlblData).
 * Original: Csfx 80035adc / 800359bc (registros 0x56 banco, 0x5B clases de objeto, 0x57 / 0x58 / 0x5A / 0x5C /
 * 0x5E plantillas por clase en DAT_800b2320[banco * 0x100 + clase]), crear 80039a80 (ancla 80036fb4 +
 * instancia 80038208), cola de impactos 8007734c / 80038a48, actualizar 80038b50, dibujar 80038ccc.
 * Portados: tipo 0x5E (particulas) y las marcas y el humo de las ruedas (fx_marks.c). Pendiente: 0x5C, 0x5A,
 * 0x57, 0x58, emisores 0x54, nube y rayos de la tormenta. */
#ifndef NR_FX_H
#define NR_FX_H

#include <stdint.h>
#include "asset.h"

void Fx_Reset(void);                             /* 80038d54 (Race_Load) */
void Fx_FreeAll(int bank);                       /* 80038e28 (2 = los dos bancos) */
int  Fx_OnCsfx(NrAsset* a);                      /* 80035adc */
void Fx_PostLoad(void);                          /* 8003913c + 8003bd60 (Level_Run / Level_WaitLoaded) */
void Fx_Update(int dt);                          /* 80038b50 */
void Fx_Draw(int view);                          /* 80038ccc */

/* Crea el efecto 'cls' pegado a la entidad (o fijo si es NULL); sp: parametros (struct FxSpawn) o NULL */
void* Fx_Spawn(void* ent, int cls, const void* sp, int bank, void* cb);   /* 80039a80 */
void Fx_Kill(void* ent, int cls);                /* 800352f8 (cls 0: todos los de la entidad) */
void Fx_Stop(void* ent, int cls);                /* 800391b0 */

/* Cola de impactos (25 entradas): 'who' 0 / 1 fisica de un coche, 2 entidad; pos puede ser NULL */
void Fx_QueueImpact(void* who, int who2, const int* pos, int speed, int id);              /* 8007734c */
void Fx_QueueImpactVel(void* who, int who2, const int* pos, const int* vel, int id);      /* 800773d4 */

/* Clases de objeto (0x5B, 12 bytes: +2 efecto al morir, +3 efecto al crear, +8 banderas) */
const uint8_t* Fx_ObjClass(int cls);
void Fx_ObjDie(int cls, void* ent);              /* 800354d4 */
void Fx_ObjHit(int cls, void* ent);              /* 8003558c */
void Fx_ObjCreated(void* ent);                   /* 80038ff8 */

/* Coches */
void Car_ExhaustFx(uint8_t* c, int dt);          /* 80077c60 */
void Car_WheelMarksFx(uint8_t* c, int dt);       /* 80076500 (cada paso de la fisica) */
void Marks_Init(uint8_t* c, int k);              /* 80075a9c */
void Marks_Free(uint8_t* c, int k);              /* 80075c08 */

#endif
