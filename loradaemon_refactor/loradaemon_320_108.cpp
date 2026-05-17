/******************************************************************************
 * Copyright (C) 2026  [LoRaHAM / Alexander Walter]
 * * LICENSE: GNU General Public License v3 (GPLv3) with the following terms:
 * 1. PRIVATE/HOBBY: Free use, modification, and redistribution for non-commercial
 * purposes is permitted.
 * 2. COMMERCIAL: Commercial or business use is STRICTLY PROHIBITED unless a
 * written license is obtained from the author for a fee (Dual-Licensing).
 * [CONTACT: loraham.de Email Contact]
 * 3. CODE MAINTENANCE: Any modifications to this code must be reported to the
 * author (preferably via Pull Request on GitHub).
 * 4. REDISTRIBUTION: Binaries may only be distributed alongside the full
 * source code (Copyleft) (Copyleft).
 * * --- DISCLAIMER OF WARRANTY & LIMITATION OF LIABILITY ---
 * THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE
 * PROGRAM IS WITH THE USER.
 *****************************************************************************/

/*
 *
 *  ----
 *  sudo apt update
 *  sudo apt install g++ make cmake build-essential
 *  sudo apt install liblgpio-dev
 *
 *  git clone https://github.com/jgromes/RadioLib ~/RadioLib
 *
 *  cd ~/RadioLib
 *  mkdir build/
 *  cd build
 *  cmake ..
 *  sudo make install
 *
 *  sudo apt install socat
 *
 *  g++ -std=c++11 -O2 -o loraham_daemon loradaemon_305d.cpp \
 *  -I/home/raspberry/RadioLib/src \
 *  -I/home/raspberry/RadioLib/src/hal \
 *  -I/home/raspberry/RadioLib/src/modules \
 *  -I/home/raspberry/RadioLib/src/protocols/PhysicalLayer \
 *  /home/raspberry/RadioLib/build/libRadioLib.a \
 *  -llgpio -lpthread
 *  ----
 *
   g++ -o loraham_daemon loradaemon_320_108.cpp -I/home/raspberry/RadioLib/src -I/home/raspberry/RadioLib/src/modules \
   -I/home/raspberry/RadioLib/src/protocols/PhysicalLayer /home/raspberry/RadioLib/build/libRadioLib.a -llgpio
 *
 *
 *  Test-Kommandos an den Socket (erfordert socat : sudo apt install socat) :
 *  # LoRa 433
 *
 *  #LoRa-APRS:
 *  echo "SET FREQ=433.900 SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
 *
 *  echo "SET FREQ=433.900 SF=11 BW=125 CR=5 CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=AUTO POWER=10" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
 *
 *  # FSK 433 aktivieren (MODE=FSK ist Pflicht!):
 *  echo "SET MODE=FSK FREQ=433.775 BR=4.8 FREQDEV=5.0 RXBW=12.5 POWER=10" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
 *
 *  # FSK mit OOK (OOK=1 aktiviert OOK-Modus statt FSK):
 *  echo "SET MODE=FSK FREQ=433.920 BR=1.2 FREQDEV=0.0 RXBW=6.3 OOK=1 POWER=10" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
 *
 *  # Zurück zu LoRa (MODE=LORA re-initialisiert das Modul):
 *  echo "SET MODE=LORA FREQ=433.900 SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
 *
 *  # FSK 868 aktivieren:
 *  echo "SET MODE=FSK FREQ=869.525 BR=9.6 FREQDEV=10.0 RXBW=25.0 POWER=10" | socat - UNIX-CONNECT:/tmp/loraconf868.sock
 *
 *
 *  # nur Frequenz ändern (433)
 *  echo "SET FREQ=433.775" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
 *  echo "FFFFFFFF67452301FFBFC1E80700000028505C33DC22F5051C" | perl -pe 's/([0-9A-Fa-f]{2})/chr(hex($1))/ge' | socat - UNIX-CONNECT:/tmp/lora868.sock
 *  echo -n "$(printf '%02X' 8)Der Text" | perl -pe 's/^([0-9A-Fa-f]{2})|./$1 ? chr(hex($1)) : $&/ge' | socat - UNIX-CONNECT:/tmp/lora433.sock
 *
 *  # LoRa 868
 *  echo "SET FREQ=869.525 SF=11 BW=250 CR=5 CRC=1 PREAMBLE=16 SYNC=0x2B LDRO=1 POWER=10" | socat - UNIX-CONNECT:/tmp/loraconf868.sock
 *  # nur Power ändern (868)
 *  echo "SET POWER=5" | socat - UNIX-CONNECT:/tmp/loraconf868.sock
 *
 *  echo "FFFFFFFF67452301FFBFC1E80700000028505C33DC22F5051C" | perl -pe 's/([0-9A-Fa-f]{2})/chr(hex($1))/ge' | socat - UNIX-CONNECT:/tmp/lora868.sock
 *
 *  0: LDRO aus (Normalbetrieb)
 *  1: LDRO an (Optimierung zwingend erforderlich)
 *  BW (kHz) \ SF,SF7,SF8,SF9,SF10,SF11,SF12
 *  7.8,1,1,1,1,1,1
 *  10.4,1,1,1,1,1,1
 *  15.6,0,1,1,1,1,1
 *  20.8,0,0,1,1,1,1
 *  31.25,0,0,1,1,1,1
 *  41.7,0,0,0,1,1,1
 *  62.5,0,0,0,1,1,1
 *  125.0,0,0,0,0,1,1
 *  250.0,0,0,0,0,0,1
 *  500.0,0,0,0,0,0,0
 *
    echo "SET MODE=FSK FREQ=439.588 BR=9.6 FREQDEV=3.0 RXBW=20.8 SHAPING=0.5 POWER=10" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
    printf '\xFF\x01DC2WA-4>APRS,DB0ARD*:!4828.19N/00956.44E-LoRaHAM Pi Chat | loraham.de' | socat - UNIX-CONNECT:/tmp/lora433.sock

    2k4
    echo "SET MODE=FSK FREQ=433.775 BR=2.4 FREQDEV=5.0 RXBW=25.0 SHAPING=0.5 SYNC=0x2DD4 PREAMBLE=32 POWER=10" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
    4k8
    echo "SET MODE=FSK FREQ=431.300 BR=4.8 FREQDEV=2.2 RXBW=20.8 SHAPING=1.0 POWER=20" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
    
    
    100k
    echo "SET MODE=FSK FREQ=433.775 BR=100 FREQDEV=10.0 RXBW=250.0 SHAPING=0.5 SYNC=0x2DD4 PREAMBLE=32 POWER=10" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
 *
 *  RSSI-Streaming (10 Hz, kontinuierlich) aktivieren / deaktivieren:
 *  Ausgabe-Format: "RSSI=-87.50\n" auf demselben Conf-Socket parallel zu CAD=...
 *  Funktioniert in LoRa- UND FSK-Modus, erkennt auch Nicht-LoRa-Signale
 *  (Funkkopfhoerer, ISM-Sender, Stoerquellen).
 *  echo "SET GETRSSI=1" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
 *  echo "SET GETRSSI=0" | socat - UNIX-CONNECT:/tmp/loraconf433.sock
 *  echo "SET GETRSSI=1" | socat - UNIX-CONNECT:/tmp/loraconf868.sock
 *  echo "SET GETRSSI=0" | socat - UNIX-CONNECT:/tmp/loraconf868.sock
 *  Auto-Stop: sobald kein Conf-Client mehr verbunden ist, stoppt der Stream
 *  und muss nach Reconnect mit GETRSSI=1 erneut aktiviert werden.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <getopt.h>
#include <stdarg.h>
#include <errno.h>

#include "hal/RPi/PiHal.h"
#include <RadioLib.h>
#include <lgpio.h>

#include "daemon_protocol.h"
#include "daemon_timing.h"
#include "daemon_lifecycle.h"
#include "daemon_log.h"
#include "data_tx.h"
#include "tx_result.h"
#include "radio_health.h"
#include "rf_packet.h"
#include "event_loop.h"
#include "unix_socket.h"
#include "client_set.h"
#include "client_slot.h"
#include "radio_channel.h"
#include "radio_controller.h"
#include "config_dispatch.h"
#include "config_stream.h"

/* --- Global socket/client state ----------------------------------------- */
// Globale Socket- und Client-Zustände
int data433_fd = -1, data868_fd = -1;
int conf433_fd = -1, conf868_fd = -1;

ClientSlot client_data433_slots[MAX_CLIENTS];
ClientSlot client_data868_slots[MAX_CLIENTS];
ClientSlot client_conf433_slots[MAX_CLIENTS];
ClientSlot client_conf868_slots[MAX_CLIENTS];


/* --- Channel IO state ---------------------------------------------------- */
// Kanal-Zustand für Socket- und Clientverwaltung
RadioChannelIo channel_433;
RadioChannelIo channel_868;


static void daemon_radio_shutdown_cleanup(void);

static void daemon_shutdown_cleanup(EventLoopSet *event_set)
{
    daemon_debug_ctx("LIFE", "Stoppe Funkmodule");
    daemon_radio_shutdown_cleanup();

    daemon_debug_ctx("LIFE", "Schließe Event-Backend");
    event_loop_close(event_set);
    daemon_debug_ctx("LIFE", "Schließe Clients");
    client_slot_close_all(client_data433_slots, MAX_CLIENTS);
    client_slot_close_all(client_data868_slots, MAX_CLIENTS);
    client_slot_close_all(client_conf433_slots, MAX_CLIENTS);
    client_slot_close_all(client_conf868_slots, MAX_CLIENTS);

    close_unix_socket(&data433_fd, DATA433_SOCKET);
    close_unix_socket(&data868_fd, DATA868_SOCKET);
    close_unix_socket(&conf433_fd, CONF433_SOCKET);
    close_unix_socket(&conf868_fd, CONF868_SOCKET);
    daemon_debug_ctx("LIFE", "Entferne Socket-Dateien");

}

static int daemon_wait_for_events(EventLoopSet *event_set,
                                  EventLoopReadySet *readfds)
{
    int ret;

    event_loop_reset(event_set);
    radio_channel_add_fds(&channel_433, event_set);
    radio_channel_add_fds(&channel_868, event_set);

    ret = event_loop_wait(event_set, readfds, DAEMON_EVENT_LOOP_TIMEOUT_USEC);
    if (ret > 0)
        daemon_debug_ctx("SOCKET", "%d Event(s)", ret);

    return ret;
}

static void daemon_runtime_init(EventLoopSet *event_set)
{
    // Event backend.
    if (event_loop_init(event_set) != 0)
        perror("epoll");
    printf("[Daemon] Event-Backend: %s\n",
           event_loop_backend_name(event_loop_backend(event_set)));

    // Stop signals.
    daemon_lifecycle_reset_stop();
    if (daemon_lifecycle_install_signal_handlers() != 0)
        perror("sigaction");
}

static void daemon_enter_background(void)
{
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS); // Elternprozess beenden

    if (setsid() < 0) exit(EXIT_FAILURE); // Neue Session

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    chdir("/");

    // FIX: Deskriptoren nicht einfach schließen, sondern umleiten
    // Das verhindert, dass neue Sockets die IDs 0, 1 oder 2 einnehmen.
    freopen("/dev/null", "r", stdin);
    freopen("/tmp/lora_daemon.log", "w", stdout); // Optional: In Datei loggen
    freopen("/tmp/lora_daemon.log", "w", stderr);

    daemon_verbose_ctx("STARTUP", "Daemon-Modus aktiv");
}
/* --- Radio controller state ---------------------------------------------- */

// --- Pin-Setup für LED
#define PIN_433 13
#define PIN_868 19

void setFlag433(void);
void setFlag868(void);

RadioController<SX1278> radio_controller_433;
RadioController<RFM95> radio_controller_868;

int h = -1;
int chip = -1;

// --- Initialisierung der LEDs ---
void LED_init() {
    h = lgGpiochipOpen(0);
    chip = h; // chip bekommt den Wert von h

    if (chip < 0) {
        printf("Fehler: gpiochip0 konnte nicht geöffnet werden!\n");
        return;
    }
    /*
     *    if (lgGpioClaimOutput(chip, PIN_433, 0, 0) < 0)
     *        printf("Fehler: GPIO13 als Ausgang\n");
     *    if (lgGpioClaimOutput(chip, PIN_868, 0, 0) < 0)
     *        printf("Fehler: GPIO19 als Ausgang\n");
     */
}

// --- LED Routinen ---
void LED_433(int state) {
    if (chip >= 0)
        lgGpioWrite(chip, PIN_433, state ? 1 : 0);
}

void LED_868(int state) {
    if (chip >= 0)
        lgGpioWrite(chip, PIN_868, state ? 1 : 0);
}

template<typename RadioT>
static void radio_controller_led(RadioController<RadioT> *ctrl, int state)
{
    if (!ctrl || chip < 0)
        return;

    lgGpioWrite(chip, ctrl->led_pin, state ? 1 : 0);
}

template<typename RadioT>
static void radio_controller_flash_led(RadioController<RadioT> *ctrl)
{
    radio_controller_led(ctrl, 1);
    usleep(15000);
    radio_controller_led(ctrl, 0);
    usleep(15000);
}


/* --- RX drop statistics -------------------------------------------------- */
// Rate-limited counters for invalid RadioLib RX reads.
#define RX_DROP_LOG_INITIAL 5
#define RX_DROP_LOG_INTERVAL 100

static void daemon_radio_controller_init(void)
{
    daemon_debug_ctx("RADIO", "Controller initialisieren");

    radio_controller_init(&radio_controller_433,
                          RADIO_BAND_433,
                          "433",
                          false,
                          setFlag433,
                          PIN_433);
    daemon_debug_band("433", "Controller bereit");

    radio_controller_init(&radio_controller_868,
                          RADIO_BAND_868,
                          "868",
                          true,
                          setFlag868,
                          PIN_868);
    daemon_debug_band("868", "Controller bereit");
}

template<typename RadioT>
static void radio_controller_shutdown(RadioController<RadioT> *ctrl)
{
    const char *tag;

    if (!ctrl)
        return;

    tag = radio_controller_tag(ctrl);
    daemon_verbose_ctx(tag, "Radio-Shutdown");

    if (ctrl->radio) {
        if (radio_controller_ready(ctrl)) {
            daemon_debug_band(tag, "Callback aus");
            ctrl->radio->clearPacketReceivedAction();
            daemon_debug_band(tag, "Standby");
            ctrl->radio->standby();
            daemon_debug_band(tag, "IRQ löschen");
            ctrl->radio->clearIrq(0xFFFFFFFF);
        } else {
            daemon_debug_band(tag, "Radio nicht bereit");
        }

        daemon_debug_band(tag, "Radio freigeben");
        ctrl->radio.reset();
    } else {
        daemon_debug_band(tag, "Kein Radio-Objekt");
    }

    daemon_debug_band(tag, "Modul freigeben");
    ctrl->mod.reset();
    daemon_debug_band(tag, "HAL freigeben");
    ctrl->hal.reset();

    ctrl->health = RADIO_HEALTH_UNINITIALIZED;
    ctrl->received = false;
    ctrl->tx_busy = false;
    ctrl->cad_active = false;
    ctrl->getrssi_active = false;
    ctrl->rx_drops = 0;
    daemon_debug_band(tag, "Zustand zurückgesetzt");
}

static void daemon_radio_shutdown_cleanup(void)
{
    daemon_debug_ctx("RADIO", "Shutdown 433");
    radio_controller_shutdown(&radio_controller_433);
    daemon_debug_ctx("RADIO", "Shutdown 868");
    radio_controller_shutdown(&radio_controller_868);
}

// --- Callback für 868 ---
void setFlag868(void) {
    radio_controller_868.received = true;
    daemon_debug_ctx("RX868", "Flag gesetzt");
    radio_controller_led(&radio_controller_868, 1);
    //usleep(50000);
}
void setFlag433(void) {
    radio_controller_433.received = true;
    daemon_debug_ctx("RX433", "Flag gesetzt");
    radio_controller_led(&radio_controller_433, 1);
    //usleep(50000);
}

void setFlashFlag868(void) {
    radio_controller_flash_led(&radio_controller_868);
}
void setFlashFlag433(void) {
    radio_controller_flash_led(&radio_controller_433);
}

// --- LoRa senden (DEBUG + FIX) ---
static bool lora_send_valid_band(int band)
{
    return band == 433 || band == 868;
}

static RadioHealth daemon_radio_health(int band)
{
    if (!lora_send_valid_band(band))
        return RADIO_HEALTH_FAILED;

    if (band == 433)
        return radio_controller_health(&radio_controller_433);

    return radio_controller_health(&radio_controller_868);
}

static bool daemon_radio_ready(int band)
{
    if (!lora_send_valid_band(band))
        return false;

    if (band == 433)
        return radio_controller_ready(&radio_controller_433);

    return radio_controller_ready(&radio_controller_868);
}

static const char *lora_tx_log_ctx(int band)
{
    return band == 433 ? "TX433" : "TX868";
}

static void lora_debug_tx_preview(const char *ctx,
                                  const uint8_t *buf,
                                  size_t len)
{
    size_t preview_len = rf_packet_preview_len(len);
    char msg[512];
    size_t pos = 0;

    pos += snprintf(msg + pos, sizeof(msg) - pos, "%zu Byte: ", len);

    for(size_t i = 0; i < preview_len && pos < sizeof(msg); i++)
        pos += snprintf(msg + pos, sizeof(msg) - pos, "%c",
                        buf[i] >= 32 && buf[i] <= 126 ? buf[i] : '.');

    pos += snprintf(msg + pos, sizeof(msg) - pos, " HEX:");

    for(size_t i = 0; i < preview_len && pos < sizeof(msg); i++)
        pos += snprintf(msg + pos, sizeof(msg) - pos, " %02X", buf[i]);

    if (preview_len < len && pos < sizeof(msg))
        snprintf(msg + pos, sizeof(msg) - pos, " ...");

    daemon_debug_ctx(ctx, "%s", msg);
}

static void lora_debug_tx_first_bytes(const char *ctx,
                                      const uint8_t *buf,
                                      size_t len)
{
    size_t preview_len = len < 7 ? len : 7;
    char msg[160];
    size_t pos = 0;

    pos += snprintf(msg + pos, sizeof(msg) - pos, "Sende jetzt %zu Byte", len);
    if (preview_len > 0) {
        pos += snprintf(msg + pos, sizeof(msg) - pos, " (");
        for (size_t i = 0; i < preview_len && pos < sizeof(msg); i++) {
            pos += snprintf(msg + pos, sizeof(msg) - pos,
                            "%s0x%02X", i > 0 ? " " : "", buf[i]);
        }
        snprintf(msg + pos, sizeof(msg) - pos, ")");
    }

    daemon_debug_ctx(ctx, "%s", msg);
}

template<typename RadioT>
static void lora_send_prepare_controller_tx(RadioController<RadioT> *ctrl)
{
    ctrl->tx_busy = true;

    // WICHTIG: RX-Flag clearen und Callback deaktivieren!
    ctrl->received = false;

    // Im FSK-Modus keinen Callback clearen (wird anders behandelt)
    if (ctrl->mode == RADIO_MODE_LORA)
        ctrl->radio->clearPacketReceivedAction();

    // Radio komplett in Standby und zurücksetzen
    ctrl->radio->standby();
    ctrl->radio->clearIrq(0xFFFFFFFF);

    // TX-FIFO komplett zurücksetzen: Erst Sleep, dann Standby - löscht die FIFOs
    // NUR 433/LoRa: In FSK würde sleep() die Modul-Konfiguration zurücksetzen.
    if (ctrl->band == RADIO_BAND_433 && ctrl->mode == RADIO_MODE_LORA) {
        ctrl->radio->sleep();
        usleep(10000); // 10ms warten
        ctrl->radio->standby();
        usleep(10000); // nochmal 10ms
    }
}

template<typename RadioT>
static TxResult lora_send_controller(RadioController<RadioT> *ctrl,
                                     uint8_t *buf,
                                     size_t len)
{
    int band = radio_controller_band_number(ctrl);
    const char *tag = radio_controller_tag(ctrl);
    const char *tx_ctx = lora_tx_log_ctx(band);

    if (!ctrl || !ctrl->radio || !radio_controller_ready(ctrl)) {
        printf("[SEND %d] radio not ready: %s\n",
               band, radio_health_name(radio_controller_health(ctrl)));
        fflush(stdout);
        return TX_RESULT_RADIO_NOT_READY;
    }

    RfPacketValidation packet_state = rf_packet_validate(buf, len);
    if (packet_state != RF_PACKET_VALID) {
        printf("[SEND %d] invalid TX packet: %s (%zu bytes)\n",
               band, rf_packet_validation_message(packet_state), len);
        fflush(stdout);
        return TX_RESULT_INVALID_PACKET;
    }

    // WICHTIG: Buffer kopieren, damit er nicht überschrieben wird!
    uint8_t send_buf[RF_PACKET_MAX_PAYLOAD_LEN];
    memcpy(send_buf, buf, len);

    lora_debug_tx_preview(tx_ctx, send_buf, len);

    if(ctrl->tx_busy) {
        printf("[%s] TX BUSY - überspringen\n", tag);
        fflush(stdout);
        return TX_RESULT_BUSY;
    }

    lora_send_prepare_controller_tx(ctrl);

    /*
     *        // WICHTIG: LoRa-Parameter nochmal setzen (für TX!)
     *        radio_controller_433.radio->setFrequency(433.775);
     *        radio_controller_433.radio->setSpreadingFactor(12);
     *        radio_controller_433.radio->setBandwidth(125.0);
     *        radio_controller_433.radio->setCodingRate(5);
     *        radio_controller_433.radio->setSyncWord(0x12);
     *        radio_controller_433.radio->setPreambleLength(8);
     *        radio_controller_433.radio->setCRC(true);
     *        radio_controller_433.radio->explicitHeader();
     *        radio_controller_433.radio->setOutputPower(10);
     */
    if (ctrl->band == RADIO_BAND_433) {
        // Nochmal IRQs clearen
        ctrl->radio->clearIrq(0xFFFFFFFF);

        daemon_debug_ctx(tx_ctx, "Radio neu konfiguriert");
        lora_debug_tx_first_bytes(tx_ctx, send_buf, len);
    }

    // WICHTIG: transmit() ist blockierend und wartet bis fertig!
    int state = ctrl->radio->transmit(send_buf, len);

    if(state != RADIOLIB_ERR_NONE) {
        daemon_debug_ctx(tx_ctx, "transmit Fehler %d", state);
        if (ctrl->band == RADIO_BAND_433)
            printf("[433] transmit ERROR: %d\n", state);
        else
            printf("[868] TX ERROR: %d\n", state);
    } else {
        daemon_debug_ctx(tx_ctx, "transmit OK");
    }

    if (ctrl->band == RADIO_BAND_868)
        usleep(50000); // 50ms Sicherheitspause

    // Nach dem Senden: IRQ clearen und Callback wieder aktivieren
    // Callback wird für LoRa UND FSK neu gesetzt - transmit() kann ihn löschen!
    ctrl->radio->clearIrq(0xFFFFFFFF);
    ctrl->received = false;
    ctrl->radio->setPacketReceivedAction(ctrl->rx_callback);

    ctrl->tx_busy = false;
    ctrl->radio->startReceive();

    return state == RADIOLIB_ERR_NONE
        ? TX_RESULT_OK
        : TX_RESULT_RADIO_ERROR;
}

TxResult lora_send(uint8_t *buf, size_t len, int band) {
    if (!lora_send_valid_band(band)) {
        printf("[SEND %d] invalid band\n", band);
        fflush(stdout);
        return TX_RESULT_INVALID_BAND;
    }

    if (!daemon_radio_ready(band)) {
        printf("[SEND %d] radio not ready: %s\n",
               band, radio_health_name(daemon_radio_health(band)));
        fflush(stdout);
        return TX_RESULT_RADIO_NOT_READY;
    }

    if (band == 433)
        return lora_send_controller(&radio_controller_433, buf, len);

    return lora_send_controller(&radio_controller_868, buf, len);
}



/* --- CONFIG apply module ------------------------------------------------ */

// --- Init LoRa ---
void lora_init() {
    printf("[Init] Starte Dualband LoRa Receiver (433 + 868)\n");
    daemon_verbose_ctx("RADIO", "Funk-Init beginnt");

    radio_controller_433.health = RADIO_HEALTH_UNINITIALIZED;
    radio_controller_868.health = RADIO_HEALTH_UNINITIALIZED;
    daemon_debug_ctx("RADIO", "Health zurückgesetzt");

    if (h < 0) {
        printf("[GPIO] Fehler: gpiochip0 konnte nicht geöffnet werden!\n");
        daemon_debug_ctx("GPIO", "Nicht bereit");
        radio_controller_433.health = RADIO_HEALTH_FAILED;
        radio_controller_868.health = RADIO_HEALTH_FAILED;
        return;
    }

    radio_controller_led(&radio_controller_433, 1);

    daemon_debug_band("433", "Objekte anlegen");
    radio_controller_433.hal.reset(new PiHal(0));
    radio_controller_433.mod.reset(new Module(radio_controller_433.hal.get(), 8, 25, 5, 24));
    radio_controller_433.radio.reset(new SX1278(radio_controller_433.mod.get()));

    daemon_debug_band("868", "Objekte anlegen");
    radio_controller_868.hal.reset(new PiHal(0));
    radio_controller_868.mod.reset(new Module(radio_controller_868.hal.get(), 7, 16, 6, 12));
    radio_controller_868.radio.reset(new RFM95(radio_controller_868.mod.get()));

    daemon_debug_band("433", "begin()");
    int state_433 = radio_controller_433.radio->begin();
    if (state_433 == RADIOLIB_ERR_NONE) {
        radio_controller_433.health = RADIO_HEALTH_READY;
        printf("[433] Init OK\n");
        daemon_verbose_ctx("433", "Radio bereit");

        // LoRa-APRS:
        radio_controller_433.radio->setFrequency(433.900);
        radio_controller_433.radio->setSpreadingFactor(12);
        radio_controller_433.radio->setBandwidth(125.0);
        radio_controller_433.radio->setSyncWord(0x12);
        radio_controller_433.radio->setPreambleLength(8);
        radio_controller_433.radio->setCodingRate(5);
        radio_controller_433.radio->setCRC(true);
        radio_controller_433.radio->autoLDRO();
        radio_controller_433.radio->forceLDRO(1);
        radio_controller_433.radio->setOutputPower(10);

        /*
         *
         * // LoRa DX Cluster:
         *    radio_controller_433.radio->setFrequency(433.900);
         *    radio_controller_433.radio->setSpreadingFactor(10);
         *    radio_controller_433.radio->setBandwidth(125.0);
         *    radio_controller_433.radio->setSyncWord(0x12);
         *    radio_controller_433.radio->setPreambleLength(8);
         *    radio_controller_433.radio->setCodingRate(5);
         *    radio_controller_433.radio->setCRC(true);
         *    radio_controller_433.radio->autoLDRO();
         *    radio_controller_433.radio->setOutputPower(10);
         *
         *
         *
         * // Meshtastic 433:
         *
         *    radio_controller_433.radio->setFrequency(433.900); // DB0ARD
         *    radio_controller_433.radio->setSpreadingFactor(11);
         *    radio_controller_433.radio->setBandwidth(125.0);
         *    radio_controller_433.radio->setSyncWord(0x2B);
         *    radio_controller_433.radio->setPreambleLength(16);
         *    radio_controller_433.radio->setCodingRate(5);
         *    radio_controller_433.radio->setCRC(true);
         *    radio_controller_433.radio->autoLDRO();
         *    radio_controller_433.radio->setOutputPower(10);
         *
         *
         * // Meshcom:
         *
         *    radio_controller_433.radio->setFrequency(433.175);
         *    radio_controller_433.radio->setSpreadingFactor(11);
         *    radio_controller_433.radio->setBandwidth(250.0);
         *    radio_controller_433.radio->setSyncWord(0x2B);
         *    radio_controller_433.radio->setPreambleLength(8);
         *    radio_controller_433.radio->setCodingRate(6);
         *    radio_controller_433.radio->setCRC(true);
         *    //radio_controller_433.radio->autoLDRO();
         *    radio_controller_433.radio->setOutputPower(10);
         */
        daemon_debug_band("433", "LoRa-Default gesetzt");
        radio_controller_433.radio->setPacketReceivedAction(setFlag433); // Callback nutzen
        daemon_debug_band("433", "Callback gesetzt");
    } else {
        radio_controller_433.health = RADIO_HEALTH_FAILED;
        printf("[433] Init FEHLGESCHLAGEN: %d\n", state_433);
        daemon_debug_band("433", "begin() Fehler %d", state_433);
    }

    LED_433(0);

    LED_868(1);

    daemon_debug_band("868", "begin()");
    int state_868 = radio_controller_868.radio->begin();
    if (state_868 == RADIOLIB_ERR_NONE) {
        radio_controller_868.health = RADIO_HEALTH_READY;
        printf("[868] Init OK\n");
        daemon_verbose_ctx("868", "Radio bereit");

        radio_controller_868.radio->setFrequency(869.525);
        radio_controller_868.radio->setSpreadingFactor(11);
        radio_controller_868.radio->setBandwidth(250.0);
        radio_controller_868.radio->setSyncWord(0x2B);
        radio_controller_868.radio->setPreambleLength(16);
        radio_controller_868.radio->setCodingRate(5);
        radio_controller_868.radio->setCRC(true);
        radio_controller_868.radio->autoLDRO();
        radio_controller_868.radio->setOutputPower(10);

        daemon_debug_band("868", "LoRa-Default gesetzt");
        radio_controller_868.radio->setPacketReceivedAction(setFlag868); // Callback nutzen
        daemon_debug_band("868", "Callback gesetzt");
    } else {
        radio_controller_868.health = RADIO_HEALTH_FAILED;
        printf("[868] Init FEHLGESCHLAGEN: %d\n", state_868);
        daemon_debug_band("868", "begin() Fehler %d", state_868);
    }

    LED_868(0);

    if (radio_controller_ready(&radio_controller_433)) {
        daemon_debug_band("433", "RX starten");
        radio_controller_433.radio->startReceive();
    } else {
        printf("[433] RX nicht gestartet: %s\n",
               radio_health_name(radio_controller_433.health));
        daemon_debug_band("433", "RX Start übersprungen");
    }

    if (radio_controller_ready(&radio_controller_868)) {
        daemon_debug_band("868", "RX starten");
        radio_controller_868.radio->startReceive();
    } else {
        printf("[868] RX nicht gestartet: %s\n",
               radio_health_name(radio_controller_868.health));
        daemon_debug_band("868", "RX Start übersprungen");
    }

    daemon_verbose_ctx("RADIO", "Funk-Init abgeschlossen");
    fflush(stdout);
}


static void daemon_radio_io_init(void)
{
    daemon_debug_ctx("CLIENT", "Slots initialisieren");
    client_slot_init_all(client_data433_slots, MAX_CLIENTS);
    client_slot_init_all(client_data868_slots, MAX_CLIENTS);
    client_slot_init_all(client_conf433_slots, MAX_CLIENTS);
    client_slot_init_all(client_conf868_slots, MAX_CLIENTS);

    daemon_debug_ctx("RADIO", "Kanal-IO initialisieren");
    radio_channel_io_init(&channel_433,
                          RADIO_BAND_433,
                          DATA433_SOCKET,
                          CONF433_SOCKET,
                          &data433_fd,
                          &conf433_fd,
                          client_data433_slots,
                          client_conf433_slots);
    radio_channel_io_init(&channel_868,
                          RADIO_BAND_868,
                          DATA868_SOCKET,
                          CONF868_SOCKET,
                          &data868_fd,
                          &conf868_fd,
                          client_data868_slots,
                          client_conf868_slots);

    daemon_debug_ctx("SOCKET", "Socket-Dateien öffnen");
    radio_channel_open_sockets(&channel_433);
    radio_channel_open_sockets(&channel_868);

    daemon_radio_controller_init();

    daemon_debug_ctx("GPIO", "LED initialisieren");
    LED_init();

    daemon_debug_ctx("RADIO", "RadioLib initialisieren");
    lora_init();
}

/* --- DATA TX structure: context, CAD guard and send callback -------------- */

template<typename RadioT>
struct DataTxDaemonContext {
    RadioController<RadioT> *ctrl;
    const char *log_ctx;
};

static void daemon_data_tx_trace_message(void *ctx, const char *msg)
{
    daemon_debug_ctx((const char *)ctx, "%s", msg);
}

static DataTxLog daemon_data_tx_log(const char *ctx)
{
    DataTxLog log = {
        (void *)ctx,
        daemon_data_tx_trace_message
    };

    return log;
}

#define DATA_TX_CAD_MAX_WAIT_TICKS 300
#define DATA_TX_CAD_SLEEP_USEC 10000

/* --- DATA TX hardware helpers -------------------------------------------- */
template<typename RadioT>
static int data_tx_modem_status(DataTxDaemonContext<RadioT> *tx)
{
    RadioController<RadioT> *ctrl = tx->ctrl;

    if (!ctrl || !ctrl->radio || !radio_controller_ready(ctrl))
        return 0;

    return ctrl->radio->getModemStatus();
}

template<typename RadioT>
static int data_tx_wait_channel_free(DataTxDaemonContext<RadioT> *tx)
{
    RadioController<RadioT> *ctrl = tx->ctrl;
    int cad_wait = 0;

    if (!ctrl || ctrl->mode != RADIO_MODE_LORA)
        return 0;

    while (cad_wait < DATA_TX_CAD_MAX_WAIT_TICKS) {
        if ((data_tx_modem_status(tx) & 0x01) == 0)
            return 0;

        usleep(DATA_TX_CAD_SLEEP_USEC);
        cad_wait++;
    }

    return 1;
}

/* --- DATA TX send callback ----------------------------------------------- */
template<typename RadioT>
static int send_data_chunk(uint8_t *chunk, size_t len, size_t offset, void *ctx)
{
    DataTxDaemonContext<RadioT> *tx = (DataTxDaemonContext<RadioT> *)ctx;
    RadioController<RadioT> *ctrl = tx->ctrl;
    const char *tag = radio_controller_tag(ctrl);
    int band = radio_controller_band_number(ctrl);

    if (!radio_controller_ready(ctrl)) {
        daemon_debug_ctx(tx->log_ctx, "Radio nicht bereit");
        printf("[%s] DATA-TX abgebrochen: RADIO_NOT_READY\n", tag);
        return 1;
    }

    // CAD guard: LoRa only.
    if (ctrl->mode == RADIO_MODE_LORA)
        daemon_debug_ctx(tx->log_ctx, "CAD prüfen");

    if (data_tx_wait_channel_free(tx)) {
        daemon_debug_ctx(tx->log_ctx, "CAD Timeout");
        printf("[%s] CAD-Timeout: Kanal dauerhaft belegt, Paket verworfen\n", tag);
        printf("[%s] DATA-TX abgebrochen: %s\n", tag,
               tx_result_name(TX_RESULT_CAD_TIMEOUT));
        return 1;
    }

    daemon_debug_ctx(tx->log_ctx, "Chunk %zu Byte Offset %zu", len, offset);

    radio_controller_led(ctrl, 1);
    TxResult result = lora_send(chunk, len, band);
    radio_controller_led(ctrl, 0);

    if (!tx_result_is_ok(result)) {
        daemon_debug_ctx(tx->log_ctx, "Abbruch: %s", tx_result_name(result));
        printf("[%s] DATA-TX abgebrochen: %s\n", tag,
               tx_result_name(result));
        return 1;
    }

    daemon_debug_ctx(tx->log_ctx, "Chunk gesendet");
    return 0;
}


/* --- Runtime context factories ------------------------------------------- */

template<typename RadioT>
static DataTxDaemonContext<RadioT> daemon_data_tx_context(RadioController<RadioT> *ctrl)
{
    const char *log_ctx = "TX?";

    if (ctrl && ctrl->band == RADIO_BAND_433)
        log_ctx = "TX433";
    else if (ctrl && ctrl->band == RADIO_BAND_868)
        log_ctx = "TX868";

    DataTxDaemonContext<RadioT> ctx = {
        ctrl,
        log_ctx
    };

    return ctx;
}

static void daemon_config_trace_message(void *ctx, const char *msg)
{
    daemon_debug_ctx((const char *)ctx, "%s", msg);
}

static void daemon_config_trace_line(void *ctx,
                                     const char *msg,
                                     const char *line)
{
    daemon_debug_ctx((const char *)ctx, "%s: %s", msg, line ? line : "");
}

static ConfigDispatchLog daemon_config_log(const char *ctx)
{
    ConfigDispatchLog log = {
        (void *)ctx,
        daemon_config_trace_message,
        daemon_config_trace_line
    };

    return log;
}

static ConfigDispatchContext<SX1278> daemon_config_433_context(void)
{
    ConfigDispatchContext<SX1278> ctx = {
        client_conf433_slots,
        &radio_controller_433,
        "CONF433",
        config_apply_command<SX1278>,
        daemon_config_log("CONFIG433")
    };

    return ctx;
}

static ConfigDispatchContext<RFM95> daemon_config_868_context(void)
{
    ConfigDispatchContext<RFM95> ctx = {
        client_conf868_slots,
        &radio_controller_868,
        "CONF868",
        config_apply_command<RFM95>,
        daemon_config_log("CONFIG868")
    };

    return ctx;
}

/* --- Loop context --------------------------------------------------------- */
typedef struct {
    DaemonDeadlineTimer rssi_timer;
    DataTxDaemonContext<SX1278> data_tx_433_ctx;
    DataTxDaemonContext<RFM95> data_tx_868_ctx;
    ConfigDispatchContext<SX1278> config_433_ctx;
    ConfigDispatchContext<RFM95> config_868_ctx;
} DaemonLoopContext;

static void daemon_loop_context_init(DaemonLoopContext *ctx)
{
    // RSSI timer.
    daemon_deadline_timer_init(&ctx->rssi_timer,
                               daemon_now_ms(),
                               DAEMON_RSSI_INTERVAL_MS);

    // DATA TX contexts.
    ctx->data_tx_433_ctx = daemon_data_tx_context(&radio_controller_433);
    ctx->data_tx_868_ctx = daemon_data_tx_context(&radio_controller_868);

    // CONFIG client slots.
    client_slot_init_all(client_conf433_slots, MAX_CLIENTS);
    client_slot_init_all(client_conf868_slots, MAX_CLIENTS);

    // CONFIG contexts.
    ctx->config_433_ctx = daemon_config_433_context();
    ctx->config_868_ctx = daemon_config_868_context();
}

/* --- Main runtime context ------------------------------------------------ */
typedef struct {
    EventLoopSet event_set;
    EventLoopReadySet readfds;
    uint8_t buf[buf_SIZE];
    uint8_t rx_buf_433[buf_SIZE];  // ← GETRENNTE Buffer pro Band!
    uint8_t rx_buf_868[buf_SIZE];  // ← GETRENNTE Buffer pro Band!
    DaemonLoopContext loop_ctx;
} DaemonMainContext;

static void daemon_main_context_init(DaemonMainContext *ctx)
{
    daemon_debug_ctx("LIFE", "Initialisiere Laufzeitkontext");
    daemon_runtime_init(&ctx->event_set);
    daemon_loop_context_init(&ctx->loop_ctx);
    daemon_debug_ctx("LIFE", "Laufzeitkontext bereit");
}
/* --- CONFIG dispatch ----------------------------------------------------- */
static void process_config_dispatch(ConfigDispatchContext<SX1278> *config_433_ctx,
                                    ConfigDispatchContext<RFM95> *config_868_ctx,
                                    const EventLoopReadySet *readfds,
                                    uint8_t *buf)
{
    config_dispatch_context<SX1278>(config_433_ctx, MAX_CLIENTS, readfds, buf);
    config_dispatch_context<RFM95>(config_868_ctx, MAX_CLIENTS, readfds, buf);
}

/* --- Socket dispatch ----------------------------------------------------- */
static int daemon_client_slot_count(ClientSlot *slots, int max_clients)
{
    int count = 0;

    if (!slots)
        return 0;

    for (int i = 0; i < max_clients; i++) {
        if (client_slot_has_client(&slots[i]))
            count++;
    }

    return count;
}

static bool daemon_client_slots_output_ready(ClientSlot *slots,
                                             int max_clients,
                                             const EventLoopReadySet *readfds)
{
    if (!slots)
        return false;

    for (int i = 0; i < max_clients; i++) {
        if (client_slot_output_ready(&slots[i], readfds))
            return true;
    }

    return false;
}

static void daemon_log_accept_delta(const char *ctx,
                                    const char *kind,
                                    int before,
                                    int after)
{
    if (after > before)
        daemon_debug_ctx(ctx, "%s-Client verbunden (%d)", kind, after);
    else
        daemon_debug_ctx(ctx, "%s-Annahme ohne neuen Client", kind);
}

static void daemon_accept_channel_logged(RadioChannelIo *channel,
                                         const EventLoopReadySet *readfds,
                                         const char *ctx)
{
    bool data_ready = event_loop_ready_fd_read(readfds, *channel->data_listen_fd);
    bool conf_ready = event_loop_ready_fd_read(readfds, *channel->conf_listen_fd);
    int data_before = daemon_client_slot_count(channel->data_slots, MAX_CLIENTS);
    int conf_before = daemon_client_slot_count(channel->conf_slots, MAX_CLIENTS);

    if (data_ready)
        daemon_debug_ctx(ctx, "DATA-Annahme bereit");
    if (conf_ready)
        daemon_debug_ctx(ctx, "CONF-Annahme bereit");

    radio_channel_accept_ready(channel, readfds);

    if (data_ready) {
        int data_after = daemon_client_slot_count(channel->data_slots, MAX_CLIENTS);
        daemon_log_accept_delta(ctx, "DATA", data_before, data_after);
    }

    if (conf_ready) {
        int conf_after = daemon_client_slot_count(channel->conf_slots, MAX_CLIENTS);
        daemon_log_accept_delta(ctx, "CONF", conf_before, conf_after);
    }
}

static void daemon_flush_channel_logged(RadioChannelIo *channel,
                                        const EventLoopReadySet *readfds,
                                        const char *ctx)
{
    if (daemon_client_slots_output_ready(channel->data_slots, MAX_CLIENTS, readfds))
        daemon_debug_ctx(ctx, "DATA-Ausgabe bereit");

    if (daemon_client_slots_output_ready(channel->conf_slots, MAX_CLIENTS, readfds))
        daemon_debug_ctx(ctx, "CONF-Ausgabe bereit");

    radio_channel_flush_ready(channel, readfds);
}

static void daemon_process_ready_sockets(ConfigDispatchContext<SX1278> *config_433_ctx,
                                         ConfigDispatchContext<RFM95> *config_868_ctx,
                                         DataTxDaemonContext<SX1278> *data_tx_433_ctx,
                                         DataTxDaemonContext<RFM95> *data_tx_868_ctx,
                                         const EventLoopReadySet *readfds,
                                         uint8_t *buf)
{
    daemon_accept_channel_logged(&channel_433, readfds, "CLIENT433");
    daemon_accept_channel_logged(&channel_868, readfds, "CLIENT868");

    data_tx_process_slots("433", client_data433_slots, MAX_CLIENTS,
                          readfds, send_data_chunk<SX1278>, data_tx_433_ctx,
                          daemon_data_tx_log("TX433"));
    data_tx_process_slots("868", client_data868_slots, MAX_CLIENTS,
                          readfds, send_data_chunk<RFM95>, data_tx_868_ctx,
                          daemon_data_tx_log("TX868"));

    process_config_dispatch(config_433_ctx, config_868_ctx, readfds, buf);
    daemon_flush_channel_logged(&channel_433, readfds, "CLIENT433");
    daemon_flush_channel_logged(&channel_868, readfds, "CLIENT868");
}


/* --- CAD/RSSI/RX log contexts ------------------------------------------- */
template<typename RadioT>
static const char *daemon_cad_log_ctx(RadioController<RadioT> *ctrl)
{
    return (ctrl && ctrl->band == RADIO_BAND_433) ? "CAD433" : "CAD868";
}

template<typename RadioT>
static const char *daemon_rssi_log_ctx(RadioController<RadioT> *ctrl)
{
    return (ctrl && ctrl->band == RADIO_BAND_433) ? "RSSI433" : "RSSI868";
}

template<typename RadioT>
static const char *daemon_rx_log_ctx(RadioController<RadioT> *ctrl)
{
    return (ctrl && ctrl->band == RADIO_BAND_433) ? "RX433" : "RX868";
}

/* --- CAD status ---------------------------------------------------------- */
template<typename RadioT>
static void daemon_process_cad_status(RadioController<RadioT> *ctrl,
                                      RadioChannelIo *io)
{
    if (!radio_controller_ready(ctrl) || !ctrl->radio)
        return;

    if (ctrl->mode != RADIO_MODE_LORA)
        return;

    uint8_t modem = ctrl->radio->getModemStatus();
    bool hardware_active = (modem & 0x01) || (modem & 0x10);
    const char *ctx = daemon_cad_log_ctx(ctrl);

    if (hardware_active) {
        if (ctrl->band == RADIO_BAND_433)
            setFlashFlag433();
        else
            setFlashFlag868();

        if (!ctrl->cad_active) {
            daemon_debug_ctx(ctx, "Aktiv modem=0x%02X", modem);
            radio_controller_led(ctrl, 1);
            client_slot_broadcast_queued(io->conf_slots, MAX_CLIENTS, "CAD=1\n");
            ctrl->cad_active = true;
        }
    } else {
        if (ctrl->cad_active && !ctrl->received) {
            daemon_debug_ctx(ctx, "Inaktiv modem=0x%02X", modem);
            radio_controller_led(ctrl, 0);
            client_slot_broadcast_queued(io->conf_slots, MAX_CLIENTS, "CAD=0\n");
            ctrl->cad_active = false;
        }
    }
}
/* --- RSSI streaming ------------------------------------------------------- */
/*
 * GETRSSI Streaming: 10 Hz RSSI an Conf-Clients.
 *
 * Sendet "RSSI=-87.50\n" auf demselben Conf-Socket, auf dem
 * SET GETRSSI=1 empfangen wurde (loraconf433.sock bzw.
 * loraconf868.sock - identisches Pattern wie CAD=1/CAD=0).
 *
 * Quelle: read_live_rssi() liest das SX127x Hardware-Register
 * direkt per SPI (RegRssiValue 0x1B im LoRa-, 0x11 im FSK-Mode).
 * RadioLib's getRSSI() liefert im LoRa-Mode nur den RSSI des
 * LETZTEN Pakets, daher der Direkt-Read.
 *
 * Funktioniert in LoRa- UND FSK-Modus, da getRSSI() das
 * Analog-Frontend (RegRssiValue) ausliest. Detektiert daher auch
 * Nicht-LoRa-Signale (Funkkopfhoerer, ISM-Sender etc.).
 *
 * RX laeuft parallel weiter: SPI-Read von RegRssiValue unterbricht
 * weder Demodulation noch FIFO-Befuellung. Waehrend TX (txBusy)
 * wird KEIN RSSI gelesen, da das Register dann undefinierte Werte
 * liefert.
 *
 * Auto-Stop: sobald kein Conf-Client mehr verbunden ist, wird das
 * Flag geloescht. Reconnect erfordert erneutes SET GETRSSI=1.
 */
template<typename RadioT>
static void daemon_radio_controller_getrssi_autostop(RadioChannelIo *io,
                                                     RadioController<RadioT> *ctrl)
{
    if(!client_slot_has_clients(io->conf_slots, MAX_CLIENTS) && ctrl->getrssi_active) {
        const char *ctx = daemon_rssi_log_ctx(ctrl);

        ctrl->getrssi_active = false;
        daemon_debug_ctx(ctx, "Auto-Stop: kein Client");
    }
}

template<typename RadioT>
static void daemon_process_rssi_stream_one(RadioController<RadioT> *ctrl,
                                           RadioChannelIo *io)
{
    const char *ctx = daemon_rssi_log_ctx(ctrl);

    if (!ctrl->getrssi_active)
        return;

    if (ctrl->tx_busy) {
        daemon_debug_ctx(ctx, "TX aktiv, überspringe");
        return;
    }

    if (!radio_controller_ready(ctrl) || !ctrl->mod) {
        daemon_debug_ctx(ctx, "Radio nicht bereit");
        return;
    }

    float rssi = radio_channel_read_live_rssi(ctrl->mod.get(),
                                              ctrl->mode,
                                              ctrl->is_hf);
    char rssi_msg[32];
    snprintf(rssi_msg, sizeof(rssi_msg), "RSSI=%.2f\n", rssi);
    daemon_debug_ctx(ctx, "Sende %.2f dBm", rssi);
    client_slot_broadcast_queued(io->conf_slots, MAX_CLIENTS, rssi_msg);
}

static void daemon_process_rssi_stream(DaemonDeadlineTimer *rssi_timer)
{
    daemon_radio_controller_getrssi_autostop(&channel_433,
                                             &radio_controller_433);
    daemon_radio_controller_getrssi_autostop(&channel_868,
                                             &radio_controller_868);

    // RSSI-Takt ist zeitbasiert.
    if (daemon_deadline_timer_due(rssi_timer, daemon_now_ms())) {
        daemon_process_rssi_stream_one(&radio_controller_433, &channel_433);
        daemon_process_rssi_stream_one(&radio_controller_868, &channel_868);
    }
}

/* --- CAD/RSSI polling ---------------------------------------------------- */
static void daemon_process_cad_rssi(DaemonDeadlineTimer *rssi_timer)
{
    daemon_process_cad_status(&radio_controller_433, &channel_433);
    daemon_process_cad_status(&radio_controller_868, &channel_868);
    daemon_process_rssi_stream(rssi_timer);
}

/* --- Main loop logging --------------------------------------------------- */
static void daemon_log_loop_start(void)
{
    printf("[Daemon] Starte Polling-Loop für LoRa und Sockets\n");
}

/* --- RX special cases ---------------------------------------------------- */
template<typename RadioT>
static void daemon_discard_rx_during_tx(RadioController<RadioT> *ctrl)
{
    ctrl->received = false;
    ctrl->radio->clearIrq(0xFFFFFFFF);
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "RX während TX verworfen");
    printf("[%s] RX während TX - verwerfe Paket\n",
           radio_controller_tag(ctrl));
}

/* --- RX output ----------------------------------------------------------- */
static void daemon_debug_hex_bytes(const char *ctx, const uint8_t *buf, int len)
{
    char msg[512];
    size_t pos = 0;

    pos += snprintf(msg + pos, sizeof(msg) - pos, "HEX:");
    for (int i = 0; i < len; i++) {
        if (pos + 4 >= sizeof(msg)) {
            snprintf(msg + pos, sizeof(msg) - pos, " ...");
            break;
        }

        pos += snprintf(msg + pos, sizeof(msg) - pos, " %02X", buf[i]);
    }

    daemon_debug_ctx(ctx, "%s", msg);
}

static void daemon_print_ascii_bytes(const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        if (buf[i] >= 32 && buf[i] <= 126)
            printf("%c", buf[i]);
        else
            printf(".");
    }
}

/* --- RX presentation metadata ------------------------------------------- */
template<typename RadioT>
static const char *daemon_controller_color(RadioController<RadioT> *ctrl)
{
    if (ctrl->band == RADIO_BAND_433)
        return "93m";

    return "32m";
}

static void daemon_print_lora_packet(const char *rx_ctx,
                                     const char *band,
                                     const char *color,
                                     uint8_t *buf,
                                     int len,
                                     float rssi)
{
    uint32_t toNode      = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
    uint32_t fromNode    = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
    uint32_t uniqueID    = buf[8] | (buf[9] << 8) | (buf[10] << 16) | (buf[11] << 24);

    uint8_t hdrFlags     = buf[12];
    uint8_t chHash       = buf[13];
    uint8_t nextHop      = buf[14];
    uint8_t rlyNodes     = buf[15];

    daemon_debug_ctx(rx_ctx,
                     "LoRa %d Byte from %08X to %08X ID:%08X Flag:%02X Hash:%02X Hop:%02X Node:%02X RSSI: %.2f dBm",
                     len, fromNode, toNode, uniqueID,
                     hdrFlags, chHash, nextHop, rlyNodes, rssi);
    daemon_debug_hex_bytes(rx_ctx, buf, len);

    printf("[\e[%s%s\e[0m] %d Bytes ASCII: ", color, band, len);
    daemon_print_ascii_bytes(buf, len);
    printf(" RSSI: %.2f dBm\n", rssi);
}

static void daemon_print_fsk_packet(const char *rx_ctx,
                                    const char *band,
                                    const char *color,
                                    uint8_t *buf,
                                    int len,
                                    float rssi)
{
    daemon_debug_hex_bytes(rx_ctx, buf, len);

    printf("[\e[%s%s-FSK\e[0m] %d Bytes ASCII: ", color, band, len);
    daemon_print_ascii_bytes(buf, len);
    printf(" RSSI: %.2f dBm\n", rssi);
}

/* --- RX forwarding ------------------------------------------------------- */
static void daemon_broadcast_rx_data(RadioChannelIo *io, uint8_t *buf, int len)
{
    if (len <= 0)
        return;

    client_slot_broadcast_bytes_queued(io->data_slots, MAX_CLIENTS, buf, len);
}

/* --- RX IRQ/FIFO sequence ------------------------------------------------ */
template<typename RadioT>
static void daemon_restart_receive_after_empty_rx(RadioController<RadioT> *ctrl)
{
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "Leer-IRQ, RX neu starten");
    ctrl->radio->clearIrq(0xFFFFFFFF);
    ctrl->radio->startReceive();
}

template<typename RadioT>
static void daemon_finish_rx_packet(RadioController<RadioT> *ctrl,
                                    uint8_t *buf,
                                    size_t buf_len)
{
    if (ctrl->band == RADIO_BAND_868)
        memset(buf, 0, buf_len);

    radio_controller_led(ctrl, 0);
    ctrl->radio->startReceive();
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "RX bereit");
}

template<typename RadioT>
static void daemon_prepare_rx_packet(RadioController<RadioT> *ctrl,
                                     uint8_t *buf,
                                     size_t buf_len)
{
    ctrl->received = false;
    memset(buf, 0, buf_len);

    // WICHTIG: Im FSK-Modus clearIrq() NICHT vor getPacketLength()/readData() aufrufen!
    // Grund: SX127x RegIrqFlags2 Bit3 = FifoOverrun -> schreibt man 0xFF rein,
    // wird Bit3 gesetzt und der FIFO wird gelöscht (laut Datasheet).
    // Im LoRa-Modus trifft das nicht zu (anderes Register).
    if (ctrl->mode == RADIO_MODE_LORA) {
        daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "IRQ vor Read löschen");
        ctrl->radio->clearIrq(0xFFFFFFFF);
    }
}

template<typename RadioT>
static int daemon_rx_packet_length(RadioController<RadioT> *ctrl)
{
    int len = ctrl->radio->getPacketLength();

    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "Länge %d", len);
    return len;
}

template<typename RadioT>
static void daemon_clear_irq_after_rx_read(RadioController<RadioT> *ctrl)
{
    if (ctrl->mode == RADIO_MODE_FSK) {
        daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "IRQ nach Read löschen");
        ctrl->radio->clearIrq(0xFFFFFFFF);
    }
}

template<typename RadioT>
static void daemon_print_rx_packet(RadioController<RadioT> *ctrl,
                                   uint8_t *buf,
                                   int len)
{
    const char *tag = radio_controller_tag(ctrl);
    const char *color = daemon_controller_color(ctrl);
    float rssi = radio_controller_packet_rssi(ctrl);

    if (ctrl->mode == RADIO_MODE_LORA)
        daemon_print_lora_packet(daemon_rx_log_ctx(ctrl), tag, color, buf, len, rssi);
    else
        daemon_print_fsk_packet(daemon_rx_log_ctx(ctrl), tag, color, buf, len, rssi);
}

/* --- RX radio accessors -------------------------------------------------- */
template<typename RadioT>
static int16_t daemon_read_rx_data(RadioController<RadioT> *ctrl,
                                   uint8_t *buf,
                                   size_t buf_len)
{
    return ctrl->radio->readData(buf, buf_len);
}

/* --- RX read validation -------------------------------------------------- */
static bool daemon_should_log_rx_drop(unsigned long drops)
{
    return drops <= RX_DROP_LOG_INITIAL ||
           (RX_DROP_LOG_INTERVAL > 0 && drops % RX_DROP_LOG_INTERVAL == 0);
}

template<typename RadioT>
static void daemon_record_rx_drop(RadioController<RadioT> *ctrl, int16_t state)
{
    ctrl->rx_drops++;
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "Drop %lu Status %d",
                     ctrl->rx_drops, state);

    if (daemon_should_log_rx_drop(ctrl->rx_drops)) {
        printf("[%d] RX read error: %d, packet dropped, drops=%lu\n",
               radio_controller_band_number(ctrl), state, ctrl->rx_drops);
        fflush(stdout);
    }
}

template<typename RadioT>
static bool daemon_rx_read_ok(RadioController<RadioT> *ctrl, int16_t state)
{
    if (state == RADIOLIB_ERR_NONE)
        return true;

    daemon_record_rx_drop(ctrl, state);
    return false;
}

/* --- RX band flow -------------------------------------------------------- */
template<typename RadioT>
static void daemon_process_radio_band(RadioController<RadioT> *ctrl,
                                      RadioChannelIo *io,
                                      uint8_t (&rx_buf)[buf_SIZE])
{
    if (!radio_controller_ready(ctrl) || !ctrl->radio)
        return;

    // Gemeinsamer RX-Ablauf; die 433/868-Originalkommentare stehen in den Wrappern.
    if (!ctrl->received)
        return;

    if (ctrl->tx_busy) {
        daemon_discard_rx_during_tx(ctrl);
        return;
    }

    daemon_prepare_rx_packet(ctrl, rx_buf, sizeof(rx_buf));

    int len = daemon_rx_packet_length(ctrl);
    // Fehlauslösung (z.B. durch CAD-IRQ): kein Paket vorhanden
    if (len <= 0) {
        // Im FSK-Modus: clearIrq erst jetzt sicher (FIFO war leer)
        daemon_restart_receive_after_empty_rx(ctrl);
        return;
    }

    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "readData()");
    int16_t read_state = daemon_read_rx_data(ctrl, rx_buf, sizeof(rx_buf)); // 5ms Timeout

    // Im FSK-Modus: clearIrq NACH readData() - FIFO ist jetzt geleert
    daemon_clear_irq_after_rx_read(ctrl);

    if (!daemon_rx_read_ok(ctrl, read_state)) {
        daemon_finish_rx_packet(ctrl, rx_buf, sizeof(rx_buf));
        return;
    }

    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "Read OK");
    daemon_print_rx_packet(ctrl, rx_buf, len);
    daemon_broadcast_rx_data(io, rx_buf, len);
    daemon_debug_ctx(daemon_rx_log_ctx(ctrl), "Broadcast %d Byte", len);

    daemon_finish_rx_packet(ctrl, rx_buf, sizeof(rx_buf));
}


/* --- RX band entry points ------------------------------------------------ */
static void daemon_process_radio_433(uint8_t (&rx_buf_433)[buf_SIZE])
{
    // --- LoRa/FSK Polling 433 (kurzer Timeout 5ms, Non-Blocking) ---
    daemon_process_radio_band(&radio_controller_433, &channel_433, rx_buf_433);
}

static void daemon_process_radio_868(uint8_t (&rx_buf_868)[buf_SIZE])
{
    // --- LoRa/FSK Polling 868 (mit Callback-Flag + Non-Blocking 5ms) ---
    daemon_process_radio_band(&radio_controller_868, &channel_868, rx_buf_868);
}

/* --- Radio polling order ------------------------------------------------- */
static void daemon_process_radio_polling(DaemonDeadlineTimer *rssi_timer,
                                         uint8_t (&rx_buf_433)[buf_SIZE],
                                         uint8_t (&rx_buf_868)[buf_SIZE])
{
    daemon_process_radio_433(rx_buf_433);
    daemon_process_radio_868(rx_buf_868);

    // --- CAD/RSSI Überwachung ---
    daemon_process_cad_rssi(rssi_timer);
}

/* --- Main loop iteration ------------------------------------------------- */
static void daemon_process_loop_iteration(EventLoopSet *event_set,
                                          EventLoopReadySet *readfds,
                                          DaemonLoopContext *loop_ctx,
                                          uint8_t *buf,
                                          uint8_t (&rx_buf_433)[buf_SIZE],
                                          uint8_t (&rx_buf_868)[buf_SIZE])
{
    // Wait for socket events.
    int ret = daemon_wait_for_events(event_set, readfds);
    if (ret < 0) {
        if (errno == EINTR && daemon_lifecycle_stop_requested()) {
            daemon_debug_ctx("LIFE", "Event-Wait durch Stop unterbrochen");
            return;
        }

        perror("event_loop_wait");
        return;
    }

    // --- Socket Clients bearbeiten ---
    daemon_process_ready_sockets(&loop_ctx->config_433_ctx,
                                &loop_ctx->config_868_ctx,
                                &loop_ctx->data_tx_433_ctx,
                                &loop_ctx->data_tx_868_ctx,
                                readfds, buf);

    daemon_process_radio_polling(&loop_ctx->rssi_timer, rx_buf_433, rx_buf_868);
}

/* --- Polling loop -------------------------------------------------------- */
static void daemon_run_polling_loop(DaemonMainContext *ctx)
{
    daemon_log_loop_start();
    daemon_verbose_ctx("LIFE", "Polling aktiv");

    while (!daemon_lifecycle_stop_requested()) {
        daemon_process_loop_iteration(&ctx->event_set,
                                      &ctx->readfds,
                                      &ctx->loop_ctx,
                                      ctx->buf,
                                      ctx->rx_buf_433,
                                      ctx->rx_buf_868);
    }
}

/* --- Daemon run ---------------------------------------------------------- */
static void daemon_run(void)
{
    DaemonMainContext main_ctx;
    daemon_main_context_init(&main_ctx);

    daemon_run_polling_loop(&main_ctx);

    daemon_log("Stop angefordert");
    daemon_verbose_ctx("LIFE", "Shutdown beginnt");
    daemon_shutdown_cleanup(&main_ctx.event_set);
    daemon_debug_ctx("LIFE", "Shutdown abgeschlossen");
}

/* --- Startup helpers ----------------------------------------------------- */
static void daemon_ignore_sigpipe(void)
{
    // SIGPIPE ignorieren: write() auf geschlossenen Socket crasht sonst den Daemon
    signal(SIGPIPE, SIG_IGN);
    daemon_debug_ctx("STARTUP", "SIGPIPE wird ignoriert");
}

static void daemon_print_usage(const char *argv0)
{
    printf("Nutzung: %s [-d] [-v|--verbose] [--debug] [--help]\n", argv0);
}

static bool daemon_parse_args(int argc, char *argv[])
{
    int opt;
    bool is_daemon = false;
    static const struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"debug",   no_argument, 0, 1000},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    // Parsen der Argumente
    while ((opt = getopt_long(argc, argv, "dvh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                is_daemon = true;
                daemon_debug_ctx("STARTUP", "Option -d erkannt");
                break;
            case 'v':
                if (daemon_log_level < DAEMON_LOG_VERBOSE)
                    daemon_log_level = DAEMON_LOG_VERBOSE;
                daemon_verbose_ctx("STARTUP", "Verbose aktiv");
                break;
            case 1000:
                daemon_log_level = DAEMON_LOG_DEBUG;
                daemon_debug_ctx("STARTUP", "Debug aktiv");
                break;
            case 'h':
                daemon_print_usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                daemon_print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    return is_daemon;
}

/* --- Startup sequence ---------------------------------------------------- */
static void daemon_startup_sequence(int argc, char *argv[])
{
    daemon_ignore_sigpipe();
    bool is_daemon = daemon_parse_args(argc, argv);

    daemon_verbose_ctx("STARTUP", "Startmodus: %s", is_daemon ? "Daemon" : "Vordergrund");
    daemon_debug_ctx("STARTUP", "Argumente verarbeitet");

    // --- Userspace-Daemon Implementation ---
    if (is_daemon)
        daemon_enter_background();

    daemon_debug_ctx("STARTUP", "Starte Radio- und Socket-Init");
    daemon_radio_io_init();
    daemon_debug_ctx("STARTUP", "Startup abgeschlossen");
}

/* --- Main entry ---------------------------------------------------------- */

int main(int argc, char *argv[]) {
    daemon_startup_sequence(argc, argv);
    daemon_run();

    return 0;
}


