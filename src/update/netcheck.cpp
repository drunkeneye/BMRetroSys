// Generic network machinery (see netcheck.h): tracing, bounded DNS,
// staged connectivity testing, and the core-0 worker. No Atarimania
// knowledge here; feature code registers a job runner.

#include "update/netcheck.h"

#include <circle/net/dnsclient.h>
#include <circle/net/in.h>
#include <circle/net/ntpclient.h>
#include <circle/net/socket.h>
#include <circle/string.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "network/network_service.h"
#include "update/circle_secure_stream.h"

// Optional host log sink: weak no-op default so kernels without a log
// file (all VICE variants) link; a kernel with a log file overrides with
// a strong definition that forwards (atari800_debug.cpp, dos_debug.cpp).
extern "C" {
__attribute__((weak)) void netcheck_host_trace(const char *line) {
  (void)line;
}
}

namespace bmx {
namespace netcheck {

// Numeric IP of the Atarimania host for direct (DNS-free) connections.
// Fragile by nature (hosting moves break it); kept in one place here and
// in the Atarimania module's fallback constant.
const char kAtarimaniaIp[] = "212.83.163.6";

namespace {

uint32_t BuildEpoch(void) {
    // __DATE__ is "Mmm dd yyyy".
    static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month[4] = {0};
    int day = 0, year = 0;
    if (sscanf(__DATE__, "%3s %d %d", month, &day, &year) != 3) {
        return 1767225600U;  // 2026-01-01 fallback
    }
    const char *found = strstr(kMonths, month);
    int mon = (found != 0) ? (int)(found - kMonths) / 3 + 1 : 1;
    // Howard Hinnant's days_from_civil.
    year -= mon <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy =
        (153U * (mon + (mon > 2 ? -3 : 9)) + 2U) / 5U + (unsigned)day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    return days > 0 ? (uint32_t)(days * 86400LL) : 1767225600U;
}

}  // namespace

// Live stage text for menu buttons; defined with the job slot below.
void SetJobStage(const char *stage);

void Trace(const char *fmt, ...) {
    char body[176];
    char line[224];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    unsigned long us = CTimer::GetClockTicks();
    snprintf(line, sizeof(line), "%10lu us %s", us, body);
    printf("networktest: %s\n", line);
    if (netcheck_host_trace != 0) {
        netcheck_host_trace(line);
    }
}

// Establishes a plausible wall clock (build epoch) so TLS certificate
// time checks pass. NTP needs DNS, which is out of scope for now, so the
// build date is the anchor (revisit together with DNS). Returns the epoch
// to trust.
uint64_t EstablishClock(CNetSubSystem *net) {
    const uint32_t build = BuildEpoch();
    const unsigned now =
        CTimer::Get() != 0 ? CTimer::Get()->GetUniversalTime() : 0U;
    (void)net;
    Trace("clock now=%u build=%u", now, build);
    if (CTimer::Get() != 0 && now >= build) {
        Trace("clock already established");
        return now;
    }
    if (CTimer::Get() != 0) {
        char err[64];
        Trace("clock fallback to build epoch begin");
        if (update::EnsureCertificateClockAtLeast(build, err, sizeof(err))) {
            Trace("clock fallback ok");
            return CTimer::Get()->GetUniversalTime();
        }
        Trace("clock fallback fail: %s", err);
    }
    return build;
}


// TCP connect to a numeric IPv4 literal (no DNS involved). Used for
// route checks and IP fallbacks.
bool TcpConnectNumeric(CNetSubSystem *net, const char *numeric_ip,
                       uint16_t port) {
    if (net == 0 || numeric_ip == 0 || port == 0) return false;
    unsigned b[4];
    int end = 0;
    if (sscanf(numeric_ip, "%u.%u.%u.%u%n", &b[0], &b[1], &b[2], &b[3],
               &end) != 4 ||
        end <= 0 || numeric_ip[end] != '\0' || b[0] > 255 || b[1] > 255 ||
        b[2] > 255 || b[3] > 255) {
        return false;
    }
    const uint8_t bytes[4] = {(uint8_t)b[0], (uint8_t)b[1], (uint8_t)b[2],
                              (uint8_t)b[3]};
    CIPAddress ip(bytes);
    if (ip.IsNull()) return false;
    CSocket sock(net, IPPROTO_TCP);
    return sock.Connect(ip, port) == 0;
}

// Diagnostic preamble for the connectivity test: logs the live stack
// addresses, proves the resolver object itself with a traffic-free
// dotted-quad lookup, lets the stack settle.
void NetDiagnostics(CNetSubSystem *net) {
    if (net == 0 || net->GetConfig() == 0) {
        Trace("netdiag no config");
        return;
    }
    const CNetConfig *cfg = net->GetConfig();
    CString ip, gw, dns;
    char mask[16] = {0};
    if (cfg->GetIPAddress() != 0) cfg->GetIPAddress()->Format(&ip);
    if (cfg->GetDefaultGateway() != 0) {
        cfg->GetDefaultGateway()->Format(&gw);
    }
    if (cfg->GetNetMask() != 0) {
        const uint8_t *m = cfg->GetNetMask();
        snprintf(mask, sizeof(mask), "%u.%u.%u.%u", m[0], m[1], m[2], m[3]);
    }
    if (cfg->GetDNSServer() != 0) cfg->GetDNSServer()->Format(&dns);
    Trace("netcfg ip=%s gw=%s mask=%s dns=%s", (const char *)ip,
          (const char *)gw, mask, (const char *)dns);
    // Stock resolver object sanity: dotted-quads resolve locally.
    {
        CDNSClient stock(net);
        CIPAddress literal;
        Trace("netdiag quad begin");
        bool ok = stock.Resolve("8.8.8.8", &literal);
        Trace("netdiag quad %s", ok ? "ok" : "FAIL");
    }
    // Let post-association settling complete before the first query.
    CTimer::SimpleusDelay(750000UL);
    Trace("netdiag settled");
}

// WLAN driver counters endpoint: call at test exits (baseline taken at
// entry). Deltas show device-level stalls vs silent network.
void TraceWlanEnd(bool ok) {
    if (!ok) return;
    bmx_wlan_flow_status_t cur;
    if (!bmx::ReadWlanFlowStatus(&cur)) return;
    Trace("wlan end tx=%llu rx=%llu wstall=%llu wstallms=%llu fstall=%llu",
          cur.tx_frames, cur.rx_data_frames, cur.tx_window_stalls,
          cur.tx_window_stall_ms, cur.tx_flow_stalls);
}
int TestConnection(char *msg, size_t msg_size) {
    Trace("test enter");
    CNetSubSystem *net = GetActiveNetworkSubsystem();
    if (net == 0) {
        snprintf(msg, msg_size,
                 "Network is not connected (see Network settings)");
        Trace("test exit rc=0 (stack down)");
        return 0;
    }
    // WLAN driver counters baseline: deltas below show whether frames
    // stall in the device (tx grows, rx flat, stalls > 0) or never
    // return from the network.
    bmx_wlan_flow_status_t wlan0;
    memset(&wlan0, 0, sizeof(wlan0));
    bool wlan_ok = bmx::ReadWlanFlowStatus(&wlan0);
    if (wlan_ok) {
        Trace("wlan base tx=%llu rx=%llu wstall=%llu wstallms=%llu fstall=%llu",
              wlan0.tx_frames, wlan0.rx_data_frames, wlan0.tx_window_stalls,
              wlan0.tx_window_stall_ms, wlan0.tx_flow_stalls);
    }
    // Stage 1: raw IP route, no DNS involved (Google Public DNS answers
    // TCP/53). Proves the device has a working internet route at all.
    NetDiagnostics(net);
    SetJobStage("route 8.8.8.8");
    Trace("test stage1 tcp 8.8.8.8:53 begin");
    {
        const uint8_t google[4] = {8, 8, 8, 8};
        CIPAddress dip(google);
        CSocket sock(net, IPPROTO_TCP);
        if (sock.Connect(dip, 53) != 0) {
            Trace("test stage1 fail");
            snprintf(msg, msg_size,
                     "No internet route (cannot reach 8.8.8.8:53)");
            TraceWlanEnd(wlan_ok);
            Trace("test exit rc=0");
            return 0;
        }
    }
    Trace("test stage1 ok");
    // Stage 2: direct TCP to Atarimania's numeric IP (no DNS anywhere).
    SetJobStage("atarimania:443");
    Trace("test stage2 tcp begin");
    if (!TcpConnectNumeric(net, kAtarimaniaIp, 443)) {
        Trace("test stage2 fail");
        snprintf(msg, msg_size, "Cannot reach Atarimania (%s:443)",
                 kAtarimaniaIp);
        TraceWlanEnd(wlan_ok);
        Trace("test exit rc=0");
        return 0;
    }
    Trace("test stage2 ok");
    TraceWlanEnd(wlan_ok);
    SetJobStage("done");
    snprintf(msg, msg_size, "Internet OK (route + Atarimania:443)");
    Trace("test exit rc=1");
    return 1;
}

}  // namespace netcheck
}  // namespace bmx

// ---- Async jobs: protocol + core-0 worker (feature-agnostic) ----
namespace bmx {
namespace netcheck {

namespace {

NetcheckRunner g_runner = 0;

NetcheckJob &Slot(void) {
    static NetcheckJob slot;
    return slot;
}

void SetJobStageInternal(const char *stage) {
    snprintf(Slot().stage, sizeof(Slot().stage), "%s",
             stage != 0 ? stage : "");
}

class NetWorkerTask : public CTask {
 public:
    NetWorkerTask() : CTask() { SetName("netck"); }
    void Run(void) override {
        Trace("worker online");
        while (1) {
            if (Slot().state == 1) {
                if (Slot().kind == NETCHECK_JOB_TEST) {
                    // Implemented here (generic); SEARCH/DETAIL go to the
                    // registered feature runner below.
                    char msg[160];
                    Trace("job test run");
                    int rc = TestConnection(msg, sizeof(msg));
                    if (Slot().cancel != 0 && rc != 0) {
                        snprintf(msg, sizeof(msg), "Canceled");
                        rc = -1;
                    }
                    Slot().result = rc;
                    snprintf(Slot().msg, sizeof(Slot().msg), "%s", msg);
                    Slot().state = 2;
                } else if (g_runner != 0) {
                    g_runner(&Slot());
                    Slot().state = 2;
                }
            }
            CScheduler::Get()->MsSleep(50);
        }
    }
};

}  // namespace

void SetJobStage(const char *stage) { SetJobStageInternal(stage); }

}  // namespace netcheck
}  // namespace bmx

extern "C" void netcheck_worker_init(bmx::netcheck::NetcheckRunner runner) {
    // Core-0 boot context only (task creation panics scheduler-foreign).
    bmx::netcheck::g_runner = runner;
    new bmx::netcheck::NetWorkerTask;
}

namespace bmx {
namespace netcheck {

namespace {

void StartCommon(int kind) {
    NetcheckJob &job = Slot();
    job.kind = kind;
    job.cancel = 0;
    job.result = 0;
    job.msg[0] = '\0';
    job.stage[0] = '\0';
    static unsigned seq = 0;
    job.tag = ++seq;
    job.state = 1;
}

}  // namespace
}  // namespace netcheck
}  // namespace bmx

extern "C" int netcheck_job_start_test(void) {
    bmx::netcheck::NetcheckJob &job = bmx::netcheck::Slot();
    if (job.state == 1) return 0;
    bmx::netcheck::StartCommon(bmx::netcheck::NETCHECK_JOB_TEST);
    return 1;
}

extern "C" int netcheck_job_start_search(const char *query, int collection,
                                          int page) {
    bmx::netcheck::NetcheckJob &job = bmx::netcheck::Slot();
    if (job.state == 1) return 0;
    snprintf(job.query, sizeof(job.query), "%s", query != 0 ? query : "");
    job.collection = collection;
    job.page = page;
    bmx::netcheck::StartCommon(bmx::netcheck::NETCHECK_JOB_SEARCH);
    return 1;
}

extern "C" int netcheck_job_start_detail(const char *url) {
    bmx::netcheck::NetcheckJob &job = bmx::netcheck::Slot();
    if (job.state == 1) return 0;
    snprintf(job.url, sizeof(job.url), "%s", url != 0 ? url : "");
    job.path[0] = '\0';
    bmx::netcheck::StartCommon(bmx::netcheck::NETCHECK_JOB_DETAIL);
    return 1;
}

extern "C" int netcheck_job_busy(void) {
    return bmx::netcheck::Slot().state == 1;
}

extern "C" void netcheck_job_cancel(void) {
    bmx::netcheck::Slot().cancel = 1;
}

extern "C" int netcheck_job_poll(int *kind_out, int *result_out,
                                  char *msg_out, size_t msg_size,
                                  char *path_out, size_t path_size,
                                  unsigned *tag_out) {
    bmx::netcheck::NetcheckJob &job = bmx::netcheck::Slot();
    if (job.state != 2) return 0;
    if (kind_out != 0) *kind_out = job.kind;
    if (result_out != 0) *result_out = job.result;
    if (msg_out != 0 && msg_size != 0) {
        snprintf(msg_out, msg_size, "%s", job.msg);
    }
    if (path_out != 0 && path_size != 0) {
        snprintf(path_out, path_size, "%s", job.path);
    }
    if (tag_out != 0) *tag_out = job.tag;
    job.state = 0;
    job.kind = bmx::netcheck::NETCHECK_JOB_NONE;
    return 1;
}

extern "C" unsigned netcheck_job_tag(void) {
    return bmx::netcheck::Slot().tag;
}

extern "C" void netcheck_job_stage_text(char *out, size_t out_size) {
    if (out == 0 || out_size == 0) return;
    bmx::netcheck::NetcheckJob &job = bmx::netcheck::Slot();
    if (job.state != 1) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%s", job.stage);
}
