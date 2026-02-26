/*
 * Process Resource Limit Test (SAFE version)
 * ST5068CEM - Platforms and Operating Systems
 *
 * This program safely demonstrates:
 *   1. Current system process limits (ulimit)
 *   2. Controlled fork to measure process creation overhead
 *   3. Why resource limits matter for security (fork bomb prevention)
 *
 * This is COMPLETELY SAFE — it creates a controlled number of
 * child processes (default 50) and cleans them all up.
 *
 * Compile: gcc -o limit_test limit_test.c
 * Run:     ./limit_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define MAX_SAFE_FORKS 50  /* Safe limit — won't harm system */

/* Get time in milliseconds */
double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* ==================== TEST 1: Show System Limits ==================== */
void show_system_limits() {
    printf("[TEST 1] Current System Resource Limits:\n");
    printf("         ------------------------------------------------\n");

    /* Max processes per user */
    struct rlimit rlim;

    if (getrlimit(RLIMIT_NPROC, &rlim) == 0) {
        printf("         Max processes (soft): ");
        if (rlim.rlim_cur == RLIM_INFINITY)
            printf("UNLIMITED ⚠️  (no fork bomb protection!)\n");
        else
            printf("%lu\n", (unsigned long)rlim.rlim_cur);

        printf("         Max processes (hard): ");
        if (rlim.rlim_max == RLIM_INFINITY)
            printf("UNLIMITED ⚠️\n");
        else
            printf("%lu\n", (unsigned long)rlim.rlim_max);
    }

    /* Max open files */
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
        printf("         Max open files:       %lu (soft) / %lu (hard)\n",
               (unsigned long)rlim.rlim_cur, (unsigned long)rlim.rlim_max);
    }

    /* Max memory */
    if (getrlimit(RLIMIT_AS, &rlim) == 0) {
        printf("         Max address space:    ");
        if (rlim.rlim_cur == RLIM_INFINITY)
            printf("UNLIMITED\n");
        else
            printf("%lu MB\n", (unsigned long)(rlim.rlim_cur / (1024 * 1024)));
    }

    /* CPU time limit */
    if (getrlimit(RLIMIT_CPU, &rlim) == 0) {
        printf("         Max CPU time:         ");
        if (rlim.rlim_cur == RLIM_INFINITY)
            printf("UNLIMITED\n");
        else
            printf("%lu seconds\n", (unsigned long)rlim.rlim_cur);
    }

    /* System info */
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        printf("         Current processes:    %d running\n", si.procs);
        printf("         Total RAM:            %lu MB\n",
               (unsigned long)(si.totalram * si.mem_unit / (1024 * 1024)));
        printf("         Free RAM:             %lu MB\n",
               (unsigned long)(si.freeram * si.mem_unit / (1024 * 1024)));
        printf("         Uptime:               %ld hours %ld min\n",
               si.uptime / 3600, (si.uptime % 3600) / 60);
    }

    /* PID max */
    FILE *f = fopen("/proc/sys/kernel/pid_max", "r");
    if (f) {
        int pid_max;
        fscanf(f, "%d", &pid_max);
        printf("         Kernel PID max:       %d\n", pid_max);
        fclose(f);
    }

    /* Check if limits are configured in limits.conf */
    printf("\n         Security Assessment:\n");

    struct rlimit nproc;
    getrlimit(RLIMIT_NPROC, &nproc);

    if (nproc.rlim_cur == RLIM_INFINITY || nproc.rlim_cur > 100000) {
        printf("         ⚠️  WARNING: No process limit configured!\n");
        printf("            A fork bomb (:(){ :|:& };:) could crash this system.\n");
        printf("            FIX: Add to /etc/security/limits.conf:\n");
        printf("              * hard nproc 4096\n");
        printf("              * hard cpu 1440\n");
    } else {
        printf("         ✅ Process limit is set to %lu — fork bomb protected.\n",
               (unsigned long)nproc.rlim_cur);
    }
}

/* ==================== TEST 2: Controlled Fork Test ==================== */
void test_controlled_fork() {
    printf("\n[TEST 2] Controlled Process Creation (%d children):\n", MAX_SAFE_FORKS);
    printf("         ------------------------------------------------\n");

    pid_t pids[MAX_SAFE_FORKS];
    double times[MAX_SAFE_FORKS];
    int created = 0;

    double start = get_time_ms();

    for (int i = 0; i < MAX_SAFE_FORKS; i++) {
        double fork_start = get_time_ms();

        pid_t pid = fork();

        if (pid < 0) {
            printf("         Fork failed at child #%d: %s\n", i, strerror(errno));
            break;
        } else if (pid == 0) {
            /* Child: just exist briefly */
            usleep(100000);  /* 100ms */
            exit(0);
        } else {
            /* Parent */
            pids[i] = pid;
            times[i] = get_time_ms() - fork_start;
            created++;
        }
    }

    double fork_total = get_time_ms() - start;
    printf("         Created %d child processes in %.1f ms\n", created, fork_total);
    printf("         Avg fork() time: %.3f ms per process\n", fork_total / created);

    /* Show first 5 and last 5 PIDs */
    printf("         PIDs: ");
    for (int i = 0; i < 5 && i < created; i++)
        printf("%d ", pids[i]);
    if (created > 10) printf("... ");
    for (int i = created - 5; i < created; i++)
        if (i >= 5) printf("%d ", pids[i]);
    printf("\n");

    /* Measure memory overhead */
    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "VmRSS:", 6) == 0) {
                printf("         Parent memory (RSS): %s", line + 6);
                break;
            }
        }
        fclose(f);
    }

    /* Wait for all children and measure cleanup time */
    double cleanup_start = get_time_ms();
    for (int i = 0; i < created; i++) {
        waitpid(pids[i], NULL, 0);
    }
    double cleanup_time = get_time_ms() - cleanup_start;

    printf("         Cleanup time: %.1f ms (waited for all children)\n", cleanup_time);
    printf("         All %d children terminated safely.\n", created);
}

/* ==================== TEST 3: Process Creation Overhead Scaling ==================== */
void test_fork_scaling() {
    printf("\n[TEST 3] Fork Overhead Scaling (how creation time grows):\n");
    printf("         ------------------------------------------------\n");
    printf("         Children | Time (ms) | Avg per fork | Status\n");
    printf("         ---------|-----------|-------------|--------\n");

    int test_sizes[] = {1, 5, 10, 20, 50};
    int num_tests = 5;

    for (int t = 0; t < num_tests; t++) {
        int count = test_sizes[t];
        pid_t pids[50];
        int created = 0;

        double start = get_time_ms();

        for (int i = 0; i < count; i++) {
            pid_t pid = fork();
            if (pid < 0) break;
            if (pid == 0) { exit(0); }  /* Child exits immediately */
            pids[created++] = pid;
        }

        double elapsed = get_time_ms() - start;

        /* Cleanup */
        for (int i = 0; i < created; i++)
            waitpid(pids[i], NULL, 0);

        printf("         %8d | %9.2f | %11.3f | %s\n",
               count, elapsed, elapsed / count,
               created == count ? "OK" : "FAILED");
    }
}

/* ==================== TEST 4: Zombie Process Demo ==================== */
void test_zombie() {
    printf("\n[TEST 4] Zombie Process Demonstration:\n");
    printf("         ------------------------------------------------\n");
    printf("         (Shows what happens when parent doesn't call wait())\n\n");

    pid_t pid = fork();

    if (pid == 0) {
        /* Child exits immediately — becomes zombie until parent waits */
        printf("         [CHILD PID:%d] Exiting... (will become zombie)\n", getpid());
        exit(0);
    } else {
        /* Parent sleeps without calling wait() */
        usleep(100000);  /* Let child exit first */

        /* Check process state */
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "ps -o pid,stat,comm -p %d 2>/dev/null | tail -1", pid);

        printf("         [PARENT] Child PID %d state:\n         ", pid);
        fflush(stdout);
        system(cmd);

        printf("\n         'Z+' means ZOMBIE — child exited but parent hasn't collected it.\n");
        printf("         Zombie processes waste PID table entries (security risk).\n");
        printf("         Uncollected zombies accumulate and can exhaust PID space.\n\n");

        /* Now properly collect it */
        waitpid(pid, NULL, 0);
        printf("         [PARENT] Called waitpid() — zombie cleaned up.\n");

        /* Verify it's gone */
        printf("         [PARENT] After wait(), child PID %d: ", pid);
        fflush(stdout);
        system(cmd);
        printf("         (empty = process fully removed from system)\n");
    }
}

/* ==================== MAIN ==================== */
int main() {
    printf("=============================================================\n");
    printf("  Process Resource Limit & Security Test\n");
    printf("  SAFE version — creates max %d processes, all cleaned up\n", MAX_SAFE_FORKS);
    printf("=============================================================\n\n");

    show_system_limits();
    test_controlled_fork();
    test_fork_scaling();
    test_zombie();

    printf("\n=============================================================\n");
    printf("  Key Findings:\n");
    printf("  - Linux default: often NO process limit (security risk)\n");
    printf("  - Windows default: ~4096 process limit (built-in protection)\n");
    printf("  - Fork bomb protection requires manual configuration on Linux\n");
    printf("  - Zombie processes waste PID table entries\n");
    printf("  - Always call wait()/waitpid() to prevent zombie accumulation\n");
    printf("  - Recommended: Add 'nproc' limit to /etc/security/limits.conf\n");
    printf("=============================================================\n");

    return 0;
}
