#include "input.h"
#include <stdint.h>

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

int input_parse_quantity(const char *s, Quantity *out) {
    int64_t n = 0;

    if (!s || !out || !*s) return 0;
    while (*s) {
        int d;
        if (!is_digit(*s)) return 0;
        d = *s++ - '0';
        if (n > (INT64_MAX - d) / 10) return 0;
        n = n * 10 + d;
    }

    if (!n) return 0;
    *out = n;
    return 1;
}

int input_parse_price(const char *s, Price *out) {
    int64_t whole = 0, fraction = 0;
    int decimals = 0;

    if (!s || !out || !is_digit(*s)) return 0;
    while (is_digit(*s)) {
        int d = *s++ - '0';
        if (whole > (INT64_MAX - d) / 10) return 0;
        whole = whole * 10 + d;
    }
    if (*s == '.') {
        ++s;
        while (is_digit(*s)) {
            if (++decimals > 2) return 0;
            fraction = fraction * 10 + (*s++ - '0');
        }
        if (!decimals) return 0;
    }
    if (*s) return 0;
    if (decimals == 1) fraction *= 10;
    if (whole > (INT64_MAX - fraction) / 100) return 0;
    whole = whole * 100 + fraction;
    if (!whole) return 0;
    *out = whole;
    return 1;
}

int input_parse_id(const char *s, uint64_t *out) {
    uint64_t n = 0;

    if (!s || !out || !*s) return 0;
    while (*s) {
        unsigned d;
        if (*s < '0' || *s > '9') return 0;
        d = (unsigned)(*s++ - '0');
        if (n > (UINT64_MAX - d) / 10) return 0;
        n = n * 10 + d;
    }
    *out = n; return 1;
}
