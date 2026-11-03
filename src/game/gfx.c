#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PsyX/common/glad.h"
#include "PsyX/PsyX_globals.h"
#include "libgte.h"
#include "libgpu.h"
#include "libetc.h"
#include "PsyX/PsyX_public.h"
#include "nr.h"
#include "mem.h"
#include "gfx.h"
#include "render.h"
#include "net.h"

typedef struct
{
    DISPENV  disp;
    DRAWENV  draw;
    OT_TAG   ot[2][GFX_OT_LEN];/* PsyCross: OT_TAG lleva puntero completo + pgxp (8 bytes en x86).
                                * La segunda capa se usa tras Gfx_FlushPartial y se dibuja encima. */
    uint8_t* prims;
} GfxBuffer;

static GfxBuffer s_buf[2];
static int       s_cur;
static int       s_primBytes;
static int       s_primUsed;
static int       s_flipCount;
static int       s_layer;          /* capa de OT en uso (1 tras Gfx_FlushPartial) */

/* Primitivas del frame en orden de creacion. Se enlazan en Gfx_Flip de la ultima a la primera, de modo
 * que dentro de un mismo indice de OT se dibujan en el orden en que se crearon (como FUN_8001a1b4). */
#define GFX_MAX_PRIMS 8192
static void*     s_pendPrim[GFX_MAX_PRIMS];
static uint16_t  s_pendIdx[GFX_MAX_PRIMS];
static int       s_pendCount;
static const char* s_shotPath;
static int       s_shotFrame = -1;

/* Tabla de 800aa4c8: 16 tramos de 0x1000 de profundidad -> base + (z >> desplazamiento) */
static const uint16_t s_depthBase[16]  = { 1, 513, 1025, 1025, 1793, 1793, 1793, 1793,
                                           1793, 1793, 1793, 1793, 1793, 1793, 1793, 1793 };
static const uint8_t  s_depthShift[16] = { 2, 3, 4, 4, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 };

void Gfx_Init(void)
{
    ResetGraph(0);
    SetGraphDebug(0);
#if USE_PGXP
    /* El juego ordena por OT (algoritmo del pintor) y depende de ese orden, p. ej. el reflejo del garaje
     * va debajo del coche; el z-buffer de PGXP lo romperia. Las primitivas 3D llevan las coordenadas de
     * pantalla precisas de la GTE (sin el temblor de PS1). La correccion de perspectiva de PsyCross
     * (reconstruye cada vertice en 3D) desplaza las texturas de los coches: de momento, texturas afines
     * como en la consola. */
    g_cfg_pgxpZBuffer = 0;
    g_cfg_pgxpTextureCorrection = 0;
#endif

    /* buffer 0 dibuja en y=0 y muestra y=256; buffer 1 al reves (como el original) */
    SetDefDrawEnv(&s_buf[0].draw, 0, 0, GFX_SCREEN_W, GFX_SCREEN_H);
    SetDefDispEnv(&s_buf[0].disp, 0, 256, GFX_SCREEN_W, GFX_SCREEN_H);
    SetDefDrawEnv(&s_buf[1].draw, 0, 256, GFX_SCREEN_W, GFX_SCREEN_H);
    SetDefDispEnv(&s_buf[1].disp, 0, 0, GFX_SCREEN_W, GFX_SCREEN_H);
    for (int i = 0; i < 2; i++)
    {
        s_buf[i].draw.isbg = 1;
        s_buf[i].draw.dtd = 1;
    }

    FntLoad(960, 256);
    FntOpen(8, 8, GFX_SCREEN_W - 16, GFX_SCREEN_H - 16, 0, 1024);
    SetDispMask(1);
}

void Gfx_InitBuffers(int numPrims)
{
    Gfx_FreeBuffers();
    s_primBytes = numPrims * 0x68 + 0x5818;
    for (int i = 0; i < 2; i++)
        s_buf[i].prims = (uint8_t*)Mem_Alloc(s_primBytes, 0);
    s_cur = 0;
}

void Gfx_FreeBuffers(void)
{
    for (int i = 0; i < 2; i++)
    {
        Mem_Free(s_buf[i].prims);
        s_buf[i].prims = NULL;
    }
}

void Gfx_BeginFrame(int r, int g, int b)
{
    Render_UpdateWide();
    GfxBuffer* buf = &s_buf[s_cur];
    setRGB0(&buf->draw, r, g, b);
    ClearOTagR((u_long*)buf->ot[0], GFX_OT_LEN);
    s_layer = 0;
    s_primUsed = 0;
    s_pendCount = 0;
}

void* Gfx_AllocPrim(int bytes)
{
    bytes = (bytes + 3) & ~3;
    if (s_primUsed + bytes > s_primBytes)
        return NULL;   /* el original tambien descarta primitivas si se llena el pool */
    void* p = s_buf[s_cur].prims + s_primUsed;
    s_primUsed += bytes;
    return p;
}

void Gfx_AddPrimDepth(int depth, void* prim)
{
    int idx = 0;
    depth &= 0xffffff;
    if (depth)
    {
        if (depth > 0xffff)
            depth = 0xffff;
        idx = s_depthBase[depth >> 12] + (depth >> s_depthShift[depth >> 12]);
    }
    Gfx_AddPrim(idx, prim);
}

static void LinkPendingPrims(GfxBuffer* buf);

void Gfx_AddPrimDepthPgxp(int depth, void* prim, unsigned pgxpIndex)
{
    Gfx_AddPrimDepth(depth, prim);
#if USE_PGXP
    if (prim)
        setpgxpindex(prim, pgxpIndex);
#else
    (void)pgxpIndex;
#endif
}

/* La OT actual se cierra enlazando su final con el principio de una OT nueva: un solo DrawOTag dibuja
 * primero todo lo anterior y luego lo nuevo (en PsyCross no se puede dibujar a mitad de frame) */
void Gfx_FlushPartial(void)
{
    GfxBuffer* buf = &s_buf[s_cur];
    if (s_layer > 0)
        return;
    ClearOTagR((u_long*)buf->ot[1], GFX_OT_LEN);
    setaddr(&buf->ot[0][0], &buf->ot[1][GFX_OT_LEN - 1]);
    LinkPendingPrims(buf);
    s_layer = 1;
}

int Gfx_GetDrawBufferY(void)
{
    return s_buf[s_cur].draw.clip.y;
}

int Gfx_GetDisplayBufferY(void)
{
    return s_buf[s_cur ^ 1].draw.clip.y;
}

int Gfx_GetFlipCount(void)
{
    return s_flipCount;
}

void Gfx_SetAutoShot(const char* path, int frame)
{
    s_shotPath = path;
    s_shotFrame = frame;
}

void Gfx_AddPrim(int z, void* prim)
{
    if (!prim || s_pendCount == GFX_MAX_PRIMS)
        return;
    if (z < 0)
        z = 0;
    if (z >= GFX_OT_LEN)
        z = GFX_OT_LEN - 1;
#if USE_PGXP
    setpgxpindex(prim, PGXP_GetIndex(1));   /* el indice PGXP se toma al crear la primitiva */
#endif
    s_pendPrim[s_pendCount] = prim;
    s_pendIdx[s_pendCount] = (uint16_t)z;
    s_pendCount++;
}

/* ---------------------------------------------------------------- prueba de visibilidad del sol */

/* Solo en el port. El original (Gfx_DrawLensFlare 8001551c) copia con DR_MOVE los 5x5 pixeles del sol justo despues
 * de dibujar el cielo (en la OT a la profundidad del sol) y al final del frame, y cuenta los que no han cambiado.
 * PsyCross dibuja en la ventana y no ve lo dibujado a mitad de frame, asi que aqui se cuentan los 25 puntos que no
 * cubre ninguna primitiva anadida durante la vista por delante del sol (indice de OT entre 1 y el suyo; el 0 es el
 * del marcador y del propio destello). */
static int s_probeStart = -1, s_probeIdx, s_probeX, s_probeY;

void Gfx_ProbeBegin(int x, int y, int depth)
{
    if (depth > 0xffff)
        depth = 0xffff;
    s_probeIdx = depth > 0 ? s_depthBase[depth >> 12] + (depth >> s_depthShift[depth >> 12]) : 0;
    s_probeX = x;
    s_probeY = y;
    s_probeStart = s_pendCount;
}

/* 1 si (px, py) esta dentro del triangulo (en cualquier sentido) */
static int InTri(float px, float py, float x0, float y0, float x1, float y1, float x2, float y2)
{
    float d0 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
    float d1 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
    float d2 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
    return (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
}

/* esquinas de una primitiva de poligono o rectangulo; 0 si no tapa nada (lineas, comandos de dibujo...) */
static int PrimCorners(const void* p, float* x, float* y)
{
    int code = getcode(p);
    if ((code & 0xe0) == 0x20)
    {
        int quad = code & 8, n = quad ? 4 : 3;
        switch (code & 0x1c)
        {
#define CORNERS(T) { const T* q = (const T*)p; x[0] = (float)q->x0; y[0] = (float)q->y0; x[1] = (float)q->x1; \
    y[1] = (float)q->y1; x[2] = (float)q->x2; y[2] = (float)q->y2; }
        case 0x00: CORNERS(POLY_F3) break;
        case 0x04: CORNERS(POLY_FT3) break;
        case 0x08: CORNERS(POLY_F4) x[3] = (float)((const POLY_F4*)p)->x3; y[3] = (float)((const POLY_F4*)p)->y3; break;
        case 0x0c: CORNERS(POLY_FT4) x[3] = (float)((const POLY_FT4*)p)->x3; y[3] = (float)((const POLY_FT4*)p)->y3; break;
        case 0x10: CORNERS(POLY_G3) break;
        case 0x14: CORNERS(POLY_GT3) break;
        case 0x18: CORNERS(POLY_G4) x[3] = (float)((const POLY_G4*)p)->x3; y[3] = (float)((const POLY_G4*)p)->y3; break;
        case 0x1c: CORNERS(POLY_GT4) x[3] = (float)((const POLY_GT4*)p)->x3; y[3] = (float)((const POLY_GT4*)p)->y3; break;
#undef CORNERS
        }
        return n;
    }
    if ((code & 0xe0) == 0x60)
    {
        /* TILE / SPRT: tamano libre (w, h) o fijo de 1, 8 o 16 */
        static const int fixed[4] = { 0, 1, 8, 16 };
        float x0, y0, w, h;
        if (code & 4)
        {
            const SPRT* s = (const SPRT*)p;
            x0 = (float)s->x0;
            y0 = (float)s->y0;
            w = (code & 0x18) ? (float)fixed[(code >> 3) & 3] : (float)s->w;
            h = (code & 0x18) ? (float)fixed[(code >> 3) & 3] : (float)s->h;
        }
        else
        {
            const TILE* t = (const TILE*)p;
            x0 = (float)t->x0;
            y0 = (float)t->y0;
            w = (code & 0x18) ? (float)fixed[(code >> 3) & 3] : (float)t->w;
            h = (code & 0x18) ? (float)fixed[(code >> 3) & 3] : (float)t->h;
        }
        x[0] = x0, y[0] = y0, x[1] = x0 + w, y[1] = y0, x[2] = x0, y[2] = y0 + h, x[3] = x0 + w, y[3] = y0 + h;
        return 4;
    }
    return 0;
}

int Gfx_ProbeEnd(void)
{
    if (s_probeStart < 0)
        return 0;
    uint32_t covered = 0;   /* bit k: punto (k % 5, k / 5) */
    for (int i = s_probeStart; i < s_pendCount && covered != 0x1ffffff; i++)
    {
        if (s_pendIdx[i] == 0 || s_pendIdx[i] >= s_probeIdx)
            continue;
        float x[4], y[4];
        int n = PrimCorners(s_pendPrim[i], x, y);
        if (!n)
            continue;
        float minX = x[0], maxX = x[0], minY = y[0], maxY = y[0];
        for (int k = 1; k < n; k++)
        {
            minX = x[k] < minX ? x[k] : minX;
            maxX = x[k] > maxX ? x[k] : maxX;
            minY = y[k] < minY ? y[k] : minY;
            maxY = y[k] > maxY ? y[k] : maxY;
        }
        if (maxX < s_probeX || minX > s_probeX + 5 || maxY < s_probeY || minY > s_probeY + 5)
            continue;
        for (int k = 0; k < 25; k++)
        {
            if (covered & (1u << k))
                continue;
            float px = s_probeX + k % 5 + 0.5f, py = s_probeY + k / 5 + 0.5f;
            /* el cuadrilatero de la PS1 son los triangulos 0-1-2 y 1-3-2 */
            if (InTri(px, py, x[0], y[0], x[1], y[1], x[2], y[2]) ||
                (n == 4 && InTri(px, py, x[1], y[1], x[3], y[3], x[2], y[2])))
                covered |= 1u << k;
        }
    }
    s_probeStart = -1;
    int n = 0;
    for (int k = 0; k < 25; k++)
        n += !(covered & (1u << k));
    return n;
}

static void LinkPendingPrims(GfxBuffer* buf)
{
    for (int i = s_pendCount - 1; i >= 0; i--)
    {
        OT_TAG* ot = &buf->ot[s_layer][s_pendIdx[i]];
        setaddr(s_pendPrim[i], getaddr(ot));
        setaddr(ot, s_pendPrim[i]);
    }
    s_pendCount = 0;
}

/* 8001a24c. Port: el area de dibujo y el desplazamiento de la pantalla partida y del retrovisor van como DR_AREA /
 * DR_OFFSET en la OT (race.c), no aqui. */
void Gfx_Flip(int mode)
{
    (void)mode;
    GfxBuffer* buf = &s_buf[s_cur];

    DrawSync(0);
    if (!Net_CatchingUp())   /* port: en red, el atrasado no espera al vblank */
        VSync(0);
    PutDispEnv(&buf->disp);
    PutDrawEnv(&buf->draw);
    LinkPendingPrims(buf);
    DrawOTag((u_long*)&buf->ot[0][GFX_OT_LEN - 1]);
    FntFlush();          /* PsyCross dibuja el texto al momento: va dentro de la escena */
    PsyX_EndScene();     /* en PsyCross el juego cierra la escena y presenta el frame */

    s_cur ^= 1;

    s_flipCount++;
    /* NR_SHOTSEQ=n: tambien guarda los n frames anteriores (nombre.bmp -> nombre_k.bmp) */
    static int s_seq = -1;
    if (s_seq < 0)
        s_seq = getenv("NR_SHOTSEQ") ? atoi(getenv("NR_SHOTSEQ")) : 0;
    if (s_shotPath && s_seq > 0 && s_flipCount < s_shotFrame && s_flipCount >= s_shotFrame - s_seq)
    {
        char p[512];
        snprintf(p, sizeof(p), "%.*s_%d.bmp", (int)(strlen(s_shotPath) - 4), s_shotPath, s_shotFrame - s_flipCount);
        Gfx_SaveScreenshot(p);
    }
    if (s_shotPath && s_flipCount == s_shotFrame)
    {
        Gfx_SaveScreenshot(s_shotPath);
        NR_Log("Captura del frame %d guardada en %s\n", s_flipCount, s_shotPath);
        PsyX_Shutdown();
        exit(0);
    }
}

int Gfx_SaveScreenshot(const char* path)
{
    /* La VRAM emulada no recibe lo que se dibuja en OpenGL: se lee el buffer frontal de la ventana */
    const int w = g_windowWidth, h = g_windowHeight, rowBytes = (w * 3 + 3) & ~3;
    uint8_t* px = (uint8_t*)malloc((size_t)rowBytes * h);
    if (!px)
        return 0;

    glFinish();
    glReadBuffer(GL_FRONT);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, px);   /* filas de abajo arriba, como el BMP */
    glReadBuffer(GL_BACK);

    FILE* f = fopen(path, "wb");
    if (!f)
    {
        free(px);
        return 0;
    }
    uint8_t hdr[54] = { 'B', 'M' };
    uint32_t fileSize = 54 + rowBytes * h;
    memcpy(hdr + 2, &fileSize, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    fwrite(px, 1, (size_t)rowBytes * h, f);
    fclose(f);
    free(px);
    return 1;
}
