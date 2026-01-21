// MicroPython port configuration for KikiOS
// Full features enabled with double-precision floats

#include <stdint.h>

// Use full features ROM level (maximum Python compatibility)
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_FULL_FEATURES)

// Core features
#define MICROPY_ENABLE_COMPILER           (1)
#define MICROPY_ENABLE_GC                 (1)
#define MICROPY_HELPER_REPL               (1)
#define MICROPY_REPL_AUTO_INDENT          (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT    (0)

// Standard library modules - enable useful ones for app development
#define MICROPY_PY_JSON                   (1)  // JSON parsing - essential for web
#define MICROPY_PY_RE                     (1)  // Regex
#define MICROPY_PY_RANDOM                 (1)  // Random numbers
#define MICROPY_PY_MATH                   (1)  // Math functions (sin, cos, sqrt, etc.)
#define MICROPY_PY_CMATH                  (0)  // Complex math - not needed
#define MICROPY_PY_STRUCT                 (1)  // Binary data packing/unpacking
#define MICROPY_PY_BINASCII               (0)  // Base64 - needs lib/uzlib
#define MICROPY_PY_HEAPQ                  (1)  // Priority queues
#define MICROPY_PY_COLLECTIONS            (1)  // OrderedDict, deque, namedtuple
#define MICROPY_PY_HASHLIB                (0)  // Hashing - needs lib/crypto-algorithms
#define MICROPY_PY_PLATFORM               (0)  // Platform info - not useful
#define MICROPY_PY_TIME                   (0)  // Time - we have vibe.datetime()
#define MICROPY_PY_DEFLATE                (0)  // Compression - needs lib/uzlib
#define MICROPY_PY_FRAMEBUF               (0)  // Framebuffer - we have vibe module
#define MICROPY_PY_UCTYPES                (0)  // Low-level types - not needed
#define MICROPY_PY_ASYNCIO                (0)  // Async - complex, maybe later

// Minimal sys module (REPL needs it for prompts)
#define MICROPY_PY_SYS                    (1)
#define MICROPY_PY_SYS_MODULES            (0)
#define MICROPY_PY_SYS_EXIT               (1)
#define MICROPY_PY_SYS_PATH               (0)
#define MICROPY_PY_SYS_ARGV               (1)
#define MICROPY_PY_SYS_PS1_PS2            (1)
#define MICROPY_PY_SYS_STDIO_BUFFER       (0)
#define MICROPY_PY_SYS_STDFILES           (0)  // Don't use stream-based stdout (we use HAL)
#define MICROPY_PY_OS                     (0)
#define MICROPY_PY_IO                     (1)  // Needed for json module
#define MICROPY_PY_ERRNO                  (0)
#define MICROPY_PY_SELECT                 (0)
#define MICROPY_PY_THREAD                 (0)
#define MICROPY_PY_MACHINE                (0)
#define MICROPY_PY_NETWORK                (0)

// Enable double-precision floats (we have FPU enabled)
#define MICROPY_FLOAT_IMPL                (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_LONGINT_IMPL              (MICROPY_LONGINT_IMPL_MPZ)

// Memory
#define MICROPY_HEAP_SIZE                 (2 * 1024 * 1024)  // 2MB
#define MICROPY_ALLOC_PATH_MAX            (256)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT    (32)

// No frozen modules
#undef MICROPY_QSTR_EXTRA_POOL

// Use setjmp for non-local returns (works on aarch64)
#define MICROPY_GCREGS_SETJMP             (1)

// No native code generation
#define MICROPY_EMIT_ARM                  (0)
#define MICROPY_EMIT_THUMB                (0)
#define MICROPY_EMIT_INLINE_THUMB         (0)

// Type definitions
typedef long mp_int_t;
typedef unsigned long mp_uint_t;
typedef long mp_off_t;

#define MP_SSIZE_MAX LONG_MAX

// State
#define MP_STATE_PORT MP_STATE_VM

// Board name
#define MICROPY_HW_BOARD_NAME "KikiOS"
#define MICROPY_HW_MCU_NAME "aarch64"

// No alloca on freestanding - we'll use stack or malloc
#define MICROPY_NO_ALLOCA (1)

// KikiOS console doesn't support VT100 escape codes
#define MICROPY_HAL_HAS_VT100 (0)

// Enable user C modules (for vibe module)
#define MICROPY_MODULE_BUILTIN_INIT (1)
