#include "scale_common.h"

#define NUM_MMAPS 512 // Number mmaps per thread
#define PAGES_PER_MMAP 1

void *worker_thread(void *arg)
{
	thread_start();

	if (data->is_unfixed_mmap_test) {
		// map them one by one
		for (size_t i = 0; i < NUM_MMAPS; i++) {
			mmap(NULL, PAGE_SIZE * PAGES_PER_MMAP,
			     PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		}
	} else {
		// map them one by one
		for (size_t i = 0; i < NUM_MMAPS; i++) {
			mmap(data->base + data->offset[i],
			     PAGE_SIZE * PAGES_PER_MMAP, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
		}
	}

	thread_end(NUM_MMAPS);
}

int main(int argc, char *argv[])
{
	int ret;

	printf("***MMAP UNFIXED***\n");

	test_config_t config;
	config.num_total_pages = 0;
	config.num_requests_per_thread = NUM_MMAPS;
	config.num_pages_per_request = PAGES_PER_MMAP;
	config.mmap_before_spawn = 0;
	config.trigger_fault_before_spawn = 0;
	config.multi_vma_assign_requests = 0;
	config.contention_level = 0;
	config.is_unfixed_mmap_test = 1;

	ret = entry_point(argc, argv, worker_thread, config);
	if (ret != 0) {
		die("entry_point failed");
		exit(EXIT_FAILURE);
	}
	printf("\n");

	for (int contention_level = 0; contention_level <= 2;
	     contention_level++) {
		printf("***MMAP FIXED %s***\n",
		       contention_level_name[contention_level]);

		config.num_total_pages = 0;
		config.num_requests_per_thread = NUM_MMAPS;
		config.num_pages_per_request = PAGES_PER_MMAP;
		config.mmap_before_spawn = 0;
		config.trigger_fault_before_spawn = 0;
		config.multi_vma_assign_requests = 0;
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