#include <string.h>
#include "mem.h"
#include "file.h"
#include "stream.h"

static int         s_slot = -1;
static int         s_offset;
static uint8_t     s_block[STREAM_BLOCK_SIZE];
static NrAsset*    s_cur;          /* DAT_800b1124: asset en montaje */
static int         s_curFill;      /* DAT_800b1128 */
static NrAsset*    s_queueHead;    /* DAT_800b1130 */
static NrAsset*    s_queueTail;
static char        s_section[17];
static StreamStats s_stats;
static int         s_blocksLeft = -1;  /* -1 sin limite (carga del nivel), 0 parado */
static int         s_sectionBase;      /* CTRL +0x14: tamano de la cabecera */
static int         s_sectionBusy;      /* DAT_800b0db0 >= 0 */
static NrAsset*    s_toc;              /* Ctos (DAT_800af768) */

int Stream_Open(const char* name)
{
    Stream_Close();
    memset(&s_stats, 0, sizeof(s_stats));
    s_section[0] = 0;
    s_slot = File_OpenSearchPaths(name);
    s_offset = 0;
    s_blocksLeft = -1;
    s_sectionBase = 0;
    s_sectionBusy = 0;
    if (s_slot < 0)
        NR_Log("Stream_Open: no se pudo abrir '%s' (%d)\n", name, s_slot);
    return s_slot;
}

void Stream_Close(void)
{
    if (s_slot >= 0)
        File_Close(s_slot);
    s_slot = -1;
    if (s_cur)
    {
        Asset_Free(s_cur);
        s_cur = NULL;
    }
    Asset_Free(s_toc);
    s_toc = NULL;
}

/* FUN_8002b810 */
int Stream_OnCtos(NrAsset* a)
{
    if (a->size < 16 || NR_Read32(a->data) != NR_TAG('t', 'T', 'O', 'S'))
        return 0;
    Asset_Free(s_toc);
    s_toc = a;
    NR_Log("Stream: tabla de secciones con %u entradas (base 0x%x)\n", NR_Read32(a->data + 12), s_sectionBase);
    return 1;
}

/* 8002b69c */
int Stream_StartSection(int section)
{
    if (s_slot < 0 || !s_toc || s_sectionBusy || s_blocksLeft != 0)
        return 0;
    uint32_t n = NR_Read32(s_toc->data + 12);
    if (section < 0 || (uint32_t)section >= n || 16 + (section + 1) * 12 > s_toc->size)
        return 0;
    const uint8_t* e = s_toc->data + 16 + section * 12;
    s_offset = s_sectionBase + (int)NR_Read32(e);
    s_blocksLeft = (int)NR_Read32(e + 4) / STREAM_BLOCK_SIZE;
    s_sectionBusy = 1;
    return 1;
}

void Stream_SectionDone(void)
{
    s_sectionBusy = 0;
}

void Stream_SetIdle(void)
{
    s_blocksLeft = 0;
}

int Stream_IsOpen(void)
{
    return s_slot >= 0;
}

/* 8002a4b0 */
static void Stream_QueueAsset(NrAsset* a)
{
    a->next = NULL;
    if (s_queueTail)
        s_queueTail->next = a;
    else
        s_queueHead = a;
    s_queueTail = a;
    s_stats.assets++;
}

/* 8002a2bc: cabecera SHDR. Nota: el original consulta FUN_80067890 para descartar assets por nombre. */
static void Stream_BeginAsset(const uint8_t* chunk)
{
    if (s_cur)
    {
        NR_Log("Stream: SHDR nuevo con un asset incompleto (%d/%d bytes)\n", s_curFill, s_cur->size);
        s_stats.errors++;
        Asset_Free(s_cur);
    }

    NrAsset* a = (NrAsset*)Mem_Alloc(sizeof(NrAsset), 0);
    memset(a, 0, sizeof(*a));
    a->pool = (int)NR_Read32(chunk + 0x14);
    a->type = NR_Read32(chunk + 0x18);
    a->id   = (int)NR_Read32(chunk + 0x1c);
    a->size = (int)NR_Read32(chunk + 0x20);
    memcpy(a->section, s_section, sizeof(a->section));
    a->data = a->size ? (uint8_t*)Pool_Alloc(a->size, a->pool) : NULL;
    s_curFill = 0;

    if (a->size == 0)
        Stream_QueueAsset(a);
    else
        s_cur = a;
}

static void Stream_AppendData(const uint8_t* data, int len)
{
    if (!s_cur)
    {
        s_stats.errors++;
        return;
    }
    if (len > s_cur->size - s_curFill)
        len = s_cur->size - s_curFill;
    memcpy(s_cur->data + s_curFill, data, (size_t)len);
    s_curFill += len;
    if (s_curFill == s_cur->size)
    {
        Stream_QueueAsset(s_cur);
        s_cur = NULL;
    }
}

/* 8002a85c: recorre los chunks de un bloque. Ningun chunk cruza el limite de bloque. */
static void Stream_ProcessChunks(const uint8_t* block, int len)
{
    int off = 0;
    while (off + 8 <= len)
    {
        uint32_t tag  = NR_Read32(block + off);
        int      size = (int)NR_Read32(block + off + 4);
        if ((tag == 0 && size == 0) || tag == TAG_FILL)
            break;
        if (size < 8 || off + size > len)
        {
            NR_Log("Stream: chunk invalido en +0x%x del bloque\n", off);
            s_stats.errors++;
            break;
        }

        const uint8_t* c = block + off;
        switch (tag)
        {
        case TAG_SHOC:
        {
            uint32_t sub = NR_Read32(c + 0x10);
            if (sub == TAG_SHDR)
                Stream_BeginAsset(c);
            else if (sub == TAG_SDAT)
                Stream_AppendData(c + 0x14, size - 0x14);
            break;
        }
        case TAG_SWVR:
            /* marcador del constructor de streams: nombre de seccion (el juego lo ignora) */
            memcpy(s_section, c + 0x14, 16);
            s_section[16] = 0;
            break;
        case TAG_VAGB:
        case TAG_VAGM:
            s_stats.vagChunks++;   /* TODO: audio (FUN_80022e50 / cola VAGM) */
            break;
        case TAG_CTRL:
            if (s_offset == 0 && off == 0 && size >= 0x18)
                s_sectionBase = (int)NR_Read32(c + 0x14);   /* fin de la cabecera = inicio de las secciones */
            s_stats.otherChunks++;
            break;
        default:
            s_stats.otherChunks++; /* CANM */
            break;
        }
        off += size;
    }
}

int Stream_Pump(int maxBlocks)
{
    int done = 0;
    while (s_slot >= 0 && done < maxBlocks && s_blocksLeft != 0)
    {
        File_Seek(s_slot, s_offset);
        int n = File_ReadAsync(s_slot, s_block, STREAM_BLOCK_SIZE, NULL);
        if (n <= 0)
            break;
        Stream_ProcessChunks(s_block, n);
        s_offset += n;
        s_stats.blocks++;
        done++;
        if (s_blocksLeft > 0)
            s_blocksLeft--;
    }
    return done;
}

NrAsset* Stream_PopAsset(void)
{
    NrAsset* a = s_queueHead;
    if (a)
    {
        s_queueHead = a->next;
        if (!s_queueHead)
            s_queueTail = NULL;
        a->next = NULL;
    }
    return a;
}

void Asset_Free(NrAsset* a)
{
    if (!a)
        return;
    Pool_Free(a->data);
    Mem_Free(a);
}

const StreamStats* Stream_GetStats(void)
{
    return &s_stats;
}
