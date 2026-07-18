# Final Pre-Release Review

## Summary

The project was feature-complete and architecturally sound before this review.
Only targeted cleanup was applied — no features added, no architecture changed.

---

## Code Cleanup

### Unused includes removed

| File | Include removed |
|---|---|
| `include/config.h` | `<stdexcept>` — only used in the `.cpp` |
| `include/logger.h` | `<chrono>` — no `chrono` types in the header |

### Dead code removed

| File | Removed |
|---|---|
| `tests/run_tests.sh` | `local all_ok=true` — declared but never read |

### Fabricated data removed

The benchmark section of `README.md` contained invented numbers (req/sec,
latency, transfer rate). These have been replaced with an honest placeholder.
**Fill in real numbers after running `tests/benchmark.sh`.**

### Placeholder clarified

The `git clone` URL `yourname/HTTPServer` now reads `<your-username>/HTTPServer`
with an inline comment.

---

## Comment Reduction

All source files had comments reduced by approximately 40%.  
Only design-decision, concurrency, security, and ownership comments were kept.

| File | Kept | Removed |
|---|---|---|
| `config.h/cpp` | One-line design note on thread-safety | Doxygen param blocks, section separators |
| `logger.cpp` | Shutdown-race explanation, stdout-lock rationale | Phase labels, restatement comments |
| `parser.cpp` | Security-cap block, RFC 7230 OWS note | Phase markers, restatement comments |
| `router.cpp` | Static-map thread-safety, `weakly_canonical` rationale, boundary-check explanation | Section separators, restatement comments |
| `server.cpp` | Concurrency model, stop() safety, CAS loop, SIGPIPE | Section separators, restatement comments |
| `threadpool.cpp` | Drain guarantee, `exchange()` idempotency | All other comments |
| `main.cpp` | None needed | Entire doc block and section separators |

---

## Const Correctness

`const` added to local variables throughout `config.cpp`, `parser.cpp`,
`router.cpp`, and `server.cpp` where a value is set once and not modified.

---

## Remaining Limitations

| Limitation | Mitigation Path |
|---|---|
| No HTTP Keep-Alive | Persistent-connection loop in `handle_client()` |
| Files loaded into memory | `sendfile(2)` or `mmap` for large assets |
| `select()` for read timeout | Replace with `poll()` to remove fd-number cap |
| No TLS | OpenSSL or TLS termination proxy |
| Body capped at 1 MiB | Make `MAX_BODY_SIZE` configurable |
| **Benchmark not yet measured** | Run `tests/benchmark.sh`, paste output into README |

---

## Suggested Future Enhancements

1. Run the benchmark and fill in README results
2. HTTP Keep-Alive (persistent connections)
3. `sendfile(2)` zero-copy file transfers
4. In-memory LRU cache for hot files
5. Rate limiting (per-IP token bucket)
6. Docker multi-stage build
7. Prometheus `/metrics` endpoint
