/* Solo en el port (depuracion, Windows): al fallar escribe la excepcion y la pila (funcion, fichero y linea, con
 * el .pdb) en la salida, antes del manejador de PsyCross, que guarda el .dmp. */
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "crash_log.h"

#pragma comment(lib, "dbghelp.lib")

static LPTOP_LEVEL_EXCEPTION_FILTER s_prev;

static LONG WINAPI CrashLog_Filter(EXCEPTION_POINTERS* ep)
{
    HANDLE proc = GetCurrentProcess(), thread = GetCurrentThread();
    printf("\n*** Excepcion 0x%08lx en %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, NULL, TRUE);
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf;
    memset(&sf, 0, sizeof(sf));
#ifdef _M_X64
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrStack.Offset = ctx.Rsp;
#else
    DWORD machine = IMAGE_FILE_MACHINE_I386;
    sf.AddrPC.Offset = ctx.Eip;
    sf.AddrFrame.Offset = ctx.Ebp;
    sf.AddrStack.Offset = ctx.Esp;
#endif
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 24; i++)
    {
        if (!StackWalk64(machine, proc, thread, &sf, &ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line;
        line.SizeOfStruct = sizeof(line);
        DWORD ldisp = 0;
        const char* name = SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym) ? sym->Name : "?";
        if (SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ldisp, &line))
            printf("  %2d %s  %s:%lu\n", i, name, line.FileName, line.LineNumber);
        else
            printf("  %2d %s  (%p)\n", i, name, (void*)(uintptr_t)sf.AddrPC.Offset);
    }
    fflush(stdout);
    return s_prev ? s_prev(ep) : EXCEPTION_CONTINUE_SEARCH;
}

void CrashLog_Install(void)
{
    s_prev = SetUnhandledExceptionFilter(CrashLog_Filter);
}
#else
void CrashLog_Install(void) {}
#endif
