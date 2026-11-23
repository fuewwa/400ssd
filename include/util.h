#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

void die(const char *fmt, ...);
void *xcalloc(size_t n, size_t sz);
size_t utf8_encode(uint32_t cp, char *out);

#endif
