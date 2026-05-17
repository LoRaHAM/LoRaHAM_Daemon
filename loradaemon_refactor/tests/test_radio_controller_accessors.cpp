#include <assert.h>
#include <math.h>
#include <string.h>

#include "radio_controller.h"

struct TestRadio {
    float getRSSI(void)
    {
        return -87.25f;
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

    *health = RADIO_HEALTH_READY;
    assert(radio_controller_health(&ctrl) == RADIO_HEALTH_READY);
    assert(radio_controller_ready(&ctrl));

    ctrl.radio = &radio;
    assert(fabs(radio_controller_packet_rssi(&ctrl) - (-87.25f)) < 0.001f);

    ctrl.radio = nullptr;
    assert(radio_controller_packet_rssi(&ctrl) == -200.0f);

    assert(radio_controller_band_number<TestRadio>(nullptr) == 0);
    assert(strcmp(radio_controller_tag<TestRadio>(nullptr), "?") == 0);
    assert(radio_controller_health<TestRadio>(nullptr) == RADIO_HEALTH_FAILED);
    assert(!radio_controller_ready<TestRadio>(nullptr));
    assert(radio_controller_mode<TestRadio>(nullptr) == RADIO_MODE_LORA);
    assert(radio_controller_packet_rssi<TestRadio>(nullptr) == -200.0f);

    return 0;
}
