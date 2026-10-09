/* Biblioteca de sonido de bajo nivel del juego (sobre libspu, sin libsnd): bancos de muestras en la RAM de
 * la SPU y voces. Original (0x80020000-0x800213D0): init 8002097c / 80020a4c (etiquetas: 4 bancos = 10,
 * 5 voces = 24), subir un banco 80020adc (SpuMalloc 80020668 y cola de transferencias 800200a4 / 80020254),
 * liberarlo 80020bd8, recolocar sus direcciones 800207bc, programa de un banco 800205d0, tocar una voz
 * 80020eac, cambiarla 800211e0, pararla 800212a0, volumen y panoramica 800212c4, reverberacion 80020840 /
 * 80020860, aplicar las pulsaciones pendientes 80020c48 (key off, atributos 800208fc y key on), voces de
 * streaming 80020fd8 / 80021154, modo mono 80020da0, reverberacion 80020e0c / 80020e80.
 * Cabecera de banco (Cvkh): +4 u8 n programas, +8 direccion en la SPU (la pone el juego), +0xC tamano de las
 * muestras (Cvkb), +0x10 offsets de los programas. Programa: u8 n patches, u8 volumen, u8 panoramica;
 * patches de 0x14 bytes desde +4: +1 / +2 volumen izquierdo / derecho (/ 128), +0xA / +0xC ADSR, +0xE tono,
 * +0x10 direccion de la muestra (relativa al banco hasta que se sube). */
#ifndef NR_SND_LIB_H
#define NR_SND_LIB_H

#include <stdint.h>

#define SNDLIB_BANKS   10
#define SNDLIB_VOICES  24

typedef struct
{
    int pitch;      /* -1 = el del patch */
    int vol;        /* 0..127 */
    int pan;        /* 0..0x7F (0x40 centro; > 0x80 fase invertida en Dolby) */
} SndLibParams;

void SndLib_Init(void);                                 /* 8002097c + 80020a4c + 8001ffa8 */
int  SndLib_SpuAlloc(int size);                         /* SpuMalloc: direccion o -1 */
int  SndLib_SpuAllocAt(int addr, int size);             /* SpuMallocWithStartAddr */
void SndLib_SpuFree(int addr);                          /* SpuFree */
void SndLib_SpuWrite(int addr, const void* data, int size);   /* SpuSetTransferStartAddr + SpuWrite */
int  SndLib_LoadBank(uint8_t* hdr, const uint8_t* body);      /* 80020adc: hueco o < 0 */
void SndLib_FreeBank(int slot);                         /* 80020bd8 */
const uint8_t* SndLib_Program(int slot, int prog);      /* 800205d0 */
int  SndLib_PatchPitch(int slot, int prog);             /* 800213d0: tono del primer patch o -1 */
int  SndLib_Play(int slot, int prog, int patch, int voice, const SndLibParams* p);   /* 80020eac */
int  SndLib_PlayRaw(int voice, const SndLibParams* p, int spuAddr);   /* 80020fd8: voz de streaming */
void SndLib_ReleaseRaw(int voice);                      /* 80021154 */
void SndLib_KeyOn(int voice);                           /* 80020890 / 800210f8 */
void SndLib_Update(int voice, const SndLibParams* p);   /* 800211e0 */
void SndLib_Stop(int voice);                            /* 800212a0 */
void SndLib_StopAll(void);                              /* 800209bc (volumen y tono a 0) */
void SndLib_StopAllBut(int voice);                      /* 800209fc */
void SndLib_ReverbOn(int voice);                        /* 80020840 */
void SndLib_ReverbOff(int voice);                       /* 80020860 */
void SndLib_SetMono(int mono);                          /* 80020da0 */
void SndLib_SetReverb(int on);                          /* 80020e0c */
void SndLib_SetReverbDepth(int l, int r);               /* 80020e80 */
void SndLib_Commit(void);                               /* 80020c48 (en el original, en la vblank) */

#endif
