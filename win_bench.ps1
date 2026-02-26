# ============================================================
#  Windows Synchronization & IPC Benchmark
#  ST5068CEM - Platforms and Operating Systems
#  
#  Run in PowerShell (Admin not required):
#    powershell -ExecutionPolicy Bypass -File win_bench.ps1
# ============================================================

Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host "  Windows Synchronization & IPC Benchmark" -ForegroundColor Cyan
Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host ""

# --- Test 1: Mutex (.NET Monitor lock) ---
Write-Host "[1] Synchronization Primitive Latency:" -ForegroundColor Yellow
Write-Host "    ------------------------------------------------"

$iterations = 1000000
$lockObj = New-Object Object

# Warmup
for ($i = 0; $i -lt 10000; $i++) {
    [System.Threading.Monitor]::Enter($lockObj)
    [System.Threading.Monitor]::Exit($lockObj)
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt $iterations; $i++) {
    [System.Threading.Monitor]::Enter($lockObj)
    [System.Threading.Monitor]::Exit($lockObj)
}
$sw.Stop()

$ns_per_op = ($sw.Elapsed.TotalMilliseconds * 1000000) / $iterations
Write-Host ("  .NET Monitor (CriticalSection): {0:F1} ns/operation  ({1} iterations)" -f $ns_per_op, $iterations)

# --- Test 2: Mutex class ---
$mutex = New-Object System.Threading.Mutex($false)

# Warmup
for ($i = 0; $i -lt 10000; $i++) {
    $mutex.WaitOne() | Out-Null
    $mutex.ReleaseMutex()
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt $iterations; $i++) {
    $mutex.WaitOne() | Out-Null
    $mutex.ReleaseMutex()
}
$sw.Stop()

$ns_per_op2 = ($sw.Elapsed.TotalMilliseconds * 1000000) / $iterations
Write-Host ("  Windows Mutex (kernel):         {0:F1} ns/operation  ({1} iterations)" -f $ns_per_op2, $iterations)
$mutex.Dispose()

# --- Test 3: Semaphore ---
$sem = New-Object System.Threading.Semaphore(1, 1)

# Warmup
for ($i = 0; $i -lt 10000; $i++) {
    $sem.WaitOne() | Out-Null
    $sem.Release() | Out-Null
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt $iterations; $i++) {
    $sem.WaitOne() | Out-Null
    $sem.Release() | Out-Null
}
$sw.Stop()

$ns_per_op3 = ($sw.Elapsed.TotalMilliseconds * 1000000) / $iterations
Write-Host ("  Windows Semaphore:              {0:F1} ns/operation  ({1} iterations)" -f $ns_per_op3, $iterations)
$sem.Dispose()

# --- Test 4: IPC - Named Pipe Throughput ---
Write-Host ""
Write-Host "[2] IPC Throughput:" -ForegroundColor Yellow
Write-Host "    ------------------------------------------------"

$pipeName = "\\.\pipe\bench_pipe"
$dataSize = 64 * 1024       # 64 KB per write
$totalSize = 64 * 1024 * 1024  # 64 MB total
$data = New-Object byte[] $dataSize

# Server (writer) in background
$job = Start-Job -ScriptBlock {
    param($pipeName, $dataSize, $totalSize)
    $pipe = New-Object System.IO.Pipes.NamedPipeServerStream($pipeName.Replace("\\.\pipe\",""), 
        [System.IO.Pipes.PipeDirection]::Out)
    $pipe.WaitForConnection()
    $data = New-Object byte[] $dataSize
    $written = 0
    while ($written -lt $totalSize) {
        $pipe.Write($data, 0, $dataSize)
        $written += $dataSize
    }
    $pipe.Close()
} -ArgumentList $pipeName, $dataSize, $totalSize

Start-Sleep -Milliseconds 500

# Client (reader)
$sw = [System.Diagnostics.Stopwatch]::StartNew()
try {
    $client = New-Object System.IO.Pipes.NamedPipeClientStream(".", $pipeName.Replace("\\.\pipe\",""),
        [System.IO.Pipes.PipeDirection]::In)
    $client.Connect(5000)
    $buf = New-Object byte[] $dataSize
    $totalRead = 0
    while ($totalRead -lt $totalSize) {
        $n = $client.Read($buf, 0, $dataSize)
        if ($n -le 0) { break }
        $totalRead += $n
    }
    $client.Close()
} catch {
    Write-Host "  Pipe test error: $_" -ForegroundColor Red
}
$sw.Stop()

$seconds = $sw.Elapsed.TotalSeconds
if ($seconds -gt 0) {
    $gbps = ($totalSize * 8) / $seconds / 1e9
    Write-Host ("  Named Pipe throughput:          {0:F1} Gbps  ({1} MB transferred)" -f $gbps, ($totalSize / 1MB))
}

$job | Wait-Job | Remove-Job -Force

# --- Test 5: Memory-mapped file (shared memory equivalent) ---
$mmfSize = 64 * 1024 * 1024  # 64 MB
$mmfName = "bench_mmf"

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew($mmfName, $mmfSize)
$accessor = $mmf.CreateViewAccessor()

# Write
for ($i = 0; $i -lt 100; $i++) {
    for ($j = 0; $j -lt $mmfSize; $j += 4096) {
        $accessor.Write($j, [byte]65)
    }
}

# Read
$val = [byte]0
for ($i = 0; $i -lt 100; $i++) {
    for ($j = 0; $j -lt $mmfSize; $j += 4096) {
        $accessor.Read($j, [ref]$val)
    }
}
$sw.Stop()

$totalBytes = $mmfSize * 200  # 100 writes + 100 reads
$gbps2 = ($totalBytes * 8) / $sw.Elapsed.TotalSeconds / 1e9
Write-Host ("  Memory-mapped file throughput:   {0:F1} Gbps  ({1} MB × 200 iterations)" -f $gbps2, ($mmfSize / 1MB))

$accessor.Dispose()
$mmf.Dispose()

# --- Summary ---
Write-Host ""
Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host "  Summary:" -ForegroundColor Cyan
Write-Host ("  Monitor lock (CriticalSection):  {0:F1} ns" -f $ns_per_op)
Write-Host ("  Kernel Mutex:                    {0:F1} ns" -f $ns_per_op2)
Write-Host ("  Semaphore:                       {0:F1} ns" -f $ns_per_op3)
Write-Host "  " -NoNewline
Write-Host "Compare these with Linux futex results!" -ForegroundColor Green
Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "SCREENSHOT THIS!" -ForegroundColor Yellow