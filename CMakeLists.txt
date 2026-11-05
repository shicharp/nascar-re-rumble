cmake_minimum_required(VERSION 3.20)
project(NascarRumble VERSION 1.0.4 LANGUAGES C CXX)   # version: datos del exe (res/nascar.rc)

# Port nativo de NASCAR Rumble (PS1, SLUS_010.68) sobre PsyCross.
# De momento solo Win32 (x86): el codigo descompilado guarda punteros en enteros de 32 bits.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Compila en x86 (32 bits): usa build.bat, que carga vcvars32")
endif()

# Runtime de C estatico: el ejecutable no necesita el Visual C++ Redistributable instalado (para compartirlo)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(TP ${CMAKE_SOURCE_DIR}/third_party)
set(SDL2_DIR_ROOT   ${TP}/SDL2-2.32.8)
set(OPENAL_DIR_ROOT ${TP}/openal-soft-1.25.2-bin)

# --- Dependencias precompiladas ---------------------------------------------
add_library(SDL2::SDL2 SHARED IMPORTED)
set_target_properties(SDL2::SDL2 PROPERTIES
    IMPORTED_LOCATION             ${SDL2_DIR_ROOT}/lib/x86/SDL2.dll
    IMPORTED_IMPLIB               ${SDL2_DIR_ROOT}/lib/x86/SDL2.lib
    INTERFACE_INCLUDE_DIRECTORIES ${SDL2_DIR_ROOT}/include)

add_library(OpenAL::OpenAL SHARED IMPORTED)
set_target_properties(OpenAL::OpenAL PROPERTIES
    IMPORTED_LOCATION             ${OPENAL_DIR_ROOT}/bin/Win32/soft_oal.dll
    IMPORTED_IMPLIB               ${OPENAL_DIR_ROOT}/libs/Win32/OpenAL32.lib
    INTERFACE_INCLUDE_DIRECTORIES ${OPENAL_DIR_ROOT}/include)

# --- PsyCross (sin tocar su CMakeLists: espera dependencias compiladas desde fuente) ---
set(PSYCROSS_DIR ${TP}/PsyCross)
file(GLOB_RECURSE PSYCROSS_SRCS_C ${PSYCROSS_DIR}/src/*.c)   # en Windows incluye los .C en mayusculas
file(GLOB_RECURSE PSYCROSS_SRCS_CPP ${PSYCROSS_DIR}/src/*.cpp)
set_source_files_properties(${PSYCROSS_SRCS_C} PROPERTIES LANGUAGE C)
set_source_files_properties(${PSYCROSS_SRCS_CPP} PROPERTIES LANGUAGE CXX)
add_library(psycross STATIC ${PSYCROSS_SRCS_C} ${PSYCROSS_SRCS_CPP})
target_include_directories(psycross PUBLIC ${PSYCROSS_DIR}/include ${PSYCROSS_DIR}/include/psx)
target_include_directories(psycross PUBLIC ${SDL2_DIR_ROOT}/include ${SDL2_DIR_ROOT}/include/SDL2)
target_compile_definitions(psycross PUBLIC RENDERER_OGL _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_link_libraries(psycross PUBLIC SDL2::SDL2 OpenAL::OpenAL opengl32)
if(MSVC)
    target_compile_options(psycross PRIVATE /W0)
endif()

# --- ENet (submodulo, v1.3.18): red UDP de las partidas en LAN -------------------
set(ENET_DIR ${TP}/enet)
add_library(enet STATIC
    ${ENET_DIR}/callbacks.c ${ENET_DIR}/compress.c ${ENET_DIR}/host.c ${ENET_DIR}/list.c
    ${ENET_DIR}/packet.c ${ENET_DIR}/peer.c ${ENET_DIR}/protocol.c ${ENET_DIR}/win32.c)
target_include_directories(enet PUBLIC ${ENET_DIR}/include)
target_compile_definitions(enet PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
target_link_libraries(enet PUBLIC ws2_32 winmm)
if(MSVC)
    target_compile_options(enet PRIVATE /W0)
endif()

# --- Juego ---------------------------------------------------------------------
file(GLOB GAME_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/game/*.c)
# El juego se compila como C++ (igual que REDRIVER2): con PGXP las coordenadas de las primitivas
# (VERTTYPE) son 'half' en C++ y convierten solas; en C habria que envolver cada una con _HF().
set_source_files_properties(${GAME_SRCS} PROPERTIES LANGUAGE CXX)
# Icono, datos de version y manifiesto del exe (res/): un exe sin ellos lo marcan mas los antivirus
configure_file(${CMAKE_SOURCE_DIR}/res/version.h.in ${CMAKE_BINARY_DIR}/version.h)
set_source_files_properties(${CMAKE_SOURCE_DIR}/res/nascar.rc PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}" OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/res/nascar.ico")
add_executable(nascar ${GAME_SRCS} res/nascar.rc res/nascar.manifest)
target_include_directories(nascar PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(nascar PRIVATE psycross enet)
if(MSVC)
    target_compile_options(nascar PRIVATE /W3 /wd4244)   # C4244: int -> half en coordenadas PGXP
    set_target_properties(nascar PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# Copia las DLL junto al ejecutable (OpenAL32.lib importa "OpenAL32.dll")
add_custom_command(TARGET nascar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL2_DIR_ROOT}/lib/x86/SDL2.dll $<TARGET_FILE_DIR:nascar>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENAL_DIR_ROOT}/bin/Win32/soft_oal.dll $<TARGET_FILE_DIR:nascar>/OpenAL32.dll)
