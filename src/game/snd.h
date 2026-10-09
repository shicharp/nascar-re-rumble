/* Sonido del juego (capa por encima de snd_lib): bancos de la pista / del front-end / globales, definiciones
 * de sonido (id -> hasta 6 muestras), voces con prioridad, fuentes posicionales (volumen por distancia,
 * panoramica por angulo y Doppler respecto a la camara), sonidos de la interfaz y la cola de sonidos de la
 * carrera. Original:
 *   - Bancos: Snd_Init 80022330, Snd_OnAsset 80022908 (Cshd / Cvkh / Cvkb / Ceng), emparejar cabecera y muestras
 *     800226d8, definiciones del banco 800225b0 (80023544, 8003fb84), descargar 80022c58 / 80022da4.
 *   - Volumenes 8003d498 (efectos), 8003d538 (musica), 8003d628 (voces), 8003d680 (motor), modo 8003d43c.
 *   - Voces (DAT_800b4db0, 24 x 0x38): reservar 8003f550, soltar 8003f0c8, listas 8003f2c8 / 8003f3f8,
 *     tocar 8003ff20 -> 800237ac, cambiar 800400a0 -> 8002387c, tono 80023708.
 *   - Fuentes (DAT_800b2e80, 30 x 100): 8003da74, 8003dbb0, 8003dc28, 8003dd00, 8003dc58; orden 8003d9d0.
 *   - Oyente por vista 8003dd84; volumen / panoramica 8003de88, 8003e664, 8003e784; Doppler 80041948.
 *   - Actualizacion 8003e848 (cada frame), sonido de interfaz 8003f6fc / 8003f720 -> 8003f898, sonido en un
 *     punto 8003fa10, cola DAT_800b52f0 (80042234), pitidos de la cuenta atras 8004214c, pausa 8003fe7c. */
#ifndef NR_SND_H
#define NR_SND_H

#include <stdint.h>
#include "stream.h"

void Snd_Init(void);                                     /* 80022330 */
int  Snd_OnAsset(NrAsset* a);                            /* 80022908: 1 si se queda con el asset */
void Snd_UnloadBanks(void);                              /* 80022da4 (Level_Unload) */
void Snd_StopRace(void);                                 /* 8003d04c: fuentes de la carrera y bancos */

/* Volumenes 0..127 (los del menu de audio) y modo 0 mono, 1 estereo, 2 Dolby */
void Snd_SetSfxVolume(int v);                            /* 8003d498 */
void Snd_SetMusicVolume(int v);                          /* 8003d538 */
void Snd_SetSpeechVolume(int v);                         /* 8003d628 */
void Snd_SetEngineVolume(int v);                         /* 8003d680 */
void Snd_SetMode(int mode);                              /* 8003d43c */
void Snd_ApplySettings(const uint8_t* volume, int mode); /* los 4 volumenes de g_Settings y el modo */
int  Snd_EffectiveVolume(int which);                     /* 8003d52c / 8003d6fc / 8003d5dc / 8003d674 */
int  Snd_GetMode(void);                                  /* 8003d480 */

/* Oyentes: n (0 en el front-end) y, por vista, la camara (posicion, rumbo 12 bits, velocidad, tunel) */
void Snd_SetListenerCount(int n);                        /* 8003d48c */
void Snd_SetListener(int view, const int* pos, int heading, const int* vel, int reverb, int curve);   /* 8003dd84 */

void Snd_Update(void);                                   /* 8003e848 */
void Snd_RaceInit(void);                                 /* 8003cfd0 */
void Snd_Queue(int id, int x, int z);                    /* cola DAT_800b52f0 (la vacia 80042234) */
void Snd_SetRaceActive(int on);                          /* DAT_800af809: fuentes y motores activos */
void Snd_Pause(int on);                                  /* 8003fe7c(0) / (1) */

/* Sonido de la interfaz o sin posicion (id 1..0x8B); entity: el coche del jugador (pantalla partida) */
void Snd_PlayUi(int id, const void* entity);             /* 8003f6fc */
void Snd_PlayUiVol(int id, const void* entity, int vol); /* 8003f6d8 */
/* Sonido en un punto del mundo (x, z) */
void Snd_PlayAt(int id, int x, int z);                   /* 8003fa10 */

/* Fuentes de sonido que siguen a una entidad (handle): indice o -1 */
int  Snd_SourceCreate(int id, int handle, int x, int z, int pitch, int speed, const int* dir);   /* 8003da74 */
void Snd_SourceMove(int handle, int x, int z, int pitch, int speed, const int* dir, int src);    /* 8003dbb0 */
void Snd_SourceSetPatch(int handle, int patch, int src);                                         /* 8003dc28 */
void Snd_SourceSetVolume(int handle, int src, int scale);                                        /* 8003dd00 */
void Snd_SourceKill(int handle, int src);                                                        /* 8003dc58 */

/* Musica (snd_music.c): bloques VAGM del fichero, decodificados y tocados con OpenAL en bucle */
void Music_Play(const char* path);
void Music_Stop(void);
void Music_Pause(int on);
void Music_SetVolume(int vol);
void Music_Update(void);

/* Comentarista (snd_speech.c): categoria (0..0x36), espera, coche y 1 = solo si no habla ya */
void Speech_SetToc(const uint8_t* toc, int size);         /* 8002b810: tabla de secciones de la pista */
int  Speech_Pick(int cat, int wait);                      /* 8002b920 */
int  Speech_Queue(int section, const void* entity, int onlyIfIdle);   /* 8002b0f4 */
void Speech_Say(int cat, int wait, const void* entity, int onlyIfIdle);
void Speech_Reset(void);
void Music_SetDuck(int on);

/* Coches (snd_car.c): motor de cada coche, bocina */
struct RaceCar;
void Engine_ResetTypes(void);                            /* 8003cfd0 (al cargar la carrera, antes de los Ceng) */
void Engine_Create(uint8_t* phys);                       /* 80040258 (Car_InitPhys) */
void Engine_Update(uint8_t* phys);                       /* 80040600 (cada paso de la fisica) */
void Engine_Free(uint8_t* phys);                         /* 800417ec */
void Car_PlaySfx(int id, const struct RaceCar* car);     /* 80041be8 (5 = bocina) */
void Car_PlaySfxAt(int id, int x, int z, const struct RaceCar* car);   /* 80041c6c */

#endif
