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
#include "script.h"
#include "snd.h"
#include "globals.h"
#include "vehicle.h"

static const uint8_t* s_pool;     /* DAT_800af6d8: RPNS de la pista (guiones de las entidades) */
static int            s_poolSize;
static int            s_nextNear; /* DAT_800af874 */

Actor* g_Actors;            /* DAT_800af87c */
static Actor* s_tail;       /* DAT_800af880 */
static int    s_nextHandle; /* DAT_800af870 */

static void Csacs_OnFreed(const Actor* a);
static int  s_numCsac;      /* generadores Csac (mas abajo) */

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
void Actor_RemoveCmd(Actor* a, int i)
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
void Actor_FindTrackPos(Actor* a)
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
    /* asset +0x24..+0x2C: guiones (golpe, muerte, cercania) en el RPNS de la pista (Asset_Dispatch les suma su base) */
    for (int k = 0; k < 3; k++)
        a->script[k] = d->asset && s_pool && d->asset->scr[k] < (uint32_t)s_poolSize ? (void*)(s_pool + d->asset->scr[k]) : NULL;
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
        /* 800138f0: con 0x80000 tiene sombra aunque el modelo no la traiga (cuadrada) */
        if ((a->flags2c & 0x80000) && a->obj.model)
        {
            if (!a->obj.model->shadow)
                a->obj.model->shadow = Model_MakeShadowQuad(a->obj.model);
            if (a->obj.model->shadow)
                a->obj.flags |= 0x10;
        }
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
        /* 8002c594: muere una vez (0x1000000 la protege) con el efecto de su clase de objeto (800354d4) */
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
         * el dano (el +0x1E del otro: 1 para los coches), la bandera 0x2000000 y el guion de golpe (80067890) */
        const RaceCar* o = (const RaceCar*)p1;
        int dmg = o ? (o->type == 1 || o->type == 0x11 ? 1 : o->unk1e) : 0;
        if (!(a->flags2c & 0x80) && dmg > 0)
        {
            a->hp = (int16_t)(a->hp - dmg);
            a->flags2c |= 0x2000000;
            if (a->script[0])   /* guion de golpe: argumento 1 si la mata, valor inicial el id del otro */
                Script_Run((const uint8_t*)a->script[0], a->hp < 0, o ? (int)(o->id & 0x7fff) : 0, a);
        }
        return 0;
    }
    case 0xe:
    {
        /* dano directo (orden 0x3C): {id, dano} */
        const DamageMsg* m = (const DamageMsg*)p1;
        if (m && !(a->flags2c & 0x80) && m->dmg > 0)
        {
            a->hp = (int16_t)(a->hp - m->dmg);
            a->flags2c |= 0x2000000;
            if (a->script[0])
                Script_Run((const uint8_t*)a->script[0], a->hp < 0, (int)(m->id & 0x7fff), a);
        }
        return 0;
    }
    case 7:
    case 8:
    case 9:
    case 0xc:
    case 0xd:
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

/* Entity_Kill 80049688: el guion de muerte (+0xD4; argumento 1 si muere sola, 0 si la mata una orden: 0x4000000;
 * 0x8000000 sin guion), fuera su sonido de ambiente, y se libera en Actors_Reap */
void Actor_Destroy(Actor* a)
{
    if (!a->handle)
        return;
    if (!(a->flags2c & 0x8000000) && a->script[1])
        Script_Run((const uint8_t*)a->script[1], (int)((a->flags2c >> 26 ^ 1) & 1), -1, a);
    if (a->flags2c & 0x400000)
        Snd_SourceKill(a->handle, (int8_t)a->unk27);
    a->handle = 0;
}

/* ---------------------------------------------------------------- guiones de las entidades */

/* 80049628 */
int Actors_OnScriptPool(NrAsset* as)
{
    Asset_Register(as);
    s_pool = as->data;
    s_poolSize = as->size;
    return 1;
}

/* Cola de reintento de las ordenes (DAT_800b5980, mascara DAT_800af878): 16 huecos {orden, argumento, 600 tics,
 * entidad, su numero}. El original guarda el puntero y mira si su numero sigue siendo el mismo; el port busca la
 * entidad por el numero (la memoria de una liberada no se puede leer). */
typedef struct
{
    uint8_t cmd, arg;
    int16_t timer;
    int     handle;
} CmdRetry;

static CmdRetry s_retry[16];
static uint32_t s_retryMask;

/* las entidades vivas con ese numero: actores y coches (en el original estan en la misma lista) */
uint8_t* Ent_ByHandle(int handle)
{
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->handle == handle)
            return (uint8_t*)a;
    for (int i = 0; i < g_NumCars; i++)
        if (g_Cars[i].handle == handle)
            return (uint8_t*)&g_Cars[i];
    return NULL;
}

/* 80049788: la orden entra en el primer hueco libre de la cola (+0x14, comun a actores y coches). Llena, con
 * 'retry' va a la cola de reintento (8004971c), que la vuelve a intentar cada 600 tics. */
static int Ent_PushCmd(uint8_t* e, int cmd, int arg, int retry)
{
    uint8_t* q = e + 0x14;
    for (int i = 0; i < 4; i++)
        if (q[i * 2] == 0)
        {
            q[i * 2] = (uint8_t)cmd;
            q[i * 2 + 1] = (uint8_t)arg;
            return 1;
        }
    if (!retry || s_retryMask == 0xffff)
        return 0;
    int k = 0;
    while (s_retryMask & (1u << k))
        k++;
    s_retry[k].cmd = (uint8_t)cmd;
    s_retry[k].arg = (uint8_t)arg;
    s_retry[k].timer = 600;
    s_retry[k].handle = *(const int*)(e + 0xc);
    s_retryMask |= 1u << k;
    if (Script_LogOn())
        NR_Log("[guion] cola llena en la entidad %d: orden 0x%x %d al reintento %d\n", NR_Read16(e + 0x1c), cmd, arg, k);
    return 1;
}

/* 800497f4 (Race_UpdateTime): reintenta cada 600 tics; suelta el hueco si entra o si la entidad ya no existe */
void Actors_RetryOrders(int dt)
{
    for (int k = 0; k < 16; k++)
    {
        if (!(s_retryMask & (1u << k)))
            continue;
        CmdRetry* r = &s_retry[k];
        r->timer = (int16_t)(r->timer - dt);
        if (r->timer < 1)
        {
            r->timer = (int16_t)(r->timer + 600);
            uint8_t* e = Ent_ByHandle(r->handle);
            if (!e || Ent_PushCmd(e, r->cmd, r->arg, 0))
                s_retryMask ^= 1u << k;
        }
    }
}

int Actor_PushCmd(Actor* a, int cmd, int arg)
{
    return Ent_PushCmd((uint8_t*)a, cmd, arg, 1);
}

/* 800498e4 (0x69: 8003938c, vacia; 0x7E: indice de la tabla de los Cfun 80046ab4, pero ningun fichero del disco
 * trae assets Cfun ni hay guiones que la usen: no hace nada) */
void Actors_CmdById(int id, int cmd, int arg)
{
    if (cmd == 0x69 || cmd == 0x7e)
        return;
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->id == (uint16_t)id && a->handle)
            Ent_PushCmd((uint8_t*)a, cmd, arg, 1);
    for (int i = 0; i < g_NumCars; i++)
        if (g_Cars[i].id == (uint16_t)id && g_Cars[i].handle)
            Ent_PushCmd((uint8_t*)&g_Cars[i], cmd, arg, 1);
}

/* 800499b4 (los coches: grupo 0xFF los humanos, 0 la IA) */
void Actors_CmdByGroup(int group, int cmd, int arg)
{
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->unk23 == (uint8_t)group && a->handle)
            Ent_PushCmd((uint8_t*)a, cmd, arg, 1);
    for (int i = 0; i < g_NumCars; i++)
        if (g_Cars[i].unk23 == (uint8_t)group && g_Cars[i].handle)
            Ent_PushCmd((uint8_t*)&g_Cars[i], cmd, arg, 1);
}

/* 80049a40 */
void Actors_CmdByTeam(int team, int cmd, int arg)
{
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->team == (uint8_t)team && a->handle)
            Ent_PushCmd((uint8_t*)a, cmd, arg, 1);
    for (int i = 0; i < g_NumCars; i++)
        if (g_Cars[i].team == (uint8_t)team && g_Cars[i].handle)
            Ent_PushCmd((uint8_t*)&g_Cars[i], cmd, arg, 1);
}

/* Solo en el port (depuracion): NR_KILLTEST=id rompe el actor con ese id a los 5 s de carrera: un golpe del coche
 * humano con toda su vida (mensaje 0xE: su guion de golpe) y despues muere (mensaje 4: su guion de muerte) */
static void Actors_KillTest(void)
{
    static int s_id = -2, s_done;
    if (s_id == -2)
        s_id = getenv("NR_KILLTEST") ? atoi(getenv("NR_KILLTEST")) : -1;
    if (s_id < 0 || s_done || g_RaceTime < 1500)
        return;
    s_done = 1;
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->handle > 0 && a->id == s_id)
        {
            NR_Log("[guion] prueba: muere el actor %d (tipo 0x%02x)\n", a->id, a->type);
            DamageMsg m = { g_Humans[0] ? (uint32_t)g_Humans[0]->id : 0u, (int16_t)(a->hp >= 0 ? a->hp + 1 : 1), 1, 0, 0 };
            a->handler(0xe, a, (intptr_t)&m, 0);
            if (a->hp >= 0)
                a->hp = -1;   /* (invulnerable: muere igual) */
        }
}

/* Solo en el port (depuracion): NR_TRIGTEST=1 hace que el coche humano toque cada disparador a los 5 s de carrera
 * (los mismos mensajes 10 y 0xB que el choque) */
static void Actors_TriggerTest(void)
{
    static int s_on = -1, s_done;
    if (s_on < 0)
        s_on = getenv("NR_TRIGTEST") != NULL;
    if (!s_on || s_done || g_RaceTime < 1500 || !g_Humans[0])
        return;
    s_done = 1;
    for (Actor* a = g_Actors; a; a = a->next)
        if (a->handle > 0 && a->type == 0x0e)
        {
            int ok = a->handler(10, a, (intptr_t)g_Humans[0], 0);
            NR_Log("[guion] prueba: disparador %d filtro %d valor %d -> %d\n", a->id, A8(a, 0xe1), A16(a, 0xe2), ok);
            if (ok)
                a->handler(0xb, a, (intptr_t)g_Humans[0], 1);
        }
}

/* 80049b74: las entidades a menos de 0x22000 del coche de la vista suenan (sonido de ambiente +0x26 en la
 * fuente +0x27); cada 300 tics la mas cercana con 0x100000 (y tipo distinto de 0x24) ejecuta su guion +0xD8
 * (80049b2c). Se borra la marca de golpe 0x2000000 */
void Actors_Activate(void)
{
    Actors_TriggerTest();
    Actors_KillTest();
    RaceCar* t = RaceCam_GetTarget(0);
    if (!t)
        return;
    int tick = 0;
    if (s_nextNear <= g_RaceTime)
    {
        tick = 1;
        s_nextNear = g_RaceTime + 300;
    }
    Actor* best = NULL;
    int bestD = 0x40000000;
    for (Actor* a = g_Actors; a; a = a->next)
    {
        if (a->handle <= 0)
            continue;
        int dx = Abs_(a->obj.pos[0] - t->obj.pos[0]), dz = Abs_(a->obj.pos[2] - t->obj.pos[2]);
        int d = dx + dz - (dz < dx ? dz : dx) / 2;
        uint32_t f = a->flags2c;
        a->flags2c = (f & 0xfdffffff) | 0x20000000;
        if (d < 0x22000)
        {
            if (tick && (f & 0x100000) && a->type != 0x24 && d < bestD)
            {
                bestD = d;
                best = a;
            }
            if (a->unk26)
            {
                if (!(a->flags2c & 0x400000))
                {
                    int src = Snd_SourceCreate(a->unk26, a->handle, a->obj.pos[0], a->obj.pos[2], -1, 0, NULL);
                    a->unk27 = (uint8_t)src;
                    if (src >= 0)
                        a->flags2c |= 0x400000;
                }
                else
                    Snd_SourceMove(a->handle, a->obj.pos[0], a->obj.pos[2], -1, 0, NULL, (int8_t)a->unk27);
            }
        }
        else if (a->flags2c & 0x400000)
        {
            Snd_SourceKill(a->handle, (int8_t)a->unk27);
            a->flags2c &= 0xffbfffff;
        }
    }
    if (best && best->script[2] && *(const uint8_t*)best->script[2])
        Script_Run((const uint8_t*)best->script[2], best->hp < 0, -1, best);
}

/* 80048f78: ordenes genericas (0x38..0x65) de la cola; las demas son del manejador de cada tipo */
void Actors_RunOrders(void)
{
    for (Actor* a = g_Actors; a; a = a->next)
    {
        if (a->handle <= 0)
            continue;
        for (int i = 0; i < 4 && a->cmds[i * 2];)
        {
            int c = a->cmds[i * 2], arg = a->cmds[i * 2 + 1];
            switch (c)
            {
            case 0x38: case 0x3b: case 0x40: case 0x41: case 0x42: case 0x43: case 0x45:
                break;
            case 0x39:
                /* otro sonido de ambiente */
                if (a->flags2c & 0x400000)
                {
                    Snd_SourceKill(a->handle, (int8_t)a->unk27);
                    a->flags2c &= 0xffbfffff;
                }
                a->unk26 = (uint8_t)arg;
                break;
            case 0x3a:
                a->unk1e = (int16_t)arg;   /* dano que hace */
                break;
            case 0x3c:
            {
                /* danarse (0 / 1: toda la vida) */
                a->flags2c &= 0xfeffff7f;
                DamageMsg m = { 0, (int16_t)(arg >= 2 ? arg : (a->hp >= 0 ? a->hp + 1 : 0)), 1, 0, 0 };
                if (m.dmg > 0)
                    a->handler(0xe, a, (intptr_t)&m, 0);
                break;
            }
            case 0x3d:
                /* morir (1: sin guion de muerte) */
                a->flags2c |= arg == 1 ? 0xc000000 : 0x4000000;
                Actor_Destroy(a);
                break;
            case 0x3f:
                if (arg == 0)
                    a->flags2c &= 0xfeffff7f;
                else
                    a->flags2c |= 0x1000080;   /* invulnerable */
                break;
            case 100:
                Fx_Spawn(a, arg, NULL, 0, NULL);   /* 80039330 -> 80039a80 */
                break;
            case 0x65:
                Fx_Stop(a, arg);                   /* 800391b0 */
                break;
            default:
                i++;
                continue;
            }
            Actor_RemoveCmd(a, i);
        }
    }
}

/* ---------------------------------------------------------------- disparadores (tipo 0xE) */

/* Campos (0xE8 bytes): +0xE0 banderas (1 mata al que entra, 2 no desaparece al tocarlo (guion +0xD0), 8 cambia la
 * ruta del vehiculo, 0x10 apagado, 0x20 al encenderse ejecuta +0xD8), +0xE1 filtro, +0xE2 valor, +0xE4 tiempo */
#define TR_FLAGS(a) A8(a, 0xe0)
#define TR_KIND(a)  A8(a, 0xe1)
#define TR_VALUE(a) A16(a, 0xe2)
#define TR_TIME(a)  A32(a, 0xe4)

/* 80034548: zona de radio tACT +0x24, filtro +0x29 / +0x2A, banderas +0x28, modo de suelo +0x26 */
static int Trigger_Create(Actor* a, const ActorDef* d)
{
    Actor_DefaultHandler(2, a, (intptr_t)d, 0);
    const uint8_t* t = d->def;
    int16_t r = *(const int16_t*)(t + 0x24);
    a->obj.radius = r << 4;
    a->obj.bb10 = a->obj.bb16 = r;
    TR_KIND(a) = t[0x29];
    TR_VALUE(a) = *(const int16_t*)(t + 0x2a);
    TR_FLAGS(a) = t[0x28];
    TR_TIME(a) = 0;
    if (a->flags2c & 0x80000)
        a->obj.flags &= 0xef;
    int mode = *(const int16_t*)(t + 0x26);
    int g = mode == 0 ? -1 : mode == 1 ? -2 : mode - 2;
    int y = Track_GroundHeight(&a->tpos, a->obj.pos, g);
    a->obj.unk34[1] = y;
    a->obj.pos[1] = y;
    a->flags2c |= 0xa00011;
    if (TR_FLAGS(a) & 8)
        TR_KIND(a) = 7;
    return 0;
}

/* 80034438: el filtro del disparador (1 tipo, 2 id, 3 grupo +0x23, 4 equipo, 5 equipo en mascara, 7 vehiculo con
 * ruta, 8 tipo con 0x12 como 1) sobre una entidad (coche o actor: los campos +0x1C..+0x24 coinciden) */
static int Trigger_Match(int kind, int value, const uint8_t* e)
{
    int v;
    switch (kind)
    {
    case 1: v = e[0x22]; break;
    case 2:
        if (e[0x22] == 0x15)
            return 0;
        v = NR_Read16(e + 0x1c);
        break;
    case 3: v = e[0x23]; break;
    case 4: v = e[0x24]; break;
    case 5:
        v = e[0x24];
        value &= 1 << (v & 31);
        break;
    case 7:
        return 0;   /* entidades de tipo 2 con ruta (+0x108): no hay manejador de ese tipo en este juego */
    case 8:
        v = e[0x22] == 0x12 ? 1 : e[0x22];
        break;
    default:
        v = 0;   /* (registro sin iniciar en el original) */
        break;
    }
    return v == (int16_t)value;
}

/* 80034660: ordenes 0x78 valor, 0x79 filtro, 0x7A encender / apagar */
static void Trigger_Orders(Actor* a)
{
    for (int i = 0; i < 4 && a->cmds[i * 2];)
    {
        int c = a->cmds[i * 2], arg = a->cmds[i * 2 + 1];
        if (c == 0x79)
            TR_KIND(a) = (uint8_t)arg;
        else if (c == 0x78)
            TR_VALUE(a) = (int16_t)arg;
        else if (c == 0x7a)
        {
            if (arg == 0)
                TR_FLAGS(a) |= 0x10;
            else
            {
                uint8_t f = TR_FLAGS(a);
                TR_FLAGS(a) = f & 0xef;
                if ((f & 0x20) && a->script[2] && *(const uint8_t*)a->script[2])
                    Script_Run((const uint8_t*)a->script[2], 0, -1, a);
            }
        }
        else
        {
            i++;
            continue;
        }
        Actor_RemoveCmd(a, i);
    }
}

/* 8003477c: algo entro en la zona */
static int Trigger_Hit(Actor* a, const uint8_t* other)
{
    TR_TIME(a) = g_RaceTime;
    int otherType = other[0x22];
    /* 8 (cambio de ruta: el filtro 7 solo acepta entidades de tipo 2, que este juego no crea) no llega a pasar */
    if ((TR_FLAGS(a) & 1) && otherType != 1 && otherType != 0x11)
    {
        Actor* o = (Actor*)other;
        o->flags2c |= 0x4000000;
        Actor_Destroy(o);
    }
    if (!(TR_FLAGS(a) & 2))
        Actor_Destroy(a);   /* de un solo uso: su guion de muerte */
    else if (a->script[0])
        Script_Run((const uint8_t*)a->script[0], 0, NR_Read16(other + 0x1c) & 0x7fff, a);
    return 0;
}

/* 80034894 */
static int Trigger_Handler(int msg, Actor* a, intptr_t p1, intptr_t p2)
{
    switch (msg)
    {
    case 1:
        return 0xe8;
    case 2:
        return Trigger_Create(a, (const ActorDef*)p1);
    case 6:
        if (a->cmds[0])
            Trigger_Orders(a);
        return 0;
    case 10:
        if (!(TR_FLAGS(a) & 0x10))
            return Trigger_Match(TR_KIND(a), TR_VALUE(a), (const uint8_t*)p1);
        return 0;
    case 3: case 4: case 5: case 7: case 8: case 9: case 0xc: case 0xd:
        return 0;
    case 0xb:
        return Trigger_Hit(a, (const uint8_t*)p1);
    default:
        return Actor_DefaultHandler(msg, a, p1, p2);
    }
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
            if (a->flags2c & 0x10000000)
                Csacs_OnFreed(a);
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
    s_numCsac = 0;                  /* 800605a4 / 800605cc */
    s_retryMask = 0;                /* 80048d28 */
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
    case 0x0e: return Trigger_Handler;  /* 80034894 */
    case 0x05: return Vehicle_Handler;  /* 80034e6c (vehicle.c) */
    default:   return NULL;             /* (8 y 0xD los crean los poderes) */
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

/* aRSL del asset resuelta en el sitio: +8 numero de entradas y {tipo, id} -> {tipo, NrAsset*} */
static const uint32_t* Cact_ResolveRes(NrAsset* as)
{
    uint8_t* rb = Cact_FindBlock(as, NR_TAG('a', 'R', 'S', 'L'));
    if (!rb)
        return NULL;
    uint32_t* res = (uint32_t*)(rb + 8);
    res[0] = (NR_Read32(rb + 4) - 0xc) >> 3;
    for (uint32_t i = 0; i < res[0]; i++)
        if (res[2 + i * 2] != 0)
            res[2 + i * 2] = (uint32_t)(uintptr_t)Asset_Find(res[1 + i * 2], (int)res[2 + i * 2]);
    return res;
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
        return 1;
    ActorDef d = { fn, Cact_ResolveRes(as), def, as };
    Actor_Create(&d);
    return 1;
}

/* ---------------------------------------------------------------- generadores Csac */

/* Bloque tSAC (en el sitio): +0xC periodo en tics (el fichero lo da / 5; negativo: el primer intervalo es el periodo
 * y no uno al azar), +0xE maximo de entidades vivas, +0x10 banderas (1 apagado; bits 8-11 y 12-15 intentos pedidos
 * por la orden 0x47), +0x12 las que quedan (0 agotado, negativo sin limite), +0x14 cuenta atras, +0x16 vivas.
 * El original guarda {manejador, aRSL, tACT, asset} en +0x20..+0x2C; el port, en Csac. Hay 21 en las pistas
 * (BL2, BL3, MC2, MG3, SE1, SE3): vehiculos de tipo 5 que salen cada pocos segundos por una red y desaparecen al
 * final, y disparadores. La condicion de la cabecera (en MG3, segun el ajuste de poderes) se vuelve a mirar en
 * cada intento. */
typedef struct
{
    NrAsset* as;
    uint8_t* sac;
    ActorDef def;
} Csac;

#define CSAC_MAX 0x80   /* Pool_Alloc(0x204) de 800605a4 */
static Csac s_csac[CSAC_MAX];   /* DAT_800af8d0 */

#define SAC16(c, o) (*(int16_t*)((c)->sac + (o)))
#define SACU16(c, o) (*(uint16_t*)((c)->sac + (o)))

/* 800605f0 */
int Csac_OnLoaded(NrAsset* as)
{
    Asset_Register(as);
    if (s_numCsac >= CSAC_MAX)
        return 1;
    uint8_t* sac = Cact_FindBlock(as, NR_TAG('t', 'S', 'A', 'C'));
    uint8_t* tb = Cact_FindBlock(as, NR_TAG('t', 'A', 'C', 'T'));
    if (!sac || !tb)
        return 1;
    Csac* c = &s_csac[s_numCsac++];
    c->as = as;
    c->sac = sac;
    int16_t p = (int16_t)(SAC16(c, 0xc) * 5);
    SAC16(c, 0x16) = 0;
    SAC16(c, 0xc) = p;
    if (p < 0)
        SAC16(c, 0xc) = SAC16(c, 0x14) = (int16_t)-p;
    else
        SAC16(c, 0x14) = p ? (int16_t)((uint32_t)Rand_A() % (uint32_t)p) : 0;
    c->def.handler = Actor_HandlerFor(tb[8 + 4]);   /* (sin manejador en el port: no crea nada) */
    c->def.res = Cact_ResolveRes(as);
    c->def.def = tb + 8;
    c->def.asset = as;
    return 1;
}

/* 80060800: si se cumple la condicion de la cabecera, crea la entidad (marcada 0x10000000) y ejecuta el segundo
 * guion */
static void Csac_Spawn(Csac* c)
{
    const uint8_t* scr = c->as->hdrScr;
    if (!Script_Run(scr, 0, -1, NULL))
        return;
    if (SAC16(c, 0x12) > 0)
        SAC16(c, 0x12)--;
    SAC16(c, 0x16)++;
    if (c->def.handler)
    {
        Actor* a = Actor_Create(&c->def);   /* 800492c0 */
        if (a)
            a->flags2c |= 0x10000000;
        if (Script_LogOn())
            NR_Log("[csac] %d crea la entidad %d (tipo 0x%02x), vivas %d\n", c->as->id, a ? a->handle : 0, c->def.def[4],
                   SAC16(c, 0x16));
    }
    Script_Run(scr ? scr + c->as->hdrLen1 : NULL, 0, -1, NULL);
}

/* 80060a60: los intentos pedidos y, encendido y por debajo del maximo, uno cada periodo */
void Csacs_Update(int dt)
{
    for (int i = 0; i < s_numCsac; i++)
    {
        Csac* c = &s_csac[i];
        for (int n = SACU16(c, 0x10) >> 8 & 0xf; n > 0; n--)
            Csac_Spawn(c);
        for (int n = SACU16(c, 0x10) >> 12; n > 0; n--)
            Csac_Spawn(c);
        uint16_t fl = SACU16(c, 0x10);
        SACU16(c, 0x10) = fl & 0xff;
        if (SAC16(c, 0x16) < SAC16(c, 0xe) && SAC16(c, 0x12) != 0 && !(fl & 1))
        {
            SAC16(c, 0x14) = (int16_t)(SACU16(c, 0x14) - dt);
            if (SAC16(c, 0x14) < 0)
            {
                SAC16(c, 0x14) = (int16_t)(SAC16(c, 0x14) + SAC16(c, 0xc));
                Csac_Spawn(c);
            }
        }
    }
}

/* 8006087c: ordenes 0x46 (1 encender y crear ya, 0 apagar) y 0x47 (crear una: 1 / 0 dos contadores distintos que
 * hacen lo mismo) */
static void Csac_Command(Csac* c, int cmd, int arg)
{
    uint16_t fl = SACU16(c, 0x10);
    if (cmd == 0x46)
    {
        if (arg != 0)
        {
            if (!(fl & 1))
                return;
            SACU16(c, 0x10) = fl & 0xfffe;
            SAC16(c, 0x14) = 0;
            return;
        }
        SACU16(c, 0x10) = fl | 1;
    }
    else if (cmd == 0x47)
    {
        if (SAC16(c, 0x16) >= SAC16(c, 0xe) || SAC16(c, 0x12) == 0)
            return;
        if (arg != 0)
            SACU16(c, 0x10) = (uint16_t)((fl & 0xf0ff) | (((fl >> 8 & 0xf) + 1) << 8));
        else
            SACU16(c, 0x10) = (uint16_t)((fl & 0xfff) | ((fl & 0xf000) + 0x1000));
    }
}

/* 8006094c: el primero cuyo tACT tenga ese id */
void Csacs_CmdById(int id, int cmd, int arg)
{
    for (int i = 0; i < s_numCsac; i++)
        if (NR_Read32(s_csac[i].def.def) == (uint32_t)id)
        {
            Csac_Command(&s_csac[i], cmd, arg);
            return;
        }
}

/* 800609c0: todos los del grupo (tACT +0x1D) */
void Csacs_CmdByGroup(int group, int cmd, int arg)
{
    for (int i = 0; i < s_numCsac; i++)
        if (s_csac[i].def.def[0x1d] == (uint32_t)group)
            Csac_Command(&s_csac[i], cmd, arg);
}

/* 8006079c (desde Entity_Reap 80049360): muere una entidad creada por un generador */
static void Csacs_OnFreed(const Actor* a)
{
    for (int i = 0; i < s_numCsac; i++)
        if (NR_Read32(s_csac[i].def.def) == a->id)
            SAC16(&s_csac[i], 0x16)--;
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
 * tocan, mensaje 0xB al coche (Body_CarHitsActor: cuerpos sueltos) y al actor. */
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
