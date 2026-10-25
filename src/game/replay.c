/* Repeticion de la carrera (ver replay.h). La simulacion es determinista con la misma semilla (800182ec), los mismos
 * mandos y el mismo dt por frame, asi que basta con grabar eso: cada humano tiene una lista de tramos de 4 bytes
 * (frames, eje analogico, botones) y hay otra de tramos de 2 bytes con el dt (frames, tics). */
#include <string.h>
#include "nr.h"
#include "globals.h"
#include "frontend.h"
#include "racecar.h"
#include "carphys_int.h"
#include "replay.h"

/* Tramo de los mandos de un humano: frames, eje del mando analogico (ctrl +0xC) y botones del juego con el tipo de
 * mando (ctrl +0xE) en los bits 13-15 */
typedef struct
{
    uint8_t  count;
    uint8_t  analog;
    uint16_t bits;
} PadRun;

/* Tramo del dt: frames y tics */
typedef struct
{
    uint8_t count;
    uint8_t dt;
} DtRun;

#define RP_PAD_RUNS 0x1800   /* 0x6000 bytes por humano */
#define RP_DT_RUNS  0x2000   /* 0x4000 bytes */

/* DAT_800af330: cabecera de 0x28 bytes y las listas. El primer tramo de cada lista es un marcador (count 0xFF, nunca
 * se alarga) y la lista acaba en un tramo con count 0. */
static struct
{
    int8_t humans;        /* +0x00 */
    int8_t racingClass;   /* +0x01 */
    int    ended;         /* +0x02 */
    int    seed;          /* +0x04 */
    int    padLeft[2];    /* +0x08 frames que quedan del tramo actual */
    int    dtLeft;        /* +0x0C */
    int    pad[2];        /* +0x18 tramo actual de cada humano */
    int    dt;            /* +0x24 tramo actual del dt */
    int    closed;        /* solo en el port: ya tiene los finales (el original puede ponerlos dos veces) */
    PadRun pads[2][RP_PAD_RUNS];
    DtRun  dts[RP_DT_RUNS];
} s_rp;

/* bit de la grabacion <-> bit de accion del controlador (ctrl +8): izquierda, derecha, acelerar, frenar, freno de
 * mano, volver a la pista y poder (R2). Los de la marcha manual (0x40 / 0x80) solo con cambio manual (+0x428 == 0). */
static const uint32_t s_padMap[7][2] = {
    { 0x1, 0x10000000 }, { 0x2, 0x20000000 }, { 0x4, 0x4000000 },  { 0x8, 0x400000 },
    { 0x10, 0x1000000 }, { 0x20, 0x8000000 }, { 0x100, 0x200000 },
};

static int Replay_CountHumans(void)
{
    int n = 0;
    for (int i = 0; i < g_RaceGrid.count && i < 8; i++)
        if (g_RaceGrid.car[i].player != 0xff)
            n++;
    return n > 2 ? 2 : n;
}

static uint16_t Replay_PackPad(const uint8_t* ctrl)
{
    uint32_t b = U32(ctrl, 8);
    unsigned v = 0;
    for (int i = 0; i < 7; i++)
        if (b & s_padMap[i][1])
            v |= s_padMap[i][0];
    if (I32(PTR(ctrl, 0), 0x428) == 0)
    {
        if (b & 0x100000)
            v |= 0x40;
        if (b & 0x2000000)
            v |= 0x80;
    }
    return (uint16_t)(v | (unsigned)U8(ctrl, 0xe) << 13);
}

/* los bits que no se graban (START, SELECT, abajo...) siguen saliendo del mando */
static void Replay_UnpackPad(uint8_t* ctrl, unsigned v, int analog)
{
    uint32_t a = U32(ctrl, 8) & 0x821fffff;
    for (int i = 0; i < 7; i++)
        if (v & s_padMap[i][0])
            a |= s_padMap[i][1];
    if (I32(PTR(ctrl, 0), 0x428) == 0)
    {
        a &= 0xfdefffff;
        if (v & 0x40)
            a |= 0x100000;
        if (v & 0x80)
            a |= 0x2000000;
    }
    U32(ctrl, 8) = a;
    U8(ctrl, 0xe) = (uint8_t)(v >> 13);
    if (U8(ctrl, 0xe) != 4)
        U8(ctrl, 0xc) = (uint8_t)analog;
}

int Replay_Begin(int seed)
{
    int humans = Replay_CountHumans();
    if (humans == 0)
    {
        g_GameInfo->isReplay = 0;
        return seed;
    }
    s_rp.ended = 0;
    if (!g_GameInfo->isReplay)
    {
        s_rp.humans = (int8_t)humans;
        s_rp.seed = seed;
        s_rp.racingClass = (int8_t)g_GameInfo->racingClass;
        s_rp.closed = 0;
        for (int h = 0; h < 2; h++)
        {
            s_rp.pad[h] = 0;
            s_rp.pads[h][0].count = 0xff;
        }
        s_rp.dt = 0;
        s_rp.dts[0].count = 0xff;
        return seed;
    }
    g_GameInfo->racingClass = s_rp.racingClass;
    for (int h = 0; h < s_rp.humans; h++)
    {
        s_rp.pad[h] = 1;
        s_rp.padLeft[h] = s_rp.pads[h][1].count;
    }
    s_rp.dt = 1;
    s_rp.dtLeft = s_rp.dts[1].count;
    return s_rp.seed;
}

/* Un tramo se alarga si el valor no cambia (y no ha llegado a 255 frames); si no, empieza otro. Port: si alguna
 * lista no tiene sitio no se graba el frame en ninguna (el original cerraba la que se llenaba y aun escribia el dt
 * detras del final) */
void Replay_RecordFrame(int dt)
{
    if (s_rp.ended || s_rp.closed)
        return;
    uint16_t bits[2] = { 0, 0 };
    uint8_t  analog[2] = { 0, 0 };
    int      same[2] = { 0, 0 }, full = 0;
    for (int h = 0; h < s_rp.humans; h++)
    {
        const RaceCar* e = g_Humans[h];
        if (e && e->ctrl)
        {
            bits[h] = Replay_PackPad(e->ctrl);
            analog[h] = U8(e->ctrl, 0xc);
        }
        const PadRun* r = &s_rp.pads[h][s_rp.pad[h]];
        same[h] = r->bits == bits[h] && ((bits[h] >> 13) == 4 || r->analog == analog[h]) && r->count != 0xff;
        if (!same[h] && s_rp.pad[h] >= RP_PAD_RUNS - 2)
            full = 1;
    }
    const DtRun* d = &s_rp.dts[s_rp.dt];
    int sameDt = d->dt == (uint8_t)dt && d->count != 0xff;
    if (!sameDt && s_rp.dt >= RP_DT_RUNS - 2)
        full = 1;
    if (full)
    {
        s_rp.ended = 1;
        Replay_End();
        return;
    }
    for (int h = 0; h < s_rp.humans; h++)
    {
        if (same[h])
        {
            s_rp.pads[h][s_rp.pad[h]].count++;
            continue;
        }
        PadRun* r = &s_rp.pads[h][++s_rp.pad[h]];
        r->count = 1;
        r->bits = bits[h];
        r->analog = (bits[h] >> 13) != 4 ? analog[h] : 0;
    }
    if (sameDt)
        s_rp.dts[s_rp.dt].count++;
    else
    {
        DtRun* n = &s_rp.dts[++s_rp.dt];
        n->count = 1;
        n->dt = (uint8_t)dt;
    }
}

int Replay_PlayFrame(void)
{
    int ended = s_rp.ended;
    for (int h = 0; h < s_rp.humans; h++)
    {
        unsigned v = 0;
        int analog = 0;
        if (!ended)
        {
            const PadRun* r = &s_rp.pads[h][s_rp.pad[h]];
            if (s_rp.padLeft[h] == 0)
            {
                if (r[1].count == 0)
                    s_rp.ended = ended = 1;
                else
                {
                    r = &s_rp.pads[h][++s_rp.pad[h]];
                    s_rp.padLeft[h] = r->count;
                }
            }
            if (!ended)
            {
                s_rp.padLeft[h]--;
                v = r->bits;
                analog = r->analog;
            }
        }
        RaceCar* e = g_Humans[h];
        if (e && e->ctrl)
            Replay_UnpackPad(e->ctrl, v, ended ? 0x80 : analog);
    }
    if (ended)
        return -2;
    if (s_rp.dtLeft == 0)
    {
        s_rp.dtLeft = s_rp.dts[++s_rp.dt].count;
        if (s_rp.dtLeft == 0)
        {
            s_rp.ended = 1;
            return -2;
        }
    }
    s_rp.dtLeft--;
    return s_rp.dts[s_rp.dt].dt;
}

void Replay_End(void)
{
    if (g_GameInfo->isReplay || s_rp.humans == 0 || s_rp.closed)
        return;
    for (int h = 0; h < s_rp.humans; h++)
        s_rp.pads[h][++s_rp.pad[h]].count = 0;
    s_rp.dts[++s_rp.dt].count = 0;
    s_rp.closed = 1;
}
