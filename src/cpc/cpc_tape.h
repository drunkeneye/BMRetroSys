//
// cpc_tape.h
//
// CDT (TZX-format) cassette image: parser plus a T-state-driven pulse
// player. Unlike trap loaders, pulse playback works with standard ROM
// loads AND custom turbo loaders (they all read the cassette bit on PPI
// Port B), and it produces authentic loading screech in the audio mix.
//

#ifndef cpc_tape_h
#define cpc_tape_h

namespace bmx {
namespace cpc {

// Attach a .cdt/.tzx image (0 = ok). Detaches any previous tape.
int tape_attach(const char *path);
void tape_detach(void);
bool tape_attached(void);
const char *tape_name(void);
// Transport: rewind to block 0; user play/stop override (default playing).
void tape_rewind(void);
void tape_set_playing(bool playing);
bool tape_playing(void);
// Progress for the tape OSD: current/total data blocks (pauses excluded).
void tape_progress(int *current, int *total);
// Non-zero once the stream has played to the end (ends autostart warp).
bool tape_finished(void);

}  // namespace cpc
}  // namespace bmx

#endif
