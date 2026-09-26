#include "config_status.h"
#include "hardware_profile.h"

/* Bodies moved verbatim from config_status.h. */

void config_status_trim_copy(char *dst,
                                           size_t dst_size,
                                           const char *src)
{
    size_t start = 0;
    size_t end = 0;
    size_t len;

    if (!dst || dst_size == 0)
        return;

    dst[0] = '\0';

    if (!src)
        return;

    len = strlen(src);
    while (start < len &&
           (src[start] == ' ' || src[start] == '\t' ||
            src[start] == '\r' || src[start] == '\n')) {
        start++;
    }

    end = len;
    while (end > start &&
           (src[end - 1] == ' ' || src[end - 1] == '\t' ||
            src[end - 1] == '\r' || src[end - 1] == '\n')) {
        end--;
    }

    len = end - start;
    if (len >= dst_size)
        len = dst_size - 1;

    memcpy(dst, src + start, len);
    dst[len] = '\0';
}

void config_status_uppercase(char *s)
{
    if (!s)
        return;

    for (; *s; s++)
        *s = (char)toupper((unsigned char)*s);
}

int config_status_command_equals(const char *line,
                                               const char *expected)
{
    char cmd[64];

    config_status_trim_copy(cmd, sizeof(cmd), line);
    config_status_uppercase(cmd);

    return strcmp(cmd, expected) == 0;
}

int config_status_is_set_txresult(const char *line,
                                               int *enabled)
{
    char cmd[64];

    config_status_trim_copy(cmd, sizeof(cmd), line);
    config_status_uppercase(cmd);

    if (strcmp(cmd, "SET TXRESULT=1") == 0) {
        if (enabled)
            *enabled = 1;
        return 1;
    }

    if (strcmp(cmd, "SET TXRESULT=0") == 0) {
        if (enabled)
            *enabled = 0;
        return 1;
    }

    return 0;
}

int config_status_is_set_txqueue(const char *line,
                                              int *enabled)
{
    char cmd[64];

    config_status_trim_copy(cmd, sizeof(cmd), line);
    config_status_uppercase(cmd);

    if (strcmp(cmd, "SET TXQUEUE=1") == 0) {
        if (enabled)
            *enabled = 1;
        return 1;
    }

    if (strcmp(cmd, "SET TXQUEUE=0") == 0) {
        if (enabled)
            *enabled = 0;
        return 1;
    }

    return 0;
}

int config_status_is_set_cadmonitor(const char *line,
                                                  int *enabled)
{
    char cmd[64];

    config_status_trim_copy(cmd, sizeof(cmd), line);
    config_status_uppercase(cmd);

    if (strcmp(cmd, "SET CADMONITOR=1") == 0) {
        if (enabled)
            *enabled = 1;
        return 1;
    }

    if (strcmp(cmd, "SET CADMONITOR=0") == 0) {
        if (enabled)
            *enabled = 0;
        return 1;
    }

    return 0;
}

int config_status_is_set_txmode(const char *line,
                                             RadioTxMode_t *mode)
{
    char cmd[64];

    config_status_trim_copy(cmd, sizeof(cmd), line);
    config_status_uppercase(cmd);

    if (strcmp(cmd, "SET TXMODE=MANAGED") == 0) {
        if (mode)
            *mode = RADIO_TX_MODE_MANAGED;
        return 1;
    }

    if (strcmp(cmd, "SET TXMODE=DIRECT") == 0) {
        if (mode)
            *mode = RADIO_TX_MODE_DIRECT;
        return 1;
    }

    return 0;
}

int config_status_parse_set_uval(const char *cmd,
                                               const char *prefix,
                                               uint32_t lo, uint32_t hi,
                                               uint32_t *out)
{
    size_t plen;
    char *endptr;
    unsigned long v;

    if (!cmd || !prefix || cmd[0] == '\0' || prefix[0] == '\0')
        return 0;

    plen = strlen(prefix);
    if (strncmp(cmd, prefix, plen) != 0)
        return 0;

    if (cmd[plen] == '\0')
        return 0;

    v = strtoul(cmd + plen, &endptr, 10);
    if (*endptr != '\0' || v < (unsigned long)lo || v > (unsigned long)hi)
        return 0;

    if (out)
        *out = (uint32_t)v;
    return 1;
}

int config_status_parse_set_ival(const char *cmd,
                                               const char *prefix,
                                               long lo, long hi,
                                               long *out)
{
    size_t plen;
    char *endptr;
    long v;

    if (!cmd || !prefix || cmd[0] == '\0' || prefix[0] == '\0')
        return 0;

    plen = strlen(prefix);
    if (strncmp(cmd, prefix, plen) != 0)
        return 0;

    if (cmd[plen] == '\0')
        return 0;

    v = strtol(cmd + plen, &endptr, 10);
    if (*endptr != '\0' || v < lo || v > hi)
        return 0;

    if (out)
        *out = v;
    return 1;
}

int config_status_is_set_cadrssi(const char *line, int *dbm)
{
    char cmd[64];
    long v = 0;

    config_status_trim_copy(cmd, sizeof(cmd), line);
    config_status_uppercase(cmd);
    if (!config_status_parse_set_ival(cmd, "SET CADRSSI=", -130, 0, &v))
        return 0;

    if (dbm)
        *dbm = (int)v;
    return 1;
}

int config_status_is_set_cadwait(const char *line, uint32_t *ms)
{
    char cmd[64];

    config_status_trim_copy(cmd, sizeof(cmd), line);
    config_status_uppercase(cmd);
    return config_status_parse_set_uval(cmd, "SET CADWAIT=", 50u, 5000u, ms);
}

int config_status_is_set_cadidle(const char *line, uint32_t *ms)
{
    char cmd[64];

    config_status_trim_copy(cmd, sizeof(cmd), line);
    config_status_uppercase(cmd);
    return config_status_parse_set_uval(cmd, "SET CADIDLE=", 0u, 2000u, ms);
}

int config_status_is_set_cadpoll(const char *line, uint32_t *ms)
{
    char cmd[64];

    config_status_trim_copy(cmd, sizeof(cmd), line);
    config_status_uppercase(cmd);
    return config_status_parse_set_uval(cmd, "SET CADPOLL=", 10u, 500u, ms);
}

int config_status_is_set_cadtxaftertimeout(const char *line, int *val)
{
    char cmd[64];

    config_status_trim_copy(cmd, sizeof(cmd), line);
    config_status_uppercase(cmd);

    if (strcmp(cmd, "SET CADTXAFTERTIMEOUT=1") == 0) {
        if (val)
            *val = 1;
        return 1;
    }

    if (strcmp(cmd, "SET CADTXAFTERTIMEOUT=0") == 0) {
        if (val)
            *val = 0;
        return 1;
    }

    return 0;
}

/* Counters report the worker's real state regardless of tx_queue_active:
 * SET TXQUEUE=0 is only accepted once the queue is fully
 * drained (drain-before-disable), but the counters must never gate on the
 * flag — hiding the worker's real history/state would blind an operator. */
size_t config_status_txq_pending(const RadioController *ctrl)
{
    if (!ctrl)
        return 0;

    return daemon_tx_async_runtime_pending();
}

size_t config_status_txq_dropped(const RadioController *ctrl)
{
    if (!ctrl)
        return 0;

    return daemon_tx_async_runtime_dropped();
}

size_t config_status_txq_rejected(const RadioController *ctrl)
{
    if (!ctrl)
        return 0;

    return daemon_tx_async_runtime_rejected();
}

size_t config_status_txq_stale(const RadioController *ctrl)
{
    if (!ctrl)
        return 0;

    return daemon_tx_async_runtime_completion_stale();
}

size_t config_status_txq_result_dropped(
    const RadioController *ctrl)
{
    if (!ctrl)
        return 0;

    return daemon_tx_async_runtime_completion_dropped();
}

size_t config_status_txq_processed(const RadioController *ctrl)
{
    if (!ctrl)
        return 0;

    return daemon_tx_async_runtime_processed();
}

int config_status_txq_last_result(const RadioController *ctrl,
                                                DaemonTxJobResult *result)
{
    if (!ctrl || !result)
        return 0;

    return daemon_tx_async_runtime_last_result(result);
}

const char *config_status_txq_last_name(const RadioController *ctrl)
{
    DaemonTxJobResult result;

    if (!config_status_txq_last_result(ctrl, &result))
        return "NONE";

    return tx_result_name(result.tx_result);
}

unsigned config_status_txq_last_seq(const RadioController *ctrl)
{
    DaemonTxJobResult result;

    if (!config_status_txq_last_result(ctrl, &result))
        return 0;

    return result.seq;
}

void config_status_format(char *buf,
                                        size_t buf_size,
                                        const RadioController *ctrl)
{
    snprintf(buf,
             buf_size,
             "STATUS RADIO=%s TX=%d CAD=%d GETRSSI=%d TXRESULT=%d TXMODE=%s TXQUEUE=%d TXQ=%zu TXQDROP=%zu TXQREJECT=%zu TXQSTALE=%zu TXQRESULTDROP=%zu TXQDONE=%zu TXQLAST=%s TXQSEQ=%u CADWAIT=%u CADIDLE=%u CADPOLL=%u CADTXAFTERTIMEOUT=%d CADMONITOR=%d CADRSSI=%.0f RXREADY=%d HIGHPOWER=%d CHIPFAMILY=%s\n",
             radio_health_name(radio_controller_health(ctrl)),
             (ctrl && ctrl->tx_busy.load()) ? 1 : 0,
             (ctrl && ctrl->cad_broadcast_active.load()) ? 1 : 0,
             (ctrl && ctrl->getrssi_active.load()) ? 1 : 0,
             (ctrl && ctrl->tx_result_active.load()) ? 1 : 0,
             radio_tx_mode_name(ctrl ? ctrl->tx_mode : RADIO_TX_MODE_MANAGED),
             (ctrl && ctrl->tx_queue_active.load()) ? 1 : 0,
             config_status_txq_pending(ctrl),
             config_status_txq_dropped(ctrl),
             config_status_txq_rejected(ctrl),
             config_status_txq_stale(ctrl),
             config_status_txq_result_dropped(ctrl),
             config_status_txq_processed(ctrl),
             config_status_txq_last_name(ctrl),
             config_status_txq_last_seq(ctrl),
             ctrl ? (unsigned)ctrl->cad_wait_timeout_ms.load()
                  : DAEMON_TX_POLICY_CAD_WAIT_TIMEOUT_MS,
             ctrl ? (unsigned)ctrl->cad_idle_stable_ms.load()
                  : DAEMON_TX_POLICY_CAD_IDLE_STABLE_MS,
             ctrl ? (unsigned)ctrl->cad_poll_interval_ms.load()
                  : DAEMON_TX_POLICY_POLL_INTERVAL_MS,
             (ctrl && ctrl->cad_send_after_timeout.load()) ? 1 : 0,
             (ctrl && ctrl->cad_monitor_active.load()) ? 1 : 0,
             (double)(ctrl ? ctrl->cad_rssi_threshold_dbm.load()
                           : RADIO_CAD_RSSI_BUSY_THRESHOLD_DBM),
             /* RXREADY (appended, audit P1-6): 1 = receiver armed; 0 = a
              * re-arm failure is latched (retry running) or radio not
              * READY. */
             (radio_controller_ready(ctrl) &&
              !(ctrl && ctrl->rx_rearm_pending.load())) ? 1 : 0,
             /* HIGHPOWER / CHIPFAMILY (appended, additive): the raw boot
              * permission and the running chip family. HIGHPOWER=1 on an
              * SX1262 is truthful and means nothing -- the family field is
              * what a reader combines it with. Neither reports the selected
              * power; POWER is not echoed anywhere. */
             (ctrl && ctrl->high_power_enabled) ? 1 : 0,
             daemon_chip_family_name(ctrl ? ctrl->chip_family
                                          : DAEMON_CHIP_FAMILY_SX127X));
}

const char *config_status_cad_state_name(RadioCadProbeStatus status)
{
    if (status == RADIO_CAD_PROBE_FREE)
        return "FREE";

    if (status == RADIO_CAD_PROBE_BUSY)
        return "BUSY";

    return "UNAVAILABLE";
}

float config_status_live_rssi_dbm(RadioController *ctrl)
{
    if (!ctrl || !ctrl->driver || ctrl->tx_busy.load())
        return -200.0f;

    std::unique_lock<std::recursive_mutex> radio_lock(
        ctrl->radio_mutex, std::try_to_lock);
    if (!radio_lock.owns_lock() || ctrl->tx_busy.load())
        return -200.0f;

    // Chip-native raw register read; lives in the driver.
    return ctrl->driver->readLiveRssi(ctrl->mode, ctrl->is_hf);
}

/* The CHANNEL line for an already-decided pending state. `pending` is a SNAPSHOT taken by the
 * caller, exactly once per request: `received` can be cleared by the RX drain at any moment, and
 * a second load here could contradict the decision the caller already acted on. Legacy
 * GET CHANNEL in particular commits to its non-scanning branch on that load, so re-reading could
 * make it answer NOTSCANNED -- a state that command has never emitted. */
static void config_status_format_channel_snapshot(char *buf,
                                                                size_t buf_size,
                                                                RadioController *ctrl,
                                                                bool pending,
                                                                float live_rssi)
{
    // The CHANNEL line WITHOUT a channel-activity scan. Never calls scanChannel(), never takes
    // the radio out of RX, and is therefore safe to answer on a timer -- a scan destroys a frame
    // that is arriving, so a status poll must never run one.
    //
    // NULL/UNREADY SAFE, and it has to be: config_dispatch answers GET CHANNEL before any
    // readiness gate, so `ctrl` can be absent entirely. The health and mode accessors already
    // tolerate that; the threshold and tx_mode reads need their own guards. (As an inner branch
    // this code was safe only because of the enclosing `ctrl && ctrl->received` test.)
    int tx_busy = (ctrl && ctrl->tx_busy.load()) ? 1 : 0;

    // `pending` is the caller's snapshot -- deliberately NOT re-read here. PENDING outranks
    // NOTSCANNED: "a completed, undrained packet is waiting" is real information, strictly
    // stronger than "no verdict was requested".
    float thr = ctrl ? ctrl->cad_rssi_threshold_dbm.load()
                     : (float)RADIO_CAD_RSSI_BUSY_THRESHOLD_DBM;
    int busy = (tx_busy || live_rssi >= thr) ? 1 : 0;

    snprintf(buf,
             buf_size,
             "CHANNEL RADIO=%s BUSY=%d CAD=0 CADSCAN=0 CADSTATE=%s "
             "RSSI=%.2f PACKETRSSI=%.2f LIVERSSI=%.2f MODE=%s TXMODE=%s\n",
             radio_health_name(radio_controller_health(ctrl)),
             busy,
             pending ? "PENDING" : "NOTSCANNED",
             live_rssi,
             live_rssi,
             live_rssi,
             radio_mode_name(radio_controller_mode(ctrl)),
             radio_tx_mode_name(ctrl ? ctrl->tx_mode : RADIO_TX_MODE_MANAGED));
}

void config_status_format_channel_passive(char *buf,
                                                        size_t buf_size,
                                                        RadioController *ctrl)
{
    /* Snapshot `received` FIRST, into a local, before the RSSI read. Two reasons, and the second
     * is why this is a local rather than an argument expression:
     *   - the RX drain can clear it at any moment, so loading it afterwards would report
     *     NOTSCANNED for a packet that was pending when the request arrived;
     *   - argument evaluation order is UNSPECIFIED in C++, so passing the load as one argument
     *     beside the RSSI read would leave the ordering up to the compiler.
     *
     * The RSSI fields all carry the live reading, exactly as the pending branch has always
     * reported them. This command must NOT reach for a truer packet RSSI:
     * radio_controller_packet_rssi() takes radio_mutex with a BLOCKING lock_guard, and
     * radio_controller.h's access rule is that no main-loop path may block on that mutex while a
     * TX can be in flight -- the TX worker holds it across transmit(), seconds at SF12. This is
     * the periodic status read, dispatched on the main loop, so blocking here would stall the
     * daemon for a whole transmission and blow the client's 1 s read timeout.
     * config_status_live_rssi_dbm() is safe precisely because it gates on tx_busy and uses
     * try_to_lock. A genuine last-packet RSSI belongs to the RX path that already has the value,
     * not to an opportunistic reread during a 3-second poll. */
    bool pending = (ctrl && ctrl->received.load());
    float live_rssi = config_status_live_rssi_dbm(ctrl);

    config_status_format_channel_snapshot(buf, buf_size, ctrl, pending, live_rssi);
}

void config_status_format_channel(char *buf,
                                                size_t buf_size,
                                                RadioController *ctrl)
{
    // Pending RX guard: a just-arrived RX packet is waiting to be read. The
    // active scanChannel probe + RX re-arm would discard it, so answer
    // non-destructively (no scanChannel) with CADSTATE=PENDING and a live-RSSI
    // based busy hint. Same formatter the NOSCAN command uses.
    //
    // Checked BEFORE the reads below, so a pending reply does not read the live-RSSI register
    // twice for one response.
    //
    // The snapshot is passed DOWN rather than re-read: this command has now committed to its
    // non-scanning branch, and a second load could see the packet already drained and answer
    // NOTSCANNED -- a state legacy GET CHANNEL has never emitted and no existing client expects.
    // Its pending branch has always meant PENDING unconditionally, and still does.
    if (ctrl && ctrl->received.load()) {
        /* Legacy GET CHANNEL's pending branch has ALWAYS reported the live reading in all three
         * RSSI fields. Unchanged here on purpose: this command's observable behaviour is not part
         * of the repair, and neither command reaches for a truer packet RSSI -- see the note in
         * the passive entry point above for why that would be unsafe here. */
        float live_rssi = config_status_live_rssi_dbm(ctrl);
        config_status_format_channel_snapshot(buf, buf_size, ctrl, true, live_rssi);
        return;
    }

    int tx_busy = (ctrl && ctrl->tx_busy.load()) ? 1 : 0;
    float live_rssi = config_status_live_rssi_dbm(ctrl);

    RadioCadProbeResult probe = radio_cad_try_probe(ctrl);

    snprintf(buf,
             buf_size,
             "CHANNEL RADIO=%s BUSY=%d CAD=%d CADSCAN=%d CADSTATE=%s "
             "RSSI=%.2f PACKETRSSI=%.2f LIVERSSI=%.2f MODE=%s TXMODE=%s\n",
             radio_health_name(radio_controller_health(ctrl)),
             tx_busy || probe.status == RADIO_CAD_PROBE_BUSY ? 1 : 0,
             probe.scan_ran ? 1 : 0,
             probe.scan_ran ? 1 : 0,
             config_status_cad_state_name(probe.status),
             probe.rssi_dbm,
             probe.rssi_dbm,
             live_rssi,
             radio_mode_name(radio_controller_mode(ctrl)),
             radio_tx_mode_name(ctrl ? ctrl->tx_mode : RADIO_TX_MODE_MANAGED));
}

void config_status_format_stats(char *buf,
                                             size_t buf_size,
                                             const RadioController *ctrl)
{
    long uptime = daemon_stats_uptime_seconds(daemon_now_ms());

    daemon_stats_format_response(buf,
                                 buf_size,
                                 uptime,
                                 radio_controller_health(ctrl),
                                 ctrl ? &ctrl->stats : NULL);
}

int config_status_classify_reserved_setter(const char *line)
{
    static const char *const reserved[] = {
        "TXRESULT", "TXQUEUE", "TXMODE", "CADRSSI", "CADMONITOR",
        "CADWAIT", "CADIDLE", "CADPOLL", "CADTXAFTERTIMEOUT",
    };

    if (!line || strncmp(line, "SET ", 4) != 0)
        return 0;

    const char *key = line + 4;
    size_t key_len = strcspn(key, "= \t\r\n");

    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (key_len == strlen(reserved[i]) &&
            strncmp(key, reserved[i], key_len) == 0) {
            /* The dedicated matcher already rejected the full line, so a
             * present value is invalid; a missing/empty value is
             * structurally incomplete. */
            const char *eq = key + key_len;

            if (*eq != '=' || eq[1] == '\0' || eq[1] == ' ')
                return 2;

            return 1;
        }
    }

    return 0;
}
