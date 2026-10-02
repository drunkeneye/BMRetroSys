//
// a2600_stella_backend.h
//
// BMX backend for the vendored Stella core (Atari 2600): OSystem,
// EventHandler, FrameBuffer backend/surface and Sound implementations.
// Mirrors the libretro port's approach (os/libretro): the core stays
// untouched, all platform I/O lives here. No keyboard/mouse/GPIO/
// network rows: joystick + console switches only.
//

#ifndef a2600_stella_backend_h
#define a2600_stella_backend_h

#include "bspf.hxx"
#include "OSystem.hxx"
#include "EventHandler.hxx"
#include "FBBackend.hxx"
#include "FBSurface.hxx"
#include "Sound.hxx"
#include "AudioQueue.hxx"
#include "AudioSettings.hxx"
#include "EmulationTiming.hxx"
#include "repository/KeyValueRepositoryNoop.hxx"
#include "repository/CompositeKeyValueRepositoryNoop.hxx"
#include "FSNode.hxx"

namespace bmx {
namespace a2600 {

// Minimal filesystem node over the FatFs-backed newlib POSIX layer
// (stat/opendir/mkdir/rename all work on SD:/, SYS:/, USER:/).
class FSNodeBMX : public AbstractFSNode {
 public:
  explicit FSNodeBMX(string_view path) { setPath(path); }
  FSNodeBMX(const FSNodeBMX&) = default;

  bool exists() const override;
  bool getChildren(AbstractFSList& list, ListMode mode) const override;
  const string& getName() const override { return myName; }
  void setName(string_view name) override;
  const string& getPath() const override { return myPath; }
  string getShortPath() const override { return myPath; }
  bool hasParent() const override;
  AbstractFSNodePtr getParent() const override;
  bool isDirectory() const override;
  bool isFile() const override;
  bool isReadable() const override;
  bool isWritable() const override;
  bool makeDir() override;
  bool rename(string_view newfile) override;
  // fopen-based reads (ifstream is unproven on bare-metal newlib).
  size_t read(ByteArray& buffer, size_t size) const override;

 private:
  void setPath(string_view path);
  string myPath;
  string myName;
};

// Fixed TIA output surface (native 160 wide, up to 320 tall incl.
// overscan). The menu scaler samples our RGB565 FBL, not this buffer.
constexpr uInt32 kStellaSurfaceW = 320;
constexpr uInt32 kStellaSurfaceH = 320;

class OSystemBMX : public OSystem {
 public:
  OSystemBMX() = default;
  ~OSystemBMX() override = default;

  shared_ptr<KeyValueRepository> getSettingsRepository() override {
    return std::make_shared<KeyValueRepositoryNoop>();
  }
  shared_ptr<CompositeKeyValueRepository> getPropertyRepository() override {
    return std::make_shared<CompositeKeyValueRepositoryNoop>();
  }
  shared_ptr<CompositeKeyValueRepositoryAtomic> getHighscoreRepository() override {
    return std::make_shared<CompositeKeyValueRepositoryNoop>();
  }

 protected:
  void getBaseDirectories(string& basedir, string& homedir, bool useappdir,
                          string_view usedir) override {
    (void)useappdir;
    (void)usedir;
    // SD card: cartridges in /systems/A2600, states beside snapshots.
    basedir = homedir = string("SD:/");
  }
  void initPersistence(FSNode& basedir) override { (void)basedir; }
  string describePersistence() override { return "none"; }

 private:
  OSystemBMX(const OSystemBMX&) = delete;
  OSystemBMX(OSystemBMX&&) = delete;
  OSystemBMX& operator=(const OSystemBMX&) = delete;
  OSystemBMX& operator=(OSystemBMX&&) = delete;
};

class EventHandlerBMX : public EventHandler {
 public:
  explicit EventHandlerBMX(OSystem& osystem) : EventHandler(osystem) {}
  ~EventHandlerBMX() override = default;

 protected:
  // The bridge pushes Stella events directly every frame; nothing to
  // collect here.
  void pollEvent() override {}

 private:
  EventHandlerBMX() = delete;
  EventHandlerBMX(const EventHandlerBMX&) = delete;
  EventHandlerBMX(EventHandlerBMX&&) = delete;
  EventHandlerBMX& operator=(const EventHandlerBMX&) = delete;
  EventHandlerBMX& operator=(EventHandlerBMX&&) = delete;
};

class FBSurfaceBMX : public FBSurface {
 public:
  FBSurfaceBMX(uInt32 width, uInt32 height)
      : myWidth{width},
        myHeight{height},
        myPixelData{std::make_unique<uInt32[]>(SZT(myWidth) * myHeight)} {
    // Required by the parent class.
    myPixels = myPixelData.get();
    myPitch = myWidth;
  }
  ~FBSurfaceBMX() override = default;

  void fillRect(uInt32 x, uInt32 y, uInt32 w, uInt32 h,
                ColorId color) override {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
  }

  uInt32 width() const override { return myWidth; }
  uInt32 height() const override { return myHeight; }

  const Common::Rect& srcRect() const override { return mySrcR; }
  const Common::Rect& dstRect() const override { return myDstR; }
  void setSrcPos(uInt32 x, uInt32 y) override {
    (void)x;
    (void)y;
  }
  void setSrcSize(uInt32 w, uInt32 h) override {
    (void)w;
    (void)h;
  }
  void setSrcRect(const Common::Rect& r) override { (void)r; }
  void setDstPos(uInt32 x, uInt32 y) override {
    (void)x;
    (void)y;
  }
  void setDstSize(uInt32 w, uInt32 h) override {
    (void)w;
    (void)h;
  }
  void setDstRect(const Common::Rect& r) override { (void)r; }

  void setVisible(bool visible) override { (void)visible; }
  void translateCoords(Int32& x, Int32& y) const override {
    (void)x;
    (void)y;
  }
  bool render() override { return true; }
  void invalidate() override {}
  void invalidateRect(uInt32, uInt32, uInt32, uInt32) override {}
  void reload() override {}
  void resize(uInt32 width, uInt32 height) override {
    (void)width;
    (void)height;
  }
  void setScalingInterpolation(ScalingInterpolation) override {}
  void enableBlend(bool) override {}
  void setBlendLevel(uInt32) override {}

 private:
  uInt32 myWidth{0}, myHeight{0};
  unique_ptr<uInt32[]> myPixelData;
  Common::Rect mySrcR, myDstR;

  FBSurfaceBMX() = delete;
  FBSurfaceBMX(const FBSurfaceBMX&) = delete;
  FBSurfaceBMX(FBSurfaceBMX&&) = delete;
  FBSurfaceBMX& operator=(const FBSurfaceBMX&) = delete;
  FBSurfaceBMX& operator=(FBSurfaceBMX&&) = delete;
};

class FBBackendBMX : public FBBackend {
 public:
  explicit FBBackendBMX(OSystem&) {}
  ~FBBackendBMX() override = default;

  int scaleX(int x) const override { return x; }
  int scaleY(int y) const override { return y; }

  string about() const override { return "Video system: BMX"; }
  void showMessage(string_view message) override { (void)message; }
  void showGaugeMessage(string_view message, string_view valueText,
                        float value, float minValue,
                        float maxValue) override {
    (void)message;
    (void)valueText;
    (void)value;
    (void)minValue;
    (void)maxValue;
  }
  void setTitle(string_view) override {}
  void showCursor(bool) override {}
  bool fullScreen() const override { return true; }
  uInt32 rMask() const override { return 0x00FF0000; }
  uInt32 gMask() const override { return 0x0000FF00; }
  uInt32 bMask() const override { return 0x000000FF; }
  uInt32 aMask() const override { return 0xFF000000; }
  const FBSurface& compositedSurface() override {
    static const FBSurfaceBMX tmp(1, 1);
    return tmp;
  }
  bool isCurrentWindowPositioned() const override { return true; }
  Common::Point getCurrentWindowPos() const override {
    return Common::Point{};
  }
  uInt32 getCurrentDisplayID() const override { return 0; }
  void clear() override {}
  void flush() override {}
  bool setVideoMode(const VideoModeHandler::Mode&, uInt32,
                    const Common::Point&) override {
    return true;
  }
  void grabMouse(bool) override {}
  void enableTextEvents(bool) override {}
  void renderToScreen() override {}
  int refreshRate() const override { return 0; }
  bool isLightTheme() const override { return false; }
  bool isDarkTheme() const override { return false; }

 protected:
  void queryHardware(
      std::unordered_map<uInt32, Common::Size>& fullscreenRes,
      std::unordered_map<uInt32, Common::Size>& windowedRes,
      VariantList& renderers) override {
    // Bare metal has a single fixed display. FrameBuffer::createDisplay
    // rejects any TIA mode larger than the desktop (FailTooLarge), so we
    // must report something plausible; the BMX scaler handles the real
    // output size. 1920x1080 covers the largest TIA zoom the menu offers.
    fullscreenRes[0] = Common::Size(1920, 1080);
    windowedRes[0] = Common::Size(1920, 1080);
    VarList::push_back(renderers, "BMX", "bmx");
  }
  unique_ptr<FBSurface> createSurface(uInt32 w, uInt32 h,
                                      ScalingInterpolation,
                                      const uInt32*) override {
    return std::make_unique<FBSurfaceBMX>(w, h);
  }

 private:
  FBBackendBMX() = delete;
  FBBackendBMX(const FBBackendBMX&) = delete;
  FBBackendBMX(FBBackendBMX&&) = delete;
  FBBackendBMX& operator=(const FBBackendBMX&) = delete;
  FBBackendBMX& operator=(FBBackendBMX&&) = delete;
};

// Sound backend: the TIA mix lands in the AudioQueue; dequeue() moves it
// to the caller, which feeds circle_sound_write(). No WAV/KidVid support.
class SoundBMX : public Sound {
 public:
  SoundBMX(OSystem& osystem, AudioSettings& audioSettings)
      : Sound(osystem), myAudioSettings{audioSettings} {}
  ~SoundBMX() override = default;

  void open(shared_ptr<AudioQueue> audioQueue,
            shared_ptr<const EmulationTiming>) override {
    audioQueue->ignoreOverflows(!myAudioSettings.enabled());
    myAudioQueue = audioQueue;
    myCurrentFragment = nullptr;
    myIsInitializedFlag = true;
  }

  // Drains queued fragments as interleaved stereo into stream (capacity
  // max_samples stereo pairs). Returns pairs actually written.
  uInt32 dequeue(Int16* stream, uInt32 max_pairs);

  void setEnabled(bool) override {}
  void setVolume(uInt32, bool = true) override {}
  void adjustVolume(int = 1) override {}
  void mute(bool) override {}
  void toggleMute() override {}
  bool pause(bool) override { return !myIsInitializedFlag; }
  string about() const override { return ""; }

 private:
  bool myIsInitializedFlag{false};
  shared_ptr<AudioQueue> myAudioQueue;
  Int16* myCurrentFragment{nullptr};
  AudioSettings& myAudioSettings;

  SoundBMX() = delete;
  SoundBMX(const SoundBMX&) = delete;
  SoundBMX(SoundBMX&&) = delete;
  SoundBMX& operator=(const SoundBMX&) = delete;
  SoundBMX& operator=(SoundBMX&&) = delete;
};

}  // namespace a2600
}  // namespace bmx

#endif
