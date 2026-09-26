#ifndef LORAHAM_DAEMON_LED_H
#define LORAHAM_DAEMON_LED_H

/* --- LED/GPIO helpers ---------------------------------------------------- */

/* Default (legacy-profile) LED lines per band; the band descriptor carries
 * the matching default and the hardware profile may override it via
 * daemon_led_configure(), including < 0 = no LED (feature disabled). */
#define DAEMON_LED_PIN_433 13
#define DAEMON_LED_PIN_868 19

/* Override the LED line of this process's band before daemon_led_init();
 * < 0 disables the LED (no claim, writes become no-ops, init stays healthy). */
void daemon_led_configure(int pin);
int daemon_led_pin_configured(void);

int daemon_led_init(void);
void daemon_led_shutdown(void);
int daemon_led_ready(void);
void daemon_led_set_pin(int pin, int state);

#endif
