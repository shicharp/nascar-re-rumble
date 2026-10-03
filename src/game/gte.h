/* Acceso a los registros de la GTE con los nombres que usa el codigo descompilado (gte_ldR11R12...).
 * PsyCross emula la GTE completa (MTC2/CTC2/MFC2/CFC2 + doCOP2), asi que los calculos de punto fijo
 * del original se pueden portar tal cual. Registros de control: 0-4 R, 5-7 TR, 8-12 L, 13-15 BK,
 * 16-20 LCM, 24/25 OFX/OFY, 26 H, 31 FLAG. Datos: 0-5 V0-V2, 6 RGB, 8-11 IR0-3, 12-14 SXY0-2,
 * 16-19 SZ0-3, 20-22 RGB0-2, 24-27 MAC0-3, 30/31 LZCS/LZCR. */
#ifndef NR_GTE_H
#define NR_GTE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
extern void MTC2(unsigned int value, int reg);
extern void CTC2(unsigned int value, int reg);
extern unsigned int MFC2(int reg);
extern int MFC2_S(int reg);
extern unsigned int CFC2(int reg);
extern int doCOP2(int op);
#ifdef __cplusplus
}
#endif

/* Operaciones (codigos COP2 del R3000) */
#define GTE_OP_RTPS   0x0180001
#define GTE_OP_NCLIP  0x1400006
#define GTE_OP_NCCT   0x118043F
#define GTE_OP_RTV0TR 0x0480012   /* MVMVA sf=1, RT * V0 + TR (gte_rt) */
#define GTE_OP_RTV0   0x0486012   /* MVMVA sf=1, RT * V0 */

static inline void Gte_SetRot(int r11, int r12, int r13, int r21, int r22, int r23, int r31, int r32, int r33)
{
    CTC2((uint32_t)(r11 & 0xffff) | (uint32_t)r12 << 16, 0);
    CTC2((uint32_t)(r13 & 0xffff) | (uint32_t)r21 << 16, 1);
    CTC2((uint32_t)(r22 & 0xffff) | (uint32_t)r23 << 16, 2);
    CTC2((uint32_t)(r31 & 0xffff) | (uint32_t)r32 << 16, 3);
    CTC2((uint32_t)(r33 & 0xffff), 4);
}

static inline void Gte_SetTrans(int x, int y, int z)
{
    CTC2((uint32_t)x, 5);
    CTC2((uint32_t)y, 6);
    CTC2((uint32_t)z, 7);
}

static inline void Gte_SetV0(int x, int y, int z)
{
    MTC2((uint32_t)(x & 0xffff) | (uint32_t)y << 16, 0);
    MTC2((uint32_t)z, 1);
}

static inline int Gte_LeadingZeros(int v)
{
    MTC2((uint32_t)v, 30);
    return (int)MFC2(31);
}

#endif
