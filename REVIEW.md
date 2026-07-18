# Code Review Report

## Bugs Fixed

### Critical

| # | File | Bug | Fix Applied |
|---|---|---|---|
| 1 | `router.cpp` | `fs::weakly_canonical()` (throwing overload) called inside worker threads — a `404` to a non-existent deep path would throw `std::filesystem::filesystem_error`, terminate the worker thread silently, and decrement the thread-pool size permanently | Replaced with error-code overloads throughout; all filesystem errors return 404/500 responses |
| 2 | `router.cpp` | Traversal guard `full_str.find(root_str) != 0` was insufficient: `/srv/pub` matches `/srv/public` as a prefix, falsely allowing access to a sibling directory | Guard now checks `full_str[root_str.size()] == '/'` (or exact equality), enforcing a path-component boundary |
| 3 | `logger.cpp` | In `flush_loop()`, the exit check `!running_ && queue_.empty()` ran **after** releasing the mutex. A producer could push a message between the swap and the check; on shutdown those messages would be lost | Exit check moved to **inside** the lock, before releasing it |
| 4 | `server.cpp` | Signal handler called `std::strlen()`, which is **not async-signal-safe** per POSIX | Replaced with compile-time `sizeof(msg) - 1` |
| 5 | `socket.cpp` | `server_fd_` was a plain `int`; `close_socket()` called from the signal-handler path could race with `accept_client()` reading the fd | `server_fd_` changed to `std::atomic<int>`; `close_socket()` uses `exchange(-1)` so exactly one caller closes, race-free |
| 6 | `server.cpp` | `status` variable initialised to 400 but only updated in the `else` branch; the `log_request()` call always logged the wrong code for 400 responses | `status` removed; `response.status_code` is read directly in both branches |
| 7 | `parser.cpp` | `'+'` in URI paths was decoded as a space. RFC 3986 §3.3 states `'+'` is a literal plus in path segments (only `application/x-www-form-urlencoded` decodes it as space) | `url_decode()` no longer converts `'+'` in any context; added a note in the doc comment |
| 8 | `parser.cpp` | No cap on `Content-Length`: a client sending `Content-Length: 2147483648` would cause the server to attempt a ~2 GiB allocation | Body read capped at `MAX_BODY_SIZE = 1 MiB` |
| 9 | `threadpool.cpp` | `stop()` could be called twice (once explicitly, once via destructor), causing the workers vector to be joined a second time | `stop()` now uses `stop_.exchange(true)`; second call returns immediately |
| 10 | `threadpool.cpp` | `submit()` accepted tasks after `stop()` was called (tasks submitted during shutdown were queued but the pool was already joining) | `submit()` checks `stop_` under the mutex and silently drops tasks if stopping |

### Minor / Quality

| # | File | Issue | Fix |
|---|---|---|---|
| 11 | `router.cpp` | Permission check `!(A) && !(B) && !(C)` — harder to read | Replaced with `!(st.st_mode & (S_IRUSR \| S_IRGRP \| S_IROTH))` |
| 12 | `router.cpp` | `send_response()` did not retry partial `send()` calls | Extracted `send_all()` lambda that loops until all bytes are sent |
| 13 | `parser.cpp` | Header value trimming used `value.erase(value.begin())` in a loop — O(n²) | Replaced with `find_first_not_of` + single `substr()` — O(n) |
| 14 | `parser.cpp` | `url_decode()` did not validate that `%XX` nibbles are valid hex digits | Added `std::isxdigit()` check; invalid sequences pass through unchanged |
| 15 | `logger.cpp` | `std::cout` output in `log_request()` happened outside any lock, racing with startup banner printed by `server.cpp` | stdout write now occurs inside the same `lock_guard` that protects the queue push |
| 16 | `server.cpp` | Lambda capture had `mutable` specifier despite no mutation | Removed `mutable` |
| 17 | `router.cpp` | `#include <cassert>` and `#include <stdexcept>` were present but unused | Removed |
| 18 | `CMakeLists.txt` | `-Wconversion` produced false positives on POSIX APIs (ssize_t/size_t) and was passed through `target_compile_options` but the sanitiser link flags were added via a string comparison, not generator expressions | `-Wconversion` removed; sanitiser link flags use `target_link_options` with the same generator expression |

---

## Performance Improvements

| Improvement | Impact |
|---|---|
| Header value trimming: O(n²) → O(n) | Reduces CPU time for requests with many long headers |
| `send_all()` retry loop in `send_response()` | Prevents silent truncation on large file transfers over slow or congested connections |
| Logger queue-swap exit check under lock | Eliminates a redundant second acquire on every flush cycle |

---

## Security Improvements

| Improvement | Detail |
|---|---|
| Traversal guard boundary check | Prevents sibling-directory access (`/srv/pub` → `/srv/public`) |
| Body size cap (1 MiB) | Prevents memory-exhaustion via large `Content-Length` |
| `url_decode` hex validation | Prevents silent decoding of invalid percent-sequences that could confuse path normalisation |
| RFC 3986 `'+'` handling | Prevents `+` in a filename URL from mapping to a space-named file |

---

## Improvements Made (Summary)

- **7 source files rewritten** with all fixes applied
- **All 7 headers** cleaned up (removed prose restating implementation; kept API contracts)
- **README.md** rewritten to ~220 lines, professional quality
- **Added** `LICENSE` (MIT), `.gitignore`, `tests/benchmark.sh`
- **CMakeLists.txt** fixed: generator expressions consistent, `-Wconversion` removed

---

## Remaining Limitations

These are **known, documented** limitations appropriate for a portfolio project:

| Limitation | Impact | Mitigation Path |
|---|---|---|
| No HTTP Keep-Alive | Each request opens and closes a TCP connection | Implement persistent-connection loop in `handle_client()` |
| Files loaded entirely into memory | Very large files (> a few hundred MiB) would exhaust RAM | Use `sendfile(2)` or memory-mapped I/O |
| Single `document_root` per process | No virtual hosting | Add host-based routing in the Router |
| No TLS | Traffic is plaintext | Wrap with OpenSSL BIO or use a TLS termination proxy |
| `select()` for socket timeout | `select()` is O(n) in the fd number; for high-fd systems `poll()` is better | Replace `wait_readable()` with `poll()` |
| Body read limited to 1 MiB | POST bodies larger than 1 MiB are silently truncated | Increase `MAX_BODY_SIZE` and add streaming support |

---

## Future Enhancements

HTTP Keep-Alive, `epoll`-based I/O multiplexing, TLS/HTTPS via OpenSSL, Gzip/Brotli compression, in-memory LRU cache, per-IP rate limiting, reverse proxy, Docker multi-stage build, Prometheus `/metrics` endpoint.
