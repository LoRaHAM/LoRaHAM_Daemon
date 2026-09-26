#ifndef LORAHAM_LOCKING_PIHAL_H
#define LORAHAM_LOCKING_PIHAL_H

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hal/RPi/PiHal.h"
#include "loraham_runtime.h"

/*
 * PiHal that serializes every SPI transaction across processes (and across
 * bands within one process) using a process-shared advisory flock.
 *
 * Why this is the correct interception point:
 *   RadioLib's Module brackets each transfer as
 *       spiBeginTransaction(); CS-low; spiTransfer(); CS-high; spiEndTransaction();
 *   (RadioLib/src/Module.cpp). Taking the lock in spiBeginTransaction() and
 *   releasing it in spiEndTransaction() protects exactly one complete SPI
 *   transaction -- never the whole daemon lifetime. The bus is free between
 *   transactions, so complete transactions to the two radios (each on its own
 *   CS line) may interleave, which is what a shared SPI bus permits.
 *
 * Why per-instance descriptors:
 *   Each radio constructs its own LockingPiHal with its own open descriptor on
 *   the same lock file; flock() on distinct open file descriptions is mutually
 *   exclusive, giving correct serialization between two separate per-band
 *   daemons sharing /dev/spidev0.0.
 *
 * FAIL CLOSED (production invariant):
 *   No SPI transfer may proceed unless the process-shared lock is confirmed
 *   held. If the trusted lock directory/file cannot be opened, spi_lock_ready()
 *   returns false and the daemon refuses to start that radio. There is no /tmp
 *   fallback. A hard (non-EINTR) flock() failure, or any attempt to transfer
 *   without the lock held, triggers a controlled fatal exit rather than an
 *   unsynchronized bus access.
 *
 * Lock ordering: callers hold the per-band radio_mutex before any SPI call, and
 * this flock is the lowest-level lock taken last -- no inverse path, no deadlock.
 */
/* SPI transactions are µs–ms; 2 s of contention on the shared bus lock can
 * only mean a wedged peer process. */
#define LORAHAM_SPI_LOCK_TIMEOUT_MS 2000L

class LockingPiHal : public PiHal {
  public:
    /* flock_fn is injectable for tests; production uses the real flock(). */
    LockingPiHal(uint8_t spiChannel, uint32_t spiSpeed = 2000000,
                 uint8_t spiDevice = 0, uint8_t gpioDevice = 0,
                 loraham_flock_fn flock_fn = flock)
      : PiHal(spiChannel, spiSpeed, spiDevice, gpioDevice),
        _ownSpiDevice(spiDevice), _ownSpiChannel(spiChannel),
        _ownSpiSpeed(spiSpeed), _ownGpioDevice(gpioDevice), _flock(flock_fn) {
        open_lock();
    }

    ~LockingPiHal() override {
        if (_lockFd >= 0) {
            /* Best-effort at teardown; the kernel releases the lock on close. */
            _flock(_lockFd, LOCK_UN);
            close(_lockFd);
            _lockFd = -1;
        }
    }

    LockingPiHal(const LockingPiHal &) = delete;
    LockingPiHal &operator=(const LockingPiHal &) = delete;

    /* True only if the process-shared SPI lock file was established. The daemon
     * must refuse to start a radio whose HAL is not lock-ready (fail closed). */
    bool spi_lock_ready() const { return _lockFd >= 0; }

    void spiBeginTransaction() override {
        /* Recursion guard: RadioLib does not nest transactions, but a depth
         * counter keeps a (hypothetical) nested begin/end pair from releasing
         * the lock early. Per-band SPI is serialized by radio_mutex, so the
         * counter is only ever touched by one thread per instance. */
        if (_depth++ == 0) {
            if (_lockFd < 0)
                fatal("SPI lock unavailable (fail-closed)");

            /* Bounded: a live-but-wedged peer must not block
             * this daemon forever. Expiry is fatal — systemd restarts a
             * dead process; it cannot see a silently hung one. */
            if (loraham_flock_acquire_ex_deadline(_lockFd, _flock,
                    LORAHAM_SPI_LOCK_TIMEOUT_MS) != 0) {
                if (errno == ETIMEDOUT)
                    fatal("SPI lock not acquired within the deadline "
                          "(peer wedged?)");
                fatal("flock(LOCK_EX) failed hard");
            }

            _held = true;
        }
    }

    /* SPI ownership: the base PiHal prints and SWALLOWS
     * lgSpiXfer/lgSpiOpen errors, so a RadioLib setter can report success
     * after the bus transfer failed. This HAL owns the SPI handle itself and
     * fails closed: an open failure leaves the handle invalid (transfers
     * fatal), a transfer failure is fatal — silent register corruption is
     * never an option. */
    void spiBegin() override {
        if (_ownSpiHandle >= 0)
            return;

        _ownSpiHandle = lgSpiOpen(_ownSpiDevice, _ownSpiChannel,
                                  _ownSpiSpeed, 0);
        if (_ownSpiHandle < 0)
            fprintf(stderr, "[SPI] lgSpiOpen failed: %s\n",
                    lguErrorText(_ownSpiHandle));
    }

    void spiEnd() override {
        if (_ownSpiHandle >= 0) {
            lgSpiClose(_ownSpiHandle);
            _ownSpiHandle = -1;
        }
    }

    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override {
        /*
         * A GPIO open that already failed during startup must not be turned
         * into a RUNTIME fatal here.
         *
         * init() latches the failure and returns void, but RadioLib carries on:
         * Module::init() and the SX127x chip detection run regardless, and the
         * detection's register read arrives at this guard. Exiting 5 from here
         * killed the process before lora_init() could inspect the latch and
         * choose the non-restartable 4 -- so a permanently mis-wired or
         * unpermitted box restarted every two seconds, which is precisely what
         * exit 4 exists to prevent.
         *
         * The transfer is still refused. It simply fails the initialisation
         * rather than the process, and the startup path decides the exit code.
         */
        if (_gpioStartupFailed && !_gpioOperational) {
            if (in)
                memset(in, 0, len);
            return;
        }

        /* Hard invariant: never touch the shared SPI bus without the lock. */
        if (!_held)
            fatal("SPI transfer without the lock held");

        if (_ownSpiHandle < 0)
            fatal("SPI transfer without an open SPI handle");

        int result = lgSpiXfer(_ownSpiHandle, (char *)out, (char *)in, len);
        if (result < 0) {
            fprintf(stderr, "[SPI] lgSpiXfer failed: %s\n",
                    lguErrorText(result));
            fatal("SPI transfer failed (bus error)");
        }
    }

    void spiEndTransaction() override {
        if (_depth > 0 && --_depth == 0) {
            /* Bookkeeping first so state stays consistent on the fatal path. */
            _held = false;
            if (_lockFd >= 0 &&
                loraham_flock_release(_lockFd, _flock) != 0) {
                /* A failed unlock would leave the kernel lock held while we
                 * believe it released -- that could wedge the peer band. Treat
                 * it as fatal: exit via the lock-error path so process teardown
                 * closes the fd and the kernel releases the lock. */
                fatal("flock(LOCK_UN) failed hard");
            }
        }
    }

    /* ---- GPIO ownership ------------------------------------------------
     * Same reasoning as the SPI ownership above, and the same failure. The
     * base PiHal logs a failed lgpio call and carries on, and its digitalRead
     * returns lgGpioRead()'s int through a uint32_t -- so a negative error
     * code (LG_BAD_HANDLE is -5) comes back as 4294967291, which every
     * RadioLib caller reads as a logic HIGH. That is not a cosmetic wart:
     *
     *   SX127x::transmit() waits `while(!digitalRead(irq))`. A truthy error
     *   makes the condition false, the wait loop never runs once, and control
     *   falls through to finishTransmit() -- which returns SUCCESS and puts
     *   the radio in standby. The transmission is cut short on air and the
     *   daemon logs the frame as sent. scanChannel() has the same shape and
     *   returns CHANNEL_FREE, so a broken GPIO tells listen-before-talk the
     *   channel is clear.
     *
     * Worse, the base init() STORES the negative result as the handle, so
     * every later read fails the same way for the life of the process, and
     * its `if(_gpioHandle != -1) return;` guard then treats that poisoned
     * handle as "already initialised" and refuses to retry.
     *
     * So this HAL owns the GPIO handle exactly as it owns the SPI handle. A
     * negative lgpio result is never an electrical level. Before the radio is
     * operational it latches a startup failure (the daemon must then refuse
     * READY and exit non-restartable, rather than restart-loop on a box whose
     * wiring is wrong); after it, radio I/O integrity is lost and we exit the
     * restartable way, as the SPI path already does.
     *
     * NOTE on pull-up flags: PiHal keeps `pinFlags` private and only
     * pullUpDown() ever writes it. Nothing in this daemon or in the SX127x
     * path calls pullUpDown, so the flags are always 0 -- which is what is
     * passed below. If a profile ever needs pulls, this is the place. */

    void init() override {
        if (_ownGpioHandle >= 0)
            return;

        int handle = lgGpiochipOpen(_ownGpioDevice);
        if (handle < 0) {
            fprintf(stderr, "[GPIO] lgGpiochipOpen failed: %s\n",
                    lguErrorText(handle));
            /* Deliberately do NOT store the negative result: a later init()
             * must really retry, and no read may ever run against it. */
            _gpioStartupFailed = true;
            return;
        }

        _ownGpioHandle = handle;
        spiBegin();
    }

    void term() override {
        /* The base term() closes PiHal's own private handle, which this class
         * never opens -- it would close -1 and leave ours dangling. */
        spiEnd();
        if (_ownGpioHandle >= 0) {
            lgGpiochipClose(_ownGpioHandle);
            _ownGpioHandle = -1;
        }
    }

    void pinMode(uint32_t pin, uint32_t mode) override {
        if (pin == RADIOLIB_NC)
            return;
        if (!gpio_ready("pinMode"))
            return;

        int result;
        switch (mode) {
            case PI_INPUT:
                result = lgGpioClaimInput(_ownGpioHandle, 0, (int)pin);
                break;
            case PI_OUTPUT:
                result = lgGpioClaimOutput(_ownGpioHandle, 0, (int)pin, LG_HIGH);
                break;
            default:
                gpio_failure("pinMode with an unknown mode");
                return;
        }

        if (result < 0)
            gpio_failure("lgGpioClaim* failed");
    }

    void digitalWrite(uint32_t pin, uint32_t value) override {
        if (pin == RADIOLIB_NC)
            return;
        if (!gpio_ready("digitalWrite"))
            return;

        if (lgGpioWrite(_ownGpioHandle, (int)pin, (int)value) < 0)
            gpio_failure("lgGpioWrite failed");
    }

    uint32_t digitalRead(uint32_t pin) override {
        if (pin == RADIOLIB_NC)
            return 0;
        if (!gpio_ready("digitalRead"))
            return 0;

        int result = lgGpioRead(_ownGpioHandle, (int)pin);
        if (result < 0) {
            gpio_failure("lgGpioRead failed");
            /* Startup path only (gpio_failure() does not return once
             * operational). LOW is the safe answer: it lets a bounded wait
             * time out, whereas HIGH manufactures a finished TX or a free
             * channel. */
            return 0;
        }

        return (uint32_t)result;
    }

    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void),
                         uint32_t mode) override {
        if ((interruptNum == RADIOLIB_NC) || (interruptNum > PI_MAX_USER_GPIO))
            return;
        if (!gpio_ready("attachInterrupt"))
            return;

        int result = lgGpioClaimAlert(_ownGpioHandle, 0, (int)mode,
                                      (int)interruptNum, -1);
        if (result < 0) {
            /* An unregistered RX callback is silent deafness: the radio
             * reports READY and no packet ever arrives. The base class only
             * printed here. */
            gpio_failure("lgGpioClaimAlert failed");
            return;
        }

        interruptEnabled[interruptNum] = true;
        interruptCallbacks[interruptNum] = interruptCb;
        interruptModes[interruptNum] =
            (mode == this->GpioInterruptFalling) ? LG_LOW : LG_HIGH;

        if (lgGpioSetAlertsFunc(_ownGpioHandle, (int)interruptNum,
                                lgpioAlertHandler, (void *)this) < 0)
            gpio_failure("lgGpioSetAlertsFunc failed");
    }

    void detachInterrupt(uint32_t interruptNum) override {
        if ((interruptNum == RADIOLIB_NC) || (interruptNum > PI_MAX_USER_GPIO))
            return;
        if (!gpio_ready("detachInterrupt"))
            return;

        interruptEnabled[interruptNum] = false;
        interruptModes[interruptNum] = 0;
        interruptCallbacks[interruptNum] = NULL;

        /* Freeing the alert leaves the line unclaimed. A later digitalRead on
         * it is ORDINARY, not a violation: every LoRa TX clears the packet
         * callback, reads DIO0 in the blocking wait, and reinstalls it
         * afterwards. lgGpioRead() claims an unallocated line as input before
         * reading, and lgpio's same-mode claim is idempotent, so the
         * attach -> detach -> blocking read -> re-attach lifecycle is valid.
         *
         * Both results are checked. The contract of this HAL is that an
         * unexpected negative lgpio result means radio-I/O integrity is gone;
         * swallowing it here would have been the one place that quietly broke
         * that rule. */
        if (lgGpioFree(_ownGpioHandle, (int)interruptNum) < 0)
            gpio_failure("lgGpioFree failed");

        if (lgGpioSetAlertsFunc(_ownGpioHandle, (int)interruptNum,
                                NULL, NULL) < 0)
            gpio_failure("lgGpioSetAlertsFunc (detach) failed");
    }

    /* True until a GPIO operation failed during startup. The daemon checks
     * this after begin(), after installing the RX alert and before READY. */
    bool gpio_startup_ok() const { return !_gpioStartupFailed; }

    /* Called once the radio is fully up (begin + IRQ installed + RX armed).
     * From here a GPIO failure is loss of radio I/O integrity, not a
     * configuration problem, and is fatal the restartable way. */
    void gpio_mark_operational() { _gpioOperational = true; }

  private:
    bool gpio_ready(const char *what) {
        if (_ownGpioHandle >= 0)
            return true;
        char why[96];
        snprintf(why, sizeof(why), "%s without an open GPIO handle", what);
        gpio_failure(why);
        return false;
    }

    /* Startup: latch and return, so initialisation fails cleanly and the
     * daemon can exit non-restartable instead of looping every two seconds.
     * Operational: identical treatment to a failed SPI transfer. */
    void gpio_failure(const char *why) {
        if (_gpioOperational)
            fatal(why);
        fprintf(stderr, "[GPIO] startup error: %s\n", why);
        _gpioStartupFailed = true;
    }

    /* Runtime fatal: every fatal in this HAL fires AFTER operation began -- a
     * transfer without the lock or without a handle, a bus error, a
     * wedged-peer timeout, a hard un/lock failure, or a GPIO call that failed
     * once the radio was live. Exit 5 — distinct from the startup
     * prerequisite code 4 — so systemd's Restart=on-failure may restart, while
     * codes 3/4 stay non-restartable.
     *
     * The tag says RADIO and not SPI: GPIO failures come through here too, and
     * labelling those "[SPI] FATAL" sent operators looking at the wrong bus. */
    [[noreturn]] static void fatal(const char *why) {
        fprintf(stderr,
                "[RADIO] FATAL: %s - aborting to prevent unsynchronised "
                "radio I/O\n", why);
        fflush(stderr);
        _exit(LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR);
    }

    int _ownSpiHandle = -1;
    uint8_t _ownSpiDevice;
    uint8_t _ownSpiChannel;
    uint32_t _ownSpiSpeed;

    int _ownGpioHandle = -1;
    uint8_t _ownGpioDevice;
    bool _gpioStartupFailed = false;
    bool _gpioOperational = false;

    void open_lock() {
        /* Validate the trusted lock directory, then create spi0.lock relative to
         * it. No insecure /tmp fallback and no silent creation of an untrusted
         * production directory: if the trusted path cannot be used we fail closed
         * (spi_lock_ready() stays false). */
        int dirfd = loraham_open_runtime_dir();
        if (dirfd < 0)
            return;

        _lockFd = loraham_open_lock_file_at(dirfd, "spi0.lock");
        close(dirfd);

        if (_lockFd >= 0)
            fprintf(stderr, "[SPI] SPI lock file: %s/spi0.lock\n",
                    loraham_runtime_dir());
    }

    loraham_flock_fn _flock;
    int _lockFd = -1;
    int _depth = 0;
    bool _held = false;
};

#endif
