// A small persistent thread pool for memory-bound work (copying video frames).
#pragma once

#include <cstddef>
#include <functional>

namespace lumen {

// Runs fn(i) for i in [0, n) on the pool and the calling thread; returns when
// all are done. Calls from several threads at once are serialized.
void parallelFor(int n, const std::function<void(int)> &fn);
// Worker threads plus the caller.
int parallelWidth();

// Copies `rows` rows of `rowBytes` bytes, splitting large copies across the
// pool. A negative stride walks upwards (vertical flip).
void parallelCopyRows(void *dst, ptrdiff_t dstStride, const void *src, ptrdiff_t srcStride, size_t rowBytes, int rows);

} // namespace lumen
