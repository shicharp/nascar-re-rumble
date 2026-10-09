/* Biblioteca de sonido de bajo nivel (ver snd_lib.h). Las subidas a la SPU son sincronas (el original las
 * encola y espera a la interrupcion de fin de DMA) y la memoria de la SPU la lleva el port: el SpuMalloc de
 * PsyCross solo avanza y SpuFree lo vacia todo. */
#include <string.h>
#include "libspu.h"
#include "nr.h"
#include "snd_lib.h"

/* ---------------------------------------------------------------- memoria de la SPU */

#define SPU_RAM_SIZE  0x80000
#define SPU_RAM_START 0x1010       /* lo de antes es del sistema (y PsyCross no deja escribir ahi) */
#define SPU_BLOCKS    32           /* SpuInitMalloc(0x20, ...) */

typedef struct
{
    int addr, size, used;
} SpuBlock;

static SpuBlock s_blocks[SPU_BLOCKS];
static int      s_numBlocks;

static void Spu_ResetHeap(void)
{
    s_numBlocks = 1;
    s_blocks[0].addr = SPU_RAM_START;
    s_blocks[0].size = SPU_RAM_SIZE - SPU_RAM_START;
    s_blocks[0].used = 0;
}

/* parte el bloque libre i para reservar [addr, addr + size) */
static int Spu_Carve(int i, int addr, int size)
{
    SpuBlock b = s_blocks[i];
    if (addr < b.addr || addr + size > b.addr + b.size || b.used || s_numBlocks + 2 > SPU_BLOCKS)
        return -1;
    SpuBlock parts[3];
    int n = 0;
    if (addr > b.addr)
    {
        parts[n].addr = b.addr;
        parts[n].size = addr - b.addr;
        parts[n++].used = 0;
    }
    parts[n].addr = addr;
    parts[n].size = size;
    parts[n++].used = 1;
    if (addr + size < b.addr + b.size)
    {
        parts[n].addr = addr + size;
        parts[n].size = b.addr + b.size - (addr + size);
        parts[n++].used = 0;
    }
    memmove(&s_blocks[i + n], &s_blocks[i + 1], (size_t)(s_numBlocks - i - 1) * sizeof(SpuBlock));
    memcpy(&s_blocks[i], parts, (size_t)n * sizeof(SpuBlock));
    s_numBlocks += n - 1;
    return addr;
}

int SndLib_SpuAlloc(int size)
{
    size = (size + 0x3f) & ~0x3f;
    for (int i = 0; i < s_numBlocks; i++)
        if (!s_blocks[i].used && s_blocks[i].size >= size)
            return Spu_Carve(i, s_blocks[i].addr, size);
    return -1;
}

int SndLib_SpuAllocAt(int addr, int size)
{
    for (int i = 0; i < s_numBlocks; i++)
        if (addr >= s_blocks[i].addr && addr < s_blocks[i].addr + s_blocks[i].size)
            return Spu_Carve(i, addr, size);
    return -1;
}

void SndLib_SpuFree(int addr)
{
    for (int i = 0; i < s_numBlocks; i++)
    {
        if (s_blocks[i].addr != addr || !s_blocks[i].used)
            continue;
        s_blocks[i].used = 0;
        /* junta los huecos libres vecinos */
        if (i + 1 < s_numBlocks && !s_blocks[i + 1].used)
        {
            s_blocks[i].size += s_blocks[i + 1].size;
            memmove(&s_blocks[i + 1], &s_blocks[i + 2], (size_t)(s_numBlocks - i - 2) * sizeof(SpuBlock));
            s_numBlocks--;
        }
        if (i > 0 && !s_blocks[i - 1].used)
        {
            s_blocks[i - 1].size += s_blocks[i].size;
            memmove(&s_blocks[i], &s_blocks[i + 1], (size_t)(s_numBlocks - i - 1) * sizeof(SpuBlock));
            s_numBlocks--;
        }
        return;
    }
}

void SndLib_SpuWrite(int addr, const void* data, int size)
{
    if (addr < SPU_RAM_START || size <= 0 || addr + size > SPU_RAM_SIZE)
        return;
    SpuSetTransferStartAddr((unsigned int)addr);
    SpuWrite((unsigned char*)data, (unsigned int)size);
}

/* ---------------------------------------------------------------- bancos y voces */

#define PATCH_SIZE 0x14
#define RD16(p, o) (*(const uint16_t*)((const uint8_t*)(p) + (o)))
#define RD32(p, o) (*(const uint32_t*)((const uint8_t*)(p) + (o)))

typedef struct
{
    const uint8_t* patch;   /* +0x00 */
    int unk04;
    int state;              /* +0x08 */
    int voice;              /* +0x0C numero de voz de la SPU */
    int pitch;              /* +0x10 */
    int volL, volR;         /* +0x14 / +0x18 */
} LibVoice;

/* patch falso de las voces de streaming (DAT_800b0710: +1 / +2 volumenes, +0xA / +0xC ADSR, +0x10 direccion) */
typedef struct
{
    uint8_t  patch[PATCH_SIZE];
    int      used;
    int      voice;
} RawSlot;

static uint8_t* s_banks[SNDLIB_BANKS];   /* DAT_800b0a08 (via DAT_800b06ec) */
static LibVoice s_voices[SNDLIB_VOICES]; /* DAT_800b0a30 (via DAT_800b06f4) */
static RawSlot  s_raw[3];                /* DAT_800b0710 */
static uint32_t s_keyOn, s_keyOff;       /* DAT_800b06fc / DAT_800b0700 */
static uint32_t s_revOn, s_revOff;       /* DAT_800b0704 / DAT_800b0708 */
static int      s_mono;                  /* DAT_800b0764 */
static int      s_master = 0x7f;         /* DAT_800b0760 */
static int      s_inited;

void SndLib_Init(void)
{
    if (!s_inited)
    {
        SpuInit();
        s_inited = 1;
    }
    Spu_ResetHeap();
    memset(s_banks, 0, sizeof(s_banks));
    memset(s_voices, 0, sizeof(s_voices));
    memset(s_raw, 0, sizeof(s_raw));
    for (int i = 0; i < SNDLIB_VOICES; i++)
        s_voices[i].voice = i;
    s_keyOn = s_keyOff = s_revOn = s_revOff = 0;
    s_mono = 0;
    s_master = 0x7f;
    SpuSetCommonMasterVolume(0x3fff, 0x3fff);   /* 800205f4 */
    SpuSetReverb(SPU_OFF);
}

const uint8_t* SndLib_Program(int slot, int prog)
{
    if (slot < 0 || slot >= SNDLIB_BANKS || !s_banks[slot] || prog < 0 || prog >= s_banks[slot][4])
        return NULL;
    uint32_t off = RD32(s_banks[slot], 0x10 + prog * 4);
    return off ? s_banks[slot] + off : NULL;
}

int SndLib_LoadBank(uint8_t* hdr, const uint8_t* body)
{
    int slot = 0;
    while (slot < SNDLIB_BANKS && s_banks[slot])
        slot++;
    if (slot == SNDLIB_BANKS)
        return -1;
    int size = (int)RD32(hdr, 0xc);
    int addr = SndLib_SpuAlloc((size + 0x3f) & ~0x3f);
    if (addr < 0)
        return -3;
    SndLib_SpuWrite(addr, body, size);
    *(int32_t*)(hdr + 8) = addr;
    /* 800207bc: las direcciones de los patches pasan a ser absolutas */
    for (int p = 0; p < hdr[4]; p++)
    {
        uint32_t off = RD32(hdr, 0x10 + p * 4);
        if (!off)
            continue;
        uint8_t* prog = hdr + off;
        for (int k = 0; k < prog[0]; k++)
            *(uint32_t*)(prog + 4 + k * PATCH_SIZE + 0x10) += (uint32_t)addr;
    }
    s_banks[slot] = hdr;
    return slot;
}

void SndLib_FreeBank(int slot)
{
    if (slot < 0 || slot >= SNDLIB_BANKS || !s_banks[slot])
        return;
    SndLib_SpuFree(*(int32_t*)(s_banks[slot] + 8));
    *(int32_t*)(s_banks[slot] + 8) = -1;
    s_banks[slot] = NULL;
}

int SndLib_PatchPitch(int slot, int prog)
{
    const uint8_t* p = SndLib_Program(slot, prog);
    return p ? RD16(p, 4 + 0xe) : -1;
}

/* 800212c4: volumen (0..127) y panoramica a izquierda / derecha (por el volumen del patch y el general) */
static void Lib_VolPan(const uint8_t* patch, int vol, int pan, int* outL, int* outR)
{
    pan &= 0xff;
    int l = vol, r = vol;
    if (!s_mono)
    {
        int p = pan > 0x80 ? 0x100 - pan : pan;
        int t = (p - 0x40) * vol, d = t >> 6;
        l = d > 0 ? vol - d : vol;
        r = t < 0 ? vol + d : vol;
        if (pan > 0x80)
            l = -l;   /* Dolby: fase invertida */
    }
    if (patch)
    {
        l = (l * patch[1]) >> 7;
        r = (r * patch[2]) >> 7;
    }
    *outL = l * s_master;
    *outR = r * s_master;
}

/* PsyCross no admite volumenes negativos (la fase invertida del modo Dolby): se toma el valor absoluto */
static short Spu_Vol(int v)
{
    if (v < 0)
        v = -v;
    return (short)(v > 0x3fff ? 0x3fff : v);
}

int SndLib_Play(int slot, int prog, int patch, int voice, const SndLibParams* p)
{
    const uint8_t* pr = SndLib_Program(slot, prog);
    if (!pr || voice < 0 || voice >= SNDLIB_VOICES || patch >= pr[0])
        return 0;
    LibVoice* v = &s_voices[voice];
    v->patch = pr + 4 + patch * PATCH_SIZE;
    Lib_VolPan(v->patch, p->vol, p->pan, &v->volL, &v->volR);
    v->pitch = p->pitch == -1 ? RD16(v->patch, 0xe) : p->pitch;
    s_keyOn |= 1u << voice;
    return 1;
}

int SndLib_PlayRaw(int voice, const SndLibParams* p, int spuAddr)
{
    if (voice < 0 || voice >= SNDLIB_VOICES)
        return -1;
    for (int i = 0; i < 3; i++)
    {
        RawSlot* r = &s_raw[i];
        if (r->used)
            continue;
        r->used = 1;
        r->voice = voice;
        memset(r->patch, 0, sizeof(r->patch));
        r->patch[1] = r->patch[2] = 0x7f;
        *(uint16_t*)(r->patch + 0xa) = 0x80ff;
        *(uint16_t*)(r->patch + 0xc) = 0x5fce;
        *(uint32_t*)(r->patch + 0x10) = (uint32_t)spuAddr;
        LibVoice* v = &s_voices[voice];
        v->patch = r->patch;
        int l, rr;
        Lib_VolPan(NULL, p->vol, p->pan, &l, &rr);
        v->pitch = p->pitch;
        v->volL = l;
        v->volR = rr;
        return voice;
    }
    s_voices[voice].patch = NULL;
    return -1;
}

void SndLib_ReleaseRaw(int voice)
{
    for (int i = 0; i < 3; i++)
        if (s_raw[i].used && s_raw[i].voice == voice)
        {
            SndLib_Stop(voice);
            s_raw[i].used = 0;
            s_raw[i].voice = -1;
        }
}

void SndLib_KeyOn(int voice)
{
    if (voice >= 0 && voice < SNDLIB_VOICES)
        s_keyOn |= 1u << voice;
}

void SndLib_Update(int voice, const SndLibParams* p)
{
    if (voice < 0 || voice >= SNDLIB_VOICES)
        return;
    LibVoice* v = &s_voices[voice];
    int l, r;
    Lib_VolPan(v->patch, p->vol, p->pan, &l, &r);
    SpuVoiceAttr a;
    memset(&a, 0, sizeof(a));
    a.voice = SPU_VOICECH(voice);
    if (p->pitch >= 0)
    {
        a.mask |= SPU_VOICE_PITCH;
        a.pitch = (unsigned short)p->pitch;
    }
    if (l >= 0 || r >= 0)
    {
        a.mask |= SPU_VOICE_VOLL | SPU_VOICE_VOLR;
        a.volume.left = Spu_Vol(l);
        a.volume.right = Spu_Vol(r);
    }
    SpuSetVoiceAttr(&a);
}

void SndLib_Stop(int voice)
{
    uint32_t bit = 1u << voice;
    s_keyOn &= ~bit;
    s_keyOff |= bit;
}

void SndLib_StopAll(void)
{
    SpuVoiceAttr a;
    memset(&a, 0, sizeof(a));
    a.voice = 0xffffff;
    a.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_PITCH;
    SpuSetVoiceAttr(&a);
}

void SndLib_StopAllBut(int voice)
{
    SpuVoiceAttr a;
    memset(&a, 0, sizeof(a));
    a.voice = ~(1u << voice) & 0xffffff;
    a.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_PITCH;
    SpuSetVoiceAttr(&a);
}

void SndLib_ReverbOn(int voice)
{
    s_revOn |= 1u << voice;
}

void SndLib_ReverbOff(int voice)
{
    uint32_t bit = 1u << voice;
    s_revOn &= ~bit;
    s_revOff |= bit;
}

void SndLib_SetMono(int mono)
{
    s_mono = mono != 0;
}

void SndLib_SetReverb(int on)
{
    SpuSetReverb(on ? SPU_ON : SPU_OFF);
    if (on)
        SpuSetReverbVoice(SPU_OFF, 0xffffff);
}

void SndLib_SetReverbDepth(int l, int r)
{
    (void)l;   /* PsyCross no implementa la profundidad (SpuSetReverbDepth) */
    (void)r;
}

/* 800208fc: atributos de la voz al pulsarla (muestra, ADSR, volumen y tono) */
static void Lib_SetupVoice(const LibVoice* v)
{
    SpuVoiceAttr a;
    memset(&a, 0, sizeof(a));
    a.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_PITCH | SPU_VOICE_WDSA | SPU_VOICE_LSAX |
             SPU_VOICE_ADSR_ADSR1 | SPU_VOICE_ADSR_ADSR2;
    a.voice = SPU_VOICECH(v->voice);
    a.addr = RD32(v->patch, 0x10);
    a.loop_addr = a.addr;
    a.adsr1 = RD16(v->patch, 0xa);
    a.adsr2 = RD16(v->patch, 0xc);
    a.volume.left = Spu_Vol(v->volL);
    a.volume.right = Spu_Vol(v->volR);
    a.pitch = (unsigned short)v->pitch;
    SpuSetVoiceAttr(&a);
}

void SndLib_Commit(void)
{
    if (s_keyOff)
    {
        SpuSetKey(SPU_OFF, s_keyOff);
        s_keyOff = 0;
    }
    if (s_keyOn)
    {
        for (int i = 0; i < SNDLIB_VOICES; i++)
        {
            if ((s_keyOn >> i) & 1 && s_voices[i].patch)
                Lib_SetupVoice(&s_voices[i]);
            s_voices[i].state = 2;
        }
        SpuSetKey(SPU_ON, s_keyOn);
    }
    if (s_revOff)
    {
        SpuSetReverbVoice(SPU_OFF, s_revOff);
        s_revOff = 0;
    }
    if (s_revOn)
    {
        SpuSetReverbVoice(SPU_ON, s_revOn);
        s_revOn = 0;
    }
    s_keyOn = 0;
}
