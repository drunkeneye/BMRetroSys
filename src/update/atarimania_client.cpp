// Atarimania online client: search/list/detail parsing plus dump download.
// Shared code (all kernels); the menu section is Atari-gated in menu.c.
//
// Network path reuses the updater's verified TLS stack (pinned ISRG Root X1
// covers atarimania.com's Let's Encrypt chain). The Pi has no RTC, so the
// wall clock is established via NTP before the first request; without real
// time the 90-day Let's Encrypt certificates cannot be validated.

#include "update/atarimania_client.h"

#include <circle/timer.h>
#include <ff.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "network/network_service.h"
#include "update/body_sinks.h"
#include "update/circle_secure_stream.h"
#include "update/https_stream.h"
#include "update/netcheck.h"
#include "update/url_policy.h"

using bmx::netcheck::EstablishClock;
using bmx::netcheck::TcpConnectNumeric;
using bmx::netcheck::Trace;

namespace bmx {
namespace atarimania {
namespace {

using namespace ::bmx::update;

static const char kHost[] = "www.atarimania.com";
static const char kMachinePath[] = "/machines/atari-400-800-xl-xe";
static const char kDumpDir[] = "SD:/dumps/atarimania";
// Last-resort transport when DNS is unusable: the TCP destination becomes
// this IP while SNI/Host/cert checks keep using the hostname. Fragile by
// nature (hosting moves break it); DNS stays the primary path.
static const char kFallbackIp[] = "212.83.163.6";
static const size_t kPageBufferBytes = 384U * 1024U;
static const uint64_t kMaxDumpBytes = 64ULL * 1024ULL * 1024ULL;

struct ClientState {
    HttpsGetWorkspace workspace;
    uint8_t page[kPageBufferBytes];
    char error[256];
    // Last search results.
    int count;
    int page_current;
    int page_total;
    char last_query[80];
    int last_collection;
    char title[ATARIMANIA_MAX_RESULTS][128];
    char year[ATARIMANIA_MAX_RESULTS][8];
    char url[ATARIMANIA_MAX_RESULTS][256];
};

ClientState &State(void) {
    static ClientState state;
    return state;
}

void SetError(const char *msg) {
    snprintf(State().error, sizeof(State().error), "%s", msg ? msg : "?");
}

const char *StatusText(HttpsGetStatus status) {
    return HttpsGetStatusString(status);
}

// Streaming file sink for dump downloads (size/hash unknown upfront,
// unlike updater assets, so FatFsDownloadSink does not fit).
class FileBodySink : public update::HttpBodySink {
 public:
    FileBodySink() : file_(0), ok_(false), bytes_(0) {}
    bool Start(const char *path) {
        file_ = new FIL;
        if (file_ == 0) return false;
        if (f_open(file_, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
            delete file_;
            file_ = 0;
            return false;
        }
        ok_ = true;
        return true;
    }
    bool Write(const uint8_t *data, size_t size) override {
        if (!ok_ || file_ == 0) return false;
        if (bytes_ + size > kMaxDumpBytes) return false;
        const uint8_t *p = data;
        size_t left = size;
        while (left != 0U) {
            UINT chunk = left > 32768U ? 32768U : (UINT)left;
            UINT written = 0U;
            if (f_write(file_, p, chunk, &written) != FR_OK ||
                written != chunk) {
                ok_ = false;
                return false;
            }
            p += chunk;
            left -= chunk;
        }
        bytes_ += size;
        return true;
    }
    bool Finish(const char *final_path, const char *part_path) {
        bool ok = ok_;
        if (file_ != 0) {
            f_sync(file_);
            ok = f_close(file_) == FR_OK && ok;
            delete file_;
            file_ = 0;
        }
        if (!ok || bytes_ == 0U) {
            f_unlink(part_path);
            return false;
        }
        f_unlink(final_path);
        return f_rename(part_path, final_path) == FR_OK;
    }
    void Abort(const char *part_path) {
        if (file_ != 0) {
            f_close(file_);
            delete file_;
            file_ = 0;
        }
        f_unlink(part_path);
    }
    uint64_t bytes() const { return bytes_; }

 private:
    FIL *file_;
    bool ok_;
    uint64_t bytes_;
};

struct FetchCtx {
    CNetSubSystem *net;
    update::CircleSecureStreamFactory *factory;
};

// GET url into the shared page buffer. Returns body bytes, or 0 on error.
// On connection failure the request is retried once with the numeric IP
// fallback (SNI/Host/cert still use the hostname): direct connection,
// no DNS involved.
size_t FetchPage(const char *url, const char *accept, FetchCtx *ctx) {
    ClientState &st = State();
    Trace("fetch begin %.120s", url);
    update::BoundedMemoryBodySink sink(st.page, sizeof(st.page));
    update::HttpsRequestOptions options;
    options.accept = accept;
    update::HttpsGetResult result = update::HttpsRequest(
        url, update::UpdateUrlPurpose::Atarimania,
        update::HttpsRequestKind::ReleaseZip, options,
        sizeof(st.page), &sink, ctx->factory, &st.workspace, 0);
    if (result.status == update::HttpsGetStatus::ConnectFailed) {
        Trace("fetch ip fallback");
        bmx::netcheck::SetJobStage("ip fallback");
        update::SetVerifiedConnectionOverride(kHost, kFallbackIp);
        sink.Reset();
        result = update::HttpsRequest(
            url, update::UpdateUrlPurpose::Atarimania,
            update::HttpsRequestKind::ReleaseZip, options,
            sizeof(st.page), &sink, ctx->factory, &st.workspace, 0);
        update::ClearVerifiedConnectionOverride();
        Trace("fetch fallback end status=%d http=%u bytes=%llu",
              (int)result.status, result.http_status,
              (unsigned long long)sink.bytes().size);
    }
    Trace("fetch end status=%d http=%u bytes=%llu", (int)result.status,
          result.http_status,
          (unsigned long long)sink.bytes().size);
    if (result.status != update::HttpsGetStatus::Ok) {
        char msg[160];
        snprintf(msg, sizeof(msg), "Download failed: %s",
                 StatusText(result.status));
        SetError(msg);
        return 0;
    }
    if (result.http_status != 200U) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Server returned HTTP %u",
                 result.http_status);
        SetError(msg);
        return 0;
    }
    if (sink.overflowed()) {
        SetError("Response too large");
        return 0;
    }
    return (size_t)sink.bytes().size;
}

bool BeginFetch(FetchCtx *ctx) {
    ctx->net = 0;
    // The stack can report down transiently right after boot/menu open;
    // retry briefly before giving up.
    for (int i = 0; i < 3 && ctx->net == 0; i++) {
        if (i > 0) {
            Trace("begin net=down, retry %d", i);
            CTimer::SimpleusDelay(500000UL);
        }
        ctx->net = GetActiveNetworkSubsystem();
    }
    Trace("begin net=%s", ctx->net != 0 ? "up" : "down");
    if (ctx->net == 0) {
        SetError("Network is not connected (see Network settings)");
        return false;
    }
    const uint64_t epoch = EstablishClock(ctx->net);
    Trace("begin factory epoch=%llu", (unsigned long long)epoch);
    ctx->factory =
        new update::CircleSecureStreamFactory(ctx->net, epoch);
    if (ctx->factory == 0) {
        SetError("Out of memory");
        return false;
    }
    return true;
}

void EndFetch(FetchCtx *ctx) {
    delete ctx->factory;
    ctx->factory = 0;
}

// Percent-encodes a search term (space becomes '+').
void UrlEncodeQuery(const char *in, char *out, size_t out_size) {
    static const char kHex[] = "0123456789ABCDEF";
    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p != '\0';
         p++) {
        if (pos + 4 >= out_size) break;
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' ||
            *p == '~') {
            out[pos++] = (char)*p;
        } else if (*p == ' ') {
            out[pos++] = '+';
        } else {
            out[pos++] = '%';
            out[pos++] = kHex[*p >> 4];
            out[pos++] = kHex[*p & 15];
        }
    }
    out[pos] = '\0';
}

void DecodeEntities(const char *in, char *out, size_t out_size) {
    size_t pos = 0;
    for (const char *p = in; *p != '\0' && pos + 1 < out_size;) {
        char c = *p;
        size_t adv = 1;
        if (*p == '&') {
            if (strncmp(p, "&amp;", 5) == 0) { c = '&'; adv = 5; }
            else if (strncmp(p, "&lt;", 4) == 0) { c = '<'; adv = 4; }
            else if (strncmp(p, "&gt;", 4) == 0) { c = '>'; adv = 4; }
            else if (strncmp(p, "&quot;", 6) == 0) { c = '"'; adv = 6; }
            else if (strncmp(p, "&#39;", 5) == 0 ||
                     strncmp(p, "&#x27;", 6) == 0) {
                c = '\'';
                adv = p[2] == '#' && p[3] == 'x' ? 6 : 5;
            }
        }
        out[pos++] = c;
        p += adv;
    }
    out[pos] = '\0';
}

void CopyField(char *dst, size_t dst_size, const char *src, size_t len) {
    if (len >= dst_size) len = dst_size - 1;
    char tmp[256];
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    memcpy(tmp, src, len);
    tmp[len] = '\0';
    DecodeEntities(tmp, dst, dst_size);
}

// Parses one <li ...> result chunk. Returns false when no usable entry.
bool ParseItem(const char *chunk, const char *chunk_end, ClientState &st,
               int index) {
    // Detail URL: first href="/games/...
    const char *a = strstr(chunk, "href=\"/games/");
    if (a == 0 || a >= chunk_end) return false;
    a += 6;
    const char *q = strchr(a, '"');
    if (q == 0 || q >= chunk_end || (size_t)(q - a) < 8) return false;
    snprintf(st.url[index], sizeof(st.url[index]), "https://%s%.*s", kHost,
             (int)(q - a), a);
    // Title: title="..." on the text-[16px] anchor.
    const char *t = strstr(chunk, "text-[16px]");
    const char *title = 0;
    size_t title_len = 0;
    if (t != 0 && t < chunk_end) {
        // Scan backwards for title=" within this anchor.
        const char *back = t;
        while (back > chunk && back > t - 400 && *back != '<') back--;
        const char *ta = strstr(back, "title=\"");
        if (ta != 0 && ta < t) {
            ta += 7;
            const char *te = strchr(ta, '"');
            if (te != 0 && te < chunk_end) {
                title = ta;
                title_len = (size_t)(te - ta);
            }
        }
        if (title == 0) {
            // Fallback: anchor inner text.
            const char *gt = strchr(t, '>');
            const char *lt = gt != 0 ? strstr(gt, "</a>") : 0;
            if (gt != 0 && lt != 0 && lt < chunk_end && lt > gt + 1) {
                title = gt + 1;
                title_len = (size_t)(lt - title);
            }
        }
    }
    if (title == 0 || title_len == 0) return false;
    CopyField(st.title[index], sizeof(st.title[index]), title, title_len);
    if (st.title[index][0] == '\0') return false;
    // Year: link text of the year= anchor ("?" when unknown). The '&' in
    // hrefs arrives HTML-escaped, so match both forms.
    st.year[index][0] = '\0';
    const char *y = strstr(chunk, "&amp;year=");
    if (y == 0 || y >= chunk_end) y = strstr(chunk, "&year=");
    if (y == 0 || y >= chunk_end) y = strstr(chunk, "?year=");
    if (y != 0 && y < chunk_end) {
        const char *gt = strstr(y, "\">");
        const char *lt = gt != 0 ? strchr(gt + 2, '<') : 0;
        if (gt != 0 && lt != 0 && lt < chunk_end && lt > gt + 2 &&
            (size_t)(lt - (gt + 2)) < sizeof(st.year[index])) {
            if (!(lt == gt + 3 && gt[2] == '?')) {
                CopyField(st.year[index], sizeof(st.year[index]), gt + 2,
                          (size_t)(lt - (gt + 2)));
            }
        }
    }
    return true;
}

int ParseList(const char *body, size_t len) {
    ClientState &st = State();
    st.count = 0;
    static const char kItem[] = "<li class=\"flex items-start gap-3";
    const char *body_end = body + len;
    const char *p = body;
    while (st.count < ATARIMANIA_MAX_RESULTS) {
        p = strstr(p, kItem);
        if (p == 0 || p >= body_end) break;
        const char *next = strstr(p + 1, kItem);
        const char *chunk_end = next != 0 ? next : body_end;
        if (ParseItem(p, chunk_end, st, st.count)) st.count++;
        p++;
    }
    // Total pages: highest &page=N in the pagination nav (site default
    // is 25 hits per page, so pages beyond hold more of the same query).
    int total = 1;
    static const char kPageNeedle[] = "&page=";
    // hrefs arrive HTML-escaped (&amp;page=).
    for (int pass = 0; pass < 2; pass++) {
        const char *q = body;
        const char *needle =
            pass == 0 ? "&amp;page=" : kPageNeedle;
        while ((q = strstr(q, needle)) != 0 && q < body_end) {
            q += strlen(needle);
            int n = 0;
            while (*q >= '0' && *q <= '9') n = n * 10 + (*q++ - '0');
            if (n > total) total = n;
        }
    }
    if (total > ATARIMANIA_MAX_PAGES) total = ATARIMANIA_MAX_PAGES;
    st.page_total = total;
    return st.count;
}

// Finds the dump href after the "Dump" spec row. Returns false if none.
bool ParseDump(const char *body, size_t len, char *url_out,
               size_t url_size) {
    const char *body_end = body + len;
    const char *dt = strstr(body, ">Dump<");
    if (dt == 0 || dt >= body_end) return false;
    const char *bound = strstr(dt, "<dt");
    if (bound == 0 || bound > body_end) bound = body_end;
    const char *h = strstr(dt, "/8bit/dumps/");
    if (h == 0 || h >= bound) return false;
    // Back up to href=".
    const char *back = h;
    while (back > dt && strncmp(back, "href=\"", 6) != 0) back--;
    if (strncmp(back, "href=\"", 6) != 0) return false;
    back += 6;
    const char *q = strchr(back, '"');
    if (q == 0 || q > bound) return false;
    size_t ulen = (size_t)(q - back);
    if (back[0] == 'h') {
        if (ulen + 1 > url_size) return false;
        memcpy(url_out, back, ulen);
        url_out[ulen] = '\0';
    } else {
        if (7 + ulen + 1 > url_size) return false;
        snprintf(url_out, url_size, "https://%s%.*s", kHost, (int)ulen,
                 back);
    }
    return true;
}

void MkdirChain(const char *path) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            f_mkdir(tmp);
            *p = '/';
        }
    }
    f_mkdir(tmp);
}

bool PathExists(const char *path) {
    FILINFO info;
    return f_stat(path, &info) == FR_OK && !(info.fattrib & AM_DIR);
}

void BaseName(const char *url, char *out, size_t out_size) {
    const char *base = strrchr(url, '/');
    base = base != 0 ? base + 1 : url;
    const char *q = strchr(base, '?');
    size_t len = q != 0 ? (size_t)(q - base) : strlen(base);
    if (len + 1 > out_size) len = out_size - 1;
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 1 < out_size; i++) {
        char c = base[i];
        out[pos++] =
            (isalnum((unsigned char)c) || c == '-' || c == '_' ||
             c == '.')
                ? c
                : '_';
    }
    out[pos] = '\0';
    if (pos == 0) snprintf(out, out_size, "dump.zip");
}

static bool PreflightDirect(FetchCtx *ctx, bool *armed_out) {
    *armed_out = false;
    if (!bmx::netcheck::TcpConnectNumeric(ctx->net, kFallbackIp, 443)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "Cannot reach %s (%s:443)", kHost,
                 kFallbackIp);
        SetError(msg);
        EndFetch(ctx);
        return false;
    }
    Trace("preflight ip fallback armed");
    bmx::netcheck::SetJobStage("ip fallback");
    update::SetVerifiedConnectionOverride(kHost, kFallbackIp);
    *armed_out = true;
    return true;
}

}  // namespace
}  // namespace atarimania

// ---- extern "C" API (menu.c) ----

// Direct preflight (no DNS anywhere): the fallback IP must answer TCP,
// then the transport override is armed for the fetch. Returns false with
// the error set and the context closed when there is no route.

static int FetchListPage(const char *query, int collection, int page) {
    using namespace bmx::atarimania;
    ClientState &st = State();
    st.count = 0;
    if (query == 0 || query[0] == '\0') {
        SetError("Type a search term first");
        return -1;
    }
    if (page < 1) page = 1;
    if (page > ATARIMANIA_MAX_PAGES) page = ATARIMANIA_MAX_PAGES;
    FetchCtx ctx;
    if (!BeginFetch(&ctx)) return -1;
    // Direct preflight (no DNS anywhere); arms the transport override.
    bool ip_armed = false;
    if (!PreflightDirect(&ctx, &ip_armed)) return -1;
    char encoded[256];
    UrlEncodeQuery(query, encoded, sizeof(encoded));
    char url[512];
    const char *type =
        collection == ATARIMANIA_DEMOS ? "&type=d"
        : collection == ATARIMANIA_UTILS ? "&type=u"
                                         : "";
    if (page <= 1) {
        snprintf(url, sizeof(url), "https://%s%s?q=%s%s&per=%d", kHost,
                 kMachinePath, encoded, type, ATARIMANIA_MAX_RESULTS);
    } else {
        snprintf(url, sizeof(url), "https://%s%s?q=%s%s&per=%d&page=%d",
                 kHost, kMachinePath, encoded, type,
                 ATARIMANIA_MAX_RESULTS, page);
    }
    const size_t body = FetchPage(url, "text/html", &ctx);
    EndFetch(&ctx);
    if (ip_armed) update::ClearVerifiedConnectionOverride();
    if (body == 0) return -1;
    st.page[body < sizeof(st.page) ? body : sizeof(st.page) - 1] = '\0';
    st.page_current = page;
    snprintf(st.last_query, sizeof(st.last_query), "%s", query);
    st.last_collection = collection;
    const int count = ParseList((const char *)st.page, body);
    if (count == 0) {
        SetError("No productions found");
        return 0;
    }
    if (st.page_current > st.page_total) {
        st.page_total = st.page_current;
    }
    return count;
}

extern "C" int atarimania_page_current(void) {
    return bmx::atarimania::State().page_current;
}

extern "C" int atarimania_page_total(void) {
    return bmx::atarimania::State().page_total;
}

extern "C" int atarimania_result_count(void) {
    return bmx::atarimania::State().count;
}

extern "C" const char *atarimania_result_title(int index) {
    bmx::atarimania::ClientState &st = bmx::atarimania::State();
    if (index < 0 || index >= st.count) return "";
    return st.title[index];
}

extern "C" const char *atarimania_result_year(int index) {
    bmx::atarimania::ClientState &st = bmx::atarimania::State();
    if (index < 0 || index >= st.count) return "";
    return st.year[index];
}

extern "C" const char *atarimania_result_url(int index) {
    bmx::atarimania::ClientState &st = bmx::atarimania::State();
    if (index < 0 || index >= st.count) return "";
    return st.url[index];
}

// Dump download core (worker thread via the job runner below).
static int FetchDumpCore(const char *detail_url, char *out_path,
                         size_t out_path_size) {
    using namespace bmx::atarimania;
    if (detail_url == 0 || detail_url[0] == '\0') {
        SetError("No production selected");
        return -1;
    }
    FetchCtx ctx;
    if (!BeginFetch(&ctx)) return -1;
    // Direct preflight (no DNS anywhere); arms the transport override.
    bool ip_armed = false;
    if (!PreflightDirect(&ctx, &ip_armed)) return -1;
    int result = -1;
    const size_t body = FetchPage(detail_url, "text/html", &ctx);
    if (body == 0) {
        EndFetch(&ctx);
        if (ip_armed) update::ClearVerifiedConnectionOverride();
        return -1;
    }
    State().page[body < sizeof(State().page) ? body
                                             : sizeof(State().page) - 1] =
        '\0';
    char dump_url[512];
    if (!ParseDump((const char *)State().page, body, dump_url,
                   sizeof(dump_url))) {
        SetError("This production has no dump available");
        EndFetch(&ctx);
        if (ip_armed) update::ClearVerifiedConnectionOverride();
        return 0;
    }
    char base[96];
    BaseName(dump_url, base, sizeof(base));
    Trace("dump file=%s", base);
    char final_path[160];
    snprintf(final_path, sizeof(final_path), "%s/%s", kDumpDir, base);
    if (PathExists(final_path)) {
        Trace("dump cached");
        snprintf(out_path, out_path_size, "%s", final_path);
        EndFetch(&ctx);
        if (ip_armed) update::ClearVerifiedConnectionOverride();
        return 1;
    }
    Trace("dump download begin");
    MkdirChain(kDumpDir);
    char part_path[160];
    snprintf(part_path, sizeof(part_path), "%s.part", final_path);
    FileBodySink sink;
    if (!sink.Start(part_path)) {
        SetError("Cannot write to SD card");
        EndFetch(&ctx);
        if (ip_armed) update::ClearVerifiedConnectionOverride();
        return -1;
    }
    update::HttpsRequestOptions options;
    options.accept = "*/*";
    Trace("dump fetch begin");
    update::HttpsGetResult res = update::HttpsRequest(
        dump_url, update::UpdateUrlPurpose::Atarimania,
        update::HttpsRequestKind::ReleaseZip, options, kMaxDumpBytes,
        &sink, ctx.factory, &State().workspace, 0);
    if (res.status == update::HttpsGetStatus::ConnectFailed) {
        Trace("dump fetch ip fallback");
        bmx::netcheck::SetJobStage("ip fallback");
        update::SetVerifiedConnectionOverride(kHost, kFallbackIp);
        sink.Abort(part_path);
        if (!sink.Start(part_path)) {
            SetError("Cannot write to SD card");
            update::ClearVerifiedConnectionOverride();
            EndFetch(&ctx);
            return -1;
        }
        res = update::HttpsRequest(
            dump_url, update::UpdateUrlPurpose::Atarimania,
            update::HttpsRequestKind::ReleaseZip, options, kMaxDumpBytes,
            &sink, ctx.factory, &State().workspace, 0);
        update::ClearVerifiedConnectionOverride();
    }
    EndFetch(&ctx);
    // The override must never leak past this fetch (the updater shares
    // the factory); the internal fallback above already cleared its own
    // arming, this covers the pre-armed path.
    update::ClearVerifiedConnectionOverride();
    Trace("dump fetch end status=%d http=%u", (int)res.status,
          res.http_status);
    if (res.status != update::HttpsGetStatus::Ok ||
        res.http_status != 200U) {
        char msg[160];
        snprintf(msg, sizeof(msg), "Dump download failed: %s",
                 StatusText(res.status));
        SetError(msg);
        sink.Abort(part_path);
        return -1;
    }
    if (!sink.Finish(final_path, part_path)) {
        SetError("Cannot save dump to SD card");
        return -1;
    }
    snprintf(out_path, out_path_size, "%s", final_path);
    result = 1;
    return result;
}

extern "C" const char *atarimania_last_error(void) {
    return bmx::atarimania::State().error;
}
}  // extern "C"

// Job runner for the netcheck core-0 worker: implements SEARCH and DETAIL
// with the fetch bodies above (TEST is implemented by netcheck itself).
// Registered at boot; runs worker-side, never on the menu thread.
void atarimania_job_runner(bmx::netcheck::NetcheckJob *job) {
    using namespace bmx::atarimania;
    if (job == 0) return;
    if (job->kind == bmx::netcheck::NETCHECK_JOB_SEARCH) {
        Trace("job search run page=%d", job->page);
        bmx::netcheck::SetJobStage("search");
        int rc = bmx::FetchListPage(job->query, job->collection, job->page);
        if (job->cancel != 0 && rc > 0) {
            job->result = -1;
            snprintf(job->msg, sizeof(job->msg), "Canceled");
        } else {
            job->result = rc;
            snprintf(job->msg, sizeof(job->msg), "%s",
                     rc >= 0 ? "" : State().error);
        }
    } else if (job->kind == bmx::netcheck::NETCHECK_JOB_DETAIL) {
        Trace("job detail run");
        bmx::netcheck::SetJobStage("detail");
        char path[160] = {0};
        int rc = bmx::FetchDumpCore(job->url, path, sizeof(path));
        if (job->cancel != 0 && rc > 0) {
            rc = -1;
            snprintf(job->msg, sizeof(job->msg), "Canceled");
        } else if (rc < 0) {
            snprintf(job->msg, sizeof(job->msg), "%s", State().error);
        } else if (rc == 0) {
            snprintf(job->msg, sizeof(job->msg),
                     "No dump available for this production");
        }
        job->result = rc;
        snprintf(job->path, sizeof(job->path), "%s", path);
    }
    job->state = 2;
}

