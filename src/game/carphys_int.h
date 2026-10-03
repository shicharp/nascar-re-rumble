/* Interno de la fisica de los coches: la memoria del coche (0x6A8 bytes, Pool_Alloc en 8004d44c), la entidad
 * (0xE8) y el controlador se manejan por desplazamientos como en el codigo de la PS1 (el port es de 32 bits y
 * los punteros caben en los mismos huecos). Mapa de campos en NOTAS.md. */
#ifndef NR_CARPHYS_INT_H
#define NR_CARPHYS_INT_H

#include <stdint.h>
#include "racecar.h"

#define B_(p)      ((uint8_t*)(p))
#define I32(p, o)  (*(int32_t*)(B_(p) + (o)))
#define U32(p, o)  (*(uint32_t*)(B_(p) + (o)))
#define I16(p, o)  (*(int16_t*)(B_(p) + (o)))
#define U16(p, o)  (*(uint16_t*)(B_(p) + (o)))
#define I8(p, o)   (*(int8_t*)(B_(p) + (o)))
#define U8(p, o)   (*(uint8_t*)(B_(p) + (o)))
#define PTR(p, o)  ((uint8_t*)(uintptr_t)U32(p, o))
#define SETPTR(p, o, v) (U32(p, o) = (uint32_t)(uintptr_t)(v))
#define IA(p, i)   I32(p, (i) * 4)          /* piVar[i] del codigo descompilado */
#define ENT(c)     PTR(c, 0)                /* entidad del coche */
#define VEC(p, o)  ((int*)(B_(p) + (o)))

/* (int)(a * b >> 16) con producto de 64 bits (el patron mult + mfhi/mflo del original) */
static inline int M16(int a, int b)
{
    return (int)(((int64_t)a * (int64_t)b) >> 16);
}

static inline int Abs_(int v)
{
    return v < 0 ? -v : v;
}

/* Division del MIPS (C trunca igual); con 0 el original salta a trap: aqui devuelve 0 */
static inline int Div_(int a, int b)
{
    return b == 0 ? 0 : (b == -1 && a == INT32_MIN ? a : a / b);
}

/* carphys_util.c */
int  Rand_A(void);                                       /* 800181ac */
int  Rand_B(void);                                       /* 80018208 */
void Rand_Init(void);
void Mat_MulT(const int* m, const int* v, int* out);     /* 800198a8: m^T v (columnas) */
void Mat_MulTAdd(const int* m, const int* v, int* out);  /* 800199fc: m^T v + m[9..11] */
void Mat_Mul(const int* m, const int* v, int* out);      /* 8004e6c4: filas de m por v */
int  Math_InvSqrt(int x);                                /* 80018448 */
int  Box_RayHit(const int* half, int* dir, int base, int* out);   /* 80059b04 */
void Gte_LoadObjMatrix(const int* m);                    /* 8001de5c */
void Gte_Rt(const short* v, int* out, int shift);        /* 80016784 */
void Gte_SetTransShift(const int* pos, int shift);       /* 80015928 */
void Obj_GetPointGte(uint8_t* obj, int idx, int* out, int load);  /* Obj_GetPoint 80013c20 */
void Obj_GetModelPoint(uint8_t* obj, int idx, int* out); /* 8004d36c (sin transformar) */
void Fx_Push(int kind, const int* pos, int life);        /* cola de efectos DAT_800b52f0 (pendiente) */
void Car_Shake(RaceCar* e, int impact);                   /* 80060540 (vibracion del mando) */

/* carphys_ctrl.c */
void Car_Brake(uint8_t* c, uint32_t mode, int amount);   /* 80059870 */
void Car_Throttle(uint8_t* c, int amount);               /* 800598dc */
void Car_Steer(uint8_t* c, int mode, int amount);        /* 8005967c */
int  Car_EngineTorque(uint8_t* c);                       /* 8004d93c */
int  Car_TopSpeed(uint8_t* c);                           /* 8004ff70 */
void Car_ClearFlags(uint8_t* c, uint32_t bits);          /* 8004f63c */
void Car_ShiftUp(uint8_t* c);                            /* 80059a50 */
void Car_ShiftDown(uint8_t* c);                          /* 80059aa4 */

/* carphys_create.c */
void Car_Reset(RaceCar* e, int stopped);                 /* 8004c834 */
void Car_TrackFrame(const int* pos, int* outPos, int* frame, int* links);   /* 8004c5dc */
void Car_SetCorners(uint8_t* c);                         /* 8004dafc */

#endif
