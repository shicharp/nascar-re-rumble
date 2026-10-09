/* Sonido de los coches: motores, superficie, derrape y bocina.
 * Original: tipos de motor de la parrilla 80041e34 (80041dc4: tipo = tabla 800aab00 por equipo del piloto y
 * clase del coche), bancos de los Ceng (parte de 80022908; ids de banco 8003d128 / 8003d160), motor de cada coche
 * Engine_Create 80040258 (desde Car_InitPhys), cada paso de la fisica Engine_Update 80040600, voces y fuentes cada
 * frame Engine_UpdateAll 80040c58 (80040b08), parar 8004170c, soltar 800417ec, bocina Car_PlaySfx 80041be8 /
 * 80041b0c y la de la IA 80041c6c.
 * Registro del motor (DAT_800b2b50, 6 x 0x78). Sonidos: id 4 motor con gas (todos; la IA con una fuente
 * posicional), id 3 motor sin gas (los humanos, banco 8 / 9), id 5 bocina, 0xB derrape; superficie (tabla
 * 800aaa7c por tipo de superficie: bucle, golpe al pasar, derrapa). La muestra de cada coche es el patch n de la
 * definicion, n = orden en que se cargo su tipo. */
#include <string.h>
#include "nr.h"
#include "mem.h"
#include "exe.h"
#include "frontend.h"
#include "globals.h"
#include "racecar.h"
#include "carphys_int.h"
#include "snd.h"
#include "snd_int.h"

#define NUM_ENGINES 6

typedef struct
{
    uint8_t   used;         /* +0x00 */
    int8_t    player;       /* +0x01 -1 = IA */
    int8_t    gear;         /* +0x02 */
    uint8_t   shifting;     /* +0x03 1 subiendo, 2 bajando */
    int       srcAi;        /* +0x04 fuente de la IA */
    int       handle;       /* +0x08 entidad */
    int       shiftPitch;   /* +0x0C */
    int       shiftStep;    /* +0x10 */
    int       lastPitch;    /* +0x14 */
    int       pitch;        /* +0x18 */
    unsigned  shiftTime;    /* +0x1C */
    int       x, z;         /* +0x20 */
    int       vel[3];       /* +0x28 */
    int       speed;        /* +0x34 */
    SndVoice* voice[4];     /* +0x38 0 con gas, 1 sin gas, 2 el del otro jugador, 3 superficie */
    uint8_t   playing[4];   /* +0x48 */
    uint8_t   throttle;     /* +0x4C */
    uint8_t   inCar;        /* +0x4D camara dentro del coche: motor mas fuerte */
    int8_t    patchMain;    /* +0x4E */
    int8_t    patchExtra;   /* +0x4F */
    int       id;           /* +0x54 */
    uint8_t*  phys;         /* +0x58 */
    int       surface;      /* +0x5C sonido de la superficie */
    int       surfMark;     /* +0x60 distancia del ultimo golpe */
    int       scale;        /* +0x64 */
    int       srcSkid;      /* +0x68 */
    int       srcHorn;      /* +0x6C */
    int       hornTimer;    /* +0x70 */
    uint8_t   hornDelay;    /* +0x74 */
    uint8_t   skid;         /* +0x75 */
} Engine;

static Engine s_eng[NUM_ENGINES];
static int    s_engNext;                /* DAT_800af7f8 */
static int    s_engDefs[3];             /* DAT_800b2e20: definiciones de los ids 4, 3 y 5 */
static int    s_engIds[3] = { 4, 3, 5 };/* DAT_800af278 */
static int    s_engDefsInit;            /* DAT_800b2e26 */
static int    s_types[6][2];            /* DAT_800b2e50 {tipo, con banco extra} */
static int    s_numTypes;               /* DAT_800af7fc */
static int    s_mainBank = 2, s_extraBank = 8;   /* DAT_800af83b / 83c */

/* 8003cfd0 (parte): al cargar cada carrera */
void Engine_ResetTypes(void)
{
    s_mainBank = 2;
    s_extraBank = 8;
    s_numTypes = 0;
    s_engDefsInit = 0;
    for (int i = 0; i < NUM_ENGINES; i++)
        s_eng[i].used = 0;
    s_engNext = 0;
}

int Engine_NextMainBank(void)
{
    return s_mainBank < 8 ? s_mainBank++ : -1;
}

int Engine_NextExtraBank(void)
{
    return s_extraBank < 10 ? s_extraBank++ : -1;
}

/* 80041dc4: tipo de motor por el equipo del piloto y la clase del coche */
static int Engine_Type(int team, int cls)
{
    int idx = 0;
    if (team > 0x20)
    {
        idx = 1;
        if (team > 0x27)
        {
            idx = 2;
            if (team > 0x2e)
                idx = (unsigned)((team - 0x2c) & 0xff) < 0x12 ? team - 0x2c : 0;
        }
    }
    return Exe_S8(0x800aab00u + (uint32_t)(idx * 3 + cls));
}

static int Grid_EngineType(const GridCar* g)
{
    return Engine_Type(g_Drivers[g->driver].team, g->car);
}

/* 80041e34: bancos a cargar para un Ceng de ese tipo (0, 1 = el del motor, 2 = tambien el sin gas de un humano) */
int Engine_BanksForCar(int type)
{
    int same = 0;
    for (int i = 0; i < s_numTypes; i++)
        if (s_types[i][0] == type)
            same++;
    if (s_numTypes >= 6)
        return 0;
    int used = 0, human = 0, both = 0, firstHuman = 0;
    for (int i = 0; i < g_RaceGrid.count; i++)
    {
        const GridCar* g = &g_RaceGrid.car[i];
        if (Grid_EngineType(g) != type)
            continue;
        used = 1;
        if (g->player == 0xff)
            continue;
        human = 1;
        if (g_SplitScreen)
        {
            if (!firstHuman)
                firstHuman = type;
            else if (firstHuman == type)
                both = 1;
        }
    }
    int res = used + human;
    if (human)
    {
        if (!g_SplitScreen || !both)
        {
            if (same)
                res = used;
        }
        else if (same > 1)
            res = used;
    }
    if (res)
    {
        s_types[s_numTypes][0] = type;
        s_types[s_numTypes][1] = res >= 2;
        s_numTypes++;
    }
    return res;
}

static NrAsset* Snd_MakeAsset(uint32_t type, int id, const uint8_t* src, int size)
{
    NrAsset* a = (NrAsset*)Mem_Alloc(sizeof(NrAsset), 0);
    if (!a)
        return NULL;
    memset(a, 0, sizeof(*a));
    a->type = type;
    a->id = id;
    a->size = size;
    a->data = (uint8_t*)Pool_Alloc(size, 1);
    if (!a->data)
    {
        Mem_Free(a);
        return NULL;
    }
    memcpy(a->data, src, (size_t)size);
    return a;
}

/* 80022908 (Ceng): {u16 1, u8 1, u8 tipo, +8 offset de los datos, por banco {tamano de la cabecera, de las
 * muestras}}; cada banco se pasa como un Cvkh + Cvkb con el id de 8003d128 (y el segundo con el de 8003d160) */
int Engine_OnCeng(NrAsset* a)
{
    const uint8_t* d = a->data;
    if (a->size < 0x14 || *(const uint16_t*)d != 1 || d[2] != 1)
        return 0;
    int n = Engine_BanksForCar(d[3]);
    if (!n)
        return 0;
    int idA = Engine_NextMainBank(), idB = n == 2 ? Engine_NextExtraBank() : 1;
    if (idA == -1 || idB == -1)
        return 0;
    const uint8_t* src = d + *(const int32_t*)(d + 8);
    for (int k = 0; k < n; k++)
    {
        int hs = *(const int32_t*)(d + 0xc + k * 8), bs = *(const int32_t*)(d + 0x10 + k * 8);
        int id = k ? idB : idA;
        NrAsset* h = Snd_MakeAsset(NR_TAG('C', 'v', 'k', 'h'), id, src, hs);
        src += hs;
        NrAsset* b = h ? Snd_MakeAsset(NR_TAG('C', 'v', 'k', 'b'), id, src, bs) : NULL;
        src += bs;
        if (!b)
        {
            Asset_Free(h);
            break;
        }
        if (!Snd_OnAsset(h))
            Asset_Free(h);
        if (!Snd_OnAsset(b))
            Asset_Free(b);
    }
    return 0;
}

/* 8005e840: la camara del jugador va dentro del coche */
static int Engine_InCar(int player)
{
    return RaceCam_IsInCar(player);
}

static Engine* Engine_Of(const uint8_t* c)
{
    int i = U8(c, 0x430);
    return i < NUM_ENGINES && s_eng[i].used ? &s_eng[i] : NULL;
}

/* 80040600: posicion, tono (revoluciones, con la subida / bajada de marcha suavizada), fuente de la IA y derrape */
void Engine_Update(uint8_t* c)
{
    Engine* e = Engine_Of(c);
    if (!e)
        return;
    RaceCar* ent = (RaceCar*)ENT(c);
    e->x = ent->obj.pos[0];
    e->z = ent->obj.pos[2];
    memcpy(e->vel, ent->dir, sizeof(e->vel));
    e->speed = ent->speed;
    int pitch = IA(c, 0xfb), free = 1;
    if (e->player != -1)
    {
        int gear = (int8_t)IA(c, 0xfd), thr = IA(c, 0x108) != 0;
        if (gear != e->gear)
        {
            if (gear != -1)
            {
                if (e->gear < gear)
                {
                    e->shifting = 1;
                    e->shiftPitch = e->lastPitch + 0x855;
                    e->shiftStep = -((e->lastPitch + 0x855 - pitch) >> 2);
                }
                else
                {
                    e->shifting = 2;
                    e->shiftPitch = e->lastPitch - 0x855;
                    e->shiftStep = (pitch - (e->lastPitch - 0x855)) >> 2;
                }
                e->shiftTime = s_tick;
            }
            e->gear = (int8_t)gear;
        }
        if (e->shifting)
        {
            int el = (int)(s_tick - e->shiftTime);
            e->shiftTime = s_tick;
            if (el > 0x3c)
                el = 0x3c;
            while (el > 0xf)
            {
                el -= 0xf;
                e->shiftPitch += e->shiftStep;
            }
            e->shiftTime -= (unsigned)el;
            if (e->shifting == 1)
            {
                if (e->shiftPitch < pitch)
                {
                    e->shifting = 0;
                    e->shiftPitch = pitch;
                }
                else
                    thr = 0;
            }
            else if (pitch < e->shiftPitch)
            {
                e->shifting = 0;
                e->shiftPitch = pitch;
            }
            else
                thr = 1;
            pitch = e->shiftPitch;
            free = 0;
        }
        e->throttle = (uint8_t)thr;
    }
    if (free)
    {
        int d = e->lastPitch - pitch;
        if (d > 0x855 || d < -0x855)
            pitch = e->lastPitch <= pitch ? e->lastPitch + 0x855 : e->lastPitch - 0x855;
    }
    e->pitch = pitch < 0x1001 ? pitch : ((((pitch - 0x1000) >> 8) * e->scale) >> 8) + 0x1000;
    int clamped = e->pitch < 0x1000 ? 0x1000 : e->pitch > 0x74aa ? 0x74aa : e->pitch;
    e->pitch = clamped;
    if (e->player == -1 && e->patchMain != -1)
    {
        if (e->srcAi != -1)
            Snd_SourceMove(ent->handle, e->x, e->z, e->pitch, e->speed, e->vel, e->srcAi);
        else
        {
            e->srcAi = Snd_SourceCreate(s_engIds[0], ent->handle, e->x, e->z, clamped, e->speed, e->vel);
            if (e->srcAi != -1)
                Snd_SourceSetPatch(ent->handle, e->patchMain, e->srcAi);
        }
    }
    e->lastPitch = pitch;
    /* derrape (fuente 0xB con el volumen de la rueda que mas desliza) */
    int skidOn = 0;
    if ((IA(c, 0xfe) & 3) && pitch >= 0x1256)
    {
        int m = 0;
        for (int k = 0; k < 4; k++)
            if (U8(c, 0x4a8 + k * 100 + 4) > m)
                m = U8(c, 0x4a8 + k * 100 + 4);
        e->skid = (uint8_t)m;
        int surf = I8(c, 0x181);
        skidOn = (unsigned)surf < 0x12 ? Exe_S8(0x800aaa7eu + (uint32_t)surf * 3) : 0;
        if (g_RaceTime < 600)
            skidOn = 0;
        if (skidOn)
        {
            if (e->srcSkid == -1)
                e->srcSkid = Snd_SourceCreate(0xb, ent->handle, e->x, e->z, -1, e->speed, e->vel);
            else
                Snd_SourceMove(ent->handle, e->x, e->z, -1, e->speed, e->vel, e->srcSkid);
            if (e->srcSkid != -1)
                Snd_SourceSetVolume(ent->handle, e->srcSkid, e->skid * 0x60);
            return;
        }
    }
    if (e->srcSkid != -1)
    {
        Snd_SourceKill(e->handle, e->srcSkid);
        e->srcSkid = -1;
    }
}

/* 80040258 */
void Engine_Create(uint8_t* c)
{
    if (!s_engDefsInit)
    {
        for (int k = 0; k < 3; k++)
            s_engDefs[k] = Def_FromId(s_engIds[k]);
        s_engDefsInit = 1;
    }
    int idx = -1;
    for (int n = 0; n < NUM_ENGINES && idx == -1; n++)
    {
        if (!s_eng[s_engNext].used)
            idx = s_engNext;
        s_engNext = s_engNext + 1 == NUM_ENGINES ? 0 : s_engNext + 1;
    }
    U8(c, 0x430) = (uint8_t)idx;
    if (idx == -1)
        return;
    Engine* e = &s_eng[idx];
    RaceCar* ent = (RaceCar*)ENT(c);
    memset(e, 0, sizeof(*e));
    e->used = 1;
    e->srcAi = -1;
    e->id = ent->id;
    e->handle = ent->handle;
    e->gear = (int8_t)IA(c, 0xfd);
    e->phys = c;
    e->srcSkid = -1;
    e->srcHorn = -1;
    const GridCar* g = &g_RaceGrid.car[(ent->id - 1000) & 7];
    e->player = (int8_t)(g->player == 0xff ? -1 : g->player);
    if (e->player != -1)
    {
        e->voice[0] = Voice_Alloc(0x7f, 10, 2, 0xff);   /* 8003f524 */
        e->voice[1] = Voice_Alloc(0x7f, 10, 2, 0xff);
        e->voice[3] = Voice_Alloc(0x7f, 10, 2, 0xff);
        e->inCar = (uint8_t)Engine_InCar(e->player);
    }
    int type = Grid_EngineType(g), extra = 0, i;
    for (i = 0; i < s_numTypes; i++)
    {
        if (s_types[i][0] == type)
        {
            e->patchMain = (int8_t)i;
            e->patchExtra = (int8_t)(s_types[i][1] ? extra : 0);
            break;
        }
        if (s_types[i][1])
            extra++;
    }
    if (i == s_numTypes)
        e->patchMain = e->patchExtra = -1;
    /* escala del tono por encima de 0x1000 (por el margen de revoluciones de la tabla de marchas) */
    int d = I16(c, 0x3a0) - I16(c, 0x3a2);
    if (d)
    {
        uint32_t q = 0x80000000u / (uint32_t)((d < 0 ? -d : d) << 15);
        int s = (int)(((uint64_t)q * 0x64aa00u) >> 16);
        e->scale = d < 0 ? -s : s;
    }
    Engine_Update(c);
}

/* 800417ec */
void Engine_Free(uint8_t* c)
{
    Engine* e = Engine_Of(c);
    if (e)
    {
        if (e->srcAi != -1)
            Snd_SourceKill(e->handle, e->srcAi);
        if (e->srcSkid != -1)
            Snd_SourceKill(e->handle, e->srcSkid);
        if (e->srcHorn != -1)
            Snd_SourceKill(e->handle, e->srcHorn);
        for (int k = 0; k < 4; k++)
            if (e->voice[k])
            {
                Voice_Free(e->voice[k]);
                e->voice[k] = NULL;
                e->playing[k] = 0;
            }
        e->used = 0;
    }
    U8(c, 0x430) = 0xff;
}

/* 8004170c: calla los motores de los humanos (pausa) */
void Engine_StopAll(void)
{
    for (int i = 0; i < NUM_ENGINES; i++)
        for (int k = 0; k < 4; k++)
        {
            Engine* e = &s_eng[i];
            if (!e->playing[k])
                continue;
            if (k == 2)
            {
                Voice_Free(e->voice[2]);
                e->voice[2] = NULL;
            }
            else if (e->voice[k])
            {
                Voice_StopLib(e->voice[k]);
                e->voice[k]->vol = 0;
            }
            e->playing[k] = 0;
        }
}

/* 80040b08: arranca, cambia o calla la voz fija k del motor (def: indice en s_engDefs o -1 = por id) */
static void Engine_Voice(Engine* e, int k, int def, int id, const SndPan* p, int pitch, int sel)
{
    SndVoice* v = e->voice[k];
    if (!v)
        return;
    if (!e->playing[k])
    {
        if (p->vol <= 0)
            return;
        int d = def == -1 ? Def_FromId(id) : s_engDefs[def];
        if (d != 0xff && Def_Play(d, v, p->pan, p->vol, pitch, sel, p->reverb))
        {
            e->playing[k] = 1;
            if (Snd_LogOn())
                NR_Log("[snd] motor %d voz %d (def %d, patch %d) vol %d tono %#x\n", e->id - 1000, k, d, sel, p->vol, pitch);
        }
    }
    else if (p->vol == 0)
    {
        Voice_StopLib(v);
        e->playing[k] = 0;
        v->vol = 0;
    }
    else
        Voice_Set(v, p->pan, p->vol, pitch, p->reverb);
}

static int Clamp7f(int v)
{
    return v < 0 ? 0 : v > 0x7f ? 0x7f : v;
}

/* 80040c58 */
void Engine_UpdateAll(int dt)
{
    int views = g_SplitScreen ? 2 : 1;
    for (int i = 0; i < NUM_ENGINES; i++)
    {
        Engine* e = &s_eng[i];
        if (!e->used)
            continue;
        /* bocina */
        if (e->hornTimer)
        {
            if (!e->hornDelay)
            {
                e->hornTimer -= dt;
                if (e->hornTimer < 1)
                {
                    e->hornTimer = 0;
                    if (e->srcHorn != -1)
                    {
                        Snd_SourceKill(e->handle, e->srcHorn);
                        e->srcHorn = -1;
                    }
                }
            }
            else
                e->hornDelay--;
        }
        if (e->player == -1)
            continue;
        for (int view = 0; view < views; view++)
        {
            const RaceCar* lis = g_Humans[view];
            SndPan p;
            if (view == e->player)
            {
                e->inCar = (uint8_t)Engine_InCar(view);
                for (int k = 0; k < 2; k++)
                {
                    int sel = k ? e->patchExtra : e->patchMain;
                    memset(&p, 0, sizeof(p));
                    Snd_Listen(e->x, e->z, &p, lis, s_engIds[k]);
                    int vol = p.vol;
                    if ((k == 0 && !e->throttle) || (k == 1 && e->throttle))
                        vol = 0;
                    SndVoice* v = e->voice[k];
                    if (!v || s_engDefs[k] == 0xff)
                        continue;
                    Snd_PanFinish(&p);
                    int cur = e->playing[k] ? v->vol : (vol ? p.vol >> 2 : 0);
                    if (e->inCar && vol)
                        vol = Clamp7f(vol + (vol >> 2));
                    int dv = cur - vol;
                    if (dv > 1 || dv < -1)
                        vol = dv < 1 ? cur + 2 : cur - 2;
                    if (sel != -1)
                    {
                        int pos[3] = { e->x, s_lis[view].pos[1], e->z };
                        v->doppler = Snd_Doppler(s_lis[view].vel, s_lis[view].pos, e->vel, e->speed, pos);
                        p.vol = (int8_t)Clamp7f(vol);
                        Engine_Voice(e, k, k, -1, &p, e->pitch, sel);
                    }
                }
                /* superficie: bucle (y golpes al pasar) segun el tipo de suelo de la fisica */
                int surf = I8(e->phys, 0x181), loop = 0xc, hit = 0;
                if ((unsigned)surf < 0x12)
                {
                    loop = Exe_S8(0x800aaa7cu + (uint32_t)surf * 3);
                    hit = Exe_S8(0x800aaa7du + (uint32_t)surf * 3);
                }
                int on = 1;
                if (e->playing[3] && (e->surface != loop || U8(e->phys, 0x638) == 0))
                {
                    Voice_Fade(e->voice[3]);
                    e->playing[3] = 0;
                    e->voice[3] = Voice_Alloc(0x7f, 10, 2, 0xff);
                }
                if (!e->playing[3] && U8(e->phys, 0x638) == 0)
                {
                    e->surface = 0;
                    on = 0;
                }
                if (!on)
                    continue;
                int prevSurf = e->surface;
                e->surface = loop;
                RaceCar* ent = (RaceCar*)ENT(e->phys);
                if (hit)
                {
                    int lo = 0x300, hi = 0xa00;
                    if (hit == 0x4b || hit == 0x5a)
                    {
                        lo = 0xc00;
                        hi = 0x1400;
                    }
                    if (prevSurf != loop)
                        e->surfMark = I32(e->phys, 0x12c);
                    int sp = ent->speed > 0x80 ? 0x80 : ent->speed;
                    if (lo + ((sp * (hi - lo)) >> 7) < I32(e->phys, 0x12c) - e->surfMark || prevSurf != loop)
                    {
                        int d = Def_FromId(hit);
                        if (d != 0xff)
                            Snd_PlayDef(e->x, e->z, d, g_SplitScreen ? lis : NULL, 1, ((sp * 0x6000) >> 7) + 0xa000, hit,
                                        -1, -1);
                        e->surfMark = I32(e->phys, 0x12c);
                    }
                }
                if (loop)
                {
                    memset(&p, 0, sizeof(p));
                    Snd_Listen(e->x, e->z, &p, lis, loop);
                    int vol = p.vol, v2 = 0;
                    if (vol)
                    {
                        int s = ent->speed, sliding = 0;
                        if (!(IA(e->phys, 0xfe) & 3) || e->lastPitch < 0x1256 || e->srcSkid != -1)
                        {
                            if (hit)
                                s += 0x14;
                        }
                        else if (g_RaceTime > 599)
                        {
                            sliding = 1;
                            s += (e->skid * 5) >> 6;
                        }
                        int t = s < 0x15 ? 0 : s - 0x14;
                        int m = t > 0x80 ? vol * 0x80 : t * vol;
                        v2 = m >> 7;
                        if (!sliding)
                            v2 -= m >> 8;
                    }
                    if (e->playing[3] && e->voice[3])
                    {
                        int cur = e->voice[3]->vol, dv = v2 - cur;
                        if (dv > 2 || dv < -2)
                            v2 = dv < 1 ? cur - 2 : cur + 2;
                    }
                    p.vol = (int8_t)v2;
                    if (v2)
                        Snd_PanFinish(&p);
                    Engine_Voice(e, 3, -1, loop, &p, -1, -1);
                }
            }
            else if (s_engDefs[0] == 0xff || e->patchMain == -1)
            {
                if (e->voice[2])
                {
                    Voice_Free(e->voice[2]);
                    e->voice[2] = NULL;
                    e->playing[2] = 0;
                }
            }
            else
            {
                /* el motor del otro humano (pantalla partida) */
                memset(&p, 0, sizeof(p));
                if (!Snd_Listen(e->x, e->z, &p, lis, 3))
                {
                    if (e->voice[2])
                    {
                        Voice_Free(e->voice[2]);
                        e->voice[2] = NULL;
                        e->playing[2] = 0;
                    }
                    continue;
                }
                Snd_PanFinish(&p);
                int pos[3] = { e->x, s_lis[view].pos[1], e->z };
                int dop = Snd_Doppler(s_lis[view].vel, s_lis[view].pos, e->vel, e->speed, pos);
                if (!e->voice[2])
                {
                    SndVoice* v = Voice_Alloc(0x7f, 10, 2, 0xff);
                    if (v)
                    {
                        v->doppler = dop;
                        if (Def_Play(s_engDefs[0], v, p.pan, p.vol, e->pitch, e->patchMain, p.reverb))
                        {
                            e->voice[2] = v;
                            e->playing[2] = 1;
                        }
                        else
                            Voice_Free(v);
                    }
                }
                else
                {
                    e->voice[2]->doppler = dop;
                    Voice_Set(e->voice[2], p.pan, p.vol, e->pitch, p.reverb);
                }
            }
        }
    }
}

/* 80041ac4 */
static Engine* Engine_ById(int id)
{
    for (int i = 0; i < NUM_ENGINES; i++)
        if (s_eng[i].used && s_eng[i].id == id)
            return &s_eng[i];
    return NULL;
}

/* 80041b0c: bocina (fuente id 5 con la muestra del tipo de motor del coche) */
static void Engine_Horn(Engine* e, int dur)
{
    if (e->hornTimer)
        return;
    RaceCar* ent = (RaceCar*)ENT(e->phys);
    if (e->srcHorn == -1)
    {
        e->srcHorn = Snd_SourceCreate(5, e->handle, ent->obj.pos[0], ent->obj.pos[2], -1, ent->speed, ent->dir);
        if (e->srcHorn != -1)
            Snd_SourceSetPatch(e->handle, e->patchMain, e->srcHorn);
    }
    else
        Snd_SourceMove(e->handle, ent->obj.pos[0], ent->obj.pos[2], -1, ent->speed, ent->dir, e->srcHorn);
    e->hornTimer = dur;
    e->hornDelay = 1;
}

/* Car_PlaySfx 80041be8: 5 = bocina; el resto, sonido de interfaz con la muestra del coche */
void Car_PlaySfx(int id, const RaceCar* car)
{
    Engine* e = car ? Engine_ById(car->id) : NULL;
    if (!e || e->patchMain == -1)
        return;
    if (id == 5)
        Engine_Horn(e, 0x78);
    else
    {
        int d = Def_FromId(id);
        if (d != 0xff)
            Snd_PlayDef(0, 0, d, car, 1, 0x10000, id, e->patchMain, -1);
    }
}

/* 80041c6c: sonido de un coche de la IA en (x, z) (la bocina cuando adelanta) */
void Car_PlaySfxAt(int id, int x, int z, const RaceCar* car)
{
    int d = Def_FromId(id);
    Engine* e = car ? Engine_ById(car->id) : NULL;
    if (d == 0xff || !e || e->patchMain == -1)
        return;
    if (id == 5)
    {
        Engine_Horn(e, 0x78);
        return;
    }
    const RaceCar* ent = NULL;
    if (g_SplitScreen)
    {
        Snd_PlayDef(x, z, d, g_Humans[0], 1, 0x10000, id, e->patchMain, -1);
        ent = g_Humans[1];
    }
    Snd_PlayDef(x, z, d, ent, 1, 0x10000, id, e->patchMain, -1);
}
