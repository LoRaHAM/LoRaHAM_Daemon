#ifndef LORAHAM_RADIO_CONTROLLER_H
#define LORAHAM_RADIO_CONTROLLER_H

#include <memory>

#include "hal/RPi/PiHal.h"
#include <RadioLib.h>

#include "radio_channel.h"
#include "radio_health.h"

/* --- Radio hardware/runtime state --------------------------------------- */

template<typename RadioT>
struct RadioController {
    RadioBand_t band;
    const char *tag;
    bool is_hf;

    std::unique_ptr<PiHal> hal;
    std::unique_ptr<Module> mod;
    std::unique_ptr<RadioT> radio;

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

    ctrl->hal.reset();
    ctrl->mod.reset();
    ctrl->radio.reset();

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

template<typename RadioT>
static inline int radio_controller_band_number(const RadioController<RadioT> *ctrl)
{
    return ctrl ? (int)ctrl->band : 0;
}

template<typename RadioT>
static inline const char *radio_controller_tag(const RadioController<RadioT> *ctrl)
{
    if (!ctrl || !ctrl->tag)
        return "?";

    return ctrl->tag;
}

template<typename RadioT>
static inline volatile RadioHealth *radio_controller_health_ptr(RadioController<RadioT> *ctrl)
{
    return ctrl ? &ctrl->health : nullptr;
}

template<typename RadioT>
static inline RadioHealth radio_controller_health(const RadioController<RadioT> *ctrl)
{
    return ctrl ? ctrl->health : RADIO_HEALTH_FAILED;
}

template<typename RadioT>
static inline bool radio_controller_ready(const RadioController<RadioT> *ctrl)
{
    return radio_health_is_ready(radio_controller_health(ctrl));
}

template<typename RadioT>
static inline RadioMode_t radio_controller_mode(const RadioController<RadioT> *ctrl)
{
    return ctrl ? ctrl->mode : RADIO_MODE_LORA;
}

template<typename RadioT>
static inline float radio_controller_packet_rssi(RadioController<RadioT> *ctrl)
{
    if (!ctrl || !ctrl->radio)
        return -200.0f;

    return ctrl->radio->getRSSI();
}

#endif
