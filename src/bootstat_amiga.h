#ifndef BOOTSTAT_AMIGA
#define BOOTSTAT_AMIGA

// Amiga boots from SD /kick (user-supplied Kickstart, never in git) with a
// compiled-in DiagROM fallback, so there are no boot-time ROM files to
// check (empty list, like the Hatari build).

int dflt_bootStatNum = 0;

int dflt_bootStatWhat[] = {
};

const char *dflt_bootStatFile[] = {
};

int dflt_bootStatSize[] = {
};

#endif
