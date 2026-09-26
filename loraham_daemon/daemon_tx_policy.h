#ifndef LORAHAM_DAEMON_TX_POLICY_H
#define LORAHAM_DAEMON_TX_POLICY_H

#include <stdint.h>

/* --- CAD/TX-Policy ------------------------------------------------------- */

#define DAEMON_TX_POLICY_BUSY_TIMEOUT_MS      120000u
#define DAEMON_TX_POLICY_CAD_WAIT_TIMEOUT_MS    1500u
#define DAEMON_TX_POLICY_CAD_IDLE_STABLE_MS      250u
#define DAEMON_TX_POLICY_POLL_INTERVAL_MS         50u

#define DAEMON_TX_POLICY_SEND_AFTER_CAD_TIMEOUT 0

static inline uint32_t daemon_tx_policy_ticks_for_ms(uint32_t timeout_ms,
                                                     uint32_t interval_ms)
{
    if (interval_ms == 0)
        return 0;

    return (timeout_ms + interval_ms - 1u) / interval_ms;
}

static inline uint32_t daemon_tx_policy_busy_timeout_ticks(void)
{
    return daemon_tx_policy_ticks_for_ms(DAEMON_TX_POLICY_BUSY_TIMEOUT_MS,
                                         DAEMON_TX_POLICY_POLL_INTERVAL_MS);
}

static inline uint32_t daemon_tx_policy_cad_wait_ticks(void)
{
    return daemon_tx_policy_ticks_for_ms(DAEMON_TX_POLICY_CAD_WAIT_TIMEOUT_MS,
                                         DAEMON_TX_POLICY_POLL_INTERVAL_MS);
}

static inline uint32_t daemon_tx_policy_cad_idle_stable_ticks(void)
{
    return daemon_tx_policy_ticks_for_ms(DAEMON_TX_POLICY_CAD_IDLE_STABLE_MS,
                                         DAEMON_TX_POLICY_POLL_INTERVAL_MS);
}


static inline uint32_t daemon_tx_policy_poll_interval_usec(void)
{
    return DAEMON_TX_POLICY_POLL_INTERVAL_MS * 1000u;
}

static inline int daemon_tx_policy_timeout_reached(uint32_t elapsed_ms,
                                                   uint32_t timeout_ms)
{
    return timeout_ms > 0u && elapsed_ms >= timeout_ms;
}

static inline int daemon_tx_policy_send_after_cad_timeout(void)
{
    return DAEMON_TX_POLICY_SEND_AFTER_CAD_TIMEOUT ? 1 : 0;
}

#endif
