/* Sonido del juego (ver snd.h). Las estructuras siguen a las del original (tamanos y campos) pero en C; nada
 * de fuera accede a ellas por desplazamientos. Motores en snd_car.c, musica en snd_music.c (lee los VAGM del
 * fichero en vez de recibirlos por el stream, 80021464..80022300) y comentarista en snd_speech.c (80023258,
 * 80022f24, 8002b920). */
#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "asset.h"
#include "vecmath.h"
#include "racecar.h"
#include "actor.h"
#include "globals.h"
#include "pause.h"
#include "snd_lib.h"
#include "snd.h"

#define ASSET_Cshd NR_TAG('C', 's', 'h', 'd')
#define ASSET_Cvkh NR_TAG('C', 'v', 'k', 'h')
#define ASSET_Cvkb NR_TAG('C', 'v', 'k', 'b')
#define ASSET_Ceng NR_TAG('C', 'e', 'n', 'g')

#include "snd_int.h"

/* ---------------------------------------------------------------- estado */

static int       s_on;                      /* DAT_800af270 */
int              s_raceActive;              /* DAT_800af809 */
static int       s_mode = 1;                /* DAT_800af800 */
static int       s_listeners;               /* DAT_800af804 */
static int       s_duck;                    /* DAT_800af808 (voz del comentarista) */
static int       s_sfxVol, s_sfxVolEff;     /* DAT_800af248 / 24c */
static int       s_musicVol, s_musicVolEff; /* DAT_800af250 / 254 */
static int       s_speechVol;               /* DAT_800af25c */
static int       s_engineVol, s_engineVolEff;   /* DAT_800af264 / 268 */
static int       s_serial = 1;              /* DAT_800af26c */
static int       s_lastTick;                /* DAT_800af274 */
unsigned         s_tick;                    /* DAT_800af714: tics de 1/300 s */

static SndDef    s_defs[NUM_DEFS];
static uint8_t   s_idMap[NUM_IDS];          /* DAT_800b3b50 */
static int       s_numDefs;                 /* DAT_800af839 */
static PatchRef  s_refs[NUM_REFS];
static int       s_refNext;                 /* DAT_800af83a */

static SndVoice  s_voices[NUM_VOICES];      /* DAT_800b4db0 */
static SndVoice* s_free;                    /* DAT_800af80c */
static int       s_freeCount;               /* DAT_800af810 */
static SndVoice* s_list[3];                 /* DAT_800af81c (0), 814 (1), 824 (2) */
static int       s_listCount[3];

static SndSource s_sources[NUM_SOURCES];    /* DAT_800b2e80 */
static SndSource* s_srcFree;                /* DAT_800b3a3c */
static SndSource* s_srcHead, *s_srcTail;    /* DAT_800b3a40 / 44 */
static int        s_srcCount;               /* DAT_800b3a38 */

SndListener s_lis[2];

static BankEntry s_banks[NUM_BANKS];
static NrAsset*  s_cshd[2];                 /* DAT_800af668 */
static NrAsset*  s_pendHdr, *s_pendBody;    /* DAT_800af144 / 148 */

typedef struct { int dur, delay, def; SndHandle h; } SndTimer;   /* DAT_800b3ae0 (2 x 0x14) */
static SndTimer  s_timers[2];
static int       s_countdownBeeps;          /* DAT_800af838 */
static int       s_pickupSrc[2] = { -1, -1 }, s_pickupHandle[2];   /* DAT_800b2b40 */

typedef struct { int kind, x, z; } QueuedSnd;
static QueuedSnd s_queue[0x14];             /* DAT_800b52f0 */
static int       s_queueCount;              /* DAT_800af27c */

/* Solo en el port (depuracion): NR_SNDLOG=1 apunta en el log los bancos cargados y los sonidos tocados */
int Snd_LogOn(void)
{
    static int s_log = -1;
    if (s_log < 0)
        s_log = getenv("NR_SNDLOG") != NULL;
    return s_log;
}

/* ---------------------------------------------------------------- volumenes */

static int Clamp127(int v)
{
    return v < 0 ? 0 : v > 0x7f ? 0x7f : v;
}

void Snd_SetSfxVolume(int v)
{
    s_sfxVol = Clamp127(v);
    s_sfxVolEff = (s_sfxVol * 0xcccc) >> 16;
}

void Snd_SetMusicVolume(int v)
{
    s_musicVol = Clamp127(v);
    s_musicVolEff = (s_musicVol * 0xcccc) >> 16;
    Music_SetVolume(s_musicVolEff);
}

void Snd_SetSpeechVolume(int v)
{
    s_speechVol = Clamp127(v);
}

void Snd_SetEngineVolume(int v)
{
    s_engineVol = Clamp127(v);
    s_engineVolEff = (((s_engineVol * 0xa000) >> 16) * 0xcccc) >> 16;
}

void Snd_SetMode(int mode)
{
    s_mode = (unsigned)mode > 2 ? 0 : mode;
    SndLib_SetMono(mode == 0);
}

int Snd_GetMode(void)
{
    return s_mode;
}

/* Ajustes del menu de audio (g_Settings: efectos, motor, musica, voces y modo) */
void Snd_ApplySettings(const uint8_t* volume, int mode)
{
    Snd_SetSfxVolume(volume[0]);
    Snd_SetEngineVolume(volume[1]);
    Snd_SetMusicVolume(volume[2]);
    Snd_SetSpeechVolume(volume[3]);
    Snd_SetMode(mode);
}

/* volumen efectivo de cada ajuste (el que usa el sonido de prueba de 80025ad4) */
int Snd_EffectiveVolume(int which)
{
    switch (which)
    {
    case 0: return s_sfxVolEff;
    case 1: return s_engineVolEff;
    case 2: return s_musicVolEff;
    default: return s_speechVol;
    }
}

/* 8003f6d8: sonido de interfaz con el volumen dado */
void Snd_PlayUiVol(int id, const void* entity, int vol)
{
    int d = Def_FromId(id);
    if (d != 0xff)
        Snd_PlayDef(0, 0, d, (const RaceCar*)entity, 1, 0x10000, id, -1, vol);
}

/* 8003d320 (parte): volumen y panorama de una frase del comentarista pegada a algo en (x, z), como un sonido de la
 * pista: al doble, con el tope del volumen de los efectos (0x70 mientras habla el comentarista); fuera de alcance,
 * volumen 0 */
void Snd_SpeechAt(int x, int z, const void* car, SndPan* o)
{
    memset(o, 0, sizeof(*o));
    if (!Snd_Listen(x, z, o, (const RaceCar*)car, 0))
    {
        o->vol = 0;
        o->pan = 0x40;
        return;
    }
    Snd_PanFinish(o);
    int v = o->vol * 2, cap = s_duck ? 0x70 : s_sfxVolEff;
    o->vol = (int8_t)(v > cap ? cap : v);
}

int Snd_SpeechVolume(void)
{
    return s_speechVol;   /* 8003d674 */
}

/* 8003d708 (on) / 8003d7ac (off): mientras habla el comentarista, las voces de las listas 0 y 2 (y la 1 al
 * volver) bajan 1/8 */
void Snd_Duck(int on)
{
    if (s_duck == (on != 0))
        return;
    s_duck = on != 0;
    for (int l = 0; l < 3; l++)
    {
        if (on && l == 1)
            continue;
        for (SndVoice* v = s_list[l]; v; v = v->next)
            if (v->volReq != -1 && v->patch)
                Voice_Set(v, v->pan, v->volReq, v->pitch, v->reverb);
    }
    Music_SetDuck(on);
}

void Snd_SetListenerCount(int n)
{
    s_listeners = n;
}

void Snd_SetRaceActive(int on)
{
    s_raceActive = on != 0;
}

/* ---------------------------------------------------------------- definiciones */

/* 8003d8ec / 8003cecc */
static void Defs_Reset(void)
{
    memset(s_idMap, 0xff, sizeof(s_idMap));
    s_numDefs = 0;
    for (int i = 0; i < NUM_DEFS; i++)
    {
        memset(&s_defs[i], 0, sizeof(s_defs[i]));
        s_defs[i].prio = 1;
        s_defs[i].key = 0xff;
    }
    memset(s_refs, 0, sizeof(s_refs));
    s_refNext = 0;
}

/* 8003d89c: definicion de un id de sonido (0xFF si no hay o no tiene muestras) */
int Def_FromId(int id)
{
    if ((unsigned)(id - 1) > 0x8a)
        return 0xff;
    int d = s_idMap[id];
    return d != 0xff && s_defs[d].count ? d : 0xff;
}

/* 8003fb84: definicion con esa clave o una nueva con los datos del registro del Cshd (que pasa a guardar su
 * indice en +0, como en el original) */
static int Def_FindOrCreate(uint8_t* rec, int key)
{
    for (int i = 0; i < NUM_DEFS; i++)
    {
        if (!s_defs[i].maxVoices)
            break;
        if (s_defs[i].key == key)
            return i;
    }
    if (s_numDefs > 0x6d)
        return -1;
    int d = s_numDefs++;
    *(uint16_t*)rec = (uint16_t)d;
    if (rec[4] == 0)
        rec[4] = 1;
    else if (rec[4] > 10)
        rec[4] = 10;
    if (rec[5] > MAX_PATCHES)
        rec[5] = MAX_PATCHES;
    SndDef* s = &s_defs[d];
    s->prio = rec[4];
    s->maxVoices = rec[5];
    s->key = (uint8_t)key;
    s->b1d = rec[6];
    for (int i = 0; i < 4; i++)
    {
        int id = rec[8 + i];
        if (id && id < NUM_IDS)
            s_idMap[id] = (uint8_t)d;
    }
    return d;
}

/* 80040148 */
static PatchRef* Ref_Alloc(void)
{
    for (int n = 0; n < NUM_REFS; n++)
    {
        PatchRef* r = &s_refs[s_refNext];
        s_refNext = s_refNext + 1 > 0x95 ? 0 : s_refNext + 1;
        if (!r->used)
        {
            r->used = 1;
            return r;
        }
    }
    return NULL;
}

/* 80023544 */
static int Def_AddPatch(int key, int prog, int bank, int slot, uint8_t* rec)
{
    int d = Def_FindOrCreate(rec, key);
    if (d < 0)
        return 0;
    SndDef* s = &s_defs[d];
    if (!s->maxVoices || s->count >= MAX_PATCHES)
        return 0;
    PatchRef* r = Ref_Alloc();
    if (!r)
        return 0;
    s->patch[s->count++] = r;
    r->prog = (uint8_t)prog;
    r->bank = (uint8_t)bank;
    r->slot = (uint8_t)slot;
    return 1;
}

/* 80023630: quita de la definicion d el patch del programa prog */
static int Def_RemovePatch(int d, int prog)
{
    SndDef* s = &s_defs[d];
    for (int i = 0; i < s->count; i++)
        if (s->patch[i]->prog == prog)
        {
            s->count--;
            s->patch[i]->used = 0;
            if (i != s->count)
                s->patch[i] = s->patch[s->count];
            return 1;
        }
    return 0;
}

/* ---------------------------------------------------------------- voces */

static void Voice_ListUnlink(SndVoice* v)
{
    if (v->prev)
        v->prev->next = v->next;
    else
        s_list[v->list > 2 ? 1 : v->list] = v->next;
    if (v->next)
        v->next->prev = v->prev;
    s_listCount[v->list > 2 ? 1 : v->list]--;
}

/* 8003f25c */
static void Voice_DefUnlink(SndVoice* v)
{
    SndDef* d = &s_defs[v->def];
    if (v->dprev)
        v->dprev->dnext = v->dnext;
    else
        d->voices = v->dnext;
    if (v->dnext)
        v->dnext->dprev = v->dprev;
    d->active--;
}

/* 8003f3f8 */
static void Voice_DefLink(SndVoice* v)
{
    SndDef* d = &s_defs[v->def];
    SndVoice* prev = NULL, *it = d->voices;
    while (it && it->prio <= v->prio)
    {
        prev = it;
        it = it->dnext;
    }
    v->dprev = prev;
    v->dnext = it;
    if (prev)
        prev->dnext = v;
    else
        d->voices = v;
    if (it)
        it->dprev = v;
    d->active++;
}

/* 8003f2c8: listas 0 y 2 por delante; la 1 ordenada por prioridad (la menor primero) */
static void Voice_Link(SndVoice* v)
{
    int l = v->list > 2 ? 1 : v->list;
    if (l != 1)
    {
        v->prev = NULL;
        v->next = s_list[l];
        if (s_list[l])
            s_list[l]->prev = v;
        s_list[l] = v;
    }
    else
    {
        SndVoice* prev = NULL, *it = s_list[1];
        while (it && it->prio <= v->prio)
        {
            prev = it;
            it = it->next;
        }
        v->prev = prev;
        v->next = it;
        if (prev)
            prev->next = v;
        else
            s_list[1] = v;
        if (it)
            it->prev = v;
    }
    s_listCount[l]++;
    if (v->def != 0xff)
        Voice_DefLink(v);
}

/* 80023910 */
void Voice_StopLib(SndVoice* v)
{
    SndLib_Stop(v->lib);
    SndLib_ReverbOff(v->lib);
}

/* 8003f0c8 */
void Voice_Free(SndVoice* v)
{
    if (!v || !v->serial)
        return;
    v->serial = 0;
    Voice_StopLib(v);
    Voice_ListUnlink(v);
    v->next = s_free;
    s_free = v;
    s_freeCount++;
    if (v->def != 0xff)
        Voice_DefUnlink(v);
    SndSource* s = v->src;
    if (s)
    {
        if (s->voice[0] == v)
            s->voice[0] = NULL;
        else if (s->voice[1] == v)
            s->voice[1] = NULL;
    }
}

/* 8003f550: voz para un sonido de volumen vol y clase de prioridad cls (lista list, definicion def); si no
 * hay libres se roba la de menor prioridad (la primera de la lista 1, o la mas baja de la definicion) */
SndVoice* Voice_Alloc(int vol, int cls, int list, int def)
{
    int prio = vol + cls * 16;
    SndVoice* v = NULL;
    if (def == 0xff || s_defs[def].active < s_defs[def].maxVoices)
    {
        if (s_free)
        {
            v = s_free;
            s_free = v->next;
            s_freeCount--;
        }
        else if (s_list[1] && s_list[1]->prio < prio)
        {
            Voice_Free(s_list[1]);
            v = s_free;
            s_free = v->next;
            s_freeCount--;
        }
    }
    else
    {
        SndVoice* low = s_defs[def].voices;
        if (low && low->prio < prio && (low->list == 1 || low->src))
        {
            Voice_Free(low);
            v = s_free;
            s_free = v->next;
            s_freeCount--;
        }
    }
    if (!v)
        return NULL;
    v->def = def;
    v->vol = (int8_t)vol;
    v->list = (uint8_t)list;
    v->src = NULL;
    v->prio = list == 1 ? prio + 0x50 : prio;
    v->volReq = -1;
    v->reverb = 0;
    v->doppler = 0;
    v->pitch = -1;
    v->serial = s_serial++;
    if (!s_serial)
        s_serial = 1;
    v->patch = NULL;
    Voice_Link(v);
    return v;
}

void Handle_Set(SndHandle* h, SndVoice* v)
{
    if (h && v)
    {
        h->v = v;
        h->serial = v->serial;
    }
}

int Handle_Valid(const SndHandle* h)
{
    return h->v && h->v->serial == h->serial;
}

int Handle_Free(SndHandle* h)
{
    if (!h || !Handle_Valid(h))
        return 0;
    Voice_Free(h->v);
    h->v = NULL;
    return 1;
}

/* 80023708: tono final (el pedido en 8.8 * 0x16, o el del patch, por el Doppler; 0..0x1000) */
static int Voice_Pitch(const SndVoice* v, int pitch)
{
    int p = pitch == -1 ? -1 : (pitch >> 8) * 0x16;
    if (v->doppler && (p != 0 || (p = v->basePitch) != -1))
        p = (int)(((int64_t)p * v->doppler) >> 16);
    if (p != -1)
        p = p < 0 ? 0 : p > 0x1000 ? 0x1000 : p;
    return p;
}

/* 80040070 */
static void Voice_SetParams(SndVoice* v, int pan, int vol, int pitch, int reverb)
{
    v->volReq = (int8_t)vol;
    if (s_duck)
        vol -= vol >> 3;
    v->vol = (int8_t)vol;
    v->pan = (int8_t)pan;
    v->pitch = pitch;
    v->reverb = (int8_t)reverb;
}

/* 800237ac: arranca la muestra del patch de la voz */
static int Voice_Start(const SndDef* d, SndVoice* v)
{
    PatchRef* r = v->patch;
    v->basePitch = SndLib_PatchPitch((int8_t)r->slot, r->prog);
    if ((int8_t)r->slot == -1)
        return 0;
    SndLibParams p = { Voice_Pitch(v, v->pitch), v->vol, v->pan };
    (void)d;
    if (!SndLib_Play((int8_t)r->slot, r->prog, 0, v->lib, &p))
        return 0;
    if (v->reverb)
        SndLib_ReverbOn(v->lib);
    return 1;
}

/* 800400a0 -> 8002387c (+ 800238c8) */
void Voice_Set(SndVoice* v, int pan, int vol, int pitch, int reverb)
{
    Voice_SetParams(v, pan, vol, pitch, reverb);
    SndLibParams p = { Voice_Pitch(v, pitch), v->vol, v->pan };
    SndLib_Update(v->lib, &p);
    if (v->reverb)
        SndLib_ReverbOn(v->lib);
    else
        SndLib_ReverbOff(v->lib);
}

/* 8003ff20: toca en la voz el patch sel de la definicion (-1: el siguiente, al azar si hay varios) */
int Def_Play(int def, SndVoice* v, int pan, int vol, int pitch, int sel, int reverb)
{
    SndDef* d = &s_defs[def];
    if (!d->count)
        return 0;
    if (sel == -1)
    {
        if (d->count == 1)
            d->rr = 0;
        else
        {
            d->rr = (uint8_t)(d->rr + (s_tick & 3));
            while (d->rr >= d->count)
                d->rr = (uint8_t)(d->rr - d->count);
        }
    }
    else if (sel < 0 || sel >= d->count)
        return 0;
    else
        d->rr = (uint8_t)sel;
    v->patch = d->patch[d->rr];
    Voice_SetParams(v, pan, vol, pitch, reverb);
    return Voice_Start(d, v);
}

/* 8003d7f4 */
static void Voices_Reset(void)
{
    s_free = NULL;
    s_freeCount = 0;
    for (int i = NUM_VOICES - 1; i >= 0; i--)
    {
        memset(&s_voices[i], 0, sizeof(s_voices[i]));
        s_voices[i].lib = (int8_t)i;
        s_voices[i].next = s_free;
        s_free = &s_voices[i];
        s_freeCount++;
    }
    memset(s_list, 0, sizeof(s_list));
    memset(s_listCount, 0, sizeof(s_listCount));
}

/* ---------------------------------------------------------------- fuentes */

/* 8003dd30 */
static void Sources_Reset(void)
{
    for (int i = 0; i < NUM_SOURCES; i++)
    {
        s_sources[i].handle = -1;
        s_sources[i].index = (uint8_t)i;
        s_sources[i].next = i + 1 < NUM_SOURCES ? &s_sources[i + 1] : NULL;
    }
    s_srcCount = 0;
    s_srcFree = &s_sources[0];
    s_srcHead = s_srcTail = NULL;
}

/* 8003d9d0: por prioridad, la mayor primero (las de prioridad 0, al final) */
static void Source_Insert(SndSource* s)
{
    SndSource* prev = NULL, *it;
    if (s->prio == 0)
        prev = s_srcTail, it = NULL;
    else
        for (it = s_srcHead; it && s->prio <= it->prio; it = it->next)
            prev = it;
    s->prev = prev;
    s->next = it;
    if (prev)
        prev->next = s;
    else
        s_srcHead = s;
    if (it)
        it->prev = s;
    else
        s_srcTail = s;
    s_srcCount++;
}

/* 8003d930 */
static void Source_Release(SndSource* s)
{
    if (s->prev)
        s->prev->next = s->next;
    else
        s_srcHead = s->next;
    if (s->next)
        s->next->prev = s->prev;
    else
        s_srcTail = s->prev;
    s->next = s_srcFree;
    s_srcFree = s;
    if (s_srcCount == 0)
        Sources_Reset();
    else
        s_srcCount--;
}

int Snd_SourceCreate(int id, int handle, int x, int z, int pitch, int speed, const int* dir)
{
    int d = Def_FromId(id);
    if (d == 0xff || s_srcCount >= NUM_SOURCES || !s_srcFree)
        return -1;
    SndSource* s = s_srcFree;
    s_srcFree = s->next;
    int idx = s->index;
    s->volScale = id == 0x11 ? 0x8000 : 0x10000;
    s->patchSel = -1;
    s->handle = handle;
    s->voice[0] = s->voice[1] = NULL;
    s->def[0] = s->def[1] = (int16_t)d;
    s->x = x;
    s->z = z;
    s->kind[0] = s->kind[1] = s->lastKind[0] = s->lastKind[1] = (int8_t)id;
    if (dir)
    {
        memcpy(s->vel, dir, sizeof(s->vel));
        s->speed = speed;
    }
    else
        s->speed = -1;
    s->prio = 0;
    s->pitch = pitch;
    Source_Insert(s);
    if (Snd_LogOn())
        NR_Log("[snd] fuente %d: id %d (def %d) entidad %d\n", idx, id, d, handle);
    return idx;
}

void Snd_SourceMove(int handle, int x, int z, int pitch, int speed, const int* dir, int src)
{
    (void)handle;
    if (src < 0 || src >= NUM_SOURCES)
        return;
    SndSource* s = &s_sources[src];
    s->x = x;
    s->z = z;
    if (dir)
    {
        memcpy(s->vel, dir, sizeof(s->vel));
        s->speed = speed;
    }
    else
        s->speed = -1;
    s->pitch = pitch;
}

void Snd_SourceSetPatch(int handle, int patch, int src)
{
    (void)handle;
    if (src >= 0 && src < NUM_SOURCES)
        s_sources[src].patchSel = (int8_t)patch;
}

void Snd_SourceSetVolume(int handle, int src, int scale)
{
    (void)handle;
    if (src >= 0 && src < NUM_SOURCES)
        s_sources[src].volScale = scale;
}

void Snd_SourceKill(int handle, int src)
{
    (void)handle;
    if (src < 0 || src >= NUM_SOURCES || s_sources[src].handle == -1)
        return;
    SndSource* s = &s_sources[src];
    s->handle = -1;
    for (int k = 0; k < 2; k++)
        if (s->voice[k])
        {
            SndVoice* v = s->voice[k];
            v->src = NULL;
            Voice_Fade(v);
            s->voice[k] = NULL;
        }
    Source_Release(s);
}

/* ---------------------------------------------------------------- oyente, volumen y panoramica */

void Snd_SetListener(int view, const int* pos, int heading, const int* vel, int reverb, int curve)
{
    SndListener* l = &s_lis[view & 1];
    memcpy(l->pos, pos, sizeof(l->pos));
    memcpy(l->vel, vel, sizeof(l->vel));
    l->heading = heading >> 4;
    l->curve = (int16_t)curve;
    l->reverb = (int8_t)reverb;   /* (la profundidad 800239bc va con el tunel: PsyCross no la implementa) */
}

static int Snd_IsEngine(int kind)
{
    return (unsigned)(kind - 3) < 2;   /* 8003de7c */
}

/* atenuacion lineal hasta 0x20000 */
static int Snd_Atten(int dist, int maxv)
{
    if (dist < 0)
        return maxv;
    int v = maxv - (((dist < 0x20001 ? dist : 0x20000) * maxv) >> 17);
    return v < 0 ? 0 : v > maxv ? maxv : v;
}

/* 8003de88: volumen y panoramica de un sonido en (x, z) para los oyentes (0 = no se oye) */
int Snd_Listen(int x, int z, SndPan* o, const RaceCar* ent, int kind)
{
    int n = s_listeners, ok = 1, view = 0;
    int maxv = Snd_IsEngine(kind) ? s_engineVolEff : s_sfxVolEff;
    if (x || z)
    {
        o->done = 0;
        if (n == 2 && ent && ent->ctrl)
        {
            int pl = ent->ctrl[0x59];
            if (pl == 0 || pl == 1)
            {
                n = 1;
                view = pl;
                o->pan = pl ? 0x7f : 0;
                o->done = 1;
            }
        }
        if (n == 1)
        {
            o->d[0][0] = x - s_lis[view].pos[0];
            o->d[0][1] = z - s_lis[view].pos[2];
            o->dist[0] = Vec_ApproxLen2(o->d[0]);
            if (o->dist[0] < 0x20000)
            {
                o->vol = (int8_t)Snd_Atten(o->dist[0], maxv);
                if (s_mode == 0)
                {
                    o->pan = 0x40;
                    o->done = 1;
                }
            }
            else
            {
                ok = 0;
                o->vol = 0;
                o->pan = 0x40;
                o->done = 1;
            }
            o->reverb = s_lis[view].reverb;
            return ok;
        }
        if (n != 2)
        {
            o->vol = (int8_t)maxv;
            o->pan = 0x40;
            o->done = 1;
            o->reverb = 0;
            return ok;
        }
        o->reverb = 0;
        for (int k = 0; k < 2; k++)
        {
            o->d[k][0] = x - s_lis[k].pos[0];
            o->d[k][1] = z - s_lis[k].pos[2];
            o->dist[k] = Vec_ApproxLen2(o->d[k]);
        }
        if (o->dist[0] > 0x1ffff && o->dist[1] > 0x1ffff)
        {
            o->vol = 0;
            o->pan = 0x40;
            o->done = 1;
            return 0;
        }
        int a = Snd_Atten(o->dist[0], maxv), b = Snd_Atten(o->dist[1], maxv);
        o->vol = (int8_t)(a > b ? a : b);
        if (s_mode)
            o->done = 0;
        else
        {
            o->pan = 0x40;
            o->done = 1;
        }
        return 1;
    }
    o->vol = (int8_t)maxv;
    o->pan = 0x40;
    if (n == 2 && s_mode && ent && ent->ctrl)
    {
        int pl = ent->ctrl[0x59];
        o->pan = pl == 0 ? 0 : pl == 1 ? 0x7f : 0x40;
    }
    o->done = 1;
    if (o->vol == 0)
        ok = 0;
    o->reverb = 0;
    return ok;
}

/* 80017788: angulo de (x, z) en 8 bits (0x40 = 90 grados) */
static int Snd_Angle8(int x, int z)
{
    if (!x && !z)
        return 0;
    int ax = x < 0 ? -x : x, az = z < 0 ? -z : z;
    unsigned q = (x < 0 ? 1u : 0) | (z < 0 ? 2u : 0) | (ax < az ? 4u : 0);
    switch (q)
    {
    case 0: return (az << 5) / ax;
    case 1: return 0x80 - (az << 5) / ax;
    case 2: return -((az << 5) / ax);
    case 3: return (az << 5) / ax - 0x80;
    case 4: return 0x40 - (ax << 5) / az;
    case 5: return (ax << 5) / az + 0x40;
    case 6: return (ax << 5) / az - 0x40;
    default: return -0x40 - (ax << 5) / az;
    }
}

/* 8003e664: panoramica respecto a la vista (angulo relativo al rumbo de la camara) */
static int Snd_PanFor(SndPan* o, int view, int k)
{
    if (o->dist[k] < 0x1000)
    {
        o->angle[view] = 0;
        return 0x40;
    }
    int a = (int8_t)Snd_Angle8(o->d[k][1], o->d[k][0]);
    o->angle[view] = (int8_t)a;
    int r = a - s_lis[view].heading;
    if (r < -0x80)
        r += 0x100;
    else if (r > 0x80)
        r -= 0x100;
    int p = r < 0x41 ? r + 0x40 : r - 0xc0;
    if (s_mode != 2)
        p = p < 0 ? -p : p;
    return p < -0x7f ? -0x7f : p > 0x7f ? 0x7f : p;
}

/* 8003e784 */
void Snd_PanFinish(SndPan* o)
{
    if (o->done)
        return;
    int a = Snd_PanFor(o, 0, 0);
    if (s_listeners == 2)
    {
        int b = Snd_PanFor(o, 1, 1);
        if (s_mode == 2)
        {
            a = a < 0 ? -a : a;
            b = b < 0 ? -b : b;
        }
        o->pan = (int8_t)(a / 2 + b / 2);
    }
    else
        o->pan = (int8_t)a;
    o->done = 1;
}

/* 80041948: Doppler (16.16) con la velocidad del oyente (que normaliza en el sitio, como el original) y la
 * de la fuente (direccion * velocidad << 8); 0x46E58 = velocidad del sonido */
int Snd_Doppler(int* lvel, const int* lpos, const int* sdir, int sspeed, const int* spos)
{
    int d[3] = { spos[0] - lpos[0], spos[1] - lpos[1], spos[2] - lpos[2] };
    Vec_Normalize(d);
    int len = Vec_NormalizeLen(lvel);
    int a = Vec_Dot16(d, lvel), b = Vec_Dot16(d, sdir);
    int den = 0x46e58 - (int)(((int64_t)a * len) >> 16) - (int)(((int64_t)b * (sspeed << 8)) >> 16);
    if (den == 0 || den == 1 || den == -1)
        return 0x10000;
    uint32_t q = 0x80000000u / (uint32_t)((den < 0 ? -den : den) >> 1);
    int r = (int)(((uint64_t)q * 0x46e58u) >> 16);
    return den < 1 ? -r : r;
}

/* 800422b0: acerca el volumen de 2 en 2 */
static int Snd_Approach(int cur, int want)
{
    int d = cur - want;
    if (d > 2 || d < -2)
        return d < 1 ? cur + 2 : cur - 2;
    return want;
}

/* ---------------------------------------------------------------- tocar sonidos */

/* 8003f898 */
SndVoice* Snd_PlayDef(int x, int z, int def, const RaceCar* ent, int list, int scale, int kind, int sel,
                             int volOverride)
{
    if (!s_on || (unsigned)def >= NUM_DEFS || !s_defs[def].count)
        return NULL;
    SndPan o;
    memset(&o, 0, sizeof(o));
    Snd_Listen(x, z, &o, ent, kind);
    if (volOverride != -1)
        o.vol = (int8_t)Clamp127(volOverride);
    if (o.vol == 0)
        return NULL;
    if (scale != 0x10000)
    {
        int v = (o.vol * scale) >> 16;
        o.vol = (int8_t)(v < 0x80 ? v : 0x7f);
    }
    SndVoice* v = Voice_Alloc(o.vol, s_defs[def].prio, list, def);
    if (!v)
        return NULL;
    Snd_PanFinish(&o);
    if (!Def_Play(def, v, o.pan, o.vol, -1, sel, o.reverb))
    {
        Voice_Free(v);
        return NULL;
    }
    if (Snd_LogOn())
    {
        const uint8_t* pr = SndLib_Program((int8_t)v->patch->slot, v->patch->prog);
        NR_Log("[snd] id %d (def %d, prog %d) voz %d vol %d pan %d muestra SPU %#x tono %#x\n", kind, def,
               v->patch->prog, v->lib, v->vol, v->pan, pr ? *(const uint32_t*)(pr + 4 + 0x10) : 0,
               pr ? *(const uint16_t*)(pr + 4 + 0xe) : 0);
    }
    return v;
}

void Snd_PlayUi(int id, const void* entity)
{
    int d = Def_FromId(id);
    if (d != 0xff)
        Snd_PlayDef(0, 0, d, (const RaceCar*)entity, 1, 0x10000, id, -1, -1);
}

void Snd_PlayAt(int id, int x, int z)
{
    int d = Def_FromId(id);
    if (d == 0xff)
        return;
    const RaceCar* ent = NULL;
    if (g_SplitScreen)
    {
        Snd_PlayDef(x, z, d, g_ViewHumans[0], 1, 0x10000, id, -1, -1);
        ent = g_ViewHumans[1];
    }
    Snd_PlayDef(x, z, d, ent, 1, 0x10000, id, -1, -1);
}

/* Cola de sonidos de la carrera (DAT_800b52f0, 20 entradas; Fx_Push del port): se tocan en Snd_Update */
void Snd_Queue(int id, int x, int z)
{
    if (s_queueCount >= 0x14)
        return;
    s_queue[s_queueCount].kind = id;
    s_queue[s_queueCount].x = x;
    s_queue[s_queueCount++].z = z;
}

/* 800415fc: el original apaga la voz y la deja apurar unos frames (bajando el volumen); aqui se suelta */
void Voice_Fade(SndVoice* v)
{
    Voice_Free(v);
}

/* 8004214c: sonido sin posicion durante dur tics (pitidos de la cuenta atras) */
static void Snd_PlayTimed(int id, int dur)
{
    int d = Def_FromId(id);
    if (d == 0xff)
        return;
    for (int i = 0; i < 2; i++)
    {
        if (s_timers[i].dur)
            continue;
        SndVoice* v = Snd_PlayDef(0, 0, d, NULL, 0, 0x10000, id, -1, -1);
        if (v)
        {
            s_timers[i].dur = dur;
            s_timers[i].delay = 1;
            s_timers[i].def = d;
            Handle_Set(&s_timers[i].h, v);
        }
        return;
    }
}

/* ---------------------------------------------------------------- bancos */

static const uint8_t* Cshd_Bank(int bank, int* outIdx, const uint8_t** outBase)
{
    for (int c = 0; c < 2; c++)
    {
        if (!s_cshd[c])
            continue;
        const uint8_t* b = s_cshd[c]->data;
        int n = *(const uint16_t*)(b + 2);
        for (int i = 0; i < n; i++)
            if (*(const uint16_t*)(b + 4 + i * 6) == bank)
            {
                *outIdx = i;
                *outBase = b;
                return b + 4 + i * 6;
            }
    }
    return NULL;
}

/* 800225b0: definiciones del banco (registros de 12 bytes del Cshd) */
static int Bank_RegisterDefs(int bank)
{
    int idx;
    const uint8_t* base;
    const uint8_t* e = Cshd_Bank(bank, &idx, &base);
    if (!e)
        return 0;
    int slot = -1;
    for (int i = 0; i < NUM_BANKS; i++)
        if (s_banks[i].lib != -1 && s_banks[i].bank == bank)
            slot = s_banks[i].lib;
    if (slot < 0)
        return 0;
    int n = *(const uint16_t*)(e + 2), off = *(const uint16_t*)(e + 4);
    uint8_t* rec = (uint8_t*)base + off;
    for (int k = 0; k < n; k++, rec += 12)
        Def_AddPatch(*(uint16_t*)rec & 0xff, rec[2], bank, slot, rec);
    return 1;
}

/* 800226d8: cuando llegan cabecera y muestras del mismo banco y el Cshd lo conoce, se suben a la SPU */
static void Bank_TryLoad(void)
{
    if (!s_pendHdr || !s_pendBody)
        return;
    int idx;
    const uint8_t* base;
    NrAsset* hdr = s_pendHdr;
    int e = -1;
    if (hdr->id == s_pendBody->id && Cshd_Bank(hdr->id, &idx, &base))
        for (int i = 0; i < NUM_BANKS && e < 0; i++)
            if (s_banks[i].lib == -1)
                e = i;
    if (e < 0)
        Asset_Free(hdr);
    else
    {
        int slot = SndLib_LoadBank(hdr->data, s_pendBody->data);
        if (slot < 0)
        {
            NR_Log("Sonido: no cabe el banco %d en la SPU\n", hdr->id);
            Asset_Free(hdr);
        }
        else
        {
            s_banks[e].lib = (int8_t)slot;
            s_banks[e].bank = (int8_t)hdr->id;
            s_banks[e].hdr = hdr;
            Bank_RegisterDefs(hdr->id);
            if (Snd_LogOn())
                NR_Log("[snd] banco %d -> hueco %d, SPU %#x (%#x bytes), %d definiciones\n", hdr->id, slot,
                       *(int32_t*)(hdr->data + 8), *(int32_t*)(hdr->data + 0xc), s_numDefs);
        }
    }
    Asset_Free(s_pendBody);
    s_pendBody = NULL;
    s_pendHdr = NULL;
}

int Snd_OnAsset(NrAsset* a)
{
    if (!s_on)
        return 0;
    if (a->type == ASSET_Cshd)
    {
        if (*(const uint16_t*)a->data != 4)
            return 0;
        for (int c = 0; c < 2; c++)
            if (!s_cshd[c])
            {
                s_cshd[c] = a;
                Bank_TryLoad();
                return 1;
            }
        return 0;
    }
    if (a->type == ASSET_Ceng)
        return Engine_OnCeng(a);   /* motores: los bancos se crean como Cvkh + Cvkb */
    if (a->type == ASSET_Cvkh || a->type == ASSET_Cvkb)
    {
        NrAsset** slot = a->type == ASSET_Cvkh ? &s_pendHdr : &s_pendBody;
        if (*slot)
            Asset_Free(*slot);
        *slot = a;
        Bank_TryLoad();
        return 1;
    }
    return 0;
}

/* 80022c58 */
static void Bank_Unload(int i)
{
    BankEntry* b = &s_banks[i];
    for (int d = 0; d < NUM_DEFS; d++)
        for (int k = s_defs[d].count - 1; k >= 0; k--)
            if (s_defs[d].patch[k]->bank == (uint8_t)b->bank)
                Def_RemovePatch(d, s_defs[d].patch[k]->prog);
    if (b->lib != -1)
        SndLib_FreeBank(b->lib);
    if (b->hdr)
        Asset_Free(b->hdr);
    b->lib = -1;
    b->bank = -1;
    b->hdr = NULL;
}

void Snd_UnloadBanks(void)
{
    if (!s_on)
        return;
    /* las voces apuntan a patches que desaparecen */
    for (int i = 0; i < NUM_VOICES; i++)
        Voice_Free(&s_voices[i]);
    for (int i = 0; i < NUM_BANKS; i++)
        if (s_banks[i].bank != -1)
            Bank_Unload(i);
    for (int c = 0; c < 2; c++)
        if (s_cshd[c])
        {
            Asset_Free(s_cshd[c]);
            s_cshd[c] = NULL;
        }
    if (s_pendHdr)
        Asset_Free(s_pendHdr);
    if (s_pendBody)
        Asset_Free(s_pendBody);
    s_pendHdr = s_pendBody = NULL;
    Defs_Reset();
}

/* 8003d04c */
void Snd_StopRace(void)
{
    for (int i = 0; i < 2; i++)
        if (s_pickupSrc[i] != -1)
        {
            Snd_SourceKill(s_pickupHandle[i], s_pickupSrc[i]);
            s_pickupSrc[i] = -1;
            s_pickupHandle[i] = 0;
        }
    for (int i = 0; i < 2; i++)
        if (s_timers[i].dur)
        {
            s_timers[i].dur = 0;
            Handle_Free(&s_timers[i].h);
        }
    /* 800400e4: todas las fuentes */
    while (s_srcHead)
        Snd_SourceKill(s_srcHead->handle, s_srcHead->index);
    Sources_Reset();
    s_queueCount = 0;
}

/* ---------------------------------------------------------------- init y pausa */

void Snd_Init(void)
{
    SndLib_Init();
    for (int i = 0; i < NUM_BANKS; i++)
    {
        s_banks[i].lib = -1;
        s_banks[i].bank = -1;
        s_banks[i].hdr = NULL;
    }
    s_cshd[0] = s_cshd[1] = NULL;
    /* buffers de streaming (musica 0x1010 + 0x17E00) y de las voces (0xC000) como en el original */
    SndLib_SpuAllocAt(0x1010, 0x17e00);
    Speech_SetSpuBuffer(SndLib_SpuAlloc(0xc000));
    s_on = 1;
    /* 8003cf48 */
    Snd_SetMode(1);
    Snd_SetListenerCount(0);
    Snd_SetSfxVolume(0x40);
    Snd_SetMusicVolume(0x40);
    Snd_SetSpeechVolume(0x7f);
    Snd_SetEngineVolume(0x40);
    Voices_Reset();
    Defs_Reset();
    Sources_Reset();
    s_raceActive = 0;
    SndLib_SetReverb(1);
}

/* 8003cfd0 (al empezar cada carrera): cuenta atras y fuentes de las cajas */
void Snd_RaceInit(void)
{
    s_countdownBeeps = 0;
    s_pickupSrc[0] = s_pickupSrc[1] = -1;
    s_pickupHandle[0] = s_pickupHandle[1] = 0;
    s_queueCount = 0;
}

/* 8003fe7c: 0 para todo (pausa, fin de nivel), 1 vuelve a activar las fuentes de la carrera */
void Snd_Pause(int on)
{
    Music_Pause(!on);
    if (on)
    {
        s_raceActive = 1;
        return;
    }
    s_raceActive = 0;
    Speech_ClearQueue();          /* 8002b4ac (la frase que suena sigue) */
    for (int i = 0; i < 2; i++)
        if (s_timers[i].dur)
        {
            s_timers[i].dur = 0;
            Handle_Free(&s_timers[i].h);
        }
    Engine_StopAll();
    while (s_list[1])
        Voice_Free(s_list[1]);
    while (s_list[0])
        Voice_Free(s_list[0]);
    SndLib_StopAll();
}

/* ---------------------------------------------------------------- por frame */

/* FUN_80042030: caja de poder disponible mas cercana (en celdas de TCRV de -7 a +7 de la camara) */
static Actor* Snd_NearestPickup(const SndListener* l)
{
    if (l->curve < 0)
        return NULL;
    Actor* best = NULL;
    int bestD = 0, cell = l->curve >> 2;
    for (Actor* a = g_Actors; a; a = a->next)
    {
        if (a->handle <= 0 || a->type != 0x16 || A16(a, 0xf4) >= 0 || a->tpos.seg == 0xff)
            continue;
        int dc = (a->tpos.curve >> 2) - cell;
        if (dc < -7 || dc > 7)
            continue;
        int v[2] = { a->obj.pos[0] - l->pos[0], a->obj.pos[2] - l->pos[2] };
        int d = Vec_ApproxLen2(v);
        if (!best || d < bestD)
        {
            best = a;
            bestD = d;
        }
    }
    return best;
}

static void Snd_UpdateSources(int view, int dt)
{
    (void)dt;
    const RaceCar* ent = g_ViewHumans[view];
    int maxv = g_SplitScreen ? 4 : 6;
    if (!s_srcCount || !s_raceActive)
        return;
    /* prioridad de cada fuente para esta vista y nuevo orden */
    SndSource* tmp[NUM_SOURCES];
    int n = 0;
    for (SndSource* s = s_srcTail; s; s = s->prev)
    {
        tmp[n++] = s;
        if (s->def[view] == 0xff || !Snd_Listen(s->x, s->z, &s->pan, ent, s->kind[view]))
            s->prio = 0;
        else
            s->prio = s->pan.vol + s_defs[s->def[view]].prio * 16;
    }
    s_srcHead = s_srcTail = NULL;
    s_srcCount = 0;
    for (int i = 0; i < n; i++)
        Source_Insert(tmp[i]);
    int playing = 0;
    for (SndSource* s = s_srcHead; s; s = s->next)
    {
        if (s->kind[view] != s->lastKind[view])
        {
            if (s->voice[view])
                Voice_Free(s->voice[view]);
            s->lastKind[view] = s->kind[view];
        }
        SndVoice* v = s->voice[view];
        if (playing >= maxv || !s->prio)
        {
            if (v)
                Voice_Free(v);
            continue;
        }
        int def = s->def[view];
        int fresh = v == NULL;
        if (fresh)
        {
            v = Voice_Alloc(s->pan.vol, s_defs[def].prio, 0, def);
            if (!v)
                continue;
            v->src = s;
            s->voice[view] = v;
        }
        else
            playing++;
        Snd_PanFinish(&s->pan);
        if (s->speed != -1 && Snd_IsEngine(s->kind[view]))
        {
            int p[3] = { s->x, s_lis[view].pos[1], s->z };
            v->doppler = Snd_Doppler(s_lis[view].vel, s_lis[view].pos, s->vel, s->speed, p);
        }
        if (s->volScale != 0x10000)
        {
            int vv = (s->pan.vol * s->volScale) >> 16;
            s->pan.vol = (int8_t)(vv < 0x80 ? vv : 0x7f);
        }
        if (fresh)
        {
            if (!Def_Play(def, v, s->pan.pan, s->pan.vol, s->pitch, s->patchSel, s->pan.reverb))
                Voice_Free(v);
            else
                playing++;
        }
        else
        {
            s->pan.vol = (int8_t)Snd_Approach(v->vol, s->pan.vol);
            Voice_Set(v, s->pan.pan, s->pan.vol, s->pitch, s->pan.reverb);
            if (v->prio != s->prio)
            {
                v->prio = s->prio;
                Voice_DefUnlink(v);
                Voice_DefLink(v);
            }
        }
    }
}

void Snd_Update(void)
{
    if (!s_on)
        return;
    /* DAT_800af714 suma 5 por vblank (tics de 1/300 s, como el tiempo de carrera); hasta 300 por frame */
    int dt = g_FrameDelta;
    if (dt < 0)
        dt = 0;
    else if (dt > 300)
        dt = 300;
    s_tick += (unsigned)dt;
    s_lastTick = (int)s_tick;
    /* cola de sonidos de la carrera (80042234) */
    for (int i = 0; i < s_queueCount; i++)
        Snd_PlayAt(s_queue[i].kind, s_queue[i].x, s_queue[i].z);
    s_queueCount = 0;
    /* sonidos con duracion */
    for (int i = 0; i < 2; i++)
    {
        SndTimer* t = &s_timers[i];
        if (!t->dur)
            continue;
        if (t->delay)
        {
            t->delay--;
            continue;
        }
        t->dur -= dt;
        if (t->dur < 1)
        {
            t->dur = 0;
            if (Handle_Valid(&t->h))
                Voice_Fade(t->h.v);
        }
    }
    /* prioridad de las voces de la lista 1 (bajan con el tiempo) */
    for (SndVoice* v = s_list[1]; v; v = v->next)
        if (v->prio > 0)
            v->prio -= dt;
    if (s_raceActive)
        Engine_UpdateAll(dt);
    int views = g_SplitScreen ? 2 : 1;
    for (int view = 0; view < views; view++)
        Snd_UpdateSources(view, dt);
    if (s_listeners && s_raceActive)
    {
        /* zumbido de la caja de poder disponible mas cercana a cada camara (sonido 6) */
        for (int view = 0; view < s_listeners && view < 2; view++)
        {
            Actor* a = Snd_NearestPickup(&s_lis[view]);
            if (!a)
            {
                if (s_pickupSrc[view] != -1)
                {
                    Snd_SourceKill(s_pickupHandle[view], s_pickupSrc[view]);
                    s_pickupSrc[view] = -1;
                    s_pickupHandle[view] = 0;
                }
                continue;
            }
            if (s_pickupSrc[view] != -1 && a->handle != s_pickupHandle[view])
                s_sources[s_pickupSrc[view]].handle = a->handle;
            if (s_pickupSrc[view] == -1)
            {
                s_pickupSrc[view] = Snd_SourceCreate(6, a->handle, a->obj.pos[0], a->obj.pos[2], -1, a->speed, a->vel);
                if (s_pickupSrc[view] != -1)
                    Snd_SourceSetVolume(a->handle, s_pickupSrc[view], 0x6000);
            }
            else
                Snd_SourceMove(a->handle, a->obj.pos[0], a->obj.pos[2], -1, a->speed, a->vel, s_pickupSrc[view]);
            s_pickupHandle[view] = a->handle;
        }
        /* pitidos de la cuenta atras: uno por segundo (0x26) y el de salida (0x27) */
        if (g_RaceTime < 0x4b0 && !g_RacePaused)
        {
            int n = 0;
            for (int t = g_RaceTime; t > 0; t -= 300)
                n++;
            if (s_countdownBeeps < n)
            {
                if (n == 4)
                    Snd_PlayTimed(0x27, 0x78);
                else
                    Snd_PlayTimed(0x26, 0x3c);
                s_countdownBeeps++;
            }
        }
    }
    Speech_Update();
    SndLib_Commit();
    Music_Update();
}
