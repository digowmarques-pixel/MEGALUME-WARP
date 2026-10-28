#include "render/stream.h"

#include "core/parallel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

namespace lumen {

namespace {

std::atomic<bool> g_enabled{true};
std::atomic<long long> g_bytes{0}, g_micros{0};

size_t align64(size_t v) { return (v + 63) & ~size_t(63); }

int bytesPerPixel(GLenum internal) {
  switch (internal) {
  case GL_R8: return 1;
  case GL_RG8:
  case GL_R16: return 2;
  case GL_RG16: return 4;
  default: return 4;
  }
}

// Waits for a buffer's previous transfer. With three buffers in the ring the
// GPU is normally long done; the timeout only guards against a lost context.
void retire(GLsync &fence) {
  if (!fence) return;
  glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 100'000'000); // 100 ms
  glDeleteSync(fence);
  fence = nullptr;
}

} // namespace

void StreamUploader::setEnabled(bool on) { g_enabled = on; }
bool StreamUploader::enabled() { return g_enabled; }

void StreamUploader::takeStats(double &megabytes, double &milliseconds) {
  megabytes = double(g_bytes.exchange(0)) / (1024.0 * 1024.0);
  milliseconds = double(g_micros.exchange(0)) / 1000.0;
}

StreamUploader::~StreamUploader() {
  for (auto &s : slots_) {
    if (s.fence) glDeleteSync(s.fence);
    if (s.pbo) glDeleteBuffers(1, &s.pbo);
  }
}

bool StreamUploader::upload(const StreamPlane *planes, int count) {
  if (!g_enabled) return false;
  auto t0 = std::chrono::steady_clock::now();

  // Layout inside the buffer: planes packed tightly (row length = width).
  size_t offsets[4] = {}, total = 0;
  for (int i = 0; i < count && i < 4; ++i) {
    const StreamPlane &p = planes[i];
    offsets[i] = total;
    size_t bytes = p.compressedBytes ? p.compressedBytes
                                     : size_t(p.width) * size_t(bytesPerPixel(p.internalFormat)) * size_t(p.height);
    total = align64(total + bytes);
  }
  if (total == 0) return false;

  Slot &slot = slots_[next_];
  next_ = (next_ + 1) % kSlots;
  retire(slot.fence);
  if (!slot.pbo) glGenBuffers(1, &slot.pbo);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, slot.pbo);
  if (slot.size < total) {
    glBufferData(GL_PIXEL_UNPACK_BUFFER, GLsizeiptr(total), nullptr, GL_STREAM_DRAW);
    slot.size = total;
  }
  // Unsynchronized: the fence above already guarantees the GPU is done with it.
  auto *dst = static_cast<unsigned char *>(glMapBufferRange(
      GL_PIXEL_UNPACK_BUFFER, 0, GLsizeiptr(total),
      GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT));
  if (!dst) {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    return false;
  }
  for (int i = 0; i < count && i < 4; ++i) {
    const StreamPlane &p = planes[i];
    if (p.compressedBytes) {
      // Rows of 4×4 blocks, so big HAP frames are copied in parallel too.
      int blockRows = std::max(1, p.height / 4);
      size_t row = p.compressedBytes % size_t(blockRows) == 0 ? p.compressedBytes / size_t(blockRows) : p.compressedBytes;
      int rows = row == p.compressedBytes ? 1 : blockRows;
      parallelCopyRows(dst + offsets[i], ptrdiff_t(row), p.data, ptrdiff_t(row), row, rows);
    } else {
      size_t row = size_t(p.width) * size_t(bytesPerPixel(p.internalFormat));
      parallelCopyRows(dst + offsets[i], ptrdiff_t(row), p.data, p.stride ? p.stride : ptrdiff_t(row), row, p.height);
    }
  }
  if (!glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER)) { // contents lost (display mode change...)
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    return false;
  }
  // With the buffer bound, the "data" pointers below are offsets into it and
  // the calls return without waiting for the transfer.
  for (int i = 0; i < count && i < 4; ++i) {
    const StreamPlane &p = planes[i];
    const void *src = reinterpret_cast<const void *>(uintptr_t(offsets[i]));
    if (p.compressedBytes) p.texture->uploadCompressed(src, p.width, p.height, p.compressedBytes, p.internalFormat);
    else p.texture->uploadPlane(src, p.width, p.height, 0, p.internalFormat);
  }
  slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  g_bytes += (long long)total;
  g_micros += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
  return true;
}

AsyncReadback::~AsyncReadback() {
  for (auto &s : slots_) {
    if (s.fence) glDeleteSync(s.fence);
    if (s.pbo) glDeleteBuffers(1, &s.pbo);
  }
}

void AsyncReadback::request(const Framebuffer &fb) {
  int w = fb.width(), h = fb.height();
  if (w <= 0 || h <= 0) return;
  Slot &slot = slots_[next_];
  next_ = (next_ + 1) % kSlots;
  if (slot.fence) { // not collected in time: drop it
    glDeleteSync(slot.fence);
    slot.fence = nullptr;
  }
  size_t bytes = size_t(w) * size_t(h) * 4;
  if (!slot.pbo) glGenBuffers(1, &slot.pbo);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
  if (slot.size < bytes) {
    glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(bytes), nullptr, GL_STREAM_READ);
    slot.size = bytes;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fb.id());
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  slot.w = w;
  slot.h = h;
  slot.serial = ++serial_;
}

bool AsyncReadback::collect(std::vector<unsigned char> &topDown, int &w, int &h) {
  // The newest slot whose transfer has finished.
  Slot *best = nullptr;
  for (auto &s : slots_) {
    if (!s.fence || s.serial <= collected_) continue;
    if (glClientWaitSync(s.fence, 0, 0) == GL_TIMEOUT_EXPIRED) continue;
    if (!best || s.serial > best->serial) best = &s;
  }
  if (!best) return false;
  // Older finished reads are superseded.
  for (auto &s : slots_)
    if (s.fence && s.serial < best->serial) {
      glDeleteSync(s.fence);
      s.fence = nullptr;
    }
  glDeleteSync(best->fence);
  best->fence = nullptr;
  collected_ = best->serial;
  w = best->w;
  h = best->h;
  size_t row = size_t(w) * 4, bytes = row * size_t(h);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, best->pbo);
  auto *src = static_cast<const unsigned char *>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(bytes), GL_MAP_READ_BIT));
  bool ok = src != nullptr;
  if (ok) {
    topDown.resize(bytes);
    // Flip while copying: GL rows are bottom-up. A negative source stride
    // walks the buffer from its last row.
    parallelCopyRows(topDown.data(), ptrdiff_t(row), src + (bytes - row), -ptrdiff_t(row), row, h);
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  return ok;
}

} // namespace lumen
