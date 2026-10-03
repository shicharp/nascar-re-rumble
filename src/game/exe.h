/* Datos inicializados del ejecutable original (SLUS_010.68): el port lee las tablas del juego (coches,
 * marchas, controles...) por su direccion en la PS1 en vez de copiarlas a mano. */
#ifndef NR_EXE_H
#define NR_EXE_H

#include <stdint.h>

int            Exe_Load(void);
const uint8_t* Exe_Ptr(uint32_t addr);          /* NULL si la direccion no esta en el ejecutable */

static inline int8_t   Exe_S8(uint32_t a)  { const uint8_t* p = Exe_Ptr(a); return p ? (int8_t)p[0] : 0; }
static inline uint8_t  Exe_U8(uint32_t a)  { const uint8_t* p = Exe_Ptr(a); return p ? p[0] : 0; }
static inline int16_t  Exe_S16(uint32_t a) { const uint8_t* p = Exe_Ptr(a); return p ? (int16_t)(p[0] | p[1] << 8) : 0; }
static inline uint16_t Exe_U16(uint32_t a) { const uint8_t* p = Exe_Ptr(a); return p ? (uint16_t)(p[0] | p[1] << 8) : 0; }
static inline int32_t  Exe_S32(uint32_t a)
{
    const uint8_t* p = Exe_Ptr(a);
    return p ? (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) : 0;
}

#endif
