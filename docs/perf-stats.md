# Performance diagnostic build

Based on upstream 2f4b8a335d5e27a5153bdea670bd877a061e37b1, matching the user's downloaded release.

Every five seconds during emulation the build appends a `[perf]` line to
`C:/KYTY/_perf-detail.txt` and prints it to stdout. A separate reporter continues writing
when no frames arrive. The log is flushed on each write and preserved between runs.
No game files, paths, account details, or saved games are included in these statistics.

- `fps`: completed guest flip groups per second; repeated display frames are excluded.
- `gpu_work_ms`: wall time processing GPU submissions and queued commands on the emulator's GPU thread.
- `gpu_idle_ms`: wall time waiting for new commands or blocked submissions to become runnable.
- `shader_ms`: shader cache miss handling, including translation, compilation, and module creation.
- `pipeline_ms`: Vulkan graphics/compute pipeline creation calls.
- `host_wait_ms`: blocking Vulkan timeline semaphore waits after the fast-path completion checks.

Each timing is followed by `/completed_calls`, cumulative `total_ms`, and the
number of unfinished scopes (`active`). A scope is charged to the reporting period
when it finishes, so long waits may exceed one period. Cumulative values are useful
when assessing these waits. Timings are inclusive and can overlap across categories
and threads. They are not hardware GPU utilization and must not be added together.

The only rendering-code changes are timers and one flip counter. This patch does
not change emulator settings or attempt to improve performance. Debug logging and
validation can significantly alter the measurements; use the same settings across
comparisons. Build only Windows in the fork, run the upstream checks plus the
concurrent-counter/reporter smoke test, and upload an artifact without publishing a release.

## Detailed renderer counters (v2)

The initial capture showed about 30 FPS in the early section and 6.2 FPS in slow steady sections, with zero new shader/pipeline compilation during those sections.

Additional inclusive timings cover draw/compute execution, descriptor preparation and commit, shader and pipeline lookups including cache hits, buffer synchronization and CPU reads, texture lookups, and submission maintenance (fault processing, downloads, garbage collection).

These timings overlap and include nested waits and compilation. Do not add them together. The v2 build preserves the emulation behavior and uses a separate log to keep the first capture intact.
