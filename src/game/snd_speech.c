/* Comentarista: frases del .AV de la sede (una seccion de stream con un bloque VAGB cada una).
 * Original: la tabla de secciones de la pista (Ctos, Stream_OnCtos 8002b810: {offset, tamano, categoria} x n)
 * agrupa las frases en 55 categorias (DAT_800b0f40: primera, cuantas, la ultima elegida, cuando sono);
 * elegir 8002b920(categoria, espera: no repite la ultima ni suena antes de espera * 256 tics * multiplicador de la
 * clase 80010de0), encolar 8002b0f4 (8 huecos; si no se fuerza, solo con la cola vacia y sin frase sonando),
 * sacar de la cola 8002b4c0, el stream lee la seccion y sus VAGB van al buffer de voz de la SPU (80022e50, 0xC000
 * bytes), y 800233e8 / 80023258 la tocan con una voz de streaming a 0x400 (11025 Hz) con el volumen de las voces;
 * mientras suena los demas sonidos bajan 1/8 (8003d708 / 8003d7ac); dura bytes / 0x15 tics (80022f24 / 80023088).
 * Frases pegadas a una entidad (codigos 0xE / 0xF de los guiones, 8002b2d8: sonidos de animales y otros de la
 * pista): al sacarlas, 8003d288 apunta la entidad y su numero; mientras suenan, el volumen y el panorama salen de
 * su posicion (8003d320, cada frame en 80022f24) sin bajar los demas sonidos, y se cortan si la entidad desaparece.
 * En el port la seccion se lee directamente del .AV (sin el stream de CD compartido con la musica). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nr.h"
#include "mem.h"
#include "file.h"
#include "exe.h"
#include "game.h"
#include "globals.h"
#include "frontend.h"
#include "racecar.h"
#include "snd.h"
#include "snd_int.h"
#include "snd_lib.h"
#include "carphys_int.h"
#include "pause.h"
#include "actor.h"

#define NUM_CATS 0x37

typedef struct
{
    int16_t first, count, last, lastTime;
} SpeechCat;                    /* DAT_800b0f40 */

typedef struct
{
    int section;
    const void* entity;         /* +8 coche (pantalla partida) */
    int attach;                 /* +2 / +4: numero de la entidad a la que va pegada (0 ninguna) */
    int x, z;                   /* solo en el port: donde estaba al encolar */
} SpeechReq;                    /* DAT_800b1164 (8 x 0xC) */

static SpeechCat s_cats[NUM_CATS];
static const uint8_t* s_toc;    /* DAT_800af768 */
static int        s_tocCount;
static SpeechReq  s_queue[8];
static int        s_qHead, s_qCount;   /* DAT_800b1162 / 1160 */
static SndHandle  s_voice;              /* DAT_800b09e8 */
static int        s_playing;            /* DAT_800b09f0 */
static unsigned   s_endTime;            /* DAT_800b09f8 */
static int        s_spuAddr = -1;       /* DAT_800af14c */
static uint8_t*   s_clip;
static int        s_attach;             /* DAT_800b0a00 + DAT_800b3ad4 / 3ad8: entidad de la frase que suena */
static int        s_attachX, s_attachZ; /* (-1 en s_attach: la entidad ya no existia al empezar, suena donde estaba) */

void Speech_Stop(void);

void Speech_SetSpuBuffer(int addr)
{
    s_spuAddr = addr;
}

/* 8002b810 (parte): categorias de la tabla de secciones de la pista */
void Speech_SetToc(const uint8_t* toc, int size)
{
    for (int i = 0; i < NUM_CATS; i++)
    {
        s_cats[i].first = s_cats[i].count = 0;
        s_cats[i].last = -1;
        s_cats[i].lastTime = (int16_t)0xd8f0;
    }
    s_toc = NULL;
    s_tocCount = 0;
    if (!toc || size < 16)
        return;
    s_toc = toc + 16;
    s_tocCount = (int)NR_Read32(toc + 12);
    if (16 + s_tocCount * 12 > size)
        s_tocCount = (size - 16) / 12;
    for (int i = 0; i < s_tocCount; i++)
    {
        uint32_t cat = NR_Read32(s_toc + i * 12 + 8);
        if (cat >= NUM_CATS)
            continue;
        if (s_cats[cat].count == 0)
            s_cats[cat].first = (int16_t)i;
        s_cats[cat].count++;
    }
}

/* 8002b920 */
int Speech_Pick(int cat, int wait)
{
    if ((unsigned)cat >= NUM_CATS || s_cats[cat].count < 1)
        return -1;
    SpeechCat* c = &s_cats[cat];
    int cls = g_GameInfo ? g_GameInfo->racingClass & 3 : 0;
    int mult = Exe_S32(0x80010de0u + (uint32_t)cls * 4);
    if ((g_RaceTime >> 8) - c->lastTime < (wait * mult) >> 16)
        return -1;
    c->lastTime = (int16_t)(g_RaceTime >> 8);
    int k = 0;
    if (c->count != 1)
    {
        k = (int)((unsigned)Rand_Fx() % (unsigned)c->count);
        if (k == c->last && ++k == c->count)
            k = 0;
    }
    c->last = (int16_t)k;
    return c->first + k;
}

/* 8002b0f4 */
int Speech_Queue(int section, const void* entity, int onlyIfIdle)
{
    if (section < 0)
        return 0;
    if (onlyIfIdle && (s_qCount > 0 || s_playing))
        return 0;
    if (g_GameState >= 5 || Snd_SpeechVolume() == 0 || s_qCount > 7)
        return 0;
    SpeechReq* r = &s_queue[(s_qHead + s_qCount) & 7];
    r->section = section;
    r->entity = entity;
    r->attach = 0;
    s_qCount++;
    return 1;
}

/* 8002b2d8: frase pegada a la entidad (codigos 0xE / 0xF de los guiones). Sin 'force', solo con la cola vacia y
 * sin frase sonando; con 'force' y cola, corta la que suena (8002b7c8) y se pone la primera. */
int Speech_QueueOnEntity(int section, const void* ent, int force)
{
    if (section < 0 || !ent)
        return 0;
    if (!force && (s_qCount > 0 || s_playing))
        return 0;
    if (g_GameState > 4)
        return 0;
    SpeechReq* r;
    if (!force || s_qCount < 1)
    {
        if (s_qCount > 7)
            return 0;
        r = &s_queue[(s_qHead + s_qCount) & 7];
        s_qCount++;
    }
    else
    {
        Speech_Stop();
        s_qHead = (s_qHead + 7) & 7;
        r = &s_queue[s_qHead];
        if (s_qCount < 8)
            s_qCount++;
    }
    r->section = section;
    r->entity = NULL;
    r->attach = *(const int*)((const uint8_t*)ent + 0xc);
    r->x = *(const int*)((const uint8_t*)ent + 0x58);
    r->z = *(const int*)((const uint8_t*)ent + 0x60);
    return 1;
}

/* 8003d3ec + 8003d320: la entidad de la frase (si sigue viva) y su volumen y panorama. Si murio antes de empezar
 * la frase, el original la da por buena (su numero ya era 0 al sacarla) y suena en su ultima posicion; si muere
 * mientras suena, se corta. */
static int Speech_AttachedPan(SndPan* o)
{
    if (s_attach == -1)
    {
        Snd_SpeechAt(s_attachX, s_attachZ, NULL, o);
        return 1;
    }
    const uint8_t* e = s_attach ? Ent_ByHandle(s_attach) : NULL;
    if (!e)
        return 0;
    s_attachX = *(const int*)(e + 0x58);
    s_attachZ = *(const int*)(e + 0x60);
    Snd_SpeechAt(s_attachX, s_attachZ, NULL, o);
    return 1;
}

void Speech_Say(int cat, int wait, const void* entity, int onlyIfIdle)
{
    int s = Speech_Pick(cat, wait);
    if (s >= 0)
        Speech_Queue(s, entity, onlyIfIdle);
}

/* 80023088 */
void Speech_Stop(void)
{
    if (s_playing)
    {
        if (Handle_Valid(&s_voice))
            SndLib_ReleaseRaw(s_voice.v->lib);
        s_playing = 0;
        if (!s_attach)
            Snd_Duck(0);   /* 8003d7ac */
    }
    Handle_Free(&s_voice);
    s_attach = 0;
}

void Speech_ClearQueue(void)
{
    s_qHead = s_qCount = 0;   /* 8002b4ac */
}

void Speech_Reset(void)
{
    Speech_Stop();
    Speech_ClearQueue();
}

/* Lee la seccion del .AV y junta sus VAGB (por indice) */
static int Speech_LoadClip(int section)
{
    if (!s_toc || section < 0 || section >= s_tocCount)
        return 0;
    int off = (int)NR_Read32(s_toc + section * 12), size = (int)NR_Read32(s_toc + section * 12 + 4);
    char path[64];
    snprintf(path, sizeof(path), "%s%s.AV", Level_GetDataDir(), Fe_VenueCode(g_Venue));
    int f = File_Open(path, NULL);
    if (f < 0)
        return 0;
    uint8_t* raw = (uint8_t*)malloc((size_t)size);
    int got = 0;
    if (raw && File_Seek(f, off) >= 0)
        got = File_ReadAsync(f, raw, size, NULL) >= 0 ? size : 0;
    File_Close(f);
    if (!got)
    {
        free(raw);
        return 0;
    }
    free(s_clip);
    s_clip = (uint8_t*)malloc(0xc000);
    int bytes = 0;
    for (int b = 0; b < size && s_clip; b += 0x6000)
    {
        int o = b, end = b + 0x6000 < size ? b + 0x6000 : size;
        while (o + 8 <= end)
        {
            uint32_t tag = NR_Read32(raw + o), len = NR_Read32(raw + o + 4);
            if (len < 8 || o + (int)len > end || (tag == 0 && len == 0))
                break;
            if (tag == NR_TAG('V', 'A', 'G', 'B') && len > 0x18)
            {
                int n = (int)len - 0x18;
                if (bytes + n > 0xc000)
                    n = 0xc000 - bytes;
                memcpy(s_clip + bytes, raw + o + 0x18, (size_t)n);
                bytes += n;
            }
            o += (int)len;
        }
    }
    free(raw);
    if (bytes > 0)
        SndLib_SpuWrite(s_spuAddr, s_clip, (bytes + 0x3f) & ~0x3f);
    return bytes;
}

/* 8002b4c0 + 800233e8 + 80023258: saca la siguiente frase y la toca */
static void Speech_Start(void)
{
    SpeechReq r = s_queue[s_qHead];
    s_qHead = (s_qHead + 1) & 7;
    s_qCount--;
    if (s_spuAddr < 0)
        return;
    int bytes = Speech_LoadClip(r.section);
    if (bytes <= 0)
        return;
    SndVoice* v = Voice_Alloc(0x7f, 10, 0, 0xff);
    if (!v)
        return;
    Handle_Set(&s_voice, v);
    s_attach = r.attach;   /* 8003d288 / 800233e8 */
    s_attachX = r.x;
    s_attachZ = r.z;
    if (s_attach && !Ent_ByHandle(s_attach))
        s_attach = -1;
    int vol = Snd_SpeechVolume(), pan = 0x40;
    if (s_attach)
    {
        /* pegada a una entidad: como un sonido de la pista (sin bajar los demas); si ya no existe, no suena */
        SndPan o;
        if (!Speech_AttachedPan(&o))
        {
            Handle_Free(&s_voice);
            s_attach = 0;
            return;
        }
        vol = o.vol;
        pan = o.pan;
    }
    else
    {
        if (g_SplitScreen && r.entity)
        {
            const RaceCar* e = (const RaceCar*)r.entity;
            pan = e->ctrl && e->ctrl[0x59] == 0 ? 0 : 0x7f;   /* 8003d220 */
        }
        if (vol)
            Snd_Duck(1);   /* 8003d708 */
    }
    SndLibParams p = { 0x400, vol, pan };
    if (SndLib_PlayRaw(v->lib, &p, s_spuAddr) < 0)
    {
        Handle_Free(&s_voice);
        if (!s_attach)
            Snd_Duck(0);
        return;
    }
    SndLib_KeyOn(v->lib);
    int dur = bytes / 0x15;
    if (dur > 3000)
        dur = 0x834;
    s_endTime = s_tick + (unsigned)dur;
    s_playing = 1;
    if (Snd_LogOn())
        NR_Log("[snd] comentarista: seccion %d (%d bytes, %d tics) vol %d pan %d%s\n", r.section, bytes, dur, vol, pan,
               s_attach ? " (pegada a una entidad)" : "");
}

/* 80022f24 (y la parte de 8002be4c que saca la siguiente) */
void Speech_Update(void)
{
    if (s_playing && (int)(s_tick - s_endTime) > 0)
        Speech_Stop();
    if (s_playing && s_attach)
    {
        /* la frase pegada sigue a su entidad; si desaparece, se corta */
        SndPan o;
        if (!Speech_AttachedPan(&o) || !Handle_Valid(&s_voice))
            Speech_Stop();
        else
        {
            SndLibParams p = { 0x400, o.vol, o.pan };
            SndLib_Update(s_voice.v->lib, &p);
        }
    }
    if (!s_playing && s_qCount > 0 && !g_RacePaused)
        Speech_Start();
}
