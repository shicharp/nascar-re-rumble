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

typedef struct
{
    DISPENV  disp;
    DRAWENV  draw;
    OT_TAG   ot[GFX_OT_LEN];   /* PsyCross: OT_TAG lleva puntero completo + pgxp (8 bytes en x86) */
    uint8_t* prims;
} GfxBuffer;

static GfxBuffer s_buf[2];
static int       s_cur;
static int       s_primBytes;
static int       s_primUsed;
static int       s_flipCount;
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
    GfxBuffer* buf = &s_buf[s_cur];
    setRGB0(&buf->draw, r, g, b);
    ClearOTagR((u_long*)buf->ot, GFX_OT_LEN);
    s_primUsed = 0;
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

int Gfx_GetDrawBufferY(void)
{
    return s_buf[s_cur].draw.clip.y;
}

int Gfx_GetDisplayBufferY(void)
{
    return s_buf[s_cur ^ 1].draw.clip.y;
}

void Gfx_SetAutoShot(const char* path, int frame)
{
    s_shotPath = path;
    s_shotFrame = frame;
}

void Gfx_AddPrim(int z, void* prim)
{
    if (z < 0)
        z = 0;
    if (z >= GFX_OT_LEN)
        z = GFX_OT_LEN - 1;
    addPrim(&s_buf[s_cur].ot[z], prim);
}

/* 8001a24c. Pendiente: modos de pantalla partida y retrovisor (cambian area de dibujo y offset). */
void Gfx_Flip(int mode)
{
    (void)mode;
    GfxBuffer* buf = &s_buf[s_cur];

    DrawSync(0);
    VSync(0);
    PutDispEnv(&buf->disp);
    PutDrawEnv(&buf->draw);
    DrawOTag((u_long*)&buf->ot[GFX_OT_LEN - 1]);
    FntFlush();          /* PsyCross dibuja el texto al momento: va dentro de la escena */
    PsyX_EndScene();     /* en PsyCross el juego cierra la escena y presenta el frame */

    s_cur ^= 1;

    if (s_shotPath && ++s_flipCount == s_shotFrame)
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
