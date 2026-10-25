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
audio (500), el Showcase (600, con los vídeos), la contraseña (700), la tarjeta de memoria (800, emulada en `memcard1.mcd`), la configuración de mandos (1000), Hints and Tips (1900), el Hall of Champs (900 y
récords de copa 1600), el nombre (1100), los récords de pista (1400) y, al volver de una carrera, los récords, la
copa (clasificación 1500, podio 1700), los desbloqueos (1800) y la pregunta de guardar, con cuadros de mensaje, la selección de piloto/coche (1200) con el coche 3D que llega por
secciones del FE.TRK y la elección de sede/pista (1300) con el trazado en 3D; al confirmar se monta la
parrilla y se carga la carrera (GlblData.psx + un coche por sección + el .trk, con su pantalla de carga y barra
de progreso). La pista se dibuja como el original (segmentos visibles, mips por distancia, subdivisión de las caras
cercanas, horizonte/cielo de capas THOR, texturas animadas y halos de luz); en carrera ya están la parrilla real, la cámara de persecución, la
cuenta atrás y la física original de los coches (X acelera, cuadrado frena, círculo freno de mano, cruceta gira,
L1/L2 cambio manual, triángulo vuelve a la pista), la IA que corre por la red de trazadas, los choques entre
coches, las vueltas y las posiciones, y el marcador (mapa, cuentarrevoluciones, tiempos; SELECT largo cambia de
nivel, corto de camara), la pausa con START (camara, marcador, audio, reiniciar, salir), dos jugadores a pantalla partida y los resultados al
terminar; "Exit Race" vuelve al front-end; "Show Replay" repite la carrera con las camaras de TV, volviendo a
simularla con los mandos grabados; tras 30 s sin tocar nada en el menu principal arranca una demostracion de 6
coches de la IA, y cualquier boton la corta). Actores de la pista: objetos (conos, vallas, carteles) que salen despedidos al golpearlos y se rompen, objetos
que giran y cajas de poder que reaparecen;
los coches recogen un poder segun su puesto y lo usan (la IA sola, el jugador con R2), con sus efectos (escudo,
turbo, ruedas grandes, embestida, onda expansiva, objetos que se sueltan en la pista, tormenta, helada y
tornado que persiguen a los rivales) e iconos y avisos en el marcador. Sonido: efectos, motores, bocina, musica
de la sede y comentarista. Efectos visuales: chispas, destellos y trozos de los choques, humo del escape y
anillos de humo del tornado, marcas y humo de las ruedas, ondas y cupulas de luz, restos de los objetos, rayos, estelas, coche helado o electrocutado y llamas del escape la nube de la tormenta con su lluvia y sus rayos, el destello del sol (se apaga si algo lo tapa), las cupulas de las ruedas grandes y de la embestida, y el nivel de detalle de la PS1 (opcional). Sombras de los coches y objetos (3DSD, gris sustractivo) y reflejos de la carroceria (pagina 8). Sonido de los golpes con su roce en bucle y del turbo. Camaras de TV de la pista al cruzar la meta (orbita y cambios de camara). Vehiculos de la pista que siguen su red (el dirigible de EA Sports, los contenedores de MC3) y generadores Csac que crean vehiculos, disparadores u objetos durante la carrera. `--shot` guarda el frame N y sale; `--press "185:down,205:right"` simula
pulsaciones (para verificar sin intervención; botones: up, down, left, right, cross, circle, triangle, square,
start, select, l1, r1, l2, r2).
Variables de depuracion: `NR_LAPS=n` vueltas, `NR_CAMCAR=n` camara en el coche n, `NR_POWER=n` poder n en la
mano del jugador al salir, `NR_POWERLOG=1` cajas recogidas y poderes usados en el log, `NR_SNDLOG=1` sonidos,
`NR_FXLOG=1` efectos creados, `NR_FXTEST=clase[,golpe]` crea un efecto en el coche del jugador cada segundo,
`NR_BODYLOG=1` golpes a los objetos sueltos, `NR_SCRIPTLOG=1` guiones, `NR_TRIGTEST=1` el coche humano toca todos los disparadores, `NR_KILLTEST=id` rompe ese actor a los 5 s (guiones de golpe y de muerte), `NR_VEHLOG=1` nodos que cruzan los vehiculos de la pista, `NR_ENDRACE=t` el jugador 1 termina la carrera a los t tics, `NR_SCREENLOG=1` cambios de pantalla del front-end, `NR_REPLAYLOG=1` suma de control de los coches cada 150 tics (la carrera y su repeticion deben coincidir), `--press` admite el mando 2 con el prefijo `2` (`900:2cross`), `--movie <fichero>` pone ese vídeo y sale (`--skip-legal` también salta los vídeos de arranque),
`NR_POWERUPS=n` / `NR_CLASS=n` fuerzan los poderes y la clase de la carrera, `NR_TVLOG=1` camaras de TV, `NR_TVCAM=1` empieza con las camaras de TV (`vN`: la virtual N), `NR_PSXLOD=1`
niveles de detalle de la PS1 (por defecto, todo con el detalle completo).
`--viewer` es el visor de páginas Cpag (flechas para cambiar de página).

## Controles

Mando compatible con SDL (Xbox, DualShock, etc.) o teclado (configuracion por defecto de PsyCross):

| PlayStation | Teclado | En carrera |
|---|---|---|
| Cruceta | Flechas | Direccion |
| X | C | Acelerar / elegir |
| Cuadrado | X | Frenar |
| Circulo | V | Freno de mano |
| Triangulo | Z | Volver a la pista / atras |
| L1 / L2 | Mayus izq. / Ctrl izq. | Subir / bajar marcha (manual); en automatico, marcador / camara |
| R1 | Mayus der. | Retrovisor (mantener) |
| R2 | Ctrl der. | Usar poder (mantener) |
| Arriba | Flecha arriba | Bocina (pendiente: sonido) |
| START | Intro | Pausa |
| SELECT | Espacio | Corto: camara; largo: nivel del marcador |

Pantalla completa con **Alt derecho + Intro**. La imagen se ensancha al aspecto de la ventana (16:9, etc.)
mostrando mas escenario a los lados; el marcador se ancla a los bordes.

## Estructura

| Fichero | Original (SLUS_010.68) |
|---|---|
| `src/game/game.c` | main 800792a8, Game_Init 80079470, Legal_Show 80090d98, Level_Run 80029be4, Level_Load 80024684, Frontend_Load 80024080, Level_WaitLoaded 800252b0, Frontend_MainLoop 8002865c, Level_Unload 80025448, Frame_Wait 800285e4 |
| `src/game/frontend.c` | Frontend_Update 8007a994 (pantallas + fundidos), pantalla 2000 80089240, Fe_GetString 80090984, Fe_DrawVramRect 8008c86c, Fe_DrawBackground 8008b79c, Crle 80092e08, TxtR 80092dec |
| `src/game/fe_menu.c` | widgets del front-end (8008e614, 80079784, 8008e51c, 8008e75c, 8008b338, 8008d064, 8008d5f4, 8008b6b4), mando (8008a7f4, 8008a994, 8008aa90, 8008a66c), menús (8007a684, 8007a780, 8008abf8, 8008ac5c) cuadro de mensaje (8008cb88) y pantallas 100 (8007b2ac), 200 (8007b7fc), 300 (8007f4a0), 400 (8007bf00), 500 (8007c540), 600 (80088edc), 700 (8007cb6c), 800 (8007d32c), 1100 (80080cbc) |
| `src/game/memcard.c` | driver de la tarjeta de memoria (8001f560, 8001f578, 8001fa3c, 8001fae8, 8001fd88, 8001f910) emulado sobre un .mcd |
| `src/game/savegame.c` | partida guardada: 8009358c, 80093938, 800937f8, 800939fc, 80093f6c, Clear Records 800931d8 |
| `src/game/password.c` | contraseña del progreso: 8008f344, 8008fb60, 80090008 |
| `src/game/movie.c` | vídeos WVE: Movie_Play 800991e4 (bucle 8009743c, audio 80096e90 / 80096d7c); `mdec.c` hace la tabla VLC como 80098ee0 / 80098f28 / 8009905c |
| `src/game/fe_pads.c` | configuración de mandos 1000 (8007f810), esquema de botones de cada jugador 8008b160 y Hints and Tips 1900 (8008a52c) |
| `src/game/fe_records.c` | vuelta de la carrera 80090fb4, desbloqueos 80091ae0, tiempos 8008ee98 y pantallas 900 (8007ddb4), 1400 (800855a0), 1500 (80085d90), 1600 (8007ea64), 1700 (80087130), 1800 (80087fa8) |
| `src/game/fe_select.c` | pantalla 1200 (80081a9c): tabla de pilotos 800aa5ec, atlas de miniaturas (80092f5c, 8008e2ec, RLE 8009307c), carrusel (8008b9d4, 8008bc34), coches del garaje (800923cc, 80090a88, 80090674), 8008aee4, 8008dc1c, 8008efec |
| `src/game/fe_track.c` | pantalla 1300 (800838f8), trazado TkFE (8001893c, 80018ef0), 80090718, 80090860, 8008e13c, 8008c308, 8008b80c, montaje de la carrera Fe_StartRace 8008927c |
| `src/game/race.c` | Race_Load 80024284, Race_NextCar 80023ef4, barra 80024f08, Ctrk 80062680, Cars_RegisterModels 800134f0, Race_MainLoop 800299a8 (provisional) |
| `src/game/track.c` | pista Ctrk: Track_Parse 80062088 (+ 80067218), Track_DrawView 800644c0 (800637bc, 80063928, 80063a34, 800641b8), Track_DrawSegments 8006327c, vertices 80063470, caras 80062de0, subdivision 80062970 / 800626b8 / 80062888, horizonte 80064078 / 80064134 / 80063af4 / 80063c08 (+ 800673f8, 80062004), TTXA 80061dfc / 80061e94 |
| `src/game/racecar.c` | coches de la carrera: salida 800317a0, parrilla 8003132c, cámara 8005d4c8 / 8005f034 / 8005e8b0 / 8005dc98 |
| `src/game/carphys*.c` | física de los coches (8004c000-8005a000): Physics_StepAll 80056c6c, creación 8004e830 / 8004dc1c / 8004e1b4, contactos 8004fb10 / 8004fdd4 / 80055748 / 80050210, neumáticos 80052dbc, en el aire 800524b4, integración 80054a14 / 80055f44, ruedas 8004ef54, controles 80059870 / 800598dc / 8005967c, mando 8005f928 / 8004cae8 |
| `src/game/ai.c` | IA: Ai_UpdatePath 80030928, Ai_UpdateTarget 8002de7c, Ai_DriveAll 8002f4ac, Ai_ChooseNext 80030504, AiCar_Create 8002cfbc; vueltas 8003014c y posiciones 8002fc9c |
| `src/game/actor.c` | actores Cact: Cact_OnLoaded 8004942c, entidades 800491ec / 8002c2a0 / 8002c894 / 80049688 / 80049360, visibles 80061a0c / 80061b28, choques con los coches 80061608 / 800613b4, objetos 0xB 8003421c, objetos que giran 0x6 800339b4 / 80033d30, animacion 8003c95c / 8003c8ac / 8003cb08 / 80013aa4 |
| `src/game/powerup.c` | poderes: cajas 0x16 8003203c (80031854, 80031f04, 800319c4), Car_UpdatePowerUps 8002ec00, Car_AddEffect 8002f268, turbo 80050130 / 800501b8, onda expansiva 8005b2a0 / 8005b068, 8004ffec, objetos que se sueltan 8002e94c / 80033468 / 80033374, clima 8002e808 / 80033274 / 8003219c / 800323f0 |
| `src/game/collide.c` | choques entre coches: 80061808 / 800613b4 / 80060f88, Car_CollidePair 8005c05c (80059f0c, 8005a534, 8005ba30) |
| `src/game/hud.c` | marcador: Race_DrawHud 80043520, mapa 8004274c, iconos de poderes 80042544, cuentarrevoluciones 800430e4 / 80042330, mensajes 800429d0, diferencia 8002cd84 |
| `src/game/pause.c` | pausa y fin de carrera: Race_UpdateState 80027e90, Pause_Menu 80025ad4 (8002552c, 80025a60), resultados 8002896c, 800266f0 |
| `src/game/exe.c` | lectura de tablas de datos del propio SLUS_010.68 (Exe_Ptr) |
| `src/game/cnet.c` | redes de trazadas Cnet 8004c368 / 8002cc50 / 8004bd74 |
| `src/game/vecmath.c` | vectores 16.16 del juego (80019518-80019da4, 800184b0) |
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

## Parches de PsyCross

`third_party/PsyCross` es un submódulo; los arreglos propios van en su rama local `nascar` y, por si se vuelve a
clonar limpio, también en `patches/` (aplicar con `git -C third_party/PsyCross apply ../../patches/<parche>`):

- `psycross-samplers-8bit.patch`: `GR_SetTexture` no volvía a poner los samplers al cambiar de shader con la misma
  textura, y el primer sprite de 8 bits detrás de un texto (4 bits) salía con los colores de otra parte de la VRAM.
