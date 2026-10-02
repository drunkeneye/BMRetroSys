#ifndef BMX_UPDATE_ATARIMANIA_CLIENT_H
#define BMX_UPDATE_ATARIMANIA_CLIENT_H

// Atarimania production search + dump download. Assumes the network works;
// all transport lives in netcheck (see netcheck.h). Search/detail run as
// jobs on netcheck's core-0 worker via atarimania_job_runner; the menu
// reads results through the accessors below.

#include <stddef.h>

#include "update/netcheck.h"

#ifdef __cplusplus
extern "C" {
#endif

// Search collections (mirrors the menu "Collection" choice order).
enum {
    ATARIMANIA_GAMES = 0,
    ATARIMANIA_DEMOS = 1,
    ATARIMANIA_UTILS = 2
};

// Site sections (one machine family per search). The menu sets this
// before each search/page turn; the worker reads it in FetchListPage.
enum {
    ATARIMANIA_MACHINE_8BIT = 0,
    ATARIMANIA_MACHINE_ST = 1
};
void atarimania_set_machine(int machine);

// Maximum productions kept per search (matches per=25 requests).
#define ATARIMANIA_MAX_RESULTS 25

// Maximum result pages per search.
#define ATARIMANIA_MAX_PAGES 10

int atarimania_result_count(void);
// All pointers remain valid until the next search completes.
const char *atarimania_result_title(int index);
const char *atarimania_result_year(int index);  // "" when unknown
const char *atarimania_result_url(int index);   // full detail-page URL

int atarimania_page_current(void);
int atarimania_page_total(void);

const char *atarimania_last_error(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// Job runner for netcheck's worker (registered at boot in viceapp).
// Implements SEARCH and DETAIL; TEST is implemented by netcheck itself.
void atarimania_job_runner(bmx::netcheck::NetcheckJob *job);
#endif

#endif  // BMX_UPDATE_ATARIMANIA_CLIENT_H
