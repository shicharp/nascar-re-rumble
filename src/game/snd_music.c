/* Musica del front-end (VAGM de FE.TRK) y de la carrera (VAGM del .AV de la sede).
 * Original: la musica llega por el mismo stream de CD que las voces (Stream_ProcessChunks 8002a85c, cola
 * DAT_800b1290 de 4 bloques) y la reproduce el streaming de la SPU (Music_Start 80023b68 -> 80021464: 14260 Hz
 * (0x37B4), bloques de 0x2FC0 bytes por canal, dos voces (80023d70: izquierda +0, derecha +0x2FC0) que se
 * rellenan con la interrupcion de la SPU (80021f38 / 800221f8)). PsyCross no emula esa interrupcion: en el port
 * se leen los bloques VAGM del fichero, se decodifican aqui (ADPCM de PS1, estado continuo por canal) y se
 * tocan con una fuente estereo de OpenAL. Bloque VAGM: +0x10 'VAGM', +0x14 u16 total, +0x16 u16 indice,
 * +0x18 u32 bytes por canal; datos desde +0x1C. Se repite al terminar. */
#include <stdlib.h>
#include <string.h>
#include "AL/al.h"
#include "nr.h"
#include "file.h"
#include "mem.h"
#include "snd.h"

#define MUSIC_RATE    14260
#define MUSIC_BUFS    4
#define STREAM_BLOCK  0x6000
#define CH_BYTES      0x2fc0
#define CH_SAMPLES    (CH_BYTES / 16 * 28)

static uint8_t* s_data;          /* n bloques x (izquierda, derecha) */
static int      s_count;
static int      s_next;
static double   s_hist[2][2];    /* estado del ADPCM por canal */
static ALuint   s_src, s_bufs[MUSIC_BUFS];
static int      s_alOk, s_playing, s_paused;
static float    s_gain = 0.4f;
static int      s_duck;          /* el comentarista habla: -1/8 (como las voces del streaming en 8003d708) */
static int16_t  s_pcm[CH_SAMPLES * 2];

static const double K0[5] = { 0.0, 0.9375, 1.796875, 1.53125, 1.90625 };
static const double K1[5] = { 0.0, 0.0, -0.8125, -0.859375, -0.9375 };

/* ADPCM de PS1: bloques de 16 bytes (filtro / desplazamiento, banderas, 28 muestras de 4 bits) */
static void Music_DecodeChannel(const uint8_t* in, int ch, int16_t* out, int stride)
{
    double s1 = s_hist[ch][0], s2 = s_hist[ch][1];
    for (int b = 0; b < CH_BYTES / 16; b++, in += 16)
    {
        int shift = in[0] & 0xf, f = (in[0] >> 4) % 5;
        for (int i = 0; i < 28; i++)
        {
            int nib = (in[2 + i / 2] >> ((i & 1) * 4)) & 0xf;
            int v = (int16_t)(nib << 12) >> shift;
            double r = v + s1 * K0[f] + s2 * K1[f];
            s2 = s1;
            s1 = r;
            int o = (int)r;
            *out = (int16_t)(o > 32767 ? 32767 : o < -32768 ? -32768 : o);
            out += stride;
        }
    }
    s_hist[ch][0] = s1;
    s_hist[ch][1] = s2;
}

static void Music_Fill(ALuint buf)
{
    if (s_next >= s_count)
    {
        s_next = 0;
        memset(s_hist, 0, sizeof(s_hist));
    }
    const uint8_t* blk = s_data + (size_t)s_next * CH_BYTES * 2;
    Music_DecodeChannel(blk, 0, s_pcm, 2);
    Music_DecodeChannel(blk + CH_BYTES, 1, s_pcm + 1, 2);
    s_next++;
    alBufferData(buf, AL_FORMAT_STEREO16, s_pcm, (ALsizei)sizeof(s_pcm), MUSIC_RATE);
}

void Music_Stop(void)
{
    if (s_alOk)
    {
        alSourceStop(s_src);
        alSourcei(s_src, AL_BUFFER, 0);
    }
    free(s_data);
    s_data = NULL;
    s_count = 0;
    s_playing = 0;
    s_paused = 0;
}

/* Lee del fichero (en bloques de stream de 0x6000) todos los bloques VAGM de la musica */
static int Music_Load(const char* path)
{
    void* file = NULL;
    int size = 0;
    if (File_LoadAlloc(path, &file, &size) < 0 || !file)
        return 0;
    const uint8_t* d = (const uint8_t*)file;
    int cap = 256;
    s_data = (uint8_t*)malloc((size_t)cap * CH_BYTES * 2);
    s_count = 0;
    for (int base = 0; base < size && s_data; base += STREAM_BLOCK)
    {
        int off = base, end = base + STREAM_BLOCK < size ? base + STREAM_BLOCK : size;
        while (off + 8 <= end)
        {
            uint32_t tag = NR_Read32(d + off), len = NR_Read32(d + off + 4);
            if ((tag == 0 && len == 0) || tag == NR_TAG('F', 'I', 'L', 'L') || len < 8 || off + (int)len > end)
                break;
            if (tag == NR_TAG('V', 'A', 'G', 'M') && len >= 0x1c + CH_BYTES * 2 &&
                NR_Read32(d + off + 0x18) == CH_BYTES && s_count < cap)
                memcpy(s_data + (size_t)s_count++ * CH_BYTES * 2, d + off + 0x1c, CH_BYTES * 2);
            off += (int)len;
        }
    }
    Pool_Free(file);
    if (!s_count)
    {
        free(s_data);
        s_data = NULL;
    }
    return s_count;
}

void Music_Play(const char* path)
{
    Music_Stop();
    if (!s_alOk)
    {
        alGetError();
        alGenSources(1, &s_src);
        alGenBuffers(MUSIC_BUFS, s_bufs);
        s_alOk = alGetError() == AL_NO_ERROR;
        if (!s_alOk)
            return;
        alSourcei(s_src, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(s_src, AL_POSITION, 0.f, 0.f, 0.f);
    }
    if (!Music_Load(path))
    {
        NR_Log("Musica: no hay bloques VAGM en %s\n", path);
        return;
    }
    NR_Log("Musica: %s, %d bloques (%d s)\n", path, s_count, s_count * CH_SAMPLES / MUSIC_RATE);
    s_next = 0;
    memset(s_hist, 0, sizeof(s_hist));
    for (int i = 0; i < MUSIC_BUFS; i++)
        Music_Fill(s_bufs[i]);
    alSourceQueueBuffers(s_src, MUSIC_BUFS, s_bufs);
    alSourcef(s_src, AL_GAIN, s_gain);
    alSourcePlay(s_src);
    s_playing = 1;
}

void Music_Pause(int on)
{
    if (!s_playing || !s_alOk || s_paused == on)
        return;
    s_paused = on;
    if (on)
        alSourcePause(s_src);
    else
        alSourcePlay(s_src);
}

/* volumen de la musica (0..127 efectivo: el de 8003d5dc); las voces del streaming van a todo el lado */
void Music_SetVolume(int vol)
{
    s_gain = (float)(vol * 0x7f) / 16384.0f;
    if (s_alOk)
        alSourcef(s_src, AL_GAIN, s_duck ? s_gain * 0.875f : s_gain);
}

void Music_SetDuck(int on)
{
    s_duck = on != 0;
    if (s_alOk)
        alSourcef(s_src, AL_GAIN, s_duck ? s_gain * 0.875f : s_gain);
}

/* Cada frame: rellena los buffers que ya han sonado */
void Music_Update(void)
{
    if (!s_playing || s_paused || !s_alOk)
        return;
    ALint done = 0;
    alGetSourcei(s_src, AL_BUFFERS_PROCESSED, &done);
    while (done-- > 0)
    {
        ALuint b;
        alSourceUnqueueBuffers(s_src, 1, &b);
        Music_Fill(b);
        alSourceQueueBuffers(s_src, 1, &b);
    }
    ALint st;
    alGetSourcei(s_src, AL_SOURCE_STATE, &st);
    if (st != AL_PLAYING)
        alSourcePlay(s_src);   /* si se quedo sin buffers (un frame muy largo) */
}
