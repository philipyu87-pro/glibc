/*
 * Malloc/Free Multi-threaded Benchmark
 * 
 * This benchmark tests malloc/free performance under various scenarios
 * to help identify lock contention issues in multi-threaded environments.
 * 
 * Compile: gcc -O2 -pthread -o malloc_benchmark malloc_benchmark.c
 * Run: ./malloc_benchmark --threads=16 --iterations=100000
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <getopt.h>

/* Configuration parameters */
static int num_threads = 8;
static long iterations_per_thread = 100000;
static int min_alloc_size = 16;
static int max_alloc_size = 1024;
static int mixed_sizes = 1;
static int verbose = 0;

/* Statistics */
typedef struct {
    double elapsed_time;
    long allocations;
    long deallocations;
} thread_stats_t;

static thread_stats_t *thread_stats;

/* Get current time in seconds */
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

/* Worker thread function - allocate and free pattern */
static void *worker_thread_alloc_free(void *arg) {
    int thread_id = *(int *)arg;
    double start_time = get_time();
    long i;
    
    for (i = 0; i < iterations_per_thread; i++) {
        size_t size;
        
        if (mixed_sizes) {
            /* Random size between min and max */
            size = min_alloc_size + (rand() % (max_alloc_size - min_alloc_size + 1));
        } else {
            size = max_alloc_size;
        }
        
        void *ptr = malloc(size);
        if (ptr == NULL) {
            fprintf(stderr, "Thread %d: malloc failed at iteration %ld\n", 
                    thread_id, i);
            break;
        }
        
        /* Touch the memory to ensure it's actually allocated */
        memset(ptr, 0, size);
        
        free(ptr);
    }
    
    double end_time = get_time();
    thread_stats[thread_id].elapsed_time = end_time - start_time;
    thread_stats[thread_id].allocations = i;
    thread_stats[thread_id].deallocations = i;
    
    return NULL;
}

/* Worker thread function - allocate many, then free all */
static void *worker_thread_batch(void *arg) {
    int thread_id = *(int *)arg;
    double start_time = get_time();
    long i;
    void **ptrs;
    
    ptrs = malloc(sizeof(void *) * iterations_per_thread);
    if (ptrs == NULL) {
        fprintf(stderr, "Thread %d: failed to allocate pointer array\n", thread_id);
        return NULL;
    }
    
    /* Allocate phase */
    for (i = 0; i < iterations_per_thread; i++) {
        size_t size;
        
        if (mixed_sizes) {
            size = min_alloc_size + (rand() % (max_alloc_size - min_alloc_size + 1));
        } else {
            size = max_alloc_size;
        }
        
        ptrs[i] = malloc(size);
        if (ptrs[i] == NULL) {
            fprintf(stderr, "Thread %d: malloc failed at iteration %ld\n", 
                    thread_id, i);
            iterations_per_thread = i;
            break;
        }
        
        memset(ptrs[i], 0, size);
    }
    
    /* Free phase */
    for (i = 0; i < iterations_per_thread; i++) {
        free(ptrs[i]);
    }
    
    free(ptrs);
    
    double end_time = get_time();
    thread_stats[thread_id].elapsed_time = end_time - start_time;
    thread_stats[thread_id].allocations = iterations_per_thread;
    thread_stats[thread_id].deallocations = iterations_per_thread;
    
    return NULL;
}

/* Run benchmark */
static void run_benchmark(const char *test_name, 
                         void *(*thread_func)(void *)) {
    pthread_t *threads;
    int *thread_ids;
    int i;
    double total_time = 0;
    long total_allocs = 0;
    
    printf("\n=== Running: %s ===\n", test_name);
    printf("Threads: %d, Iterations per thread: %ld\n", 
           num_threads, iterations_per_thread);
    printf("Allocation size range: %d - %d bytes\n", 
           min_alloc_size, max_alloc_size);
    
    threads = malloc(sizeof(pthread_t) * num_threads);
    thread_ids = malloc(sizeof(int) * num_threads);
    thread_stats = calloc(num_threads, sizeof(thread_stats_t));
    
    if (!threads || !thread_ids || !thread_stats) {
        fprintf(stderr, "Failed to allocate thread structures\n");
        return;
    }
    
    /* Start benchmark */
    double start = get_time();
    
    for (i = 0; i < num_threads; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, thread_func, &thread_ids[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            num_threads = i;
            break;
        }
    }
    
    /* Wait for threads */
    for (i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end = get_time();
    double wall_time = end - start;
    
    /* Calculate statistics */
    for (i = 0; i < num_threads; i++) {
        total_time += thread_stats[i].elapsed_time;
        total_allocs += thread_stats[i].allocations;
        
        if (verbose) {
            printf("Thread %d: %.3f seconds, %ld allocations\n",
                   i, thread_stats[i].elapsed_time, 
                   thread_stats[i].allocations);
        }
    }
    
    /* Print results */
    printf("\nResults:\n");
    printf("  Wall time: %.3f seconds\n", wall_time);
    printf("  Total CPU time: %.3f seconds\n", total_time);
    printf("  Total allocations: %ld\n", total_allocs);
    printf("  Throughput: %.0f ops/sec (wall time)\n", 
           total_allocs / wall_time);
    printf("  Throughput: %.0f ops/sec (CPU time)\n", 
           total_allocs / total_time);
    printf("  Average latency: %.3f µs/op\n", 
           (wall_time * 1000000) / total_allocs);
    
    free(threads);
    free(thread_ids);
    free(thread_stats);
}

static void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -t, --threads NUM        Number of threads (default: 8)\n");
    printf("  -i, --iterations NUM     Iterations per thread (default: 100000)\n");
    printf("  -s, --min-size SIZE      Minimum allocation size (default: 16)\n");
    printf("  -S, --max-size SIZE      Maximum allocation size (default: 1024)\n");
    printf("  -f, --fixed-size         Use fixed size instead of random\n");
    printf("  -v, --verbose            Verbose output\n");
    printf("  -h, --help               Show this help\n");
}

int main(int argc, char *argv[]) {
    int c;
    
    static struct option long_options[] = {
        {"threads", required_argument, 0, 't'},
        {"iterations", required_argument, 0, 'i'},
        {"min-size", required_argument, 0, 's'},
        {"max-size", required_argument, 0, 'S'},
        {"fixed-size", no_argument, 0, 'f'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((c = getopt_long(argc, argv, "t:i:s:S:fvh", 
                           long_options, NULL)) != -1) {
        switch (c) {
        case 't':
            num_threads = atoi(optarg);
            break;
        case 'i':
            iterations_per_thread = atol(optarg);
            break;
        case 's':
            min_alloc_size = atoi(optarg);
            break;
        case 'S':
            max_alloc_size = atoi(optarg);
            break;
        case 'f':
            mixed_sizes = 0;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }
    
    /* Validate parameters */
    if (num_threads < 1 || num_threads > 1024) {
        fprintf(stderr, "Invalid number of threads: %d\n", num_threads);
        return 1;
    }
    
    if (iterations_per_thread < 1) {
        fprintf(stderr, "Invalid number of iterations: %ld\n", 
                iterations_per_thread);
        return 1;
    }
    
    if (min_alloc_size < 1 || max_alloc_size < min_alloc_size) {
        fprintf(stderr, "Invalid allocation size range: %d - %d\n",
                min_alloc_size, max_alloc_size);
        return 1;
    }
    
    /* Print configuration */
    printf("Malloc/Free Multi-threaded Benchmark\n");
    printf("=====================================\n");
    
    /* Check for tunables */
    const char *tunables = getenv("GLIBC_TUNABLES");
    if (tunables) {
        printf("GLIBC_TUNABLES: %s\n", tunables);
    } else {
        printf("GLIBC_TUNABLES: (not set - using defaults)\n");
    }
    
    /* Run different test scenarios */
    run_benchmark("Test 1: Allocate-Free pairs", worker_thread_alloc_free);
    run_benchmark("Test 2: Batch allocate then free", worker_thread_batch);
    
    return 0;
}
