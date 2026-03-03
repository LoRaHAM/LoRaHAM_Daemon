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

----
sudo apt update
sudo apt install g++ make libgpiod-dev

gcc -Wall -o loraham_igate loraham_iGate_105d.c

./loraham_igate

Als Daemon im Hintergrund:
./loraham_igate -d

*/



#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <sys/select.h>
#include <time.h>
#include <getopt.h>
#include <stdarg.h>

// --- FARBEN ---
#define CLR_BLUE  "\x1B[34m"
#define CLR_RESET "\x1B[0m"

// --- VARIABLE KONFIGURATION (mit Defaults) ---
char my_call[20]      = "DB0abc-10";
char freq_rx[20]      = "433.775";
char freq_tx[20]      = "433.900";
int interval_is       = 600;
int interval_rf       = 600;

char lat_str[20]      = "4827.70N";
char lon_str[20]      = "00957.60E";
char lat_filter[20]   = "48.40";
char lon_filter[20]   = "9.90";
char filter_radius[10]= "5";
char symbol[2]        = "&";

const char* beacon_texts[] = {
    "LoRaHAM_Pi PiGate - loraham.de | hier könnte Ihre Werbung stehen!",
    "LoRaHAM_Pi PiGate - loraham.de | LoRa-APRS Gateway from LoRaHAM",
    "LoRaHAM_Pi PiGate - loraham.de | Hardware: Raspberry Pi + LoRaHAM_PI HAT",
    "LoRaHAM_Pi PiGate - loraham.de | Monitoring: 433.775 MHz SF12 BW125 CR5 CRC1 LDRO1",
    "LoRaHAM_Pi PiGate - loraham.de | Status: Online & Digipeating"
};
#define NUM_BEACONS 5
int beacon_idx = 0;

#define APRS_SERVER    "euro.aprs2.net"
#define APRS_PORT      14580
#define DATA_SOCKET    "/tmp/lora433.sock"
#define CONF_SOCKET    "/tmp/loraconf433.sock"

const char* LORA_HEADER = "<\xff\x01";
#define HEADER_LEN 3

#define ENABLE_IS_BEACON   1
#define ENABLE_RF_BEACON   1

int is_transmitting = 0;

// --- HILFSFUNKTIONEN ---

void log_print(const char* format, ...) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[10];
    strftime(time_str, sizeof(time_str), "%H:%M", t);

    printf("%s%s%s ", CLR_BLUE, time_str, CLR_RESET);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}

const char* get_next_beacon() {
    const char* text = beacon_texts[beacon_idx];
    beacon_idx = (beacon_idx + 1) % NUM_BEACONS;
    return text;
}

void send_lora_conf(const char* cmd) {
    int s;
    struct sockaddr_un remote;
    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) return;
    remote.sun_family = AF_UNIX;
    strncpy(remote.sun_path, CONF_SOCKET, sizeof(remote.sun_path)-1);
    if (connect(s, (struct sockaddr *)&remote, strlen(remote.sun_path) + sizeof(remote.sun_family)) != -1) {
        send(s, cmd, strlen(cmd), 0);
        send(s, "\n", 1, 0);
    }
    close(s);
}

void safe_lora_send(int data_sock, const char* payload, int len) {
    if (is_transmitting) return;
    is_transmitting = 1;
    char cmd[64];
    char out_buf[2100];

    sprintf(cmd, "SET FREQ=%s", freq_tx);
    send_lora_conf(cmd);
    usleep(100000);

    memcpy(out_buf, LORA_HEADER, HEADER_LEN);
    memcpy(out_buf + HEADER_LEN, payload, len);
    send(data_sock, out_buf, len + HEADER_LEN, 0);

    sleep(3);

    sprintf(cmd, "SET FREQ=%s", freq_rx);
    send_lora_conf(cmd);
    is_transmitting = 0;
}

int generate_aprs_passcode(const char *callsign) {
    int hash = 0x73e2;
    char call_upper[12];
    int i = 0;
    for (i = 0; i < (int)strlen(callsign) && i < 11; i++) {
        if (callsign[i] == '-') break;
        call_upper[i] = toupper(callsign[i]);
        call_upper[i+1] = '\0';
    }
    i = 0;
    while (call_upper[i]) {
        hash ^= (int)(call_upper[i]) << 8;
        if (call_upper[i+1]) hash ^= (int)(call_upper[i+1]);
        i += 2;
    }
    return (hash & 0x7fff);
}

void send_is_beacon(int aprs_sock) {
    char packet[512];
    sprintf(packet, "%s>APLG01,TCPIP*:!%s/%s%s%s\r\n", my_call, lat_str, lon_str, symbol, get_next_beacon());
    send(aprs_sock, packet, strlen(packet), 0);
    log_print("[BEACON-IS]  %s", packet);
}

void send_rf_beacon(int data_sock) {
    char payload[400];
    sprintf(payload, "%s>APLG01,WIDE1-1:!%s/%s%s%s", my_call, lat_str, lon_str, symbol, get_next_beacon());
    safe_lora_send(data_sock, payload, strlen(payload));
    log_print("[BEACON-RF]  Gesendet auf %s\n", freq_tx);
}

void print_usage(char *name) {
    printf("Nutzung: %s [OPTIONEN]\n", name);
    printf("  -c CALL      Rufzeichen\n");
    printf("  -t TX_FREQ   TX Frequenz\n");
    printf("  -r RX_FREQ   RX Frequenz\n");
    printf("  -i SEK       Intervall IS\n");
    printf("  -f SEK       Intervall RF\n");
    printf("  -L LAT       Beacon Lat\n");
    printf("  -O LON       Beacon Lon\n");
    printf("  -d           Als Daemon im Hintergrund starten\n");
}

int main(int argc, char **argv) {
    log_print("[EWOCLEM]    LoRaHAM PiGate OVERWATCH V1.00 26.02.27\n");
    log_print("[EWOCLEM]    \n");
    log_print("[EWOCLEM]     ******************************************************************************\n");
    log_print("[EWOCLEM]     * Copyright (C) 2026  [LoRaHAM / Alexander Walter]\n");
    log_print("[EWOCLEM]     * * LICENSE: GNU General Public License v3 (GPLv3) with the following terms:\n");
    log_print("[EWOCLEM]     * 1. PRIVATE/HOBBY: Free use, modification, and redistribution for non-commercial\n");
    log_print("[EWOCLEM]     * purposes is permitted.\n");
    log_print("[EWOCLEM]     * 2. COMMERCIAL: Commercial or business use is STRICTLY PROHIBITED unless a\n");
    log_print("[EWOCLEM]     * written license is obtained from the author for a fee (Dual-Licensing).\n");
    log_print("[EWOCLEM]     * [CONTACT: loraham.de Email Contact]\n");
    log_print("[EWOCLEM]     * 3. CODE MAINTENANCE: Any modifications to this code must be reported to the\n");
    log_print("[EWOCLEM]     * author (preferably via Pull Request on GitHub).\n");
    log_print("[EWOCLEM]     * 4. REDISTRIBUTION: Binaries may only be distributed alongside the full\n");
    log_print("[EWOCLEM]     * source code (Copyleft).\n");
    log_print("[EWOCLEM]     * * --- DISCLAIMER OF WARRANTY & LIMITATION OF LIABILITY ---\n");
    log_print("[EWOCLEM]     * THIS SOFTWARE IS PROVIDED ""AS IS"", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n");
    log_print("[EWOCLEM]     * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n");
    log_print("[EWOCLEM]     * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n");
    log_print("[EWOCLEM]     * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n");
    log_print("[EWOCLEM]     * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n");
    log_print("[EWOCLEM]     * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN\n");
    log_print("[EWOCLEM]     * THE SOFTWARE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE\n");
    log_print("[EWOCLEM]     * PROGRAM IS WITH THE USER.\n");
    log_print("[EWOCLEM]     *****************************************************************************\n");
    log_print("[EWOCLEM]    \n");

    int opt;
    int run_as_daemon = 0;

    while ((opt = getopt(argc, argv, "c:t:r:i:f:L:O:x:y:R:S:hd")) != -1) {
        switch (opt) {
            case 'c': strncpy(my_call, optarg, sizeof(my_call)-1); break;
            case 't': strncpy(freq_tx, optarg, sizeof(freq_tx)-1); break;
            case 'r': strncpy(freq_rx, optarg, sizeof(freq_rx)-1); break;
            case 'i': interval_is = atoi(optarg); break;
            case 'f': interval_rf = atoi(optarg); break;
            case 'L': strncpy(lat_str, optarg, sizeof(lat_str)-1); break;
            case 'O': strncpy(lon_str, optarg, sizeof(lon_str)-1); break;
            case 'x': strncpy(lat_filter, optarg, sizeof(lat_filter)-1); break;
            case 'y': strncpy(lon_filter, optarg, sizeof(lon_filter)-1); break;
            case 'R': strncpy(filter_radius, optarg, sizeof(filter_radius)-1); break;
            case 'S': symbol[0] = optarg[0]; break;
            case 'd': run_as_daemon = 1; break;
            case 'h': print_usage(argv[0]); exit(0);
            default: print_usage(argv[0]); exit(1);
        }
    }

    if (run_as_daemon) {
        if (daemon(1, 1) < 0) {
            perror("Daemon Start fehlgeschlagen");
            exit(1);
        }
        freopen("/tmp/loraham_igate.log", "a", stdout);
        freopen("/tmp/loraham_igate.log", "a", stderr);
        freopen("/dev/null", "r", stdin);
        log_print("[SYSTEM]     Daemon-Modus aktiv. Logs unter /tmp/loraham_igate.log\n");
    }

    int aprs_sock = -1, data_s = -1;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[2048];
    time_t last_is = 0, last_rf = 0;

    // Initiale LoRa-Konfiguration
    char init_cmd[256];
    sprintf(init_cmd, "SET FREQ=%s SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=1 POWER=17", freq_rx);
    send_lora_conf(init_cmd);

    log_print("[SYSTEM]     %s gestartet. Betrete Hauptschleife.\n", my_call);

    while (1) { // Äußere Schleife für automatischen Reconnect

        // 1. APRS-IS Verbindung aufbauen
        server = gethostbyname(APRS_SERVER);
        if (server == NULL) {
            log_print("[SYSTEM]     DNS Fehler. Retry in 30s...\n");
            sleep(30);
            continue;
        }

        aprs_sock = socket(AF_INET, SOCK_STREAM, 0);
        serv_addr.sin_family = AF_INET;
        bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
        serv_addr.sin_port = htons(APRS_PORT);

        if (connect(aprs_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
            log_print("[SYSTEM]     APRS-Connect fehlgeschlagen. Retry in 30s...\n");
            close(aprs_sock);
            sleep(30);
            continue;
        }

        // Login senden
        sprintf(buffer, "user %s pass %d vers RPi-LoRa-Gate 1.16 filter m/%s/%s/%s\r\n",
                my_call, generate_aprs_passcode(my_call), filter_radius, lat_filter, lon_filter);
        send(aprs_sock, buffer, strlen(buffer), 0);
        log_print("[SYSTEM]     Verbunden mit APRS-IS %s\n", APRS_SERVER);

        // 2. Data-Socket zum LoRa-Daemon (falls noch nicht offen)
        if (data_s < 0) {
            struct sockaddr_un data_remote;
            data_s = socket(AF_UNIX, SOCK_STREAM, 0);
            data_remote.sun_family = AF_UNIX;
            strncpy(data_remote.sun_path, DATA_SOCKET, sizeof(data_remote.sun_path)-1);

            log_print("[SYSTEM]     Warte auf LoRa-Daemon Socket...\n");
            while (connect(data_s, (struct sockaddr *)&data_remote, strlen(data_remote.sun_path) + sizeof(data_remote.sun_family)) == -1) {
                sleep(5);
            }
            log_print("[SYSTEM]     LoRa-Daemon Socket verbunden.\n");
        }

        // --- INNERE ARBEITSSCHLEIFE ---
        while (1) {
            fd_set readfds;
            struct timeval tv = {1, 0};
            FD_ZERO(&readfds);
            FD_SET(aprs_sock, &readfds);
            FD_SET(data_s, &readfds);
            int max_fd = (aprs_sock > data_s) ? aprs_sock : data_s;

            int sel = select(max_fd + 1, &readfds, NULL, NULL, &tv);
            if (sel < 0) break; // Fehler bei select -> Reconnect

            time_t now = time(NULL);

            if (!is_transmitting) {
                if (ENABLE_IS_BEACON && (now - last_is >= interval_is)) {
                    send_is_beacon(aprs_sock);
                    last_is = now;
                }
                if (ENABLE_RF_BEACON && (now - last_rf >= interval_rf)) {
                    send_rf_beacon(data_s);
                    last_rf = now;
                }
            }

            // Daten vom LoRa-Daemon (RF -> IS)
            if (FD_ISSET(data_s, &readfds)) {
                int len = recv(data_s, buffer, sizeof(buffer)-1, 0);
                if (len <= 0) {
                    log_print("[SYSTEM]     LoRa-Daemon Socket verloren!\n");
                    close(data_s);
                    data_s = -1;
                    break; // Raus zur äußeren Schleife
                }
                if (len > HEADER_LEN) {
                    buffer[len] = '\0';
                    char *h_pos = (char*)memmem(buffer, len, LORA_HEADER, HEADER_LEN);
                    if (h_pos != NULL) {
                        char *aprs_data = h_pos + HEADER_LEN;
                        if (isalnum((unsigned char)aprs_data[0])) {
                            char out_is[2100];
                            snprintf(out_is, sizeof(out_is), "%s\r\n", aprs_data);
                            send(aprs_sock, out_is, strlen(out_is), 0);
                            log_print("[GATEWAY]    RF -> IS: %s", out_is);

                            char *wide_ptr = strstr(aprs_data, "WIDE");
                            if (wide_ptr != NULL && strstr(aprs_data, my_call) == NULL) {
                                int n = 0, h = 0;
                                if (sscanf(wide_ptr, "WIDE%d-%d", &n, &h) == 2) {
                                    if (h > 0) {
                                        char clean_rf[512];
                                        char new_path[128];
                                        int prefix_len = wide_ptr - aprs_data;
                                        if (h > 2) h = 2;
                                        if (n == 1 && h == 1) {
                                            snprintf(new_path, sizeof(new_path), "%s*", my_call);
                                        } else {
                                            snprintf(new_path, sizeof(new_path), "%s*,WIDE%d-%d", my_call, n, h - 1);
                                        }
                                        snprintf(clean_rf, sizeof(clean_rf), "%.*s%s%s",
                                                 prefix_len, aprs_data, new_path, wide_ptr + 7);
                                        log_print("[DIGIPEATER] Re-TX: %s\n", clean_rf);
                                        usleep(800000);
                                        safe_lora_send(data_s, clean_rf, strlen(clean_rf));
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Daten vom APRS-IS (IS -> RF)
            if (FD_ISSET(aprs_sock, &readfds)) {
                int len = recv(aprs_sock, buffer, sizeof(buffer)-1, 0);
                if (len <= 0) {
                    log_print("[SYSTEM]     APRS-IS Verbindung verloren!\n");
                    close(aprs_sock);
                    aprs_sock = -1;
                    break; // Raus zur äußeren Schleife für Reconnect
                }
                buffer[len] = '\0';
                char *saveptr;
                char *line = strtok_r(buffer, "\r\n", &saveptr);
                while (line != NULL) {
                    if (line[0] != '#' && strchr(line, '>') != NULL) {
                        int line_len = strlen(line);
                        if (line_len > 0 && line_len < 250) {
                            log_print("[REPEATER]   Single Packet -> HF (%d Bytes): %s\n", line_len, line);
                            safe_lora_send(data_s, line, line_len);
                            usleep(500000);
                        }
                    }
                    line = strtok_r(NULL, "\r\n", &saveptr);
                }
            }
        } // Ende innere Schleife

        sleep(5); // Kurze Pause vor Neustart der Verbindung
    }

    return 0;
}

