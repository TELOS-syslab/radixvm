#pragma once

#include "ref.hh"
#include "refcache.hh"
#include "snzi.hh"
#include "gc.hh"
#include "types.h"

#include <cstddef>

// The page_info_map maps from physical address to page_info array.
// The map is indexed by (phys >> page_info_map_shift).  Since the
// page_info arrays live at the beginning of each region, this can
// also be used to find the page_info array of a given page_info*.
struct page_info_map_entry
{
  // The physical address of the page represented by array[0].
  paddr phys_base;
  // The page_info array, indexed by (phys - phys_base) / PGSIZE.
  class page_info *array;
};
extern page_info_map_entry page_info_map[512];
extern size_t page_info_map_shift;

// One past the last used entry of page_info_map.
extern page_info_map_entry *page_info_map_end;

class page_info : public PAGE_REFCOUNT referenced
{
protected:
  void onzero()
  {
    kfree(va());
  }

public:
  page_info() { }

  // Only placement new is allowed, because page_info must only be
  // constructed in the page_info_array.
  static void* operator new(unsigned long nbytes, page_info *buf)
  {
    return buf;
  }

  // Return the page_info for the page containing the given physical
  // address.  This may return an uninitialized page_info.  It is up
  // to the caller to construct the page_info (using placement new)
  // for pages that require reference counting upon allocation.
  static page_info *
  of(paddr pa)
  {
    page_info_map_entry *entry = &page_info_map[pa >> page_info_map_shift];
    assert(entry < page_info_map_end);
    auto index = (pa - entry->phys_base) / PGSIZE;
    return &entry->array[index];
  }

  // Return the page_info for the page at direct-mapped virtual
  // address va.
  static page_info *
  of(void *va)
  {
    return of(v2p(va));
  }

  paddr pa() const
  {
    paddr info_pa = (paddr)this - KBASE;
    page_info_map_entry *entry = &page_info_map[info_pa >> page_info_map_shift];
    return entry->phys_base + (this - entry->array) * PGSIZE;
  }

  void *va() const
  {
    return p2v(pa());
  }
};
