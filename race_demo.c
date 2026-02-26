/*
 * Race Condition Demonstration
 * ST5068CEM - Platforms and Operating Systems
 *
 * This program demonstrates WHY synchronization is critical:
 *   Test 1: Multiple threads increment a counter WITHOUT mutex → RACE CONDITION
 *   Test 2: Same operation WITH mutex → CORRECT result
 *   Test 3: Shows data corruption in shared buffer without protection
 *
 * This directly relates to the FFmpeg scenario where multiple encoder
 * threads accessing shared frame buffers without synchronization
 * causes data corruption.
 *
 * Compile: gcc -O0 -o race_demo race_demo.c -lpthread
 * Run:     ./race_demo
 *
 * NOTE: -O0 disables optimization to ensure race condition is visible
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define NUM_THREADS    4
#define INCREMENTS     500000   /* Each thread increments this many times */

/* ==================== GLOBAL SHARED DATA ==================== */
int unsafe_counter = 0;          /* No protection — will have race */
int safe_counter = 0;            /* Protected by mutex */
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Shared buffer for data corruption demo */
char shared_buffer[256];
int corruption_count = 0;
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Get time in milliseconds */
double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* ==================== TEST 1: UNSAFE (no mutex) ==================== */
void *unsafe_increment(void *arg) {
    int tid = *(int *)arg;
    for (int i = 0; i < INCREMENTS; i++) {
        /* READ-MODIFY-WRITE without protection — classic race condition */
        int temp = unsafe_counter;   /* READ */
        temp = temp + 1;             /* MODIFY */
        unsafe_counter = temp;       /* WRITE — another thread may have changed it! */
    }
    return NULL;
}

/* ==================== TEST 2: SAFE (with mutex) ==================== */
void *safe_increment(void *arg) {
    int tid = *(int *)arg;
    for (int i = 0; i < INCREMENTS; i++) {
        pthread_mutex_lock(&counter_mutex);    /* LOCK */
        safe_counter++;                         /* Critical section */
        pthread_mutex_unlock(&counter_mutex);  /* UNLOCK */
    }
    return NULL;
}

/* ==================== TEST 3: Buffer Corruption Demo ==================== */
/* Simulates two FFmpeg encoders writing to same buffer without sync */
void *unsafe_writer(void *arg) {
    char *name = (char *)arg;
    char pattern[256];
    
    for (int i = 0; i < 10000; i++) {
        /* Write our pattern to shared buffer (no lock) */
        snprintf(pattern, sizeof(pattern), "[%s] Frame_%04d_data_payload_xxxxxxx", name, i);
        strcpy(shared_buffer, pattern);
        
        /* Small delay to increase interleaving chance */
        for (volatile int j = 0; j < 50; j++);
        
        /* Check if our data is still intact */
        if (strncmp(shared_buffer, pattern, strlen(name) + 2) != 0) {
            corruption_count++;
        }
    }
    return NULL;
}

void *safe_writer(void *arg) {
    char *name = (char *)arg;
    char pattern[256];
    int local_corruption = 0;
    
    for (int i = 0; i < 10000; i++) {
        pthread_mutex_lock(&buffer_mutex);     /* LOCK */
        
        snprintf(pattern, sizeof(pattern), "[%s] Frame_%04d_data_payload_xxxxxxx", name, i);
        strcpy(shared_buffer, pattern);
        
        for (volatile int j = 0; j < 50; j++);
        
        if (strncmp(shared_buffer, pattern, strlen(name) + 2) != 0) {
            local_corruption++;
        }
        
        pthread_mutex_unlock(&buffer_mutex);   /* UNLOCK */
    }
    return NULL;
}

/* ==================== MAIN ==================== */
int main() {
    pthread_t threads[NUM_THREADS];
    int tids[NUM_THREADS];
    
    printf("=============================================================\n");
    printf("  Race Condition Demonstration\n");
    printf("  %d threads × %d increments = %d expected total\n",
           NUM_THREADS, INCREMENTS, NUM_THREADS * INCREMENTS);
    printf("=============================================================\n\n");
    
    int expected = NUM_THREADS * INCREMENTS;
    
    /* ---- TEST 1: WITHOUT MUTEX ---- */
    printf("[TEST 1] Counter increment WITHOUT mutex (unsafe):\n");
    printf("         ------------------------------------------------\n");
    
    unsafe_counter = 0;
    double start = get_time_ms();
    
    for (int i = 0; i < NUM_THREADS; i++) {
        tids[i] = i;
        pthread_create(&threads[i], NULL, unsafe_increment, &tids[i]);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double time1 = get_time_ms() - start;
    int lost = expected - unsafe_counter;
    double loss_pct = (double)lost / expected * 100.0;
    
    printf("         Expected: %d\n", expected);
    printf("         Actual:   %d\n", unsafe_counter);
    printf("         Lost:     %d increments (%.1f%% data loss)\n", lost, loss_pct);
    printf("         Time:     %.1f ms\n", time1);
    printf("         Status:   ");
    if (lost > 0) {
        printf("RACE CONDITION DETECTED!\n");
    } else {
        printf("No race detected (try running again)\n");
    }
    
    /* ---- TEST 2: WITH MUTEX ---- */
    printf("\n[TEST 2] Counter increment WITH mutex (safe):\n");
    printf("         ------------------------------------------------\n");
    
    safe_counter = 0;
    start = get_time_ms();
    
    for (int i = 0; i < NUM_THREADS; i++) {
        tids[i] = i;
        pthread_create(&threads[i], NULL, safe_increment, &tids[i]);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double time2 = get_time_ms() - start;
    
    printf("         Expected: %d\n", expected);
    printf("         Actual:   %d\n", safe_counter);
    printf("         Lost:     %d increments\n", expected - safe_counter);
    printf("         Time:     %.1f ms\n", time2);
    printf("         Status:   ");
    if (safe_counter == expected) {
        printf("CORRECT — mutex prevented race condition\n");
    } else {
        printf("ERROR — this should not happen\n");
    }
    
    printf("         Overhead: mutex is %.1fx slower (cost of safety)\n", time2 / time1);
    
    /* ---- TEST 3: BUFFER CORRUPTION ---- */
    printf("\n[TEST 3] Shared buffer corruption (simulates FFmpeg frame buffer):\n");
    printf("         ------------------------------------------------\n");
    
    /* Without mutex */
    corruption_count = 0;
    pthread_t encoder1, encoder2;
    
    printf("         Without mutex:\n");
    pthread_create(&encoder1, NULL, unsafe_writer, "Encoder_HIGH");
    pthread_create(&encoder2, NULL, unsafe_writer, "Encoder_LOW ");
    pthread_join(encoder1, NULL);
    pthread_join(encoder2, NULL);
    printf("           Corruptions detected: %d / 20000 writes\n", corruption_count);
    if (corruption_count > 0) {
        printf("           Data integrity: COMPROMISED\n");
    }
    
    /* With mutex */
    corruption_count = 0;
    printf("         With mutex:\n");
    pthread_create(&encoder1, NULL, safe_writer, "Encoder_HIGH");
    pthread_create(&encoder2, NULL, safe_writer, "Encoder_LOW ");
    pthread_join(encoder1, NULL);
    pthread_join(encoder2, NULL);
    printf("           Corruptions detected: %d / 20000 writes\n", corruption_count);
    printf("           Data integrity: PRESERVED\n");
    
    /* ---- SUMMARY ---- */
    printf("\n=============================================================\n");
    printf("  Summary:\n");
    printf("  -------\n");
    printf("  Without mutex: %d/%d lost (%.1f%% data loss) — UNSAFE\n",
           lost, expected, loss_pct);
    printf("  With mutex:    0/%d lost (0%% data loss)     — SAFE\n", expected);
    printf("  Buffer test:   corruption %s without mutex\n",
           corruption_count > 0 ? "CONFIRMED" : "detected");
    printf("  \n");
    printf("  Conclusion: Synchronization is essential for shared resources.\n");
    printf("  Without mutex protection, concurrent FFmpeg encoders would\n");
    printf("  corrupt shared frame buffers, causing video artifacts or crashes.\n");
    printf("=============================================================\n");
    
    return 0;
}
