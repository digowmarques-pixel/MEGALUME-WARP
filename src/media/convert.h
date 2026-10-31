// Conversion of video files to HAP with ffmpeg, in a separate process at low
// priority: one file at a time, in the background, cancellable.
#pragma once

#include "platform/process.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lumen {

enum class HapVariant { Hap, HapQ, HapAlpha };

const char *hapVariantName(HapVariant v); // "HAP", "HAP Q", "HAP Alpha"
// ffmpeg's -format value: "hap", "hap_q", "hap_alpha".
const char *hapFormat(HapVariant v);

// "<folder>/<name>-hap.mov" (-hapq, -hapa); "-hap (2).mov"... when taken.
std::string hapOutputPath(const std::string &source, HapVariant v);
// The ffmpeg command line (args[0] = ffmpeg). Width and height are padded to
// multiples of 4 (DXT blocks); the audio is kept as 24-bit PCM.
std::vector<std::string> hapArgs(const std::string &ffmpeg, const std::string &source,
                                 const std::string &output, HapVariant v, int width, int height);
// Seconds converted so far from a line of `-progress` output, or -1.
double hapProgressSeconds(const std::string &line);
// ffmpeg next to lumen(.exe), else the one in PATH.
std::string ffmpegPath();

struct ConvertJob {
  int id = 0;
  int mediaId = -1;   // library item to point at the new file (-1 = none)
  std::string source, output;
  HapVariant variant = HapVariant::Hap;
  int width = 0, height = 0;
  double duration = 0; // seconds, for the progress
  enum class State { Waiting, Running, Done, Failed, Cancelled } state = State::Waiting;
  float progress = 0;  // 0..1
  std::string error;
  bool applied = false; // the result was handled by the UI (swap in the project)
};

class HapConverter {
public:
  ~HapConverter();
  int add(ConvertJob job); // returns its id
  void cancel(int id);
  void clearFinished();
  std::vector<ConvertJob> jobs() const;
  bool busy() const;
  // Marks a finished job as handled; false if it was already.
  bool markApplied(int id);
  // Blocks until every job has finished (command line / tests).
  void waitAll();

private:
  void run();
  void convert(ConvertJob &job);
  mutable std::mutex mutex_;
  std::condition_variable wake_, idle_;
  std::vector<ConvertJob> jobs_;
  std::thread thread_;
  bool quit_ = false;
  int nextId_ = 1;
  int cancelId_ = 0;
  platform::ChildProcess *child_ = nullptr; // the running ffmpeg, under mutex_
};

} // namespace lumen
