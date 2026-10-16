#ifndef LORAHAM_RADIO_WIRING_H
#define LORAHAM_RADIO_WIRING_H

#include <RadioLib.h>

#include "hardware_profile.h"
#include "locking_pihal.h"

/*
 * The radio's SPI wiring as RadioLib sees it, from the resolved profile: the
 * spidev channel the HAL opens and the pins Module() drives. With kernel chip
 * selects the CS argument is NC (the kernel holds CS for each transfer). One
 * place, so the daemon and its test build exactly the same objects.
 */

static inline uint32_t hw_pin_or_nc(int pin)
{
    return pin < 0 ? RADIOLIB_NC : (uint32_t)pin;
}

static inline LockingPiHal *daemon_radio_hal(const DaemonHardwareProfile &p,
                                             loraham_flock_fn flock_fn = flock)
{
    return new LockingPiHal((uint8_t)p.spi_channel, 2000000, 0, 0, flock_fn);
}

static inline Module *daemon_radio_module(RadioLibHal *hal, const DaemonHardwareProfile &p)
{
    return new Module(hal, p.kernel_cs ? RADIOLIB_NC : hw_pin_or_nc(p.cs),
                      hw_pin_or_nc(p.irq), hw_pin_or_nc(p.rst), hw_pin_or_nc(p.gpio));
}

#endif
