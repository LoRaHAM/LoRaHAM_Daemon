#include <assert.h>
#include <string.h>

#include "radio_controller.h"

struct TestRadio {
};

static bool callback_called = false;

static void test_callback(void)
{
    callback_called = true;
}

int main(void)
{
    RadioController<TestRadio> ctrl;

    radio_controller_init(&ctrl,
                          RADIO_BAND_433,
                          "433",
                          false,
                          test_callback,
                          13);

    assert(ctrl.band == RADIO_BAND_433);
    assert(strcmp(ctrl.tag, "433") == 0);
    assert(ctrl.is_hf == false);

    assert(ctrl.hal == nullptr);
    assert(ctrl.mod == nullptr);
    assert(ctrl.radio == nullptr);

    assert(ctrl.health == RADIO_HEALTH_UNINITIALIZED);
    assert(ctrl.mode == RADIO_MODE_LORA);
    assert(ctrl.received == false);
    assert(ctrl.tx_busy == false);
    assert(ctrl.cad_active == false);
    assert(ctrl.getrssi_active == false);
    assert(ctrl.rx_drops == 0);

    assert(ctrl.rx_callback == test_callback);
    assert(ctrl.led_pin == 13);

    ctrl.rx_callback();
    assert(callback_called == true);

    return 0;
}
