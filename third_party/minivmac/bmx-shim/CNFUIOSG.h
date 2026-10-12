/*
	BMX platform configuration for Mini vMac (Macintosh 128K).

	Shadows the setup-tool generated CNFUIOSG.h (which pulls X11/ALSA
	system headers for the Unix backends). BMX bare-metal needs none
	of that: video/sound/input/files are provided by
	src/mac128k/mac128k_bmx_backend.c. Include order must place
	third_party/minivmac/bmx-shim before third_party/minivmac/cfg.
*/

#ifdef CNFUIOSG_H
#error "header already included"
#else
#define CNFUIOSG_H
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* No host OS services: no app path, no drag-drop, no fullscreen hotkeys. */
#define CanGetAppPath 0
#define HaveAppPathLink 0
#define HaveSysctlPath 0

#define EnableDragDrop 0

#define kStrAppName "Mac 128K"
#define kAppVariationStr "bmx-mac128k"
#define kStrCopyrightYear "2026"
#define kMaintainerName "BMRetroSys"
#define kStrHomePage "(unknown)"
