/* Contrasena del progreso (pantalla 700): 10 caracteres de 5 bits = 50 bits.
 * Original: codificar 8008f344, comprobar 8008fb60, aplicar 80090008, alfabetos DAT_800abeac / 800abecc
 * (alternos por posicion), contrasenas especiales en los textos 0x839..0x843. Ver password.c. */
#ifndef NR_PASSWORD_H
#define NR_PASSWORD_H

#include <stdint.h>

void Pw_Encode(char* out);                  /* 8008f344: la del progreso actual (10 caracteres + 0) */
int  Pw_Decode(const char* text, uint32_t* words);   /* 8008fb60: 1 si vale (normal o especial) */
void Pw_Apply(const uint32_t* words);       /* 80090008 */

#endif
