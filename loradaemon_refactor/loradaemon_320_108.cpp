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
#include <vector>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <arpa/inet.h>
//#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include <vector>
#include "hal/RPi/PiHal.h"
#include <RadioLib.h>
#include <lgpio.h>

#include "daemon_protocol.h"
#include "daemon_timing.h"
#include "event_loop.h"
#include "unix_socket.h"
#include "client_set.h"
#include "config_parser.h"
#include "radio_channel.h"

// --- Globale Socket-FDs
int data433_fd = -1, data868_fd = -1;
int conf433_fd = -1, conf868_fd = -1;

int client_data433[MAX_CLIENTS] = {0};
int client_data868[MAX_CLIENTS] = {0};
int client_conf433[MAX_CLIENTS] = {0};
int client_conf868[MAX_CLIENTS] = {0};

RadioChannelIo channel_433;
RadioChannelIo channel_868;
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

// --- Konfig-Parsing ---
// Erlaubte Keys: SF, BW, FREQ, CR, CRC, PREAMBLE, SYNC, LDRO, POWER
// LDRO: 0,1,AUTO
void apply_lora_param(SX1278 &radio, const char *tag, const std::string &key, const std::string &val) {
    int state = 0;

    if(key=="SF") {
        int sf=atoi(val.c_str());
        // Jetzt 7-12 als valid definiert
        if(sf>=7 && sf<=12) {
            state = radio.setSpreadingFactor(sf);
            if(state == 0) printf(" SF=\033[92m%d\033[0m", sf);
            else           printf(" SF=\033[91;5m%d\033[0m", sf);
        } else {
            // Ungültiger Bereich (z.B. 6 oder 13)
            printf(" SF=\033[91;5m%d\033[0m", sf);
        }
    }

    if(key=="BW") {
        double bw=atof(val.c_str());
        // Gültige Werte für SX1278 LoRa Modus in kHz
        if(bw == 7.8 || bw == 10.4 || bw == 15.6 || bw == 20.8 || bw == 31.25 ||
            bw == 41.7 || bw == 62.5 || bw == 125.0 || bw == 250.0 || bw == 500.0) {

            state = radio.setBandwidth(bw);
        if(state == 0) printf(" BW=\033[92m%.3f\033[0m", bw);
        else           printf(" BW=\033[91;5m%.3f\033[0m", bw);
            } else {
                // Ungültiger Wert (z.B. 200 oder 123) -> Rot blinkend
                printf(" BW=\033[91;5m%.3f\033[0m", bw);
            }
    }
    if(key=="FREQ") {
        double f=atof(val.c_str());
        if(f>0) {
            state = radio.setFrequency(f);
            if(state == 0) printf(" FREQ=\033[92m%.6f\033[0m", f);
            else           printf(" FREQ=\033[91;5m%.6f\033[0m", f);
        }
    }
    if(key=="CR") {
        int cr=atoi(val.c_str());
        // Gültige Coding Rates für LoRa sind 5, 6, 7 und 8
        if(cr >= 5 && cr <= 8) {
            state = radio.setCodingRate(cr);
            if(state == 0) printf(" CR=\033[92m%d\033[0m", cr);
            else           printf(" CR=\033[91;5m%d\033[0m", cr);
        } else {
            // Alles außer 5, 6, 7, 8 wird rot blinkend abgelehnt
            printf(" CR=\033[91;5m%d\033[0m", cr);
        }
    }

    if(key=="CRC") {
        int crc=atoi(val.c_str());
        // Nur 0 und 1 sind strikt valid
        if(crc==0 || crc==1) {
            state = radio.setCRC(crc!=0);
            if(state == 0) printf(" CRC=\033[92m%d\033[0m", crc);
            else           printf(" CRC=\033[91;5m%d\033[0m", crc);
        } else {
            // Wert wie '2' wird jetzt als Fehler angezeigt
            printf(" CRC=\033[91;5m%d\033[0m", crc);
        }
    }

    if(key=="PREAMBLE") {
        int pre=atoi(val.c_str());
        // Gültige Präambel-Länge für SX127x ist 6 bis 65535
        if(pre >= 6 && pre <= 65535) {
            state = radio.setPreambleLength(pre);
            if(state == 0) printf(" PREAMBLE=\033[92m%d\033[0m", pre);
            else           printf(" PREAMBLE=\033[91;5m%d\033[0m", pre);
        } else {
            // Werte unter 6 werden rot blinkend abgelehnt
            printf(" PREAMBLE=\033[91;5m%d\033[0m", pre);
        }
    }
    if(key=="SYNC") {
        uint8_t sw=0;
        if(val.rfind("0x",0)==0||val.rfind("0X",0)==0) sw=(uint8_t)strtoul(val.c_str(),NULL,16);
        else sw=(uint8_t)atoi(val.c_str());
        state = radio.setSyncWord(sw);
        if(state == 0) printf(" SYNC=\033[92m0x%02X\033[0m", sw);
        else           printf(" SYNC=\033[91;5m0x%02X\033[0m", sw);
    }
    if(key=="LDRO") {
        if(val=="AUTO"||val=="auto") {
            state = radio.autoLDRO();
            if(state == 0) printf(" LDRO=\033[92mAUTO\033[0m");
            else           printf(" LDRO=\033[91;5mAUTO\033[0m");
        } else {
            int l=atoi(val.c_str());
            state = radio.forceLDRO(l!=0);
            if(state == 0) printf(" LDRO=\033[92m%d\033[0m", l);
            else           printf(" LDRO=\033[91;5m%d\033[0m", l);
        }
    }
    if(key=="POWER") {
        int p = atoi(val.c_str());
        // Validierung für den Bereich 0 bis 20 dBm
        if(p >= 0 && p <= 20) {
            state = radio.setOutputPower(p);
            if(state == 0) printf(" POWER=\033[92m%d\033[0m", p);
            else           printf(" POWER=\033[91;5m%d\033[0m", p);
        } else {
            // Werte außerhalb von 0-20 werden rot blinkend abgelehnt
            printf(" POWER=\033[91;5m%d\033[0m", p);
        }
    }

    // Damit die Werte sofort im Terminal erscheinen ohne auf ein \n zu warten:
    fflush(stdout);
}

/*
 * void apply_lora_param(SX1278 &radio, const char *tag, const std::string &key, const std::string &val) {
 *    int state = 0;
 *
 *    if(key=="SF") {
 *        int sf=atoi(val.c_str());
 *        if(sf>=6&&sf<=12) {
 *            state = radio.setSpreadingFactor(sf);
 *            printf("[%s] SF=%d -> Code:%d\n", tag, sf, state);
 *        }
 *    }
 *    if(key=="BW") {
 *        double bw=atof(val.c_str());
 *        if(bw>0) {
 *            state = radio.setBandwidth(bw);
 *            printf("[%s] BW=%.3f -> Code:%d\n", tag, bw, state);
 *        }
 *    }
 *    if(key=="FREQ") {
 *        double f=atof(val.c_str());
 *        if(f>0) {
 *            state = radio.setFrequency(f);
 *            printf("[%s] FREQ=%.6f -> Code:%d\n", tag, f, state);
 *        }
 *    }
 *    if(key=="CR") {
 *        int cr=atoi(val.c_str());
 *        if(cr>=5&&cr<=8) {
 *            state = radio.setCodingRate(cr);
 *            printf("[%s] CR=%d -> Code:%d\n", tag, cr, state);
 *        }
 *    }
 *    if(key=="CRC") {
 *        int crc=atoi(val.c_str());
 *        state = radio.setCRC(crc!=0);
 *        printf("[%s] CRC=%d -> Code:%d\n", tag, crc, state);
 *    }
 *    if(key=="PREAMBLE") {
 *        int pre=atoi(val.c_str());
 *        if(pre>0) {
 *            state = radio.setPreambleLength(pre);
 *            printf("[%s] PREAMBLE=%d -> Code:%d\n", tag, pre, state);
 *        }
 *    }
 *    if(key=="SYNC") {
 *        uint8_t sw=0;
 *        if(val.rfind("0x",0)==0||val.rfind("0X",0)==0) sw=(uint8_t)strtoul(val.c_str(),NULL,16);
 *        else sw=(uint8_t)atoi(val.c_str());
 *        state = radio.setSyncWord(sw);
 *        printf("[%s] SYNC=0x%02X -> Code:%d\n", tag, sw, state);
 *    }
 *    if(key=="LDRO") {
 *        if(val=="AUTO"||val=="auto") {
 *            state = radio.autoLDRO();
 *            printf("[%s] LDRO=AUTO -> Code:%d\n", tag, state);
 *        } else {
 *            int l=atoi(val.c_str());
 *            state = radio.forceLDRO(l!=0);
 *            printf("[%s] LDRO=%d -> Code:%d\n", tag, l, state);
 *        }
 *    }
 *    if(key=="POWER") {
 *        int p=atoi(val.c_str());
 *        if(p>=1&&p<=20) {
 *            state = radio.setOutputPower(p);
 *            printf("[%s] POWER=%d -> Code:%d\n", tag, p, state);
 *        }
 *    }
 * }
 */


/*
 * void apply_lora_param(SX1278 &radio, const char *tag, const std::string &key, const std::string &val) {
 *    if(key=="SF")     { int sf=atoi(val.c_str());      if(sf>=6&&sf<=12)      { radio.setSpreadingFactor(sf);      printf(" SF=%d\n",tag,sf);      } }
 *    if(key=="BW")     { double bw=atof(val.c_str());   if(bw>0)               { radio.setBandwidth(bw);            printf(" BW=%.3f\n",tag,bw);    } }
 *    if(key=="FREQ")   { double f=atof(val.c_str());    if(f>0)                { radio.setFrequency(f);             printf(" FREQ=%.6f\n",tag,f);   } }
 *    if(key=="CR")     { int cr=atoi(val.c_str());      if(cr>=5&&cr<=8)       { radio.setCodingRate(cr);           printf(" CR=%d\n",tag,cr);      } }
 *    if(key=="CRC")    { int crc=atoi(val.c_str());                            { radio.setCRC(crc!=0);              printf(" CRC=%d\n",tag,crc);    } }
 *    if(key=="PREAMBLE"){int pre=atoi(val.c_str());     if(pre>0)              { radio.setPreambleLength(pre);      printf(" PREAMBLE=%d\n",tag,pre);} }
 *    if(key=="SYNC")   { uint8_t sw=0; if(val.rfind("0x",0)==0||val.rfind("0X",0)==0) sw=(uint8_t)strtoul(val.c_str(),NULL,16); else sw=(uint8_t)atoi(val.c_str()); radio.setSyncWord(sw); printf(" SYNC=0x%02X\n",tag,sw); }
 *    if(key=="LDRO")   { if(val=="AUTO"||val=="auto") { radio.autoLDRO(); printf(" LDRO=AUTO\n",tag); } else { int l=atoi(val.c_str()); radio.forceLDRO(l!=0); printf(" LDRO=%d\n",tag,l); } }
 *    if(key=="POWER")  { int p=atoi(val.c_str());       if(p>=1&&p<=20)        { radio.setOutputPower(p);           printf(" POWER=%d\n",tag,p);    } }
 * }
 */

void apply_lora_param(RFM95 &radio, const char *tag, const std::string &key, const std::string &val) {
    int state = 0;

    if(key=="SF") {
        int sf=atoi(val.c_str());
        // Jetzt 7-12 als valid definiert
        if(sf>=7 && sf<=12) {
            state = radio.setSpreadingFactor(sf);
            if(state == 0) printf(" SF=\033[92m%d\033[0m", sf);
            else           printf(" SF=\033[91;5m%d\033[0m", sf);
        } else {
            // Ungültiger Bereich (z.B. 6 oder 13)
            printf(" SF=\033[91;5m%d\033[0m", sf);
        }
    }

    if(key=="BW") {
        double bw=atof(val.c_str());
        // Gültige Werte für SX1278 LoRa Modus in kHz
        if(bw == 7.8 || bw == 10.4 || bw == 15.6 || bw == 20.8 || bw == 31.25 ||
           bw == 41.7 || bw == 62.5 || bw == 125.0 || bw == 250.0 || bw == 500.0) {

            state = radio.setBandwidth(bw);
            if(state == 0) printf(" BW=\033[92m%.3f\033[0m", bw);
            else           printf(" BW=\033[91;5m%.3f\033[0m", bw);
           } else {
               // Ungültiger Wert (z.B. 200 oder 123) -> Rot blinkend
               printf(" BW=\033[91;5m%.3f\033[0m", bw);
           }
    }
    if(key=="FREQ") {
        double f=atof(val.c_str());
        if(f>0) {
            state = radio.setFrequency(f);
            if(state == 0) printf(" FREQ=\033[92m%.6f\033[0m", f);
            else           printf(" FREQ=\033[91;5m%.6f\033[0m", f);
        }
    }
    if(key=="CR") {
        int cr=atoi(val.c_str());
        // Gültige Coding Rates für LoRa sind 5, 6, 7 und 8
        if(cr >= 5 && cr <= 8) {
            state = radio.setCodingRate(cr);
            if(state == 0) printf(" CR=\033[92m%d\033[0m", cr);
            else           printf(" CR=\033[91;5m%d\033[0m", cr);
        } else {
            // Alles außer 5, 6, 7, 8 wird rot blinkend abgelehnt
            printf(" CR=\033[91;5m%d\033[0m", cr);
        }
    }

    if(key=="CRC") {
        int crc=atoi(val.c_str());
        // Nur 0 und 1 sind strikt valid
        if(crc==0 || crc==1) {
            state = radio.setCRC(crc!=0);
            if(state == 0) printf(" CRC=\033[92m%d\033[0m", crc);
            else           printf(" CRC=\033[91;5m%d\033[0m", crc);
        } else {
            // Wert wie '2' wird jetzt als Fehler angezeigt
            printf(" CRC=\033[91;5m%d\033[0m", crc);
        }
    }

    if(key=="PREAMBLE") {
        int pre=atoi(val.c_str());
        // Gültige Präambel-Länge für SX127x ist 6 bis 65535
        if(pre >= 6 && pre <= 65535) {
            state = radio.setPreambleLength(pre);
            if(state == 0) printf(" PREAMBLE=\033[92m%d\033[0m", pre);
            else           printf(" PREAMBLE=\033[91;5m%d\033[0m", pre);
        } else {
            // Werte unter 6 werden rot blinkend abgelehnt
            printf(" PREAMBLE=\033[91;5m%d\033[0m", pre);
        }
    }
    if(key=="SYNC") {
        uint8_t sw=0;
        if(val.rfind("0x",0)==0||val.rfind("0X",0)==0) sw=(uint8_t)strtoul(val.c_str(),NULL,16);
        else sw=(uint8_t)atoi(val.c_str());
        state = radio.setSyncWord(sw);
        if(state == 0) printf(" SYNC=\033[92m0x%02X\033[0m", sw);
        else           printf(" SYNC=\033[91;5m0x%02X\033[0m", sw);
    }
    if(key=="LDRO") {
        if(val=="AUTO"||val=="auto") {
            state = radio.autoLDRO();
            if(state == 0) printf(" LDRO=\033[92mAUTO\033[0m");
            else           printf(" LDRO=\033[91;5mAUTO\033[0m");
        } else {
            int l=atoi(val.c_str());
            state = radio.forceLDRO(l!=0);
            if(state == 0) printf(" LDRO=\033[92m%d\033[0m", l);
            else           printf(" LDRO=\033[91;5m%d\033[0m", l);
        }
    }
    if(key=="POWER") {
        int p = atoi(val.c_str());
        // Validierung für den Bereich 0 bis 20 dBm
        if(p >= 0 && p <= 20) {
            state = radio.setOutputPower(p);
            if(state == 0) printf(" POWER=\033[92m%d\033[0m", p);
            else           printf(" POWER=\033[91;5m%d\033[0m", p);
        } else {
            // Werte außerhalb von 0-20 werden rot blinkend abgelehnt
            printf(" POWER=\033[91;5m%d\033[0m", p);
        }
    }

    // Damit die Werte sofort im Terminal erscheinen ohne auf ein \n zu warten:
    fflush(stdout);
}
/*
 * void apply_lora_param(RFM95 &radio, const char *tag, const std::string &key, const std::string &val) {
 *    if (key == "SF")      { int sf = atoi(val.c_str());      if (sf >= 6 && sf <= 12)      { radio.setSpreadingFactor(sf);      printf(" SF=%d\n", tag, sf);      } }
 *    if (key == "BW")      { double bw = atof(val.c_str());   if (bw > 0)                    { radio.setBandwidth(bw);            printf(" BW=%.3f\n", tag, bw);    } }
 *    if (key == "FREQ")    { double f = atof(val.c_str());    if (f > 0)                     { radio.setFrequency(f);             printf(" FREQ=%.6f\n", tag, f);   } }
 *    if (key == "CR")      { int cr = atoi(val.c_str());      if (cr >= 5 && cr <= 8)       { radio.setCodingRate(cr);           printf(" CR=%d\n", tag, cr);      } }
 *    if (key == "CRC")     { int crc = atoi(val.c_str());                                    { radio.setCRC(crc != 0);            printf(" CRC=%d\n", tag, crc);    } }
 *    if (key == "PREAMBLE"){ int pre = atoi(val.c_str());     if (pre > 0)                   { radio.setPreambleLength(pre);      printf(" PREAMBLE=%d\n", tag, pre);} }
 *    if (key == "SYNC")    { uint8_t sw = 0; if (val.rfind("0x",0)==0 || val.rfind("0X",0)==0) sw=(uint8_t)strtoul(val.c_str(),NULL,16); else sw=(uint8_t)atoi(val.c_str()); radio.setSyncWord(sw); printf(" SYNC=0x%02X\n", tag, sw); }
 *    if (key == "LDRO")    { if (val == "AUTO" || val == "auto") { radio.autoLDRO(); printf(" LDRO=AUTO\n", tag); } else { int l = atoi(val.c_str()); radio.forceLDRO(l != 0); printf(" LDRO=%d\n", tag, l); } }
 *    if (key == "POWER")   { int p = atoi(val.c_str());       if (p >= 1 && p <= 20)        { radio.setOutputPower(p);           printf(" POWER=%d\n", tag, p);    } }
 * }
 */

// --- FSK Parameter anwenden: SX1278 ---
// Gültige SET-Keys im FSK-Modus: FREQ, POWER, BR, FREQDEV, RXBW, OOK, SHAPING, ENCODING, PREAMBLE, SYNC
void apply_fsk_param(SX1278 &radio, const char *tag, const std::string &key, const std::string &val) {
    int state = 0;

    if(key=="FREQ") {
        double f = atof(val.c_str());
        if(f > 0) {
            state = radio.setFrequency(f);
            if(state == 0) printf(" FREQ=\033[92m%.6f\033[0m", f);
            else           printf(" FREQ=\033[91;5m%.6f\033[0m", f);
        }
    }
    if(key=="POWER") {
        int p = atoi(val.c_str());
        if(p >= 0 && p <= 20) {
            state = radio.setOutputPower(p);
            if(state == 0) printf(" POWER=\033[92m%d\033[0m", p);
            else           printf(" POWER=\033[91;5m%d\033[0m", p);
        } else {
            printf(" POWER=\033[91;5m%d\033[0m", p);
        }
    }
    if(key=="BR") {
        // Bitrate in kbps (z.B. 4.8, 9.6, 19.2, 38.4, 115.2)
        float br = (float)atof(val.c_str());
        if(br > 0) {
            state = radio.setBitRate(br);
            if(state == 0) printf(" BR=\033[92m%.3f\033[0m", br);
            else           printf(" BR=\033[91;5m%.3f\033[0m", br);
        }
    }
    if(key=="FREQDEV") {
        // Frequenzhub in kHz (z.B. 5.0, 10.0, 20.0)
        float fd = (float)atof(val.c_str());
        if(fd > 0) {
            state = radio.setFrequencyDeviation(fd);
            if(state == 0) printf(" FREQDEV=\033[92m%.3f\033[0m", fd);
            else           printf(" FREQDEV=\033[91;5m%.3f\033[0m", fd);
        }
    }
    if(key=="RXBW") {
        // RX-Filterbandbreite in kHz
        // Gültige Werte SX1278 FSK: 2.6, 3.1, 3.9, 5.2, 6.3, 7.8, 10.4, 12.5, 15.6,
        //                            20.8, 25.0, 31.3, 41.7, 50.0, 62.5, 83.3, 100.0,
        //                            125.0, 166.7, 200.0, 250.0
        float bw = (float)atof(val.c_str());
        if(bw > 0) {
            state = radio.setRxBandwidth(bw);
            if(state == 0) printf(" RXBW=\033[92m%.3f\033[0m", bw);
            else           printf(" RXBW=\033[91;5m%.3f\033[0m", bw);
        }
    }
    if(key=="OOK") {
        // OOK-Modus: 1=ein, 0=aus (normales FSK)
        int ook = atoi(val.c_str());
        if(ook == 0 || ook == 1) {
            state = radio.setOOK(ook != 0);
            if(state == 0) printf(" OOK=\033[92m%d\033[0m", ook);
            else           printf(" OOK=\033[91;5m%d\033[0m", ook);
        } else {
            printf(" OOK=\033[91;5m%d\033[0m", ook);
        }
    }
    if(key=="SHAPING") {
        // Gauss-Filter Shaping: 0.0=aus, 0.3, 0.5, 1.0
        float sh = (float)atof(val.c_str());
        state = radio.setDataShaping(sh);
        if(state == 0) printf(" SHAPING=\033[92m%.1f\033[0m", sh);
        else           printf(" SHAPING=\033[91;5m%.1f\033[0m", sh);
    }
    if(key=="ENCODING") {
        // 0=NRZ, 1=Manchester, 2=Whitening
        uint8_t enc = (uint8_t)atoi(val.c_str());
        if(enc <= 2) {
            state = radio.setEncoding(enc);
            if(state == 0) printf(" ENCODING=\033[92m%d\033[0m", enc);
            else           printf(" ENCODING=\033[91;5m%d\033[0m", enc);
        } else {
            printf(" ENCODING=\033[91;5m%d\033[0m", enc);
        }
    }
    if(key=="PREAMBLE") {
        int pre = atoi(val.c_str());
        if(pre >= 0) {
            state = radio.setPreambleLength(pre);
            if(state == 0) printf(" PREAMBLE=\033[92m%d\033[0m", pre);
            else           printf(" PREAMBLE=\033[91;5m%d\033[0m", pre);
        }
    }
    if(key=="SYNC") {
        // FSK SyncWord: 1 Byte (0xXX) oder 2 Bytes (0xXXXX)
        // RadioLib: setSyncWord(uint8_t* sync, size_t len, uint8_t maxErrBits=0)
        uint32_t sw_raw = 0;
        if(val.rfind("0x",0)==0||val.rfind("0X",0)==0)
            sw_raw = (uint32_t)strtoul(val.c_str(), NULL, 16);
        else
            sw_raw = (uint32_t)atoi(val.c_str());
        if(sw_raw <= 0xFF) {
            // 1 Byte SyncWord
            uint8_t sw[1] = { (uint8_t)sw_raw };
            state = radio.setSyncWord(sw, 1);
            if(state == 0) printf(" SYNC=\033[92m0x%02X\033[0m", sw[0]);
            else           printf(" SYNC=\033[91;5m0x%02X\033[0m", sw[0]);
        } else if(sw_raw <= 0xFFFF) {
            // 2 Byte SyncWord
            uint8_t sw[2] = { (uint8_t)(sw_raw >> 8), (uint8_t)(sw_raw & 0xFF) };
            state = radio.setSyncWord(sw, 2);
            if(state == 0) printf(" SYNC=\033[92m0x%04X\033[0m", sw_raw);
            else           printf(" SYNC=\033[91;5m0x%04X\033[0m", sw_raw);
        }
    }
    fflush(stdout);
}

// --- FSK Parameter anwenden: RFM95 ---
// Identische Keys wie SX1278-Variante (RFM95 basiert ebenfalls auf SX127x)
void apply_fsk_param(RFM95 &radio, const char *tag, const std::string &key, const std::string &val) {
    int state = 0;

    if(key=="FREQ") {
        double f = atof(val.c_str());
        if(f > 0) {
            state = radio.setFrequency(f);
            if(state == 0) printf(" FREQ=\033[92m%.6f\033[0m", f);
            else           printf(" FREQ=\033[91;5m%.6f\033[0m", f);
        }
    }
    if(key=="POWER") {
        int p = atoi(val.c_str());
        if(p >= 0 && p <= 20) {
            state = radio.setOutputPower(p);
            if(state == 0) printf(" POWER=\033[92m%d\033[0m", p);
            else           printf(" POWER=\033[91;5m%d\033[0m", p);
        } else {
            printf(" POWER=\033[91;5m%d\033[0m", p);
        }
    }
    if(key=="BR") {
        float br = (float)atof(val.c_str());
        if(br > 0) {
            state = radio.setBitRate(br);
            if(state == 0) printf(" BR=\033[92m%.3f\033[0m", br);
            else           printf(" BR=\033[91;5m%.3f\033[0m", br);
        }
    }
    if(key=="FREQDEV") {
        float fd = (float)atof(val.c_str());
        if(fd > 0) {
            state = radio.setFrequencyDeviation(fd);
            if(state == 0) printf(" FREQDEV=\033[92m%.3f\033[0m", fd);
            else           printf(" FREQDEV=\033[91;5m%.3f\033[0m", fd);
        }
    }
    if(key=="RXBW") {
        float bw = (float)atof(val.c_str());
        if(bw > 0) {
            state = radio.setRxBandwidth(bw);
            if(state == 0) printf(" RXBW=\033[92m%.3f\033[0m", bw);
            else           printf(" RXBW=\033[91;5m%.3f\033[0m", bw);
        }
    }
    if(key=="OOK") {
        int ook = atoi(val.c_str());
        if(ook == 0 || ook == 1) {
            state = radio.setOOK(ook != 0);
            if(state == 0) printf(" OOK=\033[92m%d\033[0m", ook);
            else           printf(" OOK=\033[91;5m%d\033[0m", ook);
        } else {
            printf(" OOK=\033[91;5m%d\033[0m", ook);
        }
    }
    if(key=="SHAPING") {
        float sh = (float)atof(val.c_str());
        state = radio.setDataShaping(sh);
        if(state == 0) printf(" SHAPING=\033[92m%.1f\033[0m", sh);
        else           printf(" SHAPING=\033[91;5m%.1f\033[0m", sh);
    }
    if(key=="ENCODING") {
        uint8_t enc = (uint8_t)atoi(val.c_str());
        if(enc <= 2) {
            state = radio.setEncoding(enc);
            if(state == 0) printf(" ENCODING=\033[92m%d\033[0m", enc);
            else           printf(" ENCODING=\033[91;5m%d\033[0m", enc);
        } else {
            printf(" ENCODING=\033[91;5m%d\033[0m", enc);
        }
    }
    if(key=="PREAMBLE") {
        int pre = atoi(val.c_str());
        if(pre >= 0) {
            state = radio.setPreambleLength(pre);
            if(state == 0) printf(" PREAMBLE=\033[92m%d\033[0m", pre);
            else           printf(" PREAMBLE=\033[91;5m%d\033[0m", pre);
        }
    }
    if(key=="SYNC") {
        uint32_t sw_raw = 0;
        if(val.rfind("0x",0)==0||val.rfind("0X",0)==0)
            sw_raw = (uint32_t)strtoul(val.c_str(), NULL, 16);
        else
            sw_raw = (uint32_t)atoi(val.c_str());
        if(sw_raw <= 0xFF) {
            uint8_t sw[1] = { (uint8_t)sw_raw };
            state = radio.setSyncWord(sw, 1);
            if(state == 0) printf(" SYNC=\033[92m0x%02X\033[0m", sw[0]);
            else           printf(" SYNC=\033[91;5m0x%02X\033[0m", sw[0]);
        } else if(sw_raw <= 0xFFFF) {
            uint8_t sw[2] = { (uint8_t)(sw_raw >> 8), (uint8_t)(sw_raw & 0xFF) };
            state = radio.setSyncWord(sw, 2);
            if(state == 0) printf(" SYNC=\033[92m0x%04X\033[0m", sw_raw);
            else           printf(" SYNC=\033[91;5m0x%04X\033[0m", sw_raw);
        }
    }
    fflush(stdout);
}

// gemeinsamer Parser: Modul + Tag werden übergeben
// mode_flag:    Referenz auf mode_433 oder mode_868   - wird bei MODE=FSK/LORA umgeschaltet
// getrssi_flag: Referenz auf getrssi_433_active oder getrssi_868_active
//               - wird bei GETRSSI=1/0 gesetzt (kontinuierlicher RSSI-Stream)
// Protokoll:
//   SET MODE=FSK    -> beginFSK() + mode_flag=FSK  (Pflicht um FSK zu aktivieren)
//   SET MODE=LORA   -> begin()    + mode_flag=LORA  (Re-Initialisierung LoRa)
//   SET GETRSSI=1   -> getrssi_flag=true  (10 Hz RSSI-Stream an Conf-Clients)
//   SET GETRSSI=0   -> getrssi_flag=false (Stream aus)
//   Kein MODE=      -> aktueller Modus bleibt, volle Rückwärtskompatibilität
template<typename RadioT>
void parse_and_apply_config_generic(RadioT &radio, const char *tag, const char *cmd,
                                    volatile RadioMode_t &mode_flag,
                                    volatile bool &getrssi_flag) {
    ConfigCommand parsed = config_parse_command(cmd);

    if(!parsed.is_set) {
        printf("[%s] Unbekannter Befehl: %s\n", tag, parsed.text.c_str());
        return;
    }

    if(!parsed.has_params)
        return;

    // --- 1. Pass: MODE= zuerst, GETRSSI= direkt, Rest sammeln ---
    std::vector<std::pair<std::string,std::string>> tokens;
    std::string mode_val = parsed.mode;

    for(auto &kv : parsed.tokens) {
        const std::string &key = kv.first;
        const std::string &val = kv.second;

        if(key == "GETRSSI") {
            int v = atoi(val.c_str());
            if(v == 0 || v == 1) {
                getrssi_flag = (v != 0);
                if(v == 1) printf(" GETRSSI=[92m1[0m");
                else       printf(" GETRSSI=[92m0[0m");
            } else {
                printf(" GETRSSI=[91;5m%d[0m", v);
            }
        } else {
            tokens.push_back(kv);
        }
    }

    // --- MODE= auswerten und Radio ggf. neu initialisieren ---
    if(!mode_val.empty()) {
        if(mode_val == "FSK") {
            printf("[%s] MODE=FSK -> beginFSK()", tag);
            int state = radio.beginFSK();
            if(state == RADIOLIB_ERR_NONE) {
                mode_flag = RADIO_MODE_FSK;
                printf(" \033[92mOK\033[0m");
            } else {
                printf(" \033[91;5mFEHLER:%d\033[0m", state);
            }
        } else if(mode_val == "LORA") {
            printf("[%s] MODE=LORA -> begin()", tag);
            int state = radio.begin();
            if(state == RADIOLIB_ERR_NONE) {
                mode_flag = RADIO_MODE_LORA;
                printf(" \033[92mOK\033[0m");
            } else {
                printf(" \033[91;5mFEHLER:%d\033[0m", state);
            }
        } else {
            printf("[%s] MODE=\033[91;5m%s\033[0m (unbekannt, ignoriert)", tag, mode_val.c_str());
        }
    }

    // --- 2. Pass: Alle übrigen Parameter anwenden ---
    // Je nach aktivem Modus werden LoRa- oder FSK-Parameter-Funktion aufgerufen.
    // FSK-Keys (BR, FREQDEV, RXBW, OOK, SHAPING, ENCODING) im LoRa-Modus -> Warnung
    // LoRa-Keys (SF, BW, CR, LDRO) im FSK-Modus -> Warnung
    for(auto &kv : tokens) {
        const std::string &key = kv.first;
        const std::string &val = kv.second;

        if(mode_flag == RADIO_MODE_FSK) {
            // Im FSK-Modus: LoRa-spezifische Keys abweisen
            if(key=="SF" || key=="BW" || key=="CR" || key=="LDRO" || key=="CRC") {
                printf(" \033[93m%s=IGNORIERT(LoRa-Key im FSK-Modus)\033[0m", key.c_str());
            } else {
                apply_fsk_param(radio, tag, key, val);
            }
        } else {
            // Im LoRa-Modus (Default): FSK-spezifische Keys abweisen
            if(key=="BR" || key=="FREQDEV" || key=="RXBW" || key=="OOK" ||
                key=="SHAPING" || key=="ENCODING") {
                printf(" \033[93m%s=IGNORIERT(FSK-Key im LoRa-Modus, SET MODE=FSK fehlt)\033[0m", key.c_str());
                } else {
                    apply_lora_param(radio, tag, key, val);
                }
        }
    }
    printf("\n");
}



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

    EventLoopSelectSet event_set;
    fd_set readfds;
    uint8_t buf[buf_SIZE];
    uint8_t tx_buf[buf_SIZE];  // ← NEU: nur zum Senden
    uint8_t rx_buf_433[buf_SIZE];  // ← GETRENNTE Buffer pro Band!
    uint8_t rx_buf_868[buf_SIZE];  // ← GETRENNTE Buffer pro Band!
    uint8_t len_buf;           // ← NEU!

    uint8_t len;
    // --- CAD-Polling-Steuerung: alterniert zwischen 433 und 868 ---
    int cad_counter = 0;
    int cad_band    = 0;   // 0 = nächster CAD-Scan für 433, 1 = für 868

    // --- GETRSSI Timer ---
    DaemonDeadlineTimer rssi_timer;
    daemon_deadline_timer_init(&rssi_timer, daemon_now_ms(), DAEMON_RSSI_INTERVAL_MS);


    printf("[Daemon] Starte Polling-Loop für LoRa und Sockets\n");


    while (1) {

        event_loop_select_reset(&event_set);
        radio_channel_add_fds(&channel_433, &event_set);
        radio_channel_add_fds(&channel_868, &event_set);

        // --- Select wait ---
        int ret = event_loop_select_wait(&event_set, &readfds, DAEMON_SELECT_TIMEOUT_USEC);
        if(ret<0){perror("select"); continue;}

        // --- Neue Clients ---
        radio_channel_accept_ready(&channel_433, &readfds);
        radio_channel_accept_ready(&channel_868, &readfds);


                        // --- DATA433-Clients bearbeiten ---
                        for(int i=0;i<MAX_CLIENTS;i++){
                            if(client_data433[i] > 0 && event_loop_select_ready_fd(&readfds, client_data433[i])) {
                                uint8_t large_buf[2048]; // Großer Puffer für den Socket-Eingang
                                ssize_t n = read(client_data433[i], large_buf, sizeof(large_buf));

                                if(n <= 0) {
                                    client_set_close_slot(client_data433, i);
                                } else {
                                    printf("[DEBUG 433] %zd Bytes vom Socket erhalten. Zerteile in LoRa-Pakete...\n", n);

                                    ssize_t bytes_sent = 0;
                                    while(bytes_sent < n) {
                                        // Berechne Größe des nächsten Fragments (max 255)
                                        ssize_t chunk_size = n - bytes_sent;
                                        if(chunk_size > 255) chunk_size = 255;

                                        // --- CAD-Guard: Kanal frei? ---
                                        // Nur im LoRa-Modus: getModemStatus() ist FSK-inkompatibel
                                        // Im FSK-Modus: sofort senden (kein CSMA)
                                        if (mode_433 == RADIO_MODE_LORA) {
                                            // Warten bis Modem nicht mehr aktiv (max. 3 Sekunden für SF12)
                                            int cad_wait = 0;
                                            while (((radio_433->getModemStatus() & 0x01)) && cad_wait < 300) {
                                                // Bit0=Signal, Bit4=Header: Kanal noch belegt
                                                usleep(10000); // 10ms warten
                                                cad_wait++;
                                            }
                                            if (cad_wait >= 300) {
                                                printf("[433] CAD-Timeout: Kanal dauerhaft belegt, Paket verworfen\n");
                                                break;
                                            }
                                        } // end CAD-Guard LoRa

                                        printf("  -> Sende Chunk: %zd Bytes (Offset: %zd)\n", chunk_size, bytes_sent);

                                        LED_433(1);
                                        // Sende den Teilbereich des Puffers
                                        lora_send(large_buf + bytes_sent, chunk_size, 433);
                                        LED_433(0);

                                        bytes_sent += chunk_size;

                                        // Optional: Kleines Delay zwischen Paketen, damit der Empfänger Zeit hat
                                        // usleep(50000); // 50ms Pause
                                    }
                                }
                            }

                            /*
                             *                if(client_data433[i] > 0 && FD_ISSET(client_data433[i], &readfds)) {
                             *                        // Roh-Daten direkt in den tx_buf lesen (maximal bis zum LoRa-Limit von 255)
                             *                        ssize_t n = read(client_data433[i], tx_buf, 255);
                             *
                             *                        if(n <= 0) {
                             *                            // Client hat Verbindung geschlossen oder Fehler
                             *                            close(client_data433[i]);
                             *                            client_data433[i] = 0;
                        }
                        else {
                            // Optionale Filterung: z.B. nur senden, wenn nicht nur ein '\n' ankommt
                            if (n == 1 && tx_buf[0] == '\n') continue;

                            printf("[DEBUG 433] Roh-Empfang: n=%zd, Daten: ", n);
                            for(int j=0; j<n; j++) printf("%02X ", tx_buf[j]);
                            printf("\n");

                            LED_433(1);  // LED an
                            lora_send(tx_buf, n, 433);  // Sende exakt n Bytes
                            LED_433(0);  // LED aus
                        }
                        }
                        */
                            //            if(client_data433[i]>0 && FD_ISSET(client_data433[i],&readfds)){
                            //                ssize_t n = recv_frame_nonblocking(client_data433[i],tx_buf,&len_buf);
                            //                if(n<=0){/*printf("DATA433-Client getrennt.\n");*/close(client_data433[i]);client_data433[i]=0;}
                            //                else {
                            //                    printf("[DEBUG 433] Empfangen: len_buf=%d, n=%zd, Daten: ", len_buf, n);
                            //                    for(int j=0; j<n; j++) printf("%02X ", tx_buf[j]);
                            //                    printf("\n");
                            //                    LED_433(1);
                            //                    lora_send(tx_buf, n, 433);  // ← WICHTIG: n verwenden, nicht len_buf!
                            //                    LED_433(0);
                            //                }
                            //            }

                        }

                        // --- DATA868-Clients bearbeiten ---
                        for(int i=0;i<MAX_CLIENTS;i++){

                            if(client_data868[i] > 0 && event_loop_select_ready_fd(&readfds, client_data868[i])) {
                                uint8_t large_buf[2048]; // Großer Puffer für den Socket-Eingang
                                ssize_t n = read(client_data868[i], large_buf, sizeof(large_buf));

                                if(n <= 0) {
                                    client_set_close_slot(client_data868, i);
                                } else {
                                    printf("[DEBUG 868] %zd Bytes vom Socket erhalten. Zerteile in LoRa-Pakete...\n", n);

                                    ssize_t bytes_sent = 0;
                                    while(bytes_sent < n) {
                                        // Berechne Größe des nächsten Fragments (max 255)
                                        ssize_t chunk_size = n - bytes_sent;
                                        if(chunk_size > 255) chunk_size = 255;

                                        // --- CAD-Guard: Kanal frei? ---
                                        // Nur im LoRa-Modus: getModemStatus() ist FSK-inkompatibel
                                        // Im FSK-Modus: sofort senden (kein CSMA)
                                        if (mode_868 == RADIO_MODE_LORA) {
                                            // Warten bis Modem nicht mehr aktiv (max. 3 Sekunden für SF12)
                                            int cad_wait = 0;
                                            while (((radio_868->getModemStatus() & 0x01)) && cad_wait < 300) {
                                                // Bit0=Signal, Bit4=Header: Kanal noch belegt
                                                usleep(10000); // 10ms warten
                                                cad_wait++;
                                            }
                                            if (cad_wait >= 300) {
                                                printf("[868] CAD-Timeout: Kanal dauerhaft belegt, Paket verworfen\n");
                                                break;
                                            }
                                        } // end CAD-Guard LoRa

                                        printf("  -> Sende Chunk: %zd Bytes (Offset: %zd)\n", chunk_size, bytes_sent);

                                        LED_868(1);
                                        // Sende den Teilbereich des Puffers
                                        lora_send(large_buf + bytes_sent, chunk_size, 868);
                                        LED_868(0);

                                        bytes_sent += chunk_size;

                                        // Optional: Kleines Delay zwischen Paketen, damit der Empfänger Zeit hat
                                        // usleep(50000); // 50ms Pause
                                    }
                                }
                            }

                            /*
                             *                if(client_data868[i] > 0 && FD_ISSET(client_data868[i], &readfds)){
                             *                                // Wir lesen direkt vom Socket ohne Längen-Header-Byte
                             *                                // n ist die Anzahl der tatsächlich empfangenen Bytes
                             *                                ssize_t n = read(client_data868[i], tx_buf, 255);
                             *
                             *                                if(n <= 0){
                             *                                    // Client hat die Verbindung geschlossen
                             *                                    close(client_data868[i]);
                             *                                    client_data868[i] = 0;
                        }
                        else {
                            // Verhindert das Senden von leeren Paketen, falls nur ein ENTER ankommt
                            if (n == 1 && tx_buf[0] == '\n') continue;

                            printf("[DEBUG 868] Roh-Empfang: n=%zd, Daten: ", n);
                            for(int j=0; j<n; j++) printf("%02X ", tx_buf[j]);
                            printf("\n");

                            LED_868(1);
                            lora_send(tx_buf, n, 868);  // Sende exakt n Bytes über das 868 MHz Modul
                            LED_868(0);
                        }
                        }
                        */
                            //            if(client_data868[i]>0 && FD_ISSET(client_data868[i],&readfds)){
                            //                ssize_t n = recv_frame_nonblocking(client_data868[i],tx_buf,&len_buf);
                            //                if(n<=0){/*printf("DATA868-Client getrennt.\n");*/close(client_data868[i]);client_data868[i]=0;}
                            //                else {
                            //                    printf("[DEBUG 868] Empfangen: len_buf=%d, n=%zd, Daten: ", len_buf, n);
                            //                    for(int j=0; j<n; j++) printf("%02X ", tx_buf[j]);
                            //                    printf("\n");
                            //                    LED_868(1);
                            //                    lora_send(tx_buf, n, 868);  // ← WICHTIG: n verwenden, nicht len_buf!
                            //                    LED_868(0);
                            //                }
                            //            }
                        }

                        // --- CONFIG Clients bearbeiten ---
                        // parse_config kann hier eingefügt werden
                        for(int i=0;i<MAX_CLIENTS;i++){
                            if(client_conf433[i]>0 && event_loop_select_ready_fd(&readfds, client_conf433[i])){
                                ssize_t n = read(client_conf433[i],buf,buf_SIZE-1);
                                if(n<=0){
                                    client_set_close_slot(client_conf433, i);
                                } else {
                                    buf[n]='\0';
                                    printf("[CONF433]");
                                    parse_and_apply_config_generic<SX1278>(*radio_433, "CONF 433", (char*)buf, mode_433, getrssi_433_active);
                                    // Callback nach jedem Konfig-Wechsel neu setzen:
                                    // beginFSK() / begin() löscht den IRQ-Callback im Modul!
                                    radio_433->setPacketReceivedAction(setFlag433);
                                    radio_433->startReceive();
                                    //printf("/n");
                                    // AUTO-TEST: Wenn SF=12 und Freq=433.775, sende TEST

                                    /*
                                     *                    printf("[AUTO-TEST] Sende 'TEST-12' auf 433.775...\n");
                                     *                    uint8_t test_msg[] = "TEST-12";
                                     *                    lora_send(test_msg, 7, 433);
                                     *                    printf("[AUTO-TEST] Fertig!\n");
                                     */
                                }
                            }
                            if(client_conf868[i]>0 && event_loop_select_ready_fd(&readfds, client_conf868[i])){
                                ssize_t n = read(client_conf868[i],buf,buf_SIZE-1);
                                if(n<=0){
                                    client_set_close_slot(client_conf868, i);
                                } else {
                                    buf[n]='\0';
                                    parse_and_apply_config_generic<RFM95>(*radio_868, "CONF 868", (char*)buf, mode_868, getrssi_868_active);
                                    // Callback nach jedem Konfig-Wechsel neu setzen:
                                    // beginFSK() / begin() löscht den IRQ-Callback im Modul!
                                    radio_868->setPacketReceivedAction(setFlag868);
                                    radio_868->startReceive();
                                }
                            }
                        }




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


                        // --- 433 MHz Überwachung ---
                        // getModemStatus() ist LoRa-spezifisch -> im FSK-Modus überspringen
                        if (mode_433 == RADIO_MODE_LORA) {
                            uint8_t modem433 = radio_433->getModemStatus();
                            // Bit 0: Signal erkannt, Bit 4: Header erkannt
                            bool hardwareActive433 = (modem433 & 0x01) || (modem433 & 0x10);

                            if (hardwareActive433) {
                                setFlashFlag433();
                                if (!cad433_active) {
                                    LED_433(1);
                                    client_set_broadcast(client_conf433, MAX_CLIENTS, "CAD=1\n");
                                    //printf("[\e[93m433\e[0m] CAD: Kanal belegt\n");
                                    cad433_active = true;
                                }
                            } else {
                                // Wenn das Modem NICHTS mehr sieht UND wir gerade kein Paket verarbeiten
                                if (cad433_active && !receivedFlag433) {
                                    LED_433(0);
                                    client_set_broadcast(client_conf433, MAX_CLIENTS, "CAD=0\n");
                                    //printf("[\e[93m433\e[0m] CAD: Kanal frei\n");
                                    cad433_active = false;
                                }
                            }
                        } // end mode_433 == RADIO_MODE_LORA

                        // --- 868 MHz Überwachung ---
                        // getModemStatus() ist LoRa-spezifisch -> im FSK-Modus überspringen
                        if (mode_868 == RADIO_MODE_LORA) {
                            uint8_t modem868 = radio_868->getModemStatus();
                            // Bit 0: Signal erkannt, Bit 4: Header erkannt
                            bool hardwareActive868 = (modem868 & 0x01) || (modem868 & 0x10);

                            if (hardwareActive868) {
                                setFlashFlag868();
                                if (!cad868_active) {
                                    LED_868(1);
                                    client_set_broadcast(client_conf868, MAX_CLIENTS, "CAD=1\n");
                                    //printf("[\e[93m868\e[0m] CAD: Kanal belegt\n");
                                    cad868_active = true;
                                }
                            } else {
                                // Wenn das Modem NICHTS mehr sieht UND wir gerade kein Paket verarbeiten
                                if (cad868_active && !receivedFlag868) {
                                    LED_868(0);
                                    client_set_broadcast(client_conf868, MAX_CLIENTS, "CAD=0\n");
                                    //printf("[\e[93m868\e[0m] CAD: Kanal frei\n");
                                    cad868_active = false;
                                }
                            }
                        } // end mode_868 == RADIO_MODE_LORA

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

                        // --- Auto-Stop: Pruefe ob noch ein Conf-Client verbunden ist ---
                        {
                            radio_channel_getrssi_autostop(&channel_433, &runtime_433, "CONF 433");
                            radio_channel_getrssi_autostop(&channel_868, &runtime_868, "CONF 868");
                        }

                        // Functional change: RSSI cadence is now time-based.
                        if(daemon_deadline_timer_due(&rssi_timer, daemon_now_ms())) {

                            // 433: nur lesen wenn aktiv und kein TX laeuft
                            if(getrssi_433_active && !txBusy433) {
                                float rssi433 = radio_channel_read_live_rssi(mod_433, mode_433, false);
                                char rssi_msg[32];
                                snprintf(rssi_msg, sizeof(rssi_msg), "RSSI=%.2f\n", rssi433);
                                client_set_broadcast(client_conf433, MAX_CLIENTS, rssi_msg);
                            }

                            // 868: nur lesen wenn aktiv und kein TX laeuft
                            if(getrssi_868_active && !txBusy868) {
                                float rssi868 = radio_channel_read_live_rssi(mod_868, mode_868, true);
                                char rssi_msg[32];
                                snprintf(rssi_msg, sizeof(rssi_msg), "RSSI=%.2f\n", rssi868);
                                client_set_broadcast(client_conf868, MAX_CLIENTS, rssi_msg);
                            }
                        }

    } // while(1)

    close(data433_fd); close(data868_fd); close(conf433_fd); close(conf868_fd);
    unlink(DATA433_SOCKET); unlink(DATA868_SOCKET);
    unlink(CONF433_SOCKET); unlink(CONF868_SOCKET);

    return 0;
}


