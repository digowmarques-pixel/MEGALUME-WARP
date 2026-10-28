#include "core/parallel.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lumen {

namespace {

class Pool {
public:
  Pool() {
    // Copies are limited by memory bandwidth: a few threads saturate it.
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    int workers = int(std::clamp(hw / 2, 1u, 4u)) - 1;
    for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { loop(); });
  }
  ~Pool() {
    {
      std::lock_guard<std::mutex> lock(m_);
      quit_ = true;
    }
    cv_.notify_all();
    for (auto &t : threads_) t.join();
  }
  int width() const { return int(threads_.size()) + 1; }

  void run(int n, const std::function<void(int)> &fn) {
    if (n <= 0) return;
    if (threads_.empty() || n == 1) {
      for (int i = 0; i < n; ++i) fn(i);
      return;
    }
    // Each run has its own state: a worker that wakes late only ever sees the
    // job it picked up (already exhausted), never the next one.
    auto job = std::make_shared<Job>();
    job->fn = &fn;
    job->n = n;
    job->pending = n;
    std::lock_guard<std::mutex> one(runMutex_);
    {
      std::lock_guard<std::mutex> lock(m_);
      job_ = job;
    }
    cv_.notify_all();
    work(*job);
    std::unique_lock<std::mutex> lock(job->m);
    job->done.wait(lock, [&] { return job->pending == 0; });
    std::lock_guard<std::mutex> lock2(m_);
    job_.reset();
  }

private:
  struct Job {
    const std::function<void(int)> *fn = nullptr;
    int n = 0;
    std::atomic<int> next{0};
    int pending = 0;
    std::mutex m;
    std::condition_variable done;
  };

  static void work(Job &job) {
    for (;;) {
      int i = job.next.fetch_add(1);
      if (i >= job.n) return;
      (*job.fn)(i);
      std::lock_guard<std::mutex> lock(job.m);
      if (--job.pending == 0) job.done.notify_all();
    }
  }
  void loop() {
    std::shared_ptr<Job> last;
    for (;;) {
      std::shared_ptr<Job> job;
      {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&] { return quit_ || (job_ && job_ != last); });
        if (quit_) return;
        job = last = job_;
      }
      work(*job);
    }
  }

  std::vector<std::thread> threads_;
  std::mutex m_, runMutex_;
  std::condition_variable cv_;
  std::shared_ptr<Job> job_;
  bool quit_ = false;
};

Pool &pool() {
  static Pool p;
  return p;
}

} // namespace

void parallelFor(int n, const std::function<void(int)> &fn) { pool().run(n, fn); }

int parallelWidth() { return pool().width(); }

void parallelCopyRows(void *dst, ptrdiff_t dstStride, const void *src, ptrdiff_t srcStride, size_t rowBytes, int rows) {
  auto *d = static_cast<unsigned char *>(dst);
  auto *s = static_cast<const unsigned char *>(src);
  size_t total = rowBytes * size_t(std::max(rows, 0));
  int chunks = total < (2u << 20) ? 1 : std::min(parallelWidth() * 2, rows);
  auto copy = [&](int c) {
    int r0 = int(int64_t(rows) * c / chunks), r1 = int(int64_t(rows) * (c + 1) / chunks);
    if (dstStride == ptrdiff_t(rowBytes) && srcStride == ptrdiff_t(rowBytes)) {
      std::memcpy(d + size_t(r0) * rowBytes, s + size_t(r0) * rowBytes, size_t(r1 - r0) * rowBytes);
      return;
    }
    for (int r = r0; r < r1; ++r) std::memcpy(d + ptrdiff_t(r) * dstStride, s + ptrdiff_t(r) * srcStride, rowBytes);
  };
  if (chunks == 1) copy(0);
  else parallelFor(chunks, copy);
}

} // namespace lumen
