#pragma once

#include <cstdint>

#include "pstream.hh"

#ifdef XV6_KERNEL
#include "cpu.hh"
#endif

// XXX(Austin) With decent per-CPU static variables, we could just use
// a per-CPU variable for each of these stats, plus some static
// constructor magic to build a list of them, and we could avoid this
// nonsense.  OTOH, this struct is easy to get into user space.

#define KSTATS_TLB(X)                                                  \
  /* # of TLB shootdown operations.  One shootdown may target multiple \
   * cores. */                                                         \
  X(uint64_t, tlb_shootdown_count)                                     \
  /* Total number of targets of TLB shootdowns.  This divided by       \
   * tlb_shootdowns is the average number of targets per shootdown     \
   * operation. */                                                     \
  X(uint64_t, tlb_shootdown_targets)                                   \
  /* Total number of cycles spent in TLB shootdown operations. */      \
  X(uint64_t, tlb_shootdown_cycles)                                    \

#define KSTATS_VM(X)                            \
  X(uint64_t, page_fault_count)                 \
  X(uint64_t, page_fault_cycles)                \
  X(uint64_t, page_alloc_count)                 \
                                                \
  X(uint64_t, mmap_count)                       \
  X(uint64_t, mmap_cycles)                      \
                                                \
  X(uint64_t, munmap_count)                     \
  X(uint64_t, munmap_cycles)                    \

#define KSTATS_ALL(X)                           \
  KSTATS_TLB(X)                                 \
  KSTATS_VM(X)                                  \

struct kstats
{
#define X(type, name) type name;
  KSTATS_ALL(X)
#undef X

#ifdef XV6_KERNEL
  template<class T>
  static void inc(T kstats::* field, T delta = 1)
  {
    mykstats()->*field += delta;
  }

  class timer
  {
    uint64_t kstats::* field;
    uint64_t start;

  public:
    timer(uint64_t kstats::* field) : field(field), start(rdtsc()) { }

    ~timer()
    {
      end();
    }

    void end()
    {
      if (field)
        kstats::inc(field, rdtsc() - start);
      field = nullptr;
    }
  };
#endif

  kstats &operator+=(const kstats &o)
  {
#define X(type, name) name += o.name;
    KSTATS_ALL(X);
#undef X
    return *this;
  }

  kstats operator-(const kstats &b) const
  {
    kstats res{};
#define X(type, name) res.name = name - b.name;
    KSTATS_ALL(X);
#undef X
    return res;
  }
};

__attribute__((unused))
static void
to_stream(print_stream *s, const kstats &o)
{
#define X(type, name) s->println(o.name, " " #name);
  KSTATS_ALL(X);
#undef X
}