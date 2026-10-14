# PipeFlow — A Dataflow Stream Engine
## Architecture, Wiring Model, Backpressure Mechanics & Design Specification

---

## 1. Overview
**PipeFlow** is an asynchronous dataflow stream engine built in **C11** using core **Linux POSIX primitives**. It executes computation graphs (linear pipelines, fan-out, fan-in, and Directed Acyclic Graphs) where each stage runs as an independent, memory-isolated OS process communicating across anonymous kernel pipes without writing intermediate data to disk.

PipeFlow features:
- **Zero-Disk Streaming:** Direct streaming through 64 KB kernel ring buffers.
- **Natural Backpressure:** Throttling fast sources automatically via pipe-full blocking with microsecond I/O wait measurement.
- **Deterministic EOF Propagation:** Strict file descriptor hygiene in both parent and children ensuring unblocked completion.
- **Graceful Broken Pipe Recovery:** Catching `EPIPE` and ignoring default fatal `SIGPIPE` actions.
- **DAG Topological Scheduler:** Cycle detection and dependency-ordered execution using Kahn's algorithm.
- **Fault-Tolerant Supervision:** Dynamic stage restart upon failure without tearing down the pipeline.
- **Resource Discipline:** Valgrind-verified zero memory leaks and zero leaked file descriptors across start/stop cycles.

---

## 2. System Architecture & Components

```
                              [ PIPEFLOW ENGINE (Parent) ]
                                          │
                  ┌───────────────────────┴───────────────────────┐
                  ▼                                               ▼
         [ Parser & Lexer ]                            [ Process Control Table ]
     • Command Tokenization                         • Lifecycle: CREATED -> RUNNING -> EXITED
     • .pflow Config Parser                         • PID, exit status, restart count
                  │                                               │
                  └───────────────────────┬───────────────────────┘
                                          │
                                          ▼
                             [ DAG Scheduler & Executor ]
                         • Kahn's Topological Sort (Acyclic check)
                         • (N - 1) Pipe Allocations
                         • Process Group Assignment (setpgid)
                         • Descriptor Redirection (dup2)
                                          │
                                          ▼
     ┌────────────────────────────────────┴────────────────────────────────────┐
     ▼                                    ▼                                    ▼
[ Stage 0: pf_source ]            [ Stage 1: pf_transform ]            [ Stage 2: pf_sink ]
• PID: A, PGID: A                 • PID: B, PGID: A                    • PID: C, PGID: A
• STDOUT -> Pipe 0 [write]        • STDIN  <- Pipe 0 [read]            • STDIN <- Pipe 1 [read]
• Measures I/O wait (Backpressure)• STDOUT -> Pipe 1 [write]           • Throughput & Byte metrics
• EPIPE graceful handler          • Character transformation           • Throttling delay simulation
```

### Module Breakdown
- **`include/stage.h` & `src/stage.c`**: In-memory Process Table, lifecycle tracking (`CREATED`, `READY`, `RUNNING`, `EXITED`, `SIGNALED`, `FAILED`), edge definitions, and Kahn's topological sort algorithm.
- **`include/executor.h` & `src/executor.c`**: Process spawner, pipe allocation, descriptor wiring (`dup2`), parent descriptor cleanup, `waitpid()` reaping, and supervisor auto-restart.
- **`include/parser.h` & `src/parser.c`**: Lexer supporting quotes, inline pipeline strings (`cmd1 | cmd2`), and declarative `.pflow` configuration files.
- **`include/signals.h` & `src/signals.c`**: POSIX `sigaction` configuration, `SIGPIPE` ignoring, process group atomic broadcasting (`kill(-pgid, SIGTERM)`), and `SIGALRM` deadman timers.
- **`include/record.h` & `src/record.c`**: Framing protocol and line-based I/O with `EINTR` retry and `EPIPE` detection.
- **`src/repl.c`**: Interactive GNU Readline shell with command history (`run`, `load`, `status`, `stages`, `help`, `quit`).

---

## 3. Core Syscall & OS Primitives Reference

| Syscall / API | Purpose in PipeFlow | Critical Implementation Detail |
|---|---|---|
| `pipe(int fds[2])` | Creates unidirectional kernel ring buffer (64 KB). | `fds[0]` = read, `fds[1]` = write. |
| `fork()` | Spawns isolated child process address space. | Copy-on-Write (CoW) page semantics. |
| `execvp(file, argv)` | Replaces child memory image with stage binary. | Resolves path via system `$PATH`. |
| `dup2(oldfd, newfd)` | Rebinds pipe descriptors to `STDIN_FILENO` (0) and `STDOUT_FILENO` (1). | Atomic redirection; closes `newfd` if open. |
| `close(fd)` | Enforces strict descriptor hygiene. | Must close unused ends in parent and child. |
| `sigaction()` | Installs signal dispositions for `SIGPIPE`, `SIGINT`, `SIGTERM`, `SIGALRM`. | `SA_RESTART` omitted to unblock `waitpid()` on signals. |
| `setpgid(pid, pgid)` | Assigns stages to a shared Process Group ID. | Child 0 calls `setpgid(0, 0)`; others join it. |
| `kill(-pgid, sig)` | Delivers atomic termination signal to entire pipeline. | Negative PGID signals the entire process group. |
| `waitpid(pid, &st, opts)` | Reaps child processes and collects exit statuses. | Decoded using `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`. |
| `poll(fds, nfds, timeout)` | Multiplexes multiple input streams without deadlock in `pf_merge`. | Removes closed descriptors on `POLLHUP` / `EOF`. |
| `clock_gettime(CLOCK_MONOTONIC)` | High-resolution microsecond latency measurement. | Measures time blocked on `write()` for backpressure metrics. |

---

## 4. Key Mechanisms & Problem Solutions

### 4.1 End-of-Stream (EOF) Propagation & File Descriptor Hygiene
- **The Rule:** An EOF condition (`read() == 0`) is **only delivered to a reader when all write descriptors to that pipe are closed across all processes in the system**.
- **The Hazard:** If the parent process or intermediate children forget to close their copies of write descriptors, the reader will block indefinitely waiting for data that will never arrive.
- **PipeFlow's Solution:**
  1. In child: After calling `dup2()`, child explicitly closes all pipe descriptors from all edges before calling `execvp()`.
  2. In parent: Parent immediately closes both ends of all edge pipes after forking the stages.

### 4.2 Graceful Broken Pipe Recovery (`SIGPIPE` vs `EPIPE`)
- **The Hazard:** If a downstream consumer (like `pf_head`) reads $N$ records and exits early, its read descriptor closes. The next write by an upstream producer triggers a kernel `SIGPIPE` signal whose default action is killing the process abruptly (Exit Code 141 / Signal 13).
- **PipeFlow's Solution:**
  Producers configure `sigaction(SIGPIPE, &sa, NULL)` with `sa.sa_handler = SIG_IGN`. When writing to a broken pipe, the `write()` system call returns `-1` with `errno = EPIPE`. The producer logs graceful shutdown and exits with code `0`.

### 4.3 Real Backpressure Mechanics
- **The Kernel Mechanism:** Standard Linux pipes use bounded 64 KB ring buffers. When an upstream producer generates data faster than a downstream consumer can process, the 64 KB buffer becomes full.
- **The Consequence:** The Linux scheduler puts the producer's `write()` syscall into `TASK_INTERRUPTIBLE` state, blocking it until the consumer reads data and frees buffer space.
- **Measured Proof:**
  By measuring `clock_gettime(CLOCK_MONOTONIC)` before and after every `write()` call:
  - Fast Source $\rightarrow$ Fast Sink: Source I/O Blocked Time $\approx 0.0003 \text{ s}$.
  - Fast Source $\rightarrow$ Slow Sink (1ms delay): Source I/O Blocked Time $> 1.3000 \text{ s}$.

### 4.4 Fan-In Deadlock Prevention with `poll(2)`
- If a merge stage attempts a blocking `read()` on Stream A while Stream B has a full pipe buffer, Stream B's producer will block, causing a deadlock.
- `pf_merge` uses `poll(2)` across an array of `struct pollfd`. It only reads from descriptors with data ready (`POLLIN`), and detaches descriptors that trigger `POLLHUP` or `EOF`.

### 4.5 DAG Topological Scheduling & Stage Restart
- Directed edges $(u, v)$ define dependencies.
- **Kahn's Algorithm:** Calculates in-degrees for all nodes, validates graph acyclicity (reports error if cycle is detected), and produces a topological execution order.
- **Dynamic Supervisor Restart:** If a stage terminates with an abnormal exit code or signal, the supervisor (`executor.c`) intercepts it in the `waitpid()` loop and re-forks the stage up to `max_retries` without tearing down the entire pipeline.

---

## 5. Test Suite Summary

Run the automated test battery:
```bash
make test
```
Or run the dedicated backpressure benchmark:
```bash
./tests/test_backpressure.sh
```

All 12 acceptance tests verify end-to-end correctness, broken-pipe recovery, timeout deadman switches, file redirection, multithreading, fan-out/fan-in multiplexing, DAG scheduling, auto-restart, and Valgrind memory/descriptor cleanliness.
