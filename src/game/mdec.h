/* Decodificador MDEC por software (bitstream "BS" v2 de Sony).
 * Sustituye al hardware MDEC (DecDCTin/DecDCTout de libpress), que PsyCross no implementa.
 * Original: FUN_80095d70 (preparacion + DecDCT) y FUN_8009905c (VLC -> codigos de run-level). */
#ifndef NR_MDEC_H
#define NR_MDEC_H

#include <stdint.h>

/* img apunta a: u16 ancho, u16 alto, cabecera BS (u16 n, u16 0x3800, u16 qscale, u16 version)
 * y el bitstream. Escribe ancho*alto pixeles BGR555 (bit 15 = STP) en out.
 * Devuelve 1 si va bien; outW/outH reciben las dimensiones. */
int Mdec_DecodeImage(const uint8_t* img, int imgSize, uint16_t* out, int maxPixels, int* outW, int* outH);

#endif
