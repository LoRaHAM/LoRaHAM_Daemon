#ifndef LORAHAM_TEST_FAKE_LGPIO_H
#define LORAHAM_TEST_FAKE_LGPIO_H

/*
 * Stand-in for <lgpio.h>, so a test can build and run where liblgpio-dev is not
 * installed and can supply its own bodies for every call — including failing
 * ones. Put `tests/fakes` on the include path AHEAD of the system headers and
 * do not link -llgpio; the test then owns the hardware's behaviour completely.
 *
 * THIS MUST MATCH THE REAL HEADER. Every declaration and constant below was
 * read from /usr/include/lgpio.h on the target (Raspberry Pi OS trixie) rather
 * than from documentation or memory: a fake that disagrees with the real API
 * gives a green test and different hardware, which is the exact failure this
 * whole work exists to remove. In particular `gpio` is `int`, not `unsigned`,
 * and lgSpiXfer's tx buffer is `const char *`.
 *
 * lgpio's own convention: every function returning int returns < 0 on error.
 * That matters here because RadioLib's PiHal returns lgGpioRead()'s int through
 * a uint32_t, so a negative code becomes a large positive "logic high".
 */

#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>   /* the real header includes this; PiHal relies on sched_yield() from it */

/*
 * The real header defines this, and RadioLib's PiHal tests it: below 0x00020200
 * it emits a #warning and compiles a reduced pull-up path. The target
 * (Raspberry Pi OS trixie) has exactly 0x00020200, so match it — otherwise the
 * test would compile a DIFFERENT PiHal than the box runs, which is the whole
 * failure mode this fake is supposed to avoid. `inttypes.h` is here for the
 * same reason: PiHal uses PRIu32 and gets it transitively from the real header.
 */
#ifndef LGPIO_VERSION
#define LGPIO_VERSION 0x00020200
#endif

/* Error codes (lgpio.h lines 2878-2953) */
#define LG_BAD_HANDLE            -5
#define LG_NOT_PERMITTED         -7
#define LG_GPIO_IN_USE          -11
#define LG_BAD_GPIO_NUMBER      -73
#define LG_BAD_READ             -76
#define LG_BAD_WRITE            -77
#define LG_GPIO_NOT_ALLOCATED   -80

/* Line and edge flags (lgpio.h lines 286-308) */
#define LG_RISING_EDGE         1
#define LG_FALLING_EDGE        2
#define LG_BOTH_EDGES          3
#define LG_SET_ACTIVE_LOW      4
#define LG_SET_PULL_UP        32
#define LG_SET_PULL_DOWN      64
#define LG_SET_PULL_NONE     128
#define LG_SET_INPUT         512
#define LG_LOW                 0
#define LG_HIGH                1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lgGpioReport_s
{
   uint64_t timestamp;   /* alert time in nanoseconds */
   uint8_t  chip;        /* gpiochip device number */
   uint8_t  gpio;        /* offset into gpio device */
   uint8_t  level;       /* 0=low, 1=high, 2=watchdog */
   uint8_t  flags;       /* none defined, ignore report if non-zero */
} lgGpioReport_t;

typedef struct lgGpioAlert_s
{
   lgGpioReport_t report;
   int nfyHandle;
} lgGpioAlert_t, *lgGpioAlert_p;

typedef void (*lgGpioAlertsFunc_t)(int num_alerts,
                                   lgGpioAlert_p alerts,
                                   void *userdata);

int lgGpiochipOpen(int gpioDev);
int lgGpiochipClose(int handle);

int lgGpioClaimInput(int handle, int lFlags, int gpio);
int lgGpioClaimOutput(int handle, int lFlags, int gpio, int level);
int lgGpioClaimAlert(int handle, int lFlags, int eFlags, int gpio, int nfyHandle);
int lgGpioFree(int handle, int gpio);
int lgGpioRead(int handle, int gpio);
int lgGpioWrite(int handle, int gpio, int level);
int lgGpioSetAlertsFunc(int handle, int gpio, lgGpioAlertsFunc_t cbf, void *userdata);

int lgTxPwm(int handle, int gpio, float pwmFrequency,
            float pwmDutyCycle, int pwmOffset, int pwmCycles);

int lgSpiOpen(int spiDev, int spiChan, int spiBaud, int spiFlags);
int lgSpiClose(int handle);
int lgSpiXfer(int handle, const char *txBuf, char *rxBuf, int count);

const char *lguErrorText(int error);
void        lguSleep(double sleepSecs);
uint64_t    lguTimestamp(void);

#ifdef __cplusplus
}
#endif

#endif
