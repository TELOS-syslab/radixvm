#include "scale_common.h"

#define NUM_PAGES 16384 // Number of pages to allocate totally for mmap

void *worker_thread(void *arg)
{
	thread_start();

	size_t num_pages_per_thread = NUM_PAGES / data->tot_threads;

	// Trigger page fault one by one
	for (size_t i = 0; i < num_pages_per_thread; i++) {
		data->base[(data->thread_id * num_pages_per_thread + i) *
			   PAGE_SIZE] = 1;
	}

    // Barrier at here. Output pagetable size and vma tree size.
    pthread_barrier_wait(&bar0);
    if (data->thread_id == 0) {
        printf("%lu, %lu\n", pt_pages(), radix_size());
    }
	pthread_barrier_wait(&bar1);

	thread_end(num_pages_per_thread);
}

int main(int argc, char *argv[])
{
	printf("***MEM_USAGE_SIM***\n");

	test_config_t config;
	config.num_total_pages = NUM_PAGES;
	config.num_requests_per_thread = 0;
	config.num_pages_per_request = 0;
	config.mmap_before_spawn = 0;
	config.trigger_fault_before_spawn = 0;
	config.multi_vma_assign_requests = 0;
	config.contention_level = 0;
	config.is_unfixed_mmap_test = 0;

	int ret = entry_point(argc, argv, worker_thread, config);
	if (ret != 0) {
		die("entry_point failed");
		exit(EXIT_FAILURE);
	}
	printf("\n");
}