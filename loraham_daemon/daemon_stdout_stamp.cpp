#include "daemon_stdout_stamp.h"

#include <mutex>
#include <string>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* See the header for why this is a stream wrapper and not a stamped printf. */

namespace {

/* The real streams, captured before they are replaced. */
FILE *g_real_stdout = NULL;
FILE *g_real_stderr = NULL;

/* Serialises emission so a completed line is never split by another thread. */
std::mutex g_emit_mutex;

/* The per-thread, per-stream partial-line buffers are declared further down,
 * with the writer they belong to. */

void stamp_now(char *out, size_t out_len)
{
    struct timespec ts;
    struct tm tm;
    char base[32];

    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm);
    snprintf(out, out_len, "%s.%03dZ", base, (int)(ts.tv_nsec / 1000000L));
}

void emit_line(FILE *sink, const char *line, size_t len)
{
    char stamp[40];

    stamp_now(stamp, sizeof(stamp));

    /* One mutex for both streams: they usually share a destination, and a
     * completed line must not be split by a write on the other one. */
    std::lock_guard<std::mutex> lock(g_emit_mutex);
    fprintf(sink, "%s %.*s\n", stamp, (int)len, line);
    fflush(sink);
}

/* One partial line per (thread, stream). Two buffers rather than one, or a
 * half-written stdout line would be completed by a stderr write. */
struct Pending {
    std::string out;
    std::string err;
};
thread_local Pending *tl_buffers = NULL;

ssize_t stamp_write_to(FILE *sink, std::string &pending,
                       const char *buf, size_t size)
{
    if (!sink)
        return (ssize_t)size;

    std::string *tl_pending = &pending;
    tl_pending->append(buf, size);

    size_t start = 0;
    for (;;) {
        const size_t nl = tl_pending->find('\n', start);
        if (nl == std::string::npos)
            break;
        emit_line(sink, tl_pending->data() + start, nl - start);
        start = nl + 1;
    }

    if (start > 0)
        tl_pending->erase(0, start);

    /* A line that grows without ever ending would otherwise grow the buffer
     * without bound. 8 KiB is far past any line this daemon writes; flush it
     * rather than keep accumulating. */
    if (tl_pending->size() > 8192) {
        emit_line(sink, tl_pending->data(), tl_pending->size());
        tl_pending->clear();
    }

    return (ssize_t)size;
}

Pending *buffers()
{
    if (!tl_buffers)
        tl_buffers = new Pending();
    return tl_buffers;
}

ssize_t stamp_write_out(void *, const char *buf, size_t size)
{
    return stamp_write_to(g_real_stdout, buffers()->out, buf, size);
}

ssize_t stamp_write_err(void *, const char *buf, size_t size)
{
    return stamp_write_to(g_real_stderr, buffers()->err, buf, size);
}

FILE *wrap(FILE **slot, FILE **real, ssize_t (*writer)(void *, const char *, size_t))
{
    const int fd = dup(fileno(*slot));
    if (fd < 0)
        return NULL;

    FILE *kept = fdopen(fd, "w");
    if (!kept) {
        close(fd);
        return NULL;
    }

    cookie_io_functions_t fns;
    memset(&fns, 0, sizeof(fns));
    fns.write = writer;

    FILE *wrapper = fopencookie(NULL, "w", fns);
    if (!wrapper) {
        fclose(kept);
        return NULL;
    }

    /* Unbuffered: the wrapper does its own line assembly, and a second layer
     * of buffering would only delay lines and reorder the two streams. */
    setvbuf(wrapper, NULL, _IONBF, 0);

    fflush(*slot);
    *real = kept;
    *slot = wrapper;
    return wrapper;
}

}  /* namespace */

void daemon_stdout_stamp_install(void)
{
    if (g_real_stdout)
        return;   /* already installed */

    wrap(&stdout, &g_real_stdout, stamp_write_out);
    wrap(&stderr, &g_real_stderr, stamp_write_err);
}

void daemon_stdout_stamp_flush_partial(void)
{
    if (!tl_buffers)
        return;

    if (g_real_stdout && !tl_buffers->out.empty()) {
        emit_line(g_real_stdout, tl_buffers->out.data(), tl_buffers->out.size());
        tl_buffers->out.clear();
    }

    if (g_real_stderr && !tl_buffers->err.empty()) {
        emit_line(g_real_stderr, tl_buffers->err.data(), tl_buffers->err.size());
        tl_buffers->err.clear();
    }
}
