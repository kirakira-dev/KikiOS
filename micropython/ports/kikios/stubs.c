// Stubs for MicroPython on KikiOS

#include <stddef.h>
#include "py/mphal.h"
#include "py/obj.h"

// strchr needs to be a real function (not just inline) because objstr.c uses it
char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : (void *)0;
}

// VT100 stubs - KikiOS console doesn't support escape codes
void mp_hal_move_cursor_back(unsigned int pos) {
    while (pos--) {
        mp_hal_stdout_tx_strn("\b", 1);
    }
}

void mp_hal_erase_line_from_cursor(unsigned int n_chars) {
    for (unsigned int i = 0; i < n_chars; i++) {
        mp_hal_stdout_tx_strn(" ", 1);
    }
    mp_hal_move_cursor_back(n_chars);
}

// ============ Math library functions ============
// Implementations for functions not provided by math.h inline functions
// Note: math.h provides hardware-accelerated sqrt, fabs, floor, ceil, fmod, sin, cos, exp, log, pow, acos

// Helper - use hardware fabs from math.h
static double my_fabs(double x) {
    double result;
    __asm__ __volatile__("fabs %d0, %d1" : "=w"(result) : "w"(x));
    return result;
}

double nan(const char *s) { (void)s; return 0.0 / 0.0; }

double copysign(double x, double y) {
    union { double d; unsigned long long i; } ux = {x}, uy = {y};
    ux.i = (ux.i & 0x7FFFFFFFFFFFFFFFULL) | (uy.i & 0x8000000000000000ULL);
    return ux.d;
}

double nearbyint(double x) {
    return (double)(long long)(x + (x < 0 ? -0.5 : 0.5));
}

double trunc(double x) { return (double)(long long)x; }

double expm1(double x) { return exp(x) - 1.0; }
double log10(double x) { return log(x) / 2.302585092994046; }
double log2(double x) { return log(x) / 0.693147180559945; }

double tan(double x) { return sin(x) / cos(x); }

double asin(double x) {
    if (x < -1 || x > 1) return nan("");
    double sum = x, term = x;
    for (int i = 1; i < 30; i++) {
        term *= x * x * (2*i - 1) * (2*i - 1) / (2*i * (2*i + 1));
        sum += term;
        if (my_fabs(term) < 1e-15) break;
    }
    return sum;
}

double atan(double x) {
    if (my_fabs(x) > 1) {
        return (x > 0 ? 1 : -1) * 1.5707963267949 - atan(1/x);
    }
    double sum = 0, term = x;
    for (int i = 0; i < 50; i++) {
        sum += term / (2*i + 1);
        term *= -x * x;
        if (my_fabs(term / (2*i + 3)) < 1e-15) break;
    }
    return sum;
}

double atan2(double y, double x) {
    if (x > 0) return atan(y / x);
    if (x < 0 && y >= 0) return atan(y / x) + 3.14159265358979;
    if (x < 0 && y < 0) return atan(y / x) - 3.14159265358979;
    if (x == 0 && y > 0) return 1.5707963267949;
    if (x == 0 && y < 0) return -1.5707963267949;
    return 0;
}

// Hyperbolic functions
double sinh(double x) { return (exp(x) - exp(-x)) / 2; }
double cosh(double x) { return (exp(x) + exp(-x)) / 2; }
double tanh(double x) { return sinh(x) / cosh(x); }
double asinh(double x) { return log(x + sqrt(x*x + 1)); }
double acosh(double x) { return log(x + sqrt(x*x - 1)); }
double atanh(double x) { return 0.5 * log((1 + x) / (1 - x)); }

// Error and gamma functions (simplified stubs)
double erf(double x) {
    // Approximation
    double t = 1.0 / (1.0 + 0.5 * my_fabs(x));
    double tau = t * exp(-x*x - 1.26551223 + t*(1.00002368 + t*(0.37409196 +
        t*(0.09678418 + t*(-0.18628806 + t*(0.27886807 + t*(-1.13520398 +
        t*(1.48851587 + t*(-0.82215223 + t*0.17087277)))))))));
    return x >= 0 ? 1 - tau : tau - 1;
}

double erfc(double x) { return 1 - erf(x); }
double tgamma(double x) { (void)x; return 1.0; }  // Stub - complex to implement
double lgamma(double x) { (void)x; return 0.0; }  // Stub

// Float decomposition functions
double modf(double x, double *iptr) {
    double i = trunc(x);
    *iptr = i;
    return x - i;
}

double ldexp(double x, int e) {
    while (e > 0) { x *= 2.0; e--; }
    while (e < 0) { x /= 2.0; e++; }
    return x;
}

double frexp(double x, int *e) {
    if (x == 0) { *e = 0; return 0; }
    int exp = 0;
    double ax = x < 0 ? -x : x;
    while (ax >= 1.0) { ax /= 2.0; exp++; }
    while (ax < 0.5) { ax *= 2.0; exp--; }
    *e = exp;
    return x < 0 ? -ax : ax;
}

// Note: sys.stdin/stdout/stderr stubs removed
// With MICROPY_PY_SYS_STDFILES=0, print() uses mp_hal_stdout_tx_strn instead

// Stub for open() builtin - we don't support file I/O
#include "py/runtime.h"
#include "py/objfun.h"

static mp_obj_t mp_builtin_open_stub(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)n_args; (void)args; (void)kwargs;
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("open() not supported"));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open_stub);
