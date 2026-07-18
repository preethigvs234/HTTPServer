# High-Performance Multi-threaded HTTP Server

A production-quality **HTTP/1.1 static-file server** written in **C++17**, built from scratch using POSIX sockets and a fixed-size thread pool.  Designed as a portfolio project demonstrating Operating Systems, Computer Networks, Multithreading, and Modern C++ skills.

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/Build-CMake-green.svg)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows-orange.svg)](https://windows.org/)

---

## Features

| Category | Detail |
|---|---|
| **Protocol** | HTTP/1.1 GET and HEAD; status codes 200, 400, 403, 404, 405, 500 |
| **Sockets** | Raw POSIX `socket()` → `bind()` → `listen()` → `accept()` with `SO_REUSEADDR` |
| **Concurrency** | Fixed thread pool – no thread-per-request overhead |
| **Synchronisation** | `std::mutex`, `std::condition_variable`, RAII locks, `std::atomic` |
| **Security** | Double directory-traversal guard, 8 KiB header cap, 1 MiB body cap, `SIGPIPE` suppression |
| **Static Files** | HTML, CSS, JS, PNG, JPG, GIF, SVG, PDF, WOFF2, MP4, … with auto MIME detection |
| **Configuration** | JSON config file with safe defaults |
| **Logging** | Async dedicated I/O thread; workers never block on disk writes |
| **Metrics** | Lock-free atomic counters: requests/s, avg latency, peak concurrency |
| **Shutdown** | SIGINT/SIGTERM → drain queue → join threads → close socket |

---

## Architecture

```
main thread:  socket_.setup() → [accept loop] → pool_.stop()
              │
              ├─ accept() ──────────────────────────────────────────────┐
              └─ pool_.submit(λ)                                        │
                                                                        ▼
         ┌──────────────────────────────────────────────────────────────────────┐
         │                   ThreadPool  (N fixed workers)                     │
         │  Worker 0          Worker 1          Worker N-1                     │
         │  parse_request()   parse_request()   parse_request()                │
         │  router_.route()   router_.route()   router_.route()                │
         │  send_response()   send_response()   send_response()                │
         │  close(fd)         close(fd)         close(fd)                      │
         └──────────────┬───────────────────────────────────────────────────────┘
                        │ logger_.log_request(...)
                        ▼
         ┌──────────────────────────────────────────┐
         │  Logger I/O thread                       │
         │  mutex lock → queue.swap() → unlock      │
         │  file.write() + flush (no mutex held)    │
         └──────────────────────────────────────────┘
```

### Request Lifecycle

```
Client connects → accept() → pool_.submit(λ)         [main thread ends here]
Worker picks up λ:
  parse_request(fd)       select() + recv() loop, 5 s timeout
  router_.route(req)      resolve path, check permissions, read file
  send_response(fd)       write headers + body in one send_all() loop
  close(fd)
  atomic stats update
  logger_.log_request()   queue push (non-blocking)
```

---

## Design Decisions

| Decision | Rationale |
|---|---|
| **Fixed thread pool** | Thread creation costs ~50–200 µs; pooling amortises this to zero and bounds peak memory usage |
| **`std::atomic<int>` for server_fd_** | `close_socket()` is called from the signal handler; atomic `exchange()` makes it safe to race with `accept()` |
| **Queue-swap in logger** | Workers hold the mutex only for a `std::swap` of two pointers; the file write happens lock-free |
| **Error-code overloads for `fs::weakly_canonical`** | The throwing overload would propagate uncaught exceptions into worker threads on a 404 path; the ec overload returns cleanly |
| **Traversal guard with boundary check** | `full.find(root) == 0` alone is insufficient: `/srv/pub` matches `/srv/public`. We additionally require a `/` or exact equality at the boundary |
| **`SO_REUSEADDR`** | Allows immediate restart after SIGKILL without waiting up to 4 minutes for TIME_WAIT |
| **`SIGPIPE` → `SIG_IGN`** | Writing to a peer-closed socket raises SIGPIPE; ignoring it lets `send()` return `EPIPE` instead of killing the process |
| **`sizeof(msg)-1` in signal handler** | `std::strlen` is not async-signal-safe (POSIX); `sizeof` of a string literal is evaluated at compile time |

---

## Folder Structure

```
HTTPServer/
├── CMakeLists.txt          CMake build (C++17, Release/Debug, ASan/UBSan)
├── config.json             Runtime configuration
├── LICENSE                 MIT
├── README.md               This file
├── .gitignore
├── include/                Public API headers
│   ├── config.h
│   ├── logger.h
│   ├── parser.h
│   ├── router.h
│   ├── server.h
│   ├── socket.h
│   └── threadpool.h
├── src/                    Implementation
│   ├── main.cpp
│   ├── config.cpp
│   ├── logger.cpp
│   ├── parser.cpp
│   ├── router.cpp
│   ├── server.cpp
│   ├── socket.cpp
│   └── threadpool.cpp
├── public/                 Document root
│   ├── index.html
│   ├── about.html
│   ├── styles.css
│   └── logo.png
├── logs/
│   └── server.log
└── tests/
    ├── run_tests.sh        Integration test suite (bash + curl)
    └── benchmark.sh        ApacheBench / wrk benchmark script
```

---

## Build

### Prerequisites (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install -y build-essential cmake git
# GCC 9+ or Clang 10+ required
```

### Release

```bash
git clone https://github.com/preethigvs234/HTTPServer.git   
cd HTTPServer
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Debug (AddressSanitizer + UBSan)

```bash
mkdir build-debug && cd build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

---

## Run

```bash
# From the project root so config.json and public/ resolve correctly
cd build
./HTTPServer                      # uses ../config.json
./HTTPServer /path/to/config.json # explicit config path
```

**Startup output:**
```
╔══════════════════════════════════════════╗
║   High-Performance Multi-threaded HTTP   ║
║   Server v1.0  –  port  8080            ║
╚══════════════════════════════════════════╝
  Workers  : 8
  DocRoot  : public
  Log file : logs/server.log
  Press Ctrl+C to stop
```

---

## Configuration

`config.json`:

```json
{
    "port":            8080,
    "workers":         8,
    "document_root":   "public",
    "log_file":        "logs/server.log",
    "backlog":         128,
    "recv_timeout_ms": 5000
}
```

| Key | Default | Description |
|---|---|---|
| `port` | 8080 | TCP listening port |
| `workers` | 8 | Fixed worker thread count |
| `document_root` | "public" | Static file directory |
| `log_file` | "logs/server.log" | Append-mode log path |
| `backlog` | 128 | `listen()` queue depth |
| `recv_timeout_ms` | 5000 | Per-request socket read timeout |

---

## Example Requests

```bash
# Basic GET
curl http://localhost:8080/

# HEAD (no body)
curl -I http://localhost:8080/

# CSS with MIME type check
curl -I http://localhost:8080/styles.css

# 404 response
curl -v http://localhost:8080/nonexistent.html

# Save binary file
curl http://localhost:8080/logo.png -o logo.png

# Directory traversal attempt (blocked → 403/404)
curl http://localhost:8080/../../../etc/passwd
```

---

## Benchmark

Run with the server already started:

```bash
bash tests/benchmark.sh           # defaults to 127.0.0.1:8080
bash tests/benchmark.sh 10.0.0.1 9090
```

> **Results not yet recorded.** Start the server, run the script above, and paste the output here.  
> Expected metrics to capture: requests/sec, mean latency, 99th-percentile latency, transfer rate.

---


## Testing

```bash
# Terminal 1: start server
cd build && ./HTTPServer

# Terminal 2: run integration suite
cd ..
bash tests/run_tests.sh
```

**Test cases covered:**

| Test | Verifies |
|---|---|
| `GET /` → 200 | Directory index resolution |
| `GET /index.html` → 200 | Explicit HTML serving |
| `GET /styles.css` → 200 + `text/css` | MIME type detection |
| `GET /nonexistent` → 404 | Missing file |
| `HEAD /` → 200, 0-byte body | HEAD method |
| `/../../../etc/passwd` → 403/404 | Traversal block |
| `%2F..%2F` → 403/404 | URL-encoded traversal block |
| 20 concurrent GETs | Thread pool under load |
| 10 sequential GETs | Stability |
| Response headers present | HTTP compliance |
| Latency < 500 ms | Performance baseline |

---

## Future Enhancements

| Enhancement | Description |
|---|---|
| HTTP Keep-Alive | Persistent connections (HTTP/1.1 pipelining) |
| `epoll` event loop | Edge-triggered I/O for C10k-scale concurrency |
| TLS / HTTPS | OpenSSL integration |
| Gzip compression | `zlib`-based response compression |
| In-memory LRU cache | Cache hot files to eliminate repeated disk reads |
| Rate limiting | Per-IP token-bucket with atomics |
| Reverse proxy | Forward requests to upstream HTTP servers |
| Docker | Multi-stage Dockerfile + compose |
| Prometheus metrics | `/metrics` endpoint |

---

