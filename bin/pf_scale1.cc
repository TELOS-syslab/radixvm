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
#define NUM_PAGES 4096 // Number of pages to allocate for mmap
#define WARMUP_ITERATIONS 10
#define TEST_ITERATIONS 50
#define MAX_THREADS 96

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


uint64_t get_time_in_cycles(uint64_t start, uint64_t end)
{
	return end - start;
}

typedef struct {
	char *region;
	size_t pages;
	int thread_id;
	int num_threads;
	int nr_workers;
} thread_data_t;

int DISPATCH_LIGHT;
int FINISHED_WORKERS;
long THREAD_TOTAL_TIME;
uint64_t time_end;

void *worker_thread(void *arg)
{
	thread_data_t *data = (thread_data_t *)arg;
	if (setaffinity(get_cpu_order(data->thread_id)) < 0) {
		die("setaffinity err");
		exit(EXIT_FAILURE);
	}

	size_t pages_per_thread = data->pages / data->num_threads;
	size_t start_page = data->thread_id * pages_per_thread;
	size_t end_page = start_page + pages_per_thread;
	int nr_workers = data->nr_workers;

	uint64_t thread_time_start, thread_time_end;

	// Wait for the main thread to signal that all threads are ready
	while (__atomic_load_n(&DISPATCH_LIGHT, __ATOMIC_ACQUIRE) == 0) {
		yield();
	}

	thread_time_start = rdtsc();

	for (size_t i = start_page; i < end_page; i++) {
		data->region[i * PAGE_SIZE] = 1; // Trigger page fault
	}

	thread_time_end = rdtsc();
	long time = get_time_in_cycles(thread_time_start, thread_time_end);

	if (__atomic_add_fetch(&FINISHED_WORKERS, 1, __ATOMIC_RELEASE) ==
	    nr_workers) {
		time_end = thread_time_end;
	}

	__atomic_add_fetch(&THREAD_TOTAL_TIME, time, __ATOMIC_RELEASE);

	return NULL;
}

typedef struct {
	long completion_time;
	long per_thread_time;
} test_result_t;

void run_test(test_result_t *result, int num_threads)
{
	pthread_t threads[num_threads];
	thread_data_t thread_data[num_threads];

	char *region = (char*)mmap(NULL, NUM_PAGES * PAGE_SIZE, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (region == MAP_FAILED) {
		die("mmap failed");
		exit(EXIT_FAILURE);
	}

	// Initialize global variables
	__atomic_clear(&DISPATCH_LIGHT, __ATOMIC_RELEASE);
	__atomic_store_n(&FINISHED_WORKERS, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&THREAD_TOTAL_TIME, 0, __ATOMIC_RELEASE);

	uint64_t start;

	// Create threads and trigger page faults in parallel
	for (int i = 0; i < num_threads; i++) {
		thread_data[i].region = region;
		thread_data[i].pages = NUM_PAGES;
		thread_data[i].thread_id = i;
		thread_data[i].num_threads = num_threads;
		thread_data[i].nr_workers = num_threads;

		// Set the thread affinity to a specific core
		if (setaffinity(get_cpu_order(i)) < 0) {
			die("setaffinity err");
			exit(EXIT_FAILURE);
		}

		if (xthread_create(&threads[i], 0, worker_thread,
				   &thread_data[i]) != 0) {
			die("pthread_create failed");
			exit(EXIT_FAILURE);
		}
	}
	if (setaffinity(get_cpu_order(0)) < 0) {
	    die("setaffinity err");
		exit(EXIT_FAILURE);
	}

	// Signal all threads to start
	start = rdtsc();
	__atomic_store_n(&DISPATCH_LIGHT, 1, __ATOMIC_RELEASE);

	// Join threads
	for (int i = 0; i < num_threads; i++) {
		xpthread_join(threads[i]);
	}

	result->completion_time = get_time_in_cycles(start, time_end);
	long thread_total_time =
		__atomic_load_n(&THREAD_TOTAL_TIME, __ATOMIC_ACQUIRE);
	result->per_thread_time = thread_total_time / num_threads;

	munmap(region, NUM_PAGES * PAGE_SIZE);
}

void run_multiple_avg_test(int num_threads)
{
	for (int i = 0; i < WARMUP_ITERATIONS; i++) {
		test_result_t result;
		run_test(&result, num_threads);
	}

	// Calculate average time excluding the best and worst results
	long min = 0x7FFFFFFFFFFFFFFF;
	int min_index = 0;
	long max = 0;
	int max_index = 0;
	test_result_t test_result[TEST_ITERATIONS];

	for (int i = 0; i < TEST_ITERATIONS; i++) {
		run_test(&test_result[i], num_threads);

		if (test_result[i].completion_time < min) {
			min = test_result[i].completion_time;
			min_index = i;
		}
		if (test_result[i].completion_time > max) {
			max = test_result[i].completion_time;
			max_index = i;
		}
	}

	test_result_t avg;
	avg.completion_time = 0;
	avg.per_thread_time = 0;

	for (int i = 0; i < TEST_ITERATIONS; i++) {
		if (i != min_index && i != max_index) {
			avg.completion_time += test_result[i].completion_time /
					       (TEST_ITERATIONS - 2);
			avg.per_thread_time += test_result[i].per_thread_time /
					       (TEST_ITERATIONS - 2);
		}
	}

	printf("%d, %.6f, %.6f\n", num_threads,
	       (double)avg.completion_time / cpuhz(),
	       (double)avg.per_thread_time / cpuhz());
}

int main(int argc, char *argv[])
{
	printf("Threads, Completion Time (s), Per-Thread Time (s)\n");

	// Usage: ./pf_scale [num_threads_from] [num_threads_to]

	int num_threads_from, num_threads_to;
	if (argc == 1) {
		num_threads_from = 1;
		num_threads_to = MAX_THREADS;
	} else if (argc == 2) {
		num_threads_from = atoi(argv[1]);
		num_threads_to = num_threads_from;
	} else if (argc == 3) {
		num_threads_from = atoi(argv[1]);
		num_threads_to = atoi(argv[2]);
	} else {
		fprintf(stderr,
			"Usage: %s [num_threads_from] [num_threads_to]\n",
			argv[0]);
		exit(EXIT_FAILURE);
	}

	for (int num_threads = num_threads_from; num_threads <= num_threads_to;
	     num_threads++) {
		// Spawn a process for a test in order to avoid interference between tests
		int pid = fork(0);
		if (pid == -1) {
			die("fork failed");
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			// Child process
			run_multiple_avg_test(num_threads);
			exit(EXIT_SUCCESS);
		} else {
			// Parent process
			wait(pid);
		}
	}

	return 0;
}