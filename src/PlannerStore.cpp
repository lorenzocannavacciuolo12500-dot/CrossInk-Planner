#include "PlannerStore.h"

#include <Arduino.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PlannerHash.h>
#include <PlannerJson.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "platform/PlannerPlatform.h"

namespace PlannerStore {
namespace {

constexpr size_t kIoChunk = 256;

std::string newStoreId() {
  char buf[12];
  std::snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(planner_platform::random32()));
  return buf;
}

// Reads path (or its .bak left by an interrupted replace) into the decoder.
// Returns 1 = decoded, 0 = no file, -1 = unreadable or malformed.
int readInto(const char* path, planner::PlannerJsonDecoder& decoder) {
  std::string readPath = path;
  if (!Storage.exists(path)) {
    const std::string backup = std::string(path) + ".bak";
    if (!Storage.exists(backup.c_str())) return 0;
    if (Storage.rename(backup.c_str(), path)) {
      LOG_INF("PLN", "Recovered interrupted write for %s", path);
    } else {
      readPath = backup;
    }
  }

  HalFile file;
  if (!Storage.openFileForRead("PLN", readPath, file)) {
    LOG_ERR("PLN", "Cannot open %s", readPath.c_str());
    return -1;
  }
  char chunk[kIoChunk];
  while (true) {
    const int n = file.read(chunk, sizeof(chunk));
    if (n <= 0) break;
    decoder.feed(chunk, static_cast<size_t>(n));
  }
  file.close();
  if (!decoder.finish()) {
    LOG_ERR("PLN", "Malformed %s", readPath.c_str());
    return -1;
  }
  if (decoder.skippedItems() > 0) {
    LOG_INF("PLN", "Skipped %u invalid entries in %s", static_cast<unsigned>(decoder.skippedItems()), path);
  }
  return 1;
}

// Buffers the encoder's small writes into SD-friendly chunks.
struct FileSink {
  explicit FileSink(HalFile* f) : file(f) {}
  HalFile* file;
  char buf[kIoChunk] = {};
  size_t used = 0;
  bool failed = false;

  void flush() {
    if (used == 0 || failed) return;
    if (file->write(buf, used) != used) failed = true;
    used = 0;
  }
  static void write(void* ctx, const char* data, size_t len) {
    auto* self = static_cast<FileSink*>(ctx);
    while (len > 0 && !self->failed) {
      const size_t room = sizeof(self->buf) - self->used;
      const size_t n = len < room ? len : room;
      std::memcpy(self->buf + self->used, data, n);
      self->used += n;
      data += n;
      len -= n;
      if (self->used == sizeof(self->buf)) self->flush();
    }
  }
};

// Writes through path.tmp, keeps path.bak until the rename succeeds (same
// recovery contract as PersistableStoreBase::writeDocToFileAtomically).
template <typename EncodeFn>
bool writeAtomically(const char* path, EncodeFn encode) {
  Storage.mkdir(kDir);
  const std::string tempPath = std::string(path) + ".tmp";
  const std::string backupPath = std::string(path) + ".bak";
  if (Storage.exists(tempPath.c_str())) Storage.remove(tempPath.c_str());

  {
    HalFile file;
    if (!Storage.openFileForWrite("PLN", tempPath, file)) {
      LOG_ERR("PLN", "Cannot create %s", tempPath.c_str());
      return false;
    }
    // Heap-free: the sink (≈270 bytes) lives on the caller's stack only for this
    // write. Callers are the main task and the web server task, both of which
    // have multi-kilobyte stacks.
    FileSink sink(&file);
    encode(planner::JsonSink{&sink, &FileSink::write});
    sink.flush();
    const bool ok = !sink.failed && file.sync();
    file.close();
    if (!ok) {
      LOG_ERR("PLN", "Write failed for %s", tempPath.c_str());
      Storage.remove(tempPath.c_str());
      return false;
    }
  }

  const bool hadOriginal = Storage.exists(path);
  if (hadOriginal) {
    if (Storage.exists(backupPath.c_str())) Storage.remove(backupPath.c_str());
    if (!Storage.rename(path, backupPath.c_str())) {
      LOG_ERR("PLN", "Cannot back up %s", path);
      Storage.remove(tempPath.c_str());
      return false;
    }
  }
  if (!Storage.rename(tempPath.c_str(), path)) {
    LOG_ERR("PLN", "Cannot replace %s", path);
    if (hadOriginal) Storage.rename(backupPath.c_str(), path);
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (hadOriginal) Storage.remove(backupPath.c_str());
  return true;
}

}  // namespace

std::mutex& lock() {
  static std::mutex mutex;
  return mutex;
}

bool loadTodos(planner::TodoList& out) {
  int result;
  {
    planner::PlannerJsonDecoder decoder(out);
    result = readInto(kTodosPath, decoder);
  }
  if (result < 0) {
    // Keep the unreadable file for inspection instead of silently overwriting it.
    const std::string corrupt = std::string(kTodosPath) + ".corrupt";
    Storage.remove(corrupt.c_str());
    Storage.rename(kTodosPath, corrupt.c_str());
    out = planner::TodoList{};
  }
  if (out.storeId.empty()) out.storeId = newStoreId();
  return result >= 0;
}

bool saveTodos(const planner::TodoList& list) {
  return writeAtomically(kTodosPath, [&list](const planner::JsonSink& sink) {
    planner::writeTodoListJson(list, planner::TodoJsonMode::File, sink);
  });
}

namespace {
// Events fingerprint of calendar.json as last read or written; 0 = unknown.
std::atomic<uint32_t> gCalendarHash{0};

uint32_t knownHash(const planner::Calendar& calendar) {
  const uint32_t h = planner::calendarEventsHash(calendar);
  return h == 0 ? 1 : h;  // keep 0 for "unknown"
}
}  // namespace

bool loadCalendar(planner::Calendar& out) {
  int result;
  {
    planner::PlannerJsonDecoder decoder(out);
    result = readInto(kCalendarPath, decoder);
  }
  if (result < 0) out = planner::Calendar{};
  gCalendarHash.store(result >= 0 ? knownHash(out) : 0, std::memory_order_relaxed);
  return result >= 0;
}

bool saveCalendar(const planner::Calendar& calendar) {
  const bool ok = writeAtomically(
      kCalendarPath, [&calendar](const planner::JsonSink& sink) { planner::writeCalendarJson(calendar, sink); });
  gCalendarHash.store(ok ? knownHash(calendar) : 0, std::memory_order_relaxed);
  return ok;
}

bool calendarUnchanged(const planner::Calendar& calendar) {
  const uint32_t last = gCalendarHash.load(std::memory_order_relaxed);
  return last != 0 && last == knownHash(calendar);
}

int64_t nowForEdits() {
  time_t now = 0;
  return planner_platform::getUnixTime(now) ? static_cast<int64_t>(now) : 0;
}

namespace {
std::atomic<uint32_t> gExternalChanges{0};
std::atomic<uint8_t> gActivePage{0};
}  // namespace

void noteExternalChange() { gExternalChanges.fetch_add(1, std::memory_order_relaxed); }
uint32_t externalChangeCount() { return gExternalChanges.load(std::memory_order_relaxed); }

void setActivePage(const ResumePage page) { gActivePage.store(static_cast<uint8_t>(page), std::memory_order_relaxed); }
ResumePage activePage() { return static_cast<ResumePage>(gActivePage.load(std::memory_order_relaxed)); }

}  // namespace PlannerStore
