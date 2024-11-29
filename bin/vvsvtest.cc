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

#define PAGE_SIZE 4096 // Typical page size in bytes

void *worker_thread1(void *arg)
{
    if (setaffinity(get_cpu_order(0)) < 0) {
		die("setaffinity err");
		exit(EXIT_FAILURE);
	}

    char *region = (char*)arg;
    // trigger page fault
    for(int i = 0; i < 100; i++) {
        volatile char c = region[i * PAGE_SIZE];
        c++;
        printf("%c\n",c);
    }
    // munmap(region, 64 * PAGE_SIZE);
    return NULL;
}

void *worker_thread2(void *arg)
{
    if (setaffinity(get_cpu_order(1)) < 0) {
		die("setaffinity err");
		exit(EXIT_FAILURE);
	}

    char *region = (char*)arg;
    // trigger page fault
    for(int i = 30; i < 128; i++) {
        volatile char c = region[i * PAGE_SIZE];
        c++;
        printf("%c\n",c);
    }
    // munmap(region + 64 * PAGE_SIZE, 64 * PAGE_SIZE);
    return NULL;
}

int main(int argc, char *argv[])
{
    char *region = (char*)mmap(0, PAGE_SIZE * 128, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    // trigger page fault
    for(int i = 0; i < 128; i++) {
        region[i * PAGE_SIZE] = 'a';
    }

    pthread_t thread1, thread2;
    // Create threads and trigger page faults in parallel
    if (xthread_create(&thread1, 0, worker_thread1, (void*)region) != 0) {
        die("pthread_create failed");
        exit(EXIT_FAILURE);
    }
    // Create threads and trigger page faults in parallel
    if (xthread_create(&thread2, 0, worker_thread2, (void*)region) != 0) {
        die("pthread_create failed");
        exit(EXIT_FAILURE);
    }

    xpthread_join(thread1);
    xpthread_join(thread2);

    // munmap(region, PAGE_SIZE * 128);
    // munmap(region, 64 * PAGE_SIZE);
    // munmap(region + 64 * PAGE_SIZE, 64 * PAGE_SIZE);
    return 0;
}