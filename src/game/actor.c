/* Actores de la pista: creacion desde los Cact, mensajes, dibujo, choques con los coches y animacion de los
 * modelos. Ver actor.h. */
#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "model.h"
#include "asset.h"
#include "cnet.h"
#include "vecmath.h"
#include "racecar.h"
#include "carphys_int.h"
#include "collide.h"
#include "powerup.h"
#include "actor.h"
#include "fx.h"
#include "body.h"
#include "tvcam.h"

Actor* g_Actors;            /* DAT_800af87c */
static Actor* s_tail;       /* DAT_800af880 */
static int    s_nextHandle; /* DAT_800af870 */

/* ---------------------------------------------------------------- animacion de modelos */

/* 80013aa4: fotograma del modelo (y su radio y caja aproximada) */
void Obj_SetFrame(GfxObj* o, int frame)
{
    if (o->frame == frame || !o->model)
        return;
    const uint8_t* bb = o->model->bbox + frame * 0x10;
    o->frame = (short)frame;
    o->radius = *(const int16_t*)(bb + 0x14);
    o->bb10 = (short)(*(const int16_t*)(bb + 0x10) >> 4);
    o->bb16 = (short)(*(const int16_t*)(bb + 0x16) >> 4);
}

#define SEQ16(q, o) (*(const int16_t*)((q) + (o)))

/* 8003c8ac: empieza la secuencia 'seq' (mode < 1 hacia el principio, >= 1 hacia el final, 2 sin reiniciar el
 * tiempo) */
const uint8_t* Anim_SetSeq(AnimState* s, int seq, int mode, int flags)
{
    if (!s->seqs)
        return NULL;
    const uint8_t* q = s->seqs + seq * 0x10;
    s->seq = (uint8_t)seq;
    s->cur = q;
    if (mode != 2)
        s->time = 0;
    if (mode < 1)
    {
        s->dir = -1;
        s->target = SEQ16(q, 4);
    }
    else
    {
        s->dir = 1;
        s->target = SEQ16(q, 6);
    }
    if (SEQ16(q, 6) < SEQ16(q, 4))
        s->dir = (int8_t)-s->dir;
    s->frame = SEQ16(q, 4);
    s->flags |= (uint8_t)flags;
    return q;
}

/* 8003c95c */
void Anim_Init(const uint32_t* anim, AnimState* s, int forward, int seq)
{
    if (!anim)
    {
        s->seqs = NULL;
        return;
    }
    s->seqs = (const uint8_t*)anim + 4;
    s->count = *(const uint8_t*)anim;
    Anim_SetSeq(s, seq, forward ? 1 : -1, 0);
    s->time = 0;
    s->flags = 0;
}

/* 8002c1a8: quita la orden i de la cola del guion */
static void Actor_RemoveCmd(Actor* a, int i)
{
    for (; i < 3; i++)
    {
        a->cmds[i * 2] = a->cmds[i * 2 + 2];
        a->cmds[i * 2 + 1] = a->cmds[i * 2 + 3];
    }
    a->cmds[6] = a->cmds[7] = 0;
}

/* 8003c9cc: ordenes del guion para la animacion (0x32 secuencia, 0x33 seguir desde el fotograma, 0x34 parar) */
static void Anim_Commands(Actor* a, AnimState* s)
{
    for (int i = 0; i < 4;)
    {
        uint8_t c = a->cmds[i * 2], arg = a->cmds[i * 2 + 1];
        if (c == 0)
            return;
        if (c == 0x32)
            Anim_SetSeq(s, arg, 1, 0);
        else if (c == 0x33 || c == 0x34)
        {
            s->flags = c == 0x33 ? (uint8_t)(s->flags & 0xfd) : (uint8_t)(s->flags | 2);
            s->frame = arg;
        }
        else
        {
            i++;
            continue;
        }
        Actor_RemoveCmd(a, i);
    }
}

/* 8003cabc: eventos de la secuencia (fotograma en los bits 0-9, codigo en los 10-15) */
static void Anim_Event(int ev, int frame, uint32_t* shift, uint32_t* out)
{
    if (frame == (ev & 0x3ff) && (int)*shift < 0x1a)
    {
        *out |= (uint32_t)((ev & 0xfc00) >> 10) << *shift;
        *shift += 6;
    }
}

/* 8003cb08: avanza la animacion dt tics. pick elige la siguiente secuencia (0, 1, 2) al terminar una de ida.
 * Devuelve el bit 0 al terminar la secuencia y los eventos (6 bits cada uno desde el bit 1). */
uint32_t Anim_Step(Actor* a, AnimState* s, int dt, uint32_t pick)
{
    if (!s->seqs)
        return 0;
    if (a)
        Anim_Commands(a, s);
    if (s->flags & 2)
        return 0;
    const uint8_t* q = s->cur;
    uint32_t out = 0, shift = 1;
    for (;;)
    {
        if (s->flags & 1)
        {
            s->flags &= 0xfe;
            if (SEQ16(q, 10))
                Anim_Event(SEQ16(q, 10), s->frame, &shift, &out);
        }
        if (q[1] == 0)
        {
            if (SEQ16(q, 10))
                Anim_Event(SEQ16(q, 10), s->frame, &shift, &out);
            return out;
        }
        if (q[1] == 3)
        {
            s->frame = (int16_t)(SEQ16(q, 4) + (int16_t)((uint32_t)((SEQ16(q, 6) - SEQ16(q, 4)) * dt) >> 16));
            return out;
        }
        if (q[3] & 0x40)
        {
            uint32_t r = (uint32_t)Rand_A() & 0x3f;
            pick = r < 0x2b ? (r < 0x16) ^ 1 : 2;
        }
        if (s->dir == 0)
            return out;
        if (q[3] & 0x20)
            dt += ((0x80 - (Rand_A() & 0xff)) * dt) >> 9;
        s->time += dt;
        dt = 0;
        int len = *(const int32_t*)(q + 0xc);
        if (s->time <= len)
            return out;
        int restart = 0;
        do
        {
            int16_t f = (int16_t)(s->frame + s->dir);
            s->time -= len;
            s->frame = f;
            if (SEQ16(q, 10))
                Anim_Event(SEQ16(q, 10), f, &shift, &out);
            if (q[2] == 1)
            {
                if (s->frame == s->target)
                {
                    s->dir = (int8_t)-s->dir;
                    s->target = (int16_t)(SEQ16(q, 4) + SEQ16(q, 6) - s->target);
                }
            }
            else if (q[2] == 0)
            {
                if (s->frame == s->target + s->dir)
                {
                    out |= 1;
                    uint8_t next = pick == 0 ? q[0] : pick == 1 ? q[8] : q[9];
                    if (s->seq != next)
                    {
                        q = Anim_SetSeq(s, next, 2, 1);
                        restart = 1;
                        break;
                    }
                    int v = (uint16_t)SEQ16(q, 4) + (uint16_t)SEQ16(q, 6) - (uint16_t)s->target;
                    s->frame = (int16_t)v;
                    if (SEQ16(q, 10))
                        Anim_Event(SEQ16(q, 10), (int16_t)v, &shift, &out);
                }
            }
            else if (q[2] == 2)
            {
                if (s->frame == s->target + s->dir)
                {
                    s->frame = s->target;
                    if (SEQ16(q, 10))
                        Anim_Event(SEQ16(q, 10), s->target, &shift, &out);
                    out |= 1;
                }
            }
        } while (s->time > len);
        if (!restart)
            return out;
    }
}

/* ---------------------------------------------------------------- entidades */

ModelHdr* Actor_ResModel(const Actor* a, int i)
{
    if (!a->res || (uint32_t)i >= a->res[0])
        return NULL;
    const NrAsset* as = (const NrAsset*)(uintptr_t)a->res[2 + i * 2];
    return as ? (ModelHdr*)as->obj : NULL;
}

/* 80015928 + 80016784: centro de la caja del modelo en el mundo (+0x64) */
void Actor_UpdateCenter(Actor* a)
{
    if (!a->obj.model || !a->obj.model->bbox)
    {
        memcpy(a->obj.unk34, a->obj.pos, sizeof(a->obj.unk34));
        return;
    }
    Gte_LoadColumns(a->obj.rot);
    Gte_SetTransShift(a->obj.pos, 0);
    Gte_Rt((const short*)(a->obj.model->bbox + a->obj.frame * 0x10 + 8), a->obj.unk34, 0);
}

/* 8002c1e8: punto de la pista bajo el actor (si no hay suelo, el del nodo de la red mas cercano) */
static void Actor_FindTrackPos(Actor* a)
{
    Track_FindSurface(&a->tpos, a->obj.pos, NULL, 1);
    if (a->tpos.curve == -1 && g_NumNets > 0)
    {
        int n = Cnet_FindNearest3D(g_Nets[0], a->obj.pos), p[3];
        if (n < 0)
            return;
        Cnet_NodePos(g_Nets[0], n, p);
        Track_FindSurface(&a->tpos, p, NULL, 1);
    }
}

/* 8002c2a0: parte comun de la creacion (sin los coches). tACT +8: id, +0xC tipo, +0x10 x, y, z, +0x1C
 * banderas, +0x20 vida, +0x22, +0x24 equipo, +0x25, +0x28 clase de objeto, +0x29 */
static void Actor_InitCommon(Actor* a, const ActorDef* d)
{
    const uint8_t* t = d->def;
    a->body = NULL;
    memset(a->cmds, 0, sizeof(a->cmds));
    memset(a->script, 0, sizeof(a->script));   /* asset +0x24..+0x2C (guiones): pendiente */
    a->visible = 1;
    a->team = t[0x1c];
    a->id = *(const uint16_t*)t;
    a->unk23 = t[0x1d];
    a->type = t[4];
    a->objClass = t[0x20];
    a->unk27 = 0xff;
    a->unk26 = t[0x21];
    a->hp = *(const int16_t*)(t + 0x18);
    a->unk1e = *(const int16_t*)(t + 0x1a);
    uint32_t fl = *(const uint32_t*)(t + 0x14);
    a->flags2c = fl & 0xffbfffff;
    if (fl & 0x80)
        a->flags2c |= 0x1000000;
    if (!d->res)
    {
        memset(&a->obj, 0, sizeof(a->obj));
        a->obj.rot[0] = a->obj.rot[4] = a->obj.rot[8] = 0x10000;
        a->model2 = NULL;
    }
    else
    {
        Obj_Init(&a->obj, Actor_ResModel(a, 0));
        a->model2 = Actor_ResModel(a, 1);
    }
    memcpy(a->obj.pos, t + 8, sizeof(a->obj.pos));
    a->tpos.seg = 0xff;
    a->tpos.curve = -1;
    Actor_FindTrackPos(a);
    memcpy(a->obj.unk34, t + 8, sizeof(a->obj.unk34));
    memset(a->vel, 0, sizeof(a->vel));
    a->speed = 0;
    Fx_ObjCreated(a);   /* 80038ff8: efecto de la clase de objeto (+3; ninguno en GlblData) */
}

/* 8002c894 (sin los cuerpos sueltos, los danos 8002c594 ni los guiones) */
int Actor_DefaultHandler(int msg, Actor* a, intptr_t p1, intptr_t p2)
{
    (void)p2;
    switch (msg)
    {
    case 1:
        return 0xe0;
    case 2:
        Actor_InitCommon(a, (const ActorDef*)p1);
        return 0;
    case 3:
        if (a->obj.model && !(a->flags2c & 0x20))
            Render_Add(&a->obj);
        return 0;
    case 10:
    case 0x10:
        return 1;
    case 0xf:
        return Math_Atan2(a->vel[0], a->vel[2]) & 0xfff;
    case 4:
        /* 8002c594: muere una vez (0x1000000 la protege; el sonido de la clase 800354d4 queda pendiente) */
        if (a->flags2c & 0x1000000)
            return 0;
        a->flags2c |= 0x1000010;
        if (a->objClass)
            Fx_ObjDie(a->objClass, a);   /* 800354d4: restos / chispas de la clase de objeto */
        Actor_Destroy(a);
        return 1;
    case 5:
        Body_Free(a);   /* 8004de8c */
        return 0;
    case 6:
        /* 8005610c: el cuerpo suelto; dormido, se libera y el objeto se queda donde cayo */
        if (a->body && Body_Step((uint8_t*)a->body, (int)p1) == 1)
            Body_Free(a);
        return 0;
    case 0xb:
    {
        /* Entity_OnCollide 8002c6a0: el choque ya lo resuelve el coche (los coches tienen clase 0); aqui solo
         * el dano (el +0x1E del otro: 1 para los coches) y la bandera 0x2000000 (el guion 80067890, pendiente) */
        const RaceCar* o = (const RaceCar*)p1;
        int dmg = o ? (o->type == 1 || o->type == 0x11 ? 1 : o->unk1e) : 0;
        if (!(a->flags2c & 0x80) && dmg > 0)
        {
            a->hp = (int16_t)(a->hp - dmg);
            a->flags2c |= 0x2000000;
        }
        return 0;
    }
    case 7:
    case 8:
    case 9:
    case 0xc:
    case 0xd:
    case 0xe:
        return 0;
    default:
        return -1;
    }
}

/* 800491ec + 800491c4 (al final de la lista) */
Actor* Actor_Create(const ActorDef* d)
{
    Actor* a = (Actor*)calloc(1, sizeof(Actor));
    if (!a)
        return NULL;
    a->handler = d->handler;
    a->handle = ++s_nextHandle;
    a->def = d->def;
    a->res = d->res;
    d->handler(2, a, (intptr_t)d, 0);
    if (s_tail)
        s_tail->next = a;
    else
        g_Actors = a;
    s_tail = a;
    a->next = NULL;
    return a;
}

/* 80049688 (sin guiones ni sonidos): la entidad muere y se libera en Actors_Reap */
void Actor_Destroy(Actor* a)
{
    a->handle = 0;
}

/* 80049360: libera las muertas (mensaje 5) */
static void Actors_Reap(void)
{
    Actor* prev = NULL;
    for (Actor* a = g_Actors; a;)
    {
        Actor* next = a->next;
        if (a->handle == 0)
        {
            a->handler(5, a, 0, 0);
            if (prev)
                prev->next = next;
            else
                g_Actors = next;
            if (!next)
                s_tail = prev;
            free(a);
        }
        else
            prev = a;
        a = next;
    }
}

/* 80048d5c */
/* Numero de entidad nuevo (DAT_800af870): los coches tambien son entidades en el original */
int Ent_NewHandle(void)
{
    return ++s_nextHandle;
}

void Actors_Free(void)
{
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->handle)
        {
            a->flags2c |= 0x4000000;
            Actor_Destroy(a);
        }
    Actors_Reap();
    g_Actors = s_tail = NULL;
    s_nextHandle = 0;
    Pickup_Reset();
}

/* ---------------------------------------------------------------- actores de la pista */

/* 8003421c: objeto de la pista (tACT +0x24 modo de suelo, +0x25 bit 0 apoyado en la pendiente, +0x26 rumbo,
 * +0x28 altura sobre el suelo) */
static int Prop_Create(Actor* a, const ActorDef* d)
{
    Actor_DefaultHandler(2, a, (intptr_t)d, 0);
    const uint8_t* t = d->def;
    int mode = (int8_t)t[0x24], yoff = *(const int32_t*)(t + 0x28), heading = *(const int16_t*)(t + 0x26);
    int g = mode == 0 ? -1 : mode == 1 ? -2 : mode - 2;
    int y = Track_GroundHeight(&a->tpos, a->obj.pos, g) + yoff;
    a->obj.pos[1] = y;
    if (a->obj.model)
    {
        /* la base de la caja del modelo (centro Y - semialto) sobre el suelo */
        const uint8_t* bb = a->obj.model->bbox;
        a->obj.pos[1] = y + (*(const int16_t*)(bb + 0x10) - *(const int16_t*)(bb + 0xa));
    }
    int s, c;
    if (!(t[0x25] & 1))
    {
        Math_SinCos(heading, &s, &c);
        int* r = a->obj.rot;
        r[1] = r[3] = r[5] = r[7] = 0;
        r[4] = 0x10000;
        r[6] = s;
        r[8] = c;
        r[0] = c;
        r[2] = -s;
    }
    else
    {
        /* 80017fc8: ejes con Y = normal del suelo, X = normal x rumbo, Z = X x normal */
        int n[3];
        a->obj.pos[1] = Track_FindSurface(&a->tpos, a->obj.pos, n, 1) + yoff;
        Math_SinCos(heading, &s, &c);
        int h[3] = { c, 0, s };
        int* r = a->obj.rot;
        Vec_Cross(n, h, r);
        Vec_Normalize(r);
        Vec_Cross(r, n, r + 6);
        memcpy(r + 3, n, sizeof(n));
    }
    Actor_UpdateCenter(a);
    return 0;
}

/* 800343dc */
static int Prop_Handler(int msg, Actor* a, intptr_t p1, intptr_t p2)
{
    if (msg == 1)
        return 0xe0;
    if (msg == 2)
        return Prop_Create(a, (const ActorDef*)p1);
    return Actor_DefaultHandler(msg, a, p1, p2);
}

/* Objeto que gira (tipo 6, 0xF8 bytes): +0xE0 parametro del modo, +0xE2 modo de seguimiento (0 girar sin parar,
 * 1-4 mirar a la entidad +0xE4 si su handle sigue siendo +0xE8 y esta a menos de +0xEE * 16), +0xE3 bits (1 se
 * orienta, 2 gira sobre su eje Z, 4 parado, 8 tambien cabecea, 0x40 rumbo limitado a +-0x300), +0xEC velocidad,
 * +0xF0 rumbo inicial, +0xF2 giro fijo (modo 2) o cuenta atras, +0xF4 rumbo, +0xF6 cabeceo.
 * tACT +0x34 modo de suelo, +0x35 / +0x36 modo y parametro, +0x38 rumbo, +0x3A altura / 16, +0x3C velocidad,
 * +0x3E alcance, +0x40 bits (1 / 4 orientarse, 2 cabecear, 8 girar, 0x10 lista DAT_800b61e0, 0x20 limite,
 * 0x80 sin sonidos de clase al crearlo). */
#define TU_PARAM(a)  AU16(a, 0xe0)
#define TU_MODE(a)   A8(a, 0xe2)
#define TU_BITS(a)   A8(a, 0xe3)
#define TU_TARGET(a) (*(uint8_t**)((uint8_t*)(a) + 0xe4))
#define TU_THANDLE(a) A32(a, 0xe8)
#define TU_SPEED(a)  A16(a, 0xec)
#define TU_RANGE(a)  A16(a, 0xee)
#define TU_BASE(a)   A16(a, 0xf0)
#define TU_AUX(a)    A16(a, 0xf2)
#define TU_YAW(a)    A16(a, 0xf4)
#define TU_PITCH(a)  A16(a, 0xf6)

/* 800339b4 */
static int Turret_Create(Actor* a, const ActorDef* d)
{
    const uint8_t* t = d->def;
    uint8_t fl = t[0x40];
    Actor_DefaultHandler(2, a, (intptr_t)d, 0);   /* (0x80: sin la clase de objeto mientras se crea; solo sonidos) */
    TU_BITS(a) = (uint8_t)((fl & 1 ? 0x10 : 0) | (fl & 2 ? 8 : 0));
    int16_t heading = *(const int16_t*)(t + 0x38);
    Math_RotMatrix(heading, 0, 0, a->obj.rot);
    TU_PITCH(a) = 0;
    TU_BASE(a) = TU_YAW(a) = heading;
    int spin = (fl & 8) != 0;
    if (fl & 5)
    {
        TU_SPEED(a) = (int16_t)(((int)*(const int16_t*)(t + 0x3c) * 0x10 / 0xda) >> 2);
        TU_AUX(a) = 0;
        TU_TARGET(a) = NULL;
        TU_BITS(a) |= 1;
        TU_RANGE(a) = *(const int16_t*)(t + 0x3e);
        TU_MODE(a) = t[0x35];
        TU_PARAM(a) = *(const uint16_t*)(t + 0x36);
        spin = 0;   /* el original borra el bit 8 del tACT */
    }
    if (spin)
    {
        TU_SPEED(a) = (int16_t)(((int)*(const int16_t*)(t + 0x3c) << 4) / 300 >> 2);
        TU_BITS(a) |= 2;
        TU_AUX(a) = heading;
    }
    int mode = (int8_t)t[0x34];
    int y = Track_GroundHeight(&a->tpos, a->obj.pos, mode == 0 ? -1 : mode == 1 ? -2 : mode - 2) +
            *(const int16_t*)(t + 0x3a) * 0x10;
    a->obj.pos[1] = y;
    if (a->obj.model)
    {
        const uint8_t* bb = a->obj.model->bbox;
        a->obj.pos[1] = y + (*(const int16_t*)(bb + 0x10) - *(const int16_t*)(bb + 0xa));
    }
    Actor_UpdateCenter(a);
    if (fl & 0x10)
        TU_BITS(a) |= 0x20;   /* 80061c88: lista DAT_800b61e0 (24 entradas, uso sin identificar) */
    if (fl & 0x20)
        TU_BITS(a) |= 0x40;
    return 0;
}

/* 80033c5c: ordenes del guion 0x29-0x2C (modo de seguimiento y parametro) y 0x2D (0 parar, si no seguir) */
static void Turret_Commands(Actor* a)
{
    for (int i = 0; i < 4;)
    {
        uint8_t c = a->cmds[i * 2], arg = a->cmds[i * 2 + 1];
        if (c == 0)
            return;
        if (c > 0x28 && c < 0x2d)
        {
            TU_MODE(a) = (uint8_t)(c - 0x28);
            TU_PARAM(a) = arg;
        }
        else if (c == 0x2d)
            TU_BITS(a) = arg == 0 ? (uint8_t)(TU_BITS(a) | 4) : (uint8_t)(TU_BITS(a) & 0xfb);
        else
        {
            i++;
            continue;
        }
        Actor_RemoveCmd(a, i);
    }
}

/* gira 'cur' hacia 'want' como mucho 'step' (angulos de 12 bits) */
static int16_t Angle_Approach(int cur, int want, int step)
{
    int d = (want - cur) & 0xfff;
    if (d > 0x800)
        d -= 0x1000;
    int m = Abs_(d) < step ? Abs_(d) : step;
    return (int16_t)((cur + (d < 0 ? -m : m)) & 0xfff);
}

/* 80033d30 */
static void Turret_Update(Actor* a, int dt)
{
    if (a->cmds[0])
        Turret_Commands(a);
    if (!a->obj.model || (TU_BITS(a) & 4))
        return;
    if (TU_BITS(a) & 2)
    {
        TU_YAW(a) = (int16_t)((TU_YAW(a) + ((dt * TU_SPEED(a)) >> 2)) & 0xfff);
        Math_RotMatrix(TU_AUX(a), 0, TU_YAW(a), a->obj.rot);
        Actor_UpdateCenter(a);
        return;
    }
    if (!(TU_BITS(a) & 1))
        return;
    int yaw, pitch = 0;
    if (TU_MODE(a) == 0)
        yaw = (TU_YAW(a) + 0x800) & 0xfff;
    else
    {
        uint8_t* t = TU_TARGET(a);
        TU_AUX(a) = (int16_t)(TU_AUX(a) - dt);
        yaw = TU_BASE(a);
        if (t && A32(t, 0xc) == TU_THANDLE(a))
        {
            int v[3] = { A32(t, 0x64) - a->obj.unk34[0], A32(t, 0x68) - a->obj.unk34[1], A32(t, 0x6c) - a->obj.unk34[2] };
            int dist = Vec_ApproxLen3(v);
            if (dist - (A32(t, 0x70) + a->obj.radius) < TU_RANGE(a) << 4)
            {
                yaw = Math_Atan2(v[0], v[2]) & 0xfff;
                if (TU_BITS(a) & 8)
                {
                    /* cabeceo hacia el objetivo, limitado a +-0x200 */
                    int ax = Abs_(v[0]), az = Abs_(v[2]), hi = ax > az ? ax : az, lo = ax > az ? az : ax;
                    if ((hi >> 1) < lo)
                    {
                        int t2 = lo * 0x6a;
                        lo = hi - lo;
                        hi = (hi * 0x4b + t2) >> 7;
                    }
                    int p = -Math_Atan2(v[1], (hi + (lo >> 2)) - (lo >> 6));
                    pitch = p < -0x200 ? -0x200 : p > 0x200 ? 0x200 : p;
                }
            }
        }
    }
    if (TU_YAW(a) != yaw && (TU_BITS(a) & 0x40))
    {
        int d = (yaw - TU_BASE(a)) & 0xfff;
        if (d > 0x800)
            d -= 0x1000;
        if (d < -0x300)
            yaw = (TU_BASE(a) - 0x300) & 0xfff;
        else if (d > 0x300)
            yaw = (TU_BASE(a) + 0x300) & 0xfff;
    }
    if (TU_YAW(a) == yaw && TU_PITCH(a) == pitch)
        return;
    int step = (dt * TU_SPEED(a)) >> 2;
    if (TU_YAW(a) != yaw)
        TU_YAW(a) = Angle_Approach(TU_YAW(a), yaw, step);
    if (TU_PITCH(a) != pitch)
        TU_PITCH(a) = Angle_Approach(TU_PITCH(a), pitch, step);
    Math_RotMatrix(TU_YAW(a), TU_PITCH(a), 0, a->obj.rot);
    Actor_UpdateCenter(a);
    a->vel[0] = a->obj.rot[6];
    a->vel[1] = a->obj.rot[7];
    a->vel[2] = a->obj.rot[8];
}

/* 80034198 */
static int Turret_Handler(int msg, Actor* a, intptr_t p1, intptr_t p2)
{
    switch (msg)
    {
    case 1:
        return 0xf8;
    case 2:
        Turret_Create(a, (const ActorDef*)p1);
        return 0;
    case 6:
        Turret_Update(a, (int)p1);
        return 0;
    default:
        return Actor_DefaultHandler(msg, a, p1, p2);
    }
}

/* 8002ca3c: manejador de cada tipo de tACT (los de 1 y 0x11, coches, no llegan por aqui) */
static ActorFn Actor_HandlerFor(int type)
{
    switch (type)
    {
    case 0x06: return Turret_Handler;   /* 80034198 */
    case 0x0b: return Prop_Handler;     /* 800343dc */
    case 0x16: return Pickup_Handler;   /* 8003203c */
    default:   return NULL;             /* 5 80034e6c, 0xE 80034894 (8 y 0xD los crean los poderes) */
    }
}

/* bloque [tag][tamano][id]... del Cact con ese tag y el id del asset (800187d0 / 80018838) */
static uint8_t* Cact_FindBlock(NrAsset* as, uint32_t tag)
{
    uint8_t* d = as->data;
    for (uint8_t* c = d; c + 12 <= d + as->size;)
    {
        uint32_t len = NR_Read32(c + 4);
        if (NR_Read32(c) == tag && (int)NR_Read32(c + 8) == as->id)
            return c;
        if (len < 8)
            break;
        c += len;
    }
    return NULL;
}

/* 8004942c: tACT tipo 0x55 = salida; el resto crea la entidad con su lista de recursos aRSL (en el sitio:
 * +8 numero de entradas y {tipo, id} -> {tipo, NrAsset*}) */
int Cact_OnLoaded(NrAsset* as)
{
    uint8_t* tb = Cact_FindBlock(as, NR_TAG('t', 'A', 'C', 'T'));
    Asset_Register(as);
    if (!tb)
        return 1;
    const uint8_t* def = tb + 8;
    int type = def[4];
    if (type == 0x55)
    {
        int pos[3] = { (int)NR_Read32(def + 8), (int)NR_Read32(def + 0xc), (int)NR_Read32(def + 0x10) };
        Race_SetStart(pos);
        return 1;
    }
    if (type == 0x54)
    {
        TvCam_AddFromDef(def);          /* Emitter_OnLoaded 80072208: camara de TV */
        return 1;
    }
    ActorFn fn = Actor_HandlerFor(type);
    if (!fn)
        return 1;                       /* los tipos 5 y 0xE: pendientes */
    uint8_t* rb = Cact_FindBlock(as, NR_TAG('a', 'R', 'S', 'L'));
    uint32_t* res = NULL;
    if (rb)
    {
        res = (uint32_t*)(rb + 8);
        res[0] = (NR_Read32(rb + 4) - 0xc) >> 3;
        for (uint32_t i = 0; i < res[0]; i++)
            if (res[2 + i * 2] != 0)
                res[2 + i * 2] = (uint32_t)(uintptr_t)Asset_Find(res[1 + i * 2], (int)res[2 + i * 2]);
    }
    ActorDef d = { fn, res, def, as };
    Actor_Create(&d);
    return 1;
}

/* Solo en el port: resumen de lo creado (con NR_POWERLOG, tambien donde esta cada actor) */
void Actors_LogSummary(void)
{
    int count[256] = { 0 }, total = 0;
    for (Actor* a = g_Actors; a; a = a->next)
    {
        count[a->type]++;
        total++;
    }
    NR_Log("Actores: %d (objetos 0x0B: %d, giratorios 0x06: %d, cajas de poder 0x16: %d)\n", total, count[0x0b],
           count[0x06], count[0x16]);
    for (Actor* a = g_Actors; a; a = a->next)
        if (getenv("NR_POWERLOG"))
            NR_Log("  tipo 0x%02x id %d clase 0x%x (banderas %x, masa %d) vida %d: curva %d segmento %d, altura %d, modelo %s\n", a->type, a->id, a->objClass, a->objClass && Fx_ObjClass(a->objClass) ? NR_Read32(Fx_ObjClass(a->objClass) + 8) : 0, a->objClass && Fx_ObjClass(a->objClass) ? Fx_ObjClass(a->objClass)[1] : -1, a->hp, a->tpos.curve,
                   a->tpos.seg, a->obj.pos[1], a->obj.model ? "si" : "no");
}

/* ---------------------------------------------------------------- por frame */

/* 80048dec(6): actualiza las vivas */
void Actors_Update(int dt)
{
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->handle > 0)
            a->handler(6, a, dt, 0);
    Actors_Reap();
}

/* 80048e84 (tras la simulacion): las que tienen la vida en negativo reciben el mensaje 4 (morir). Se liberan
 * en el siguiente Actors_Update, asi lo que las apunta (el aviso de clima del marcador) sigue valido este frame. */
void Actors_PostUpdate(void)
{
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->handle > 0 && a->hp < 0)
            a->handler(4, a, 0, 0);
}

/* 80061a0c + 80061b28: las de un segmento visible (o con 0x10000) se dibujan con el mensaje 3 (max 0xC0) */
void Actors_AddToDrawList(int view)
{
    int n = 0;
    for (Actor* a = g_Actors; a; a = a->next)
    {
        int vis = 0;
        if (a->handle > 0 && a->tpos.seg != 0xff &&
            (Track_SegFlags(view, a->tpos.seg) || (a->flags2c & 0x10000)) && n < 0xc0)
        {
            a->handler(3, a, 0, 0);
            n++;
            vis = 1;
        }
        a->visible = (uint8_t)vis;
    }
}

/* 80061808 / 80061608 / 800613b4 (coche contra actor): misma celda de TCRV (curva >> 2) o la siguiente,
 * alguno con 0x2200, el que tenga 0x800000 pregunta con el mensaje 10 (los coches dicen que si) y, si se
 * tocan, mensaje 0xB al actor (al coche tambien en el original: cuerpos sueltos, pendiente). */
void Actors_CollideCars(void)
{
    int cells = g_Track && g_Track->numCurve > 0 ? (g_Track->numCurve >> 2) + 1 : 1;
    for (Actor* a = g_Actors; a; a = a->next)
    {
        if (a->handle <= 0 || a->tpos.seg == 0xff || (a->flags2c & 4))
            continue;
        int ca = a->tpos.curve >> 2;
        for (int i = 0; i < g_NumCars && a->handle > 0; i++)
        {
            RaceCar* c = &g_Cars[i];
            if (!c->phys || c->tpos.seg == 0xff)
                continue;
            int dc = Abs_(ca - (c->tpos.curve >> 2));
            if (dc > 1 && dc != cells - 1)
                continue;
            uint32_t f = a->flags2c | c->flags2c;
            if (!(f & 0x2200))
                continue;
            if ((a->flags2c & 0x800000) && !a->handler(10, a, (intptr_t)c, 0))
                continue;
            if (Ent_Near(&c->obj, c->flags2c, &a->obj, a->flags2c))
            {
                Body_CarHitsActor(c, a);   /* mensaje 0xB del coche: 8005cdb8 */
                a->handler(0xb, a, (intptr_t)c, 1);
            }
        }
    }
}
