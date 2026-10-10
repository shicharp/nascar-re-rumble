/* Objetos sueltos: los actores (conos, vallas, barriles...) reciben un cuerpo con la fisica de los coches al primer
 * golpe y salen despedidos.
 * Original:
 *  - 8005cd50 (desde Entity_CollideBodies 8005cdb8): cuerpo nuevo (8004de5c, 0x1B4 bytes, DAT_800af310 cuenta) con
 *    Car_InitBody 8004e1b4 y masa = clase de objeto +1 * 0x1A90.
 *  - Entity_CollideBodies 8005cdb8 (mensaje 0xB de la entidad, Entity_OnCollide 8002c6a0): un coche contra un
 *    actor cuya clase lo permite (bandera 4 con el humano, 8 con la IA) crea el cuerpo y resuelve el choque con
 *    Car_CollidePair 8005c05c (sin fisica de coche ni ruedas para el actor). Del lado del actor el choque no se
 *    resuelve otra vez (los coches tienen clase 0) y solo le quita vida (Entity_OnCollide).
 *  - Body_Step 8005610c (mensaje 6 de Entity_DefaultHandler 8002c894): el paso de la fisica de los coches sin
 *    motor ni ruedas: velocidad local, Car_Integrate, giro, suelo (Car_ClampGround), esquinas de la caja con muros
 *    y suelo, contactos y respuesta (Car_HandleContacts / Car_Collide). Cada golpe le da 3000 tics (+0x17C); al
 *    acabarse se duerme (+0x198 & 0x20) y el cuerpo se libera (8004de8c), quedandose el objeto donde cayo. Fuera
 *    de la pista muere (vida -1).
 *  - 8005bebc: al golpearlo (clase & 0x70) sale despedido hacia arriba con giro al azar, mas segun la clase (0x20,
 *    0x40) y menos en los golpes flojos. */
#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "track.h"
#include "vecmath.h"
#include "racecar.h"
#include "actor.h"
#include "carphys.h"
#include "carphys_int.h"
#include "collide.h"
#include "snd.h"
#include "fx.h"
#include "body.h"

void Car_Integrate(uint8_t* c, int dt, int limit, int fromStep);
void Car_ClampGround(uint8_t* c, int ground, int dt, int fromStep);
void Car_RotateMatrix(int* m, const int* w);
void Car_BoxCorners(uint8_t* c);
void Car_Collide(uint8_t* c, uint8_t* rec, int dt, int ground, int self);
void Car_HandleContacts(uint8_t* c, int dt, uint8_t* self, int fromStep);

static int s_bodies;   /* DAT_800af310 */

/* Solo en el port (depuracion): NR_BODYLOG=1 apunta los golpes a los actores, los cuerpos y su final */
static int Body_LogOn(void)
{
    static int s_on = -1;
    if (s_on < 0)
        s_on = getenv("NR_BODYLOG") != NULL;
    return s_on;
}

static uint32_t Cls_Flags(const uint8_t* cl)
{
    return cl ? NR_Read32(cl + 8) : 0;
}

static const uint8_t* Body_Class(const uint8_t* b)
{
    const RaceCar* e = (const RaceCar*)ENT(b);
    return e && e->objUnder ? Fx_ObjClass(e->objUnder) : NULL;
}

/* 8005cd50 / 8004de5c */
uint8_t* Body_Ensure(Actor* a, int mass)
{
    if (!a->body)
    {
        uint8_t* b = (uint8_t*)calloc(1, 0x6a8);   /* (0x1B4 en el original; el resto queda a cero) */
        if (!b)
            return NULL;
        Car_InitBody((RaceCar*)a, b, mass);
        a->body = b;
        s_bodies++;
    }
    return (uint8_t*)a->body;
}

/* 8004de8c: fuera el roce y el cuerpo */
void Body_Free(Actor* a)
{
    uint8_t* b = (uint8_t*)a->body;
    if (!b)
        return;
    if (IA(b, 0x5e) != -1 && a->handle)
        Snd_SourceKill(a->handle, IA(b, 0x5e));
    free(b);
    a->body = NULL;
    s_bodies--;
}

int Body_Count(void)
{
    return s_bodies;
}

/* 8005bebc */
void Body_Fling(uint8_t* b, int speed)
{
    if (speed < 0x10)
        return;
    uint32_t f = Cls_Flags(Body_Class(b));
    int up, base, mask;
    if (f & 0x20)
    {
        up = 0x4000;
        base = 0x2000;
        mask = 0xbfff;
    }
    else if (f & 0x40)
    {
        up = 0x8000;
        base = 0x4000;
        mask = 0x17fff;
    }
    else
    {
        up = 0x2000;
        base = 0x1000;
        mask = 0x5fff;
    }
    if (speed < 0x20)
    {
        up >>= 2;
        base >>= 2;
        mask >>= 2;
    }
    else if (speed < 0x40)
    {
        up >>= 1;
        base >>= 1;
        mask >>= 1;
    }
    /* velocidad al mundo, hacia arriba 'up' y otra vez a ejes del objeto; giro al azar en los tres ejes */
    Car_VelToWorld(b, VEC(b, 0x18c));
    IA(b, 100) = up;
    Mat_Mul(((RaceCar*)ENT(b))->obj.rot, VEC(b, 0x18c), VEC(b, 0x154));
    IA(b, 0x52) += base - (int)((uint32_t)Rand_A() & (uint32_t)mask);
    IA(b, 0x53) += base - (int)((uint32_t)Rand_A() & (uint32_t)mask);
    IA(b, 0x54) += base - (int)((uint32_t)Rand_A() & (uint32_t)mask);
}

/* Entity_CollideBodies 8005cdb8 (coche contra actor, mensaje 0xB del coche) */
int Body_CarHitsActor(RaceCar* car, Actor* a)
{
    uint8_t* cp = car->phys;
    const uint8_t* cl = a->objClass ? Fx_ObjClass(a->objClass) : NULL;
    if (Body_LogOn())
        NR_Log("[cuerpo] t=%d coche %d toca el actor %d (tipo 0x%x, clase 0x%x, banderas %x, masa %d, vida %d, %s)\n",
               g_RaceTime, car->id - 1000, a->id, a->type, a->objClass, Cls_Flags(cl), cl ? cl[1] : -1, a->hp,
               a->body ? "con cuerpo" : "sin cuerpo");
    if (!cp || !cl || !(Cls_Flags(cl) & (car->type == 1 ? 4u : 8u)))
        return 0;
    uint8_t* b = Body_Ensure(a, cl[1] * 0x1a90);
    if (!b)
        return 0;
    return Body_CollidePair(cp, 1, b, 0);
}

/* 8005610c: 1 si el cuerpo se ha dormido (el llamador lo libera) */
int Body_Step(uint8_t* b, int dt)
{
    RaceCar* e = (RaceCar*)ENT(b);
    if (I16(b, 0x17c) != 0)
    {
        if (I16(b, 0x17c) < dt)
        {
            I16(b, 0x17c) = 0;
            U16(b, 0x198) |= 0x20;
        }
        else
            I16(b, 0x17c) = (int16_t)(I16(b, 0x17c) - dt);
    }
    if (U16(b, 0x198) & 0x20)
    {
        if (Body_LogOn())
            NR_Log("[cuerpo] t=%d el actor %d se duerme en (%d, %d, %d)\n", g_RaceTime, e->id, e->obj.pos[0],
                   e->obj.pos[1], e->obj.pos[2]);
        return 1;
    }
    /* velocidad en ejes del objeto (+0x1A8 en 8.8 y +0x18C) */
    IA(b, 0x6a) = IA(b, 0x55) << 8;
    IA(b, 0x6b) = IA(b, 0x56) << 8;
    IA(b, 0x6c) = IA(b, 0x57) << 8;
    int m[9];
    for (int k = 0; k < 9; k++)
        m[k] = e->obj.rot[k] << 6;
    Mat_MulT(m, VEC(b, 0x1a8), VEC(b, 0x1a8));
    IA(b, 99) = IA(b, 0x6a) >> 14;
    IA(b, 0x65) = IA(b, 0x6c) >> 14;
    IA(b, 100) = IA(b, 0x6b) >> 14;
    Car_Integrate(b, dt, 0xff, 0);
    const uint8_t* cl = Body_Class(b);
    if (cl && cl[0] == 10)
    {
        /* clase de tipo 10: se frena (1/32 por paso) */
        IA(b, 99) += M16(IA(b, 99), -0x800);
        IA(b, 100) += M16(IA(b, 100), -0x800);
        IA(b, 0x65) += M16(IA(b, 0x65), -0x800);
    }
    int w[3];
    Mat_MulT(e->obj.rot, VEC(b, 0x148), w);
    for (int k = 0; k < 3; k++)
        w[k] = w[k] * dt >> 8;
    Car_RotateMatrix(e->obj.rot, w);
    Mat_Mul(e->obj.rot, VEC(b, 0x1a8), VEC(b, 0x154));
    IA(b, 0x55) >>= 0xe;
    IA(b, 0x57) >>= 0xe;
    IA(b, 0x56) >>= 0xe;
    int g = Track_FindSurface(&e->tpos, e->obj.pos, VEC(b, 0x160), 1);
    if (g != TRACK_NO_SURFACE)
    {
        e->obj.shadowY = (short)(g >> 4);
        e->obj.shadowN[0] = (short)(IA(b, 0x58) >> 4);
        e->obj.shadowN[1] = (short)(IA(b, 0x59) >> 4);
        e->obj.shadowN[2] = (short)(IA(b, 0x5a) >> 4);
        Car_ClampGround(b, g, dt, 0);
    }
    else
    {
        e->obj.shadowY = 0;
        e->obj.shadowN[0] = 0;
        e->obj.shadowN[1] = 0x1000;
        e->obj.shadowN[2] = 0;
    }
    const uint8_t* face = NULL;
    U8(b, 0x181) = (uint8_t)Track_SurfaceType(&e->tpos, &face);
    I16(b, 0x146) = (int16_t)(I16(b, 0x146) + dt);
    U16(b, 0x18a) = face ? NR_Read16(face + 8) : 0;
    Car_BoxCorners(b);
    Car_WallContactsOne(b);
    int heights[8], normals[8 * 3];
    for (int i = 0; i < I16(b, 0x144) && i < 8; i++)
        heights[i] = Track_FindSurface((TrackPos*)(B_(b) + 4 + i * 4), VEC(b, 0x84 + i * 0xc), normals + i * 3, 0);
    Car_GroundContactsOne(b, heights, normals);
    if (I16(b, 0xec) != 0)
    {
        U8(b, 0x180) |= 1;
        Contact_Average(B_(b) + 0xec);
    }
    if (I16(b, 0x10c) != 0)
    {
        U8(b, 0x180) |= 2;
        Contact_Average(B_(b) + 0x10c);
    }
    Car_HandleContacts(b, dt, b, 0);
    if (U8(b, 0x180) & 1)
        Car_Collide(b, B_(b) + 0xec, dt, 0, 0);
    else if (U8(b, 0x180) & 2)
        Car_Collide(b, B_(b) + 0x10c, dt, 1, 0);
    int g2 = Track_FindSurface(&e->tpos, e->obj.pos, VEC(b, 0x160), 1);
    if (g2 < 0)
    {
        /* fuera de la pista (y ya lo estaba al principio del paso): muere */
        if (g == TRACK_NO_SURFACE)
        {
            e->flags2c &= 0xfeffff7f;
            e->unk20 = -1;
            if (Body_LogOn())
                NR_Log("[cuerpo] t=%d el actor %d cae fuera de la pista\n", g_RaceTime, e->id);
        }
        U8(b, 0x180) &= 0xf7;
    }
    else
        U8(b, 0x180) |= 8;
    I16(b, 0x1a6) = (int16_t)(dt << 6);
    if (Abs_(IA(b, 0x52)) > 0x18000 || Abs_(IA(b, 0x53)) > 0x18000 || Abs_(IA(b, 0x54)) > 0x18000)
    {
        Vec_Normalize(VEC(b, 0x148));
        Vec_Scale(0x18000, VEC(b, 0x148));
    }
    Actor_UpdateCenter((Actor*)e);
    return 0;
}
