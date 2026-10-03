/* Vectores de 3 enteros del juego (posiciones del mundo, direcciones en 16.16).
 * Original: Vec_Scale 80019bfc, Vec_Cross 80019da4, Vec_Normalize 800184b0 (GTE + tabla 1/raiz),
 * longitudes aproximadas 8001957c / 80019608, normalizar devolviendo la longitud 80019644, longitud al
 * cuadrado 80019518, producto escalar 80019b8c, SquareRoot12 (libgte). */
#ifndef NR_VECMATH_H
#define NR_VECMATH_H

#include <stdint.h>

static inline int Fix_Mul16(int a, int b)
{
    return (int)(((int64_t)a * b) >> 16);
}

int  Math_SquareRoot12(int x);                  /* sqrt(x) en 20.12 = sqrt(x * 4096) */
int  Vec_ApproxLen2(const int* v);              /* 8001957c: |(v[0], v[1])| aproximada */
int  Vec_ApproxLen3(const int* v);              /* 80019608 */
int  Vec_LenSq16(const int* v);                 /* 80019518: (x*x + y*y + z*z) >> 16 */
int  Vec_NormalizeLen(int* v);                  /* 80019644: a 16.16 y devuelve la longitud */
void Vec_Normalize(int* v);                     /* 800184b0: a 16.16 */
void Vec_Scale(int k, int* v);                  /* 80019bfc: v * k >> 16 */
void Vec_Cross(const int* a, const int* b, int* out);   /* 80019da4 (>> 16) */
int  Vec_Dot16(const int* a, const int* b);     /* 80019b8c */
int  Math_Atan2(int y, int x);                  /* ratan2 de libgte: 4096 = vuelta */

#endif
