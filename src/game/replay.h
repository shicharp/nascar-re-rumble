/* Repeticion de la carrera: se graban los mandos de los humanos y el dt de cada frame (en tramos de valores iguales)
 * y "Show Replay" vuelve a cargar la pista y a simular la carrera con la misma semilla y esos mandos.
 * Original: bufer 80074ee8 (Game_Init, 0x10028 bytes en DAT_800af330), preparar 80074d90 (Race_Load), grabar
 * 80074ac8 y reproducir 80074860 (Race_UpdateTime 800269c0), cerrar 800747a4 (8002456c, al descargar la carrera).
 * GameInfo +0x60 = 1 durante la repeticion (Level_Run 80029be4). */
#ifndef NR_REPLAY_H
#define NR_REPLAY_H

int  Replay_Begin(int seed);       /* 80074d90: devuelve la semilla de la carrera (en la repeticion, la grabada) */
void Replay_RecordFrame(int dt);   /* 80074ac8: mandos de los humanos y dt del frame */
int  Replay_PlayFrame(void);       /* 80074860: pone los mandos grabados y devuelve el dt; -2 al acabarse */
void Replay_End(void);             /* 800747a4: cierra las listas de la grabacion */

#endif
