//
// bootstat_a2600.h
//
// The 2600 needs no BIOS ROMs (cartridges carry everything), so there
// is no boot-time ROM checksum table.
//

#ifndef BOOTSTAT_A2600
#define BOOTSTAT_A2600

int dflt_bootStatNum = 0;

int dflt_bootStatWhat[] = {
};

const char *dflt_bootStatFile[] = {
};

int dflt_bootStatSize[] = {
};

#endif
