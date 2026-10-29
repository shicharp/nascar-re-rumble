/* Solo en el port: extraer la imagen del disco (BIN MODE2/2352) a la carpeta que lee el juego. */
#ifndef NR_DISC_EXTRACT_H
#define NR_DISC_EXTRACT_H

int Disc_Extract(const char* image, const char* outDir);   /* 1 si ha ido bien y es el disco del juego */

#endif
