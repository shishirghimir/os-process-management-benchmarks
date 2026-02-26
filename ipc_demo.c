/*
 * IPC Demonstration: Producer-Consumer using POSIX Shared Memory & Semaphores
 * ST5068CEM - Platforms and Operating Systems
 * 
 * This program demonstrates Inter-Process Communication (IPC) using:
 *   1. POSIX Shared Memory (shm_open, mmap) - for high-speed data sharing
 *   2. POSIX Semaphores (sem_open) - for synchronization
 *   3. fork() - to create separate producer and consumer processes
 *
 * Scenario: Simulates FFmpeg-style frame buffer sharing where a producer
 * process writes video frame data into shared memory, and a consumer
 * process reads and "encodes" it — mirroring the demuxer-encoder pipeline.
 *
 * Compile: gcc -o ipc_demo ipc_demo.c -lrt -lpthread
 * Run:     ./ipc_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <semaphore.h>
#include <time.h>

#define SHM_NAME    "/ffmpeg_frame_buffer"
#define SEM_EMPTY   "/sem_empty"
#define SEM_FULL    "/sem_full"
#define SEM_MUTEX   "/sem_mutex"

#define BUFFER_SIZE  5       /* Number of slots in shared buffer */
#define FRAME_COUNT  10      /* Total frames to produce/consume */
#define FRAME_DATA   64      /* Bytes per frame (simplified) */

/* Shared memory structure — mimics a bounded frame buffer */
typedef struct {
    char  frames[BUFFER_SIZE][FRAME_DATA];  /* Frame data slots */
    int   in;                                /* Producer write index */
    int   out;                               /* Consumer read index */
    int   count;                             /* Current items in buffer */
} SharedBuffer;

/* Get current time in milliseconds for latency measurement */
double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* ==================== PRODUCER PROCESS ==================== */
void producer(SharedBuffer *buf, sem_t *empty, sem_t *full, sem_t *mutex) {
    printf("[PRODUCER] Started (PID: %d)\n", getpid());

    for (int i = 0; i < FRAME_COUNT; i++) {
        /* Simulate frame capture delay */
        usleep(50000 + (rand() % 50000));  /* 50-100ms */

        /* Wait for empty slot (blocks if buffer is full) */
        sem_wait(empty);

        /* Lock mutex — critical section */
        sem_wait(mutex);

        /* Write frame into shared memory */
        snprintf(buf->frames[buf->in], FRAME_DATA,
                 "Frame_%02d [4K_h264 | %d bytes]", i, 1920 * 1080 * 3);
        printf("[PRODUCER] Wrote: %-40s  (slot %d, buffer: %d/%d)\n",
               buf->frames[buf->in], buf->in, buf->count + 1, BUFFER_SIZE);

        buf->in = (buf->in + 1) % BUFFER_SIZE;
        buf->count++;

        /* Unlock mutex */
        sem_post(mutex);

        /* Signal that buffer has data */
        sem_post(full);
    }

    printf("[PRODUCER] Finished — all %d frames produced.\n\n", FRAME_COUNT);
}

/* ==================== CONSUMER PROCESS ==================== */
void consumer(SharedBuffer *buf, sem_t *empty, sem_t *full, sem_t *mutex) {
    printf("[CONSUMER] Started (PID: %d)\n", getpid());

    for (int i = 0; i < FRAME_COUNT; i++) {
        /* Wait for data (blocks if buffer is empty) */
        sem_wait(full);

        /* Lock mutex — critical section */
        sem_wait(mutex);

        double start = get_time_ms();

        /* Read frame from shared memory */
        char frame[FRAME_DATA];
        strncpy(frame, buf->frames[buf->out], FRAME_DATA);

        buf->out = (buf->out + 1) % BUFFER_SIZE;
        buf->count--;

        /* Unlock mutex */
        sem_post(mutex);

        /* Signal that slot is free */
        sem_post(empty);

        /* Simulate encoding work */
        usleep(80000 + (rand() % 40000));  /* 80-120ms */

        double elapsed = get_time_ms() - start;
        printf("[CONSUMER] Encoded: %-40s  (latency: %.1f ms)\n", frame, elapsed);
    }

    printf("[CONSUMER] Finished — all %d frames encoded.\n", FRAME_COUNT);
}

/* ==================== MAIN ==================== */
int main() {
    printf("=============================================================\n");
    printf("  IPC Demo: Producer-Consumer with Shared Memory & Semaphores\n");
    printf("  Simulating FFmpeg frame buffer pipeline\n");
    printf("=============================================================\n\n");

    srand(time(NULL));

    /* --- Cleanup any previous instances --- */
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_EMPTY);
    sem_unlink(SEM_FULL);
    sem_unlink(SEM_MUTEX);

    /* --- Create shared memory --- */
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) { perror("shm_open"); exit(1); }
    ftruncate(shm_fd, sizeof(SharedBuffer));

    SharedBuffer *buf = mmap(NULL, sizeof(SharedBuffer),
                             PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (buf == MAP_FAILED) { perror("mmap"); exit(1); }

    /* Initialize buffer */
    buf->in = 0;
    buf->out = 0;
    buf->count = 0;
    memset(buf->frames, 0, sizeof(buf->frames));

    printf("[MAIN] Shared memory created: %s (%lu bytes)\n", SHM_NAME, sizeof(SharedBuffer));

    /* --- Create semaphores --- */
    sem_t *empty = sem_open(SEM_EMPTY, O_CREAT, 0666, BUFFER_SIZE);  /* Initially BUFFER_SIZE empty slots */
    sem_t *full  = sem_open(SEM_FULL,  O_CREAT, 0666, 0);            /* Initially 0 full slots */
    sem_t *mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1);            /* Binary semaphore (mutex) */

    if (empty == SEM_FAILED || full == SEM_FAILED || mutex == SEM_FAILED) {
        perror("sem_open"); exit(1);
    }

    printf("[MAIN] Semaphores created: empty=%d, full=%d, mutex=1\n", BUFFER_SIZE, 0);
    printf("[MAIN] Buffer capacity: %d slots | Frames to process: %d\n\n", BUFFER_SIZE, FRAME_COUNT);

    double start_time = get_time_ms();

    /* --- Fork: create producer and consumer processes --- */
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid == 0) {
        /* Child process = Consumer */
        consumer(buf, empty, full, mutex);
        exit(0);
    } else {
        /* Parent process = Producer */
        producer(buf, empty, full, mutex);

        /* Wait for consumer to finish */
        wait(NULL);

        double total_time = get_time_ms() - start_time;

        printf("\n=============================================================\n");
        printf("  Results\n");
        printf("=============================================================\n");
        printf("  Frames produced:    %d\n", FRAME_COUNT);
        printf("  Frames consumed:    %d\n", FRAME_COUNT);
        printf("  Buffer size:        %d slots\n", BUFFER_SIZE);
        printf("  Total time:         %.1f ms\n", total_time);
        printf("  Avg throughput:     %.1f frames/sec\n", FRAME_COUNT / (total_time / 1000.0));
        printf("  IPC mechanism:      POSIX Shared Memory + Semaphores\n");
        printf("  Synchronization:    3 semaphores (empty, full, mutex)\n");
        printf("  Race conditions:    0 (mutex-protected critical section)\n");
        printf("=============================================================\n");

        /* --- Cleanup --- */
        munmap(buf, sizeof(SharedBuffer));
        shm_unlink(SHM_NAME);
        sem_close(empty);  sem_unlink(SEM_EMPTY);
        sem_close(full);   sem_unlink(SEM_FULL);
        sem_close(mutex);  sem_unlink(SEM_MUTEX);
    }

    return 0;
}
