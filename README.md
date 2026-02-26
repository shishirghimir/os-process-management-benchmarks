# ST5068CEM — Process Management Benchmarks

Empirical benchmark suite comparing Windows 11 and Linux (Arch) process management. Developed as part of ST5068CEM Platforms and Operating Systems coursework.

## Overview

Six custom programs test four pillars of process management across both operating systems:

| Program | Language | Tests | Platform |
|---------|----------|-------|----------|
| `sync_bench.c` | C | Mutex, semaphore, spinlock latency + IPC throughput | Linux |
| `race_demo.c` | C | Race condition proof with/without mutex | Linux |
| `ipc_demo.c` | C | Producer-consumer shared memory pipeline | Linux |
| `pipe_demo.c` | C | Anonymous, named, bidirectional pipes | Linux |
| `limit_test.c` | C | Process limits, fork scaling, zombie demo | Linux |
| `win_bench.ps1` | PowerShell | Critical Section, Mutex, Semaphore latency + IPC | Windows |

## Key Results

```
┌─────────────────────────┬──────────────────┬──────────────────┬────────┐
│ Metric                  │ Windows 11       │ Linux (Arch)     │ Ratio  │
├─────────────────────────┼──────────────────┼──────────────────┼────────┤
│ Mutex latency           │ 2,304.9 ns       │ 11.3 ns          │ 204×   │
│ Kernel Mutex            │ 50,246.6 ns      │ 58.4 ns          │ 860×   │
│ Semaphore               │ 102,783.9 ns     │ 23.0 ns          │ 4,469× │
│ Pipe throughput         │ 0.2 Gbps         │ 14.8 Gbps        │ 74×    │
│ Shared memory           │ 6.0 Gbps         │ 417.5 Gbps       │ 70×    │
│ Race condition (no lock)│ —                │ 72.8% data loss  │ —      │
│ Race condition (mutex)  │ —                │ 0% data loss     │ —      │
│ Fork overhead (50 proc) │ —                │ 2.89 ms          │ —      │
│ FFmpeg encode time      │ 90 s             │ 30 s             │ 3×     │
└─────────────────────────┴──────────────────┴──────────────────┴────────┘
```

## Build & Run

### Linux (Arch/Ubuntu/Debian)

```bash
# Synchronization & IPC benchmark
gcc -O2 -o sync_bench sync_bench.c -lpthread -lrt
./sync_bench

# Race condition demonstration
gcc -O0 -o race_demo race_demo.c -lpthread
./race_demo

# Producer-consumer shared memory IPC
gcc -o ipc_demo ipc_demo.c -lrt -lpthread
./ipc_demo

# Pipe IPC (anonymous, named, bidirectional)
gcc -o pipe_demo pipe_demo.c
./pipe_demo

# Process limits, fork scaling, zombie demo
gcc -o limit_test limit_test.c
./limit_test
```

### Windows (PowerShell)

```powershell
powershell -ExecutionPolicy Bypass -File win_bench.ps1
```

## Program Details

### sync_bench.c

Measures synchronization primitive latency over 1,000,000 iterations per test:
- **pthread_mutex (futex):** Uncontended lock/unlock cycle
- **POSIX semaphore:** sem_wait/sem_post cycle
- **pthread_spinlock:** Busy-wait lock for short critical sections
- **Contended mutex:** Two threads competing for the same lock
- **Shared memory throughput:** 64 MB × 100 iterations via memcpy
- **Pipe throughput:** 64 MB single transfer

### race_demo.c

Proves why synchronization matters. Four threads perform 500,000 increments each on a shared counter:
- **Without mutex:** 543,255 of 2,000,000 survived — 72.8% data loss
- **With mutex:** 2,000,000 of 2,000,000 — zero loss, 16.6× slower
- **Buffer corruption test:** 679 corruptions without mutex, 0 with mutex

Compiled with `-O0` to prevent compiler optimization from hiding the race condition.

### ipc_demo.c

Full producer-consumer pipeline using POSIX shared memory:
- Shared memory region: 332 bytes (5-slot circular buffer)
- Frame size: 6,220,800 bytes (simulated 4K raw frame)
- Synchronization: 3 POSIX semaphores (empty, full, mutex)
- Result: 10 frames at 9.0 frames/sec, latencies 89.6–120.0 ms, 0 race conditions

### pipe_demo.c

Tests all three pipe types available on Linux:
- **Anonymous pipe:** Parent-child frame transfer (10 frames, 242.4 ms, 41.3 fps)
- **Named pipe (FIFO):** Unrelated process communication via /tmp/ffmpeg_pipe_demo
- **Throughput benchmark:** 64 MB in 36.4 ms = 14.8 Gbps (1,760 MB/s)
- **Bidirectional pipes:** Request/response pattern, 0.08 ms average round-trip

### limit_test.c

Safe process resource testing:
- Reads system limits via `getrlimit()` and `sysinfo()`
- Creates 50 child processes with measured fork() timing
- Fork overhead scaling: 1→0.17ms, 5→0.50ms, 10→0.90ms, 20→1.55ms, 50→2.89ms
- Zombie demonstration: child exits without parent calling waitpid(), confirms Z+ state
- All children cleaned up safely — no fork bomb risk

### win_bench.ps1

Windows equivalent of sync_bench, measuring over 1,000,000 iterations:
- **.NET Monitor (Critical Section):** 2,304.9 ns/operation
- **Windows Kernel Mutex:** 50,246.6 ns/operation
- **Windows Semaphore:** 102,783.9 ns/operation
- **Named Pipe throughput:** 0.2 Gbps (64 MB)
- **Memory-mapped file throughput:** 6.0 Gbps (64 MB × 200 iterations)

## Test Environment

| | Windows 11 | Linux (Arch) |
|---|---|---|
| **Kernel** | NT 10.0 | 6.x |
| **RAM** | 16 GB | 15,689 MB |
| **CPU** | 8 cores | 8 cores |
| **Scheduler** | Multi-level feedback queues | CFS (red-black tree) |
| **Process limit** | ~4,096 (built-in) | 62,173 (configured) |
| **PID max** | — | 4,194,304 |

## File Structure

```
.
├── sync_bench.c        # Synchronization primitive latency + IPC throughput
├── race_demo.c         # Race condition demonstration (with/without mutex)
├── ipc_demo.c          # Producer-consumer shared memory IPC
├── pipe_demo.c         # Anonymous, named, bidirectional pipe tests
├── limit_test.c        # Process limits, fork scaling, zombie demo
├── win_bench.ps1       # Windows synchronization & IPC benchmark
└── README.md
```

## References

- Silberschatz, A., Galvin, P. B., & Gagne, G. (2018). *Operating System Concepts* (10th ed.). Wiley.
- Love, R. (2010). *Linux Kernel Development* (3rd ed.). Addison-Wesley.
- Russinovich, M. E., Solomon, D. A., & Ionescu, A. (2012). *Windows Internals* (6th ed.). Microsoft Press.

## License

Academic use only. Developed for ST5068CEM coursework assessment.
