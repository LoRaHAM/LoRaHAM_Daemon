#ifndef LORAHAM_DAEMON_RFLOG_H
#define LORAHAM_DAEMON_RFLOG_H

/* --- RF log: what this process heard and sent on the radio --------------- */
/*
 * One persistent, human-readable line per RF frame, appended to a file the
 * controller names explicitly (`--rflog on --rflog-path <absolute path>`).
 * The daemon derives nothing: no path, no directory, no band suffix — the
 * caller decides where the log lives, and this process writes exactly one file.
 *
 * Line contract (one definition, shared with the other LoRaHAM writers):
 *
 *   <utc> RX rssi=<dBm> snr=<dB> len=<n> band=<b> hex=<..> ascii="<..>"
 *   <utc> TX rssi=- snr=- len=<n> outcome=<ok|unconfirmed> band=<b> hex=<..> ascii="<..>"
 *
 * RSSI/SNR are RECEIVE metadata; a TX line carries payload and outcome, never
 * signal strength. The payload is raw — this is an RF log, not a decoder.
 *
 * Retention: the active file is capped at DAEMON_RFLOG_MAX_BYTES; on the write
 * that would exceed it the whole file is COPIED to <path>.1 and the active file
 * is truncated in place. The inode never changes (no rename), so an external
 * truncation — the controller's Clear — is tolerated: O_APPEND writes land at
 * the new end. One writer per file: no lock, no rollover race.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAEMON_RFLOG_MAX_BYTES (5u * 1024u * 1024u)

/* Parse "on"|"off" (case-insensitive). Returns false on invalid. */
bool daemon_parse_rflog_switch(const char *arg, bool *out);

/* CLI setters for the two boot slots. The path must be absolute. */
bool daemon_set_rflog_switch_global(const char *arg);
bool daemon_set_rflog_path_global(const char *arg);

/*
 * Resolve the boot slots and open the file. Returns false, with a message in
 * `err`, when the switch is on and no path was given, or the path cannot be
 * opened for append — an RF log the operator asked for must never silently
 * not exist. With the switch off this is a no-op that returns true.
 */
bool daemon_rflog_start(char *err, size_t err_len);
void daemon_rflog_stop(void);
bool daemon_rflog_active(void);

/* The two boundaries. No-ops while the log is not active. */
void daemon_rflog_rx(const char *band_tag, int16_t rssi_cdbm, int16_t snr_cdb,
                     const uint8_t *buf, size_t len);
void daemon_rflog_tx(const char *band_tag, const char *outcome,
                     const uint8_t *buf, size_t len);

/* --- Pure helpers, exposed for the unit test ----------------------------- */
size_t daemon_rflog_format_rx(char *out, size_t out_len, const char *utc,
                              const char *band_tag, int16_t rssi_cdbm, int16_t snr_cdb,
                              const uint8_t *buf, size_t len);
size_t daemon_rflog_format_tx(char *out, size_t out_len, const char *utc,
                              const char *band_tag, const char *outcome,
                              const uint8_t *buf, size_t len);

/* Test helpers: reset both boot slots and the open file; lower the cap. */
void daemon_rflog_reset(void);
void daemon_rflog_set_max_bytes(size_t max_bytes);

#endif
