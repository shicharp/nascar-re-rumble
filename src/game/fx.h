/* Efectos visuales de la carrera: chispas, humo, tierra, destellos... (plantillas del Csfx de GlblData).
 * Original: Csfx 80035adc / 800359bc (registros 0x56 banco, 0x5B clases de objeto, 0x57 / 0x58 / 0x5A / 0x5C /
 * 0x5E plantillas por clase en DAT_800b2320[banco * 0x100 + clase]), crear 80039a80 (ancla 80036fb4 +
 * instancia 80038208), cola de impactos 8007734c / 80038a48, actualizar 80038b50, dibujar 80038ccc.
 * Tipos: 0x5E particulas (fx_psys.c), 0x5C anillos y cupulas (fx_ring.c), 0x5A restos (fx_debris.c), 0x58 rayos
 * (fx_bolt.c), 0x57 estelas (fx_trail.c); marcas y humo de las ruedas (fx_marks.c); llamas del escape
 * (fx_exhaust.c); nube de la tormenta (fx_storm.c); cupulas de los poderes (fx.c). */
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

/* Nube de la tormenta (fx_storm.c) y rayo suelto (fx_bolt.c) */
typedef struct Storm Storm;
struct RaceCar;
struct Actor;
Storm* Storm_Create(void);                       /* 80070e58 */
void Storm_Fade(Storm* s);                       /* 80071dd4 */
void Storm_Free(Storm* s);                       /* 80071de4 */
void Storm_FreeAll(void);                        /* 80070e1c */
void Storm_SetTarget(Storm* s, struct RaceCar* car, struct Actor* ent);
void Bolt_Strike(const int* from, const int* to);   /* 8006d95c x 2 + 8006dc10 (rayo de la tormenta) */

/* Coches */
void Car_ExhaustFx(uint8_t* c, int dt);          /* 80077c60 */
void Car_WheelMarksFx(uint8_t* c, int dt);       /* 80076500 (cada paso de la fisica) */
void Car_JumpFx(uint8_t* c);                     /* 80078bb8 */
void Car_PowerSkinFx(uint8_t* c, unsigned changed); /* 800786b0 / 800787d0 (helado / electrocutado) */
void Car_PowerSkinFree(uint8_t* c);              /* (tambien las cupulas) */
void Car_PowerDomes(uint8_t* c, unsigned changed);  /* Physics_StepAll: 80078878 / 80078c88 */
void Car_DomePulse(int dt);                      /* 80075730 */
void Car_DrawWheelDomes(void);                   /* 80078b30 */
void Car_DrawRamDomes(int view);                 /* 80078d78 */
void Marks_Init(uint8_t* c, int k);              /* 80075a9c */
void Marks_Free(uint8_t* c, int k);              /* 80075c08 */

#endif
