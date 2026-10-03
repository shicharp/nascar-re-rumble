/* Gestor de memoria.
 * Original: Mem_Alloc 8001cb84 / Mem_Free 8001cc04 (malloc3 con cabecera de tamano y contador de uso/pico)
 *           Pool_Alloc 80017154 / Pool_Free 8001745c (pools propios con modos 0-4).
 * De momento los pools van sobre el heap; el modo se conserva para cuando importe la ubicacion. */
#ifndef NR_MEM_H
#define NR_MEM_H

void* Mem_Alloc(int size, int tag);
void  Mem_Free(void* p);
void* Pool_Alloc(int size, int mode);
void  Pool_Free(void* p);

int Mem_GetUsed(void);
int Mem_GetPeak(void);

#endif
