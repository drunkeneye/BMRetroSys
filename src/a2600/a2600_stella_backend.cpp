//
// a2600_stella_backend.cpp
//
// SoundBMX::dequeue implementation (needs full AudioQueue definition).
//

#include "a2600_stella_backend.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "AudioQueue.hxx"
#include "EmulationWorker.hxx"
#include "TimerManager.hxx"
#include "tv_filters/AtariNTSC.hxx"

// TimerManager ctor (declared in the vendored header; defined here since
// the threaded TimerManager.cxx is excluded from the BMX build).
TimerManager::TimerManager() {}

// EmulationWorker runs the 6507 on a worker thread (mainLoop/
// dispatchEmulation), which cannot exist on bare metal. Our frame loop
// drives the TIA directly, so mainLoop/dispatchEmulation never run;
// these stubs exist only to satisfy the linker for OSystem.o.
EmulationWorker::EmulationWorker() {}
EmulationWorker::~EmulationWorker() {}
void EmulationWorker::start(uInt32, uInt64, uInt64, DispatchResult*, TIA*) {}
uInt64 EmulationWorker::stop() { return 0; }
void EmulationWorker::handlePossibleException() {}

// TimerManager runs callbacks on a worker thread, which cannot exist on
// bare metal. The only timer source (EventHandler held-event clear) is
// compiled out and StaggeredLogger timers never fire, so addTimer/clear
// are link-only stubs.
TimerManager::~TimerManager() {}
TimerManager::TimerId TimerManager::addTimer(millisec, millisec,
                                            const TFunction&) {
  return 0;
}
bool TimerManager::clear(TimerId) { return false; }

// AtariNTSC renders through worker threads; the BMX kernel always runs
// with tv.filter=OFF and blits the unfiltered base surface instead, so
// these never execute (linker stubs only).
void AtariNTSC::enableThreading(bool) {}
void AtariNTSC::render(const uInt8*, uInt32, uInt32, void*, uInt32, uInt32*) {}
void AtariNTSC::setPalette(const PaletteArray&) {}
void AtariNTSC::initialize(const Setup&) {}

extern "C" {
#include <unistd.h>
}

#include <circle/timer.h>

// Bare-metal libc shims (Stella unittest/mainLoop paths only; never
// executed by the BMX frame loop, which paces itself via CTimer).
extern "C" unsigned int sleep(unsigned int seconds) {
  for (unsigned int i = 0; i < seconds; i++) {
    CTimer::SimpleMsDelay(1000);
  }
  return 0;
}

extern "C" int usleep(useconds_t usec) {
  CTimer::SimpleusDelay((unsigned int)usec);
  return 0;
}

extern "C" int _fcntl(int fd, int cmd, int arg) {
  (void)fd;
  (void)cmd;
  (void)arg;
  return -1;
}

namespace bmx {
namespace a2600 {

void FSNodeBMX::setPath(string_view path) {
  myPath.assign(path.data(), path.size());
  // Preserve bare-metal volume roots ("SD:/", "SYS:/", "USER:/", "/"):
  // stripping the trailing slash turns "SD:/" into "SD:" which stat()
  // cannot resolve on the FatFs newlib layer. A trailing slash preceded
  // by ':' marks a volume root, so keep it.
  while (myPath.size() > 1 && myPath.back() == '/' &&
         myPath[myPath.size() - 2] != ':') {
    myPath.pop_back();
  }
  const size_t slash = myPath.find_last_of('/');
  if (slash == string::npos) {
    myName = myPath;
  } else if (slash + 1 >= myPath.size()) {
    // Volume root ("SD:/"): name is the volume itself.
    myName = myPath.substr(0, myPath.size() - 1);
  } else {
    myName = myPath.substr(slash + 1);
  }
}

void FSNodeBMX::setName(string_view name) {
  const size_t slash = myPath.find_last_of('/');
  if (slash == string::npos) {
    myPath.assign(name.data(), name.size());
  } else {
    myPath = myPath.substr(0, slash + 1) + string(name.data(), name.size());
  }
  setPath(myPath);
}

bool FSNodeBMX::exists() const {
  struct stat st;
  return ::stat(myPath.c_str(), &st) == 0;
}

bool FSNodeBMX::isDirectory() const {
  struct stat st;
  if (::stat(myPath.c_str(), &st) != 0) {
    return false;
  }
  return (st.st_mode & S_IFMT) == S_IFDIR;
}

bool FSNodeBMX::isFile() const {
  struct stat st;
  if (::stat(myPath.c_str(), &st) != 0) {
    return false;
  }
  return (st.st_mode & S_IFMT) == S_IFREG;
}

bool FSNodeBMX::isReadable() const { return exists(); }

bool FSNodeBMX::isWritable() const { return isFile() || !exists(); }

bool FSNodeBMX::hasParent() const {
  if (myPath.empty() || myPath == "/") {
    return false;
  }
  // Volume roots ("SD:/", "SYS:/", ...) have no parent.
  if (myPath.size() >= 2 && myPath.back() == '/' &&
      myPath[myPath.size() - 2] == ':') {
    return false;
  }
  return myPath.find('/') != string::npos;
}

AbstractFSNodePtr FSNodeBMX::getParent() const {
  if (!hasParent()) {
    return std::make_shared<FSNodeBMX>("");
  }
  const size_t slash = myPath.find_last_of('/');
  if (slash == string::npos) {
    return std::make_shared<FSNodeBMX>("");
  }
  if (slash == 0) {
    return std::make_shared<FSNodeBMX>("/");
  }
  return std::make_shared<FSNodeBMX>(myPath.substr(0, slash));
}

bool FSNodeBMX::getChildren(AbstractFSList& list, ListMode mode) const {
  (void)mode;
  DIR* dir = opendir(myPath.c_str());
  if (dir == nullptr) {
    return false;
  }
  struct dirent* ent = nullptr;
  while ((ent = readdir(dir)) != nullptr) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    string child;
    if (!myPath.empty() && myPath.back() == '/') {
      child = myPath + ent->d_name;
    } else {
      child = myPath + "/" + ent->d_name;
    }
    list.push_back(std::make_shared<FSNodeBMX>(child));
  }
  closedir(dir);
  return true;
}

size_t FSNodeBMX::read(ByteArray& buffer, size_t size) const {
  FILE* fp = std::fopen(myPath.c_str(), "rb");
  if (fp == nullptr) {
    return 0;
  }
  std::fseek(fp, 0, SEEK_END);
  const long file_size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  if (file_size <= 0) {
    std::fclose(fp);
    return 0;
  }
  size_t want = (size_t)file_size;
  if (size > 0 && size < want) {
    want = size;
  }
  buffer.resize(want);
  const size_t got = std::fread(buffer.data(), 1, want, fp);
  std::fclose(fp);
  if (got != want) {
    buffer.clear();
    return 0;
  }
  return got;
}

bool FSNodeBMX::makeDir() {
  if (exists()) {
    return isDirectory();
  }
  return ::mkdir(myPath.c_str(), 0777) == 0;
}

bool FSNodeBMX::rename(string_view newfile) {
  string dst(newfile.data(), newfile.size());
  if (::rename(myPath.c_str(), dst.c_str()) != 0) {
    return false;
  }
  setPath(dst);
  return true;
}

uInt32 SoundBMX::dequeue(Int16* stream, uInt32 max_pairs) {
  if (stream == nullptr || max_pairs == 0 || myAudioQueue == nullptr) {
    return 0;
  }
  uInt32 out_pairs = 0;
  // One AudioQueue fragment per iteration; stop when the queue is dry
  // or the caller buffer is full.
  while (out_pairs < max_pairs) {
    Int16* next = myAudioQueue->dequeue(myCurrentFragment);
    if (next == nullptr) {
      break;
    }
    myCurrentFragment = next;
    const uInt32 frag = myAudioQueue->fragmentSize();
    const bool stereo = myAudioQueue->isStereo();
    for (uInt32 i = 0; i < frag && out_pairs < max_pairs; i++) {
      Int16 l = 0, r = 0;
      if (stereo) {
        l = myCurrentFragment[2 * i + 0];
        r = myCurrentFragment[2 * i + 1];
      } else {
        l = r = myCurrentFragment[i];
      }
      stream[out_pairs * 2 + 0] = l;
      stream[out_pairs * 2 + 1] = r;
      out_pairs++;
    }
  }
  return out_pairs;
}

}  // namespace a2600
}  // namespace bmx
