/*
 * Windows API Stubs for KikiOS
 * 
 * Provides emulation of common Windows API functions
 */

#ifndef _WINAPI_H
#define _WINAPI_H

#include <stdint.h>
#include "x86emu.h"

// Windows data types
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t  BYTE;
typedef int32_t  BOOL;
typedef uint32_t HANDLE;
typedef uint32_t LPVOID;
typedef uint32_t LPCSTR;
typedef uint32_t LPSTR;

// Standard handles
#define STD_INPUT_HANDLE  ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)

// Pseudo-handles
#define WINAPI_STDIN   0x100
#define WINAPI_STDOUT  0x101
#define WINAPI_STDERR  0x102

// Initialize Windows API emulation
void winapi_init(void);

// Windows API call handler
// Returns: 0 = not handled, 1 = handled, -1 = exit
int winapi_call(x86emu_state_t *emu, const char *dll, const char *func);

// Console output callback
void winapi_set_console_output(void (*func)(const char *str, int len));

// Get exit code
int winapi_get_exit_code(void);

#endif // _WINAPI_H
