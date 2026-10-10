/* Efectos visuales: plantillas del Csfx, anclas, instancias, registros persistentes y la cola de impactos.
 * Original (0x80035000-0x8003A000):
 *  - Csfx 80035adc / 800359bc: registros {+0 ?, +4 tamano, +8 datos}; el tipo va en datos +4. 0x56 cambia el
 *    banco (DAT_800af228), 0x5B clase de objeto (80035680, DAT_800b1b38), el resto plantillas por clase (+0x14)
 *    en DAT_800b2320[banco * 0x100 + clase] (80035d88) con sus banderas en DAT_800b1338 (byte 0 de cada
 *    entrada de 4). Las plantillas con la bandera 1 son efectos fijos en la pista (80035c28).
 *  - Crear 80039a80(entidad, clase, parametros, banco, aviso): ancla 80036fb4 (lista DAT_800af23c) e
 *    instancia 80038208 (marco 8003742c / 800372b4, distancia al foco < 0x20000, creador del tipo, mensaje 0,
 *    sonido de la plantilla y, si no tiene vida propia, la clase siguiente en el acto). Las instancias nuevas
 *    esperan en DAT_800af22c y pasan a su lista (80035260, DAT_800b12e8[7] por tipo) al final del frame.
 *  - Clases persistentes (bandera 4 de clase, 80035f14: bucles y cadenas sin fin) pegadas a una entidad:
 *    registro 80039394 (lista DAT_800af234) que se enciende y apaga con la distancia (800395e4).
 *  - Actualizar 80038b50: cola de impactos 80038a48, anclas 80036b9c, tiempos de cada instancia 800387d0 /
 *    80036484 (vida +4, intervalo +0x14 con repeticiones +7, temporizador +0xC), tipos 80038998.
 *  - Dibujar 80038ccc. Cola de impactos (25 x 40 bytes en DAT_800b1f38): 8007734c / 800773d4 (tipo 5),
 *    80075130 (3, ruedas), proceso 800774a4 (clase segun el golpe y la superficie).
 * Pendiente: tipos 0x57, 0x58, 0x5A, 0x5C, llamas del escape (800780d0), emisores 0x54, nube y rayos de
 * la tormenta. Marcas y humo de las ruedas en fx_marks.c. */
#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "gte.h"
#include "gfx.h"
#include "globals.h"
#include "fx_int.h"
#include "actor.h"
#include "carphys_int.h"
#include "track.h"
#include "vecmath.h"
#include "snd.h"

#define ENT_HANDLE(e) (((RaceCar*)(e))->handle)
#define ENT_OBJ(e)    (&((RaceCar*)(e))->obj)

static uint8_t*   s_tmpl[2][256];      /* DAT_800b2320 */
static uint8_t    s_clsFlags[2][256];  /* DAT_800b1338: 1 ?, 4 persistente */
static uint8_t*   s_objCls[256];       /* DAT_800b1b38 */
static int        s_bank;              /* DAT_800af228 */
static FxAnchor*  s_anchors;           /* DAT_800af23c */
static FxInst*    s_lists[7];          /* DAT_800b12e8 */
static FxInst*    s_pending;           /* DAT_800af22c */
static FxPersist* s_persist;           /* DAT_800af234 */
static int        s_keepAnchors;       /* DAT_800af230 */
static int        s_frameWorld[12];    /* DAT_800b1308 */
int g_FxParticles;                     /* DAT_800af7f0 */
static int        s_log = -1;          /* NR_FXLOG=1 (solo en el port): registro de los efectos creados */

static int Fx_LogOn(void)
{
    if (s_log < 0)
        s_log = getenv("NR_FXLOG") != NULL;
    return s_log;
}

/* Entidades creadas durante la carga con efecto de clase de objeto (DAT_800af7d8 / 7dc) */
#define MAX_DEFERRED 256
static void* s_deferred[MAX_DEFERRED];
static int   s_deferredHandle[MAX_DEFERRED];
static int   s_numDeferred;
static int   s_deferOn;

typedef struct
{
    int     kind;        /* +0x00 3 rueda, 4, 5 golpe */
    int     pos[3];      /* +0x04 */
    int     vel[3];      /* +0x10 */
    void*   who;         /* +0x1C (tipo 3: fisica) */
    int16_t speed;       /* +0x20 */
    int16_t id;          /* +0x22 */
    uint8_t who2;        /* +0x24 0 / 1 fisica de un coche, 2 entidad; tipo 3: rueda */
} FxImpact;

static FxImpact s_impacts[25];         /* DAT_800b1f38 */
static int      s_numImpacts;          /* DAT_800af240 */

uint8_t* Fx_Template(int bank, int cls)
{
    return (unsigned)bank < 2 ? s_tmpl[bank][cls & 0xff] : NULL;
}

const uint8_t* Fx_ObjClass(int cls)
{
    return s_objCls[cls & 0xff];
}

/* 80035260: lista de cada tipo */
int Fx_ListOf(int type)
{
    switch (type)
    {
    case 0x57: return 5;
    case 0x58: return 6;
    case 0x59: return 1;
    case 0x5a: return 4;
    case 0x5c: return 2;
    case 0x5d: return 0;
    case 0x5e: return 3;
    default:   return 0;
    }
}

/* ---- matrices (la GTE con la matriz m cargada, como Camera_LoadRotation 8001032c) ---- */

/* 8001006c: rota n vectores de 3 ints (a escala 1/16 en la GTE) */
void Fx_RotI(const int* m, const int* in, int* out, int n)
{
    Gte_LoadColumns(m);
    for (int k = 0; k < n; k++, in += 3, out += 3)
    {
        Gte_SetV0(in[0] >> 4, in[1] >> 4, in[2] >> 4);
        doCOP2(GTE_OP_RTV0);
        out[0] = MFC2_S(9) << 4;
        out[1] = -(MFC2_S(10) << 4);
        out[2] = MFC2_S(11) << 4;
    }
}

/* 8001010c: rota un vector de 3 shorts */
void Fx_RotS(const int* m, const int16_t* in, int16_t* out)
{
    Gte_LoadColumns(m);
    Gte_SetV0(in[0], in[1], in[2]);
    doCOP2(GTE_OP_RTV0);
    out[0] = (int16_t)MFC2_S(9);
    out[1] = (int16_t)-MFC2_S(10);
    out[2] = (int16_t)MFC2_S(11);
}

static void Mat_Identity(int* m)
{
    memset(m, 0, 9 * sizeof(int));
    m[0] = m[4] = m[8] = 0x10000;
}

/* 80035914: punto de referencia de la vista (el coche del jugador o el que sigue la camara) */
void Fx_Focus(int view, int* out)
{
    RaceCar* t = RaceCam_GetTarget(view);
    const RaceCar* c = (!t || t->type == 1) ? g_Humans[view] : t;
    if (c)
        memcpy(out, c->obj.pos, 3 * sizeof(int));
    else
        memcpy(out, g_Camera.pos, 3 * sizeof(int));
}

static int Fx_Near(const int* p, const int* focus)
{
    int d[3] = { p[0] - focus[0], p[1] - focus[1], p[2] - focus[2] };
    return Vec_ApproxLen3(d) < 0x20000;
}

/* ---- plantillas ---- */

/* 80035d88 */
static void Fx_RegisterTemplate(uint8_t* t)
{
    int cls = T8(t, TP_CLASS);
    s_clsFlags[s_bank][cls] = 0;
    free(s_tmpl[s_bank][cls]);
    if (T16(t, TP_REPEAT) == 1)
        T16(t, TP_INTERV) = 0;
    if (T16(t, TP_INTERV) < 1)
    {
        T16(t, TP_INTERV) = 0;
        T16(t, TP_REPEAT) = -1;
    }
    s_tmpl[s_bank][cls] = t;
}

static FxPersist* Fx_AddPersist(int cls, int bank, void* ent, const FxSpawn* sp);

/* 80035c28: plantilla fija en la pista (bandera 1): registro persistente en su sitio. Devuelve 1 si la clase
 * ya tenia plantilla fija (este registro solo anade otro sitio) */
static int Fx_PlaceInWorld(const uint8_t* rec)
{
    if (!(TU16(rec, TP_FLAGS) & 1))
        return 0;
    FxSpawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.flags = 4;
    int m[9];
    Math_RotMatrix(T16(rec, TP_OFSYAW), -T16(rec, TP_OFSPIT), 0, m);
    int d = T16(rec, TP_OFSDIST);
    int p[3] = { T32(rec, TP_POS), T32(rec, TP_POS + 4), T32(rec, TP_POS + 8) };
    TrackPos tp = { 0xff, 0xff, -1 };
    sp.pos[0] = p[0] + ((m[6] >> 4) * d >> 8);
    sp.pos[1] = Track_FindSurface(&tp, p, NULL, 0) + ((m[7] >> 4) * d >> 8);
    sp.pos[2] = p[2] + ((m[8] >> 4) * d >> 8);
    Fx_AddPersist(T8(rec, TP_CLASS), s_bank, NULL, &sp);
    const uint8_t* t = s_tmpl[s_bank][T8(rec, TP_CLASS)];
    return t && (TU16(t, TP_FLAGS) & 1);
}

/* 8003c02c / 8006a514 / 80070c5c / 8006e450 / 80048b00: copia del registro (tamano de cada tipo) */
static void Fx_AddTemplate(const uint8_t* rec, int avail, int size)
{
    if (Fx_PlaceInWorld(rec))
        return;
    uint8_t* t = (uint8_t*)calloc(1, (size_t)size);
    if (!t)
        return;
    memcpy(t, rec, (size_t)(avail < size ? avail : size));
    Fx_RegisterTemplate(t);
}

/* 80035680: clase de objeto (12 bytes) */
static void Fx_AddObjClass(const uint8_t* rec)
{
    int cls = rec[0x14];
    free(s_objCls[cls]);
    uint8_t* o = (uint8_t*)calloc(1, 0xc);
    s_objCls[cls] = o;
    if (!o)
        return;
    o[0] = rec[0x15];
    o[1] = rec[0x16];
    o[2] = rec[0x17];
    o[3] = rec[0x18];
    o[4] = rec[0x19];
    o[5] = rec[0x1a];
    o[6] = rec[0x1b];
    o[7] = rec[0x1c];
    memcpy(o + 8, rec + 0x24, 4);
}

/* 80035adc / 800359bc */
int Fx_OnCsfx(NrAsset* a)
{
    const uint8_t* d = (const uint8_t*)a->data;
    int left = a->size, saved = s_bank, n = 0;
    s_bank = 0;
    while (left > 8)
    {
        int size = (int)NR_Read32(d + 4);
        if (size < 8 || size > left)
            break;
        const uint8_t* rec = d + 8;
        int avail = size - 8;
        if (avail < 0x18)
            break;
        switch (rec[4])
        {
        case 0x56:
            s_bank = (int)NR_Read32(d + 0x1c) & 1;
            break;
        case 0x57:
            Fx_AddTemplate(rec, avail, 0x58);   /* (la clase 0xDF especial de 80070c5c no esta en GlblData) */
            break;
        case 0x58:
        case 0x5a:
            Fx_AddTemplate(rec, avail, 0x4c);
            break;
        case 0x5b:
            Fx_AddObjClass(rec);
            break;
        case 0x5c:
            Fx_AddTemplate(rec, avail, 0x54);
            break;
        case 0x5e:
            Fx_AddTemplate(rec, avail, 0x68);
            break;
        default:
            s_bank = saved;
            return 0;
        }
        n++;
        left -= size;
        d += size;
    }
    s_bank = saved;
    NR_Log("Csfx: %d registros\n", n);
    return 0;
}

/* 80035e84: marca como persistentes las clases de un ciclo */
static void Fx_MarkCycle(int start, int stop, int bank)
{
    for (int guard = 0; guard < 256; guard++)
    {
        const uint8_t* t = s_tmpl[bank][start];
        if (!t)
            return;
        start = T8(t, TP_NEXT);
        s_clsFlags[bank][start] |= 4;
        if (T8(t, TP_CLASS) == stop)
            return;
    }
}

/* 80035f14: bandera 4 de las clases que no terminan solas (repiten sin fin, particulas eternas o ciclos) */
static void Fx_ComputeClassFlags(int bank)
{
    int16_t seen[256];
    memset(seen, 0, sizeof(seen));
    for (int start = 1; start < 256; start++)
    {
        if (!s_tmpl[bank][start] || seen[start] || (s_clsFlags[bank][start] & 4))
            continue;
        int cur = start;
        for (int guard = 0; cur != 0 && guard < 256; guard++)
        {
            const uint8_t* t = s_tmpl[bank][cur];
            if (!t)
                break;
            if (!seen[cur])
                seen[cur] = (int16_t)start;
            int mark;
            if (T16(t, TP_REPEAT) == 0 && T16(t, TP_INTERV) > 0)
                mark = 1;
            else
            {
                int endless;
                switch (T8(t, TP_TYPE))
                {
                case 0x57:
                case 0x58:
                case 0x59:
                    endless = 1;
                    break;
                case 0x5c:
                    endless = T16(t, 0x48) == 0;
                    break;
                case 0x5e:
                    endless = T32(t, TP_TIME) == 0 && (TU16(t, TP_FLAGS) & 2);
                    break;
                default:
                    endless = 0;
                    break;
                }
                mark = endless && T32(t, TP_TIME) == 0;
            }
            if (mark)
                s_clsFlags[bank][cur] |= 4;
            int next = T8(t, TP_NEXT);
            if (next)
            {
                if (!s_tmpl[bank][next])
                    break;
                if (seen[next] == start)
                {
                    seen[cur] = 0;
                    Fx_MarkCycle(start, cur, bank);
                    break;
                }
            }
            cur = next;
        }
    }
}

/* ---- anclas ---- */

/* 80036fb4 */
static FxAnchor* Fx_NewAnchor(void* ent, int cls, int bank, const FxSpawn* sp, FxCallback* cb, int keep)
{
    FxAnchor* a = (FxAnchor*)calloc(1, sizeof(FxAnchor));
    if (!a)
        return NULL;
    a->cb = cb;
    a->unk42 = 1;
    if (!ent)
    {
        if (sp && (sp->flags & 8))
            memcpy(a->rot, sp->rot, sizeof(a->rot));
        else
            Mat_Identity(a->rot);
        a->isStatic = 1;
    }
    else
    {
        a->ent = ent;
        a->entHandle = ENT_HANDLE(ent) ? ENT_HANDLE(ent) : -1;
        a->obj = ENT_OBJ(ent);
    }
    if (sp)
    {
        if (sp->flags & 0x40)
            a->obj = sp->obj;
        if (a->isStatic && (sp->flags & 4))
            memcpy(a->pos, sp->pos, sizeof(a->pos));
    }
    if (a->obj)
        a->scale = (int16_t)(a->obj->radius >> 4);
    else
    {
        a->scale = 0x100;
        if (sp && (sp->flags & 1) && sp->scale >= 1)
            a->scale = (int16_t)(sp->scale >> 4);
    }
    const uint8_t* t = Fx_Template(bank, cls);
    a->flags = (t && (keep || (TU16(t, TP_FLAGS) & 0x40) || (s_clsFlags[bank & 1][cls & 0xff] & 4))) ? 4 : 0;
    a->next = s_anchors;
    s_anchors = a;
    return a;
}

static void Fx_FreeAnchor(FxAnchor* a)
{
    if (a->flags & 2)
        free(a->obj);
    free(a);
}

/* 80036a40: punto de salida en el espacio del objeto */
static void Fx_LocalPoint(GfxObj* o, int lp, int random, const FxSpawn* sp, unsigned tflags, int* out)
{
    out[0] = out[1] = out[2] = 0;
    if (!o->model)
        return;
    if (random && !((o->model->flags >> 25) & 1))
    {
        int n;
        const uint8_t* v = Model_FrameVerts(o->model, o->frame, &n);
        const int16_t* p = (const int16_t*)(v + ((Rand_B() & 0xfff) * n >> 12) * 8);
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        return;
    }
    if ((unsigned)(lp & 0xff) < 0x10)
    {
        Obj_GetLocalPoint(o, lp, out);
        return;
    }
    if (sp && (sp->flags & 4) && (tflags & 0x104))
        memcpy(out, sp->pos, 3 * sizeof(int));
}

/* 8003674c */
static int Fx_HasLocalPoint(const GfxObj* o, int lp)
{
    const ModelHdr* m = o->model;
    if (!m)
        return 0;
    if (!((m->flags >> 25) & 1))
        return lp < 0 || lp >= 12 || m->hardpoint[lp] != 0xff;
    return m->points && lp >= 0 && (unsigned)lp < m->points[0];
}

/* 80036b9c: si la entidad desaparece el ancla se queda con su ultimo marco (o una copia del objeto) */
static void Fx_UpdateAnchors(void)
{
    FxAnchor* prev = NULL;
    for (FxAnchor* a = s_anchors; a;)
    {
        FxAnchor* next = a->next;
        if (!a->isStatic && a->ent && ENT_HANDLE(a->ent) != a->entHandle)
        {
            GfxObj* src = ENT_OBJ(a->ent);
            GfxObj* copy = NULL;
            if (!(a->flags & 4) && (copy = (GfxObj*)malloc(sizeof(GfxObj))) != NULL)
            {
                *copy = *src;
                a->obj = copy;
                a->flags |= 2;
            }
            else
            {
                memcpy(a->rot, src->rot, sizeof(a->rot));
                memcpy(a->pos, src->pos, sizeof(a->pos));
                a->obj = NULL;
            }
            a->isStatic = 1;
            a->flags |= 1;
        }
        if (a->live == 0)
            a->flags |= 8;
        if (a->obj && (a->flags & 0xa) == 0xa)
        {
            memcpy(a->rot, a->obj->rot, sizeof(a->rot));
            memcpy(a->pos, a->obj->pos, sizeof(a->pos));
            free(a->obj);
            a->obj = NULL;
            a->isStatic = 1;
            a->flags = (uint8_t)((a->flags & 0xf5) | 1);
        }
        if (a->count == 0)
        {
            if (prev)
                prev->next = next;
            else
                s_anchors = next;
            Fx_FreeAnchor(a);
        }
        else
            prev = a;
        a = next;
    }
}

/* ---- marcos ---- */

/* 80037280: marco base de una instancia (el objeto del ancla, el ancla fija o el propio) */
const int* Fx_BaseFrame(FxInst* in)
{
    static int s_tmp[12];
    FxAnchor* a = in->anchor;
    if (!a)
        return in->frame;
    if (a->obj)
    {
        memcpy(s_tmp, a->obj->rot, 9 * sizeof(int));
        memcpy(s_tmp + 9, a->obj->pos, 3 * sizeof(int));
    }
    else
    {
        memcpy(s_tmp, a->rot, 9 * sizeof(int));
        memcpy(s_tmp + 9, a->pos, 3 * sizeof(int));
    }
    return s_tmp;
}

/* 800372b4: marco en el mundo de un marco local (bandera 0x100). Como el original, la posicion local se suma
 * sin rotar (la rotada se calcula y se pisa). */
const int* Fx_WorldFrame(GfxObj* obj, unsigned tflags, int lp, int doRot, FxAnchor* an, const int* frame)
{
    (void)lp;
    if (!(tflags & 0x100))
        return frame;
    const int* rot;
    const int* pos;
    if (obj)
    {
        rot = obj->rot;
        pos = obj->pos;
    }
    else if (an)
    {
        rot = an->rot;
        pos = an->pos;
    }
    else
        return frame;
    for (int k = 0; k < 3; k++)
        s_frameWorld[9 + k] = frame[9 + k] + pos[k];
    if (doRot)
        Fx_RotI(rot, frame, s_frameWorld, 3);
    return s_frameWorld;
}

static void Fx_RandAngles(int yaw, int pitch, int* outYaw, int* outPitch)
{
    if ((int16_t)yaw < 0)
        yaw = (int)((unsigned)Rand_B() % (unsigned)-(int16_t)yaw);
    if ((int16_t)pitch < 0)
        pitch = (int16_t)((unsigned)Rand_B() % (unsigned)-(int16_t)pitch);
    *outYaw = yaw;
    *outPitch = pitch;
}

/* Desplazamiento de la plantilla (rumbo / inclinacion al azar si son negativos, distancia 8.8) */
static int Fx_TemplateOffset(const uint8_t* t, int* out)
{
    if (T16(t, TP_OFSDIST) == 0)
        return 0;
    int yaw = T16(t, TP_OFSYAW), pit = T16(t, TP_OFSPIT);
    if (yaw < 0)
        yaw = (int16_t)((unsigned)Rand_B() % (unsigned)-yaw);
    if (pit < 0)
        pit = (int16_t)((unsigned)Rand_B() % (unsigned)-pit);
    int m[9];
    Math_RotMatrix(yaw, -pit, 0, m);
    int d = T16(t, TP_OFSDIST);
    out[0] = (m[6] >> 4) * d >> 8;
    out[1] = (m[7] >> 4) * d >> 8;
    out[2] = (m[8] >> 4) * d >> 8;
    return 1;
}

/* Orientacion de la emision (0x5C: +0x3C / +0x3E, 0x5E: +0x48 / +0x4A); 0 si es la identidad */
static int Fx_EmitRotation(const uint8_t* t, int* m)
{
    int yo, po;
    if (T8(t, TP_TYPE) == 0x5c)
    {
        yo = 0x3c;
        po = 0x3e;
    }
    else if (T8(t, TP_TYPE) == 0x5e)
    {
        yo = 0x48;
        po = 0x4a;
    }
    else
        return 0;
    int yaw, pitch;
    Fx_RandAngles(TU16(t, yo), TU16(t, po), &yaw, &pitch);
    if ((yaw & 0xffff) == 0 && (int16_t)pitch == 0x400)
        return 0;
    Math_RotMatrix((int16_t)yaw, 0x400 - (int16_t)pitch, 0, m);
    return 1;
}

/* 8003742c: escala, punto local y marco (rotacion + posicion) de una instancia nueva */
void Fx_SetupFrame(uint8_t* t, FxInst* parent, FxAnchor* an, const FxSpawn* sp, int* frame,
                   int16_t* scale, int8_t* lp)
{
    int16_t s;
    if (sp && (sp->flags & 1))
        s = (int16_t)(sp->scale >> 4);
    else
        switch (T8(t, TP_TYPE))
        {
        case 0x57:
        case 0x58:
        case 0x59:
            s = T16(t, 0x34);
            break;
        case 0x5c:
            s = T16(t, 0x4c);
            break;
        case 0x5d:
            s = T16(t, 0x36);
            break;
        case 0x5e:
            s = TS8(t, 0x50);
            break;
        default:
            s = 0;
            break;
        }
    if (!sp)
    {
        if (TS8(t, TP_LP) != -1)
            *lp = TS8(t, TP_LP);
    }
    else if (sp->flags & 0x20)
        *lp = sp->lp;
    else if (TS8(t, TP_LP) != -1)
        *lp = TS8(t, TP_LP);
    else
        *lp = parent ? parent->lp : -1;
    if (s == 0)
        s = an ? an->scale : 0x100;
    else if (s < 0)
        s = an ? (int16_t)(an->scale * -s >> 12) : 0x100;
    *scale = s;

    int* pos = frame + 9;
    int ofs[3], m[9];
    unsigned fl = TU16(t, TP_FLAGS);
    if (!an)
    {
        if (sp && (sp->flags & 4))
            memcpy(pos, sp->pos, 3 * sizeof(int));
        else if (!Fx_TemplateOffset(t, pos))
            pos[0] = pos[1] = pos[2] = 0;
    }
    else if (an->obj)
    {
        GfxObj* o = an->obj;
        int random = (fl & 0x80) && o->model && !((o->model->flags >> 25) & 1);
        Fx_LocalPoint(o, *lp, random, sp, fl, pos);
        if (Fx_TemplateOffset(t, ofs))
            for (int k = 0; k < 3; k++)
                pos[k] += ofs[k];
        if (!(fl & 0x100))
        {
            if (fl & 4)
            {
                Fx_RotI(o->rot, pos, pos, 1);
                for (int k = 0; k < 3; k++)
                    pos[k] += o->pos[k];
                if (Fx_EmitRotation(t, m))
                    Fx_RotI(o->rot, m, frame, 3);
                else
                    memcpy(frame, o->rot, 9 * sizeof(int));
                return;
            }
            for (int k = 0; k < 3; k++)
                pos[k] += o->pos[k];
        }
    }
    else
    {
        if (!(fl & 1))
        {
            if (!(fl & 0x100))
            {
                if (Fx_TemplateOffset(t, ofs))
                    for (int k = 0; k < 3; k++)
                        pos[k] = an->pos[k] + ofs[k];
                else
                    memcpy(pos, an->pos, 3 * sizeof(int));
            }
            else if (!Fx_TemplateOffset(t, pos))
                pos[0] = pos[1] = pos[2] = 0;
        }
        else
            memcpy(pos, an->pos, 3 * sizeof(int));
        if (fl & 0x104)
        {
            if (Fx_EmitRotation(t, m))
                Fx_RotI(an->rot, m, frame, 3);
            else
                memcpy(frame, an->rot, 9 * sizeof(int));
            return;
        }
    }
    if (!Fx_EmitRotation(t, frame))
        Mat_Identity(frame);
}

/* ---- instancias ---- */

static uint16_t Fx_Ticks14(int v)
{
    return (uint16_t)((uint32_t)(v * 0x4b) >> 14);   /* 16.16 s -> tics de 1/300 s */
}

/* 80035b18: campos comunes desde la plantilla */
void Fx_InitInst(FxInst* in, uint8_t* t)
{
    in->nextCls = T8(t, TP_NEXT);
    Mat_Identity(in->frame);
    memset(in->origin, 0, sizeof(in->origin));
    in->frame[9] = in->frame[10] = in->frame[11] = 0;
    in->snd = T8(t, TP_SND);
    in->delay = (int16_t)Fx_Ticks14(T32(t, TP_TIME));
    in->life = (int16_t)Fx_Ticks14(T32(t, TP_LIFE));
    in->handler = NULL;
    in->state = 0;
    in->repeats = (int8_t)T8(t, TP_REPEAT);
    in->cflags = 0;
    in->tflags = TU16(t, TP_FLAGS);
    in->counter = T16(t, TP_COUNT);
    in->lp = TS8(t, TP_LP);
    in->next = NULL;
    in->anchor = NULL;
    if (in->nextCls && in->life < 0)
        in->life = 0;
    in->interval = (int16_t)((uint32_t)(T16(t, TP_INTERV) * 0x4b) >> 10);
}

/* 80036170: mensajes comunes (0 crear: temporizadores; 1 emitir; 10 liberar). Devuelve 1 si no hay sitio. */
int Fx_BaseHandler(int msg, FxInst* in)
{
    if (msg == 1)
        in->state &= 0xb;
    else if (msg == 0)
    {
        /* (el original comprueba aqui el sitio libre para primitivas: +0xA) */
        in->delay = (int16_t)Fx_Ticks14(T32(in->tmpl, TP_TIME));
        in->life = (int16_t)Fx_Ticks14(T32(in->tmpl, TP_LIFE));
        in->killTimer = in->delay;
        in->state = 0;
        in->interval = (int16_t)((uint32_t)(T16(in->tmpl, TP_INTERV) * 0x4b) >> 10);
    }
    else if (msg == 10)
    {
        FxAnchor* a = in->anchor;
        if (!a)
            return 0;
        a->count--;
        if (!(in->cflags & 4))
            a->live--;
        if (a->cb && a->cb->fn)
            a->cb->fn(2, in, a->cb);
    }
    return 0;
}

/* 800362f4 */
int Fx_MiscHandler(int msg, FxInst* in)
{
    if (msg == 7)
    {
        if (in->cflags & 4)
            in->state |= 1;
        return 1;
    }
    if (msg == 8)
        in->state |= 1;
    /* 9: mensaje 0xE a la entidad del ancla con TP_MSG9 (sin uso en GlblData) */
    return 0;
}

/* 800363f4: parar (deja de emitir y vence) */
static void Fx_StopInst(FxInst* in)
{
    in->handler(7, in, 0, NULL);
    in->repeats = -1;
    if (in->delay == 0 && (in->tflags & 2))
        in->state |= 1;
    in->interval = 0;
    in->nextCls = 0;
    in->life = -1;
    in->state |= 2;
}

/* 80036484: tiempos. *spawn = clase a crear (la siguiente al vencer, o la propia si no pudo emitir). */
static int Fx_Timers(FxInst* in, int dt, int* spawn)
{
    int again = 0;
    if (!(in->tflags & 0x800) && !(in->state & 2) && in->life >= 0)
    {
        in->life = (int16_t)(in->life - dt);
        if (in->life <= 0)
        {
            if (in->nextCls && (in->counter < 1 || --in->counter > 0))
            {
                *spawn = in->nextCls;
                in->nextCls = 0;
            }
            in->state |= 2;
        }
    }
    if ((in->state & 4) && *spawn == 0)
    {
        again = in->handler(1, in, 0, NULL) != 1;
        if (again)
            *spawn = T8(in->tmpl, TP_CLASS);
        in->state &= 0xfb;
    }
    if (in->interval > 0)
    {
        in->interval = (int16_t)(in->interval - dt);
        if (in->interval <= 0)
        {
            in->interval = (int16_t)((uint32_t)(T16(in->tmpl, TP_INTERV) * 0x4b) >> 10);
            if (in->repeats > 0 && --in->repeats == 0)
            {
                in->repeats = -1;
                if (in->tflags & 0x800)
                {
                    *spawn = in->nextCls;
                    in->nextCls = 0;
                    in->life = -1;
                    in->state |= 2;
                }
            }
            if (in->repeats >= 0)
                in->state |= 4;
        }
    }
    if (in->killTimer > 0)
    {
        in->killTimer = (int16_t)(in->killTimer - dt);
        if (in->killTimer <= 0)
            in->handler(8, in, 0, NULL);
    }
    FxAnchor* a = in->anchor;
    if (a && !a->obj && (a->flags & 1))
    {
        if (in->tflags & 0x1000)
        {
            *spawn = 0;
            Fx_StopInst(in);
            in->state |= 1;
        }
        else if (in->cflags & 4)
        {
            *spawn = 0;
            Fx_StopInst(in);
        }
    }
    return again;
}

static FxInst* Fx_SpawnInst(FxAnchor* an, FxInst* parent, int cls, const FxSpawn* sp, int bank);

/* 800387d0: devuelve 1 si la instancia ha terminado */
static int Fx_UpdateInst(FxInst* in, int dt)
{
    int spawn = 0;
    if (!(in->state & 1))
    {
        if ((in->state & 8) && (in->repeats == -1 || in->life < 1))
            in->state |= 1;
        int again = Fx_Timers(in, dt, &spawn);
        if (spawn)
        {
            FxInst* n = Fx_SpawnInst(in->anchor, in, spawn, NULL, (in->cflags >> 1) & 1);
            if (n)
            {
                n->counter = in->counter;
                if (again)
                {
                    n->repeats = -1;
                    n->life = -1;
                    n->interval = 0;
                    n->state |= 2;
                    n->tflags &= 0xf7ff;
                }
            }
        }
        if (!(in->state & 1))
            return 0;
    }
    if (in->repeats >= 0 || in->life > 0)
        in->state = (uint8_t)((in->state & 0xfe) | 8);
    return in->state & 1;
}

/* 80035260 */
static void Fx_Link(FxInst* in)
{
    int l = Fx_ListOf(T8(in->tmpl, TP_TYPE));
    in->next = s_lists[l];
    s_lists[l] = in;
}

/* 80038208: crea una instancia de la clase en el ancla */
static FxInst* Fx_SpawnInst(FxAnchor* an, FxInst* parent, int cls, const FxSpawn* sp, int bank)
{
    uint8_t* t = Fx_Template(bank, cls);
    if (!t)
        return NULL;
    GfxObj* obj = an ? an->obj : NULL;
    int frame[12];
    int16_t scale = 0;
    int8_t lp = -1;
    Fx_SetupFrame(t, parent, an, sp, frame, &scale, &lp);
    const int* w = Fx_WorldFrame(obj, TU16(t, TP_FLAGS), lp, 0, an, frame);
    if ((s_clsFlags[bank & 1][cls & 0xff] & 5) == 0)
    {
        int vis = 0;
        for (int view = 0; view < (g_SplitScreen ? 2 : 1) && !vis; view++)
        {
            if (TU16(t, TP_FLAGS) & 0x4000)
                vis = 1;
            else
            {
                /* (bandera 8, prueba de la esfera del objeto en la vista: sin uso en GlblData) */
                int f[3];
                Fx_Focus(view, f);
                vis = Fx_Near(w + 9, f);
            }
        }
        if (!vis)
            return NULL;
    }
    FxInst* in;
    switch (T8(t, TP_TYPE))
    {
    case 0x5e:
        in = Psys_Create(t, obj);
        break;
    default:
        in = NULL;   /* 0x57 80070684, 0x58 8006e4b4, 0x5A 80048b64, 0x5C 8006a578: pendientes */
        break;
    }
    if (!in)
        return NULL;
    in->tmpl = t;
    in->cflags |= s_clsFlags[bank & 1][cls & 0xff];   /* 8003520c */
    if (bank == 1)
        in->cflags |= 2;
    in->anchor = an;
    if (an)
    {
        an->count++;
        if (!(in->cflags & 4))
            an->live++;
    }
    if (obj && !Fx_HasLocalPoint(obj, lp))
        lp = -1;
    in->lp = lp;
    memcpy(in->frame, frame, sizeof(frame));
    in->scale = scale;
    if (!in->handler(0, in, (intptr_t)sp, sp))
    {
        in->handler(10, in, 0, NULL);
        return NULL;
    }
    in->next = s_pending;
    s_pending = in;
    if (Fx_LogOn())
        NR_Log("[fx] f=%d t=%d clase %02x tipo %02x banco %d en (%d, %d, %d) %s, particulas %d\n",
               Gfx_GetFlipCount(), g_RaceTime, cls,
               T8(t, TP_TYPE), bank, in->frame[9], in->frame[10], in->frame[11],
               (in->tflags & 0x100) ? "local" : "mundo", g_FxParticles);
    if (in->snd)
    {
        if (obj)
            Snd_PlayAt(in->snd, obj->pos[0], obj->pos[2]);
        else
            Snd_PlayAt(in->snd, in->frame[9], in->frame[11]);
    }
    if (an && (in->tflags & 0x40))
        an->flags |= 8;
    if (parent && T8(parent->tmpl, TP_CLASS) == (cls & 0xff) && parent->nextCls == 0)
        return in;
    if (T32(t, TP_LIFE) != 0 || T8(t, TP_NEXT) == 0 || (TU16(t, TP_FLAGS) & 0x800))
        return in;
    /* sin vida propia: la clase siguiente sale ya */
    Fx_SpawnInst(an, in, T8(t, TP_NEXT), NULL, bank);
    in->nextCls = 0;
    in->state |= 2;
    return in;
}

/* ---- registros persistentes ---- */

/* 80039394 */
static FxPersist* Fx_AddPersist(int cls, int bank, void* ent, const FxSpawn* sp)
{
    FxPersist* r = (FxPersist*)calloc(1, sizeof(FxPersist));
    if (!r)
        return NULL;
    r->cls = (uint8_t)cls;
    r->bank = bank;
    r->ent = ent;
    if (ent)
        r->entHandle = ENT_HANDLE(ent);
    if (sp)
    {
        r->spawn = (FxSpawn*)malloc(sizeof(FxSpawn));
        if (!r->spawn)
        {
            free(r);
            return NULL;
        }
        *r->spawn = *sp;
    }
    r->next = s_persist;
    s_persist = r;
    return r;
}

static void Fx_FreePersist(FxPersist* r)
{
    free(r->spawn);
    free(r);
}

/* 80039474: enciende */
static void Fx_PersistOn(FxPersist* r)
{
    FxAnchor* a = Fx_NewAnchor(r->ent, r->cls, r->bank, r->spawn, NULL, 1);
    if (!a)
        return;
    a->owner = r;
    if (Fx_SpawnInst(a, NULL, r->cls, r->spawn, r->bank))
        r->flags |= 1;
}

static void Fx_FreeInstList(FxInst** head, FxPersist* owner)
{
    FxInst* prev = NULL;
    for (FxInst* in = *head; in;)
    {
        FxInst* next = in->next;
        if (!owner || (in->anchor && in->anchor->owner == owner))
        {
            if (prev)
                prev->next = next;
            else
                *head = next;
            in->next = NULL;
            in->handler(10, in, 0, NULL);
        }
        else
            prev = in;
        in = next;
    }
}

/* 800394f8: apaga */
static void Fx_PersistOff(FxPersist* r)
{
    for (int l = 0; l < 7; l++)
        Fx_FreeInstList(&s_lists[l], r);
    r->flags &= 0xfe;
}

/* 800395e4 */
static void Fx_UpdatePersist(void)
{
    int focus[2][3];
    Fx_Focus(0, focus[0]);
    if (g_SplitScreen)
        Fx_Focus(1, focus[1]);
    FxPersist* prev = NULL;
    for (FxPersist* r = s_persist; r;)
    {
        FxPersist* next = r->next;
        const uint8_t* t = Fx_Template(r->bank, r->cls);
        if (!t)
        {
            prev = r;
            r = next;
            continue;
        }
        const int* p;
        if (r->ent)
        {
            if (ENT_HANDLE(r->ent) != r->entHandle)
            {
                if (prev)
                    prev->next = next;
                else
                    s_persist = next;
                Fx_FreePersist(r);
                r = next;
                continue;
            }
            p = ENT_OBJ(r->ent)->pos;
        }
        else
            p = (r->spawn && (r->spawn->flags & 4)) ? r->spawn->pos : (const int*)(t + TP_POS);
        unsigned fl = TU16(t, TP_FLAGS);
        int vis = 0;
        if (fl & 0x4000)
            vis = 1;
        else if (r->ent && (fl & 0x400))
            vis = ENT_OBJ(r->ent)->flags & 10;
        else
        {
            vis = Fx_Near(p, focus[0]);
            if (!vis && g_SplitScreen)
                vis = Fx_Near(p, focus[1]);
        }
        if (!(r->flags & 1))
        {
            if (vis)
                Fx_PersistOn(r);
        }
        else if (!vis)
            Fx_PersistOff(r);
        prev = r;
        r = next;
    }
}

/* ---- API ---- */

/* 80039a80 */
void* Fx_Spawn(void* ent, int cls, const void* spv, int bank, void* cb)
{
    const FxSpawn* sp = (const FxSpawn*)spv;
    cls &= 0xff;
    if (!Fx_Template(bank, cls))
        return NULL;
    if ((s_clsFlags[bank & 1][cls] & 5) == 4)
    {
        if (ent && ENT_HANDLE(ent) != 0)
            Fx_AddPersist(cls, bank, ent, sp);
        return NULL;
    }
    FxAnchor* a = Fx_NewAnchor(ent, cls, bank, sp, (FxCallback*)cb, s_keepAnchors);
    if (!a)
        return NULL;
    return Fx_SpawnInst(a, NULL, cls, sp, bank);
}

static int Fx_InstOf(FxInst* in, void* ent)
{
    FxAnchor* a = in->anchor;
    if (!a || a->isStatic)
        return ent == NULL;
    return a->ent == ent;
}

/* 800352f8: para los efectos de la entidad (de la clase, o todos con 0) y borra sus registros */
void Fx_Kill(void* ent, int cls)
{
    for (int l = 0; l < 7; l++)
        for (FxInst* in = s_lists[l]; in; in = in->next)
            if (Fx_InstOf(in, ent) && (cls == 0 || cls == T8(in->tmpl, TP_CLASS)))
                Fx_StopInst(in);
    FxPersist* prev = NULL;
    for (FxPersist* r = s_persist; r;)
    {
        FxPersist* next = r->next;
        if (r->ent == ent && (cls == 0 || cls == r->cls))
        {
            if (prev)
                prev->next = next;
            else
                s_persist = next;
            Fx_FreePersist(r);
        }
        else
            prev = r;
        r = next;
    }
}

/* 800391b0: igual, pero las instancias no persistentes mueren en el acto */
void Fx_Stop(void* ent, int cls)
{
    for (int l = 0; l < 7; l++)
        for (FxInst* in = s_lists[l]; in; in = in->next)
            if (T8(in->tmpl, TP_CLASS) == cls && Fx_InstOf(in, ent))
            {
                if (!(in->cflags & 4))
                    in->state |= 1;
                else
                    Fx_StopInst(in);
            }
    FxPersist* prev = NULL;
    for (FxPersist* r = s_persist; r;)
    {
        FxPersist* next = r->next;
        if (r->ent == ent && r->cls == cls)
        {
            if (prev)
                prev->next = next;
            else
                s_persist = next;
            Fx_FreePersist(r);
        }
        else
            prev = r;
        r = next;
    }
}

/* 800354d4: efecto de la clase de objeto al morir la entidad */
void Fx_ObjDie(int cls, void* ent)
{
    int eff;
    Actor* a = (Actor*)ent;
    if (!(a->flags2c & 0x40000))
    {
        const uint8_t* o = s_objCls[cls & 0xff];
        if (!o || !o[2])
            return;
        eff = o[2];
    }
    else
        eff = a->objClass;
    Fx_Spawn(ent, eff, NULL, 0, NULL);
}

/* 8003558c: golpe a la entidad: efecto de su clase de objeto por la cola de impactos */
void Fx_ObjHit(int cls, void* ent)
{
    Actor* a = (Actor*)ent;
    int eff;
    if (!(a->flags2c & 0x40000))
    {
        const uint8_t* o = s_objCls[cls & 0xff];
        if (!o || !o[2])
            return;
        eff = o[2];
    }
    else
        eff = a->objClass;
    Fx_QueueImpact(ent, 2, NULL, a->speed, eff + 1000);
}

/* 80035474 */
static int Fx_ObjCreateEffect(const Actor* a)
{
    if (!a->objClass || (a->flags2c & 0x40000) || !s_objCls[a->objClass])
        return 0;
    return s_objCls[a->objClass][3];
}

/* 80038ff8: efecto de la clase de objeto al crear la entidad (durante la carga se aplaza a Fx_PostLoad) */
void Fx_ObjCreated(void* ent)
{
    int eff = Fx_ObjCreateEffect((const Actor*)ent);
    if (!eff)
        return;
    if (s_deferOn)
    {
        if (s_numDeferred < MAX_DEFERRED)
        {
            s_deferred[s_numDeferred] = ent;
            s_deferredHandle[s_numDeferred++] = ENT_HANDLE(ent);
        }
        return;
    }
    Fx_Spawn(ent, eff, NULL, 0, NULL);
}

/* ---- cola de impactos ---- */

static FxImpact* Fx_NewImpact(int kind)
{
    if (s_numImpacts >= 25)
        return NULL;
    FxImpact* q = &s_impacts[s_numImpacts++];
    q->kind = kind;
    return q;
}

/* 80075130 (la espera por superficie va en fx_marks.c) */
void Fx_QueueWheel(uint8_t* c, int k)
{
    FxImpact* q = Fx_NewImpact(3);
    if (!q)
        return;
    q->who = c;
    q->who2 = (uint8_t)k;
}

/* 8007734c */
void Fx_QueueImpact(void* who, int who2, const int* pos, int speed, int id)
{
    FxImpact* q = Fx_NewImpact(5);
    if (!q)
        return;
    q->who2 = (uint8_t)who2;
    q->who = who;
    q->speed = (int16_t)speed;
    q->id = (int16_t)id;
    if (pos)
        memcpy(q->pos, pos, sizeof(q->pos));
}

/* 800773d4 */
void Fx_QueueImpactVel(void* who, int who2, const int* pos, const int* vel, int id)
{
    FxImpact* q = Fx_NewImpact(5);
    if (!q)
        return;
    q->who2 = (uint8_t)who2;
    q->who = who;
    q->speed = 0;
    if (vel)
        memcpy(q->vel, vel, sizeof(q->vel));
    q->id = (int16_t)id;
    if (pos)
        memcpy(q->pos, pos, sizeof(q->pos));
}

/* 800774a4: clase del golpe (id) y de la superficie (10) */
static void Fx_DoImpact(FxImpact* q)
{
    uint8_t* phys = NULL;    /* piVar6: solo con who2 == 1 */
    uint8_t* physB = NULL;   /* piVar9 */
    void* ent;
    if (q->who2 == 1)
    {
        phys = physB = (uint8_t*)q->who;
        ent = phys ? ENT(phys) : NULL;
    }
    else if (q->who2 == 0)
    {
        physB = (uint8_t*)q->who;
        ent = physB ? ENT(physB) : NULL;
    }
    else
        ent = q->who;
    int frozen = phys && PTR(phys, 0x1b4) && PTR(phys, 0x1b4)[1] == '3';
    if (physB && U8(physB, 0x181) == 7 && q->id != 10)
        return;
    if (q->id > 999)
    {
        Fx_Spawn(ent, (q->id + 0x18) & 0xff, NULL, 0, NULL);
        return;
    }
    FxSpawn sp;
    memset(&sp, 0, sizeof(sp));
    int cls;
    switch (q->id)
    {
    case 0:
        cls = 7;
        break;
    default:
        if (frozen)
            Fx_Spawn(ent, 4, NULL, 0, NULL);   /* trozos de hielo */
        cls = 5;
        break;
    case 2:
        cls = 0x13;
        break;
    case 3:
        cls = 6;
        break;
    case 6:
    case 7:
        if (frozen)
            Fx_Spawn(ent, 4, NULL, 0, NULL);
        cls = 0xb;
        break;
    case 8:
    case 9:
        if (frozen)
            Fx_Spawn(ent, 4, NULL, 0, NULL);
        cls = 0xd;
        break;
    case 10:
        switch (physB ? U8(physB, 0x181) : 0)
        {
        default:
            cls = 0;
            if (!phys || U8(phys, 0x315) == 0)
            {
                cls = 0x3b;
                if (ent && ((RaceCar*)ent)->speed < 0x3d)
                {
                    for (int k = 0; k < 3; k++)
                        sp.vel[k] = q->vel[k] >> 4;
                    sp.flags = 2;
                }
                else
                    cls = 0x16;
                if (phys)
                    U8(phys, 0x315) = 0x40;
            }
            break;
        case 3:
        case 4:
        case 5:
        case 0x11:
            for (int k = 0; k < 3; k++)
                sp.vel[k] = q->vel[k] >> 3;
            sp.flags = 2;
            cls = 0xf;
            break;
        case 7:
            for (int k = 0; k < 3; k++)
                sp.vel[k] = q->vel[k] >> 4;
            sp.flags = 2;
            cls = 0xe;
            Fx_Spawn(ent, 0x17, NULL, 0, NULL);
            break;
        case 9:
        case 10:
            for (int k = 0; k < 3; k++)
                sp.vel[k] = q->vel[k] >> 3;
            sp.flags = 2;
            cls = 0x3c;
            break;
        case 0xb:
            for (int k = 0; k < 3; k++)
                sp.vel[k] = q->vel[k] >> 3;
            sp.flags = 2;
            cls = 0x3e;
            break;
        }
        break;
    case 0xb:
        cls = 0x12;
        break;
    case 0xc:
        cls = 0x41;
        break;
    case 0xd:
        cls = 0x34;
        break;
    }
    /* chispas que siguen al coche si va deprisa (marco local) o giradas con el (4) */
    if ((cls == 7 || cls == 0x12 || cls == 0x13) && physB)
    {
        uint8_t* t = Fx_Template(0, cls);
        if (t)
        {
            uint16_t f = (uint16_t)(TU16(t, TP_FLAGS) & 0xfefb);
            if (!(U16(physB, 0x198) & 0x10) && ((RaceCar*)ENT(physB))->speed > 9)
                f |= 0x100;
            else
                f |= 4;
            TU16(t, TP_FLAGS) = f;
        }
    }
    if (cls == 7 || cls == 0x13)
    {
        uint8_t* t = Fx_Template(0, cls);
        if (t && T8(t, TP_TYPE) == 0x5e)
            T16(t, 0x42) = (TU16(t, TP_FLAGS) & 0x100) ? 0x200 : 0x40;
    }
    memcpy(sp.pos, q->pos, sizeof(sp.pos));
    sp.flags |= 4;
    if (cls)
        Fx_Spawn(ent, cls, &sp, 0, NULL);
}

/* 80038a48 */
static void Fx_ProcessImpacts(void)
{
    for (int i = 0; i < s_numImpacts; i++)
    {
        FxImpact* q = &s_impacts[i];
        switch (q->kind)
        {
        case 0:
            Fx_Spawn(q->who, 0xf, NULL, 1, NULL);
            break;
        case 1:
            Fx_Spawn(q->who, 0xa2, NULL, 0, NULL);
            break;
        case 3:
            Fx_WheelSmoke((uint8_t*)q->who, q->who2);   /* 80075330 */
            break;
        case 4:
            Fx_Spawn(q->who, 4, NULL, 0, NULL);   /* 80077478 */
            break;
        case 5:
            Fx_DoImpact(q);
            break;
        }
    }
    s_numImpacts = 0;
}

/* ---- ciclo de la carrera ---- */

/* 80038d54 */
void Fx_Reset(void)
{
    s_numImpacts = 0;
    memset(s_tmpl, 0, sizeof(s_tmpl));
    memset(s_clsFlags, 0, sizeof(s_clsFlags));
    s_bank = 0;
    s_keepAnchors = 0;
    s_numDeferred = 0;
    s_deferOn = 1;
    memset(s_lists, 0, sizeof(s_lists));
    s_pending = NULL;
    g_FxParticles = 0;
}

/* 80038e28 */
void Fx_FreeAll(int bank)
{
    for (int l = 0; l < 7; l++)
        Fx_FreeInstList(&s_lists[l], NULL);
    Fx_FreeInstList(&s_pending, NULL);
    for (int b = 0; b < 2; b++)
        if (bank == 2 || bank == b)
            for (int c = 0; c < 256; c++)
            {
                free(s_tmpl[b][c]);
                s_tmpl[b][c] = NULL;
            }
    for (int c = 0; c < 256; c++)
    {
        free(s_objCls[c]);
        s_objCls[c] = NULL;
    }
    while (s_persist)
    {
        FxPersist* n = s_persist->next;
        Fx_FreePersist(s_persist);
        s_persist = n;
    }
    while (s_anchors)   /* 800369fc */
    {
        FxAnchor* n = s_anchors->next;
        Fx_FreeAnchor(s_anchors);
        s_anchors = n;
    }
    g_FxParticles = 0;
}

/* 8003913c (+ 8003bd60 de 80075808) */
void Fx_PostLoad(void)
{
    Fx_ComputeClassFlags(0);
    Fx_ComputeClassFlags(1);
    s_deferOn = 0;
    for (int i = 0; i < s_numDeferred; i++)
        if (ENT_HANDLE(s_deferred[i]) == s_deferredHandle[i])
            Fx_ObjCreated(s_deferred[i]);
    s_numDeferred = 0;
    /* (80039098 sube una textura gris de 2x2 en (0x340, 0x100); sin uso conocido) */
    Psys_InitSprites();
}

/* NR_FXTEST=clase[,id] (solo en el port, depuracion): crea la clase en el coche del jugador cada segundo
 * (con id, un golpe de la cola en su lugar) */
static void Fx_DebugSpawn(int dt)
{
    static int s_cls = -1, s_id = -1;
    if (s_cls < 0)
    {
        const char* v = getenv("NR_FXTEST");
        s_cls = v ? (int)strtol(v, NULL, 0) : 0;
        const char* c = v ? strchr(v, ',') : NULL;
        s_id = c ? (int)strtol(c + 1, NULL, 0) : -1;
    }
    RaceCar* h = g_Humans[0];
    if (!s_cls || !h || !h->phys || (g_RaceTime / 300) == ((g_RaceTime - dt) / 300))
        return;
    if (s_id >= 0)
    {
        int p[3] = { 0, 0x100, 0x800 };
        Fx_QueueImpact(h->phys, 1, p, h->speed, s_id);
    }
    else
        Fx_Spawn(h, s_cls, NULL, 0, NULL);
}

/* 80038b50 */
void Fx_Update(int dt)
{
    Fx_DebugSpawn(dt);
    Fx_ProcessImpacts();
    Fx_UpdateAnchors();
    for (int l = 0; l < 7; l++)
    {
        FxInst* prev = NULL;
        for (FxInst* in = s_lists[l]; in;)
        {
            FxInst* next = in->next;
            if (Fx_UpdateInst(in, dt))
            {
                if (prev)
                    prev->next = next;
                else
                    s_lists[l] = next;
                in->next = NULL;
                in->handler(10, in, 0, NULL);
            }
            else
                prev = in;
            in = next;
        }
    }
    /* 80038998 */
    Psys_UpdateAll(s_lists[3], dt);
    if (s_persist)
        Fx_UpdatePersist();
    while (s_pending)
    {
        FxInst* next = s_pending->next;
        Fx_Link(s_pending);
        s_pending = next;
    }
    if (Fx_LogOn() && (g_RaceTime / 1500) != ((g_RaceTime - dt) / 1500))
    {
        int n[7] = { 0 }, na = 0, np = 0;
        for (int l = 0; l < 7; l++)
            for (FxInst* in = s_lists[l]; in; in = in->next)
                n[l]++;
        for (FxAnchor* a = s_anchors; a; a = a->next)
            na++;
        for (FxPersist* r = s_persist; r; r = r->next)
            np++;
        NR_Log("[fx] t=%d instancias 0x5E %d 0x5C %d 0x5A %d 0x57 %d 0x58 %d, anclas %d, persistentes %d, "
               "particulas %d\n", g_RaceTime, n[3], n[2], n[4], n[5], n[6], na, np, g_FxParticles);
    }
}

/* 80038ccc */
void Fx_Draw(int view)
{
    int focus[3];
    Fx_Focus(view, focus);
    /* 800780d0 (sin las llamas del escape, pendientes): baja las banderas de humo y llama del frame */
    for (int i = 0; i < g_NumCars; i++)
        if (g_CarPhys[i])
            U8(g_CarPhys[i], 0x445) &= 0xe3;
    /* 8007567c: marcas de las ruedas de los humanos */
    for (int i = 0; i < g_NumHumans; i++)
        if (g_Humans[i] && g_Humans[i]->phys)
            Marks_Draw(g_Humans[i]->phys, view);
    Psys_DrawAll(s_lists[3], focus, view);
}

/* ---- coches ---- */

/* 80077c60: humo del escape (0x21 al acelerar cada 0x20 tics, 0x22 con la bandera 4 de +0x445) y petardeo */
void Car_ExhaustFx(uint8_t* c, int dt)
{
    RaceCar* e = (RaceCar*)ENT(c);
    if (U16(c, 0x33a) & 1)
    {
        if ((int)U16(c, 0x448) < dt)
        {
            unsigned r = (unsigned)Rand_B();
            U8(c, 0x445) |= 0xc;
            U16(c, 0x448) = (uint16_t)(r % 0x1c2);
            Fx_Push(9, e->obj.pos, -1);   /* petardeo (cola de sonidos DAT_800b52f0) */
        }
        else
            U16(c, 0x448) = (uint16_t)(U16(c, 0x448) - dt);
    }
    if ((int)U8(c, 0x446) < dt)
    {
        U8(c, 0x446) = 0x80;
        if (I32(c, 0x3fc) < -8000 && I32(c, 0x3ec) > 20000)
            U8(c, 0x445) |= 0xc;
        else if (I32(c, 0x3fc) > 80000 && I32(c, 0x3ec) < 16000)
            U8(c, 0x445) |= 4;
    }
    else
        U8(c, 0x446) = (uint8_t)(U8(c, 0x446) - dt);
    int cls;
    if (!(U16(c, 0x33a) & 1))
    {
        if (!(U8(c, 0x445) & 4))
            return;
        cls = 0x22;
    }
    else
    {
        if ((int)U8(c, 0x447) >= dt)
        {
            U8(c, 0x447) = (uint8_t)(U8(c, 0x447) - dt);
            return;
        }
        U8(c, 0x447) = 0x20;
        cls = 0x21;
    }
    for (int k = 0; k < U8(c, 0x444); k++)
    {
        const uint8_t* x = B_(c) + 0x44c + k * 0xe;
        FxSpawn sp;
        memset(&sp, 0, sizeof(sp));
        sp.lp = (int8_t)x[0];
        sp.flags = 0x22;
        sp.vel[0] = (*(const int16_t*)(x + 8) >> 4) + (I32(c, 0x154) >> 5);
        sp.vel[1] = (*(const int16_t*)(x + 10) >> 4) + (I32(c, 0x158) >> 5);
        sp.vel[2] = (*(const int16_t*)(x + 12) >> 4) + (I32(c, 0x15c) >> 5);
        Fx_Spawn(e, cls, &sp, 0, NULL);
    }
}
