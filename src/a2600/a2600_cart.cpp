//
// a2600_cart.cpp
//
// See a2600_cart.h. Owns the Stella OSystem + console, steps frames.
//

#include "a2600_cart.h"
#include "a2600_config.h"
#include "a2600_debug.h"
#include "a2600_input.h"
#include "a2600_stella_backend.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "bspf.hxx"
#include "OSystem.hxx"
#include "Console.hxx"
#include "TIA.hxx"
#include "TIASurface.hxx"
#include "FrameBuffer.hxx"
#include "M6532.hxx"
#include "Settings.hxx"
#include "AudioSettings.hxx"
#include "StateManager.hxx"
#include "Serializer.hxx"
#include "FSNode.hxx"
#include "MediaFactory.hxx"
#include "Event.hxx"
#include "EventHandler.hxx"

namespace bmx {
namespace a2600 {

namespace {

std::unique_ptr<OSystem> s_os;
char s_path[256] = {0};

float stella_rate(bool ntsc) {
  return ntsc ? (262.0f * 76.0f * 60.0f) / 38.0f
              : (312.0f * 76.0f * 50.0f) / 38.0f;
}

}  // namespace

bool cart_create(const char *path) {
  cart_destroy();
  if (path == nullptr || *path == '\0') {
    return false;
  }
  try {
    s_os = MediaFactory::createOSystem();
    const Settings::Options options;
    if (!s_os->initialize(options)) {
      debug_log("cart: OSystem initialize failed");
      cart_destroy();
      return false;
    }
    Settings &settings = s_os->settings();
    settings.setValue("speed", 1.0);
    settings.setValue("uimessages", false);
    settings.setValue("format", "AUTO");
    settings.setValue("palette", "standard");
    settings.setValue("tia.zoom", 1);
    settings.setValue("tia.inter", false);
    settings.setValue("tv.filter", 0);
    settings.setValue("tv.phosphor", "never");
    settings.setValue(AudioSettings::SETTING_PRESET,
                      I32(AudioSettings::Preset::custom));
    settings.setValue(AudioSettings::SETTING_SAMPLE_RATE,
                      stella_rate(true));
    settings.setValue(AudioSettings::SETTING_BUFFER_SIZE, 8);
    settings.setValue(AudioSettings::SETTING_HEADROOM, 0);
    settings.setValue(AudioSettings::SETTING_RESAMPLING_QUALITY,
                      I32(AudioSettings::ResamplingQuality::nearestNeighbour));
    settings.setValue(AudioSettings::SETTING_VOLUME, 100);
    settings.setValue(AudioSettings::SETTING_STEREO, "mono");
    // No mouse on this port (no USB mouse routing, pollEvent() is empty):
    // keep Stella from ever instantiating mouse controllers for Trak-Ball
    // ROMs. Sticks only.
    settings.setValue("usemouse", "never");
    const FSNode rom(path);
    string err = s_os->createConsole(rom);
    if (!err.empty()) {
      debug_log("cart: createConsole failed: %s", err.c_str());
      cart_destroy();
      return false;
    }
    // Match the audio rate to the detected region.
    const bool ntsc = s_os->console().gameRefreshRate() == 60;
    settings.setValue(AudioSettings::SETTING_SAMPLE_RATE,
                      stella_rate(ntsc));
    s_os->console().initializeAudio();
    snprintf(s_path, sizeof(s_path), "%s", path);
    debug_log("cart: '%s' ready (%s)", path, ntsc ? "NTSC" : "PAL");
    return true;
  } catch (std::exception &e) {
    debug_log("cart: exception: %s", e.what());
  } catch (...) {
    debug_log("cart: unknown exception");
  }
  cart_destroy();
  return false;
}

void cart_destroy(void) {
  s_os.reset();
  s_path[0] = '\0';
}

bool cart_has_console(void) {
  return s_os != nullptr && s_os->hasConsole();
}

const char *cart_path(void) { return s_path; }

bool cart_run_frame(const uint32_t *&out_pixels, int &out_pitch, int &out_w,
                    int &out_h, int &out_refresh) {
  out_pixels = nullptr;
  if (!cart_has_console()) {
    return false;
  }
  try {
    Console &console = s_os->console();
    TIA &tia = console.tia();
    // Step scanlines until the frame completes (scanlines wraps to 0).
    // Cap iterations so a jammed CPU / vsync-less ROM can never hang the
    // frame loop (which would freeze the F12 menu too).
    for (int i = 0; i < 1024; i++) {
      tia.updateScanline();
      if (tia.scanlines() == 0) {
        break;
      }
    }
    if (!tia.newFramePending()) {
      return false;
    }
    // Keeps the TIA frame counters exact (same call the other ports
    // make); we blit from baseSurface() below instead of this output.
    tia.renderToFrameBuffer();
    Common::Rect rect;
    const FBSurface &surface =
        s_os->frameBuffer().tiaSurface().baseSurface(rect);
    uInt32 pitch = 0;
    uInt32 *pixels = nullptr;
    surface.basePtr(pixels, pitch);
    if (pixels == nullptr) {
      return false;
    }
    out_pixels = pixels;
    out_pitch = (int)pitch;
    out_w = rect.w();
    out_h = rect.h();
    out_refresh = console.gameRefreshRate();
    return true;
  } catch (std::exception &e) {
    debug_log("cart: frame exception: %s", e.what());
  } catch (...) {
    debug_log("cart: frame unknown exception");
  }
  return false;
}

unsigned int cart_drain_audio(int16_t *stereo, unsigned int max_pairs,
                             int &stella_rate_out) {
  stella_rate_out = 0;
  if (!cart_has_console() || stereo == nullptr || max_pairs == 0) {
    return 0;
  }
  try {
    SoundBMX &sound = static_cast<SoundBMX &>(s_os->sound());
    uInt32 got = 0;
    // Pull fragments until one frame worth is drained.
    const uInt32 want =
        (uInt32)(s_os->settings().getFloat(AudioSettings::SETTING_SAMPLE_RATE) /
                 (float)s_os->console().gameRefreshRate()) +
        1;
    uInt32 budget = want < max_pairs ? want : max_pairs;
    while (got < budget) {
      uInt32 room = budget - got;
      uInt32 n = sound.dequeue(stereo + got * 2, room);
      if (n == 0) {
        break;
      }
      got += n;
    }
    stella_rate_out = (int)s_os->settings().getFloat(
        AudioSettings::SETTING_SAMPLE_RATE);
    return got;
  } catch (...) {
    return 0;
  }
}

void cart_pump_input(void) {
  // Always pump (even with no cartridge) so the framework key queue is
  // drained every frame; input_pump_frame() skips Stella objects when
  // there is no console.
  try {
    input_pump_frame(cart_has_console() ? s_os.get() : nullptr);
  } catch (...) {
  }
}

bool cart_save_state(const char *path) {
  if (!cart_has_console() || path == nullptr || *path == '\0') {
    return false;
  }
  try {
    // Memory blob + C file I/O (fstream is unproven on bare metal).
    Serializer out;
    if (!s_os->state().saveState(out)) {
      return false;
    }
    const size_t size = out.size();
    if (size == 0) {
      return false;
    }
    std::vector<uInt8> buf(size);
    out.rewind();
    out.getByteArray(ByteMSpan(buf.data(), size));
    FILE *fp = fopen(path, "wb");
    if (fp == nullptr) {
      return false;
    }
    const size_t wrote = fwrite(buf.data(), 1, size, fp);
    fclose(fp);
    if (wrote != size) {
      remove(path);
      return false;
    }
    debug_log("cart: state saved '%s' (%u bytes)", path, (unsigned)size);
    return true;
  } catch (...) {
    return false;
  }
}

bool cart_load_state(const char *path) {
  if (!cart_has_console() || path == nullptr || *path == '\0') {
    return false;
  }
  try {
    FILE *fp = fopen(path, "rb");
    if (fp == nullptr) {
      return false;
    }
    fseek(fp, 0, SEEK_END);
    const long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > 8 * 1024 * 1024) {
      fclose(fp);
      return false;
    }
    std::vector<uInt8> buf((size_t)size);
    const size_t got = fread(buf.data(), 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size) {
      return false;
    }
    Serializer in;
    in.putByteArray(ByteSpan(buf.data(), buf.size()));
    in.rewind();
    const bool ok = s_os->state().loadState(in);
    debug_log("cart: state load '%s' %s", path, ok ? "ok" : "FAILED");
    return ok;
  } catch (...) {
    return false;
  }
}

}  // namespace a2600
}  // namespace bmx
