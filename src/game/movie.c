/* Videos WVE de EA (Movie_Play 800991e4 -> 80097b68 -> bucle 8009743c).
 *
 * El fichero es una serie de bloques {etiqueta, u32 tamaño big-endian (con la cabecera), datos}:
 *  - VLC0: 0xE0 u16, los valores de los codigos VLC de este video (80098ee0; ver mdec.c).
 *  - MDEC (y MDC2): un fotograma. +8 u16 ancho y +0xA u16 alto (big-endian), +0xC u32 numero, +0x10 el bitstream
 *    con su cabecera BS (Mdec_DecodeVlc 8009905c + DecDCTin / DecDCTout). 320x224 a 15 fotogramas por segundo.
 *  - au00 / au01 (el ultimo): audio estereo, ADPCM de la SPU en bloques de 15 bytes (sin el byte de banderas, que
 *    80096d7c añade al copiarlos a la SPU): ((tamaño - 0x10) / 30) * 15 bytes del canal izquierdo desde +0x10 y
 *    otros tantos del derecho. 22050 Hz: 1470 muestras por fotograma.
 *  - ad10 / ad11 / ad20 / ad21 / Ad10 / Ad11: otros formatos de audio (80096a20); no salen en este disco.
 * El original los lee del CD en un anillo, pone cada fotograma cuando el reloj del audio de la SPU llega a su
 * hora (80096994: fotograma << 10) y lo corta X o START a partir del cuarto fotograma.
 * En el port: el fichero entero en memoria, el MDEC por software (mdec.c), los fotogramas a la zona del fondo
 * (mitad izquierda en (0x140, y), derecha en (0x140, 0x100 + y), como las pantallas .LSC) y el audio por una
 * fuente de OpenAL; cada fotograma espera a que el audio llegue a su muestra. */
#include <stdlib.h>
#include <string.h>
#include "AL/al.h"
#include "libgte.h"
#include "libgpu.h"
#include "nr.h"
#include "file.h"
#include "gfx.h"
#include "pad.h"
#include "globals.h"
#include "frontend.h"
#include "game.h"
#include "mdec.h"

#define MOVIE_RATE     22050
#define MOVIE_FPS      15
#define MOVIE_MAXW     320
#define MOVIE_MAXH     240
#define MOVIE_ALBUFS   8

static const double K0[5] = { 0.0, 0.9375, 1.796875, 1.53125, 1.90625 };
static const double K1[5] = { 0.0, 0.0, -0.8125, -0.859375, -0.9375 };

typedef struct
{
    ALuint   src, bufs[MOVIE_ALBUFS];
    int      ok, started;
    int      queued;                 /* buffers en la cola */
    int      bufSamples[MOVIE_ALBUFS];
    int      head;                   /* el mas antiguo de la cola (indice en bufs) */
    long     donePlayed;             /* muestras de los buffers ya tocados */
    double   hist[2][2];             /* estado del ADPCM por canal */
} MovieAudio;

static uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static int      Be16(const uint8_t* p) { return p[0] << 8 | p[1]; }

/* ADPCM de la SPU: bloques de 15 bytes (filtro / desplazamiento y 28 muestras de 4 bits) */
static void DecodeAdpcm15(const uint8_t* in, int blocks, double* h, int16_t* out)
{
    double s1 = h[0], s2 = h[1];
    for (int b = 0; b < blocks; b++, in += 15)
    {
        int shift = in[0] & 0xf, f = (in[0] >> 4) % 5;
        for (int i = 0; i < 28; i++)
        {
            int nib = (in[1 + i / 2] >> ((i & 1) * 4)) & 0xf;
            double r = (int16_t)(nib << 12) >> shift;
            r += s1 * K0[f] + s2 * K1[f];
            s2 = s1;
            s1 = r;
            int o = (int)r;
            *out = (int16_t)(o > 32767 ? 32767 : o < -32768 ? -32768 : o);
            out += 2;
        }
    }
    h[0] = s1;
    h[1] = s2;
}

/* muestras ya tocadas (reloj del video) */
static long AudioPos(MovieAudio* a)
{
    if (!a->ok || !a->started)
        return -1;
    ALint processed = 0, offset = 0;
    alGetSourcei(a->src, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0)
    {
        ALuint b;
        alSourceUnqueueBuffers(a->src, 1, &b);
        a->donePlayed += a->bufSamples[a->head];
        a->head = (a->head + 1) % MOVIE_ALBUFS;
        a->queued--;
    }
    alGetSourcei(a->src, AL_SAMPLE_OFFSET, &offset);
    ALint state = 0;
    alGetSourcei(a->src, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING && a->queued > 0)
        alSourcePlay(a->src);   /* se habia quedado sin datos */
    return a->donePlayed + offset;
}

static void AudioQueue(MovieAudio* a, const uint8_t* chunk, uint32_t size)
{
    if (!a->ok || size <= 0x10)
        return;
    int n = (int)((size - 0x10) / 30);   /* bloques por canal */
    int samples = n * 28;
    int16_t* pcm = (int16_t*)malloc((size_t)samples * 4);
    if (!pcm)
        return;
    DecodeAdpcm15(chunk + 0x10, n, a->hist[0], pcm);
    DecodeAdpcm15(chunk + 0x10 + n * 15, n, a->hist[1], pcm + 1);
    int slot = (a->head + a->queued) % MOVIE_ALBUFS;
    alBufferData(a->bufs[slot], AL_FORMAT_STEREO16, pcm, samples * 4, MOVIE_RATE);
    alSourceQueueBuffers(a->src, 1, &a->bufs[slot]);
    a->bufSamples[slot] = samples;
    a->queued++;
    free(pcm);
    if (!a->started)
    {
        alSourcePlay(a->src);
        a->started = 1;
    }
}

/* el fotograma actual (en la zona del fondo) a pantalla; devuelve 1 si X o START lo cortan */
static int PresentFrame(int frames)
{
    Gfx_BeginFrame(0, 0, 0);
    Fe_DrawBackground(0);
    Gfx_Flip(GFX_FLIP_FULL);
    Timer_Update();
    Pad_ReadAll();
    return frames > 3 && ((g_PadButtons[0] | g_PadButtons[1]) & (PADB_CROSS | PADB_START));
}

/* 800991e4. a, b, w, h: ventana del original (siempre 320x240); y: fila de la pantalla donde empieza el video */
void Movie_Play(const char* name, int a, int b, int y, int w, int h)
{
    (void)a; (void)b; (void)w; (void)h;
    int slot = File_OpenSearchPaths(name);
    if (slot < 0)
    {
        NR_Log("Movie_Play: '%s' no esta\n", name);
        return;
    }
    int size = File_GetSize(slot);
    uint8_t* data = (uint8_t*)malloc((size_t)size);
    if (!data || File_ReadAll(slot, data, &size) < 0)
    {
        free(data);
        File_Close(slot);
        return;
    }
    File_Close(slot);

    static uint16_t pixels[MOVIE_MAXW * MOVIE_MAXH], half[160 * MOVIE_MAXH];
    MovieAudio au;
    memset(&au, 0, sizeof(au));
    alGetError();
    alGenSources(1, &au.src);
    alGenBuffers(MOVIE_ALBUFS, au.bufs);
    au.ok = alGetError() == AL_NO_ERROR;
    if (au.ok)
    {
        alSourcei(au.src, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(au.src, AL_POSITION, 0.f, 0.f, 0.f);
        alSourcef(au.src, AL_GAIN, 0.8f);
    }

    /* zona del fondo en negro */
    memset(half, 0, sizeof(half));
    RECT16 r;
    setRECT(&r, 0x140, 0, 160, 256);
    LoadImage(&r, (u_long*)half);
    setRECT(&r, 0x140, 0x100, 160, 256);
    LoadImage(&r, (u_long*)half);
    DrawSync(0);

    int frames = 0, cut = 0, startTick = g_Ticks;
    uint32_t pos = 0;
    while (!cut && pos + 8 <= (uint32_t)size)
    {
        const uint8_t* c = data + pos;
        uint32_t len = Be32(c + 4);
        if (len < 8 || pos + len > (uint32_t)size)
            break;
        uint32_t tag = Be32(c);
        if (tag == 0x564c4330u)                          /* VLC0 */
            Mdec_SetVlcTable(c + 8);
        else if (tag == 0x61753030u || tag == 0x61753031u)   /* au00 / au01 */
        {
            /* no mas de MOVIE_ALBUFS en la cola: se espera enseñando el fotograma */
            while (au.ok && au.queued >= MOVIE_ALBUFS && !cut)
            {
                AudioPos(&au);
                if (au.queued >= MOVIE_ALBUFS)
                    cut = PresentFrame(frames);
            }
            AudioQueue(&au, c, len);
        }
        else if (tag == 0x4d444543u || tag == 0x4d444332u)   /* MDEC / MDC2 */
        {
            int fw = Be16(c + 8), fh = Be16(c + 10);
            if (fw <= MOVIE_MAXW && fh <= MOVIE_MAXH && Mdec_DecodeFrame(c + 0x10, (int)len - 0x10, fw, fh, pixels))
            {
                /* espera a la hora del fotograma (por el audio; si no hay, por el reloj) */
                for (;;)
                {
                    long p = AudioPos(&au);
                    int due = p >= 0 ? p >= (long)frames * (MOVIE_RATE / MOVIE_FPS)
                                     : (g_Ticks - startTick) * MOVIE_FPS >= frames * 300;
                    if (due || cut)
                        break;
                    cut = PresentFrame(frames);
                }
                int hw = fw / 2;
                for (int side = 0; side < 2; side++)
                {
                    for (int row = 0; row < fh; row++)
                        memcpy(half + row * hw, pixels + row * fw + side * hw, (size_t)hw * 2);
                    setRECT(&r, 0x140, side * 0x100 + y, hw, fh);
                    LoadImage(&r, (u_long*)half);
                }
                DrawSync(0);
                frames++;
                cut = cut || PresentFrame(frames);
            }
        }
        pos += len;
    }
    /* deja terminar el audio */
    while (!cut && au.ok && au.started && au.queued > 0)
    {
        AudioPos(&au);
        ALint state = 0;
        alGetSourcei(au.src, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING)
            break;
        cut = PresentFrame(frames);
    }
    if (au.ok)
    {
        alSourceStop(au.src);
        alSourcei(au.src, AL_BUFFER, 0);
        alDeleteSources(1, &au.src);
        alDeleteBuffers(MOVIE_ALBUFS, au.bufs);
    }
    Mdec_SetVlcTable(NULL);
    free(data);
    NR_Log("Movie_Play: '%s', %d fotogramas%s%s\n", name, frames, cut ? " (cortado)" : "", au.ok ? "" : " (sin audio)");
}
