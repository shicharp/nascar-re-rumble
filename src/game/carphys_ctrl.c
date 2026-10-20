#include "nr.h"
#include "pad.h"
#include "carphys.h"
#include "carphys_int.h"
#include "snd.h"
#include "hud.h"
#include "cnet.h"
#include "actor.h"
#include "racecar.h"

/* ---------------------------------------------------------------- controles del coche */

/* 80059870: freno (+0x424) y su modo en +0x3F8 (0x80 freno de mano, 0x100 freno, 0x200) */
void Car_Brake(uint8_t* c, uint32_t mode, int amount)
{
    if (!(U32(c, 0x3f8) & 0x40) || U8(c, 0x638) == 0xf)
    {
        I32(c, 0x424) = M16(I32(c, 0x130) << 1, (int32_t)((uint32_t)amount << 16) >> 12);
        U32(c, 0x3f8) = (U32(c, 0x3f8) & 0xfffffc7f) | mode;
    }
}

/* 8004d93c: par del motor en el regimen actual (+0x3EC) segun la curva de la tabla de marchas */
int Car_EngineTorque(uint8_t* c)
{
    uint8_t* t = PTR(c, 1000);
    int redline = I16(t, 4);
    int u = (I32(c, 0x3ec) >> 8) - redline;
    int r = ((u >> 31) & u) + redline;
    if (r < 0)
        r += 0xf;
    int idx = r >> 4;
    int cur = I16(t, idx * 2 + 0x2c);
    int u2 = idx - 0xe;
    int nidx = ((u2 >> 31) & u2) + 0xf;
    int next = I16(t, nidx * 2 + 0x2c);
    int frac;
    if (I32(c, 0x428) == 0 || (frac = next - cur, I8(c, 0x3f4) == 0))
    {
        if (redline < 0)
            redline += 0xf;
        if (I8(c, 0x3f4) == 0)
        {
            cur -= 100;
            next -= 100;
            if (cur < 0)
                cur = 0;
            if (next < 0)
                next = 0;
        }
        if (nidx < (redline >> 4) + 1)
        {
            frac = next - cur;
            if (nidx < (redline >> 4))
                goto done;
            frac = next >> 1;
        }
        else
        {
            frac = 0;
            cur >>= 1;
        }
        frac -= cur;
    }
done:
    if (frac < 0)
        frac += 0xf;
    return (int16_t)(cur + (frac >> 4));
}

/* 800598dc: acelerador (+0x420) */
void Car_Throttle(uint8_t* c, int amount)
{
    if ((U32(c, 0x3f8) & 0x100000) || I16(c, 0x184) != 0 || ((U32(c, 0x3f8) & 0x40) && U8(c, 0x638) != 0xf))
        return;
    int tq = (int16_t)Car_EngineTorque(c);
    int16_t a = (int16_t)amount;
    if (U16(c, 0x33a) & 1)
    {
        int t = a * 0xc00;
        if (t < 0)
            t += 0xfff;
        a = (int16_t)(a - (int16_t)(t >> 12));
    }
    if (U16(c, 0x33a) & 0x30)
    {
        int t = a;
        if (t < 0)
            t += 3;
        a = (int16_t)(a - (int16_t)(t >> 2));
    }
    if (U16(c, 0x33a) & 0x240)
    {
        int t = a;
        if (t < 0)
            t += 3;
        a = (int16_t)(a + (int16_t)(t >> 2));
    }
    if (U8(c, 0x431) != 0xcc)
        tq = (tq >> 1) + (int)((uint32_t)(tq >> 1) * (uint32_t)U8(c, 0x431)) / 0xcc;
    I32(c, 0x420) = (tq * a) >> 4;
}

/* 8005967c: direccion. mode 0 suma 'amount' al angulo (+0x2D8), 1 analogico, 2 centrar, 3/4 a cada lado;
 * calcula el angulo maximo (+0x2DA) segun la velocidad */
void Car_Steer(uint8_t* c, int mode, int amount)
{
    if ((U16(c, 0x33a) & 2) || ((U32(c, 0x3f8) & 0x40) && U8(c, 0x638) == 0))
        return;
    uint16_t amt = (uint16_t)amount;
    I32(c, 0x42c) = mode;
    int sp = 0;
    if (mode == 1)
    {
        if (U8(c, 0x4a0))
        {
            amt = (uint16_t)(U8(c, 0x4a0) << 7);
            if (U8(c, 0x4a0) > 0x20)
                amt = 0x1000;
            if (U32(c, 0x3f8) & 0x200000)
                amt = (uint16_t)-(int16_t)amt;
        }
        I32(c, 0x42c) = (int16_t)amt <= I16(c, 0x2d8) ? 3 : 4;
        if ((int16_t)amt < 0)
            amt = (uint16_t)-(int16_t)amt;
    }
    else
    {
        if (mode == 0)
        {
            U16(c, 0x2d8) = (uint16_t)(U16(c, 0x2d8) + amt);
            U16(c, 0x2da) = U16(c, 0x312);
            return;
        }
        if (U8(c, 0x4a0))
            I32(c, 0x42c) = (U32(c, 0x3f8) & 0x200000) ? 4 : 3;
        amt = U16(c, 0x312);
    }
    if (I16(c, 0x2b4) < 0x81)
    {
        int u = Abs_(I32(c, 0x15c)) - 0x6000;
        sp = ((u >> 31) & u) + 0x6000;
    }
    int s = (int16_t)amt;
    I16(c, 0x2da) = (int16_t)((s >> 3) + (int16_t)((uint32_t)((s - (s >> 3)) * ((0x6000 - sp) / 0x60)) >> 8));
}

/* 8004ff70: velocidad maxima de la marcha actual */
int Car_TopSpeed(uint8_t* c)
{
    uint8_t* t = PTR(c, 1000);
    int g;
    if (I32(c, 0x428) == 0)
    {
        if ((U16(c, 0x198) & 0x10) || I8(c, 0x3f4) < 0 || I8(c, 0x3f4) == 4)
            return I32(t, 0);
        g = I8(c, 0x3f4);
    }
    else
    {
        g = 0;
        if (I8(c, 0x3f4) != 0)
            return I32(t, 0);
    }
    return I16(t, g * 2 + 0x22);
}

/* 8004f63c */
void Car_ClearFlags(uint8_t* c, uint32_t bits)
{
    U32(c, 0x3f8) &= ~bits;
}

/* 80059a50 / 80059aa4: cambio manual (+0x3F4: -1 marcha atras, 0 punto muerto, 1..n) */
void Car_ShiftUp(uint8_t* c)
{
    if (I32(c, 0x428) != 0)
        return;
    int g = I8(c, 0x3f4);
    int8_t n;
    if (g < 0)
        n = 1;
    else if (g == 0)
        n = -1;
    else
    {
        n = (int8_t)(g + 1);
        if (g >= I16(PTR(c, 1000), 0xc) - 1)
            return;
    }
    I8(c, 0x3f4) = n;
}

void Car_ShiftDown(uint8_t* c)
{
    if (I32(c, 0x428) != 0)
        return;
    int8_t g = I8(c, 0x3f4);
    if (g >= 2)
        I8(c, 0x3f4) = (int8_t)(g - 1);
    else if (g == 1)
        I8(c, 0x3f4) = -1;
    else if (g == -1)
        I8(c, 0x3f4) = 0;
}

/* ---------------------------------------------------------------- mando del jugador */

/* 8001cedc: asignacion por defecto (boton del mando -> bit del juego) */
static const uint32_t s_defaultBinds[14][2] = {
    { PADB_UP, 0x40000000 },       { PADB_DOWN, 0x80000000u },   { PADB_LEFT, 0x10000000 },
    { PADB_RIGHT, 0x20000000 },    { PADB_SQUARE, 0x8000000 },   { PADB_CROSS, 0x4000000 },
    { PADB_CIRCLE, 0x2000000 },    { PADB_TRIANGLE, 0x1000000 }, { PADB_R1, 0x800000 },
    { PADB_L1, 0x400000 },         { PADB_L2, 0x100000 },        { PADB_R2, 0x200000 },
    { PADB_SELECT, 0x40000 },      { PADB_START, 0x80000 },
};

/* 8005f928: bits del juego -> bits de accion del controlador */
static uint32_t Pad_ActionBits(uint32_t g)
{
    static const uint32_t map[][2] = {
        { 0x80000, 0x80000 },       { 0x40000, 0x40000 },     { 0x40000000, 0x40000000 },
        { 0x80000000u, 0x80000000u }, { 0x10000000, 0x10000000 }, { 0x20000000, 0x20000000 },
        { 0x8000000, 0x4000000 },   { 0x4000000, 0x400000 },  { 0x2000000, 0x1000000 },
        { 0x1000000, 0x8000000 },   { 0x400000, 0x2000000 },  { 0x800000, 0x800000 },
        { 0x100000, 0x100000 },     { 0x200000, 0x200000 },
    };
    uint32_t a = 0;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (g & map[i][0])
            a |= map[i][1];
    return a;
}

/* 8005fbfc: estado de un boton asignado {mascara, estado}: 2 suelto, 0 recien pulsado, 3 pulsado,
 * 1 recien soltado */
static void Pad_Edge(uint32_t bits, uint8_t* bind)
{
    uint32_t* st = (uint32_t*)(bind + 4);
    if (!(bits & U32(bind, 0)))
    {
        if (*st == 3)
            *st = 1;
        else if (*st != 2)
            *st = 2;
    }
    else if (*st == 2)
        *st = 0;
    else
        *st = 3;
}

/* 8005fed8: ordenes de los guiones al coche humano (grupo 0xFF): 0x15 / 0x14 apagan / encienden el nodo arg de la
 * red de la salida (DAT_800af7d4; bit 1: la IA solo toma esos nodos con habilidad > 0x37). Las mandan los guiones
 * de casi todas las pistas, p. ej. al romper un obstaculo que abre un atajo. */
static void Human_Orders(RaceCar* e)
{
    const uint8_t* net = g_NumNets > 0 ? g_Nets[0] : NULL;
    for (int i = 0; i < 4;)
    {
        int c = e->pad14[i * 2], arg = e->pad14[i * 2 + 1];
        if (c == 0)
            return;
        if (c != 0x14 && c != 0x15)
        {
            i++;
            continue;
        }
        if (net && arg < Cnet_Count(net))
        {
            uint8_t* n = (uint8_t*)net + 4 + arg * 12;
            n[0] = (uint8_t)(c == 0x15 ? n[0] | 2 : n[0] & 0xfd);
        }
        Actor_RemoveCmd((Actor*)e, i);   /* (la cola esta en +0x14 tambien en los coches) */
    }
}

/* Humans_Update 8005ffbc (parte): tiempo con un poder sin usar (+0x68, para el aviso del marcador), R2 (+0x34,
 * usar el poder) y arriba en la cruceta (+0x4C, la bocina 80041be8(5)) */
void Human_UpdateButtons(RaceCar* e, int dt)
{
    uint8_t* ctrl = e->ctrl;
    if (I32(ctrl, 0x68) >= 0)
        I32(ctrl, 0x68) += dt;
    if (e->pad14[0])
        Human_Orders(e);   /* 8005fed8 */
    Pad_Edge(U32(ctrl, 8), B_(ctrl) + 0x34);
    Pad_Edge(U32(ctrl, 8), B_(ctrl) + 0x4c);
    if (I32(ctrl, 0x50) == 0 && I32(PTR(ctrl, 0), 0x330) == 0)
        Car_PlaySfx(5, e);   /* 80041be8: bocina */
}

/* Lectura del mando (Race_UpdateTime: 8005fc54 + 8005f928 para cada humano). Mando digital (tipo 4). */
void Human_ReadPad(RaceCar* e, int port)
{
    uint8_t* ctrl = e->ctrl;
    uint32_t raw = g_PadButtons[port & 1], g = 0;
    for (int i = 0; i < 14; i++)
        if (raw & s_defaultBinds[i][0])
            g |= s_defaultBinds[i][1];
    U32(ctrl, 8) = Pad_ActionBits(g);
    U8(ctrl, 0xe) = 4;
}

/* 8004c9d0: acelerador / freno (con el cambio automatico, el freno parado engrana la marcha atras) */
static void Human_Pedals(uint8_t* ctrl)
{
    uint8_t* c = PTR(ctrl, 0);
    uint32_t b = U32(ctrl, 8);
    if (I32(c, 0x428) == 0)
    {
        if (!(b & 0x400000))
        {
            if (b & 0x4000000)
                Car_Brake(c, 0x100, 0x1000);
            goto handbrake;
        }
    }
    else if (!(b & 0x400000))
    {
        if (!(b & 0x4000000))
            goto handbrake;
        if (I8(c, 0x3f4) == -1)
            I8(c, 0x3f4) = 0;
        else if (I8(c, 0x3f4) != 0)
        {
            Car_Brake(c, 0x100, 0x1000);
            goto handbrake;
        }
    }
    else if (I8(c, 0x3f4) == -1)
        I8(c, 0x3f4) = 1;
    else if (I8(c, 0x3f4) == 0)
    {
        Car_Brake(c, 0x100, 0x1000);
        goto handbrake;
    }
    Car_Throttle(c, 0x1000);
handbrake:
    if (b & 0x1000000)
        Car_Brake(c, 0x80, 0x1000);
}

/* 8004cae8: direccion, pedales, cambio manual (L1 / L2) y triangulo = volver a la pista */
void Human_Control(RaceCar* e)
{
    static uint8_t s_shiftHeld;   /* DAT_800af30c */
    uint8_t* ctrl = e->ctrl;
    uint8_t* c = PTR(ctrl, 0);
    if (I32(c, 0x330) != 0)
    {
        /* carrera terminada: conduce solo */
        U8(ctrl, 0xc) = 0xff;
        U32(ctrl, 8) = (U32(ctrl, 8) & 0xc6bfffff) | 0x21000000;
    }
    if (U32(c, 0x3f8) & 0x100000)
        Car_Brake(c, 0x100, 0x1000);
    int mode, amount = 0;
    if (U8(ctrl, 0xe) == 2 || U8(ctrl, 0xe) == 7)
    {
        int s = (U8(ctrl, 0xc) - 0x80) * 0x20;
        int v = (s * s) >> 12;
        if (s < 0)
            v = -v;
        mode = 1;
        amount = (v * I16(c, 0x312) * 0x10) >> 16;
    }
    else if (U32(ctrl, 8) & 0x20000000)
        mode = 4;
    else if (U32(ctrl, 8) & 0x10000000)
        mode = 3;
    else
        mode = 2;
    Car_Steer(c, mode, amount);
    Human_Pedals(ctrl);
    if (I32(c, 0x428) == 0)
    {
        if (U32(ctrl, 8) & 0x2000000)
        {
            if (!s_shiftHeld)
                Car_ShiftUp(c);
            s_shiftHeld = 1;
        }
        else if (U32(ctrl, 8) & 0x100000)
        {
            if (!s_shiftHeld)
                Car_ShiftDown(c);
            s_shiftHeld = 1;
        }
        else
            s_shiftHeld = 0;
    }
    Pad_Edge(U32(ctrl, 8), B_(ctrl) + 0x2c);
    if (I32(ctrl, 0x30) == 0 && g_RaceTime > 900 && !(U16(c, 0x33a) & 0x82))
    {
        Speech_Say(0xb, 5, e, 1);   /* vuelta a la pista */
        Car_Reset(e, 1);
    }
}

/* 8005f708 */
void Human_Buttons(RaceCar* e)
{
    uint8_t* ctrl = e->ctrl;
    uint8_t* c = PTR(ctrl, 0);
    uint32_t a = U32(ctrl, 8);
    if (I32(c, 0x428) != 0)
    {
        Pad_Edge(a, ctrl + 0x18);
        if (I32(ctrl, 0x1c) == 0)
            RaceCam_CycleView(ctrl[0x59], 1);
        Pad_Edge(a, ctrl + 0x20);
        if (I32(ctrl, 0x24) == 0)
            Hud_CycleLevel(e, 1);
    }
    Pad_Edge(a, ctrl + 0x10);
    if (I32(ctrl, 0x14) == 0)
        I32(ctrl, 0x54) = g_RaceTime;
    else if (I32(ctrl, 0x14) == 1)
    {
        if (g_RaceTime - I32(ctrl, 0x54) < 300)
            RaceCam_CycleView(ctrl[0x59], 1);
        else
            Hud_CycleLevel(e, 1);
    }
}
