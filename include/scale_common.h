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

#if defined(XV6_USER) && defined(HW_ben)
int get_cpu_order(int thread)
{
  const int cpu_order[] = {
    // Socket 0
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    // Socket 1
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    // Socket 3
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    // Socket 2
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    // Socket 5
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
    // Socket 4
    40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    // Socket 6
    60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
    // Socket 7
    70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
  };

  return cpu_order[thread];
}
#else
int get_cpu_order(int thread)
{
  return thread;
}
#endif

long get_time_in_nanos(long start_tsc, long end_tsc)
{
	// Assume a 2.6 GHz CPU
	return (end_tsc - start_tsc) / 2.6;
}

unsigned int simple_get_rand(unsigned int last_rand)
{
	return ((long)last_rand * 1103515245 + 12345) & 0x7fffffff;
}

#define PAGE_SIZE 4096 // Typical page size in bytes
#define WARMUP_ITERATIONS 1
// Single thread test, will be executed 2048 times;
// 2 thread test will be executed 1024 times;
// 128 thread test will be excuted 16 times, etc.
// Statistics are per-thread basis.
#define TOT_THREAD_RUNS 2048

typedef struct {
	char *region;
	size_t region_size;
	int thread_id;
	// Pass the result back to the main thread
	long lat;
} thread_data_t;

static pthread_barrier_t bar;

typedef struct {
	size_t num_prealloc_pages_per_thread;
	int trigger_fault_before_spawn;
} test_config_t;

typedef struct {
	long max_lat;
	long min_lat;
	long avg_lat;
	long posvar2_lat;
	long negvar2_lat;
} test_result_t;

// Decls

int entry_point(int argc, char *argv[], void *(*worker_thread)(void *),
		test_config_t config);
void run_test_specify_threads(int num_threads, void *(*worker_thread)(void *),
			      test_config_t config);
void run_test_with_warmup(int num_threads, void *(*worker_thread)(void *),
			  test_config_t config);
void run_test(test_result_t *result, int num_threads,
	      void *(*worker_thread)(void *), test_config_t config);

// Impls

int entry_point(int argc, char *argv[], void *(*worker_thread)(void *),
		test_config_t config)
{
	int num_threads;
	if (argc == 1) {
		num_threads = -1;
	} else if (argc == 2) {
		num_threads = atoi(argv[1]);
	} else {
		fprintf(stderr, "Usage: %s [num_threads]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	run_test_specify_threads(num_threads, worker_thread, config);

	return 0;
}

void run_test_specify_threads(int num_threads, void *(*worker_thread)(void *),
			      test_config_t config)
{
	printf("Threads, Min lat (ns), Avg lat (ns), Max lat (ns), Pos err lat (ns2), Neg err lat (ns2)\n");

	if (num_threads == -1) {
		// int threads[] = { 1, 16, 32, 48, 64 };
		int threads[] = { 1, 2, 4, 8 };
		for (int i = 0; i < sizeof(threads) / sizeof(int); i++) {
			run_test_with_warmup(threads[i], worker_thread, config);
		}
	} else {
		run_test_with_warmup(num_threads, worker_thread, config);
	}
}

void run_test_with_warmup(int num_threads, void *(*worker_thread)(void *),
			  test_config_t config)
{
	// Spawn a process for a test in order to avoid interference between tests
	int pid = xfork();
	if (pid == -1) {
		die("fork failed");
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		// Child process

		for (int i = 0; i < WARMUP_ITERATIONS; i++) {
			test_result_t result;
			run_test(&result, num_threads, worker_thread, config);
		}

		test_result_t result;
		run_test(&result, num_threads, worker_thread, config);

		printf("%d, %ld, %ld, %ld, %ld, %ld\n", num_threads,
		       result.min_lat, result.avg_lat, result.max_lat,
		       result.posvar2_lat, result.negvar2_lat);

		exit(EXIT_SUCCESS);
	} else {
		// Parent process
		wait(pid);
	}
}

pthread_t threads[TOT_THREAD_RUNS];
thread_data_t thread_data[TOT_THREAD_RUNS];

void run_test(test_result_t *result, int num_threads,
	      void *(*worker_thread)(void *), test_config_t config)
{
	size_t num_prealloc_pages = config.num_prealloc_pages_per_thread;
	int trigger_fault_before_spawn = config.trigger_fault_before_spawn;
	int runs = TOT_THREAD_RUNS / num_threads;

	for (int run_id = 0; run_id < runs; run_id++) {
		pthread_barrier_init(&bar, 0, num_threads);

		char *region =
			(char*)mmap(0, num_prealloc_pages * PAGE_SIZE * num_threads,
			     PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (region == MAP_FAILED) {
			die("mmap failed");
			exit(EXIT_FAILURE);
		}

		if (trigger_fault_before_spawn) {
			// Trigger page faults before spawning threads
			for (int i = 0; i < num_prealloc_pages * num_threads;
			     i++) {
				region[i * PAGE_SIZE] = 1;
			}
		}

		// Create threads and trigger page faults in parallel
		for (int i = 0; i < num_threads; i++) {
			int thread_id = run_id * num_threads + i;
			thread_data[thread_id].region = region;
			thread_data[thread_id].region_size =
				num_prealloc_pages * PAGE_SIZE;
			thread_data[thread_id].thread_id = i;

			if (xthread_create(&threads[thread_id], 0,
					   worker_thread,
					   &thread_data[thread_id]) != 0) {
				die("pthread_create failed");
				exit(EXIT_FAILURE);
			}
		}

		// Join threads
		for (int i = 0; i < num_threads; i++) {
			int thread_id = run_id * num_threads + i;
			xpthread_join(threads[thread_id]);
		}

		if (trigger_fault_before_spawn) {
			munmap(region, num_prealloc_pages * PAGE_SIZE * num_threads);
		}
	}

	// Calculate the maximum, minimum, average, and variance of the latencies
	int tot_runs = num_threads * runs;

	long max = 0;
	long min = 0x7FFFFFFFFFFFFFFF;
	long avg = 0;
	for (int i = 0; i < tot_runs; i++) {
		if (thread_data[i].lat > max) {
			max = thread_data[i].lat;
		}
		if (thread_data[i].lat < min) {
			min = thread_data[i].lat;
		}
		avg += thread_data[i].lat;
	}
	avg /= tot_runs;
	long posvar2 = 0;
	long numpos = 0;
	long negvar2 = 0;
	long numneg = 0;
	for (int i = 0; i < tot_runs; i++) {
		long diff = thread_data[i].lat - avg;
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

	result->max_lat = max;
	result->min_lat = min;
	result->avg_lat = avg;
	result->posvar2_lat = posvar2;
	result->negvar2_lat = negvar2;
}