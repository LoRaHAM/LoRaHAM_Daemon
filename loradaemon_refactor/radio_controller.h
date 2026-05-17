#ifndef LORAHAM_RADIO_CONTROLLER_H
#define LORAHAM_RADIO_CONTROLLER_H

#include "radio_channel.h"
#include "radio_health.h"

class PiHal;

/* --- Radio hardware/runtime state --------------------------------------- */

template<typename RadioT>
struct RadioController {
    RadioBand_t band;
    const char *tag;
    bool is_hf;

    PiHal *hal;
    Module *mod;
    RadioT *radio;

    volatile RadioHealth health;
    volatile RadioMode_t mode;
    volatile bool received;
    volatile bool tx_busy;
    volatile bool cad_active;
    volatile bool getrssi_active;

    unsigned long rx_drops;

    void (*rx_callback)(void);

    int led_pin;
};

template<typename RadioT>
static inline void radio_controller_init(RadioController<RadioT> *ctrl,
                                         RadioBand_t band,
                                         const char *tag,
                                         bool is_hf,
                                         void (*rx_callback)(void),
                                         int led_pin)
{
    if (!ctrl)
        return;

    ctrl->band = band;
    ctrl->tag = tag;
    ctrl->is_hf = is_hf;

    ctrl->hal = nullptr;
    ctrl->mod = nullptr;
    ctrl->radio = nullptr;

    ctrl->health = RADIO_HEALTH_UNINITIALIZED;
    ctrl->mode = RADIO_MODE_LORA;
    ctrl->received = false;
    ctrl->tx_busy = false;
    ctrl->cad_active = false;
    ctrl->getrssi_active = false;

    ctrl->rx_drops = 0;

    ctrl->rx_callback = rx_callback;
    ctrl->led_pin = led_pin;
}

#endif
