# NASCAR Rumble — port nativo (PsyCross)

Descompilación funcional en C del ejecutable de PS1 (SLUS_010.68) sobre
[PsyCross](https://github.com/OpenDriver2/PsyCross) (capa PsyQ → SDL2 + OpenGL + OpenAL).

## Compilar

Requisitos: Visual Studio 2022 (MSVC 14.38), CMake 3.31 y Ninja
(`pip install --user cmake==3.31.10 ninja`). Dependencias en `third_party/`:

- PsyCross: submódulo git (`git submodule update --init`).
- SDL2 2.32.8: descomprimir `SDL2-devel-2.32.8-VC.zip`
  (https://github.com/libsdl-org/SDL/releases/tag/release-2.32.8) en `third_party/`.
- OpenAL Soft 1.25.2: descomprimir `openal-soft-1.25.2-bin.zip`
  (https://openal-soft.org/openal-binaries/) en `third_party/`.

Los binarios no se versionan (ver `.gitignore`).

```bat
build.bat Debug
```

Genera `build\Debug\nascar.exe` (Win32/x86: el código descompilado guarda punteros en 32 bits).

## Ejecutar

Necesita el disco extraído (`D:\Proyecto Nascar\disc`, lo genera `tools/extract_iso.py`); lo busca
subiendo carpetas desde el directorio actual, o se indica con `--disc`.

```
nascar.exe [--disc <carpeta>] [--skip-legal] [--shot salida.bmp [--shot-frame N]]
nascar.exe --viewer [fichero de stream] [--page N]
```

Estado actual: arranque real del juego. `main` → `Game_Init` → pantalla legal (`Legal.lsc`, MDEC por
software) → vídeos (omitidos) → `Level_Run("Fe.trk")` → pantalla de carga `Feld.lsc` mientras llega
`FE.TRK` por streaming hasta el `Cctr` → `Frontend_MainLoop` (pantalla 2000 con fundido → **menús**
navegables: principal (100), tipo de carrera (200), opciones de carrera (300), opciones de juego (400) y
audio (500), con cuadros de mensaje, la selección de piloto/coche (1200) con el coche 3D que llega por
secciones del FE.TRK y la elección de sede/pista (1300) con el trazado en 3D; al confirmar se monta la
parrilla y se carga la carrera (GlblData.psx + un coche por sección + el .trk, con su pantalla de carga y barra
de progreso). La pista se dibuja como el original (segmentos visibles, mips por distancia, subdivisión de las caras
cercanas, horizonte/cielo de capas THOR, texturas animadas y halos de luz); el bucle de carrera aún no está portado: de momento la cámara recorre la línea central con los
coches de la parrilla en la salida, X acelera y START vuelve al front-end). `--shot` guarda el frame N y sale; `--press "185:down,205:right"` simula
pulsaciones (para verificar sin intervención; botones: up, down, left, right, cross, circle, triangle, square,
start, select, l1, r1, l2, r2).
`--viewer` es el visor de páginas Cpag (flechas para cambiar de página).

## Estructura

| Fichero | Original (SLUS_010.68) |
|---|---|
| `src/game/game.c` | main 800792a8, Game_Init 80079470, Legal_Show 80090d98, Level_Run 80029be4, Level_Load 80024684, Frontend_Load 80024080, Level_WaitLoaded 800252b0, Frontend_MainLoop 8002865c, Level_Unload 80025448, Frame_Wait 800285e4 |
| `src/game/frontend.c` | Frontend_Update 8007a994 (pantallas + fundidos), pantalla 2000 80089240, Fe_GetString 80090984, Fe_DrawVramRect 8008c86c, Fe_DrawBackground 8008b79c, Crle 80092e08, TxtR 80092dec |
| `src/game/fe_menu.c` | widgets del front-end (8008e614, 80079784, 8008e51c, 8008e75c, 8008b338, 8008d064, 8008d5f4, 8008b6b4), mando (8008a7f4, 8008a994, 8008aa90, 8008a66c), menús (8007a684, 8007a780, 8008abf8, 8008ac5c) cuadro de mensaje (8008cb88) y pantallas 100 (8007b2ac), 200 (8007b7fc), 300 (8007f4a0), 400 (8007bf00), 500 (8007c540) |
| `src/game/fe_select.c` | pantalla 1200 (80081a9c): tabla de pilotos 800aa5ec, atlas de miniaturas (80092f5c, 8008e2ec, RLE 8009307c), carrusel (8008b9d4, 8008bc34), coches del garaje (800923cc, 80090a88, 80090674), 8008aee4, 8008dc1c, 8008efec |
| `src/game/fe_track.c` | pantalla 1300 (800838f8), trazado TkFE (8001893c, 80018ef0), 80090718, 80090860, 8008e13c, 8008c308, 8008b80c, montaje de la carrera Fe_StartRace 8008927c |
| `src/game/race.c` | Race_Load 80024284, Race_NextCar 80023ef4, barra 80024f08, Ctrk 80062680, Cars_RegisterModels 800134f0, Race_MainLoop 800299a8 (provisional) |
| `src/game/track.c` | pista Ctrk: Track_Parse 80062088 (+ 80067218), Track_DrawView 800644c0 (800637bc, 80063928, 80063a34, 800641b8), Track_DrawSegments 8006327c, vertices 80063470, caras 80062de0, subdivision 80062970 / 800626b8 / 80062888, horizonte 80064078 / 80064134 / 80063af4 / 80063c08 (+ 800673f8, 80062004), TTXA 80061dfc / 80061e94 |
| `src/game/sprite.c` | halos/destellos 2D: lista DAT_800b5500, Sprite_DrawAll 80045268 (formas 800aafa4 / 800aab44 / 800aad34) |
| `src/game/render.c` | render 3D: Render_DrawList 80015f4c (80015280, 800162d4, 80015b74), luz 800137d8/800153e4, objetos 800139fc/80013c20/800367d0, matrices 80017ad8, seno 800176c0, frustum 8001607c |
| `src/game/model.c` | Cobj_OnLoaded 80013374, Model_Register 8001315c (80012c08, 80012de0, 80012f14), registro de assets 8001860c/80018788 |
| `src/game/texslot.c` | slots de textura: 80066fe0, 800669d4, Cpag_UploadToSlot 80066d00, 80066c4c, CLUTs 8006674c/800668cc, 800666c8, 800674fc |
| `src/game/font.c` | fuentes Cfnt: 800446b8, 80043f88, 800441a4, 800443cc, 8004463c, 800447b8, 80044944, 80044a44, 80044d24, 80045140 |
| `src/game/lsc.c` | Lsc_LoadToVram 80090ef8, LoadScreen_Show 80024c58, FUN_800921a0 |
| `src/game/mdec.c` | FUN_80095d70 + FUN_8009905c + hardware MDEC (BS v2 por software) |
| `src/game/pad.c` | Pad_GetButtons 8001dac0, FUN_8005fc54 |
| `src/game/mem.c` | Mem_Alloc 8001cb84, Mem_Free 8001cc04, Pool_Alloc 80017154, Pool_Free 8001745c |
| `src/game/file.c` | File_* 8001bd68…8001ca54 (CD-ROM → stdio sobre el disco extraído) |
| `src/game/stream.c` | Stream_* 8002a190…8002baa8 (bloques de 0x6000, SHOC/SHDR/SDAT), secciones Ctos 8002b810 / 8002b69c |
| `src/game/asset.c` | Asset_Dispatch 80024710 (Cpag, Cctr, Crle, TxtR por ahora) |
| `src/game/cpag.c` | Cpag_OnLoaded 8006710c, Cpag_UploadToSlot 80066d00 |
| `src/game/gfx.c` | Gfx_InitBuffers 8001a0b4, Gfx_Flip 8001a24c, reparto por profundidad 8001a1b4 |
| `src/debug_viewer.c` | visor de páginas Cpag (depuración) |

## Notas de PsyCross

- El código del juego se compila como **C++** (como REDRIVER2): con PGXP las coordenadas de las
  primitivas son `half` y en C habría que escribirlas con `_HF()`.
- Las entradas de la OT son `OT_TAG` (8 bytes en x86), no `u_long`.
- El juego cierra cada frame con `PsyX_EndScene()` después de `DrawOTag`; `FntFlush()` va antes.
- `PadRead` no está implementado: el mando se lee con `PadInitDirect` + buffer.
- PsyCross no implementa MDEC: las imágenes BS v2 (.LSC) se decodifican en `mdec.c`.
- El original deja pantallas fijas sin presentar frames (legal, carga); con PsyCross hay que seguir
  redibujando y presentando cada frame.
- PGXP: las primitivas 3D llevan la proyección en coma flotante de la GTE (`g_FP_SXYZ2`), sin el temblor de
  PS1. El z-buffer de PGXP está desactivado (el juego depende del orden de la OT, p. ej. el reflejo del garaje
  bajo el coche) y también su corrección de perspectiva, que desplaza las texturas de los coches.
- `Gfx_Flip(0)` (dibujar lo acumulado a mitad de frame) se emula encadenando una segunda OT: PsyCross no
  admite bien dos `DrawOTag` en el mismo frame.
- La VRAM emulada no recibe lo dibujado en OpenGL (`StoreImage` del framebuffer da negro); las
  capturas leen el buffer frontal con `glReadPixels`.
