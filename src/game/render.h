/* Render 3D de objetos (modelos Cobj) con la GTE.
 * Original: lista de dibujo DAT_800af56c (+0x984 numero, +0x988 objetos), Render_DrawList 80015f4c
 * (80015280 matrices de vista, 800162d4 visibilidad/vertices/luz, 80015b74 primitivas), luz por defecto
 * DAT_800af9c0 (FUN_800137d8), sol FUN_800153e4, objeto FUN_800139fc/800138f0, matrices 80017ad8,
 * seno/coseno 800176c0 (tabla 800a9c98), look-at de camara 80016bfc/80017a68.
 * Convencion del juego: Y hacia arriba, mano izquierda; las matrices son 3 columnas (ejes X, Y, Z del
 * objeto) en 16.16 y al cargarlas en la GTE se niega la fila Y (la pantalla crece hacia abajo).
 * Pendiente: sombras 3DSD (80014c24/80014d90), reflejos (80015a88) y texturas animadas 4DTA. */
#ifndef NR_RENDER_H
#define NR_RENDER_H

#include <stdint.h>
#include "libgte.h"
#include "model.h"

/* Luz (0x40 bytes, DAT_800af9c0) */
typedef struct
{
    int16_t  lm[9];         /* +0x00 una fila por luz: direccion en el espacio del objeto (4.12) */
    uint8_t  numLights;     /* +0x12 */
    uint8_t  pad13;
    int16_t  lcm[9];        /* +0x14 colores de las luces (columnas) */
    int16_t  bk[3];         /* +0x26 luz ambiente */
    uint8_t  rgb[4];        /* +0x2C color base */
    SVECTOR  dir2;          /* +0x30 */
    SVECTOR  dir3;          /* +0x38 */
} Light;

static_assert(sizeof(Light) == 0x40, "Light debe medir 0x40");

/* Objeto dibujable (0x8C bytes) */
typedef struct
{
    ModelHdr* model;        /* +0x00 */
    int       rot[9];       /* +0x04 columnas X, Y, Z (16.16) */
    int       pos[3];       /* +0x28 */
    int       unk34[3];
    int       radius;       /* +0x40 */
    short     bb10;         /* +0x44 */
    short     bb16;         /* +0x46 */
    short     frame;        /* +0x48 */
    uint8_t   flags;        /* +0x4A bit n: visible en la vista n; 0x10 sombra; 0x40 reflejos; 0x80 */
    uint8_t   unk4b;
    short     shadowY;      /* +0x4C */
    short     shadowN[3];   /* +0x4E normal del suelo (0, 0x1000, 0) */
    uint32_t  envColor;     /* +0x54 */
    Light*    light;        /* +0x58 */
    int       viewRot[9];   /* +0x5C matriz respecto a la camara */
    int       viewPos[3];   /* +0x80 */
} GfxObj;

static_assert(sizeof(GfxObj) == 0x8c, "GfxObj debe medir 0x8C");

/* Camara: solo lo que usa el render (DAT_800af6d4 +0x0C vista, +0x60 posicion) */
typedef struct
{
    int view[9];            /* columnas: imagen de los ejes del mundo en el espacio de la camara */
    int pos[3];
} Camera3D;

/* Proyeccion del preset activo: pendientes del frustum (DAT_800afa30-33 = scratchpad 1f800000-03),
 * holguras de radio (DAT_800afa3c/3e), H (DAT_800afa36) y desplazamientos de LOD (DAT_800afa38 = 1f800008) */
typedef struct
{
    uint8_t  sx0, sx1, sy0, sy1;
    uint16_t rx, ry;
    uint16_t h;
    uint8_t  lod[2];
} RenderProj;

extern Camera3D g_Camera;
extern Light    g_DefaultLight;

void Render_Init(void);                                         /* FUN_800137d8 */
void Render_SetPreset(int preset);                              /* parte de Camera_ApplyProjection */
const RenderProj* Render_GetProjection(void);

/* Pantalla ancha (Hor+, solo en el port): PsyCross ensancha la proyeccion 2D al aspecto de la ventana y deja
 * el 320x240 de la PS1 en el centro. g_WideFix = ensanche horizontal en 8.8 (256 = 4:3) y g_WideMargin = pixeles
 * PSX que se ven a cada lado; los tests de visibilidad horizontales del juego se ensanchan con ellos. */
extern int g_WideFix;
extern int g_WideMargin;
void Render_UpdateWide(void);
void Render_SetMirrorProjection(int on);   /* Camera_ApplyProjection con el retrovisor */
static inline int Wide_X(int ax) { return (int)(((long long)ax << 8) / g_WideFix); }
static inline int Wide_OnScreenX(int sx) { return (unsigned)(sx + g_WideMargin) < (unsigned)(2 * 160 + 2 * g_WideMargin); }
void Gte_LoadColumns(const int* m);                             /* 80015880: columnas 16.16 -> GTE 4.12, fila Y negada */
void Render_LoadCameraRotation(void);                           /* 8001032c */
void Math_SinCos(int angle, int* s, int* c);                    /* 800176c0: 4096 = vuelta, 16.16 */
void Math_RotMatrix(int ay, int ax, int az, int* m);            /* 80017ad8 */
void Camera_LookAt(const int* eye, const int* target);          /* 80016bfc + 80017a68 */
void Light_SetSun(const int* pos, int r, int g, int b);         /* 800153e4 (sin el destello) */

void Obj_Init(GfxObj* o, ModelHdr* m);                          /* 800139fc */
/* 80013db0: nivel de detalle por la distancia a la camara (0 cerca .. 3 muy lejos); deja en +0x54 el brillo del
 * reflejo (0x404040 cerca, se apaga en el nivel 1, 0 desde el 2). Solo en el port: devuelve siempre 0 (todo con el
 * detalle completo) salvo con NR_PSXLOD=1, que imita a la PS1 */
int  Obj_LodLevel(GfxObj* o);
void Obj_GetPoint(const GfxObj* o, int i, int* out);            /* 80013c20: punto i en el mundo */
void Obj_GetLocalPoint(const GfxObj* o, int i, int* out);       /* 800367d0 */

void Render_ClearList(void);
void Render_Add(GfxObj* o);
void Render_DrawList(int mirror);                               /* 80015f4c(0, mirror) */

/* 8001931c / 8001607c: esfera (centro en el espacio de la camara, a 1/16) en la vista: 0 fuera, 1 detras, 2 dentro */
int  Render_SphereInFrustum(const int16_t* c, int r);
/* Proyecta un punto relativo a la camara ya cargada (80010234); devuelve la profundidad */
int  Render_ProjectPoint(const SVECTOR* v, int* sx, int* sy);
/* Carga en la GTE la matriz de la camara compuesta con (rot, pos) a escala 1/16 (80016828 + 8001025c) */
void Render_LoadObjectMatrix16(const int* rot, const int* pos);

#endif
