/****************************************************************************** * Copyright (C) 2026  [LoRaHAM / Alexander Walter]
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
#include <vector>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include "hal/RPi/PiHal.h"
#include <RadioLib.h>
#include <lgpio.h>

#include "daemon_protocol.h"
#include "daemon_timing.h"
#include "daemon_lifecycle.h"
#include "data_tx.h"
#include "event_loop.h"
#include "unix_socket.h"
#include "client_set.h"
#include "radio_channel.h"
#include "config_dispatch.h"

// --- Globale Socket-FDs
int data433_fd = -1, data868_fd = -1;
int conf433_fd = -1, conf868_fd = -1;

int client_data433[MAX_CLIENTS] = {0};
int client_data868[MAX_CLIENTS] = {0};
int client_conf433[MAX_CLIENTS] = {0};
int client_conf868[MAX_CLIENTS] = {0};

RadioChannelIo channel_433;
RadioChannelIo channel_868;

static void daemon_shutdown_cleanup(EventLoopSet *event_set)
{
    event_loop_close(event_set);

    client_set_close_all(client_data433, MAX_CLIENTS);
    client_set_close_all(client_data868, MAX_CLIENTS);
    client_set_close_all(client_conf433, MAX_CLIENTS);
    client_set_close_all(client_conf868, MAX_CLIENTS);

    close_unix_socket(&data433_fd, DATA433_SOCKET);
    close_unix_socket(&data868_fd, DATA868_SOCKET);
    close_unix_socket(&conf433_fd, CONF433_SOCKET);
    close_unix_socket(&conf868_fd, CONF868_SOCKET);
}

static int daemon_wait_for_events(EventLoopSet *event_set,
                                  EventLoopReadySet *readfds)
{
    event_loop_reset(event_set);
    radio_channel_add_fds(&channel_433, event_set);
    radio_channel_add_fds(&channel_868, event_set);

    return event_loop_wait(event_set, readfds, DAEMON_SELECT_TIMEOUT_USEC);
}

static void daemon_runtime_init(EventLoopSet *event_set)
{
    // --- Event-Backend ---
    if (event_loop_init_default(event_set) != 0)
        perror("epoll");
    printf("[Daemon] Event-Backend: %s\n",
           event_loop_backend_name(event_loop_backend(event_set)));

    // --- Signal-Stop ---
    daemon_lifecycle_reset_stop();
    if (daemon_lifecycle_install_signal_handlers() != 0)
        perror("sigaction");
}
RadioChannelRuntime runtime_433;
RadioChannelRuntime runtime_868;

// --- Pin-Setup für LED
#define PIN_433 13
#define PIN_868 19

// --- LoRa Setup ---
PiHal* hal_433 = nullptr;
Module* mod_433 = nullptr;
SX1278* radio_433 = nullptr;

PiHal* hal_868 = nullptr;
Module* mod_868 = nullptr;
RFM95* radio_868 = nullptr;

int h = -1;
int chip = -1;

// --- Initialisierung der LEDs ---
void LED_init() {
    h = lgGpiochipOpen(0);
    chip = h; // chip bekommt den Wert von h

    chip = lgGpiochipOpen(0);
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


// --- Hilfsfunktionen für Frames ---

// --- Neue Hilfsfunktion für Rohdaten ---
ssize_t recv_raw_nonblocking(int fd, uint8_t *buf, size_t max_size) {
    // Liest alles, was aktuell im Socket-Puffer bereitsteht
    // Da der Socket im non-blocking Modus ist (durch select),
    // liefert read() sofort zurück, was da ist.
    ssize_t n = read(fd, buf, max_size);
    return n;
}

// Wenn du zurück an den Client sendest, kannst du entscheiden:
// Willst du dem Client die Länge mitschicken oder nur die Rohdaten?
ssize_t send_raw(int fd, const uint8_t *buf, uint8_t len) {
    // Nur die Daten senden, ohne Längen-Byte vorab
    return write(fd, buf, len);
}

/*
 * ssize_t recv_frame_nonblocking(int fd, uint8_t *buf, uint8_t *len) {
 *    // Non-blocking read mit maximal 1 Byte sofort
 *    ssize_t n = read(fd, len, 1);
 *    if (n <= 0) return n;
 *    ssize_t total = 0;
 *    while (total < *len) {
 *        n = read(fd, buf + total, *len - total);
 *        if (n <= 0) break; // Non-blocking -> nicht warten
 *        total += n;
 *    }
 *    return total;
 * }
 */

ssize_t send_frame(int fd,const uint8_t *buf,uint8_t len){
    if(write(fd,&len,1)!=1) return -1;
    return write(fd,buf,len);
}

// --- CAD broadcast helper moved to client_set.cpp ---


// --- Flag für empfangene Pakete 868 ---
volatile bool receivedFlag868 = false;
volatile bool receivedFlag433 = false;

volatile bool txBusy433 = false;
volatile bool txBusy868 = false;

// --- CAD-Status (Channel Activity Detection) für beide Bänder ---
// true = Kanal war beim letzten Scan belegt
volatile bool cad433_active = false;
volatile bool cad868_active = false;

// --- GETRSSI-Status: Kontinuierliche RSSI-Übertragung an CONF-Clients ---
// Aktivierung: SET GETRSSI=1 / Deaktivierung: SET GETRSSI=0 auf dem jeweiligen
// Conf-Socket (loraconf433.sock bzw. loraconf868.sock).
// Sendet im aktiven Zustand mit 10 Hz "RSSI=-87.50\n" an alle verbundenen
// Conf-Clients dieses Bandes. Funktioniert in LORA- UND FSK-Modus.
// Auto-Stop: wird auf false gesetzt, sobald kein Conf-Client mehr verbunden ist.
volatile bool getrssi_433_active = false;
volatile bool getrssi_868_active = false;

// --- Modusverwaltung: LoRa oder FSK pro Band ---
// Default: LORA -> volle Rückwärtskompatibilität mit alten Clients
// Umschalten nur durch explizites "SET MODE=FSK" bzw. "SET MODE=LORA"
volatile RadioMode_t mode_433 = RADIO_MODE_LORA;
volatile RadioMode_t mode_868 = RADIO_MODE_LORA;

// --- Live RSSI helper moved to radio_channel.cpp ---
//void setNonBlocking(int fd) {
//fcntl(fd, F_SETFL, O_NONBLOCK);
//}


// --- Callback für 868 ---
void setFlag868(void) {
    receivedFlag868 = true;
    LED_868(1);
    //usleep(50000);
}
void setFlag433(void) {
    receivedFlag433 = true;
    LED_433(1);
    //usleep(50000);
}

void setFlashFlag868(void) {

    LED_868(1);
    usleep(15000);
    LED_868(0);
    usleep(15000);
}
void setFlashFlag433(void) {

    LED_433(1);
    usleep(15000);
    LED_433(0);
    usleep(15000);
}

// --- LoRa senden ---

// --- LoRa senden (DEBUG + FIX) ---
void lora_send(uint8_t *buf, size_t len, int band) {
    // WICHTIG: Buffer kopieren, damit er nicht überschrieben wird!
    uint8_t send_buf[256];
    memcpy(send_buf, buf, len);

    printf("[SEND %d] %zu Bytes: ", band, len);
    for(size_t i = 0; i < std::min(len, (size_t)20); i++)
        printf("%c", send_buf[i] >= 32 && send_buf[i] <= 126 ? send_buf[i] : '.');
    printf(" (HEX: ");
    for(size_t i = 0; i < std::min(len, (size_t)20); i++)
        printf("%02X ", send_buf[i]);
    printf(")\n");

    if (band == 433) {
        if(txBusy433) {
            printf("[433] TX BUSY - überspringen\n");
            return;
        }
        txBusy433 = true;

        // WICHTIG: RX-Flag clearen und Callback deaktivieren!
        receivedFlag433 = false;
        // Im FSK-Modus keinen Callback clearen (wird anders behandelt)
        if (mode_433 == RADIO_MODE_LORA)
            radio_433->clearPacketReceivedAction();

        // Radio komplett in Standby und zurücksetzen
        radio_433->standby();
        radio_433->clearIrq(0xFFFFFFFF);

        // TX-FIFO komplett zurücksetzen: Erst Sleep, dann Standby - löscht die FIFOs
        // NUR im LoRa-Modus! In FSK würde sleep() die Modul-Konfiguration zurücksetzen.
        if (mode_433 == RADIO_MODE_LORA) {
            radio_433->sleep();
            usleep(10000); // 10ms warten
            radio_433->standby();
            usleep(10000); // nochmal 10ms
        }
        /*
         *        // WICHTIG: LoRa-Parameter nochmal setzen (für TX!)
         *        radio_433->setFrequency(433.775);
         *        radio_433->setSpreadingFactor(12);
         *        radio_433->setBandwidth(125.0);
         *        radio_433->setCodingRate(5);
         *        radio_433->setSyncWord(0x12);
         *        radio_433->setPreambleLength(8);
         *        radio_433->setCRC(true);
         *        radio_433->explicitHeader();
         *        radio_433->setOutputPower(10);
         */
        // Nochmal IRQs clearen
        radio_433->clearIrq(0xFFFFFFFF);

        printf("[433] Radio neu konfiguriert für TX\n");
        printf("[433] Sende jetzt %zu Bytes (0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X)...\n",
               len, send_buf[0], send_buf[1], send_buf[2], send_buf[3], send_buf[4],
               len > 5 ? send_buf[5] : 0, len > 6 ? send_buf[6] : 0);

        // Zurück zu transmit() - das sollte blockierend sein
        int state = radio_433->transmit(send_buf, len);

        if(state != RADIOLIB_ERR_NONE) {
            printf("[433] transmit ERROR: %d\n", state);
        } else {
            printf("[433] transmit returned OK\n");
        }

        // EXTRA lange warten für SF12!
        // SF12, BW125, CR4/5, 5 Bytes Payload: ~370ms + Overhead
        //        printf("[433] Warte 1 Sekunde Sicherheit...\n");
        //        usleep(1000000); // 1 Sekunde!

        // Nach dem Senden: IRQ clearen und Callback wieder aktivieren
        // Callback wird für LoRa UND FSK neu gesetzt - transmit() kann ihn löschen!
        radio_433->clearIrq(0xFFFFFFFF);
        receivedFlag433 = false;
        radio_433->setPacketReceivedAction(setFlag433);

        txBusy433 = false;
        radio_433->startReceive();

    } else if (band == 868) {
        if(txBusy868) {
            printf("[868] TX BUSY - überspringen\n");
            return;
        }
        txBusy868 = true;

        // WICHTIG: RX-Flag clearen und Callback deaktivieren!
        receivedFlag868 = false;
        // Im FSK-Modus keinen Callback clearen (wird anders behandelt)
        if (mode_868 == RADIO_MODE_LORA)
            radio_868->clearPacketReceivedAction();

        // WICHTIG: FIFO und IRQ flags clearen VOR dem Senden!
        radio_868->standby();
        radio_868->clearIrq(0xFFFFFFFF);

        // WICHTIG: transmit() ist blockierend und wartet bis fertig!
        int state = radio_868->transmit(send_buf, len);

        if(state != RADIOLIB_ERR_NONE) {
            printf("[868] TX ERROR: %d\n", state);
        } else {
            printf("[868] TX OK - %zu Bytes gesendet\n", len);
        }

        // Kurz warten, damit TX wirklich abgeschlossen ist
        usleep(50000); // 50ms Sicherheitspause

        // Nach dem Senden: IRQ clearen und Callback wieder aktivieren
        // Callback wird für LoRa UND FSK neu gesetzt - transmit() kann ihn löschen!
        radio_868->clearIrq(0xFFFFFFFF);
        receivedFlag868 = false;
        radio_868->setPacketReceivedAction(setFlag868);

        txBusy868 = false;
        radio_868->startReceive();
    }
}


/*
 * void lora_send(uint8_t *buf, size_t len, int band) {
 *    printf("[SEND %d] %d Bytes: ", band, (int)len);
 *    for(size_t i = 0; i < std::min(len, (size_t)20); i++) printf("%c", buf[i] >= 32 && buf[i] <= 126 ? buf[i] : '.');
 *    printf("...\n");
 *
 *    if (band == 433) {
 *        // Buffer leeren vor Senden
 *        radio_433->clearIrq(0xFFFFFFFF);  // ← FIX: clearIrq()
 *        radio_433->standby();
 *
 *        int state = radio_433->transmit(buf, len);
 *        if (state != RADIOLIB_ERR_NONE) {
 *            printf("[433] TRANSMIT FEHLER: %d\n", state);
 *        } else {
 *            printf("[433] TRANSMIT OK\n");
 *        }
 *
 *        // WICHTIG: Kurz warten bis TX fertig
 *        usleep(100000);  // 100ms
 *
 *        // Zurück zu RX
 *        radio_433->clearIrq(0xFFFFFFFF);  // ← FIX: clearIrq()
 *        radio_433->startReceive();
 *
 *    } else if (band == 868) {
 *        radio_868->clearIrq(0xFFFFFFFF);  // ← FIX: clearIrq()
 *        radio_868->standby();
 *
 *        int state = radio_868->transmit(buf, len);
 *        if (state != RADIOLIB_ERR_NONE) {
 *            printf("[868] TRANSMIT FEHLER: %d\n", state);
 *        } else {
 *            printf("[868] TRANSMIT OK\n");
 *        }
 *
 *        usleep(100000);
 *        radio_868->clearIrq(0xFFFFFFFF);  // ← FIX: clearIrq()
 *        radio_868->startReceive();
 *    }
 * }
 */

/*
 * void lora_send(uint8_t *buf, size_t len, int band) {
 *    if (band == 433) {
 *        if (radio_433->transmit(buf, len) != RADIOLIB_ERR_NONE){
 *            printf("[433] Fehler beim Senden!\n");}else{
 *            radio_433->startReceive();}
 *    } else if (band == 868) {
 *        if (radio_868->transmit(buf, len) != RADIOLIB_ERR_NONE){
 *            printf("[868] Fehler beim Senden!\n");}else{
 *            radio_868->startReceive();}
 *    }
 * }
 */

/* --- CONFIG apply: see config_apply.cpp --- */

// --- Init LoRa ---
void lora_init() {
    printf("[Init] Starte Dualband LoRa Receiver (433 + 868)\n");

    if (h < 0) {
        printf("[GPIO] Fehler: gpiochip0 konnte nicht geöffnet werden!\n");
        return;
    }

    LED_433(1);

    hal_433   = new PiHal(0);
    mod_433   = new Module(hal_433, 8, 25, 5, 24);
    radio_433 = new SX1278(mod_433);

    hal_868   = new PiHal(0);
    mod_868   = new Module(hal_868, 7, 16, 6, 12);
    radio_868 = new RFM95(mod_868);

    if (radio_433->begin() == RADIOLIB_ERR_NONE)
        printf("[433] Init OK\n");
    else printf("[433] Init FEHLGESCHLAGEN\n");


    // LoRa-APRS:

    radio_433->setFrequency(433.900);
    radio_433->setSpreadingFactor(12);
    radio_433->setBandwidth(125.0);
    radio_433->setSyncWord(0x12);
    radio_433->setPreambleLength(8);
    radio_433->setCodingRate(5);
    radio_433->setCRC(true);
    radio_433->autoLDRO();
    radio_433->forceLDRO(1);
    radio_433->setOutputPower(10);

    /*
     *
     * // LoRa DX Cluster:
     *    radio_433->setFrequency(433.900);
     *    radio_433->setSpreadingFactor(10);
     *    radio_433->setBandwidth(125.0);
     *    radio_433->setSyncWord(0x12);
     *    radio_433->setPreambleLength(8);
     *    radio_433->setCodingRate(5);
     *    radio_433->setCRC(true);
     *    radio_433->autoLDRO();
     *    radio_433->setOutputPower(10);
     *
     *
     *
     * // Meshtastic 433:
     *
     *    radio_433->setFrequency(433.900); // DB0ARD
     *    radio_433->setSpreadingFactor(11);
     *    radio_433->setBandwidth(125.0);
     *    radio_433->setSyncWord(0x2B);
     *    radio_433->setPreambleLength(16);
     *    radio_433->setCodingRate(5);
     *    radio_433->setCRC(true);
     *    radio_433->autoLDRO();
     *    radio_433->setOutputPower(10);
     *
     *
     * // Meshcom:
     *
     *    radio_433->setFrequency(433.175);
     *    radio_433->setSpreadingFactor(11);
     *    radio_433->setBandwidth(250.0);
     *    radio_433->setSyncWord(0x2B);
     *    radio_433->setPreambleLength(8);
     *    radio_433->setCodingRate(6);
     *    radio_433->setCRC(true);
     *    //radio_433->autoLDRO();
     *    radio_433->setOutputPower(10);
     * /*
     */
     radio_433->setPacketReceivedAction(setFlag433); // Callback nutzen
     LED_433(0);

     LED_868(1);
     if (radio_868->begin() == RADIOLIB_ERR_NONE)
         printf("[868] Init OK\n");
    else printf("[868] Init FEHLGESCHLAGEN\n");

    radio_868->setFrequency(869.525);
    radio_868->setSpreadingFactor(11);
    radio_868->setBandwidth(250.0);
    radio_868->setSyncWord(0x2B);
    radio_868->setPreambleLength(16);
    radio_868->setCodingRate(5);
    radio_868->setCRC(true);
    radio_868->autoLDRO();
    radio_868->setOutputPower(10);

    radio_868->setPacketReceivedAction(setFlag868); // Callback nutzen
    LED_868(0);

    radio_433->startReceive();
    radio_868->startReceive();
    fflush(stdout);
}

/* --- Data client TX handling --- */


typedef struct {
    const char *tag;
    int band;
    volatile RadioMode_t *mode;
} DataTxDaemonContext;

#define DATA_TX_CAD_MAX_WAIT_TICKS 300
#define DATA_TX_CAD_SLEEP_USEC 10000

static int data_tx_modem_status(int band)
{
    return (band == 433)
        ? radio_433->getModemStatus()
        : radio_868->getModemStatus();
}

static void data_tx_led(int band, int state)
{
    if (band == 433)
        LED_433(state);
    else
        LED_868(state);
}

static int data_tx_wait_channel_free(DataTxDaemonContext *tx)
{
    int cad_wait = 0;

    if (*tx->mode != RADIO_MODE_LORA)
        return 0;

    while (cad_wait < DATA_TX_CAD_MAX_WAIT_TICKS) {
        if ((data_tx_modem_status(tx->band) & 0x01) == 0)
            return 0;

        usleep(DATA_TX_CAD_SLEEP_USEC);
        cad_wait++;
    }

    return 1;
}

static int send_data_chunk(uint8_t *chunk, size_t len, size_t offset, void *ctx)
{
    DataTxDaemonContext *tx = (DataTxDaemonContext *)ctx;

    // --- CAD-Guard: LoRa only ---
    if (data_tx_wait_channel_free(tx)) {
        printf("[%s] CAD-Timeout: Kanal dauerhaft belegt, Paket verworfen\n", tx->tag);
        return 1;
    }

    printf("  -> Sende Chunk: %zu Bytes (Offset: %zu)\n", len, offset);

    data_tx_led(tx->band, 1);
    lora_send(chunk, len, tx->band);
    data_tx_led(tx->band, 0);

    return 0;
}

/* --- CONFIG dispatch wiring --- */

static void process_config_dispatch(ConfigDispatchContext<SX1278> *config_433_ctx,
                                    ConfigDispatchContext<RFM95> *config_868_ctx,
                                    const EventLoopReadySet *readfds,
                                    uint8_t *buf)
{
    config_dispatch_context<SX1278>(config_433_ctx, MAX_CLIENTS, readfds, buf);
    config_dispatch_context<RFM95>(config_868_ctx, MAX_CLIENTS, readfds, buf);
}

static void daemon_process_ready_sockets(ConfigDispatchContext<SX1278> *config_433_ctx,
                                         ConfigDispatchContext<RFM95> *config_868_ctx,
                                         DataTxDaemonContext *data_tx_433_ctx,
                                         DataTxDaemonContext *data_tx_868_ctx,
                                         const EventLoopReadySet *readfds,
                                         uint8_t *buf)
{
    radio_channel_accept_ready(&channel_433, readfds);
    radio_channel_accept_ready(&channel_868, readfds);

    data_tx_process_clients("433", client_data433, MAX_CLIENTS,
                            readfds, send_data_chunk, data_tx_433_ctx);
    data_tx_process_clients("868", client_data868, MAX_CLIENTS,
                            readfds, send_data_chunk, data_tx_868_ctx);

    process_config_dispatch(config_433_ctx, config_868_ctx, readfds, buf);
}


static void daemon_process_cad_status(int band)
{
    if (band == 433) {
        if (mode_433 != RADIO_MODE_LORA)
            return;

        uint8_t modem433 = radio_433->getModemStatus();
        bool hardwareActive433 = (modem433 & 0x01) || (modem433 & 0x10);

        if (hardwareActive433) {
            setFlashFlag433();
            if (!cad433_active) {
                LED_433(1);
                client_set_broadcast(client_conf433, MAX_CLIENTS, "CAD=1\n");
                cad433_active = true;
            }
        } else {
            if (cad433_active && !receivedFlag433) {
                LED_433(0);
                client_set_broadcast(client_conf433, MAX_CLIENTS, "CAD=0\n");
                cad433_active = false;
            }
        }

        return;
    }

    if (mode_868 != RADIO_MODE_LORA)
        return;

    uint8_t modem868 = radio_868->getModemStatus();
    bool hardwareActive868 = (modem868 & 0x01) || (modem868 & 0x10);

    if (hardwareActive868) {
        setFlashFlag868();
        if (!cad868_active) {
            LED_868(1);
            client_set_broadcast(client_conf868, MAX_CLIENTS, "CAD=1\n");
            cad868_active = true;
        }
    } else {
        if (cad868_active && !receivedFlag868) {
            LED_868(0);
            client_set_broadcast(client_conf868, MAX_CLIENTS, "CAD=0\n");
            cad868_active = false;
        }
    }
}
static void daemon_process_rssi_stream(DaemonDeadlineTimer *rssi_timer)
{
    radio_channel_getrssi_autostop(&channel_433, &runtime_433, "CONF 433");
    radio_channel_getrssi_autostop(&channel_868, &runtime_868, "CONF 868");

    // Functional change: RSSI cadence is now time-based.
    if (daemon_deadline_timer_due(rssi_timer, daemon_now_ms())) {
        // 433: nur lesen wenn aktiv und kein TX laeuft
        if (getrssi_433_active && !txBusy433) {
            float rssi433 = radio_channel_read_live_rssi(mod_433, mode_433, false);
            char rssi_msg[32];
            snprintf(rssi_msg, sizeof(rssi_msg), "RSSI=%.2f\n", rssi433);
            client_set_broadcast(client_conf433, MAX_CLIENTS, rssi_msg);
        }

        // 868: nur lesen wenn aktiv und kein TX laeuft
        if (getrssi_868_active && !txBusy868) {
            float rssi868 = radio_channel_read_live_rssi(mod_868, mode_868, true);
            char rssi_msg[32];
            snprintf(rssi_msg, sizeof(rssi_msg), "RSSI=%.2f\n", rssi868);
            client_set_broadcast(client_conf868, MAX_CLIENTS, rssi_msg);
        }
    }
}

// --- Unix socket setup moved to unix_socket.cpp ---

int main(int argc, char *argv[]) {
    int opt;
    bool is_daemon = false;
    // SIGPIPE ignorieren: write() auf geschlossenen Socket crasht sonst den Daemon
    signal(SIGPIPE, SIG_IGN);

    // Parsen der Argumente
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
            case 'd':
                is_daemon = true;
                break;
            default:
                fprintf(stderr, "Nutzung: %s [-d]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // --- Userspace-Daemon Implementation ---
    if (is_daemon) {
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
    }

    radio_channel_io_init(&channel_433,
                          RADIO_BAND_433,
                          DATA433_SOCKET,
                          CONF433_SOCKET,
                          &data433_fd,
                          &conf433_fd,
                          client_data433,
                          client_conf433);
    radio_channel_io_init(&channel_868,
                          RADIO_BAND_868,
                          DATA868_SOCKET,
                          CONF868_SOCKET,
                          &data868_fd,
                          &conf868_fd,
                          client_data868,
                          client_conf868);

    radio_channel_open_sockets(&channel_433);
    radio_channel_open_sockets(&channel_868);

    radio_channel_runtime_init(&runtime_433,
                               &mode_433,
                               &receivedFlag433,
                               &txBusy433,
                               &cad433_active,
                               &getrssi_433_active);
    radio_channel_runtime_init(&runtime_868,
                               &mode_868,
                               &receivedFlag868,
                               &txBusy868,
                               &cad868_active,
                               &getrssi_868_active);

    LED_init();
    lora_init();

    EventLoopSet event_set;
    daemon_runtime_init(&event_set);

    EventLoopReadySet readfds;
    uint8_t buf[buf_SIZE];
    uint8_t rx_buf_433[buf_SIZE];  // ← GETRENNTE Buffer pro Band!
    uint8_t rx_buf_868[buf_SIZE];  // ← GETRENNTE Buffer pro Band!

    // --- GETRSSI Timer ---
    DaemonDeadlineTimer rssi_timer;
    daemon_deadline_timer_init(&rssi_timer, daemon_now_ms(), DAEMON_RSSI_INTERVAL_MS);

    // --- DATA TX callbacks ---
    DataTxDaemonContext data_tx_433_ctx = {"433", 433, &mode_433};
    DataTxDaemonContext data_tx_868_ctx = {"868", 868, &mode_868};

    // --- CONFIG dispatch ---
    ConfigDispatchContext<SX1278> config_433_ctx = {
        client_conf433,
        radio_433,
        "CONF 433",
        "[CONF433]",
        &mode_433,
        &getrssi_433_active,
        config_apply_command<SX1278>,
        setFlag433
    };
    ConfigDispatchContext<RFM95> config_868_ctx = {
        client_conf868,
        radio_868,
        "CONF 868",
        NULL,
        &mode_868,
        &getrssi_868_active,
        config_apply_command<RFM95>,
        setFlag868
    };


    printf("[Daemon] Starte Polling-Loop für LoRa und Sockets\n");


    while (!daemon_lifecycle_stop_requested()) {

        // --- Event wait ---
        int ret = daemon_wait_for_events(&event_set, &readfds);
        if(ret<0){perror("event_loop_wait"); continue;}

        // --- Socket Clients bearbeiten ---
        daemon_process_ready_sockets(&config_433_ctx, &config_868_ctx,
                                    &data_tx_433_ctx, &data_tx_868_ctx,
                                    &readfds, buf);




                        // --- LoRa/FSK Polling 433 (kurzer Timeout 5ms, Non-Blocking) ---
                        if(receivedFlag433){
                            if(txBusy433 == false){
                                receivedFlag433 = false;
                                memset(rx_buf_433, 0, sizeof(rx_buf_433));           // Buffer leeren

                                // WICHTIG: Im FSK-Modus clearIrq() NICHT vor getPacketLength()/readData() aufrufen!
                                // Grund: SX127x RegIrqFlags2 Bit3 = FifoOverrun -> schreibt man 0xFF rein,
                                // wird Bit3 gesetzt und der FIFO wird gelöscht (laut Datasheet).
                                // Im LoRa-Modus trifft das nicht zu (anderes Register).
                                if (mode_433 == RADIO_MODE_LORA) {
                                    radio_433->clearIrq(0xFFFFFFFF);
                                }

                                int len433 = radio_433->getPacketLength();
                                // Fehlauslösung (z.B. durch CAD-IRQ): kein Paket vorhanden
                                if (len433 <= 0) {
                                    // Im FSK-Modus: clearIrq erst jetzt sicher (FIFO war leer)
                                    radio_433->clearIrq(0xFFFFFFFF);
                                    radio_433->startReceive();
                                } else {
                                    int16_t n433 = radio_433->readData(rx_buf_433,sizeof(rx_buf_433)); // 5ms Timeout

                                    // Im FSK-Modus: clearIrq NACH readData() - FIFO ist jetzt geleert
                                    if (mode_433 == RADIO_MODE_FSK) {
                                        radio_433->clearIrq(0xFFFFFFFF);
                                    }

                                    if (mode_433 == RADIO_MODE_LORA) {
                                        // --- LoRa: LoRaHAM-Binär-Header dekodieren ---
                                        uint32_t toNode      = rx_buf_433[0] | (rx_buf_433[1] << 8) | (rx_buf_433[2] << 16) | (rx_buf_433[3] << 24);
                                        uint32_t fromNode    = rx_buf_433[4] | (rx_buf_433[5] << 8) | (rx_buf_433[6] << 16) | (rx_buf_433[7] << 24);
                                        uint32_t uniqueID    = rx_buf_433[8] | (rx_buf_433[9] << 8) | (rx_buf_433[10] << 16) | (rx_buf_433[11] << 24);

                                        uint8_t hdrFlags     = rx_buf_433[12];
                                        uint8_t chHash       = rx_buf_433[13];
                                        uint8_t nextHop      = rx_buf_433[14];
                                        uint8_t rlyNodes     = rx_buf_433[15];

                                        //            printf("[\e[93m433\e[0m] %d Bytes HEX from Node ", len);
                                        //            printf("%08X:", fromNode); // %08X für Hex mit führenden Nullen
                                        //            printf("\e[93m%08X\e[0m:", fromNode); // %08X für Hex mit führenden Nullen
                                        //            printf("\n");

                                        printf("[\e[93m433\e[0m] %d Bytes HEX from Node ", len433);
                                        //            printf("%08X:", fromNode); // %08X für Hex mit führenden Nullen
                                        printf("\e[93m%08X\e[0m to ", fromNode); // %08X für Hex mit führenden Nullen
                                        printf("\e[93m%08X\e[0m ID:", toNode); // %08X für Hex mit führenden Nullen
                                        printf("\e[93m%08X\e[0m", uniqueID); // %08X für Hex mit führenden Nullen

                                        printf(" Flag:\e[93m%02X\e[0m", hdrFlags); // %08X für Hex mit führenden Nullen
                                        printf(" Hash:\e[93m%02X\e[0m", chHash); // %08X für Hex mit führenden Nullen
                                        printf(" Hop:\e[93m%02X\e[0m", nextHop); // %08X für Hex mit führenden Nullen
                                        printf(" Node:\e[93m%02X\e[0m", rlyNodes); // %08X für Hex mit führenden Nullen

                                        printf("\n");

                                        for (size_t i = 0; i < len433; i++) {
                                            printf("%02X ", rx_buf_433[i]);
                                        }
                                        printf("\n");

                                        printf("[\e[93m433\e[0m] %d Bytes    : ", len433);

                                        for (int i = 0; i < len433; i++) {
                                            if (rx_buf_433[i] >= 32 && rx_buf_433[i] <= 126)
                                                printf("%c", rx_buf_433[i]);
                                            else
                                                printf(".");
                                        }
                                        printf(" RSSI: %.2f dBm\n", radio_433->getRSSI());

                                    } else {
                                        // --- FSK: Rohdaten-Ausgabe (kein LoRaHAM-Header!) ---
                                        printf("[\e[93m433-FSK\e[0m] %d Bytes HEX: ", len433);
                                        for (int i = 0; i < len433; i++) {
                                            printf("%02X ", rx_buf_433[i]);
                                        }
                                        printf("\n");

                                        printf("[\e[93m433-FSK\e[0m] %d Bytes    : ", len433);
                                        for (int i = 0; i < len433; i++) {
                                            if (rx_buf_433[i] >= 32 && rx_buf_433[i] <= 126)
                                                printf("%c", rx_buf_433[i]);
                                            else
                                                printf(".");
                                        }
                                        printf(" RSSI: %.2f dBm\n", radio_433->getRSSI());
                                    }

                                    if(len433 > 0){
                                        client_set_broadcast_bytes(client_data433, MAX_CLIENTS, rx_buf_433, len433);
                                    }

                                    len433=0;
                                    LED_433(0);
                                    radio_433->startReceive();
                                }
                            } else {
                                // TX läuft noch - Flag zurücksetzen und IRQ clearen
                                receivedFlag433 = false;
                                radio_433->clearIrq(0xFFFFFFFF);
                                printf("[433] RX während TX - verwerfe Paket\n");
                            }
                        }

                        // --- LoRa/FSK Polling 868 (mit Callback-Flag + Non-Blocking 5ms) ---
                        if(receivedFlag868){
                            if(txBusy868 == false){
                                receivedFlag868 = false;
                                memset(rx_buf_868, 0, sizeof(rx_buf_868));           // rx_buffer leeren

                                // WICHTIG: Im FSK-Modus clearIrq() NICHT vor getPacketLength()/readData() aufrufen!
                                // Grund: SX127x RegIrqFlags2 Bit3 = FifoOverrun -> schreibt man 0xFF rein,
                                // wird Bit3 gesetzt und der FIFO wird gelöscht (laut Datasheet).
                                // Im LoRa-Modus trifft das nicht zu (anderes Register).
                                if (mode_868 == RADIO_MODE_LORA) {
                                    radio_868->clearIrq(0xFFFFFFFF);        // ← FIX: clearIrq()
                                }

                                int len868 = radio_868->getPacketLength();
                                // Fehlauslösung (z.B. durch CAD-IRQ): kein Paket vorhanden
                                if (len868 <= 0) {
                                    // Im FSK-Modus: clearIrq erst jetzt sicher (FIFO war leer)
                                    radio_868->clearIrq(0xFFFFFFFF);
                                    radio_868->startReceive();
                                } else {
                                    int16_t n868 = radio_868->readData(rx_buf_868,sizeof(rx_buf_868)); // 5ms Timeout

                                    // Im FSK-Modus: clearIrq NACH readData() - FIFO ist jetzt geleert
                                    if (mode_868 == RADIO_MODE_FSK) {
                                        radio_868->clearIrq(0xFFFFFFFF);
                                    }

                                    if (mode_868 == RADIO_MODE_LORA) {
                                        // --- LoRa: LoRaHAM-Binär-Header dekodieren ---
                                        uint32_t toNode      = rx_buf_868[0] | (rx_buf_868[1] << 8) | (rx_buf_868[2] << 16) | (rx_buf_868[3] << 24);
                                        uint32_t fromNode    = rx_buf_868[4] | (rx_buf_868[5] << 8) | (rx_buf_868[6] << 16) | (rx_buf_868[7] << 24);
                                        uint32_t uniqueID    = rx_buf_868[8] | (rx_buf_868[9] << 8) | (rx_buf_868[10] << 16) | (rx_buf_868[11] << 24);

                                        uint8_t hdrFlags     = rx_buf_868[12];
                                        uint8_t chHash       = rx_buf_868[13];
                                        uint8_t nextHop      = rx_buf_868[14];
                                        uint8_t rlyNodes     = rx_buf_868[15];

                                        printf("[\e[32m868\e[0m] %d Bytes HEX from Node ", len868);
                                        //            printf("%08X:", fromNode); // %08X für Hex mit führenden Nullen
                                        printf("\e[32m%08X\e[0m to ", fromNode); // %08X für Hex mit führenden Nullen
                                        printf("\e[32m%08X\e[0m ID:", toNode); // %08X für Hex mit führenden Nullen
                                        printf("\e[32m%08X\e[0m", uniqueID); // %08X für Hex mit führenden Nullen
                                        printf(" Flag:\e[32m%02X\e[0m", hdrFlags); // %08X für Hex mit führenden Nullen
                                        printf(" Hash:\e[32m%02X\e[0m", chHash); // %08X für Hex mit führenden Nullen
                                        printf(" Hop:\e[32m%02X\e[0m", nextHop); // %08X für Hex mit führenden Nullen
                                        printf(" Node:\e[32m%02X\e[0m", rlyNodes); // %08X für Hex mit führenden Nullen

                                        printf("\n");

                                        for (size_t i = 0; i < len868; i++) {
                                            printf("%02X ", rx_buf_868[i]);
                                        }
                                        printf("\n");

                                        printf("[\e[32m868\e[0m] %d Bytes    : ", len868);

                                        for (int i = 0; i < len868; i++) {
                                            if (rx_buf_868[i] >= 32 && rx_buf_868[i] <= 126)
                                                printf("%c", rx_buf_868[i]);
                                            else
                                                printf(".");
                                        }
                                        printf(" RSSI: %.2f dBm\n", radio_868->getRSSI());

                                    } else {
                                        // --- FSK: Rohdaten-Ausgabe (kein LoRaHAM-Header!) ---
                                        printf("[\e[32m868-FSK\e[0m] %d Bytes HEX: ", len868);
                                        for (int i = 0; i < len868; i++) {
                                            printf("%02X ", rx_buf_868[i]);
                                        }
                                        printf("\n");

                                        printf("[\e[32m868-FSK\e[0m] %d Bytes    : ", len868);
                                        for (int i = 0; i < len868; i++) {
                                            if (rx_buf_868[i] >= 32 && rx_buf_868[i] <= 126)
                                                printf("%c", rx_buf_868[i]);
                                            else
                                                printf(".");
                                        }
                                        printf(" RSSI: %.2f dBm\n", radio_868->getRSSI());
                                    }

                                    if(len868 > 0){
                                        client_set_broadcast_bytes(client_data868, MAX_CLIENTS, rx_buf_868, len868);
                                    }

                                    len868=0;
                                    memset(rx_buf_868, 0, sizeof(rx_buf_868) );
                                    n868=0;
                                    LED_868(0);
                                    //radio_868->standby();
                                    //radio_868->clearIrqFlags(0xFFFFFFFF);
                                    radio_868->startReceive();
                                }
                            } else {
                                // TX läuft noch - Flag zurücksetzen und IRQ clearen
                                receivedFlag868 = false;
                                radio_868->clearIrq(0xFFFFFFFF);
                                printf("[868] RX während TX - verwerfe Paket\n");
                            }
                        }


                        // --- CAD Überwachung ---
                        daemon_process_cad_status(433);
                        daemon_process_cad_status(868);

                        // ============================================================
                        // --- GETRSSI Streaming: 10 Hz RSSI an Conf-Clients ---
                        // ============================================================
                        // Sendet "RSSI=-87.50\n" auf demselben Conf-Socket auf dem
                        // SET GETRSSI=1 empfangen wurde (loraconf433.sock bzw.
                        // loraconf868.sock - identisches Pattern wie CAD=1/CAD=0).
                        //
                        // Quelle: read_live_rssi() liest das SX127x Hardware-
                        // Register direkt per SPI (RegRssiValue 0x1B im LoRa-,
                        // 0x11 im FSK-Mode). RadioLib's getRSSI() liefert im
                        // LoRa-Mode nur den RSSI des LETZTEN Pakets, daher der
                        // Direkt-Read.
                        //
                        // Funktioniert in LoRa- UND FSK-Modus, da getRSSI() das
                        // Analog-Frontend (RegRssiValue) ausliest. Detektiert daher
                        // auch Nicht-LoRa-Signale (Funkkopfhoerer, ISM-Sender etc.).
                        //
                        // RX laeuft parallel weiter: SPI-Read von RegRssiValue
                        // unterbricht weder Demodulation noch FIFO-Befuellung.
                        // Waehrend TX (txBusy) wird KEIN RSSI gelesen, da das
                        // Register dann undefinierte Werte liefert.
                        //
                        // Auto-Stop: sobald kein Conf-Client mehr verbunden ist,
                        // wird das Flag geloescht. Reconnect erfordert erneutes
                        // SET GETRSSI=1.
                        // ============================================================

                        daemon_process_rssi_stream(&rssi_timer);

    } // while stop not requested

    printf("[Daemon] Stop requested\n");
    daemon_shutdown_cleanup(&event_set);

    return 0;
}


