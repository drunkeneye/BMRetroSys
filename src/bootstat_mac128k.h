#ifndef BOOTSTAT_MAC128K
#define BOOTSTAT_MAC128K

// Mac boots from user ROM in /roms/MAC128K with no compiled-in fallback
// list, so there are no boot-time ROM files to check (empty list, like
// the Amiga and Hatari builds).

int dflt_bootStatNum = 0;

int dflt_bootStatWhat[] = {
};

const char *dflt_bootStatFile[] = {
};

int dflt_bootStatSize[] = {
};

#endif
