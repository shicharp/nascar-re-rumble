/* Pantallas .LSC (legal, carga, ayuda): dos imagenes MDEC de 160x256 que se suben a VRAM en
 * (0x140, 0) y (0x140, 0x100) y se dibujan como dos sprites de 160x240.
 * Original: Lsc_LoadToVram 80090ef8, LoadScreen_Show 80024c58, Lsc_DecodeToVram = FUN_80095d70,
 * LoadScreen_GetName = FUN_800921a0. */
#ifndef NR_LSC_H
#define NR_LSC_H

#define LSC_VRAM_X 0x140

int  Lsc_DecodeToVram(const unsigned char* img, int size, int x, int y);
void Lsc_LoadToVram(const char* name, int show);
const char* LoadScreen_GetName(void);
int  LoadScreen_Show(void);

#endif
