#include "scale_common.h"

#define NUM_MMAPS 512 // Number mmaps per thread
#define NUM_PAGES (((NUM_MMAPS) * sizeof(char *) + (PAGE_SIZE)-1) / (PAGE_SIZE))

void *worker_thread(void *arg)
{
	thread_data_t *data = (thread_data_t *)arg;

    if (setaffinity(get_cpu_order(data->thread_id)) < 0) {
		die("setaffinity err");
		exit(EXIT_FAILURE);
	}

    char *my_region = (data->region + data->thread_id * NUM_PAGES * PAGE_SIZE);
    // Trigger page fault on each page
	for (size_t i = 0; i < NUM_PAGES; i++) {
		my_region[i * PAGE_SIZE] = 1;
	}

	long tsc_start, tsc_end;

	// Wait for that all threads are ready
	pthread_barrier_wait(&bar);

	tsc_start = rdtsc();

	// map them one by one, use preallocated region to store the pointers
	char **region = (char **)my_region;
	for (size_t i = 0; i < NUM_MMAPS; i++) {
		region[i] = (char*)mmap(0, PAGE_SIZE * 8, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	}

	tsc_end = rdtsc();
	long tot_time = get_time_in_nanos(tsc_start, tsc_end);

	data->lat = tot_time / NUM_MMAPS;

	for (size_t i = 0; i < NUM_MMAPS; i++) {
		munmap(region[i], PAGE_SIZE * 8);
	}

    munmap(my_region, PAGE_SIZE * NUM_PAGES);

	return NULL;
}

int main(int argc, char *argv[])
{
	return entry_point(argc, argv, worker_thread,
			   (test_config_t){ .num_prealloc_pages_per_thread =
						    NUM_PAGES,
					    .trigger_fault_before_spawn = 0 });
}