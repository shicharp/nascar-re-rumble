/* Pausa, fin de la carrera y resultados (80027e90, 80025ad4, 8002896c, 800266f0) */
#ifndef NR_PAUSE_H
#define NR_PAUSE_H

extern int g_RacePaused;     /* DAT_800af710 */
extern int g_RaceEndTimer;   /* DAT_800af740 */
extern int g_RaceRestart;    /* DAT_800af691 */
extern int g_ShowReplay;     /* DAT_800af690: volver a cargar la carrera en repeticion */
extern int g_ReplayEnd;      /* DAT_800af688: 1 se acabo la repeticion, 2 resultados */
extern int g_PauseCamPreview;   /* DAT_800af1d0 */

void Pause_Init(void);
void Race_UpdateState(int dt);   /* 80027e90 */
void Pause_Menu(void);           /* 80025ad4 */
void Race_Results(int dt);       /* 8002896c */
void Race_Finalize(void);        /* 800266f0 */
void Pause_ReplayEnded(void);   /* 800269c0 con 80074860 = -2: para la carrera al acabarse la repeticion */
void Pause_ReplayWait(void);    /* 800269c0: con la repeticion acabada, un boton pasa a los resultados */
void Race_SpectatorResults(int dt);   /* solo en el port: en red, los resultados del que ya ha llegado */
int  Pause_ResultsTime(void);
int  Pause_MenuOpen(void);        /* solo en el port: el menu de la pausa esta abierto */     /* DAT_800af70c: tics en los resultados (-1 antes) */

#endif
