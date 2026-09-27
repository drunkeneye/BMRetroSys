#ifndef BMX_UPDATE_NETCHECK_H
#define BMX_UPDATE_NETCHECK_H

// Generic network machinery: timestamped tracing, bounded DNS, staged
// connectivity testing, and the core-0 worker that runs network jobs off
// the scheduler-foreign menu thread. No Atarimania knowledge lives here;
// feature code (Atarimania) uses these primitives and registers a job
// runner (see atarimania_client.h).

#include <stddef.h>
#include <stdint.h>

class CNetSubSystem;
class CIPAddress;

namespace bmx {
namespace netcheck {

// Timestamped trace line: serial printf plus an optional host sink (the
// Atari kernel forwards into its debug log; weak elsewhere).
void Trace(const char *fmt, ...);

// Plausible wall clock for TLS validation (build epoch anchor).
uint64_t EstablishClock(CNetSubSystem *net);

// Staged connectivity test: TCP 8.8.8.8:53 (raw route), then TCP to
// Atarimania's numeric IP. No DNS anywhere. Returns 1/0, msg set.
int TestConnection(char *msg, size_t msg_size);

// Live stage text for menu buttons (shown while a job runs).
void SetJobStage(const char *stage);

// TCP connect to a numeric IPv4 literal (no DNS involved). Used for
// route checks and IP fallbacks.
bool TcpConnectNumeric(CNetSubSystem *net, const char *numeric_ip,
                       uint16_t port);

// ---- Async jobs (core-0 worker) ----
// Protocol: starters fill the slot (0 = busy); the menu pump polls with a
// watchdog; cancel is honored between stages. Kinds are opaque ints; the
// registered runner implements them.
enum {
    NETCHECK_JOB_NONE = 0,
    NETCHECK_JOB_TEST,
    NETCHECK_JOB_SEARCH,
    NETCHECK_JOB_DETAIL
};

struct NetcheckJob {
    volatile int state;  // 0 idle, 1 running, 2 done
    volatile int cancel;
    volatile unsigned tag;
    int kind;
    char query[80];
    int collection;
    int page;
    char url[256];
    int result;
    char msg[160];
    char path[160];
    char stage[64];  // live stage text for menu buttons
};

typedef void (*NetcheckRunner)(NetcheckJob *job);

}  // namespace netcheck
}  // namespace bmx

#ifdef __cplusplus
extern "C" {
#endif

// Optional host log sink for netcheck_trace lines (forwarded by kernels
// that keep a log file; weak everywhere else).
void netcheck_host_trace(const char *line);

void netcheck_worker_init(bmx::netcheck::NetcheckRunner runner);
int netcheck_job_start_test(void);
int netcheck_job_start_search(const char *query, int collection, int page);
int netcheck_job_start_detail(const char *url);
int netcheck_job_busy(void);
void netcheck_job_cancel(void);
// Returns 1 when a fresh completion is available (consumes it).
int netcheck_job_poll(int *kind_out, int *result_out, char *msg_out,
                       size_t msg_size, char *path_out, size_t path_size,
                       unsigned *tag_out);
unsigned netcheck_job_tag(void);
// Live stage text of the running job ("", when idle).
void netcheck_job_stage_text(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif  // BMX_UPDATE_NETCHECK_H
