/* Tipos y utilidades comunes del port de NASCAR Rumble. */
#ifndef NR_H
#define NR_H

#include <stdint.h>
#include <stddef.h>

/* Constante multicaracter de C ('SHOC') como la usa el codigo original; en disco va en LE. */
#define NR_TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

void NR_Log(const char* fmt, ...);
void NR_TagToStr(uint32_t tag, char out[5]);

static inline uint32_t NR_Read32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint16_t NR_Read16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

#endif
