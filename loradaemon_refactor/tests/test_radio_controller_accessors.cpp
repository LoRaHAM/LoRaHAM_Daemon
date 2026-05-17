#include <assert.h>
#include <math.h>
#include <string.h>

#include "radio_controller.h"

struct TestRadio {
    float getRSSI(void)
    {
        return -87.25f;
    }

    int getModemStatus(void)
    {
        return 0x11;
    }
};

static void test_callback(void)
{
}

int main(void)
{
    RadioController<TestRadio> ctrl;
    TestRadio radio;

    radio_controller_init(&ctrl,
                          RADIO_BAND_868,
                          "868",
                          true,
                          test_callback,
                          19);

    assert(radio_controller_band_number(&ctrl) == 868);
    assert(strcmp(radio_controller_tag(&ctrl), "868") == 0);
    assert(radio_controller_mode(&ctrl) == RADIO_MODE_LORA);
    assert(radio_controller_health(&ctrl) == RADIO_HEALTH_UNINITIALIZED);
    assert(!radio_controller_ready(&ctrl));

    volatile RadioHealth *health = radio_controller_health_ptr(&ctrl);
    assert(health == &ctrl.health);

    RadioControllerTxView tx_view = radio_controller_tx_view(&ctrl);
    assert(tx_view.band == 868);
    assert(strcmp(tx_view.tag, "868") == 0);
    assert(tx_view.health == &ctrl.health);
    assert(tx_view.mode == &ctrl.mode);
    assert(tx_view.modem_status_ctx == &ctrl);
    assert(tx_view.read_modem_status != nullptr);

    *health = RADIO_HEALTH_READY;
    assert(radio_controller_health(&ctrl) == RADIO_HEALTH_READY);
    assert(radio_controller_ready(&ctrl));

    ctrl.radio = &radio;
    assert(fabs(radio_controller_packet_rssi(&ctrl) - (-87.25f)) < 0.001f);
    assert(tx_view.read_modem_status(tx_view.modem_status_ctx) == 0x11);

    ctrl.radio = nullptr;
    assert(radio_controller_packet_rssi(&ctrl) == -200.0f);

    assert(radio_controller_band_number<TestRadio>(nullptr) == 0);
    assert(strcmp(radio_controller_tag<TestRadio>(nullptr), "?") == 0);
    assert(radio_controller_health<TestRadio>(nullptr) == RADIO_HEALTH_FAILED);
    assert(!radio_controller_ready<TestRadio>(nullptr));
    assert(radio_controller_mode<TestRadio>(nullptr) == RADIO_MODE_LORA);
    assert(radio_controller_packet_rssi<TestRadio>(nullptr) == -200.0f);

    RadioControllerTxView null_tx_view = radio_controller_tx_view<TestRadio>(nullptr);
    assert(null_tx_view.band == 0);
    assert(strcmp(null_tx_view.tag, "?") == 0);
    assert(null_tx_view.health == nullptr);
    assert(null_tx_view.mode == nullptr);
    assert(null_tx_view.modem_status_ctx == nullptr);
    assert(null_tx_view.read_modem_status != nullptr);
    assert(null_tx_view.read_modem_status(null_tx_view.modem_status_ctx) == 0);

    return 0;
}
