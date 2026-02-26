/*
 * IPC Demonstration: Producer-Consumer using Unix Pipes
 * ST5068CEM - Platforms and Operating Systems
 *
 * This program demonstrates Inter-Process Communication using:
 *   1. Anonymous Pipe (pipe()) - for parent-child communication
 *   2. Named Pipe (mkfifo) - for unrelated process communication
 *   3. Bidirectional pipe communication
 *
 * Scenario: Simulates FFmpeg pipeline where demuxer sends raw frames
 * through a pipe to the encoder process — the same model used by:
 *   ffmpeg -i input.mp4 -f rawvideo - | ffmpeg -f rawvideo -i - output.mp4
 *
 * Compile: gcc -o pipe_demo pipe_demo.c
 * Run:     ./pipe_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#define FRAME_COUNT     10
#define FRAME_SIZE      256
#define NAMED_PIPE_PATH "/tmp/ffmpeg_pipe_demo"

/* Get time in milliseconds */
double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* ==================== TEST 1: ANONYMOUS PIPE ==================== */
/* Parent (demuxer) sends frames to child (encoder) via pipe */
void test_anonymous_pipe() {
    printf("[TEST 1] Anonymous Pipe — Parent (Demuxer) → Child (Encoder)\n");
    printf("         ------------------------------------------------\n");

    int pipefd[2];  /* pipefd[0] = read end, pipefd[1] = write end */

    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    double start = get_time_ms();
    pid_t pid = fork();

    if (pid == 0) {
        /* ---- CHILD: Encoder (reads from pipe) ---- */
        close(pipefd[1]);  /* Close write end */

        char buffer[FRAME_SIZE];
        int frame_num = 0;

        while (1) {
            int bytes = read(pipefd[0], buffer, FRAME_SIZE);
            if (bytes <= 0) break;
            buffer[bytes] = '\0';

            printf("         [ENCODER] Received: %s\n", buffer);
            frame_num++;

            /* Simulate encoding work */
            usleep(30000);  /* 30ms */
        }

        close(pipefd[0]);
        printf("         [ENCODER] Done — %d frames encoded via pipe.\n", frame_num);
        exit(0);

    } else {
        /* ---- PARENT: Demuxer (writes to pipe) ---- */
        close(pipefd[0]);  /* Close read end */

        for (int i = 0; i < FRAME_COUNT; i++) {
            char frame[FRAME_SIZE];
            snprintf(frame, FRAME_SIZE, "Frame_%02d [4K raw | pipe transfer]", i);

            write(pipefd[1], frame, strlen(frame));
            printf("         [DEMUXER] Sent:     %s\n", frame);

            /* Simulate capture delay */
            usleep(20000);  /* 20ms */
        }

        close(pipefd[1]);  /* Close write end — signals EOF to child */
        wait(NULL);

        double elapsed = get_time_ms() - start;
        printf("         Total time: %.1f ms | Throughput: %.1f frames/sec\n", 
               elapsed, FRAME_COUNT / (elapsed / 1000.0));
    }
}

/* ==================== TEST 2: NAMED PIPE (FIFO) ==================== */
/* Two unrelated processes communicate via filesystem pipe */
void test_named_pipe() {
    printf("\n[TEST 2] Named Pipe (FIFO) — Unrelated Process Communication\n");
    printf("         ------------------------------------------------\n");

    /* Remove old pipe if exists, create new one */
    unlink(NAMED_PIPE_PATH);
    if (mkfifo(NAMED_PIPE_PATH, 0666) == -1) {
        perror("mkfifo");
        return;
    }

    printf("         Created FIFO: %s\n", NAMED_PIPE_PATH);

    double start = get_time_ms();
    pid_t pid = fork();

    if (pid == 0) {
        /* ---- CHILD: Reader process ---- */
        int fd = open(NAMED_PIPE_PATH, O_RDONLY);
        char buffer[FRAME_SIZE];
        int count = 0;

        while (1) {
            int bytes = read(fd, buffer, FRAME_SIZE - 1);
            if (bytes <= 0) break;
            buffer[bytes] = '\0';
            printf("         [READER PID:%d] Got: %s\n", getpid(), buffer);
            count++;
        }

        close(fd);
        printf("         [READER] Done — %d messages received via FIFO.\n", count);
        exit(0);

    } else {
        /* ---- PARENT: Writer process ---- */
        /* Small delay to let reader open the pipe */
        usleep(50000);

        int fd = open(NAMED_PIPE_PATH, O_WRONLY);
        printf("         [WRITER PID:%d] Connected to FIFO\n", getpid());

        for (int i = 0; i < 5; i++) {
            char msg[FRAME_SIZE];
            snprintf(msg, FRAME_SIZE, "Message_%d [via named pipe /tmp/ffmpeg_pipe_demo]", i);
            write(fd, msg, strlen(msg));
            printf("         [WRITER] Sent: %s\n", msg);
            usleep(20000);
        }

        close(fd);
        wait(NULL);

        double elapsed = get_time_ms() - start;
        printf("         Total time: %.1f ms\n", elapsed);
    }

    unlink(NAMED_PIPE_PATH);  /* Cleanup */
}

/* ==================== TEST 3: PIPE THROUGHPUT BENCHMARK ==================== */
void test_pipe_throughput() {
    printf("\n[TEST 3] Pipe Throughput Benchmark\n");
    printf("         ------------------------------------------------\n");

    int pipefd[2];
    pipe(pipefd);

    int total_mb = 64;
    int chunk_size = 64 * 1024;  /* 64 KB per write */
    long total_bytes = (long)total_mb * 1024 * 1024;

    pid_t pid = fork();

    if (pid == 0) {
        /* Child: Reader */
        close(pipefd[1]);
        char *buf = malloc(chunk_size);
        long total_read = 0;

        while (total_read < total_bytes) {
            int n = read(pipefd[0], buf, chunk_size);
            if (n <= 0) break;
            total_read += n;
        }

        free(buf);
        close(pipefd[0]);
        exit(0);

    } else {
        /* Parent: Writer */
        close(pipefd[0]);
        char *buf = malloc(chunk_size);
        memset(buf, 'X', chunk_size);

        double start = get_time_ms();
        long total_written = 0;

        while (total_written < total_bytes) {
            int n = write(pipefd[1], buf, chunk_size);
            if (n <= 0) break;
            total_written += n;
        }

        close(pipefd[1]);
        wait(NULL);

        double elapsed = get_time_ms() - start;
        double seconds = elapsed / 1000.0;
        double gbps = (total_bytes * 8.0) / seconds / 1e9;
        double mbps = (total_bytes) / seconds / (1024 * 1024);

        printf("         Data transferred: %d MB\n", total_mb);
        printf("         Time:             %.1f ms\n", elapsed);
        printf("         Throughput:        %.1f Gbps (%.0f MB/s)\n", gbps, mbps);

        free(buf);
    }
}

/* ==================== TEST 4: BIDIRECTIONAL PIPE ==================== */
void test_bidirectional() {
    printf("\n[TEST 4] Bidirectional Pipe — Request/Response Pattern\n");
    printf("         ------------------------------------------------\n");
    printf("         (Simulates encoder reporting progress back to controller)\n\n");

    int pipe_to_child[2];   /* Parent → Child */
    int pipe_to_parent[2];  /* Child → Parent */

    pipe(pipe_to_child);
    pipe(pipe_to_parent);

    pid_t pid = fork();

    if (pid == 0) {
        /* ---- CHILD: Encoder ---- */
        close(pipe_to_child[1]);   /* Close write end of input */
        close(pipe_to_parent[0]);  /* Close read end of output */

        char request[FRAME_SIZE], response[FRAME_SIZE];

        while (1) {
            int n = read(pipe_to_child[0], request, FRAME_SIZE - 1);
            if (n <= 0) break;
            request[n] = '\0';

            /* Process and send response */
            snprintf(response, FRAME_SIZE, "DONE: %s [encoded OK]", request);
            write(pipe_to_parent[1], response, strlen(response));
        }

        close(pipe_to_child[0]);
        close(pipe_to_parent[1]);
        exit(0);

    } else {
        /* ---- PARENT: Controller ---- */
        close(pipe_to_child[0]);   /* Close read end of output */
        close(pipe_to_parent[1]);  /* Close write end of input */

        double total_latency = 0;

        for (int i = 0; i < 5; i++) {
            char request[FRAME_SIZE], response[FRAME_SIZE];
            snprintf(request, FRAME_SIZE, "Encode_Frame_%02d", i);

            double start = get_time_ms();

            /* Send request */
            write(pipe_to_child[1], request, strlen(request));

            /* Wait for response */
            int n = read(pipe_to_parent[0], response, FRAME_SIZE - 1);
            response[n] = '\0';

            double latency = get_time_ms() - start;
            total_latency += latency;

            printf("         [REQUEST]  → %s\n", request);
            printf("         [RESPONSE] ← %s  (%.2f ms)\n\n", response, latency);
        }

        close(pipe_to_child[1]);
        close(pipe_to_parent[0]);
        wait(NULL);

        printf("         Avg round-trip latency: %.2f ms\n", total_latency / 5);
    }
}

/* ==================== MAIN ==================== */
int main() {
    printf("=============================================================\n");
    printf("  IPC Demonstration: Unix Pipes (Anonymous, Named, Bidirectional)\n");
    printf("  Simulating FFmpeg demuxer → encoder pipeline\n");
    printf("=============================================================\n\n");

    test_anonymous_pipe();
    test_named_pipe();
    test_pipe_throughput();
    test_bidirectional();

    printf("\n=============================================================\n");
    printf("  Summary:\n");
    printf("  - Anonymous pipes: fast parent-child IPC (unidirectional)\n");
    printf("  - Named pipes (FIFO): filesystem-based IPC for any processes\n");
    printf("  - Bidirectional: request/response using two pipe pairs\n");
    printf("  - Pipes are simpler than shared memory but lower throughput\n");
    printf("  - FFmpeg uses: ffmpeg ... -f rawvideo - | encoder -i pipe:0\n");
    printf("=============================================================\n");

    return 0;
}
