#include "scale_common.h"

#define NUM_PAGES 1024 // Number of pages to allocate per thread for mmap

void *worker_thread(void *arg)
{
	thread_start();

	// Trigger page fault one by one
	for (size_t i = 0; i < NUM_PAGES; i++) {
		data->base[data->offset[i]] = 1;
	}

	thread_end(NUM_PAGES);
}

int main(int argc, char *argv[])
{
	int ret;
	test_config_t config;

	for (int one_multi = 0; one_multi <= 1; one_multi++) {
		for (int contention_level = 0; contention_level <= 2;
		     contention_level++) {
			printf("***PF %s %s***\n",
			       one_multi ? "MULTI_VMAS" : "ONE_VMA",
			       contention_level_name[contention_level]);

			config.num_total_pages = 0;
			config.num_requests_per_thread = NUM_PAGES;
			config.num_pages_per_request = 1;
			config.mmap_before_spawn = 1;
			config.trigger_fault_before_spawn = 0;
			config.multi_vma_assign_requests = one_multi;
			config.contention_level = contention_level;
			config.is_unfixed_mmap_test = 0;

			ret = entry_point(
				argc, argv, worker_thread, config);
			if (ret != 0) {
				die("entry_point failed");
				exit(EXIT_FAILURE);
			}
			printf("\n");
		}
	}
}