#pragma once

#include <string.h>

static inline void
bzero(void* s, size_t n)
{
  memset(s, 0, n);
}
