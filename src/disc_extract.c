/* Solo en el port: saca los ficheros de una imagen del disco (BIN MODE2/2352, la del CD propio) a una carpeta, como
 * tools/extract_iso.py: los ficheros Form 1 con sus 2048 bytes por sector y los que tienen algun sector Form 2 (audio
 * XA, video) con los sectores crudos de 2352 bytes, que es lo que leen el sonido y los videos del port. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <direct.h>
#include "disc_extract.h"

#define RAW      2352
#define USER_OFF 24   /* sincronia (12) + cabecera (4) + subcabecera (8) */

typedef struct
{
    FILE*    f;
    uint32_t nsect;
} Disc;

typedef struct
{
    char     path[256];
    uint32_t lba, size;
    uint16_t xa;
} DiscFile;

static DiscFile* s_files;
static int       s_numFiles, s_capFiles;

static int Disc_Raw(Disc* d, uint32_t lba, uint8_t* out)
{
    return _fseeki64(d->f, (int64_t)lba * RAW, SEEK_SET) == 0 && fread(out, 1, RAW, d->f) == RAW;
}

static uint8_t* Disc_User(Disc* d, uint32_t lba, uint32_t n)
{
    uint8_t* buf = (uint8_t*)malloc((size_t)n * 2048 + 1);
    uint8_t raw[RAW];
    for (uint32_t i = 0; buf && i < n; i++)
    {
        if (!Disc_Raw(d, lba + i, raw))
            memset(raw, 0, sizeof(raw));
        memcpy(buf + (size_t)i * 2048, raw + USER_OFF, 2048);
    }
    return buf;
}

static uint32_t U32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void Disc_ParseDir(Disc* d, uint32_t lba, uint32_t size, const char* path, int depth)
{
    if (depth > 16)
        return;
    uint32_t nsec = (size + 2047) / 2048;
    uint8_t* data = Disc_User(d, lba, nsec);
    if (!data)
        return;
    uint32_t len = nsec * 2048, pos = 0;
    while (pos < len)
    {
        uint32_t recLen = data[pos];
        if (recLen == 0)
        {
            pos = (pos / 2048 + 1) * 2048;   /* relleno hasta el siguiente sector */
            continue;
        }
        const uint8_t* rec = data + pos;
        uint32_t extLba = U32(rec + 2), extSize = U32(rec + 10);
        int flags = rec[25], nameLen = rec[32];
        const uint8_t* su = rec + 33 + nameLen + (1 - nameLen % 2);   /* datos del sistema (XA) */
        uint16_t xa = (su[6] == 'X' && su[7] == 'A') ? (uint16_t)(su[4] << 8 | su[5]) : 0;
        pos += recLen;
        if (nameLen == 1 && (rec[33] == 0 || rec[33] == 1))
            continue;   /* . y .. */
        char name[128];
        int n = 0;
        for (int i = 0; i < nameLen && rec[33 + i] != ';' && n < (int)sizeof(name) - 1; i++)
            name[n++] = (char)rec[33 + i];
        name[n] = 0;
        char full[256];
        snprintf(full, sizeof(full), "%s%s%s", path, *path ? "/" : "", name);
        if (flags & 2)
            Disc_ParseDir(d, extLba, extSize, full, depth + 1);
        else
        {
            if (s_numFiles == s_capFiles)
            {
                s_capFiles = s_capFiles ? s_capFiles * 2 : 256;
                s_files = (DiscFile*)realloc(s_files, (size_t)s_capFiles * sizeof(DiscFile));
            }
            DiscFile* f = &s_files[s_numFiles++];
            snprintf(f->path, sizeof(f->path), "%s", full);
            f->lba = extLba;
            f->size = extSize;
            f->xa = xa;
        }
    }
    free(data);
}

/* crea las carpetas de 'path' (hasta la ultima barra) */
static void MakeDirs(const char* path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++)
        if (*p == '/' || *p == '\\')
        {
            char c = *p;
            *p = 0;
            _mkdir(tmp);
            *p = c;
        }
}

int Disc_Extract(const char* image, const char* outDir)
{
    Disc d;
    d.f = fopen(image, "rb");
    if (!d.f)
    {
        printf("No se puede abrir %s\n", image);
        return 0;
    }
    _fseeki64(d.f, 0, SEEK_END);
    d.nsect = (uint32_t)(_ftelli64(d.f) / RAW);
    uint8_t* pvd = Disc_User(&d, 16, 1);
    if (!pvd || memcmp(pvd + 1, "CD001", 5) != 0)
    {
        printf("%s no es una imagen BIN (MODE2/2352) de un disco de PS1\n", image);
        free(pvd);
        fclose(d.f);
        return 0;
    }
    s_numFiles = 0;
    Disc_ParseDir(&d, U32(pvd + 156 + 2), U32(pvd + 156 + 10), "", 0);
    free(pvd);
    int ok = 1, found = 0;
    uint8_t raw[RAW];
    for (int i = 0; i < s_numFiles && ok; i++)
    {
        const DiscFile* f = &s_files[i];
        uint32_t nsec = (f->size + 2047) / 2048;
        int form2 = (f->xa & 0x1000) != 0;
        for (uint32_t k = 0; k < nsec && !form2; k++)
            if (Disc_Raw(&d, f->lba + k, raw) && raw[15] == 2 && (raw[18] & 0x20))
                form2 = 1;
        char dst[512];
        snprintf(dst, sizeof(dst), "%s/%s", outDir, f->path);
        MakeDirs(dst);
        FILE* o = fopen(dst, "wb");
        if (!o)
        {
            printf("No se puede escribir %s\n", dst);
            ok = 0;
            break;
        }
        uint32_t left = f->size;
        for (uint32_t k = 0; k < nsec; k++)
        {
            if (!Disc_Raw(&d, f->lba + k, raw))
                memset(raw, 0, sizeof(raw));
            if (form2)
                fwrite(raw, 1, RAW, o);
            else
            {
                uint32_t n = left < 2048 ? left : 2048;
                fwrite(raw + USER_OFF, 1, n, o);
                left -= n;
            }
        }
        fclose(o);
        if (!strcmp(f->path, "SLUS_010.68"))
            found = 1;
    }
    fclose(d.f);
    printf("%d ficheros extraidos de %s en %s\n", s_numFiles, image, outDir);
    if (ok && !found)
        printf("Ojo: no esta SLUS_010.68; no parece el disco de NASCAR Rumble (USA)\n");
    free(s_files);
    s_files = NULL;
    s_capFiles = s_numFiles = 0;
    return ok && found;
}
