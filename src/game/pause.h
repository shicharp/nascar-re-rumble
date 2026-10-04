/* Pausa, fin de la carrera y resultados (80027e90, 80025ad4, 8002896c, 800266f0) */
#ifndef NR_PAUSE_H
#define NR_PAUSE_H

extern int g_RacePaused;     /* DAT_800af710 */
extern int g_RaceEndTimer;   /* DAT_800af740 */
extern int g_RaceRestart;    /* DAT_800af691 */
extern int g_PauseCamPreview;   /* DAT_800af1d0 */

void Pause_Init(void);
void Race_UpdateState(int dt);   /* 80027e90 */
void Pause_Menu(void);           /* 80025ad4 */
void Race_Results(int dt);       /* 8002896c */
void Race_Finalize(void);        /* 800266f0 */

#endif
