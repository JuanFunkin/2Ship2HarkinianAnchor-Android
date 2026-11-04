// Android (API < 33) no trae backtrace()/backtrace_symbols(); ZAPD los usa solo para imprimir trazas al fallar.
// Se inyecta con -include en ZAPDLib. Devuelven "0 frames", asi que el codigo que las llama simplemente no imprime nada.
#pragma once
#ifdef __ANDROID__
#include <stddef.h>
#include <stdlib.h>

static inline int backtrace(void** buffer, int size) {
    (void)buffer;
    (void)size;
    return 0;
}

static inline char** backtrace_symbols(void* const* buffer, int size) {
    (void)buffer;
    (void)size;
    return (char**)calloc(1, sizeof(char*));
}
#endif
