#include <atomic>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "libutil.h"
#include "amd64.h"
#include "rnd.hh"
#include "xsys.h"

#if !defined(XV6_USER)
#include <pthread.h>
#include <sys/wait.h>
#else
#include "types.h"
#include "user.h"
#include "pthread.h"
#include "bits.hh"
#include "kstats.hh"
#endif

#define PAGE_SIZE 4096 // Typical page size in bytes
#define MAX_PAGES 262144
#define WARMUP_ITERATIONS 64
#define TEST_ITERATIONS 64

long get_time_in_nanos(long start_tsc, long end_tsc)
{
	// Assume a 2.6 GHz CPU
	return (end_tsc - start_tsc) / 2.6;
}

long run_test(int num_pages)
{
	int region_size = num_pages * PAGE_SIZE;

	char *region = (char*)mmap(0, region_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (region == MAP_FAILED) {
		die("mmap failed");
		exit(EXIT_FAILURE);
	}

	// Trigger page fault on every pages
	for (size_t i = 0; i < num_pages; i++) {
		region[i * PAGE_SIZE] = 1;
	}

	long tsc_start, tsc_end;
	tsc_start = rdtsc();

	// Fork
	int pid = xfork();
	if (pid == -1) {
		die("fork failed");
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		// Child
		exit(EXIT_SUCCESS);
	} else {
		// Parent
		wait(pid);
		tsc_end = rdtsc();
	}

	munmap(region, region_size);

	return get_time_in_nanos(tsc_start, tsc_end);
}

long lat[TEST_ITERATIONS];

int main(int argc, char *argv[])
{
	printf("Pages, Min lat (ns), Avg lat (ns), Max lat (ns), Pos err lat (ns2), Neg err lat (ns2)\n");

	for (int num_pages = 1; num_pages <= MAX_PAGES; num_pages <<= 1) {
		for (int i = 0; i < WARMUP_ITERATIONS; i++) {
			run_test(num_pages);
		}

		for (int i = 0; i < TEST_ITERATIONS; i++) {
			lat[i] = run_test(num_pages);
		}

		// Calculate the maximum, minimum, average, and variance of the latencies
		long max = 0;
		long min = 0x7FFFFFFFFFFFFFFF;
		long avg = 0;
		for (int i = 0; i < TEST_ITERATIONS; i++) {
			if (lat[i] > max) {
				max = lat[i];
			}
			if (lat[i] < min) {
				min = lat[i];
			}
			avg += lat[i];
		}
		avg /= TEST_ITERATIONS;
		long posvar2 = 0;
		long numpos = 0;
		long negvar2 = 0;
		long numneg = 0;
		for (int i = 0; i < TEST_ITERATIONS; i++) {
			long diff = lat[i] - avg;
			if (diff > 0) {
				posvar2 += diff * diff;
				numpos++;
			} else {
				negvar2 += diff * diff;
				numneg++;
			}
		}
		posvar2 /= numpos;
		negvar2 /= numneg;

		printf("%d, %ld, %ld, %ld, %ld, %ld\n", num_pages,
				min, avg, max,
				posvar2, negvar2);
	}

	return 0;
}
