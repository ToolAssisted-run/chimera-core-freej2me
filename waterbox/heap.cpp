/* heap.cpp - where the Java heap is, for the core's memory domain (core.c).
 * Compiled for the guest only, with HotSpot's own flags (build-guest.py).
 * The VM is started with its whole heap committed (-Xms = -Xmx), so every
 * byte of the reserved range is the machine's memory, readable at any time.
 * SPDX-License-Identifier: MIT */
#include "gc/shared/genCollectedHeap.hpp"
#include "memory/universe.hpp"

extern "C" int chimera_java_heap(void **base, size_t *bytes)
{
	if (Universe::heap() == nullptr)
		return 0;
	const MemRegion r = GenCollectedHeap::heap()->reserved_region();
	*base = r.start();
	*bytes = r.byte_size();
	return 1;
}
