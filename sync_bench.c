/*
 * Synchronization Primitive Latency Benchmark
 * ST5068CEM - Platforms and Operating Systems
 *
 * Tests the actual lock/unlock latency of:
 *   1. pthread_mutex (uses futex internally on Linux)
 *   2. POSIX semaphore
 *   3. Spinlock
 *
 * Compile: gcc -O2 -o sync_bench sync_bench.c -lpthread -lrt
 * Run:     ./sync_bench
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <string.h>

#define ITERATIONS 1000000  /* 1 million lock/unlock cycles */

/* Get time in nanoseconds */
static inline long long get_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* ==================== TEST 1: Mutex (futex) ==================== */
void bench_mutex() {
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    long long start, end;

    /* Warmup */
    for (int i = 0; i < 10000; i++) {
        pthread_mutex_lock(&mtx);
        pthread_mutex_unlock(&mtx);
    }

    start = get_ns();
    for (int i = 0; i < ITERATIONS; i++) {
        pthread_mutex_lock(&mtx);
        pthread_mutex_unlock(&mtx);
    }
    end = get_ns();

    double avg_ns = (double)(end - start) / ITERATIONS;
    printf("  pthread_mutex (futex):    %8.1f ns/operation  (%d iterations)\n", avg_ns, ITERATIONS);

    pthread_mutex_destroy(&mtx);
}

/* ==================== TEST 2: Semaphore ==================== */
void bench_semaphore() {
    sem_t sem;
    sem_init(&sem, 0, 1);
    long long start, end;

    /* Warmup */
    for (int i = 0; i < 10000; i++) {
        sem_wait(&sem);
        sem_post(&sem);
    }

    start = get_ns();
    for (int i = 0; i < ITERATIONS; i++) {
        sem_wait(&sem);
        sem_post(&sem);
    }
    end = get_ns();

    double avg_ns = (double)(end - start) / ITERATIONS;
    printf("  POSIX semaphore:          %8.1f ns/operation  (%d iterations)\n", avg_ns, ITERATIONS);

    sem_destroy(&sem);
}

/* ==================== TEST 3: Spinlock ==================== */
void bench_spinlock() {
    pthread_spinlock_t spin;
    pthread_spin_init(&spin, PTHREAD_PROCESS_PRIVATE);
    long long start, end;

    /* Warmup */
    for (int i = 0; i < 10000; i++) {
        pthread_spin_lock(&spin);
        pthread_spin_unlock(&spin);
    }

    start = get_ns();
    for (int i = 0; i < ITERATIONS; i++) {
        pthread_spin_lock(&spin);
        pthread_spin_unlock(&spin);
    }
    end = get_ns();

    double avg_ns = (double)(end - start) / ITERATIONS;
    printf("  pthread_spinlock:         %8.1f ns/operation  (%d iterations)\n", avg_ns, ITERATIONS);

    pthread_spin_destroy(&spin);
}

/* ==================== TEST 4: Contended Mutex (2 threads) ==================== */
pthread_mutex_t contended_mtx = PTHREAD_MUTEX_INITIALIZER;
volatile int contended_counter = 0;
#define CONTENDED_ITERS 500000

void *contended_worker(void *arg) {
    for (int i = 0; i < CONTENDED_ITERS; i++) {
        pthread_mutex_lock(&contended_mtx);
        contended_counter++;
        pthread_mutex_unlock(&contended_mtx);
    }
    return NULL;
}

void bench_contended_mutex() {
    pthread_t t1, t2;
    long long start, end;

    contended_counter = 0;

    start = get_ns();
    pthread_create(&t1, NULL, contended_worker, NULL);
    pthread_create(&t2, NULL, contended_worker, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    end = get_ns();

    double avg_ns = (double)(end - start) / (CONTENDED_ITERS * 2);
    printf("  Contended mutex (2 threads): %5.1f ns/operation  (%d total ops)\n",
           avg_ns, CONTENDED_ITERS * 2);
    printf("    Counter value: %d (expected: %d) — %s\n",
           contended_counter, CONTENDED_ITERS * 2,
           contended_counter == CONTENDED_ITERS * 2 ? "NO RACE CONDITION" : "RACE DETECTED!");
}

/* ==================== TEST 5: IPC Shared Memory Throughput ==================== */
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#define SHM_BENCH_NAME "/bench_shm"
#define SHM_BENCH_SIZE (64 * 1024 * 1024)  /* 64 MB */
#define SHM_ITERATIONS 100

void bench_shared_memory() {
    /* Create shared memory */
    shm_unlink(SHM_BENCH_NAME);
    int fd = shm_open(SHM_BENCH_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(fd, SHM_BENCH_SIZE);
    char *shm = mmap(NULL, SHM_BENCH_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    /* Fill with data */
    memset(shm, 'A', SHM_BENCH_SIZE);

    long long start = get_ns();

    pid_t pid = fork();
    if (pid == 0) {
        /* Child: read from shared memory */
        volatile char sum = 0;
        for (int iter = 0; iter < SHM_ITERATIONS; iter++) {
            for (int i = 0; i < SHM_BENCH_SIZE; i += 4096) {
                sum += shm[i];
            }
        }
        exit(0);
    } else {
        /* Parent: write to shared memory */
        for (int iter = 0; iter < SHM_ITERATIONS; iter++) {
            memset(shm, 'B' + (iter % 26), SHM_BENCH_SIZE);
        }
        wait(NULL);
    }

    long long end = get_ns();

    double total_bytes = (double)SHM_BENCH_SIZE * SHM_ITERATIONS * 2;  /* read + write */
    double seconds = (end - start) / 1000000000.0;
    double gbps = (total_bytes * 8) / seconds / 1e9;

    printf("  Shared memory throughput: %8.1f Gbps  (%d MB × %d iterations)\n",
           gbps, SHM_BENCH_SIZE / (1024*1024), SHM_ITERATIONS);

    munmap(shm, SHM_BENCH_SIZE);
    shm_unlink(SHM_BENCH_NAME);
}

/* ==================== TEST 6: Pipe Throughput ==================== */
#define PIPE_BUF_SIZE (64 * 1024)  /* 64 KB chunks */
#define PIPE_TOTAL    (64 * 1024 * 1024)  /* 64 MB total */

void bench_pipe() {
    int pipefd[2];
    pipe(pipefd);

    char *buf = malloc(PIPE_BUF_SIZE);
    memset(buf, 'X', PIPE_BUF_SIZE);

    long long start = get_ns();

    pid_t pid = fork();
    if (pid == 0) {
        /* Child: read from pipe */
        close(pipefd[1]);
        char *rbuf = malloc(PIPE_BUF_SIZE);
        int total = 0;
        while (total < PIPE_TOTAL) {
            int n = read(pipefd[0], rbuf, PIPE_BUF_SIZE);
            if (n <= 0) break;
            total += n;
        }
        free(rbuf);
        close(pipefd[0]);
        exit(0);
    } else {
        /* Parent: write to pipe */
        close(pipefd[0]);
        int total = 0;
        while (total < PIPE_TOTAL) {
            int n = write(pipefd[1], buf, PIPE_BUF_SIZE);
            if (n <= 0) break;
            total += n;
        }
        close(pipefd[1]);
        wait(NULL);
    }

    long long end = get_ns();

    double seconds = (end - start) / 1000000000.0;
    double gbps = ((double)PIPE_TOTAL * 8) / seconds / 1e9;

    printf("  Pipe throughput:         %8.1f Gbps  (%d MB transferred)\n",
           gbps, PIPE_TOTAL / (1024*1024));

    free(buf);
}

/* ==================== MAIN ==================== */
int main() {
    printf("=============================================================\n");
    printf("  Synchronization & IPC Benchmark — Linux\n");
    printf("  %d iterations per test (uncontended)\n", ITERATIONS);
    printf("=============================================================\n\n");

    printf("[1] Synchronization Primitive Latency (uncontended):\n");
    printf("    ------------------------------------------------\n");
    bench_mutex();
    bench_semaphore();
    bench_spinlock();

    printf("\n[2] Contended Mutex (race condition test):\n");
    printf("    ------------------------------------------------\n");
    bench_contended_mutex();

    printf("\n[3] IPC Throughput:\n");
    printf("    ------------------------------------------------\n");
    bench_shared_memory();
    bench_pipe();

    printf("\n=============================================================\n");
    printf("  Notes:\n");
    printf("  - pthread_mutex uses futex internally on Linux\n");
    printf("  - Uncontended = single thread, no competition for lock\n");
    printf("  - Contended = 2 threads fighting for same lock\n");
    printf("  - Lower latency = better synchronization performance\n");
    printf("  - Higher throughput = better IPC performance\n");
    printf("=============================================================\n");

    return 0;
}
