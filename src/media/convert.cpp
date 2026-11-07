#include "media/convert.h"

#include "platform/platform.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace lumen {

const char *hapVariantName(HapVariant v) {
  switch (v) {
  case HapVariant::HapQ: return "HAP Q";
  case HapVariant::HapAlpha: return "HAP Alpha";
  default: return "HAP";
  }
}

const char *hapFormat(HapVariant v) {
  switch (v) {
  case HapVariant::HapQ: return "hap_q";
  case HapVariant::HapAlpha: return "hap_alpha";
  default: return "hap";
  }
}

std::string hapOutputPath(const std::string &source, HapVariant v) {
  std::filesystem::path src = platform::toPath(source);
  std::string suffix = v == HapVariant::HapQ ? "-hapq" : v == HapVariant::HapAlpha ? "-hapa" : "-hap";
  std::string stem = platform::toUtf8(src.stem()) + suffix;
  std::filesystem::path out = src.parent_path() / platform::toPath(stem + ".mov");
  std::error_code ec;
  for (int n = 2; std::filesystem::exists(out, ec); ++n)
    out = src.parent_path() / platform::toPath(stem + " (" + std::to_string(n) + ").mov");
  return platform::toUtf8(out);
}

std::vector<std::string> hapArgs(const std::string &ffmpeg, const std::string &source,
                                 const std::string &output, HapVariant v, int width, int height) {
  // Big frames in several chunks: Lumen (and other players) decompress them
  // in parallel.
  long long pixels = (long long)width * height;
  int chunks = pixels > 4000LL * 2200 ? 8 : pixels > 2000LL * 1100 ? 4 : 1;
  // Half the CPU cores at most (decoding and encoding): the rest stay free for
  // the show, on top of the low priority of the process.
  std::string threads = std::to_string(std::max(2u, std::thread::hardware_concurrency() / 2));
  return {ffmpeg, "-hide_banner", "-nostdin", "-y", "-loglevel", "error", "-nostats", "-progress", "pipe:1",
          "-threads", threads, "-i", source, "-threads", threads, "-map", "0:v:0", "-map", "0:a?", "-vf", "pad=ceil(iw/4)*4:ceil(ih/4)*4:color=black@0",
          "-c:v", "hap", "-format", hapFormat(v), "-chunks", std::to_string(chunks), "-c:a", "pcm_s24le",
          "-f", "mov", output};
}

double hapProgressSeconds(const std::string &line) {
  // out_time_us (out_time_ms is also in microseconds, despite its name).
  static const std::string key = "out_time_us=";
  if (line.compare(0, key.size(), key) != 0) return -1;
  char *end = nullptr;
  long long us = std::strtoll(line.c_str() + key.size(), &end, 10);
  if (end == line.c_str() + key.size() || us < 0) return -1;
  return double(us) / 1e6;
}

std::string ffmpegPath() {
#ifdef _WIN32
  const char *name = "ffmpeg.exe";
#else
  const char *name = "ffmpeg";
#endif
  std::error_code ec;
  std::filesystem::path local = platform::toPath(platform::executableDir()) / name;
  if (std::filesystem::exists(local, ec)) return platform::toUtf8(local);
  return "ffmpeg";
}

HapConverter::~HapConverter() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
    if (child_) child_->kill();
  }
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
}

int HapConverter::add(ConvertJob job) {
  std::lock_guard<std::mutex> lock(mutex_);
  job.id = nextId_++;
  job.state = ConvertJob::State::Waiting;
  jobs_.push_back(job);
  if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
  wake_.notify_all();
  return job.id;
}

void HapConverter::cancel(int id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &j : jobs_) {
    if (j.id != id) continue;
    if (j.state == ConvertJob::State::Waiting) j.state = ConvertJob::State::Cancelled;
    else if (j.state == ConvertJob::State::Running) {
      cancelId_ = id;
      if (child_) child_->kill();
    }
  }
  idle_.notify_all();
}

void HapConverter::clearFinished() {
  std::lock_guard<std::mutex> lock(mutex_);
  jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                             [](const ConvertJob &j) {
                               return j.state != ConvertJob::State::Waiting && j.state != ConvertJob::State::Running;
                             }),
              jobs_.end());
}

std::vector<ConvertJob> HapConverter::jobs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return jobs_;
}

bool HapConverter::busy() const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &j : jobs_)
    if (j.state == ConvertJob::State::Waiting || j.state == ConvertJob::State::Running) return true;
  return false;
}

bool HapConverter::markApplied(int id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &j : jobs_)
    if (j.id == id && !j.applied) return j.applied = true;
  return false;
}

void HapConverter::waitAll() {
  std::unique_lock<std::mutex> lock(mutex_);
  idle_.wait(lock, [this] {
    for (auto &j : jobs_)
      if (j.state == ConvertJob::State::Waiting || j.state == ConvertJob::State::Running) return false;
    return true;
  });
}

void HapConverter::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    auto next = std::find_if(jobs_.begin(), jobs_.end(),
                             [](const ConvertJob &j) { return j.state == ConvertJob::State::Waiting; });
    if (quit_) return;
    if (next == jobs_.end()) {
      idle_.notify_all();
      wake_.wait(lock);
      continue;
    }
    next->state = ConvertJob::State::Running;
    ConvertJob job = *next; // work on a copy: jobs_ may grow meanwhile
    lock.unlock();
    convert(job);
    lock.lock();
    for (auto &j : jobs_)
      if (j.id == job.id) {
        j.state = job.state;
        j.progress = job.progress;
        j.error = job.error;
      }
    idle_.notify_all();
  }
}

// "frame=12", "speed=2.1x", "progress=continue": the -progress lines.
static bool isProgressKey(const std::string &line) {
  size_t eq = line.find('=');
  if (eq == std::string::npos || eq == 0) return false;
  return std::all_of(line.begin(), line.begin() + long(eq),
                     [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

void HapConverter::convert(ConvertJob &job) {
  // ffmpeg writes a temporary file, renamed only when complete: a stopped or
  // failed conversion never leaves a half file under the final name.
  std::string part = job.output + ".part";
  platform::ChildProcess child;
  std::string err;
  if (!child.start(hapArgs(ffmpegPath(), job.source, part, job.variant, job.width, job.height), &err)) {
    job.state = ConvertJob::State::Failed;
    job.error = "ffmpeg não encontrado (o ffmpeg.exe vem na pasta do Lumen): " + err;
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    child_ = &child;
    if (quit_ || cancelId_ == job.id) child.kill();
  }
  std::string line, lastError;
  while (child.readLine(line)) {
    double s = hapProgressSeconds(line);
    if (s >= 0) {
      if (job.duration > 0) {
        float p = float(std::clamp(s / job.duration, 0.0, 1.0));
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &j : jobs_)
          if (j.id == job.id) j.progress = p;
      }
    } else if (!line.empty() && !isProgressKey(line)) {
      lastError = line; // with -loglevel error only problems are printed
    }
  }
  int code = child.wait();
  bool cancelled;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    child_ = nullptr;
    cancelled = cancelId_ == job.id || quit_;
  }
  std::error_code ec;
  if (cancelled) {
    std::filesystem::remove(platform::toPath(part), ec);
    job.state = ConvertJob::State::Cancelled;
    return;
  }
  if (code != 0) {
    std::filesystem::remove(platform::toPath(part), ec);
    job.state = ConvertJob::State::Failed;
    job.error = code == 127 ? "ffmpeg não encontrado" : lastError.empty() ? "ffmpeg saiu com código " + std::to_string(code) : lastError;
    return;
  }
  std::filesystem::rename(platform::toPath(part), platform::toPath(job.output), ec);
  if (ec) {
    std::error_code ignore;
    std::filesystem::remove(platform::toPath(part), ignore); // no multi-GB leftover
    job.state = ConvertJob::State::Failed;
    job.error = ec.message();
    return;
  }
  job.progress = 1;
  job.state = ConvertJob::State::Done;
}

} // namespace lumen
