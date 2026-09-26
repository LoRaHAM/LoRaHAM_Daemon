#include "radio_cad.h"
#include "daemon_rx_rearm.h"
#include "daemon_cad_monitor.h"

/* Bodies moved verbatim from radio_cad.h and daemon_cad_monitor.h. */

const char *radio_cad_probe_status_name(RadioCadProbeStatus status)
{
    if (status == RADIO_CAD_PROBE_FREE)
        return "FREE";

    if (status == RADIO_CAD_PROBE_BUSY)
        return "BUSY";

    return "UNAVAILABLE";
}

RadioCadProbeStatus radio_cad_status_from_scan_state(int state)
{
#ifdef RADIOLIB_CHANNEL_FREE
    if (state == RADIOLIB_CHANNEL_FREE)
        return RADIO_CAD_PROBE_FREE;
#endif
#ifdef RADIOLIB_CHANNEL_OCCUPIED
    if (state == RADIOLIB_CHANNEL_OCCUPIED)
        return RADIO_CAD_PROBE_BUSY;
#endif
#ifdef RADIOLIB_CHANNEL_BUSY
    if (state == RADIOLIB_CHANNEL_BUSY)
        return RADIO_CAD_PROBE_BUSY;
#endif
#ifdef RADIOLIB_LORA_DETECTED
    if (state == RADIOLIB_LORA_DETECTED)
        return RADIO_CAD_PROBE_BUSY;
#endif
#ifdef RADIOLIB_PREAMBLE_DETECTED
    if (state == RADIOLIB_PREAMBLE_DETECTED)
        return RADIO_CAD_PROBE_BUSY;
#endif
#ifdef RADIOLIB_ERR_CHANNEL_BUSY
    if (state == RADIOLIB_ERR_CHANNEL_BUSY)
        return RADIO_CAD_PROBE_BUSY;
#endif

    if (state == 0)
        return RADIO_CAD_PROBE_FREE;

    if (state > 0)
        return RADIO_CAD_PROBE_BUSY;

    return RADIO_CAD_PROBE_UNAVAILABLE;
}

RadioCadProbeResult radio_cad_probe_unavailable(void)
{
    RadioCadProbeResult result;

    result.status = RADIO_CAD_PROBE_UNAVAILABLE;
    result.scan_state = 0;
    result.scan_ran = 0;
    result.rssi_dbm = -200.0f;

    return result;
}

/*
 * Stop reception, then decide whether a packet is pending.
 *
 * Asking the chip for RxDone is only meaningful if the receiver can no longer
 * produce a new one. An earlier version asked while RX was still running, which
 * narrowed the window instead of closing it: the query could return false and a
 * packet complete microseconds later, before the probe detached DIO0 and
 * entered CAD -- and the restore path then cleared the IRQs and forced
 * received=false, so the packet was gone.
 *
 * So: standby() first, with the IRQ flags untouched and the RX callback still
 * installed, and only then look. After standby the answer cannot change under
 * us. startChannelScan() would have gone to standby anyway, so this costs
 * nothing on the path where no packet is pending.
 *
 * Returns true when the caller must abandon the probe. `pending` distinguishes
 * "a packet is waiting" (report BUSY; the main loop drains the FIFO and re-arms)
 * from "the radio would not quiesce" (report UNAVAILABLE, state untouched).
 */
static bool radio_cad_quiesce_and_check_rx(RadioController *ctrl, bool *pending)
{
    *pending = false;

    if (ctrl->driver->standby() != RADIOLIB_ERR_NONE)
        return true;   /* fail closed: no scan on a radio we could not stop */

    if (!ctrl->received.load() && !ctrl->driver->rxDonePending())
        return false;  /* nothing arrived; the probe may proceed */

    /* A packet completed at or before the quiesce. Make sure the main loop
     * knows: the chip's flag may be set while the software one is not, and the
     * RX consumer keys off the software one. Nothing here clears the IRQs or
     * re-arms -- the drain path does both, and doing either now would destroy
     * the packet this check exists to protect. */
    ctrl->received.store(true);
    *pending = true;
    return true;
}

/*
 * The other half of radio_cad_restore_rx_after_probe(), and the half that was
 * missing.
 *
 * startChannelScan() remaps DIO0 from RxDone to **CadDone**. The daemon's
 * packet-received alert is attached to that same pin, so when the scan
 * completes and DIO0 rises, the RX callback fires and sets `received` -- and
 * the main loop then reads a FIFO that still holds the PREVIOUS packet and
 * delivers it to every client a second time.
 *
 * Measured, not reasoned: ten unique frames sent between the two boxes arrived
 * as sixteen receptions, five of them exact duplicates with identical RSSI and
 * SNR. The baseline daemon on the same board and the same test duplicated none
 * -- its blocking scanChannel() spins on sched_yield() and never sleeps, so the
 * alert thread rarely got to run before the flags were cleared again. Polling
 * the result register with a real 1 ms sleep gives that thread the CPU every
 * time, which turned a latent race into a reliable defect.
 *
 * The TX path has done exactly this since before the repair (daemon_tx.cpp:118)
 * for the same reason; the CAD path simply never did.
 */
static void radio_cad_detach_rx_before_probe(RadioController *ctrl)
{
    if (!ctrl || !ctrl->driver)
        return;

    if (ctrl->mode != RADIO_MODE_LORA)
        return;

    ctrl->driver->clearPacketReceivedAction();
}

void radio_cad_restore_rx_after_probe(RadioController *ctrl)
{
    if (!ctrl || !ctrl->driver || !radio_controller_ready(ctrl))
        return;

    if (ctrl->mode != RADIO_MODE_LORA)
        return;

    // A CAD/scanChannel may have left an RxDone/preamble IRQ pending. Clear it
    // and the received flag before re-arming so the probe never surfaces as a
    // (re-delivered) RX packet. Order mirrors the TX-end restore in daemon_tx.
    ctrl->driver->clearIrq(0xFFFFFFFF);
    ctrl->received.store(false);
    ctrl->driver->setPacketReceivedAction(ctrl->rx_callback);
    daemon_rx_rearm_note_result(ctrl, ctrl->driver->startReceive(),
                                "CAD-Probe");
}

/*
 * HW-3: the modem and the receiver must be checked BEFORE the radio is touched,
 * not after.
 *
 * Both conditions make a reading meaningless, and both are reachable:
 *
 *   - Not in LoRa mode. Every probe here reads a LoRa RSSI register or runs a
 *     LoRa CAD; in FSK the answer describes a different modem. The checks
 *     existed, but they sat AFTER the register read.
 *   - An RX re-arm is pending. The datasheet defines RSSI only while the
 *     receiver has been active for TS_RE + TS_RSSI; a value read in standby is
 *     undefined. This is not hypothetical: the daemon deliberately retries a
 *     failed re-arm before escalating to FAILED, and config_status.cpp folds
 *     rx_rearm_pending into RXREADY, so READY with the receiver temporarily not
 *     armed is a real state. Without this guard an undefined standby reading
 *     would become a FREE or BUSY verdict during a re-arm incident -- and a
 *     false FREE is a transmission on top of someone else.
 *
 * UNAVAILABLE already means "state untouched, no verdict": the CAD monitor
 * skips the sample and the TX gate ends the attempt rather than sending anyway.
 * So no new state is needed, only the check in the right place.
 *
 * "The right place" is BEFORE any radio call, in EVERY probe. An earlier
 * version of this repair let the two active probes take their RSSI snapshot
 * first, reasoning that a snapshot is a harmless register read. It is not, in
 * FSK: RadioDriver::getRSSI() delegates to RadioLib's ordinary getRSSI(),
 * whose FSK branch calls startReceive() -- which rewrites the DIO mapping and
 * clears ALL IRQ flags, so it can discard a received packet -- reads the
 * register, and drops the chip back to standby. That is exactly the
 * destructive sequence HW-3 exists to remove, and GET CHANNEL runs through
 * this path. Only Sx127xDriver::rssiProbe(), which calls getRSSI(false, true),
 * skips the receive.
 *
 * So: gate first. Outside LoRa, take the snapshot with rssiProbe() -- GET
 * CHANNEL keeps reporting an FSK RSSI -- and return UNAVAILABLE without
 * scanning. Inside LoRa the existing call is a pure register read and keeps
 * the packet-RSSI semantics GET CHANNEL has always reported.
 */
static bool radio_cad_probe_preconditions_ok(const RadioController *ctrl)
{
    return ctrl->mode == RADIO_MODE_LORA && !ctrl->rx_rearm_pending.load();
}

RadioCadProbeResult radio_cad_probe_passive(RadioController *ctrl)
{
    RadioCadProbeResult result = radio_cad_probe_unavailable();

    if (!ctrl || !ctrl->driver || !radio_controller_ready(ctrl))
        return result;

    /* Lock discipline (see radio_controller.h): the monitoring tick runs in
     * the main loop and must never block behind a TX holding radio_mutex.
     * UNAVAILABLE means "state untouched", so the CAD monitor simply skips
     * this sample. */
    if (!radio_cad_probe_preconditions_ok(ctrl))
        return result;   /* before the radio is touched at all */

    if (ctrl->tx_busy.load())
        return result;

    std::unique_lock<std::recursive_mutex> radio_lock(
        ctrl->radio_mutex, std::try_to_lock);
    if (!radio_lock.owns_lock())
        return result;

    if (ctrl->tx_busy.load())
        return result;

    // Live channel RSSI: the driver's rssiProbe() reads the instant RSSI
    // (current channel energy, not the stale last-packet RSSI) without
    // re-entering RX. Non-destructive, same source as the GETRSSI live stream.
    result.rssi_dbm = ctrl->driver->rssiProbe();

    /* Validity gate: the -200 sentinel (and anything below any
     * physical noise floor) means "no usable reading", not "quiet channel" —
     * report UNAVAILABLE instead of a false FREE. */
    if (result.rssi_dbm <= -190.0f) {
        result.status = RADIO_CAD_PROBE_UNAVAILABLE;
        return result;
    }

    result.scan_ran = 0;
    result.status = (result.rssi_dbm >= ctrl->cad_rssi_threshold_dbm.load())
                        ? RADIO_CAD_PROBE_BUSY
                        : RADIO_CAD_PROBE_FREE;
    return result;
}

RadioCadProbeResult radio_cad_try_probe(RadioController *ctrl)
{
    RadioCadProbeResult result = radio_cad_probe_unavailable();

    if (!ctrl || !ctrl->driver || !radio_controller_ready(ctrl))
        return result;

    /* No trustworthy active CAD on this profile/driver combination: the
     * capability flag says so. Degrade defined: answer from the passive RSSI
     * probe (scan_ran stays 0, so CADSCAN=0 marks the non-scan source). */
    if (!ctrl->cad_scan_available)
        return radio_cad_probe_passive(ctrl);

    if (ctrl->tx_busy.load())
        return result;

    std::unique_lock<std::recursive_mutex> radio_lock(
        ctrl->radio_mutex, std::try_to_lock);
    if (!radio_lock.owns_lock())
        return result;

    if (ctrl->tx_busy.load())
        return result;

    /* Pending-RX guard: a packet that finished reception but has
     * not been drained by the main loop yet — flag set, payload still in the
     * FIFO — must not be destroyed by the probe's IRQ-clear/re-arm. A channel
     * that just delivered a packet is legitimately BUSY: the MANAGED CAD loop
     * backs off and the main loop gets its tick to drain the packet. RSSI
     * comes from the passive live read (mirrors GET CHANNEL's pending path).
     */
    if (ctrl->received.load()) {
        result.rssi_dbm = ctrl->driver->rssiProbe();
        result.scan_ran = 0;
        result.status = RADIO_CAD_PROBE_BUSY;
        return result;
    }

    /* Gate BEFORE any radio call (see the note above radio_cad_detach...). */
    if (ctrl->rx_rearm_pending.load())
        return result;

    if (ctrl->mode != RADIO_MODE_LORA) {
        result.rssi_dbm = ctrl->driver->rssiProbe();
        return result;
    }

    result.rssi_dbm = ctrl->driver->getRSSI();

    /* Stop the receiver, THEN decide. See radio_cad_quiesce_and_check_rx(). */
    {
        bool pending = false;

        if (radio_cad_quiesce_and_check_rx(ctrl, &pending)) {
            result.scan_ran = 0;
            result.status = pending ? RADIO_CAD_PROBE_BUSY
                                    : RADIO_CAD_PROBE_UNAVAILABLE;
            return result;
        }
    }

    ctrl->cad_active.store(true);
    /* DIO0 is about to mean CadDone, not RxDone: the packet-received alert
     * must come off it first, or CAD completion re-delivers the last packet. */
    radio_cad_detach_rx_before_probe(ctrl);
    result.scan_state = ctrl->driver->scanChannel();
    ctrl->cad_active.store(false);
    radio_cad_restore_rx_after_probe(ctrl);

    result.scan_ran = 1;
    result.status = radio_cad_status_from_scan_state(result.scan_state);

    return result;
}

RadioCadProbeResult radio_cad_probe(RadioController *ctrl)
{
    RadioCadProbeResult result = radio_cad_probe_unavailable();

    if (!ctrl || !ctrl->driver || !radio_controller_ready(ctrl))
        return result;

    /* No trustworthy active CAD (see radio_cad_try_probe). MANAGED TX gating
     * then runs on the passive RSSI probe: BUSY/FREE by CADRSSI threshold, so
     * LBT stays functional on such a profile. */
    if (!ctrl->cad_scan_available)
        return radio_cad_probe_passive(ctrl);

    std::lock_guard<std::recursive_mutex> radio_lock(ctrl->radio_mutex);

    /* Pending-RX guard: a packet that finished reception but has
     * not been drained by the main loop yet — flag set, payload still in the
     * FIFO — must not be destroyed by the probe's IRQ-clear/re-arm. A channel
     * that just delivered a packet is legitimately BUSY: the MANAGED CAD loop
     * backs off and the main loop gets its tick to drain the packet. RSSI
     * comes from the passive live read (mirrors GET CHANNEL's pending path).
     */
    if (ctrl->received.load()) {
        result.rssi_dbm = ctrl->driver->rssiProbe();
        result.scan_ran = 0;
        result.status = RADIO_CAD_PROBE_BUSY;
        return result;
    }

    /* Same order, same reason: radio_controller_packet_rssi() ends in the very
     * getRSSI() whose FSK branch re-enters RX. */
    if (ctrl->rx_rearm_pending.load())
        return result;

    if (ctrl->mode != RADIO_MODE_LORA) {
        result.rssi_dbm = ctrl->driver->rssiProbe();
        return result;
    }

    result.rssi_dbm = radio_controller_packet_rssi(ctrl);

    /* Stop the receiver, THEN decide. See radio_cad_quiesce_and_check_rx(). */
    {
        bool pending = false;

        if (radio_cad_quiesce_and_check_rx(ctrl, &pending)) {
            result.scan_ran = 0;
            result.status = pending ? RADIO_CAD_PROBE_BUSY
                                    : RADIO_CAD_PROBE_UNAVAILABLE;
            return result;
        }
    }

    ctrl->cad_active.store(true);
    /* DIO0 is about to mean CadDone, not RxDone: the packet-received alert
     * must come off it first, or CAD completion re-delivers the last packet. */
    radio_cad_detach_rx_before_probe(ctrl);
    result.scan_state = ctrl->driver->scanChannel();
    ctrl->cad_active.store(false);
    radio_cad_restore_rx_after_probe(ctrl);

    result.scan_ran = 1;
    result.status = radio_cad_status_from_scan_state(result.scan_state);

    return result;
}
DaemonCadMonitorTick daemon_cad_monitor_tick(
    RadioController *ctrl)
{
    DaemonCadMonitorTick tick;

    tick.edge = 0;
    tick.rssi_dbm = -200.0f;
    tick.sampled = 0;

    if (!ctrl)
        return tick;

    RadioCadProbeResult probe = radio_cad_probe_passive(ctrl);

    tick.rssi_dbm = probe.rssi_dbm;

    if (probe.status == RADIO_CAD_PROBE_UNAVAILABLE)
        return tick;

    bool was_active = ctrl->cad_broadcast_active.load();
    int free_streak = ctrl->cad_monitor_free_streak.load();
    int now_busy = daemon_monitoring_cad_next_busy(
        was_active ? 1 : 0,
        probe.rssi_dbm,
        ctrl->cad_rssi_threshold_dbm.load(),
        &free_streak);

    ctrl->cad_monitor_free_streak.store(free_streak);
    ctrl->cad_broadcast_active.store(now_busy != 0);

    tick.edge = daemon_monitoring_cad_broadcast_edge(was_active ? 1 : 0,
                                                     now_busy);
    tick.sampled = 1;

    return tick;
}
