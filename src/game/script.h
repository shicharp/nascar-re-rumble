/* Interprete de guiones del juego (80067890), ver script.c */
#ifndef NR_SCRIPT_H
#define NR_SCRIPT_H

#include <stdint.h>

/* Ejecuta el bytecode; 'init' >= 0 se mete en la pila al empezar; 'ent' = entidad (Actor*) o NULL */
int  Script_Run(const uint8_t* code, int arg, int init, void* ent);   /* 80067890 */
void Script_ResetRace(void);                                         /* 80067680 */
void Script_Tick(int dt);                                            /* 80067834 */
void Script_Disable(void);                                           /* 80067880 */
int  Script_LogOn(void);
void Script_PlayRequestedSound(void);                                /* 80045100 */                                             /* solo en el port: NR_SCRIPTLOG */

#endif
