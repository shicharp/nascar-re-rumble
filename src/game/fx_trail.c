/* Estelas (efectos de tipo 0x57): una cinta de nodos que deja un punto del modelo y se va fundiendo. En GlblData
 * solo la clase 1, gris tenue sin textura, que sale de dos puntos del coche mientras va por el aire (80078bb8 al
 * despegar, 80078c24 la quita al aterrizar).
 * Original (0x8006E594-0x80070C5C, 0xA0 bytes por instancia y 100 nodos de 0x2C):
 *  - crear 80070684 (plantilla de 0x58 bytes: +0x34 / +0x36 ancho inicial y final, +0x38..+0x40 color de cada una de
 *    las 3 etapas, +0x41 textura (< 0: animada por nodo), +0x42 / +0x44 / +0x46 duracion de cada etapa, +0x48
 *    distancia entre nodos, +0x4A / +0x4C / +0x4E velocidad inicial de los nodos, +0x50 ruido, +0x52 caida,
 *    +0x54 rozamiento, +0x56 banderas: 1 muere sin nodos, 4 sin textura, 8 los nodos mueren bajo el suelo,
 *    0x10 posicion absoluta, 0x2000 sin cabeza viva), manejador 8006fa04 (0: primer nodo; 7 / 8: deja de crecer).
 *  - actualizar 8006fd98: cada nodo pasa por las 3 etapas de color (8006f704), crece y, con velocidad, cae y se
 *    frena; se anaden nodos cada +0x90 de recorrido de la cabeza (8006f840).
 *  - dibujar 8006e594: tiras de POLY_G4 aditivas (del color en el centro a negro en los bordes) con el ancho
 *    escalado por la distancia, vertices a 1/8 del mundo. Las estelas con textura (tabla 800ab528) no se usan. */
#include <stdlib.h>
#include <string.h>
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "gte.h"
#include "gfx.h"
#include "globals.h"
#include "track.h"
#include "vecmath.h"
#include "render.h"
#include "fx_int.h"
#include "carphys_int.h"

typedef struct Node
{
    struct Node* next;    /* +0x00 */
    int      pos[3];      /* +0x04 */
    int16_t  width;       /* +0x10 */
    int8_t   timer;       /* +0x12 */
    uint8_t  stage;       /* +0x14 */
    uint8_t  rgb[3];      /* +0x15 */
    uint16_t flags;       /* +0x18 1 muerto */
    int16_t  vel[3];      /* +0x1C */
} Node;

typedef struct
{
    FxInst   h;           /* 0x00-0x63 */
    Node*    nodes;       /* +0x68 (el mas nuevo primero) */
    int      numNodes;
    int16_t  widthStep;   /* +0x70 */
    int8_t   tex;         /* +0x72 */
    uint8_t  col[3][3];   /* +0x73.. color de cada etapa (r, g, b) */
    int16_t  step[3][3];  /* +0x7C.. paso de color de cada etapa (r, g, b) */
    uint16_t flags;       /* +0x8E */
    int16_t  spacing;     /* +0x90 */
    int16_t  noise;       /* +0x94 */
    int16_t  fall;        /* +0x96 */
    int16_t  drag;        /* +0x98 */
    int16_t  vel0[3];     /* +0x9A */
} Trail;

#define MAX_NODES 100

/* 80019f34: matriz (columnas 16.16) por vector de shorts */
static void RotShort(const int* m, const int16_t* v, int16_t* out)
{
    for (int a = 0; a < 3; a++)
        out[a] = (int16_t)((v[0] * (m[a] >> 4) + v[1] * (m[3 + a] >> 4) + v[2] * (m[6 + a] >> 4)) >> 12);
}

/* 8006f840: nodo nuevo en la cabeza */
static void Trail_AddNode(Trail* t, const int* pos, const int* frame)
{
    if (t->numNodes >= MAX_NODES)
        return;
    Node* n = (Node*)calloc(1, sizeof(Node));
    if (!n)
        return;
    t->numNodes++;
    memcpy(n->pos, pos, sizeof(n->pos));
    n->width = t->h.scale;
    n->stage = 0;
    for (int a = 0; a < 3; a++)
        n->rgb[a] = t->col[0][a];
    if (t->flags & 0x8000)
    {
        /* (el aviso del ancla, mensaje 4, puede dar otra velocidad inicial) */
        RotShort((t->h.tflags & 0x100) ? t->h.frame : frame, t->vel0, n->vel);
        if (t->noise)
            for (int a = 0; a < 3; a++)
                n->vel[a] = (int16_t)(n->vel[a] + ((Rand_B() >> 20) * t->noise >> 12));
    }
    n->next = t->nodes;
    t->nodes = n;
}

/* Posicion de la cabeza (el punto del modelo); frame = marco base */
static void Trail_Head(Trail* t, const int* base, int* out)
{
    FxInst* in = &t->h;
    if (in->tflags & 0x100)
    {
        memcpy(out, in->frame + 9, 3 * sizeof(int));
        return;
    }
    /* (el original rota el desplazamiento y luego lo pisa: queda sin rotar) */
    if (!(t->flags & 0x10))
        for (int a = 0; a < 3; a++)
            out[a] = in->origin[a] + base[9 + a];
    else
        memcpy(out, in->origin, 3 * sizeof(int));
}

/* Desplazamiento del punto del modelo respecto a la base (en +0x28) */
static void Trail_TrackPoint(Trail* t, const int* base)
{
    FxInst* in = &t->h;
    FxAnchor* an = in->anchor;
    if (in->tflags & 0x100)
    {
        Fx_WorldFrame(an ? an->obj : NULL, in->tflags, in->lp, 1, an, in->frame);
        return;
    }
    GfxObj* obj = an ? an->obj : NULL;
    int p[3];
    const int* pp = base + 9;
    if (obj && in->lp >= 0 && in->lp < 0x10)
    {
        Obj_GetPoint(obj, in->lp, p);
        pp = p;
    }
    if (!(t->flags & 0x10))
        for (int a = 0; a < 3; a++)
            in->origin[a] = pp[a] - base[9 + a];
    else
        memcpy(in->origin, pp, sizeof(in->origin));
}

/* 8006fa04 */
static int Trail_Handler(int msg, FxInst* in, intptr_t a, const FxSpawn* sp)
{
    (void)a;
    (void)sp;
    Trail* t = (Trail*)in;
    if (msg == 10)
    {
        Fx_BaseHandler(10, in);
        while (t->nodes)
        {
            Node* n = t->nodes->next;
            free(t->nodes);
            t->nodes = n;
        }
        free(t);
        return 0;
    }
    if (Fx_BaseHandler(msg, in))
        return 0;
    switch (msg)
    {
    case 0:
    {
        int base[12], head[3];
        memcpy(base, Fx_BaseFrame(in), sizeof(base));
        Trail_TrackPoint(t, base);
        FxAnchor* an = in->anchor;
        if (an && an->cb && an->cb->fn)
            an->cb->fn(3, in, an->cb);
        if (in->tflags & 0x100)
            Mat_MulT(base, in->frame + 9, head);
        else
            Trail_Head(t, base, head);
        Trail_AddNode(t, head, base);
        return 1;
    }
    case 6:
        return (t->flags >> 14) & 1;
    case 7:
    {
        uint16_t f = t->flags;
        t->flags = (uint16_t)(f | 0x2001);
        if (f & 0x4000)
            in->state |= 1;
        return 1;
    }
    case 8:
        t->flags |= 0x2001;
        return 0;
    case 1:
    case 9:
        return 0;
    default:
        return Fx_MiscHandler(msg, in);
    }
}

/* 80070684 */
FxInst* Trail_Create(uint8_t* tp)
{
    Trail* t = (Trail*)calloc(1, sizeof(Trail));
    if (!t)
        return NULL;
    Fx_InitInst(&t->h, tp);
    t->h.handler = Trail_Handler;
    t->tex = TS8(tp, 0x41);
    t->h.scale = T16(tp, 0x34);
    t->spacing = T16(tp, 0x48);
    t->noise = T16(tp, 0x50);
    t->fall = T16(tp, 0x52);
    t->drag = T16(tp, 0x54);
    int yaw = T16(tp, 0x4a), pit = T16(tp, 0x4c);
    if (yaw < 0)
        yaw = (int)((unsigned)Rand_B() % (unsigned)-yaw);
    if (pit < 0)
        pit = (int)((unsigned)Rand_B() % (unsigned)-pit);
    int m[9];
    Math_RotMatrix(yaw, -pit, 0, m);
    for (int a = 0; a < 3; a++)
        t->vel0[a] = (int16_t)((m[6 + a] >> 4) * T16(tp, 0x4e) >> 12);
    int T[3] = { T16(tp, 0x42), T16(tp, 0x44), T16(tp, 0x46) };
    for (int k = 0; k < 3; k++)
    {
        t->col[k][0] = T8(tp, 0x38 + k * 3);
        t->col[k][1] = T8(tp, 0x39 + k * 3);
        t->col[k][2] = T8(tp, 0x3a + k * 3);
    }
    for (int k = 0; k < 3; k++)
        for (int c = 0; c < 3; c++)
        {
            if (T[k] < 0x10)
                t->step[k][c] = 0;
            else if (k == 2)
                t->step[k][c] = (int16_t)(t->col[2][c] * -0x1000 / T[k]);
            else
                t->step[k][c] = (int16_t)(((int)t->col[k + 1][c] - (int)t->col[k][c]) * 0x1000 / T[k]);
        }
    int sum = T[0] + T[1] + T[2];
    t->widthStep = (int16_t)(sum ? (T16(tp, 0x36) - T16(tp, 0x34)) * 0x1000 / sum : 0);
    uint16_t f = TU16(tp, 0x56);
    if (T16(tp, 0x4e) != 0 || T16(tp, 0x50) != 0 || T16(tp, 0x52) != 0)
        f |= 0x8000;
    if (T16(tp, 0x48) < (T16(tp, 0x4e) * sum >> 8))
        f |= 0x4000;
    t->flags = f;
    t->h.prims = 0;
    t->h.prims2 = 10;
    return &t->h;
}

/* 8006f704: avanza el color hacia el de la etapa siguiente; devuelve los canales que faltan */
static int Node_Color(Node* n, int dr, int dg, int db, const uint8_t* to)
{
    int d[3] = { dr, dg, db }, left = 3;
    for (int a = 0; a < 3; a++)
    {
        int v = n->rgb[a] + d[a];
        if (d[a] > 0)
        {
            if (v < to[a])
                n->rgb[a] = (uint8_t)v;
            else
            {
                n->rgb[a] = to[a];
                left--;
            }
        }
        else if (d[a] < 0)
        {
            if (v <= to[a])
            {
                n->rgb[a] = to[a];
                left--;
            }
            else
                n->rgb[a] = (uint8_t)v;
        }
        else
            left--;
    }
    return left;
}

/* 8006fd98 */
void Trail_UpdateAll(FxInst* list, int dt)
{
    int k16 = dt * 0x10;
    for (FxInst* in = list; in; in = in->next)
    {
        if (in->state & 8)
            continue;
        Trail* t = (Trail*)in;
        int dW = (int16_t)((uint32_t)(t->widthStep * k16) >> 12);
        if (!t->nodes && (t->flags & 1))
            in->state |= 1;
        int base[12], head[3];
        memcpy(base, Fx_BaseFrame(in), sizeof(base));
        if (!(t->flags & 0x10))
            Trail_TrackPoint(t, base);
        else
        {
            memcpy(base, in->frame, 9 * sizeof(int));
            memcpy(base + 9, in->origin, 3 * sizeof(int));
        }
        Trail_Head(t, base, head);
        static const uint8_t black[3] = { 0, 0, 0 };
        int kill = 0, prevStage = 3;
        Node* prev = NULL;
        for (Node* n = t->nodes; n;)
        {
            Node* next = n->next;
            if (n->timer > 0 && --n->timer <= 0)
                n->flags |= 1;
            if (n->flags & 1)
                kill = 1;
            if (n->stage < 3)
            {
                int s = n->stage;
                if (Node_Color(n, t->step[s][0] * dt >> 8, t->step[s][1] * dt >> 8, t->step[s][2] * dt >> 8,
                               s == 2 ? black : t->col[s + 1]) == 0 &&
                    ++n->stage < 3)
                    memcpy(n->rgb, t->col[n->stage], 3);
            }
            n->width = (int16_t)(n->width + dW);
            if (t->flags & 0x8000)
            {
                if (t->fall)
                    n->vel[1] = (int16_t)(n->vel[1] + (t->fall * k16 >> 12));
                if (t->drag)
                    for (int a = 0; a < 3; a++)
                        n->vel[a] = (int16_t)(n->vel[a] - (n->vel[a] * t->drag >> 12));
                for (int a = 0; a < 3; a++)
                    n->pos[a] += n->vel[a] * k16 >> 8;
            }
            if (kill || prevStage + n->stage == 6)
            {
                if (prev)
                    prev->next = next;
                else
                    t->nodes = next;
                free(n);
                t->numNodes--;
                if (!t->nodes && (t->flags & 1))
                {
                    in->state |= 1;
                    break;
                }
            }
            else
            {
                if (t->flags & 8)
                {
                    TrackPos tp = { 0xff, 0xff, -1 };
                    if (n->pos[1] < Track_FindSurface(&tp, n->pos, NULL, 0))
                        n->flags |= 1;
                }
                prev = n;
            }
            prevStage = n->stage;   /* (en el original, tambien del que acaba de quitar) */
            n = next;
        }
        if (in->state & 1)
            continue;
        if (!t->nodes)
            Trail_AddNode(t, head, base);
        else if (!(t->flags & 0x2000) && t->spacing > 0)
        {
            int d[3] = { head[0] - t->nodes->pos[0], head[1] - t->nodes->pos[1], head[2] - t->nodes->pos[2] };
            int len = (int16_t)Vec_NormalizeLen(d);
            if (len >= t->spacing)
            {
                for (int a = 0; a < 3; a++)
                    d[a] = d[a] * t->spacing >> 16;
                int p[3] = { t->nodes->pos[0], t->nodes->pos[1], t->nodes->pos[2] };
                for (int rest = len - t->spacing;; rest -= t->spacing)
                {
                    for (int a = 0; a < 3; a++)
                        p[a] += d[a];
                    Trail_AddNode(t, p, base);
                    if (rest < t->spacing)
                        break;
                }
            }
        }
    }
}

/* 80038920: aviso 1 a los duenos de las anclas de las estelas */
void Trail_NotifyAll(FxInst* list)
{
    for (FxInst* in = list; in; in = in->next)
        if (in->anchor && in->anchor->cb && in->anchor->cb->fn)
            in->anchor->cb->fn(1, in, in->anchor->cb);
}

/* 8006e594 */
void Trail_DrawAll(FxInst* list, const int* focus)
{
    for (FxInst* in = list; in; in = in->next)
    {
        Trail* t = (Trail*)in;
        if ((in->state & 8) || !t->nodes)
            continue;
        if (!(t->flags & 4))
            continue;   /* (con textura: sin uso en GlblData) */
        FxAnchor* an = in->anchor;
        int w[12], base[12];
        memcpy(w, Fx_WorldFrame(an ? an->obj : NULL, in->tflags, in->lp, 1, an, in->frame), sizeof(w));
        memcpy(base, Fx_BaseFrame(in), sizeof(base));
        if (!(in->cflags & 1) && !(in->tflags & 0x4000))
        {
            int d[3] = { base[9] - focus[0], base[10] - focus[1], base[11] - focus[2] };
            if (Vec_ApproxLen3(d) > 0x1ffff)
                continue;
        }
        int local = (in->tflags & 0x100) != 0;
        int16_t v[MAX_NODES + 1][4];
        int16_t wid[MAX_NODES + 1];
        uint8_t col[MAX_NODES + 1][3];
        int nv = 0;
        if (!(t->flags & 0x2000))
        {
            /* cabeza viva con el ancho y el color iniciales */
            int head[3];
            Trail_Head(t, base, head);
            for (int a = 0; a < 3; a++)
                v[0][a] = (int16_t)(local ? (in->frame[9 + a] >> 4) << 1 : ((head[a] - g_Camera.pos[a]) >> 4) << 1);
            wid[0] = in->scale;
            memcpy(col[0], t->col[0], 3);
            nv = 1;
        }
        for (Node* n = t->nodes; n && nv <= MAX_NODES && !(n->flags & 1); n = n->next, nv++)
        {
            for (int a = 0; a < 3; a++)
                v[nv][a] = (int16_t)(local ? (n->pos[a] >> 4) << 1 : ((n->pos[a] - g_Camera.pos[a]) >> 4) << 1);
            wid[nv] = n->width;
            memcpy(col[nv], n->rgb, 3);
        }
        if (nv < 2)
            continue;
        if (!local)
            Render_LoadCameraRotation();
        else
        {
            int m[12], rel[3];
            for (int a = 0; a < 3; a++)
                rel[a] = (base[9 + a] - g_Camera.pos[a]) * 2;
            Fx_RotI(g_Camera.view, base, m, 3);
            Fx_RotI(g_Camera.view, rel, m + 9, 1);
            Fx_LoadMatrix(m);
        }
        uint32_t sxy[MAX_NODES + 1];
        int sz[MAX_NODES + 1];
        uint8_t fl[MAX_NODES + 1];
        for (int i = 0; i < nv; i++)
        {
            v[i][3] = 0;
            Gte_SetV0(v[i][0], v[i][1], v[i][2]);
            doCOP2(GTE_OP_RTPS);
            if (CFC2(31) & 0x1c66000)
            {
                fl[i] = 1;
                continue;
            }
            sxy[i] = MFC2(14);
            sz[i] = (int)MFC2(19);
            fl[i] = (uint8_t)(sz[i] < 0x20);
        }
        const RenderProj* pr = Render_GetProjection();
        int16_t ox[MAX_NODES + 1], oy[MAX_NODES + 1];
        int prev = 0, cur = 0, next = 1;
        do
        {
            ox[cur] = oy[cur] = 0;
            if (!((fl[next] | fl[cur] | fl[prev]) & 1))
            {
                int zz = (int)((uint32_t)(uint16_t)sz[prev] << 16) >> 17;
                int sw = zz ? wid[cur] * pr->h / zz : 3;
                if (sw < 3)
                    sw = 3;
                int ay = (int16_t)(sxy[next] >> 16) - (int16_t)(sxy[prev] >> 16);
                int ax = (int16_t)sxy[prev] - (int16_t)sxy[next];
                int a = Abs_(ay), c = Abs_(ax), lo = c;
                if (a < c)
                {
                    lo = a;
                    a = c;
                }
                if (a >> 1 < lo)
                {
                    int tt = lo * 0x6a;
                    lo = a - lo;
                    a = (a * 0x4b + tt) >> 7;
                }
                int len = a + (lo >> 2) - (lo >> 6);
                if (len >= 1)
                {
                    int k = (sw << 10) / len;
                    ox[cur] = (int16_t)(ay * k >> 10);
                    oy[cur] = (int16_t)(ax * k >> 10);
                }
            }
            next = cur + 2 >= nv ? nv - 1 : cur + 2;
            prev = cur;
            cur++;
        } while (cur < nv);
        for (int i = 0; i < nv - 1; i++)
        {
            if ((fl[i] | fl[i + 1]) & 1)
                continue;
            int z = ((int16_t)sz[i] + (int16_t)sz[i + 1]) >> 2;
            int x0 = (int16_t)sxy[i], y0 = (int16_t)(sxy[i] >> 16);
            int x1 = (int16_t)sxy[i + 1], y1 = (int16_t)(sxy[i + 1] >> 16);
            Fx_DrawModeAdd(z);
            for (int side = 0; side < 2; side++)
            {
                int sg = side ? -1 : 1, lim = side ? 1 : 2;   /* (la primera mitad se prueba contra el doble) */
                int qx[4] = { side ? x0 : x0 + ox[i], side ? x0 - ox[i] : x0, side ? x1 : x1 + ox[i + 1],
                              side ? x1 - ox[i + 1] : x1 };
                int qy[4] = { side ? y0 : y0 + oy[i], side ? y0 - oy[i] : y0, side ? y1 : y1 + oy[i + 1],
                              side ? y1 - oy[i + 1] : y1 };
                (void)sg;
                int vis = 0;
                for (int k = 0; k < 4; k++)
                    if ((int16_t)qx[k] < 0x140 * lim + g_WideMargin && (int16_t)qy[k] < 0xf0 * lim)
                        vis = 1;
                if (!vis)
                    break;
                POLY_G4* p = (POLY_G4*)Gfx_AllocPrim(sizeof(POLY_G4));
                if (!p)
                    return;
                setPolyG4(p);
                setSemiTrans(p, 1);
                const uint8_t* c0 = col[i];
                const uint8_t* c1 = col[i + 1];
                if (side == 0)
                {
                    setRGB0(p, 0, 0, 0);
                    setRGB1(p, c0[0], c0[1], c0[2]);
                    setRGB2(p, 0, 0, 0);
                    setRGB3(p, c1[0], c1[1], c1[2]);
                }
                else
                {
                    setRGB0(p, c0[0], c0[1], c0[2]);
                    setRGB1(p, 0, 0, 0);
                    setRGB2(p, c1[0], c1[1], c1[2]);
                    setRGB3(p, 0, 0, 0);
                }
                setXY4(p, qx[0], qy[0], qx[1], qy[1], qx[2], qy[2], qx[3], qy[3]);
                Gfx_AddPrimDepth(z, p);
            }
        }
    }
}
