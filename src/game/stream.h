/* Streaming de ficheros de chunks EA (GLBLDATA.PSX, *.TRK, *.AV).
 * Original: Stream_Open 8002baa8, Stream_Close 8002ba0c, Stream_Pump 8002a548, Stream_KickRead 8002a190,
 * Stream_ProcessChunks 8002a85c, Stream_BeginAsset 8002a2bc, Stream_QueueAsset 8002a4b0.
 * El original lee bloques de 0x6000 por DMA en una cola de 4 buffers; aqui la lectura es sincrona
 * pero el troceo por bloques y el montaje de assets SHDR/SDAT es el mismo. */
#ifndef NR_STREAM_H
#define NR_STREAM_H

#include "nr.h"

#define STREAM_BLOCK_SIZE 0x6000

#define TAG_SHOC NR_TAG('S', 'H', 'O', 'C')
#define TAG_SHDR NR_TAG('S', 'H', 'D', 'R')
#define TAG_SDAT NR_TAG('S', 'D', 'A', 'T')
#define TAG_CTRL NR_TAG('C', 'T', 'R', 'L')
#define TAG_FILL NR_TAG('F', 'I', 'L', 'L')
#define TAG_SWVR NR_TAG('S', 'W', 'V', 'R')
#define TAG_VAGB NR_TAG('V', 'A', 'G', 'B')
#define TAG_VAGM NR_TAG('V', 'A', 'G', 'M')
#define TAG_CANM NR_TAG('C', 'A', 'N', 'M')

/* Asset completo (cabecera SHDR + datos SDAT concatenados) */
typedef struct NrAsset
{
    struct NrAsset* next;
    uint32_t type;      /* SHDR +0x18: 'Cpag', 'Cobj', 'Ctrk'... */
    int      id;        /* SHDR +0x1c */
    int      size;      /* SHDR +0x20 */
    int      pool;      /* SHDR +0x14: modo de Pool_Alloc */
    char     section[17];
    uint8_t* data;
} NrAsset;

typedef struct
{
    int blocks;
    int assets;
    int vagChunks;
    int otherChunks;
    int errors;
} StreamStats;

int  Stream_Open(const char* name);
void Stream_Close(void);
int  Stream_IsOpen(void);

/* Lee y procesa hasta maxBlocks bloques. Devuelve los procesados (0 = fin de fichero). */
int  Stream_Pump(int maxBlocks);

/* Saca el siguiente asset completo de la cola (o NULL) */
NrAsset* Stream_PopAsset(void);
void     Asset_Free(NrAsset* a);

const StreamStats* Stream_GetStats(void);

#endif
