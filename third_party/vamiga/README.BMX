// third_party/vamiga/README.BMX
//
// vAmiga integration for BMX bare-metal.
//
// Upstream: vAmiga v5.0b2 by Dirk W. Hoffmann (MPL-2.0,
// https://github.com/dirkwhoffmann/vAmiga), vendored as Core/ below
// (upstream LICENSE kept alongside). Only BMX-owned files live here
// besides the vendored core:
//
//   bmx-shim/vamiga_prefix.h  force-included (-include) on every upstream
//                             vAmiga TU: idempotent system headers + the
//                             BMX_BARE_METAL marker.
//
// Compile set (see top Makefile RASPI_AMIGA block): every Core/**/*.cpp
// EXCEPT:
//   - Misc/RemoteServers/HttpTransport.cpp (cpp-httplib needs OpenSSL/
//     Brotli/pthreads; stubbed in src/amiga/amiga_bmx_stubs.cpp)
//   - Headless.cpp (desktop self-test app; BMX owns the frame loop)
// The emulator thread is never launched: BMX owns a vamiga::VAmiga and
// drives it through its public raw component pointers (amiga.amiga, mem,
// df0, ...) which all act synchronously. CmdQueue-based facade calls
// (powerOn/run/set/press/trigger) are never used.
// Everything else (Moira 68k, CIA, Agnus, Denise, Paula, Memory, Drive,
// Ports, Keyboard/Joystick/Mouse, RetroShell, OSDebugger, rvlib images)
// compiles as-is under -std=c++20.
//
// To update the core: copy a newer upstream Core/ over this directory
// (plus LICENSE if it changed), keep bmx-shim/ untouched, rebuild the
// Amiga kernels on Pi4 + Pi5.
//
