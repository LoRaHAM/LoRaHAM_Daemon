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
 * loraham_iGate_106.c  –  LoRaHAM PiGate
 *
 * Änderungen gegenüber v105d:
 *   Fix 1: Korrekter IS-Login-Handshake  (verified / unverified)
 *   Fix 2: Korrektes RF->IS Upload-Format (qAR / qAO + RFONLY/NOGATE/TCPIP-Checks)
 *   Fix 3: Korrekte Digipeater-Pfadlogik (WIDE1-1, WIDE2-n, cleanPathAsterisks)
 *   Fix 4: 25s-Duplikat-Erkennung        (DJB2-Hash-Buffer, 64 Einträge)
 *   Fix 5: IS->RF nur für gehörte Stationen (wasHeard, 30min-Timeout)
 *   Neu:   Standalone Repeater-Modus     (-p Flag, kein Internet nötig)
 *   Neu:   Automatischer Reconnect       (IS und LoRa-Socket, DLS-fest)
 *   Beibehalten: Passcode-Autogenerierung aus Rufzeichen
 *
 * Kompilieren:
 *   gcc -Wall -o loraham_igate loraham_iGate_106.c
 *
 * Starten:
 *   ./loraham_igate -c DB0XXX-10 -L 4827.72N -O 00957.94E -m
 *   ./loraham_igate -p             (Reiner Repeater ohne Internet)
 *   ./loraham_igate -d             (Daemon im Hintergrund)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
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
#include <stdint.h>

/* ============================================================
 * KONSTANTEN
 * ============================================================ */

#define VERSION                 "1.06"

#define APRS_SERVER             "euro.aprs2.net"
#define APRS_PORT               14580

//#define APRS_SERVER             "aprs1.hc.r1.ampr.org"
//#define APRS_PORT               14501
#define DATA_SOCKET             "/tmp/lora433.sock"
#define CONF_SOCKET             "/tmp/loraconf433.sock"

/* 3-Byte LoRa-APRS Header: '<' 0xFF 0x01  (= 0x3C 0xFF 0x01) */
#define LORA_HEADER             "\x3c\xff\x01"
#define HEADER_LEN              3

#define IS_CONNECT_TIMEOUT_S    10      /* TCP-Connect-Timeout in Sekunden    */
#define IS_RECONNECT_INTERVAL_S 30      /* Sekunden zwischen Reconnect-Versuchen */
#define LORA_RECONNECT_INTERVAL_S 2     /* Sekunden zwischen LoRa-Socket-Versuchen */

#define DEDUP_BUFFER_SIZE       64      /* Max. gecachte Paket-Hashes         */
#define DEDUP_WINDOW_S          25      /* Duplikat-Fenster in Sekunden       */

#define MAX_HEARD_STATIONS      64      /* Max. verfolgte Stationen           */
#define HEARD_TIMEOUT_S         1800    /* 30 Minuten                         */

/* IS-Verbindungszustand */
#define IS_DISCONNECTED         0
#define IS_WAIT_VERIFY          1
#define IS_VERIFIED             2
#define IS_UNVERIFIED           3       /* Passcode abgelehnt – sollte nicht vorkommen */

/* Digipeater-Modi */
#define DIGI_OFF                0       /* Kein Digipeater                    */
#define DIGI_WIDE1              1       /* Nur WIDE1-1  (Standard)            */
#define DIGI_FULL               2       /* WIDE1-1 + WIDE2-n                  */

/* Farben für Konsolausgabe */
#define CLR_RED     "\x1B[31m"
#define CLR_GREEN   "\x1B[32m"
#define CLR_YELLOW  "\x1B[33m"
#define CLR_BLUE    "\x1B[34m"
#define CLR_RESET   "\x1B[0m"

/* ============================================================
 * KONFIGURATIONSVARIABLEN (mit Defaults)
 * ============================================================ */

static char my_call[20]        = "DB0xyz-10";
static char freq_rx[20]        = "433.775";
static char freq_tx[20]        = "433.900";
static int  interval_is        = 600;
static int  interval_rf        = 600;
static char lat_str[20]        = "4827.70N";
static char lon_str[20]        = "00957.60E";
static char lat_filter[20]     = "48.40";
static char lon_filter[20]     = "9.90";
static char filter_radius[10]  = "5";
static char symbol[2]          = "&";
static int  digi_mode          = DIGI_WIDE1;
static int  repeater_only      = 0;     /* 0=iGate+Digi, 1=Nur Repeater      */
static int  enable_is_to_rf    = 0;     /* IS->RF Nachrichten-Relay (opt-in)   */

/* ============================================================
 * LAUFZEIT-ZUSTAND
 * ============================================================ */

static int    aprs_sock         = -1;
static int    data_s            = -1;
static int    is_state          = IS_DISCONNECTED;
static int    is_transmitting   = 0;
static time_t last_is_reconnect = 0;
static time_t last_lora_reconnect = 0;
static time_t last_is_rx        = 0;    /* Letzter Empfang vom IS-Server      */

/* ============================================================
 * BEACON-TEXTE (rotierend)
 * ============================================================ */

static const char *beacon_texts[] = {
    "LoRaHAM_Pi PiGate - loraham.de | hier könnte Ihre Werbung stehen!",
    "LoRaHAM_Pi PiGate - loraham.de | Ulm/Dornstadt LoRa-APRS Gateway",
    "LoRaHAM_Pi PiGate - loraham.de | Hardware: Raspberry Pi + LoRaHAM_PI HAT",
    "LoRaHAM_Pi PiGate - loraham.de | Monitoring: 433.775 MHz SF12 BW125 CR5 CRC1 LDRO1",
    "LoRaHAM_Pi PiGate - loraham.de | Status: Online & Digipeating"
};
#define NUM_BEACONS 5
static int beacon_idx = 0;

/* ============================================================
 * DEDUP: 25-Sekunden-Hash-Buffer
 * ============================================================
 *
 * Verhindert, dass dasselbe Paket innerhalb von 25 Sekunden
 * mehrfach weitergeleitet wird. Verwendet DJB2-Hash über
 * Sender-Callsign + Paket-Payload.
 */

struct DedupEntry {
    uint32_t hash;
    time_t   ts;
};
static struct DedupEntry dedup_buf[DEDUP_BUFFER_SIZE];
static int dedup_count = 0;

static uint32_t djb2_hash(const char *sender, const char *payload) {
    uint32_t h = 5381;
    while (*sender)  h = ((h << 5) + h) + (unsigned char)*sender++;
    while (*payload) h = ((h << 5) + h) + (unsigned char)*payload++;
    return h;
}

static void dedup_clean(time_t now) {
    int i = 0;
    while (i < dedup_count) {
        if (now - dedup_buf[i].ts > DEDUP_WINDOW_S) {
            /* Entfernen durch Ersetzen mit letztem Element */
            dedup_buf[i] = dedup_buf[--dedup_count];
        } else {
            i++;
        }
    }
}

/* Gibt 1 zurück wenn Duplikat, 0 wenn neu (und fügt es ein) */
static int dedup_check_and_add(const char *sender, const char *payload) {
    time_t now = time(NULL);
    dedup_clean(now);
    uint32_t h = djb2_hash(sender, payload);

    for (int i = 0; i < dedup_count; i++) {
        if (dedup_buf[i].hash == h) return 1;
    }

    if (dedup_count < DEDUP_BUFFER_SIZE) {
        dedup_buf[dedup_count].hash = h;
        dedup_buf[dedup_count].ts   = now;
        dedup_count++;
    } else {
        /* Buffer voll: ältesten Eintrag überschreiben */
        int oldest = 0;
        for (int i = 1; i < dedup_count; i++) {
            if (dedup_buf[i].ts < dedup_buf[oldest].ts) oldest = i;
        }
        dedup_buf[oldest].hash = h;
        dedup_buf[oldest].ts   = now;
    }
    return 0;
}

/* ============================================================
 * HEARD-STATIONEN: Zuletzt auf RF gehörte Rufzeichen
 * ============================================================
 *
 * Für IS->RF-Relay: Nachrichten werden nur weitergegeben wenn
 * der Adressat innerhalb der letzten HEARD_TIMEOUT_S Sekunden
 * auf RF empfangen wurde (Referenz: CA2RXU wasHeard).
 */

struct HeardStation {
    char   callsign[16];
    time_t last_heard;
};
static struct HeardStation heard[MAX_HEARD_STATIONS];
static int heard_count = 0;

static void heard_clean(time_t now) {
    int i = 0;
    while (i < heard_count) {
        if (now - heard[i].last_heard > HEARD_TIMEOUT_S) {
            heard[i] = heard[--heard_count];
        } else {
            i++;
        }
    }
}

static void heard_update(const char *callsign) {
    time_t now = time(NULL);
    heard_clean(now);
    for (int i = 0; i < heard_count; i++) {
        if (strncmp(heard[i].callsign, callsign, sizeof(heard[i].callsign)) == 0) {
            heard[i].last_heard = now;
            return;
        }
    }
    if (heard_count < MAX_HEARD_STATIONS) {
        strncpy(heard[heard_count].callsign, callsign, sizeof(heard[0].callsign) - 1);
        heard[heard_count].callsign[sizeof(heard[0].callsign) - 1] = '\0';
        heard[heard_count].last_heard = now;
        heard_count++;
    }
}

static int was_heard(const char *callsign) {
    time_t now = time(NULL);
    heard_clean(now);
    for (int i = 0; i < heard_count; i++) {
        if (strncmp(heard[i].callsign, callsign, sizeof(heard[i].callsign)) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ============================================================
 * LOG-AUSGABE
 * ============================================================ */

static void log_print(const char *format, ...) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[10];
    strftime(ts, sizeof(ts), "%H:%M", t);
    printf("%s%s%s ", CLR_BLUE, ts, CLR_RESET);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}

/* ============================================================
 * PASSCODE-GENERIERUNG (aus Rufzeichen, kein manueller Eintrag nötig)
 * ============================================================ */

static int generate_aprs_passcode(const char *callsign) {
    int hash = 0x73e2;
    char call_upper[12];
    int i = 0;
    for (i = 0; i < (int)strlen(callsign) && i < 11; i++) {
        if (callsign[i] == '-') break;
        call_upper[i] = (char)toupper((unsigned char)callsign[i]);
    }
    call_upper[i] = '\0';
    i = 0;
    while (call_upper[i]) {
        hash ^= (unsigned char)call_upper[i] << 8;
        if (call_upper[i + 1]) hash ^= (unsigned char)call_upper[i + 1];
        i += 2;
    }
    return (hash & 0x7fff);
}

/* ============================================================
 * LoRa-SOCKET UND FREQUENZ
 * ============================================================ */

static void send_lora_conf(const char *cmd) {
    int s;
    struct sockaddr_un r;
    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) return;
    r.sun_family = AF_UNIX;
    strncpy(r.sun_path, CONF_SOCKET, sizeof(r.sun_path) - 1);
    if (connect(s, (struct sockaddr *)&r,
                strlen(r.sun_path) + sizeof(r.sun_family)) != -1) {
        send(s, cmd, strlen(cmd), 0);
        send(s, "\n", 1, 0);
    }
    close(s);
}

static void safe_lora_send(const char *payload, int len) {
    if (is_transmitting || data_s < 0 || len <= 0) return;
    if (len > 2000) len = 2000;
    is_transmitting = 1;
    char cmd[64];
    char out_buf[2100];

    /* Auf TX-Frequenz wechseln */
    snprintf(cmd, sizeof(cmd), "SET FREQ=%s", freq_tx);
    send_lora_conf(cmd);
    usleep(100000);  /* 100ms Settling-Zeit */

    /* 3-Byte LoRa-Header + Nutzlast senden */
    memcpy(out_buf, LORA_HEADER, HEADER_LEN);
    memcpy(out_buf + HEADER_LEN, payload, len);
    send(data_s, out_buf, len + HEADER_LEN, MSG_NOSIGNAL);

    /* SF12 / BW125 braucht ~3s Airtime */
    sleep(3);

    /* Zurück auf RX-Frequenz */
    snprintf(cmd, sizeof(cmd), "SET FREQ=%s", freq_rx);
    send_lora_conf(cmd);
    is_transmitting = 0;
}

/* Non-blocking Verbindungsaufbau zum LoRa-Datensocket */
static int connect_lora_socket(void) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return -1;

    /* Non-blocking setzen */
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un r;
    r.sun_family = AF_UNIX;
    strncpy(r.sun_path, DATA_SOCKET, sizeof(r.sun_path) - 1);

    int ret = connect(s, (struct sockaddr *)&r,
                      strlen(r.sun_path) + sizeof(r.sun_family));
    if (ret == 0) {
        fcntl(s, F_SETFL, flags);
        return s;   /* Sofort verbunden (Unix socket) */
    }
    if (ret < 0 && errno != EINPROGRESS) {
        close(s);
        return -1;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    struct timeval tv = { LORA_RECONNECT_INTERVAL_S, 0 };
    if (select(s + 1, NULL, &wfds, NULL, &tv) <= 0) {
        close(s);
        return -1;
    }
    int error = 0;
    socklen_t elen = sizeof(error);
    getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &elen);
    if (error != 0) { close(s); return -1; }

    fcntl(s, F_SETFL, flags);
    return s;
}

/* ============================================================
 * APRS-IS VERBINDUNG
 * ============================================================ */

/* Non-blocking TCP-Connect mit Timeout */
static int connect_to_is(void) {
    struct hostent *h = gethostbyname(APRS_SERVER);
    if (!h) {
        log_print("[IS]         DNS-Fehler: %s\n", APRS_SERVER);
        return -1;
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;

    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, h->h_addr, h->h_length);
    addr.sin_port = htons(APRS_PORT);

    int ret = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        close(s);
        return -1;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    struct timeval tv = { IS_CONNECT_TIMEOUT_S, 0 };
    if (select(s + 1, NULL, &wfds, NULL, &tv) <= 0) {
        log_print("[IS]         Connect-Timeout nach %ds\n", IS_CONNECT_TIMEOUT_S);
        close(s);
        return -1;
    }

    int error = 0;
    socklen_t elen = sizeof(error);
    getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &elen);
    if (error != 0) { close(s); return -1; }

    fcntl(s, F_SETFL, flags);   /* Blocking wiederherstellen */
    return s;
}

/* IS-Verbindung sauber schliessen und Zustand zurücksetzen */
static void is_disconnect(void) {
    if (aprs_sock >= 0) {
        close(aprs_sock);
        aprs_sock = -1;
    }
    is_state = IS_DISCONNECTED;
    log_print("[IS]         %sVerbindung getrennt%s – Reconnect in %ds\n",
              CLR_RED, CLR_RESET, IS_RECONNECT_INTERVAL_S);
}

/* Login-String senden und auf Verifizierung warten */
static void is_send_login(void) {
    char login[300];
    snprintf(login, sizeof(login),
             "user %s pass %d vers LoRaHAM-PiGate %s filter m/%s/%s/%s\r\n",
             my_call,
             generate_aprs_passcode(my_call),
             VERSION,
             filter_radius, lat_filter, lon_filter);

    if (send(aprs_sock, login, strlen(login), MSG_NOSIGNAL) < 0) {
        is_disconnect();
        return;
    }
    is_state = IS_WAIT_VERIFY;
    log_print("[IS]         Login gesendet (Passcode: %d) – warte auf Bestätigung...\n",
              generate_aprs_passcode(my_call));
}

/* ============================================================
 * PAKETVORBEREITUNG ALLGEMEIN
 * ============================================================ */

/* Extrahiert das Rufzeichen bis zum '>' aus einem APRS-Paket */
static void extract_callsign(const char *pkt, char *call, int call_size) {
    int i = 0;
    while (pkt[i] && pkt[i] != '>' && i < call_size - 1) {
        call[i] = pkt[i];
        i++;
    }
    call[i] = '\0';
}

/*
 * Entfernt eingebettete LoRa-Header-Bytes aus dem Payload.
 * (Referenz: checkForStartingBytes in CA2RXU aprs_is_utils.cpp)
 * Gibt die bereinigte Länge zurück.
 */
static int strip_embedded_header(const char *in, int in_len,
                                  char *out, int out_size) {
    const char *p = (const char *)memmem(in, in_len, LORA_HEADER, HEADER_LEN);
    int copy_len;
    if (p != NULL) {
        copy_len = (int)(p - in);
    } else {
        copy_len = in_len;
    }
    if (copy_len >= out_size) copy_len = out_size - 1;
    memcpy(out, in, copy_len);
    out[copy_len] = '\0';
    return copy_len;
}

/* ============================================================
 * RF -> IS: UPLOAD-PAKET BAUEN
 * ============================================================
 *
 * Referenz: buildPacketToUpload() in CA2RXU aprs_is_utils.cpp
 *
 * Eingabe (pkt): "SENDER>TOCALL,PATH:payload"   (OHNE 3-Byte-Header)
 * Ausgabe (out): "SENDER>TOCALL,PATH,qAR,MYCALL:payload\r\n"
 *                             ^^^^
 *                             qAO wenn nicht verifiziert
 */
static int build_upload_packet(const char *pkt, int pkt_len,
                                char *out, int out_size) {
    const char *colon = strchr(pkt, ':');
    if (!colon) return 0;

    int path_part_len = (int)(colon - pkt);  /* Länge von "SENDER>TOCALL,PATH" */

    /* Payload bereinigen (eingebetteten Header entfernen falls vorhanden) */
    char clean_payload[1024];
    int  clean_len = strip_embedded_header(colon,
                                           pkt_len - path_part_len,
                                           clean_payload,
                                           sizeof(clean_payload));
    if (clean_len <= 0) return 0;

    const char *via = (is_state == IS_VERIFIED) ? "qAR" : "qAO";

    int written = snprintf(out, out_size, "%.*s,%s,%s%s\r\n",
                           path_part_len, pkt, via, my_call, clean_payload);
    return (written > 0 && written < out_size) ? 1 : 0;
}

/* ============================================================
 * DIGIPEATER: PFAD-MANIPULATION
 * ============================================================
 *
 * Referenz: cleanPathAsterisks() + buildPacket() in CA2RXU digi_utils.cpp
 */

/*
 * Entfernt alle Asterisken aus einem Pfad-String (in-place).
 * Beispiel: "WIDE1-1*,WIDE2-2" -> "WIDE1-1,WIDE2-2"
 */
static void clean_path_asterisks(char *path) {
    char *p;
    /* Spezifische Kombinationen zuerst entfernen */
    while ((p = strstr(path, ",WIDE1*")) != NULL)
        memmove(p, p + 7, strlen(p + 7) + 1);
    while ((p = strstr(path, ",WIDE2*")) != NULL)
        memmove(p, p + 7, strlen(p + 7) + 1);
    /* Verbleibende Asterisken entfernen */
    while ((p = strchr(path, '*')) != NULL)
        memmove(p, p + 1, strlen(p + 1) + 1);
}

/*
 * Baut das digipeatete RF-Paket.
 *
 * Referenz: generateDigipeatedPacket() + buildPacket() in CA2RXU digi_utils.cpp
 *
 * Eingabe (pkt):  "SENDER>TOCALL,WIDE1-1:payload"
 * Ausgabe (out):  "SENDER>TOCALL,MYCALL*:payload"      (WIDE1-1)
 *            od.: "SENDER>TOCALL,MYCALL*,WIDE2-1:payload" (WIDE2-2)
 *
 * Gibt 1 zurück bei Erfolg, 0 wenn Paket nicht digipeated werden soll.
 */
static int build_digipeated_packet(const char *pkt, char *out, int out_size) {
    if (!pkt || !out) return 0;

    /* NOGATE / RFONLY: nicht weiterleiten */
    if (strstr(pkt, "NOGATE") != NULL) return 0;
    if (strstr(pkt, "RFONLY") != NULL) return 0;

    /* Eigenes Rufzeichen bereits im Paket? -> schon digipeated, ignorieren */
    if (strstr(pkt, my_call) != NULL) return 0;

    /* Via IS empfangen (TCPIP im Pfad)? -> nicht erneut auf RF */
    if (strstr(pkt, "TCPIP") != NULL) return 0;

    const char *gt_ptr    = strchr(pkt, '>');
    if (!gt_ptr) return 0;
    const char *colon_ptr = strchr(gt_ptr, ':');
    if (!colon_ptr) return 0;

    /* Prüfen ob ':' an sinnvoller Position steht (>5 Zeichen, nicht am Ende) */
    int colon_pos = (int)(colon_ptr - pkt);
    if (colon_pos < 5 || colon_pos >= (int)strlen(pkt) - 1) return 0;

    /* Third-Party-Paket (:}) nicht re-digipeaten */
    if (colon_ptr[1] == '}') return 0;

    const char *comma_ptr = strchr(gt_ptr, ',');
    if (!comma_ptr || comma_ptr >= colon_ptr) return 0;  /* kein Pfad vorhanden */

    /* Pfad extrahieren: von comma_ptr+1 bis colon_ptr */
    int path_len = (int)(colon_ptr - comma_ptr - 1);
    if (path_len <= 0 || path_len >= 128) return 0;

    char path[128];
    memcpy(path, comma_ptr + 1, path_len);
    path[path_len] = '\0';

    /* Pfad für Analyse bereinigen (Asterisken entfernen) */
    char clean[128];
    strncpy(clean, path, sizeof(clean) - 1);
    clean[sizeof(clean) - 1] = '\0';
    clean_path_asterisks(clean);

    /* WIDE1-1 und WIDE2-n Positionen */
    char *w1 = strstr(clean, "WIDE1-1");
    char *w2 = strstr(clean, "WIDE2-");

    /* Pfad prüfen ob WIDE1 vor WIDE2 steht (APRS-Spezifikation) */
    if (w1 && w2 && w2 < w1) return 0;

    char new_path[256] = {0};
    int  have_path = 0;

    if (w1 != NULL && (digi_mode == DIGI_WIDE1 || digi_mode == DIGI_FULL)) {
        /*
         * WIDE1-1 -> MYCALL*
         * Alles vor WIDE1-1 bleibt, WIDE1-1 wird ersetzt.
         */
        int before_len = (int)(w1 - clean);
        char after[128];
        /* strlen("WIDE1-1") == 7 */
        strncpy(after, w1 + 7, sizeof(after) - 1);
        after[sizeof(after) - 1] = '\0';
        snprintf(new_path, sizeof(new_path), "%.*s%s*%s",
                 before_len, clean, my_call, after);
        have_path = 1;

    } else if (w2 != NULL && digi_mode == DIGI_FULL) {
        /*
         * WIDE2-1 -> MYCALL*
         * WIDE2-2 -> MYCALL*,WIDE2-1
         */
        int hop = w2[6] - '0';
        if (hop < 1 || hop > 2) return 0;

        int before_len = (int)(w2 - clean);
        char after[128];
        /* strlen("WIDE2-x") == 7 */
        strncpy(after, w2 + 7, sizeof(after) - 1);
        after[sizeof(after) - 1] = '\0';

        if (hop == 1) {
            snprintf(new_path, sizeof(new_path), "%.*s%s*%s",
                     before_len, clean, my_call, after);
        } else {   /* hop == 2 */
            snprintf(new_path, sizeof(new_path), "%.*s%s*,WIDE2-1%s",
                     before_len, clean, my_call, after);
        }
        have_path = 1;
    }

    if (!have_path) return 0;

    /* Neues Paket: "SENDER>TOCALL," + new_path + ":payload" */
    int prefix_len = (int)(comma_ptr - pkt);   /* "SENDER>TOCALL" ohne Komma */
    snprintf(out, out_size, "%.*s,%s%s",
             prefix_len, pkt, new_path, colon_ptr);
    return 1;
}

/* ============================================================
 * RF-PAKET VERARBEITEN (empfangen vom LoRa-Socket)
 * ============================================================ */

static void process_rf_packet(const char *aprs_data, int len) {
    if (len <= 0) return;

    /* Erstes Zeichen muss alphanumerisch sein (Rufzeichen) */
    if (!isalnum((unsigned char)aprs_data[0])) return;

    /* Sender-Rufzeichen extrahieren */
    char sender[16] = {0};
    extract_callsign(aprs_data, sender, sizeof(sender));

    /* Eigene Pakete ignorieren */
    if (strncmp(sender, my_call, sizeof(sender)) == 0) return;

    /* Via IS empfangen (TCPIP im Pfad)? -> nicht re-gaten */
    if (strstr(aprs_data, "TCPIP") != NULL) return;

    /* NOGATE / RFONLY -> nicht ins IS weiterleiten */
    if (strstr(aprs_data, "NOGATE") != NULL ||
        strstr(aprs_data, "RFONLY") != NULL) {
        log_print("[RF RX]      %.*s (NOGATE/RFONLY – kein IS-Upload)\n", len, aprs_data);
        goto do_digi;   /* Digipeaten trotzdem erlaubt */
    }

    {
        /* Payload nach ':' für den Dedup-Hash verwenden */
        const char *colon = strchr(aprs_data, ':');
        const char *payload_hash = colon ? (colon + 1) : aprs_data;

        /* ':' Position prüfen (muss sinnvoll liegen) */
        if (!colon || (int)(colon - aprs_data) < 5) return;

        /* Duplikat-Check */
        if (dedup_check_and_add(sender, payload_hash)) {
            log_print("[DEDUP]      Duplikat verworfen: %.60s\n", aprs_data);
            return;
        }

        /* Station als gehört markieren (für IS->RF-Relay) */
        heard_update(sender);

        log_print("[RF RX]      %.*s\n", len, aprs_data);

        /* --- RF -> IS ---
         *
         * Voraussetzungen:
         *   - kein reiner Repeater-Modus
         *   - IS-Socket verbunden und verifiziert
         *   - ':' nicht direkt gefolgt von '}' (kein Third-Party-Paket)
         */
        if (!repeater_only &&
            aprs_sock >= 0 &&
            is_state >= IS_VERIFIED &&
            colon[1] != '}') {
            char upload_pkt[2100];
            if (build_upload_packet(aprs_data, len, upload_pkt, sizeof(upload_pkt))) {
                if (send(aprs_sock, upload_pkt, strlen(upload_pkt), MSG_NOSIGNAL) < 0) {
                    is_disconnect();
                } else {
                    log_print("[RF->IS]     %s", upload_pkt);
                }
            }
        }
    }

do_digi:
    /* --- Digipeater: RF -> RF ---
     *
     * Läuft unabhängig vom IS-Status, auch im reinen Repeater-Modus.
     */
    if (digi_mode != DIGI_OFF) {
        char digi_pkt[512];
        if (build_digipeated_packet(aprs_data, digi_pkt, sizeof(digi_pkt))) {
            log_print("[DIGI]       Re-TX: %s\n", digi_pkt);
            usleep(800000);     /* 800ms Verzögerung vor Retransmit */
            safe_lora_send(digi_pkt, (int)strlen(digi_pkt));
        }
    }
}

/* ============================================================
 * IS-PAKET VERARBEITEN (empfangen vom APRS-IS-Server)
 * ============================================================ */

static void process_is_line(const char *line) {
    if (!line || !*line) return;

    /* --- Login-Handshake ---
     *
     * Der Server antwortet mit:
     *   "# logresp MYCALL verified, server ..."
     *   "# logresp MYCALL unverified, ..."
     */
    if (is_state == IS_WAIT_VERIFY && strstr(line, "logresp") != NULL) {
        if (strstr(line, "unverified") != NULL) {
            is_state = IS_UNVERIFIED;
            log_print("[IS]         %sLogin NICHT verifiziert%s – Rufzeichen prüfen!\n",
                      CLR_YELLOW, CLR_RESET);
        } else if (strstr(line, "verified") != NULL) {
            is_state = IS_VERIFIED;
            log_print("[IS]         %sLogin verifiziert%s – bereit\n",
                      CLR_GREEN, CLR_RESET);
        }
        return;
    }

    /* Keep-Alive und Server-Kommentare */
    if (line[0] == '#') {
        last_is_rx = time(NULL);
        return;
    }

    last_is_rx = time(NULL);

    /* --- IS -> RF: Nachrichten-Relay ---
     *
     * Nur wenn explizit aktiviert (-m Flag).
     * Nur Nachrichten-Pakete (::) weiterleiten.
     * Nur wenn der Adressat kürzlich auf RF gehört wurde.
     *
     * Referenz: processAPRSISPacket() in CA2RXU aprs_is_utils.cpp
     */
    if (!enable_is_to_rf || data_s < 0) return;
    if (strstr(line, "NOGATE") != NULL) return;

    const char *dc = strstr(line, "::");
    if (!dc) return;

    /* Adressat extrahieren: hinter "::" bis zum nächsten ":" */
    const char *addr_start = dc + 2;
    const char *addr_end   = strchr(addr_start, ':');
    if (!addr_end) return;

    char addressee[16] = {0};
    int  addr_len = (int)(addr_end - addr_start);
    if (addr_len <= 0 || addr_len >= (int)sizeof(addressee)) return;
    memcpy(addressee, addr_start, addr_len);

    /* Führende/nachfolgende Leerzeichen trimmen */
    int end = addr_len - 1;
    while (end >= 0 && addressee[end] == ' ') { addressee[end] = '\0'; end--; }

    if (!was_heard(addressee)) {
        /* Station nicht kürzlich auf RF gehört – kein TX */
        return;
    }

    /*
     * Third-Party-Paket auf RF senden:
     * "MYCALL>APLG01,WIDE1-1:}ORIGCALL>TOCALL,TCPIP,MYCALL*::ADDRESSEE:message"
     *
     * Referenz: buildPacketToTx(packet, 1) in CA2RXU aprs_is_utils.cpp
     */
    log_print("[IS->RF]     Nachricht für %s: %.80s\n", addressee, line);

    char sender_tocall[64] = {0};
    const char *first_comma = strchr(line, ',');
    if (first_comma) {
        int st_len = (int)(first_comma - line);
        if (st_len > 0 && st_len < (int)sizeof(sender_tocall))
            memcpy(sender_tocall, line, st_len);
    }

    char rf_pkt[512];
    snprintf(rf_pkt, sizeof(rf_pkt),
             "%s>APLG01,WIDE1-1:}%s,TCPIP,%s*%s",
             my_call,
             sender_tocall[0] ? sender_tocall : "UNKNOWN>APRS",
             my_call,
             dc);   /* dc zeigt auf "::" inklusive */
    safe_lora_send(rf_pkt, (int)strlen(rf_pkt));
}

/* ============================================================
 * BEACON
 * ============================================================ */

static const char *get_next_beacon(void) {
    const char *text = beacon_texts[beacon_idx];
    beacon_idx = (beacon_idx + 1) % NUM_BEACONS;
    return text;
}

static void send_is_beacon(void) {
    if (aprs_sock < 0 || is_state < IS_VERIFIED) return;
    char pkt[512];
    snprintf(pkt, sizeof(pkt), "%s>APLG01,TCPIP*:!%s/%s%s%s\r\n",
             my_call, lat_str, lon_str, symbol, get_next_beacon());
    if (send(aprs_sock, pkt, strlen(pkt), MSG_NOSIGNAL) < 0) {
        is_disconnect();
    } else {
        log_print("[BEACON-IS]  %s", pkt);
    }
}

static void send_rf_beacon(void) {
    if (data_s < 0) return;
    char payload[400];
    snprintf(payload, sizeof(payload), "%s>APLG01,WIDE1-1:!%s/%s%s%s",
             my_call, lat_str, lon_str, symbol, get_next_beacon());
    safe_lora_send(payload, (int)strlen(payload));
    log_print("[BEACON-RF]  Gesendet auf %s MHz\n", freq_tx);
}

/* ============================================================
 * USAGE
 * ============================================================ */

static void print_usage(char *name) {
    printf("Nutzung: %s [OPTIONEN]\n\n", name);
    printf("  -c CALL       Rufzeichen (Standard: %s)\n",        my_call);
    printf("  -t TX_FREQ    TX-Frequenz MHz (Standard: %s)\n",   freq_tx);
    printf("  -r RX_FREQ    RX-Frequenz MHz (Standard: %s)\n",   freq_rx);
    printf("  -i SEK        IS-Beacon-Intervall s (Standard: %d)\n", interval_is);
    printf("  -f SEK        RF-Beacon-Intervall s (Standard: %d)\n", interval_rf);
    printf("  -L LAT        Beacon Breitengrad    (z.B. 4827.72N)\n");
    printf("  -O LON        Beacon Längengrad     (z.B. 00957.94E)\n");
    printf("  -x LAT_DEC    Filter Breitengrad (Dezimal, z.B. 48.46)\n");
    printf("  -y LON_DEC    Filter Längengrad  (Dezimal, z.B. 9.96)\n");
    printf("  -R KM         Filter-Radius km   (Standard: %s)\n", filter_radius);
    printf("  -S SYMBOL     APRS-Symbol        (Standard: &)\n");
    printf("  -D MODUS      Digipeater: 0=aus, 1=WIDE1 (Standard), 2=WIDE1+WIDE2\n");
    printf("  -m            IS->RF Nachrichten-Relay aktivieren\n");
    printf("  -p            Reiner Repeater-Modus (kein Internet / kein IS)\n");
    printf("  -d            Als Daemon im Hintergrund starten\n");
    printf("  -h            Diese Hilfe\n\n");
//    printf("Passcode wird automatisch aus dem Rufzeichen berechnet.\n");
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(int argc, char **argv) {

    /* --- Banner & Copyright --- */

    log_print("[EWOCLEM]    LoRaHAM PiGate OVERWATCH V%s\n", VERSION);
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


    /* --- Kommandozeile --- */
    int opt, run_as_daemon = 0;
    while ((opt = getopt(argc, argv, "c:t:r:i:f:L:O:x:y:R:S:D:mpdh")) != -1) {
        switch (opt) {
            case 'c': strncpy(my_call,       optarg, sizeof(my_call)-1);       break;
            case 't': strncpy(freq_tx,       optarg, sizeof(freq_tx)-1);       break;
            case 'r': strncpy(freq_rx,       optarg, sizeof(freq_rx)-1);       break;
            case 'i': interval_is  = atoi(optarg);                             break;
            case 'f': interval_rf  = atoi(optarg);                             break;
            case 'L': strncpy(lat_str,       optarg, sizeof(lat_str)-1);       break;
            case 'O': strncpy(lon_str,       optarg, sizeof(lon_str)-1);       break;
            case 'x': strncpy(lat_filter,    optarg, sizeof(lat_filter)-1);    break;
            case 'y': strncpy(lon_filter,    optarg, sizeof(lon_filter)-1);    break;
            case 'R': strncpy(filter_radius, optarg, sizeof(filter_radius)-1); break;
            case 'S': symbol[0] = optarg[0];                                   break;
            case 'D': digi_mode    = atoi(optarg);                             break;
            case 'm': enable_is_to_rf = 1;                                     break;
            case 'p': repeater_only   = 1;                                     break;
            case 'd': run_as_daemon   = 1;                                     break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    /* --- Daemon --- */
    if (run_as_daemon) {
        if (daemon(1, 0) < 0) {
            perror("Daemon-Start fehlgeschlagen");
            return 1;
        }
    }

    /* --- Startmeldung --- */
    log_print("[SYSTEM]     Rufzeichen : %s\n", my_call);
    log_print("[SYSTEM]     Passcode   : %d (automatisch)\n",
              generate_aprs_passcode(my_call));
    log_print("[SYSTEM]     Modus      : %s\n",
              repeater_only ? "Reiner Repeater (kein IS)" : "iGate + Digipeater");
    log_print("[SYSTEM]     Digi-Modus : %s\n",
              digi_mode == DIGI_OFF   ? "Deaktiviert" :
              digi_mode == DIGI_WIDE1 ? "WIDE1-1" : "WIDE1-1 + WIDE2-n");
    if (!repeater_only)
        log_print("[SYSTEM]     IS->RF-Relay: %s\n",
                  enable_is_to_rf ? "Aktiv" : "Inaktiv (kein -m)");
    log_print("[SYSTEM]     RX/TX Freq : %s / %s MHz\n", freq_rx, freq_tx);

    /* --- LoRa initialisieren --- */
    {
        char init_cmd[256];
        snprintf(init_cmd, sizeof(init_cmd),
                 "SET FREQ=%s SF=12 BW=125 CR=5 CRC=1 PREAMBLE=8 SYNC=0x12 LDRO=AUTO POWER=17",
                 freq_rx);
        send_lora_conf(init_cmd);
        log_print("[LORA]       Init: %s\n", init_cmd);
    }

    /* --- Initiale Verbindungen --- */
    time_t last_is_beacon  = 0;
    time_t last_rf_beacon  = 0;

    /* LoRa-Socket */
    data_s = connect_lora_socket();
    /* --- LoRa-Socket Reconnect (Daemon-Neustart während Betrieb) --- */
    if (data_s < 0 && (time(NULL) - last_lora_reconnect >= LORA_RECONNECT_INTERVAL_S))     {
        int attempts = 5;
        log_print("[LORA]       Warte auf Daemon (%s)...\n", DATA_SOCKET);
        for (int i = 1; i <= attempts && data_s < 0; i++) {
            data_s = connect_lora_socket();
            if (data_s < 0) {
                log_print("[LORA]       Versuch %d/%d fehlgeschlagen – warte 3s...\n",
                          i, attempts);
                sleep(3);
            }
        }
        if (data_s < 0) {
            log_print("[FEHLER]     %sLoRa-Daemon nicht erreichbar!%s\n"
            "[FEHLER]     Starte zuerst: loraham_daemon\n"
            "[FEHLER]     iGate wird beendet.\n",
            CLR_RED, CLR_RESET);
            return 1;
        }
        log_print("[LORA]       Socket verbunden: %s\n", DATA_SOCKET);
        last_lora_reconnect = time(NULL);
    }

    /* ============================================================
     * HAUPTSCHLEIFE
     * ============================================================ */
    char buffer[2048];

    while (1) {
        time_t now = time(NULL);

        /* --- LoRa-Socket Reconnect ---
         *
         * Wird der loraham_daemon neu gestartet oder ist er
         * noch nicht bereit, verbindet sich das iGate automatisch.
         */
        if (data_s < 0 && (now - last_lora_reconnect >= LORA_RECONNECT_INTERVAL_S)) {
            last_lora_reconnect = now;
            data_s = connect_lora_socket();
            if (data_s >= 0)
                log_print("[LORA]       Socket %swiederverbunden%s\n",
                          CLR_GREEN, CLR_RESET);
        }

        /* --- IS Reconnect ---
         *
         * Greift bei:
         *   - Erstverbindung fehlgeschlagen
         *   - DLS-Zwangstrennung (ISP trennt nach 24h)
         *   - Kurzer Internetausfall
         *   - Server-Neustart (recv() liefert 0)
         *
         * Im Intervall IS_RECONNECT_INTERVAL_S (30s) wird ein
         * neuer Verbindungsversuch unternommen.
         */
        if (!repeater_only &&
            aprs_sock < 0 &&
            (now - last_is_reconnect >= IS_RECONNECT_INTERVAL_S)) {
            last_is_reconnect = now;
            log_print("[IS]         Reconnect-Versuch (%s:%d)...\n",
                      APRS_SERVER, APRS_PORT);
            aprs_sock = connect_to_is();
            if (aprs_sock >= 0) {
                is_send_login();
            } else {
                log_print("[IS]         Fehlgeschlagen – nächster Versuch in %ds\n",
                          IS_RECONNECT_INTERVAL_S);
            }
        }

        /* --- select() --- */
        fd_set readfds;
        FD_ZERO(&readfds);
        struct timeval tv = { 1, 0 };
        int max_fd = 0;

        if (data_s >= 0) {
            FD_SET(data_s, &readfds);
            if (data_s > max_fd) max_fd = data_s;
        }
        if (aprs_sock >= 0) {
            FD_SET(aprs_sock, &readfds);
            if (aprs_sock > max_fd) max_fd = aprs_sock;
        }

        select(max_fd + 1, &readfds, NULL, NULL, &tv);
        now = time(NULL);

        /* --- Beacons --- */
        if (!is_transmitting) {
            /* IS-Beacon: nur wenn verbunden und verifiziert */
            if (!repeater_only &&
                aprs_sock >= 0 &&
                is_state >= IS_VERIFIED &&
                (now - last_is_beacon >= interval_is)) {
                send_is_beacon();
                last_is_beacon = now;
            }
            /* RF-Beacon: immer wenn LoRa-Socket offen */
            if (data_s >= 0 &&
                (now - last_rf_beacon >= interval_rf)) {
                send_rf_beacon();
                last_rf_beacon = now;
            }
        }

        /* --- LoRa-Daten lesen --- */
        if (data_s >= 0 && FD_ISSET(data_s, &readfds)) {
            int len = recv(data_s, buffer, sizeof(buffer) - 1, 0);
            if (len <= 0) {
                /* Socket geschlossen oder Fehler -> Reconnect einleiten */
                log_print("[LORA]       Socket %sgetrennt%s\n", CLR_RED, CLR_RESET);
                close(data_s);
                data_s = -1;
                last_lora_reconnect = 0;    /* sofort neu versuchen */
            } else if (len > HEADER_LEN) {
                buffer[len] = '\0';
                /* LoRa-Header im Datenstrom finden */
                const char *h_pos = (const char *)memmem(buffer, len,
                                                          LORA_HEADER, HEADER_LEN);
                if (h_pos != NULL) {
                    const char *aprs_data = h_pos + HEADER_LEN;
                    int data_len = len - (int)(aprs_data - buffer);
                    if (data_len > 0) {
                        /* In lokalen Puffer kopieren und null-terminieren */
                        char pkt[1024];
                        int  pkt_len = (data_len < (int)sizeof(pkt) - 1)
                                       ? data_len : (int)sizeof(pkt) - 1;
                        memcpy(pkt, aprs_data, pkt_len);
                        pkt[pkt_len] = '\0';
                        process_rf_packet(pkt, pkt_len);
                    }
                }
            }
        }

        /* --- IS-Daten lesen --- */
        if (aprs_sock >= 0 && FD_ISSET(aprs_sock, &readfds)) {
            int len = recv(aprs_sock, buffer, sizeof(buffer) - 1, 0);
            if (len <= 0) {
                /* Verbindung getrennt (DLS-Zwangstrennung, Server-Timeout etc.) */
                is_disconnect();
                /* last_is_reconnect bleibt auf altem Wert -> Reconnect nach Intervall */
            } else {
                buffer[len] = '\0';
                /* Zeilenweise verarbeiten (IS liefert \r\n getrennte Zeilen) */
                char *saveptr;
                char *line = strtok_r(buffer, "\r\n", &saveptr);
                while (line != NULL) {
                    process_is_line(line);
                    line = strtok_r(NULL, "\r\n", &saveptr);
                }
            }
        }

    } /* while(1) */

    return 0;
}


