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
	// Our setup is a 1.9 GHz CPU
	return (end_tsc - start_tsc) * 10 / 19;
}

unsigned int simple_get_rand(unsigned int last_rand)
{
	return ((long)last_rand * 1103515245 + 12345) & 0x7fffffff;
}

size_t up_align(size_t size, size_t alignment)
{
	return ((size) + ((alignment)-1)) & ~((alignment)-1);
}

#define PAGE_SIZE 4096 // Typical page size in bytes
// Single thread test, will be executed 4096 times;
// 8 thread test will be executed 512 times;
// 128 thread test will be executed 32 times, etc.
// Statistics are per-thread basis.
#define TOT_THREAD_RUNS 4096

char *const BASE_PTR = (char *)0x100000000UL;

#define RESULT_FILE "results"

static pthread_barrier_t bar, bar0, bar1;

#define thread_start()                                                     \
	thread_data_t *data = (thread_data_t *)arg;                        \
    if (setaffinity(get_cpu_order(data->thread_id)) < 0) {             \
		die("setaffinity err");                                        \
		exit(EXIT_FAILURE);                                            \
	}                                                                  \
	long tsc_start, tsc_end;                                           \
    /* Wait for that all threads are ready */                          \
	pthread_barrier_wait(&bar);                                        \
	tsc_start = rdtsc();

#define thread_end(num_requests)                               \
	tsc_end = rdtsc();                                     \
	long tot_time = get_time_in_nanos(tsc_start, tsc_end); \
	data->lat = tot_time / (num_requests);                 \
	return NULL;

typedef struct {
	char *base;
	long *offset;
	int thread_id;
	int tot_threads;
	int is_unfixed_mmap_test;

	// Pass the latency(ns) result back to the main thread
	long lat;
} thread_data_t;

typedef struct {
	// Only for mem usage tests
	size_t num_total_pages;

	// Only for time usage tests
	size_t num_requests_per_thread;
	size_t num_pages_per_request;
	int mmap_before_spawn;
	int trigger_fault_before_spawn;
	int multi_vma_assign_requests;
	int contention_level;
	int is_unfixed_mmap_test;
} test_config_t;

const char contention_level_name[3][20] = { "LOW_CONTENTION", "MID_CONTENTION",
					    "HIGH_CONTENTION" };

typedef struct {
	long avglat;
	long medlat;
	long p99lat;
} latency_result_t;

// Decls

int entry_point(int argc, char *argv[], void *(*worker_thread)(void *),
		test_config_t config);
void run_test_specify_threads(int num_threads, void *(*worker_thread)(void *),
			      test_config_t config);
void run_test_and_print(int num_threads, void *(*worker_thread)(void *),
			test_config_t config);
void run_test_specify_rounds(int num_threads, void *(*worker_thread)(void *),
			     test_config_t config, latency_result_t *result);
void run_test_forked(int num_threads, void *(*worker_thread)(void *),
		     test_config_t config);
void run_test(int num_threads, void *(*worker_thread)(void *),
	      test_config_t config);

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
	printf("Threads, Avg Lat (ns), Med Lat (ns), p99 Lat (ns)\n");

	// We don't have sched_getaffinity, so we simply set num_cpus to 128.
	// cpu_set_t cpuset;
	// if (sched_getaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
	// 	perror("sched_getaffinity failed");
	// 	exit(EXIT_FAILURE);
	// }
	// int num_cpus = CPU_COUNT(&cpuset);
	int num_cpus = 128;

	if (num_threads == -1) {
		int threads[] = { 1, 2, 4, 8, 16, 32, 48, 64, 80, 96, 112, 128 };
		for (int i = 0; i < sizeof(threads) / sizeof(int); i++) {
			if (threads[i] > num_cpus)
				break;
			run_test_and_print(threads[i], worker_thread, config);
		}
	} else {
		run_test_and_print(num_threads, worker_thread, config);
	}
}

void run_test_and_print(int num_threads, void *(*worker_thread)(void *),
			test_config_t config)
{
	latency_result_t result;
	run_test_specify_rounds(num_threads, worker_thread, config, &result);
	printf("%d, %ld, %ld, %ld\n", num_threads, result.avglat, result.medlat,
	       result.p99lat);
}

pthread_t threads[TOT_THREAD_RUNS];
thread_data_t thread_data[TOT_THREAD_RUNS];
long thread_lat[TOT_THREAD_RUNS];

void run_test_specify_rounds(int num_threads, void *(*worker_thread)(void *),
			     test_config_t config, latency_result_t *result)
{
	unlink(RESULT_FILE);

	int runs = TOT_THREAD_RUNS / num_threads;
	for (int run_id = 0; run_id < runs; run_id++) {
		run_test_forked(num_threads, worker_thread, config);
	}

	// Read latencies data from RESULT_FILE
    int fd = open(RESULT_FILE, O_RDONLY);
	if (fd == -1) {
		die("open failed");
		exit(EXIT_FAILURE);
	}
	int tot_runs = 0;
	while (1) {
		long lat = 0;
		int r = read(fd, &lat, sizeof(lat));
		if (r<0) {
			close(fd);
			die("readline failed");
		}
		else if (r==0)
			break;
		else {
			thread_lat[tot_runs++] = lat;
		}
	}
	close(fd);

    if (tot_runs != num_threads * runs) {
		die("Incorrect number of runs");
		exit(EXIT_FAILURE);
	}

	long avglat = 0, medlat = 0, p99lat = 0;

	for (int i = 0; i < tot_runs; i++) {
		avglat += thread_lat[i];
	}
	avglat /= tot_runs;

	// Calculate the p99 lat
	// bubble sort
	for (int i = 0; i < tot_runs; i++) {
		for (int j = i + 1; j < tot_runs; j++) {
			if (thread_lat[i] > thread_lat[j]) {
				long temp = thread_lat[i];
				thread_lat[i] = thread_lat[j];
				thread_lat[j] = temp;
			}
		}
	}
	medlat = thread_lat[tot_runs / 2];
	p99lat = thread_lat[tot_runs * 99 / 100];

	result->avglat = avglat;
	result->medlat = medlat;
	result->p99lat = p99lat;

	return;
}

void run_test_forked(int num_threads, void *(*worker_thread)(void *),
		     test_config_t config)
{
	// Spawn a process for a test in order to avoid interference between tests
	int pid = xfork();
	if (pid == -1) {
		die("fork failed");
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		// Child process
		run_test(num_threads, worker_thread, config);
		exit(EXIT_SUCCESS);
	} else {
		// Parent process
		wait(pid);
	}
}

void run_test(int num_threads, void *(*worker_thread)(void *),
	      test_config_t cfg)
{
	char *base = NULL;
	long *offset = NULL;

	pthread_barrier_init(&bar, 0, num_threads);

	if (cfg.num_total_pages != 0) {
        // Memory usage tests
        pthread_barrier_init(&bar0, 0, num_threads);
        pthread_barrier_init(&bar1, 0, num_threads);

		base = (char *)mmap(NULL, cfg.num_total_pages * PAGE_SIZE,
			    PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
			    -1, 0);
		if (base == MAP_FAILED) {
			die("mmap failed");
			exit(EXIT_FAILURE);
		}
	} else {
		// Time usage tests
		size_t num_tot_requests =
			cfg.num_requests_per_thread * num_threads;

		int offset_size =
			up_align(num_tot_requests * sizeof(long), PAGE_SIZE);
		offset = (long *)mmap(NULL, offset_size, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (offset == MAP_FAILED) {
			die("mmap failed");
			exit(EXIT_FAILURE);
		}

		unsigned long reserved_region_size =
			cfg.num_pages_per_request * PAGE_SIZE;

		for (int i = 0; i < num_tot_requests; i++) {
			offset[i] = i * reserved_region_size;
		}

		if (cfg.mmap_before_spawn) {
			// munmap or pagefault tests
			if (cfg.multi_vma_assign_requests) {
				// Each request is in a VMA
				base = BASE_PTR;
				for (int i = 0; i < num_tot_requests; i++) {
					char *temp =
						(char *)mmap(base + offset[i],
						     reserved_region_size,
						     PROT_READ | PROT_WRITE,
						     MAP_PRIVATE | MAP_FIXED |
							     MAP_ANONYMOUS,
						     -1, 0);
					if (temp == MAP_FAILED) {
						die("mmap failed");
						exit(EXIT_FAILURE);
					}
				}
			} else {
				// All requests are in one VMA
				base = (char *)mmap(NULL,
					    num_tot_requests *
						    reserved_region_size,
					    PROT_READ | PROT_WRITE,
					    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (base == MAP_FAILED) {
					die("mmap failed");
					exit(EXIT_FAILURE);
				}
			}

			if (cfg.trigger_fault_before_spawn) {
				// Trigger page faults before spawning threads
				for (int i = 0; i < num_tot_requests; i++) {
					char *region = base + offset[i];
					for (int j = 0;
					     j < cfg.num_pages_per_request; j++)
						region[j * PAGE_SIZE] = 1;
				}
			}
		} else {
			// mmap tests
			if (cfg.is_unfixed_mmap_test) {
				base = NULL;
			} else {
				base = BASE_PTR;
			}
		}

		if (cfg.contention_level == 0) {
			// Low Contention
			// Do nothing.
		} else if (cfg.contention_level == 1) {
			// Medium Contention
			// Random shuffle
			unsigned int rand = 0xdeadbeef - num_threads;
			for (int i = num_tot_requests - 1; i > 0; i--) {
				rand = simple_get_rand(rand);
				int j = rand % (i + 1);
				long temp = offset[i];
				offset[i] = offset[j];
				offset[j] = temp;
			}
		} else if (cfg.contention_level == 2) {
			// High Contention
			for (int i = 0; i < num_threads; i++) {
				int idx = i;
				for (int j = i * cfg.num_requests_per_thread;
				     j < (i + 1) * cfg.num_requests_per_thread;
				     j++) {
					offset[j] = idx * reserved_region_size;
					idx += num_threads;
				}
			}
		} else {
			die("Invalid Contention Level");
			exit(EXIT_FAILURE);
		}
	}

	// Create threads and trigger page faults in parallel
	for (int i = 0; i < num_threads; i++) {
		thread_data[i].base = base;
		if (offset != NULL) {
			thread_data[i].offset =
				offset + i * cfg.num_requests_per_thread;
		}
		thread_data[i].thread_id = i;
		thread_data[i].tot_threads = num_threads;
		thread_data[i].is_unfixed_mmap_test = cfg.is_unfixed_mmap_test;

		if (xthread_create(&threads[i], 0, worker_thread,
				   &thread_data[i]) != 0) {
			die("pthread_create failed");
			exit(EXIT_FAILURE);
		}
	}

	// Join threads
	for (int i = 0; i < num_threads; i++) {
		xpthread_join(threads[i]);
	}

	// Write latencies data to RESULT_FILE
    int fd = open(RESULT_FILE, O_RDWR | O_APPEND | O_CREAT, 0666);
	if (fd == -1) {
		die("open failed");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < num_threads; i++) {
		write(fd, &(thread_data[i].lat), sizeof(thread_data[i].lat));
	}
	close(fd);
}