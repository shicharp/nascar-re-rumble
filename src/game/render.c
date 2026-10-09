#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gfx.h"
#include "gte.h"
#include "render.h"
#include "PsyX/PsyX_public.h"

Camera3D g_Camera;
Light    g_DefaultLight;

static uint16_t s_sinTable[0x401];  /* 800a9c98: round(32768 * sin(i * pi / 2048)) */
static SVECTOR s_lightDir;          /* DAT_800afa24: direccion del sol (4.12) */

/* Proyeccion del preset activo (DAT_800afa30..3f y scratchpad 1f800000, ver Camera_ApplyProjection) */
static RenderProj s_frustum = { 4, 1, 4, 3, 20, 25, 360, { 3, 3 } };

static GfxObj* s_list[64];          /* DAT_800af56c + 0x988 */
static int     s_listCount;         /* + 0x984 */

/* Vertices transformados del objeto actual (scratchpad 1f80000c / 1f800064 en el original) */
#define MAX_VERTS 512
static uint32_t s_scrXY[MAX_VERTS];
static int16_t  s_scrZ[MAX_VERTS];
static uint8_t  s_clip[MAX_VERTS];
static uint32_t s_color[MAX_VERTS]; /* DAT_800af56c + 0xB18: color iluminado de cada normal */
#if USE_PGXP
/* Coordenadas de pantalla precisas (coma flotante de la GTE emulada): sin el temblor de PS1 */
static VERTTYPE s_preciseX[MAX_VERTS], s_preciseY[MAX_VERTS];
#endif

/* g_CameraPresets 800a9bf8: H, r_x, r_y, r_ysplit, shiftX, shiftY, shiftYsplit, LOD */
static const struct { uint16_t h, rx, ry, rys; uint8_t sx[2], sy[2], sys[2], lod[2]; } s_presets[10] = {
    { 120, 10, 11, 18, { 2, 1 }, { 2, 2 }, { 3, 3 }, { 5, 4 } },
    { 180, 12, 14, 25, { 3, 0 }, { 3, 2 }, { 4, 3 }, { 4, 4 } },
    { 240, 14, 18, 33, { 3, 2 }, { 3, 3 }, { 4, 4 }, { 4, 3 } },
    { 360, 20, 25, 49, { 4, 1 }, { 4, 3 }, { 5, 4 }, { 3, 3 } },
    { 480, 25, 33, 64, { 4, 3 }, { 4, 4 }, { 5, 5 }, { 3, 2 } },
    { 720, 37, 49, 96, { 5, 2 }, { 5, 4 }, { 6, 5 }, { 2, 2 } },
    { 960, 49, 64, 128, { 5, 4 }, { 5, 5 }, { 6, 6 }, { 2, 1 } },
    { 1440, 72, 96, 192, { 6, 3 }, { 6, 5 }, { 7, 6 }, { 1, 1 } },
    { 1920, 96, 128, 256, { 6, 5 }, { 6, 6 }, { 7, 7 }, { 1, 0 } },
    { 2880, 144, 192, 384, { 7, 4 }, { 7, 6 }, { 8, 7 }, { 0, 0 } },
};

/* ---------------------------------------------------------------- matematicas */

void Math_SinCos(int angle, int* s, int* c)
{
    int a = angle & 0x3ff;
    int sn = (int)s_sinTable[a] * 2, cs = (int)s_sinTable[0x400 - a] * 2;
    switch ((angle & 0xfff) >> 10)
    {
    case 0: *s = sn; *c = cs; break;
    case 1: *s = cs; *c = -sn; break;
    case 2: *s = -sn; *c = -cs; break;
    default: *s = -cs; *c = sn; break;
    }
}

/* 80017ad8: matriz (columnas 16.16) de los angulos ay (eje Y), ax (eje X), az (eje Z) */
void Math_RotMatrix(int ay, int ax, int az, int* m)
{
    int sy, cy, sx, cx, sz, cz;
    ax &= 0xfff;
    ay &= 0xfff;
    if ((az & 0xfff) == 0)
    {
        if (ax == 0)
        {
            m[4] = 0x10000;
            m[7] = m[5] = m[3] = m[1] = 0;
            if (ay == 0)
            {
                m[8] = m[0] = 0x10000;
                m[6] = m[2] = 0;
            }
            else
            {
                Math_SinCos(ay, &sy, &cy);
                m[0] = m[8] = cy;
                m[6] = sy;
                m[2] = -sy;
            }
        }
        else
        {
            Math_SinCos(ax, &sx, &cx);
            if (ay == 0)
            {
                m[0] = 0x10000;
                m[6] = m[3] = m[2] = m[1] = 0;
                m[5] = sx;
                m[8] = m[4] = cx;
                m[7] = -sx;
            }
            else
            {
                Math_SinCos(ay, &sy, &cy);
                m[0] = cy;
                m[1] = 0;
                m[2] = -sy;
                m[4] = cx;
                m[7] = -sx;
                m[3] = (sx >> 2) * (sy >> 2) >> 12;
                m[5] = (sx >> 2) * (cy >> 2) >> 12;
                m[6] = (cx >> 2) * (sy >> 2) >> 12;
                m[8] = (cx >> 2) * (cy >> 2) >> 12;
            }
        }
        return;
    }
    Math_SinCos(az & 0xfff, &sz, &cz);
    if (ax == 0)
    {
        if (ay == 0)
        {
            m[6] = 0; m[0] = cz; m[8] = 0x10000; m[7] = 0; m[5] = 0; m[2] = 0;
            m[4] = cz; m[1] = sz; m[3] = -sz;
            return;
        }
        Math_SinCos(ay, &sy, &cy);
        m[6] = sy;
        m[0] = (cz >> 2) * (cy >> 2) >> 12;
        m[7] = 0;
        m[8] = cy;
        m[1] = sz;
        m[4] = cz;
        m[2] = -((cz >> 2) * (sy >> 2)) >> 12;
        m[3] = -((sz >> 2) * (cy >> 2)) >> 12;
        m[5] = (sz >> 2) * (sy >> 2) >> 12;
        return;
    }
    Math_SinCos(ax, &sx, &cx);
    if (ay == 0)
    {
        m[6] = 0;
        m[0] = cz;
        m[3] = -sz;
        m[7] = -sx;
        m[8] = cx;
        m[1] = (sz >> 2) * (cx >> 2) >> 12;
        m[2] = (sz >> 2) * (sx >> 2) >> 12;
        m[4] = (cz >> 2) * (cx >> 2) >> 12;
        m[5] = (cz >> 2) * (sx >> 2) >> 12;
        return;
    }
    Math_SinCos(ay, &sy, &cy);
    sz >>= 2; cz >>= 2; cy >>= 2; sy >>= 2; cx >>= 2;
    int t = sz * (sx >> 2) >> 14;
    m[0] = (cz * cy + t * sy) >> 12;
    int u = (sx >> 2) * cz >> 14;
    m[7] = -sx;
    m[1] = sz * cx >> 12;
    m[2] = (t * cy - cz * sy) >> 12;
    m[4] = cz * cx >> 12;
    m[6] = cx * sy >> 12;
    m[8] = cx * cy >> 12;
    m[3] = (u * sy - sz * cy) >> 12;
    m[5] = (sz * sy + u * cy) >> 12;
}

/* Carga en la GTE una matriz de columnas 16.16 (80015880): fila Y negada */
void Gte_LoadColumns(const int* m)
{
    Gte_SetRot(m[0] >> 4, m[3] >> 4, m[6] >> 4,
               -m[1] >> 4, -m[4] >> 4, -m[7] >> 4,
               m[2] >> 4, m[5] >> 4, m[8] >> 4);
}

int g_WideFix = 256;
int g_WideMargin;

void Render_UpdateWide(void)
{
#if USE_PGXP
    RECT16 r;
    PsyX_GetPSXWidescreenMappedViewport(&r);
    int w = r.w - r.x;   /* ancho visible en pixeles PSX */
    g_WideFix = w > 320 ? (w << 8) / 320 : 256;
    g_WideMargin = w > 320 ? (w - 320) / 2 : 0;
#endif
}

const RenderProj* Render_GetProjection(void)
{
    return &s_frustum;
}

/* 8001032c: rotacion de la camara sin traslacion (para la pista) */
void Render_LoadCameraRotation(void)
{
    Gte_LoadColumns(g_Camera.view);
    Gte_SetTrans(0, 0, 0);
}

/* 8001596c: la traspuesta (pasa vectores del mundo al espacio del objeto) */
static void Gte_LoadRows(const int* m)
{
    Gte_SetRot(m[0] >> 4, m[1] >> 4, m[2] >> 4,
               -m[3] >> 4, -m[4] >> 4, -m[5] >> 4,
               m[6] >> 4, m[7] >> 4, m[8] >> 4);
}

/* 800151e4: aplica la rotacion cargada a n vectores de 3 ints */
static void Gte_RotateVectors(const int* in, int* out, int n)
{
    for (int k = 0; k < n; k++, in += 3, out += 3)
    {
        Gte_SetV0(in[0] >> 4, in[1] >> 4, in[2] >> 4);
        doCOP2(GTE_OP_RTV0);
        out[0] = MFC2_S(9) << 4;
        out[1] = -(MFC2_S(10) << 4);
        out[2] = MFC2_S(11) << 4;
    }
}

/* 80016140: V0 * R + TR; devuelve (x, -y, z) >> shift en out y los ceros a la izquierda de z + extra */
static int Gte_TransformShort(const SVECTOR* v, int16_t* out, int shift, int extra)
{
    MTC2((uint32_t)(v->vx & 0xffff) | (uint32_t)v->vy << 16, 0);
    MTC2((uint32_t)v->vz, 1);
    doCOP2(GTE_OP_RTV0TR);
    int x = MFC2_S(25), y = MFC2_S(26), z = MFC2_S(27) >> shift;
    out[0] = (int16_t)(x >> shift);
    out[1] = (int16_t)-(y >> shift);
    out[2] = (int16_t)z;
    return Gte_LeadingZeros(z + (extra >> shift));
}

/* ---------------------------------------------------------------- camara y luz */

void Render_SetPreset(int preset)
{
    if (preset < 0 || preset > 9)
        return;
    s_frustum.sx0 = s_presets[preset].sx[0];
    s_frustum.sx1 = s_presets[preset].sx[1];
    s_frustum.sy0 = s_presets[preset].sy[0];
    s_frustum.sy1 = s_presets[preset].sy[1];
    s_frustum.rx = s_presets[preset].rx;
    s_frustum.ry = s_presets[preset].ry;
    s_frustum.h = s_presets[preset].h;
    s_frustum.lod[0] = s_presets[preset].lod[0];
    s_frustum.lod[1] = s_presets[preset].lod[1];
    SetGeomOffset(160, 120);
    SetGeomScreen(s_presets[preset].h);
}

/* 80016a50 con el retrovisor (camara +7): H * 56 / 160 y centro en el del rectangulo del retrovisor
 * (el original usa (56, 21) con el desplazamiento de dibujo en (0x68, 10); aqui se proyecta ya en su sitio) */
void Render_SetMirrorProjection(int on)
{
    if (on)
    {
        SetGeomOffset(0x68 + 56, 10 + 21);
        SetGeomScreen(s_frustum.h * 0x38 / 0xa0);
    }
    else
    {
        SetGeomOffset(160, 120);
        SetGeomScreen(s_frustum.h);
    }
}

/* 80016bfc + 80017a68 (en coma flotante): base derecha/arriba/adelante mirando de eye a target */
void Camera_LookAt(const int* eye, const int* target)
{
    double f[3] = { (double)(target[0] - eye[0]), (double)(target[1] - eye[1]), (double)(target[2] - eye[2]) };
    double len = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len > 0x400)
    {
        for (int i = 0; i < 3; i++)
            f[i] /= len;
        double r[3] = { f[2], 0, -f[0] };   /* cross((0,1,0), f) */
        double rl = sqrt(r[0] * r[0] + r[2] * r[2]);
        if (rl < 1e-6)
        {
            r[0] = 1;
            r[2] = 0;
            rl = 1;
        }
        r[0] /= rl;
        r[2] /= rl;
        double u[3] = { f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0] };
        for (int k = 0; k < 3; k++)
        {
            g_Camera.view[3 * k + 0] = (int)lround(r[k] * 65536.0);
            g_Camera.view[3 * k + 1] = (int)lround(u[k] * 65536.0);
            g_Camera.view[3 * k + 2] = (int)lround(f[k] * 65536.0);
        }
    }
    g_Camera.pos[0] = eye[0];
    g_Camera.pos[1] = eye[1];
    g_Camera.pos[2] = eye[2];
}

/* 800153e4: direccion del sol (normalizada, 4.12) y color de la luz 1 */
void Light_SetSun(const int* pos, int r, int g, int b)
{
    double x = pos[0], y = pos[1], z = pos[2];
    double len = sqrt(x * x + y * y + z * z);
    if (len > 0)
    {
        s_lightDir.vx = (short)lround(x / len * 4096.0);
        s_lightDir.vy = (short)lround(y / len * 4096.0);
        s_lightDir.vz = (short)lround(z / len * 4096.0);
    }
    g_DefaultLight.lcm[0] = (int16_t)(r << 5);
    g_DefaultLight.lcm[3] = (int16_t)(g << 5);
    g_DefaultLight.lcm[6] = (int16_t)(b << 5);
}

/* 800137d8 */
void Render_Init(void)
{
    for (int i = 0; i <= 0x400; i++)
        s_sinTable[i] = (uint16_t)lround(32768.0 * sin(i * 3.14159265358979323846 / 2048.0));
    memset(&g_DefaultLight, 0, sizeof(g_DefaultLight));
    g_DefaultLight.numLights = 1;
    g_DefaultLight.rgb[0] = g_DefaultLight.rgb[1] = g_DefaultLight.rgb[2] = 0x80;
    g_DefaultLight.bk[0] = g_DefaultLight.bk[1] = g_DefaultLight.bk[2] = 0x600;
    static const int sun[3] = { 0x2000000, 0x1000000, 0x2000000 };
    Light_SetSun(sun, 0x80, 0x80, 0x80);
    s_listCount = 0;
}

/* ---------------------------------------------------------------- objetos */

/* 800139fc + 800138f0 */
void Obj_Init(GfxObj* o, ModelHdr* m)
{
    memset(o, 0, sizeof(*o));
    o->model = m;
    if (m)
    {
        o->radius = *(int16_t*)(m->bbox + 0x14);
        o->bb10 = (short)(*(int16_t*)(m->bbox + 0x10) >> 4);
        o->bb16 = (short)(*(int16_t*)(m->bbox + 0x16) >> 4);
        if ((m->flags >> 30) & 1)
        {
            o->flags = 0x40;
            o->envColor = 0x404040;
        }
        if (m->shadow)
            o->flags |= 0x10;   /* TODO: FUN_8001309c crea una sombra cuadrada si el modelo no trae 3DSD */
    }
    o->shadowN[1] = 0x1000;
    o->light = &g_DefaultLight;
    o->rot[0] = o->rot[4] = o->rot[8] = 0x10000;
}

/* 800367d0: punto i del modelo en coordenadas del objeto */
void Obj_GetLocalPoint(const GfxObj* o, int i, int* out)
{
    out[0] = out[1] = out[2] = 0;
    const ModelHdr* m = o->model;
    if (!m || i < 0 || i >= 12)
        return;
    const int16_t* v = NULL;
    if (!((m->flags >> 25) & 1))
    {
        int n;
        const uint8_t* verts = Model_FrameVerts(m, o->frame, &n);
        if (m->hardpoint[i] != 0xff && m->hardpoint[i] < n)
            v = (const int16_t*)(verts + m->hardpoint[i] * 8);
    }
    else if (m->points && i < (int)m->points[0])
        v = (const int16_t*)(m->points + o->frame * m->points[0] * 2 + i * 2 + 1);
    if (v)
    {
        out[0] = v[0];
        out[1] = v[1];
        out[2] = v[2];
    }
}

/* 80013c20: punto i del modelo en el mundo (pos + rot * local) */
void Obj_GetPoint(const GfxObj* o, int i, int* out)
{
    if (!o->model)
    {
        memcpy(out, o->pos, sizeof(o->pos));
        return;
    }
    int l[3];
    Obj_GetLocalPoint(o, i, l);
    for (int a = 0; a < 3; a++)
        out[a] = o->pos[a] + (int)(((int64_t)o->rot[a] * l[0] + (int64_t)o->rot[3 + a] * l[1] +
                                    (int64_t)o->rot[6 + a] * l[2]) >> 16);
}

void Render_ClearList(void)
{
    s_listCount = 0;
}

void Render_Add(GfxObj* o)
{
    if (s_listCount < (int)(sizeof(s_list) / sizeof(s_list[0])))
    {
        s_list[s_listCount++] = o;
        o->flags &= ~2;   /* se vuelve a marcar en Render_DrawList si esta cerca */
    }
}

/* 8001607c: 0 fuera, 1 detras de la camara, 2 visible */
static int Camera_SphereInFrustum(const int16_t* c, int r)
{
    if (c[2] * 0x10 + r <= 0x20)
        return 1;
    int ax = Wide_X(c[0] * 0x10);
    if (ax < 0)
        ax = -ax;
    int z8 = c[2] * 0x80;
    if ((ax << s_frustum.sx0) + (ax << s_frustum.sx1) > z8 + s_frustum.rx * r)
        return 0;
    int ay = c[1] * 0x10;
    if (ay < 0)
        ay = -ay;
    return z8 + s_frustum.ry * r < (ay << s_frustum.sy0) + (ay << s_frustum.sy1) ? 0 : 2;
}

/* 800162d4: visibilidad, vertices a pantalla y color de cada normal */
static int Render_PrepareObject(GfxObj* o)
{
    ModelHdr* m = o->model;
    int f = o->frame;
    const uint8_t* sphere = m->bbox + f * 0x10 + 8;
    int16_t c[3];
    int lzc = Gte_TransformShort((const SVECTOR*)sphere, c, 4, *(const int16_t*)(sphere + 0xc));
    if (lzc <= 0x10)
        return 0;
    int lod = 0x15 - lzc;
    if (lod < 0)
        lod = 0;
    if (lod > 4)
        lod = 4;
    if (Camera_SphereInFrustum(c, o->radius) != 2)
        return 0;

    int n;
    const int16_t* v = (const int16_t*)Model_FrameVerts(m, f, &n);
    if (n > MAX_VERTS)
        n = MAX_VERTS;
    Gte_SetTrans(o->viewPos[0] >> lod, -o->viewPos[1] >> lod, o->viewPos[2] >> lod);

    /* 80016190 */
    for (int i = 0; i < n; i++, v += 4)
    {
        Gte_SetV0(v[0] >> lod, v[1] >> lod, v[2] >> lod);
        doCOP2(GTE_OP_RTPS);
        uint32_t flag = CFC2(31);
        uint8_t clip = 1;
        if ((flag & 0x1c66000) == 0)
        {
            uint32_t sxy = MFC2(14);
            s_scrZ[i] = (int16_t)(MFC2_S(27) >> (4 - lod));
            s_scrXY[i] = sxy;
#if USE_PGXP
            s_preciseX[i] = g_FP_SXYZ2.x;
            s_preciseY[i] = g_FP_SXYZ2.y;
#endif
            clip = (uint8_t)(Wide_OnScreenX((int16_t)sxy) << 6 | ((sxy >> 16) < 2 * 120u) << 7);
        }
        s_clip[i] = clip;
    }

    /* luz: direcciones al espacio del objeto (sin traslacion) */
    Light* l = o->light;
    Gte_SetTrans(0, 0, 0);
    Gte_LoadRows(o->rot);
    memset(&l->lm[2], 0, 7 * sizeof(int16_t));
    Gte_TransformShort(&s_lightDir, &l->lm[0], 0, 0);
    if (l->numLights > 1)
    {
        Gte_TransformShort(&l->dir2, &l->lm[3], 0, 0);
        if (l->numLights > 2)
            Gte_TransformShort(&l->dir3, &l->lm[6], 0, 0);
    }
    /* 80016260 */
    MTC2((uint32_t)l->rgb[0] | l->rgb[1] << 8 | l->rgb[2] << 16 | (uint32_t)l->rgb[3] << 24, 6);
    CTC2((uint32_t)l->bk[0], 13);
    CTC2((uint32_t)l->bk[1], 14);
    CTC2((uint32_t)l->bk[2], 15);
    for (int i = 0; i < 4; i++)
        CTC2((uint32_t)(uint16_t)l->lm[i * 2] | (uint32_t)l->lm[i * 2 + 1] << 16, 8 + i);
    CTC2((uint32_t)(uint16_t)l->lm[8], 12);
    for (int i = 0; i < 4; i++)
        CTC2((uint32_t)(uint16_t)l->lcm[i * 2] | (uint32_t)l->lcm[i * 2 + 1] << 16, 16 + i);
    CTC2((uint32_t)(uint16_t)l->lcm[8], 20);

    /* 80015a14: NCCT de 3 en 3 normales */
    const uint32_t* nl = (const uint32_t*)(uintptr_t)m->normals[1 + f];
    int nn = (int)nl[0];
    if (nn > MAX_VERTS)
        nn = MAX_VERTS;
    const uint32_t* nv = nl + 1;
    for (int i = 0; i < nn; i += 3)
    {
        for (int k = 0; k < 3; k++)
        {
            int j = i + k < nn ? i + k : nn - 1;
            MTC2(nv[j * 2], k * 2);
            MTC2(nv[j * 2 + 1], k * 2 + 1);
        }
        doCOP2(GTE_OP_NCCT);
        for (int k = 0; k < 3 && i + k < nn; k++)
            s_color[i + k] = MFC2(20 + k);
    }
    return 2;
}

static int NClip(uint32_t a, uint32_t b, uint32_t c)
{
    int x0 = (int16_t)a, y0 = (int16_t)(a >> 16), x1 = (int16_t)b, y1 = (int16_t)(b >> 16);
    int x2 = (int16_t)c, y2 = (int16_t)(c >> 16);
    return x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1;
}

#if USE_PGXP
/* NCLIP con las coordenadas precisas con las que se dibuja (ver track.c): evita huecos en las caras casi de
 * canto a resolucion mayor que la de la PS1 */
static int NClipP(int a, int b, int c)
{
    float x0 = s_preciseX[a], y0 = s_preciseY[a], x1 = s_preciseX[b], y1 = s_preciseY[b];
    float x2 = s_preciseX[c], y2 = s_preciseY[c];
    float n = x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1;
    float an = n < 0 ? -n : n;
    if (0.1f < an && an < 1.0f)
        n += n < 0 ? -1.0f : 1.0f;
    return (int)n;
}
#define NCLIPI(a, b, c) NClipP(a, b, c)
#else
#define NCLIPI(a, b, c) NClip(s_scrXY[a], s_scrXY[b], s_scrXY[c])
#endif

/* Con PGXP las primitivas llevan la proyeccion en coma flotante de la GTE (half con decimales) */
#if USE_PGXP
static VERTTYPE s_hx[4], s_hy[4];
static void StorePreciseXY(int k, int v)
{
    s_hx[k] = s_preciseX[v];
    s_hy[k] = s_preciseY[v];
}
#define APPLY_PRECISE3(p) ((p)->x0 = s_hx[0], (p)->y0 = s_hy[0], (p)->x1 = s_hx[1], (p)->y1 = s_hy[1], \
                           (p)->x2 = s_hx[2], (p)->y2 = s_hy[2])
#define APPLY_PRECISE4(p) (APPLY_PRECISE3(p), (p)->x3 = s_hx[3], (p)->y3 = s_hy[3])
#else
static void StorePreciseXY(int k, int v) { (void)k; (void)v; }
#define APPLY_PRECISE3(p) ((void)0)
#define APPLY_PRECISE4(p) ((void)0)
#endif

#define SX(v) ((int16_t)(v))
#define SY(v) ((int16_t)((v) >> 16))

/* 80015b74: una primitiva FT3/FT4/GT3/GT4 por cara visible. mirror = -1 invierte la orientacion. */
static void Render_EmitPrims(const GfxObj* o, int mirror)
{
    const uint32_t* face = o->model->faces + 1;
    int n = (int)o->model->faces[0];
    for (int fi = 0; fi < n; fi++, face += 6)
    {
        uint32_t idx = face[4];
        int i0 = idx & 0xff, i1 = (idx >> 8) & 0xff, i2 = (idx >> 16) & 0xff, i3 = idx >> 24;
        if (i0 >= MAX_VERTS || i1 >= MAX_VERTS || i2 >= MAX_VERTS || i3 >= MAX_VERTS)
            continue;
        uint8_t fl = s_clip[i0] | s_clip[i1] | s_clip[i2] | s_clip[i3];
        if ((fl & 1) || fl != 0xc0)
            continue;
        uint32_t p0 = s_scrXY[i0], p1 = s_scrXY[i1], p2 = s_scrXY[i2], p3 = s_scrXY[i3];
        int quad = i2 != i3;
        int nc = NCLIPI(i0, i1, i3);
        if (mirror)
            nc = -nc;
        if (nc < 1)
        {
            if (!quad)
                continue;
            int nc2 = NCLIPI(i2, i1, i3);
            if (mirror)
                nc2 = -nc2;
            if (nc2 >= 0)
                continue;
        }
        int depth = ((int)s_scrZ[i0] + s_scrZ[i1] + s_scrZ[i2] + s_scrZ[i3]) >> 2;
        unsigned pgxp = 0xffff;   /* sin busqueda PGXP: la correccion de perspectiva esta desactivada */
        const int order[4] = { i0, i1, i3, i2 };
        for (int k = 0; k < 4; k++)
            StorePreciseXY(k, order[k]);
        uint32_t w0 = face[0], w1 = face[1], w2 = face[2], w3 = face[3], w5 = face[5];
        uint8_t code = (uint8_t)w0;

        if (!(w0 & 0x10))
        {
            uint32_t col = (w0 & 0x20000) ? s_color[w5 & 0xff] : w5 >> 8;
            if (!quad)
            {
                POLY_FT3* p = (POLY_FT3*)Gfx_AllocPrim(sizeof(POLY_FT3));
                if (!p)
                    return;
                setPolyFT3(p);
                setXY3(p, SX(p0), SY(p0), SX(p1), SY(p1), SX(p3), SY(p3));
                APPLY_PRECISE3(p);
                setUV3(p, w1 & 0xff, (w1 >> 8) & 0xff, w2 & 0xff, (w2 >> 8) & 0xff, (w3 >> 16) & 0xff, (w3 >> 24) & 0xff);
                p->clut = (u_short)(w1 >> 16);
                p->tpage = (u_short)(w2 >> 16);
                setRGB0(p, col & 0xff, (col >> 8) & 0xff, (col >> 16) & 0xff);
                setcode(p, code);
                Gfx_AddPrimDepthPgxp(depth, p, pgxp);
            }
            else
            {
                POLY_FT4* p = (POLY_FT4*)Gfx_AllocPrim(sizeof(POLY_FT4));
                if (!p)
                    return;
                setPolyFT4(p);
                setXY4(p, SX(p0), SY(p0), SX(p1), SY(p1), SX(p3), SY(p3), SX(p2), SY(p2));
                APPLY_PRECISE4(p);
                setUV4(p, w1 & 0xff, (w1 >> 8) & 0xff, w2 & 0xff, (w2 >> 8) & 0xff,
                       (w3 >> 16) & 0xff, (w3 >> 24) & 0xff, w3 & 0xff, (w3 >> 8) & 0xff);
                p->clut = (u_short)(w1 >> 16);
                p->tpage = (u_short)(w2 >> 16);
                setRGB0(p, col & 0xff, (col >> 8) & 0xff, (col >> 16) & 0xff);
                setcode(p, code);
                Gfx_AddPrimDepthPgxp(depth, p, pgxp);
            }
        }
        else
        {
            uint32_t c0 = s_color[w5 & 0xff], c1 = s_color[(w5 >> 8) & 0xff];
            uint32_t c2 = s_color[w5 >> 24], c3 = s_color[(w5 >> 16) & 0xff];
            if (!quad)
            {
                POLY_GT3* p = (POLY_GT3*)Gfx_AllocPrim(sizeof(POLY_GT3));
                if (!p)
                    return;
                setPolyGT3(p);
                setXY3(p, SX(p0), SY(p0), SX(p1), SY(p1), SX(p3), SY(p3));
                APPLY_PRECISE3(p);
                setUV3(p, w1 & 0xff, (w1 >> 8) & 0xff, w2 & 0xff, (w2 >> 8) & 0xff, (w3 >> 16) & 0xff, (w3 >> 24) & 0xff);
                p->clut = (u_short)(w1 >> 16);
                p->tpage = (u_short)(w2 >> 16);
                setRGB0(p, c0 & 0xff, (c0 >> 8) & 0xff, (c0 >> 16) & 0xff);
                setRGB1(p, c1 & 0xff, (c1 >> 8) & 0xff, (c1 >> 16) & 0xff);
                setRGB2(p, c2 & 0xff, (c2 >> 8) & 0xff, (c2 >> 16) & 0xff);
                setcode(p, code);
                Gfx_AddPrimDepthPgxp(depth, p, pgxp);
            }
            else
            {
                POLY_GT4* p = (POLY_GT4*)Gfx_AllocPrim(sizeof(POLY_GT4));
                if (!p)
                    return;
                setPolyGT4(p);
                setXY4(p, SX(p0), SY(p0), SX(p1), SY(p1), SX(p3), SY(p3), SX(p2), SY(p2));
                APPLY_PRECISE4(p);
                setUV4(p, w1 & 0xff, (w1 >> 8) & 0xff, w2 & 0xff, (w2 >> 8) & 0xff,
                       (w3 >> 16) & 0xff, (w3 >> 24) & 0xff, w3 & 0xff, (w3 >> 8) & 0xff);
                p->clut = (u_short)(w1 >> 16);
                p->tpage = (u_short)(w2 >> 16);
                setRGB0(p, c0 & 0xff, (c0 >> 8) & 0xff, (c0 >> 16) & 0xff);
                setRGB1(p, c1 & 0xff, (c1 >> 8) & 0xff, (c1 >> 16) & 0xff);
                setRGB2(p, c2 & 0xff, (c2 >> 8) & 0xff, (c2 >> 16) & 0xff);
                setRGB3(p, c3 & 0xff, (c3 >> 8) & 0xff, (c3 >> 16) & 0xff);
                setcode(p, code);
                Gfx_AddPrimDepthPgxp(depth, p, pgxp);
            }
        }
    }
}

/* 80015f4c + 80015280: matrices respecto a la camara (si esta a menos de 0x70000) y dibujo */
void Render_DrawList(int mirror)
{
    for (int k = 0; k < s_listCount; k++)
    {
        GfxObj* o = s_list[k];
        if (!o->model)
            continue;
        int d[12];
        memcpy(d, o->rot, sizeof(o->rot));
        int dx = o->pos[0] - g_Camera.pos[0], dy = o->pos[1] - g_Camera.pos[1], dz = o->pos[2] - g_Camera.pos[2];
        d[9] = dx;
        d[10] = dy;
        d[11] = dz;
        int ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
        int diff = ax - az;
        if (ax + az - ((((diff >> 31) & diff) + az) >> 1) >= 0x70000)
            continue;
        Gte_LoadColumns(g_Camera.view);
        Gte_RotateVectors(d, o->viewRot, 4);
        if (!mirror)
            o->flags |= 2;   /* 80015280: cerca de la camara de la vista 0 (1 << 1); lo miran los efectos */

        Gte_LoadColumns(o->viewRot);
        Gte_SetTrans(o->viewPos[0], -o->viewPos[1], o->viewPos[2]);
        if (Render_PrepareObject(o))
            Render_EmitPrims(o, mirror ? -1 : 0);
    }
}

void Render_LoadObjectMatrix16(const int* rot, const int* pos)
{
    int d[12], out[12];
    memcpy(d, rot, 9 * sizeof(int));
    for (int a = 0; a < 3; a++)
        d[9 + a] = pos[a] - g_Camera.pos[a];
    Gte_LoadColumns(g_Camera.view);
    Gte_RotateVectors(d, out, 4);
    Gte_LoadColumns(out);
    Gte_SetTrans(out[9] >> 4, -out[10] >> 4, out[11] >> 4);
}

int Render_ProjectPoint(const SVECTOR* v, int* sx, int* sy)
{
    Gte_SetV0(v->vx, v->vy, v->vz);
    doCOP2(GTE_OP_RTPS);
    uint32_t sxy = MFC2(14);
    *sx = (int16_t)sxy;
    *sy = (int16_t)(sxy >> 16);
    return (int)MFC2(19);
}
