#include "daemon_rflog.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* --- Boot slots ----------------------------------------------------------- */
static bool g_switch_on = false;
static char g_path[4096] = {0};
static size_t g_max_bytes = DAEMON_RFLOG_MAX_BYTES;

/* --- The one open file ---------------------------------------------------- */
static int g_fd = -1;

bool daemon_parse_rflog_switch(const char *arg, bool *out)
{
    if (!arg || !out)
        return false;
    if (strcasecmp(arg, "on") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(arg, "off") == 0) {
        *out = false;
        return true;
    }
    return false;
}

bool daemon_set_rflog_switch_global(const char *arg)
{
    bool on = false;
    if (!daemon_parse_rflog_switch(arg, &on))
        return false;
    g_switch_on = on;
    return true;
}

bool daemon_set_rflog_path_global(const char *arg)
{
    /* Absolute only: the controller names the file; this process never
     * resolves a relative path against a directory it did not choose. */
    if (!arg || arg[0] != '/' || strlen(arg) >= sizeof(g_path))
        return false;
    strcpy(g_path, arg);
    return true;
}

bool daemon_rflog_start(char *err, size_t err_len)
{
    if (err && err_len)
        err[0] = 0;
    if (!g_switch_on)
        return true;
    if (g_path[0] == 0) {
        if (err)
            snprintf(err, err_len, "--rflog on needs --rflog-path <absolute path>");
        return false;
    }
    /* O_RDWR, not O_WRONLY: the rollover reads this same descriptor to copy the
     * tail out. O_APPEND still lands every write at the current end. */
    int fd = open(g_path, O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (err)
            snprintf(err, err_len, "cannot open RF log %s: %s", g_path, strerror(errno));
        return false;
    }
    if (g_fd >= 0)
        close(g_fd);
    g_fd = fd;
    return true;
}

void daemon_rflog_stop(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
}

bool daemon_rflog_active(void)
{
    return g_fd >= 0;
}

void daemon_rflog_reset(void)
{
    daemon_rflog_stop();
    g_switch_on = false;
    g_path[0] = 0;
    g_max_bytes = DAEMON_RFLOG_MAX_BYTES;
}

void daemon_rflog_set_max_bytes(size_t max_bytes)
{
    g_max_bytes = max_bytes;
}

/* --- Formatting ----------------------------------------------------------- */
static void rflog_utc_now(char *out, size_t out_len)
{
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    char base[32];
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm);
    snprintf(out, out_len, "%s.%03dZ", base, (int)(ts.tv_nsec / 1000000L));
}

/* hex=<..> ascii="<..>" for the payload; bounded, never overruns `out`. */
static size_t rflog_append_payload(char *out, size_t out_len, size_t pos,
                                   const uint8_t *buf, size_t len)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    pos += (size_t)snprintf(out + pos, pos < out_len ? out_len - pos : 0, " hex=");
    for (i = 0; i < len && pos + 2 < out_len; i++) {
        out[pos++] = digits[buf[i] >> 4];
        out[pos++] = digits[buf[i] & 0x0f];
    }
    pos += (size_t)snprintf(out + pos, pos < out_len ? out_len - pos : 0, " ascii=\"");
    for (i = 0; i < len && pos + 1 < out_len; i++) {
        uint8_t c = buf[i];
        /* Printable ASCII only; the quote and the backslash would break the
         * line's own quoting, so they are dots like every other non-printable. */
        out[pos++] = (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') ? (char)c : '.';
    }
    pos += (size_t)snprintf(out + pos, pos < out_len ? out_len - pos : 0, "\"\n");
    if (pos >= out_len) {
        /* Truncated: still end the line, so the file stays one record per line. */
        out[out_len - 2] = '\n';
        out[out_len - 1] = 0;
        return out_len - 1;
    }
    out[pos] = 0;
    return pos;
}

size_t daemon_rflog_format_rx(char *out, size_t out_len, const char *utc,
                              const char *band_tag, int16_t rssi_cdbm, int16_t snr_cdb,
                              const uint8_t *buf, size_t len)
{
    if (!out || out_len < 8)
        return 0;
    size_t pos = (size_t)snprintf(out, out_len, "%s RX rssi=%.2f snr=%.2f len=%zu band=%s",
                                  utc ? utc : "-", rssi_cdbm / 100.0, snr_cdb / 100.0,
                                  len, band_tag ? band_tag : "-");
    if (pos >= out_len)
        pos = out_len - 1;
    return rflog_append_payload(out, out_len, pos, buf, len);
}

size_t daemon_rflog_format_tx(char *out, size_t out_len, const char *utc,
                              const char *band_tag, const char *outcome,
                              const uint8_t *buf, size_t len)
{
    if (!out || out_len < 8)
        return 0;
    size_t pos = (size_t)snprintf(out, out_len, "%s TX rssi=- snr=- len=%zu outcome=%s band=%s",
                                  utc ? utc : "-", len, outcome ? outcome : "-",
                                  band_tag ? band_tag : "-");
    if (pos >= out_len)
        pos = out_len - 1;
    return rflog_append_payload(out, out_len, pos, buf, len);
}

/* --- Retention: copy-truncate, same inode --------------------------------- */
static bool rflog_copy_to_previous(void)
{
    char prev[sizeof(g_path) + 2];
    snprintf(prev, sizeof(prev), "%s.1", g_path);
    int pfd = open(prev, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (pfd < 0)
        return false;
    uint8_t chunk[65536];
    off_t off = 0;
    bool ok = true;
    for (;;) {
        ssize_t n = pread(g_fd, chunk, sizeof(chunk), off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            ok = false;
            break;
        }
        if (n == 0)
            break;
        ssize_t done = 0;
        while (done < n) {
            ssize_t w = write(pfd, chunk + done, (size_t)(n - done));
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                ok = false;
                break;
            }
            done += w;
        }
        if (!ok)
            break;
        off += n;
    }
    close(pfd);
    return ok;
}

static void rflog_roll_if_needed(size_t incoming)
{
    struct stat st;
    if (fstat(g_fd, &st) != 0)
        return;
    if ((size_t)st.st_size + incoming <= g_max_bytes)
        return;
    /* The previous segment is replaced only when the copy succeeded; a failed
     * copy keeps the live file intact and merely lets it grow past the cap. */
    if (rflog_copy_to_previous() && ftruncate(g_fd, 0) != 0) {
        /* Copied but not truncated: the live file keeps growing and the next
         * roll overwrites <path>.1 again. Nothing is lost either way. */
    }
}

static void rflog_write_line(const char *line, size_t n)
{
    if (g_fd < 0)
        return;
    rflog_roll_if_needed(n);
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(g_fd, line + done, n - done);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return;                 /* a lost line is not worth blocking the radio */
        }
        done += (size_t)w;
    }
}

/* --- The boundaries ------------------------------------------------------- */
/* 255-byte payload -> 510 hex + 255 ascii + the fixed fields: 1024 is ample. */
#define RFLOG_LINE_MAX 1024

void daemon_rflog_rx(const char *band_tag, int16_t rssi_cdbm, int16_t snr_cdb,
                     const uint8_t *buf, size_t len)
{
    if (g_fd < 0)
        return;
    char utc[40];
    char line[RFLOG_LINE_MAX];
    rflog_utc_now(utc, sizeof(utc));
    size_t n = daemon_rflog_format_rx(line, sizeof(line), utc, band_tag,
                                      rssi_cdbm, snr_cdb, buf, len);
    rflog_write_line(line, n);
}

void daemon_rflog_tx(const char *band_tag, const char *outcome,
                     const uint8_t *buf, size_t len)
{
    if (g_fd < 0)
        return;
    char utc[40];
    char line[RFLOG_LINE_MAX];
    rflog_utc_now(utc, sizeof(utc));
    size_t n = daemon_rflog_format_tx(line, sizeof(line), utc, band_tag, outcome, buf, len);
    rflog_write_line(line, n);
}
