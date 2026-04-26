// =====================================================
// MESHTASTIC DECRYPTOR MIT UNIX SOCKET (BINÄR)
// Dynamische, tabellarische Ausgabe je nach PortNum
// Farben und Tabellen bleiben erhalten
// =====================================================
// Kompiliere:
// gcc -Wall -O2 -o mt_decrypt_socket mt_decrypt_parser_socket_200.c -lcrypto
// =====================================================

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <inttypes.h>
#include <openssl/evp.h>
#include <ctype.h>

#define DATA433_SOCKET "/tmp/lora868.sock"
#define MAX_FRAME_LEN 512
#define LORA_MAX_PAYLOAD 237

// ANSI Escape Codes für die Farbe
#define COLOR_RESET             "\033[0m"
#define COLOR_YELLOW            "\033[33m"
#define COLOR_MAGENTA           "\033[35m"
#define COLOR_BRIGHT_PURPLE     "\033[1;35m"

#define COLOR_BLUE          "\033[34m"// Klassisches Blau (Dunkler)
#define COLOR_LIGHT_BLUE    "\033[36m"// Hellblau / Cyan (Sehr gut lesbar auf dunklem Grund)
#define COLOR_BRIGHT_CYAN   "\033[1;36m"// Extra leuchtendes Hellblau (Fett + Cyan)

size_t key_len = 0;

// ================= STRUCTS =================
typedef struct {
    uint32_t dest;
    uint32_t sender;
    uint32_t packet_id;
    uint8_t flags;
    uint8_t channel;
    uint8_t next_hop;
    uint8_t relay;
} meshtastic_header_t;

typedef struct __attribute__((packed)) {
    uint32_t dest;
    uint32_t sender;
    uint32_t packet_id;
    uint8_t flags;
    uint8_t channel_hash;
    uint8_t next_hop;
    uint8_t relay;
    uint8_t data[LORA_MAX_PAYLOAD];
} LoRaPacket;

typedef struct {
    int portnum;
    int length;
    double lat;
    double lon;
    int alt;
    int battery;
    int signal;
    char text[256];
    char name[128];
    char shortname[16];
    char beacon_rest[128];
    uint64_t deviceid;
    uint32_t flags;
    // Erweiterte Felder gemäß Doku
    float volt;
    float humidity;
    float pressure;
    float temp;
    int hw_model;
    uint32_t uptime;
    float voltage;
    uint32_t free_heap;


} decoded_values_t;

// ================= NONCE =================
static void build_v2_nonce(const meshtastic_header_t *hdr, uint8_t *nonce)
{
    nonce[0] = (hdr->packet_id >> 0) & 0xFF;
    nonce[1] = (hdr->packet_id >> 8) & 0xFF;
    nonce[2] = (hdr->packet_id >> 16) & 0xFF;
    nonce[3] = (hdr->packet_id >> 24) & 0xFF;
    memset(nonce + 4, 0, 4);
    nonce[8]  = (hdr->sender >> 0) & 0xFF;
    nonce[9]  = (hdr->sender >> 8) & 0xFF;
    nonce[10] = (hdr->sender >> 16) & 0xFF;
    nonce[11] = (hdr->sender >> 24) & 0xFF;
    memset(nonce + 12, 0, 4);
}

// ================= DEFAULT KEY =================
static void build_default_key(uint8_t *key, size_t *out_key_len)
{
    const uint8_t default_key[16] = {
        0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
        0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
    };
    memcpy(key, default_key, 16);
    *out_key_len = 16;
    //printf("USING DEFAULT KEY (16 bytes): ");
    //for (int i = 0; i < 16; i++) printf("%02X ", key[i]);
    //printf("\n");
}

// ================= DEFAULT KEY =================
static void build_hamradio_key(uint8_t *key, size_t *out_key_len)
{
    const uint8_t hamradio_key[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };

    memcpy(key, hamradio_key, 16);
    *out_key_len = 16;
    //printf("USING HAMRADIO KEY (16 bytes): ");
    //for (int i = 0; i < 16; i++) printf("%02X ", key[i]);
    //printf("\n");
}


// ================= ADMIN KEY (Zusätzlich) =================
static void build_admin_key(uint8_t *key, size_t *out_key_len) {
    const uint8_t admin_key[16] = {
        0x7a, 0x33, 0x21, 0x0f, 0x4f, 0x35, 0x1c, 0x22,
        0x55, 0x10, 0x33, 0x77, 0x44, 0x22, 0x11, 0x00
    };
    memcpy(key, admin_key, 16);
    *out_key_len = 16;
}


// ================= AES CTR DECRYPT =================
static int aes_ctr_decrypt(const uint8_t *ciphertext, int ciphertext_len,
                           const uint8_t *key, const uint8_t *iv,
                           uint8_t *plaintext)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len = 0, plaintext_len = 0;

    if (key_len == 16) EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), NULL, key, iv);
    else if (key_len == 32) EVP_DecryptInit_ex(ctx, EVP_aes_256_ctr(), NULL, key, iv);
    else {
        printf("Invalid key length: %zu\n", key_len);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len);
    plaintext_len += len;
    EVP_DecryptFinal_ex(ctx, plaintext + plaintext_len, &len);
    plaintext_len += len;
    EVP_CIPHER_CTX_free(ctx);

    return plaintext_len;
}

// ================= TABLE PRINT =================
void print_table_header(int portnum) {
    //static int printed[256] = {0};
    //if (!printed[portnum]) {

        switch(portnum) {
            case 1: // TEXT_MESSAGE_APP
                printf("\033[1;36mPortNum | LEN | TEXT-MESSAGE                                     | FLAGS\033[0m\n");
                printf("--------|-----|--------------------------------------------------|------\n");
                break;
            case 2: // REMOTE_STATS_APP
                printf("PortNum | LEN | UPTIME (s) | VOLTAGE | FREE HEAP | FLAGS\n");
                printf("--------|-----|------------|---------|-----------|------\n");
                break;
            case 3: // POSITION
                printf("\033[1;32mPortNum | LEN | LAT        | LON        | ALT | DEVICEID         | FLAGS\033[0m\n");
                printf("--------|-----|------------|------------|-----|-----------------|------\n");
                break;
            case 4: // NODEINFO/BEACON
                printf("\033[1;35mPortNum | LEN | ID         | NAME            | SHORT | BEACON    | FLAGS\033[0m\n");
                printf("--------|-----|------------|-----------------|-------|-----------|------\n");
                break;
            case 5: // ADMIN_APP
                printf("PortNum | LEN | ADMIN COMMAND: TYPE / DETAIL                | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 8: // WAYPOINT_APP
                printf("PortNum | LEN | WAYPOINT: NAME / POSITION / ICON            | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 10: // SERIAL_APP
                printf("PortNum | LEN | SERIAL DATA: RAW STRING / HEX               | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 11: // MODULE_CONFIG_APP
                printf("PortNum | LEN | MODULE CONFIG: TYPE / ACTION                | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 16: // EXPERIMENTAL / PRIVATE DATA
                printf("PortNum | LEN | PRIVATE DATA: RAW / HEX / UNKNOWN           | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 18: // SERIAL_APP
                printf("PortNum | LEN | SERIAL BRIDGE: DATA PAYLOAD                 | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 19: // STORE_AND_FORWARD_APP
                printf("PortNum | LEN | S&F: REQUEST / STATUS / HISTORY             | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 21: // IP_TUNNEL_APP
                printf("PortNum | LEN | IP TUNNEL: PACKET DATA                      | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 26: // IP_TUNNEL_APP
                printf("PortNum | LENGTH | IP TUNNELING DATA (EXPERIMENTAL)    | FLAGS\n");
                printf("--------|--------|------------------------------------|------\n");
                break;
            case 29: // TAK_APP
                printf("PortNum | LEN | TAK/CoT: CALLSIGN / TYPE / UID              | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 31: // REMOTEQ_APP
                printf("PortNum | LEN | REMOTE QUERY: REQUEST TYPE                  | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 37: // RANGE_TEST_APP
                printf("PortNum | LEN | RANGE TEST: SEQ / SENDER POS                | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 42: // LEGACY_WAYPOINT / BEACON
                printf("PortNum | LEN | BEACON / POI: NAME / POSITION               | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 45: // MAP_REPORT_APP
                printf("PortNum | LEN | MAP REPORT: REGION / VERSION / TILES        | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 49: // MAP_REPORT_APP
                printf("PortNum | LEN | MAP REPORT: POS & HARDWARE INFO             | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 51: // RANGE_TEST_APP
                printf("PortNum | LEN | RANGE TEST: SEQ & SENDER INFO               | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 67:
                printf("PortNum | LEN | BATTERIE | VOLT  | TEMP  | DRUCK     | CH-UTIL | FLAGS\n");
                printf("--------|-----|----------|-------|-------|-----------|---------|------\n");
                break;
            case 78: // PAXCOUNTER_APP
                printf("PortNum | LEN | PAXCOUNTER: WIFI / BLUETOOTH / TOTAL        | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 79: // STORE_FORWARD_APP
                printf("PortNum | LEN | STORE & FORWARD: STATUS / HISTORY DATA      | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 80:
                printf("PortNum | LENGTH | EXPERIMENTAL / WEB MODULE DATA     | FLAGS\n");
                printf("--------|--------|------------------------------------|------\n");
                break;
            case 86: // TRX_APP
                printf("PortNum | LEN | TRX / SERIAL DATA: RAW PAYLOAD              | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 87: // HEARTBEAT_APP
                printf("PortNum | LEN | HEARTBEAT: STATUS / INTERVAL                | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 88: // MAP_REPORT_APP
                printf("PortNum | LEN | HARDWARE TYPE          | VERSION | BATT %% | FLAGS\n");
                printf("--------|-----|------------------------|---------|--------|------\n");
                break;
            case 102: // DETECTION_SENSOR_APP
                printf("PortNum | LEN | DETECTION SENSOR: STATE / EVENT COUNT       | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 120: // STORM_REPORT_APP
                printf("PortNum | LEN | STORM REPORT: EVENT TYPE / INTENSITY        | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 124: // API_RESPONSE / ROUTING
                printf("PortNum | LEN | API RESPONSE: SERVICE DATA / NODE INFO      | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 126: // MAP_REPORT_APP
                printf("PortNum | LENGTH | MAP REPORTING DATA (NODE STATUS)    | FLAGS\n");
                printf("--------|--------|------------------------------------|------\n");
                break;
            case 140: // REMOTE_ADMIN_APP
                printf("PortNum | LEN | ADMIN COMMAND / CONFIG ACTION               | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 142: // PLAK_APP
                printf("PortNum | LEN | PLAK DATA: STATUS / PAYLOAD                 | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 146: // NEIGHBORINFO_APP
                printf("PortNum | LEN | NEIGHBOR LIST (NODES HEARD BY SENDER)       | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 148: // ATAK_FORWARDER_APP
                printf("PortNum | LEN | ID         | NAME            | SHORT | FLAGS\n");
                printf("--------|-----|------------|-----------------|-------|------\n");
                break;
            case 157: // DETECTION_SENSOR_APP
                printf("PortNum | LEN | SENSOR EVENT: TYPE / VALUE / COUNT          | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 168: // NODEDB_SYNC / ZPS
                printf("PortNum | LEN | NODE SYNC: USER & DATABASE INFO             | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 185: // REMOTE_HARDWARE_APP
                printf("PortNum | LEN | REMOTE HW: PIN / ACTION / VALUE             | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 199: // NEIGHBOR_INFO_APP
                printf("PortNum | LEN | NEIGHBOR INFO: NODE_ID / SNR / CONNECTIONS  | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 201: // TAK_TRACKER_APP
                printf("PortNum | LEN | TAK TRACKER: CALLSIGN / PLI DATA            | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 215: // POWERCYCLED_APP
                printf("PortNum | LEN | POWER EVENT: REBOOT REASON / COUNT          | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 238: // WAYPOINT_APP
                printf("PortNum | LEN | WAYPOINT: NAME & COORDINATES                | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 253: // FIRMWARE_UPDATE_APP
                printf("PortNum | LEN | FW-UPDATE: CHUNK / BINARY PAYLOAD           | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            case 255: // MAX / SENTINEL
                printf("PortNum | LEN | SPECIAL: SENTINEL / MAX_PORT_RESERVED       | FLAGS\n");
                printf("--------|-----|---------------------------------------------|------\n");
                break;
            default:
                printf("PortNum | LEN | RAW HEX / TEXT (FALLBACK)                        | FLAGS\n");
                printf("--------|-----|--------------------------------------------------|------\n");
                break;
        }
    //printed[portnum] = 1;
    //}
}

void print_table_row(decoded_values_t *dv) {


    switch(dv->portnum) {
        case 1:
            printf("%-7d | %-3d | %-48s | 0x%02X\n", dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 2: // Remote Stats
            printf("%-7d | %-3d | %-10u | %-6.2fV | %-9u | 0x%02X\n",
                   dv->portnum, dv->length, dv->uptime, dv->voltage, dv->free_heap, dv->flags);
            break;
        case 3:
            printf("%-7d | %-3d | %-10.7f | %-10.7f | %-3d | 0x%08llX      | 0x%02X\n",
                   dv->portnum, dv->length, dv->lat, dv->lon, dv->alt, (unsigned long long)dv->deviceid, dv->flags);
            break;
        case 4:
            printf("%-7d | %-3d | 0x%08llX | %-15s | %-5s | %-9s | 0x%02X\n",
                   dv->portnum, dv->length, (unsigned long long)dv->deviceid, dv->name, dv->shortname, dv->beacon_rest, dv->flags);
            break;
        case 5: // Admin Row
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 8: // Waypoint App
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 11: // Module Config Row
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 10: // Serial Bridge
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 16: // Private Data Row
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 18: // Serial App
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 19: // Store & Forward
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 21: // IP Tunnel
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 26: // IP_TUNNEL_APP
            printf("%-7d | %-6d | %-34s | 0x%08X\n",
                   dv->portnum, dv->length, "IP TUNNEL DATA", dv->flags);
            break;
        case 29: // TAK Row
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 31: // Remote Query Row
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 37: // Range Test
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 42: // Legacy Beacon
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 45:
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 49: // Map Report
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 51: // Range Test
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;

        case 67:
            printf("%-7d | %-3d | %-5d | %-5.2f | %-5.1f | %-9.1f | %-6d | 0x%02X\n",
                   dv->portnum, dv->length, dv->battery, dv->volt, dv->temp, dv->pressure, dv->signal, dv->flags);
            break;
        case 78: // Paxcounter
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 79: // Store & Forward
            // Wir prüfen, ob wir Text-Inhalt haben (z.B. eine gepufferte Nachricht)
            if (strlen(dv->text) > 0) {
                printf("%-7d | %-3d | S&F MSG: %-34.34s | 0x%02X\n",
                       dv->portnum, dv->length, dv->text, dv->flags);
            } else {
                printf("%-7d | %-3d | %-44s | 0x%02X\n",
                       dv->portnum, dv->length, "S&F HEARTBEAT / SYNC", dv->flags);
            }
            break;
        case 80:
            printf("%-7d | %-6d | %-34s | 0x%08X\n",
                   dv->portnum, dv->length, "WEB/EXP DATA", dv->flags);
            break;
        case 86: // Transceiver App
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 87: // Heartbeat Row
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 88: // Map Report Row
        {
            char *hw = strtok(dv->text, "|");
            char *fw = strtok(NULL, "|");
            printf("%-7d | %-3d | %-22.22s | %-7.7s | %-6d | 0x%02X\n",
                   dv->portnum, dv->length, hw ? hw : "?", fw ? fw : "?", dv->battery, dv->flags);
        }
        break;
        case 102: // Detection Sensor
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 104:
            printf("%-7d | %-3d | !%08X | %-15s | %-5s | 0x%02X\n",
                   dv->portnum, dv->length, (uint32_t)dv->deviceid, dv->name, dv->shortname, dv->flags);
            break;
        case 120: // Storm Report
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 124: // API Response
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 126: // Map Report
            printf("%-7d | %-6d | %-34s | 0x%02X\n",
                   dv->portnum, dv->length, "MAP REPORT / MESH MAP INFO", dv->flags);
            break;
        case 140: // Remote Admin
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 142: // PLAK App
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 146: // Neighbor Info
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 148: // ATAK Forwarder (Nutzt das Namens-Layout)
            printf("%-7d | %-3d | !%08X | %-15s | %-5s | 0x%02X\n",
                   dv->portnum, dv->length, (uint32_t)dv->deviceid, dv->name, dv->shortname, dv->flags);
            break;

        case 157: // Detection Sensor
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 168: // Node Sync
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 185: // Remote Hardware
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 199: // Neighbor Info
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 201: // TAK Tracker
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 215: // Power Cycle
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 238: // Waypoint
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 253: // Firmware Update Row
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        case 255:
            printf("%-7d | %-3d | %-44.44s | 0x%02X\n",
                   dv->portnum, dv->length, dv->text, dv->flags);
            break;
        default:
            printf("%-7d | %-3d | ", dv->portnum, dv->length);
            for(int i=0; i<15 && i<dv->length; i++) printf("%02X", (uint8_t)dv->text[i]);
            printf("... | 0x%02X\n", dv->flags);
        break;
    }

}

// ================= PROTOBUF PARSE =================
uint64_t decode_varint(const uint8_t **ptr, const uint8_t *end) {
    uint64_t res = 0; int shift = 0;
    while (*ptr < end) {
        uint8_t b = **ptr; (*ptr)++;
        res |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return res;
}

// Die fehlende Funktion skip_field (Behebt deinen Linker-Fehler)
void skip_field(int wire_type, const uint8_t **p, const uint8_t *end) {
    switch (wire_type) {
        case 0: // Varint
            decode_varint(p, end);
            break;
        case 1: // 64-bit
            *p += 8;
            break;
        case 2: // Length-delimited
        {
            uint64_t len = decode_varint(p, end);
            *p += len;
        }
        break;
        case 5: // 32-bit
            *p += 4;
            break;
        default:
            // Unbekannter Wire-Type, wir brechen sicherheitshalber ab
            *p = end;
            break;
    }
}



// Protobuf Analyzer

int Chat_MSG(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 3) return 0;

    const uint8_t *p = data;
    const uint8_t *end = data + len;

    // Wir durchlaufen das Paket feldweise
    while (p < end) {
        // Dekodiere Key (Tag und Wire-Type)
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        // Feld 2 (Payload) ist der Container für den Text
        if (fnum == 2 && wire == 2) {
            uint64_t payload_len = decode_varint(&p, end);

            // Plausibilitäts-Check der Länge
            if (payload_len == 0 || p + payload_len > end) return 0;

            int valid_text_count = 0;
            int invalid_binary_count = 0;

            // Systematische Inhaltsprüfung Byte für Byte
            for (size_t i = 0; i < payload_len; i++) {
                uint8_t c = p[i];

                // A: Standard ASCII (Druckbar + gängige Whitespaces)
                if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t') {
                    valid_text_count++;
                }
                // B: UTF-8 Multi-Byte (Umlaute ÄÖÜ, ß sowie Emojis)
                // In UTF-8 haben alle Nicht-ASCII-Zeichen das MSB gesetzt (>= 128)
                else if (c >= 0x80) {
                    valid_text_count++;
                }
                // C: Binärer Müll (Steuerzeichen < 32, die keine Whitespaces sind)
                else if (c < 32 && c != 0) {
                    invalid_binary_count++;
                }
            }

            // Entscheidung: Es muss Text vorhanden sein und darf kein Binär-Müll enthalten sein
            if (valid_text_count > 0 && invalid_binary_count == 0) {
                // Text sicher in die dv-Struktur kopieren
                size_t copy_limit = sizeof(dv->text) - 1;
                size_t actual_copy = (payload_len < copy_limit) ? (size_t)payload_len : copy_limit;

                memcpy(dv->text, p, actual_copy);
                dv->text[actual_copy] = '\0'; // Null-Terminierung garantieren

                return 1; // Erfolgreich als Chat identifiziert
            }

            // Wenn es kein Chat war, springen wir hinter den Payload und suchen weiter
            p += payload_len;
        } else {
            // Alle anderen Felder (Port-Header etc.) überspringen
            skip_field(wire, &p, end);
        }
    }

    return 0; // Kein Chat-Muster gefunden
}
int Position_MSG(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 5 || data[1] != 0x03) return 0; // Nur Port 3

    const uint8_t *p = data;
    const uint8_t *end = data + len;

    while (p < end) {
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        if (fnum == 2 && wire == 2) { // Payload Container
            uint64_t payload_len = decode_varint(&p, end);
            const uint8_t *u_end = p + payload_len;

            while (p < u_end) {
                uint64_t uk = decode_varint(&p, u_end);
                int ufn = (int)(uk >> 3);
                int uw = (int)(uk & 7);

                if (ufn == 1 && uw == 5) { // Latitude (fixed32)
                    int32_t lat_raw = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    dv->lat = lat_raw * 1e-7;
                    p += 4;
                } else if (ufn == 2 && uw == 5) { // Longitude (fixed32)
                    int32_t lon_raw = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    dv->lon = lon_raw * 1e-7;
                    p += 4;
                } else if (ufn == 3 && uw == 0) { // Altitude (Varint)
                    dv->alt = (int)decode_varint(&p, u_end);
                } else {
                    skip_field(uw, &p, u_end);
                }
            }
        } else {
            skip_field(wire, &p, end);
        }
    }
    return (dv->lat != 0) ? 1 : 0;
}

/*
int Position_MSG(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 5 || data[1] != 0x03) return 0; // Nur Port 3

    const uint8_t *p = data;
    const uint8_t *end = data + len;

    while (p < end) {
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        if (fnum == 2 && wire == 2) { // Payload Container
            uint64_t payload_len = decode_varint(&p, end);
            const uint8_t *u_end = p + payload_len;

            while (p < u_end) {
                uint64_t uk = decode_varint(&p, u_end);
                int ufn = (int)(uk >> 3);
                int uw = (int)(uk & 7);

                if (ufn == 1 && uw == 5) { // Latitude (fixed32)
                    int32_t lat_raw = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    dv->lat = lat_raw * 1e-7;
                    p += 4;
                } else if (ufn == 2 && uw == 5) { // Longitude (fixed32)
                    int32_t lon_raw = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    dv->lon = lon_raw * 1e-7;
                    p += 4;
                } else if (ufn == 3 && uw == 0) { // Altitude (Varint)
                    dv->alt = (int)decode_varint(&p, u_end);
                } else {
                    skip_field(uw, &p, u_end);
                }
            }
        } else {
            skip_field(wire, &p, end);
        }
    }
    return (dv->lat != 0) ? 1 : 0;
}*/
int Telemetry_MSG(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 2 || data[1] != 0x43) return 0;

    const uint8_t *p = data + 2;
    const uint8_t *end = data + len;

    printf("TELEMETRY  |");

    while (p < end) {
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        if (fnum == 2 && wire == 2) { // DeviceMetrics
            uint64_t sub_len = decode_varint(&p, end);
            const uint8_t *s_end = p + sub_len;
            while (p < s_end) {
                uint64_t skey = decode_varint(&p, s_end);
                int sfn = (int)(skey >> 3);
                int sw = (int)(skey & 7);

                if (sfn == 1 && sw == 0) {
                    int bat = (int)decode_varint(&p, s_end);
                    if (bat >= 0 && bat <= 127) printf(" [Bat: %d%%]", bat);
                }
                else if (sfn == 2 && sw == 5) {
                    if (p + 4 <= s_end) {
                        uint32_t v_raw = (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                        float volt; memcpy(&volt, &v_raw, 4);
                        printf(" [Volt: %.2fV]", volt);
                        p += 4;
                    }
                }
                else if (sfn == 4 && sw == 5) {
                    if (p + 4 <= s_end) {
                        uint32_t c_raw = (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                        float util; memcpy(&util, &c_raw, 4);
                        printf(" [ChUtil: %.1f%%]", util);
                        p += 4;
                    }
                }
                else if (sw == 2) { // Falls ein Sub-Container auftaucht (12 16)
                    uint64_t l = decode_varint(&p, s_end);
                    (void)l;
                }
                else { skip_field(sw, &p, s_end); }
            }
        }
        else if (fnum == 3 && wire == 2) { // EnvironmentMetrics
            uint64_t env_len = decode_varint(&p, end);
            const uint8_t *e_end = p + env_len;
            while (p < e_end) {
                uint64_t ek = decode_varint(&p, e_end);
                int efn = (int)(ek >> 3);
                int ew = (int)(ek & 7);

                if (efn == 1 && ew == 5) { // Temperatur
                    uint32_t t_raw = (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    float temp; memcpy(&temp, &t_raw, 4);
                    printf(" [Temp: %.2f°C]", temp);
                    p += 4;
                }
                else if ((efn == 2 || efn == 9) && ew == 5) { // Luftfeuchtigkeit (Feld 2 ODER 9)
                    uint32_t h_raw = (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    float hum; memcpy(&hum, &h_raw, 4);
                    printf(" [Hum: %.1f%%]", hum);
                    p += 4;
                }
                else if (efn == 3 && ew == 5) { // Luftdruck
                    uint32_t p_raw = (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    float press; memcpy(&press, &p_raw, 4);
                    printf(" [Press: %.1f hPa]", press);
                    p += 4;
                }
                else { skip_field(ew, &p, e_end); }
            }
        }
        else { skip_field(wire, &p, end); }
    }
    printf("\n");
    return 1;
}

/*
int Telemetry_MSG(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 2 || data[1] != 0x43) return 0; // Port 67

    const uint8_t *p = data;
    const uint8_t *end = data + len;

    while (p < end) {
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        if (fnum == 2 && wire == 2) { // DeviceMetrics Container
            uint64_t sub_len = decode_varint(&p, end);
            const uint8_t *s_end = p + sub_len;
            while (p < s_end) {
                uint64_t skey = decode_varint(&p, s_end);
                int sfn = (int)(skey >> 3);
                if (sfn == 1) { // Battery Level
                    int bat = (int)decode_varint(&p, s_end);
                    printf(" [Battery: %d%%] ", bat);
                } else if (sfn == 2) { // Voltage
                    if (p + 4 <= s_end) {
                    uint32_t v_raw = (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    float volt;
                    // Sicherer Weg ohne Aliasing-Probleme:
                    memcpy(&volt, &v_raw, sizeof(volt));

                    printf(" [Voltage: %.2fV] ", volt);
                    p += 4;
                }
                } else { skip_field((int)(skey & 7), &p, s_end); }
            }
        } else { skip_field(wire, &p, end); }
    }
    return 1;
}


int Telemetry_MSG(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 4) return 0;

    const uint8_t *p = data;
    const uint8_t *end = data + len;
    int found_metrics = 0;

    while (p < end) {
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        if (fnum == 2 && wire == 2) {
            uint64_t m_len = decode_varint(&p, end);
            const uint8_t *m_end = p + m_len;
            while (p < m_end) {
                uint64_t mk = decode_varint(&p, m_end);
                int m_fnum = (int)(mk >> 3);
                if (m_fnum == 1) {
                    dv->battery = (int)decode_varint(&p, m_end);
                    if (dv->battery >= 0 && dv->battery <= 100) found_metrics++;
                }
                else if (m_fnum == 2) {
                    dv->volt = (float)decode_varint(&p, m_end) / 1000.0f;
                    if (dv->volt > 0.0f && dv->volt < 6.0f) found_metrics++;
                }
                else {
                    skip_field((int)(mk & 7), &p, m_end);
                }
            }
        }
        else {
            skip_field(wire, &p, end);
        }
    }
    return (found_metrics > 0) ? 1 : 0;
}*/

const char* get_hw_name(int id) {
    switch(id) {
        case 1:  return "Heltec V2.1";
        case 4:  return "T-Beam V1.x";
        case 13: return "T-Echo";
        case 20: return "Station G1";
        case 43: return "Heltec V3";
        case 45: return "T-Beam S3 Core";
        case 0:  return "Unset";
        default: return "Unknown/Other";
    }
}


int UserInfo_Check(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 10) return 0;

    const uint8_t *p = data;
    const uint8_t *end = data + len;
    int found_fields = 0;

    while (p < end) {
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        // Meshtastic User-Objekte liegen fast immer in Feld 2 (Data Payload)
        if (fnum == 2 && wire == 2) {
            uint64_t payload_len = decode_varint(&p, end);
            const uint8_t *u_end = p + payload_len;
            if (u_end > end) return 0;

            while (p < u_end) {
                uint64_t uk = decode_varint(&p, u_end);
                int ufn = (int)(uk >> 3);
                int uw = (int)(uk & 7);

                if (ufn == 1 && uw == 2) { // Feld 1: ID (z.B. !75ce5ad8)
                    uint64_t id_l = decode_varint(&p, u_end);
                    if (id_l > 0 && id_l < sizeof(dv->text)) {
                        memcpy(dv->text, p, id_l);
                        dv->text[id_l] = '\0';
                        if (dv->text[0] == '!') found_fields += 2;
                    }
                    p += id_l;
                }
                else if (ufn == 2 && uw == 2) { // Feld 2: LongName
                    uint64_t l = decode_varint(&p, u_end);
                    if (l > 0 && l < sizeof(dv->name)) {
                        snprintf(dv->name, sizeof(dv->name), "%.*s", (int)l, p);
                        found_fields++;
                    }
                    p += l;
                }
                else if (ufn == 3 && uw == 2) { // Feld 3: ShortName
                    uint64_t l = decode_varint(&p, u_end);
                    if (l > 0 && l < sizeof(dv->shortname)) {
                        snprintf(dv->shortname, sizeof(dv->shortname), "%.*s", (int)l, p);
                        found_fields++;
                    }
                    p += l;
                }
                else if (ufn == 5 && uw == 0) { // Feld 5: Hardware Modell (Varint)
                    uint64_t hw_id = decode_varint(&p, u_end);
                    dv->hw_model = (int)hw_id;
                    found_fields++;
                }
                else {
                    // Wichtig: skip_field muss vorhanden sein
                    skip_field(uw, &p, u_end);
                }
            } // Ende while (p < u_end)
        }
        else {
            // Felder außerhalb des User-Payloads überspringen
            skip_field(wire, &p, end);
        }
    } // Ende while (p < end)

    // Ein valider User hat meist ID + Name + Shortname (Score >= 3)
    return (found_fields >= 3) ? 1 : 0;
}

int Routing_MSG(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 2 || data[1] != 0x46) return 0;

    const uint8_t *p = data;
    const uint8_t *end = data + len;

    int hop_count = 0;
    int snr_count = 0;
    uint32_t req_id = 0;
    uint32_t hops[8] = {0};
    float snrs[8] = {0.0f};

    while (p < end) {
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        if (fnum == 2 && wire == 2) { // RouteDiscovery Container
            uint64_t sub_len = decode_varint(&p, end);
            const uint8_t *u_end = p + sub_len;

            while (p < u_end) {
                uint64_t uk = decode_varint(&p, u_end);
                int ufn = (int)(uk >> 3);
                int uw = (int)(uk & 7);

                // 1. Neuer Typ: Detaillierte Hop-Einträge (Feld 1)
                if (ufn == 1 && uw == 2) {
                    uint64_t hop_len = decode_varint(&p, u_end);
                    const uint8_t *h_end = p + hop_len;
                    while(p < h_end) {
                        uint64_t hk = decode_varint(&p, h_end);
                        int hfn = (int)(hk >> 3);
                        int hw = (int)(hk & 7);
                        if (hfn == 4 && hw == 0) { // Node ID
                            if (hop_count < 8) hops[hop_count++] = (uint32_t)decode_varint(&p, h_end);
                        } else { skip_field(hw, &p, h_end); }
                    }
                }
                // 2. Neuer Typ: SNR Liste (Feld 2)
                else if (ufn == 2 && uw == 2) {
                    uint64_t snr_l = decode_varint(&p, u_end);
                    const uint8_t *snr_end = p + snr_l;
                    while(p < snr_end && snr_count < 8) {
                        int32_t s = (int32_t)decode_varint(&p, snr_end);
                        snrs[snr_count++] = (float)s / 4.0f;
                    }
                }
                // 3. Klassischer Typ: Direkte Node IDs (Feld 1 oder 2 als Bytes)
                else if ((ufn == 1 || ufn == 2) && uw == 2) {
                    uint64_t id_l = decode_varint(&p, u_end);
                    if (id_l == 4 && hop_count < 8) {
                        hops[hop_count++] = (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    }
                    p += id_l;
                }
                // 4. Klassischer Typ: Einzelnes SNR Feld (Feld 3)
                else if (ufn == 3 && uw == 0) {
                    int32_t raw_snr = (int32_t)decode_varint(&p, u_end);
                    if (snr_count < 8) snrs[snr_count++] = (float)raw_snr / 4.0f;
                }
                else {
                    skip_field(uw, &p, u_end);
                }
            }
        }
        else if (fnum == 6 && wire == 5) { // Request ID (fixed32)
            if (p + 4 <= end) {
                req_id = (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                p += 4;
            }
        }
        else {
            skip_field(wire, &p, end);
        }
    }

    // --- AUSGABE ---
    if (hop_count > 0 || snr_count > 0) {
        printf("TRACEROUTE | "COLOR_BRIGHT_PURPLE"REPLY"COLOR_RESET " | ");
        for (int i = 0; i < hop_count; i++) {
            printf("-> 0x%08X ", hops[i]);
        }
        for (int j = 0; j < snr_count; j++) {
            printf(COLOR_MAGENTA"[%+.2f dB] "COLOR_RESET, snrs[j]);
        }
        if (req_id != 0) printf("(Ref ID: 0x%08X)", req_id);
        printf("\n");
    } else {
        if (req_id != 0) {
            printf("TRACEROUTE | ACK/INFO for ID: 0x%08X (No route data yet)\n", req_id);
        } else {
            printf("TRACEROUTE | REQUEST (Ping)\n");
        }
    }
    return 1;
}

/*
int Routing_MSG(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 2 || data[1] != 0x46) return 0;

    const uint8_t *p = data;
    const uint8_t *end = data + len;
    int hop_count = 0;
    int snr_count = 0;
    int route_type = 0;

    uint32_t hops[8];
    float snrs[8];

    while (p < end) {
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        if (fnum == 2 && wire == 2) {
            uint64_t payload_len = decode_varint(&p, end);
            const uint8_t *u_end = p + payload_len;

            while (p < u_end) {
                uint64_t uk = decode_varint(&p, u_end);
                int ufn = (int)(uk >> 3);
                int uw = (int)(uk & 7);

                if (ufn == 1 || ufn == 2) {
                    uint64_t id_l = decode_varint(&p, u_end);
                    if (id_l == 4 && hop_count < 8) {
                        hops[hop_count++] = (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                    }
                    p += id_l;
                }
                else if (ufn == 3 && uw == 0) {
                    int32_t raw_snr = (int32_t)decode_varint(&p, u_end);
                    if (snr_count < 8) {
                        snrs[snr_count++] = raw_snr / 4.0f;
                    }
                }
                else {
                    skip_field(uw, &p, u_end);
                }
            }
        }
        else if (fnum == 3 && wire == 0) {
            // Hier wird route_type gesetzt UND benutzt, das löscht die Warnung
            route_type = (int)decode_varint(&p, end);
        }
        else {
            skip_field(wire, &p, end);
        }
    }

    // Logik-Check: route_type wird jetzt aktiv abgefragt
    if (hop_count == 0) {
        if (route_type == 1) {
            printf("Traceroute [REQUEST] (Ping) sent...\n");
        } else {
            printf("Traceroute [CONTROL] Type: %d\n", route_type);
        }
    } else {
        printf("Traceroute [REPLY] Type %d | Route: ", route_type);
        for (int i = 0; i < hop_count; i++) {
            printf("-> 0x%08X ", hops[i]);
            if (i < snr_count) {
                printf("(SNR: %.2f dB) ", snrs[i]);
            }
        }
        printf("\n");
    }
    return 1;
}
*/

int Variant_Position_MSG(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 5 || data[1] != 0x03) return 0; // Port 3 (Position)

    const uint8_t *p = data;
    const uint8_t *end = data + len;
    int found_coords = 0;

    // Wir überspringen den Port-Header (08 03)
    p += 2;

    while (p < end) {
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        if (fnum == 1 && wire == 5) { // Latitude (fixed32)
            if (p + 4 <= end) {
                int32_t lat_raw = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                dv->lat = lat_raw * 1e-7;
                p += 4;
                found_coords++;
            } else break;
        }
        else if (fnum == 2 && wire == 5) { // Longitude (fixed32)
            if (p + 4 <= end) {
                int32_t lon_raw = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
                dv->lon = lon_raw * 1e-7;
                p += 4;
                found_coords++;
            } else break;
        }
        else if (fnum == 3 && wire == 0) { // Altitude (Varint)
            dv->alt = (int)decode_varint(&p, end);
        }
        else {
            // Hier war der Fehler: Nur aufrufen, nicht prüfen
            skip_field(wire, &p, end);
        }

        // Sicherheitscheck, damit wir nicht in einer Endlosschleife landen
        if (p >= end) break;
    }

    return (found_coords >= 2) ? 1 : 0;
}


int NodeInfo_MSG(const uint8_t *data, size_t len, decoded_values_t *dv) {
    if (len < 2 || data[1] != 0x05) return 0; // Port 5

    const uint8_t *p = data;
    const uint8_t *end = data + len;
    int is_response = 0;

    while (p < end) {
        uint64_t key = decode_varint(&p, end);
        int fnum = (int)(key >> 3);
        int wire = (int)(key & 7);

        if (fnum == 1 && wire == 2) { // Feld 1: Enthält meistens die User-Struktur (Response)
            is_response = 1;
            skip_field(wire, &p, end);
        } else {
            skip_field(wire, &p, end);
        }
    }

    if (is_response) {
        printf(" [NodeInfo RESPONSE] Sent User-Profile\n");
    } else {
        printf(" [NodeInfo REQUEST] Asking for Name/Hardware\n");
    }
    return 1;
}

void meshtastic_protobuf_decode_table_tabular(const uint8_t *data, size_t len, decoded_values_t *dv) {
    // Initialisierung des Structs (Wichtig, damit keine alten Daten drinstehen)
    memset(dv, 0, sizeof(decoded_values_t));
    dv->length = (int)len;


    // Aufruf der Erkennungs-Funktion mit allen Werten laut Vorschlag
    if (Chat_MSG(data, len, dv) == 1) {
        printf("Chat-Message found! Content: \"%s\"\n", dv->text);

        // Hier würde nun die eigentliche Extraktion des Textes folgen,
        // da wir wissen, dass es eine Chat-Nachricht ist.
        // z.B. Aufruf einer Extraktions-Funktion oder direkte Logik.
    }

    if (NodeInfo_MSG(data, len, dv)) { // <-- NEU
        dv->portnum = 5;
        // Die Print-Ausgabe erfolgt bereits im Parser, oder hier ergänzen:
        //printf("NODE-REQ   | Remote Query for Name/Hardware\n");
    }

    if (Position_MSG(data, len, dv)) {
        dv->portnum = 3;
        printf("Position found! "COLOR_LIGHT_BLUE"Lat: %.6f"COLOR_RESET" | "COLOR_LIGHT_BLUE"Lon: %.6f"COLOR_RESET" | Alt: %dm\n", dv->lat, dv->lon, dv->alt);
    }
    else if (Variant_Position_MSG(data, len, dv)) {
        dv->portnum = 3;
        printf("Variant-Position found! "COLOR_LIGHT_BLUE"Lat: %.6f"COLOR_RESET" | "COLOR_LIGHT_BLUE"Lon: %.6f"COLOR_RESET" | Alt: %dm\n", dv->lat, dv->lon, dv->alt);
    }

    if (Position_MSG(data, len, dv) == 1) {
        dv->portnum = 3;
    }

    if (Telemetry_MSG(data, len, dv) == 1) {
        dv->portnum = 67;
        snprintf(dv->text, sizeof(dv->text), "[\033[1;32mBat: %d%%\033[0m] %.2fV", dv->battery, dv->volt);
    }

    if (UserInfo_Check(data, len, dv)) {
        dv->portnum = 4;

        printf("ID: %s | Name: " COLOR_YELLOW "%-20s" COLOR_RESET " | Short: %-6s | HW: %s\n",
               dv->text,
               (dv->name[0] ? dv->name : "n/a"),
               (dv->shortname[0] ? dv->shortname : "n/a"),
               get_hw_name(dv->hw_model));
        return;
    }

    if (Routing_MSG(data, len, dv)) {
        dv->portnum = 70;
        // Die Ausgabe erfolgt bereits innerhalb der Routing_MSG Funktion
        return;
    }

    if (data[0] != 0x08 && data[1] != 0x43 && data[1] != 0x46 && data[1] != 0x05) {
//        printf(" [ENCRYPTED] Private Channel Data (0x%02X)\n", data[1]);
//        return;
    }
}

/**
 * Versucht ein Paket zu entschlüsseln, indem der Kanal-Hash in der Nonce
 * von 0-255 durchprobiert wird.
 * @return 1 bei Erfolg, 0 bei Misserfolg
 */
int try_nonce_manipulation(const uint8_t *payload, int payload_len,
                           const uint8_t *key, size_t k_len,
                           const uint8_t *original_nonce, uint8_t *out_plaintext)
{
    uint8_t test_nonce[16];
    memcpy(test_nonce, original_nonce, 16);

    for (int h = 0; h < 256; h++) {
        test_nonce[13] = (uint8_t)h;
        key_len = k_len;

        int p_len = aes_ctr_decrypt(payload, payload_len, key, test_nonce, out_plaintext);

        // --- DER LÜGENDETEKTOR (Neu & Sicher) ---
        if (p_len > 1) {
            uint8_t first = out_plaintext[0];
            uint8_t second = out_plaintext[1];

            // Ein echtes Paket beginnt mit 0x08 (Protobuf Tag für Port)
            // Danach kommt die PortNum (1=Chat, 3=Pos, 4=User, 0x45=Dein Privat-Port)
            if (first == 0x08 && (second == 0x01 || second == 0x03 || second == 0x04 || second == 0x45)) {
                printf("\n\033[5;97;101m !!! VERIFIZIERTER HIT: HASH 0x%02X !!! \033[0m\n", h);
                return 1; // Nur bei echtem Treffer stoppen
            }
        }
    }
    return 0; // Wenn 0x07 nur Müll war, läuft er hier einfach weiter
}
/*
int try_nonce_manipulation(const uint8_t *payload, int payload_len,
                           const uint8_t *key, size_t k_len,
                           const uint8_t *original_nonce, uint8_t *out_plaintext)
{
    uint8_t test_nonce[16];
    memcpy(test_nonce, original_nonce, 16);

    for (int h = 0; h < 256; h++) {
        // Manipuliere nur das Byte für den Channel-Hash in der Nonce
        test_nonce[13] = (uint8_t)h;

        // Temporäre globale Variable setzen, falls aes_ctr_decrypt darauf zugreift
        key_len = k_len;

        int p_len = aes_ctr_decrypt(payload, payload_len, key, test_nonce, out_plaintext);

        if (p_len > 0) {
            uint8_t p = out_plaintext[0];
            // Prüfung auf gültige Meshtastic PortNum
            if (p == 0x01 || p == 0x03 || p == 0x04 || p == 0x08 || p == 0x43) {
                // ANSI Erklärungen:
                // \033[5m  = Blinken (Blink)
                // \033[97m = Weißer Text (White Text)
                // \033[101m = Hellroter Hintergrund (Light Red BG)
                // \033[0m  = Reset
                printf("\n\033[5;97;101m !!! NONCE-MANIPULATOR HIT: HASH 0x%02X GEFUNDEN !!! \033[0m\n", h);
                return 1; // Erfolg!
            }
        }
    }
    return 0; // Kein Treffer
}
*/

// ================= MESHTASTIC DECRYPT (SUPER SCANNER) =================
static void meshtastic_decrypt(const uint8_t *payload, int payload_len,
                               const meshtastic_header_t *hdr,
                               const uint8_t *original_key)
{
    if (payload_len < 1) return;

    //printf("Payload length: %d bytes\n", payload_len);

    uint8_t nonce[16];
    build_v2_nonce(hdr, nonce);

    uint8_t plaintext[512];
    uint8_t try_key[32];
    size_t try_key_len;
    int success = 0;
    int found_idx = -1;

    // --- DER BEWÄHRTE WEG ---
    for (int k = 0; k < 13; k++) {
        if (k == 0) {
            build_default_key(try_key, &try_key_len);
        } else if (k == 1) {
            build_hamradio_key(try_key, &try_key_len);
        } else if (k == 2) {
            build_admin_key(try_key, &try_key_len);
        } else {
            build_default_key(try_key, &try_key_len);
            try_key[15] = (uint8_t)(try_key[15] + (k - 2));
        }

        key_len = try_key_len; // Globale Länge für AES setzen

        int p_len = aes_ctr_decrypt(payload, payload_len, try_key, nonce, plaintext);

        if (p_len > 0) {
            uint8_t p = plaintext[0];

            // NUR das erste Byte prüfen (PortNum).
            // Das hat vorher funktioniert und das nehmen wir wieder!
            if (p == 0x01 || p == 0x03 || p == 0x04 || p == 0x08 || p == 0x43) {
                success = 1;
                //found_idx = k;
                break;
            }
        }
    }

    if (!success) {
        //printf("Starte Deep-Scan (Nonce-Manipulation)...\n");

        // Teste mit Default-Key
        build_default_key(try_key, &try_key_len);
        if (try_nonce_manipulation(payload, payload_len, try_key, try_key_len, nonce, plaintext)) {
            success = 1;
            found_idx = 99; // Markierung für Nonce-Hit
        }

        // Optional: Teste auch mit HAM-Key, falls Default nicht ging
        if (!success) {
            build_hamradio_key(try_key, &try_key_len);
            if (try_nonce_manipulation(payload, payload_len, try_key, try_key_len, nonce, plaintext)) {
                success = 1;
                found_idx = 98;
            }
        }
    }


    if (success) {
        // Deine gewohnten Erfolgsmeldungen
        if (found_idx == 0) printf("\033[1;32mKey #1 (DEFAULT)\033[0m ");
        else if (found_idx == 1) printf("\033[1;93mKey #2 (HAM-RADIO)\033[0m ");
        else if (found_idx == 2) printf("\033[1;95mKey #3 (ADMIN)\033[0m ");
        else printf("\033[1;96mKey #%d \033[0m ", found_idx + 1);

        /*
        printf("PLAINTEXT HEX: ");
        for (int i = 0; i < payload_len; i++) printf("%02X ", plaintext[i]);

        printf("\nPLAINTEXT ASCII: \033[92m");
        for (int i = 0; i < payload_len; i++) {
            char c = plaintext[i];
            if(c>=32 && c<=126) putchar(c);
            else { printf("\033[90m.\033[92m"); }
        }
        */
        printf("\033[0m");

        // Hop-Info exakt wie du sie willst
        uint8_t remaining_hops = hdr->flags & 0x07;
/*        printf("\nHop-Information:\n");
        printf("  Flags-Byte:          0x%02X\n", hdr->flags);
        printf("  Verbleibende Hops:   %u\n", remaining_hops); */
        printf("Verbrauchte Hops:%u (von max 7) ", 7 - remaining_hops);

        decoded_values_t dv = {0};
        dv.portnum = plaintext[0];
        dv.length = payload_len;
        dv.flags = hdr->flags;
        dv.deviceid = hdr->sender;
        meshtastic_protobuf_decode_table_tabular(plaintext, payload_len, &dv);
    } else {
        // Hier landen jetzt wieder die wirklich privaten Kanäle
        printf("\033[1;31mFAILED:\033[0m Private Key (Hash: 0x%02X) ", hdr->channel);
        printf("Payload: ");
        for (int i = 0; i < payload_len; i++) printf("%02X ", payload[i]);
        printf("\n");
    }


}

// ================= SOCKET READING =================
static ssize_t recv_lora_frame(int fd, uint8_t *buf, size_t maxlen)
{
    uint8_t len_byte;
    ssize_t n = read(fd, &len_byte, 1);
    if (n <= 0) return n;
    if (len_byte == 0 || len_byte > maxlen) return -1;

    size_t total = 0;
    while (total < len_byte) {
        n = read(fd, buf + total, len_byte - total);
        if (n <= 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("read payload");
            return -1;
        }
        total += n;
    }
    return total;
}

// ================= MAIN =================
int main(int argc, char *argv[])
{
    int lora_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lora_fd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DATA433_SOCKET, sizeof(addr.sun_path)-1);

    if (connect(lora_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }

    printf("Verbunden mit %s\n", DATA433_SOCKET);

    int flags = fcntl(lora_fd, F_GETFL, 0);
    fcntl(lora_fd, F_SETFL, flags | O_NONBLOCK);

    uint8_t key[32];
    if(argc >= 2){
        key_len = 32;
    } else build_default_key(key, &key_len);

    uint8_t rxbuf[MAX_FRAME_LEN];
    while(1){
        ssize_t n = recv_lora_frame(lora_fd, rxbuf, sizeof(rxbuf));
        if(n > 0){
            if(n < (ssize_t)sizeof(meshtastic_header_t)) continue;

            LoRaPacket packet;
            memcpy(&packet, rxbuf, n);

            //printf("\n=== NEW FRAME ===\n");
//            printf("Dest: \033[93m0x%08X\033[0m | Sender: \033[93m0x%08X\033[0m | PacketID: \033[33m0x%08X\033[0m | Flags: \033[33m0x%02X\033[0m | Channel: \033[33m0x%02X\033[0m\n",
//                   packet.dest, packet.sender, packet.packet_id, packet.flags, packet.channel_hash);
            printf("\033[93m0x%08X\033[0m > \033[93m0x%08X\033[0m | ",
                   packet.sender, packet.dest);

            meshtastic_header_t hdr = {
                .dest = packet.dest, .sender = packet.sender, .packet_id = packet.packet_id,
                .flags = packet.flags, .channel = packet.channel_hash, .next_hop = packet.next_hop, .relay = packet.relay
            };

            size_t payload_len = n - (sizeof(LoRaPacket) - LORA_MAX_PAYLOAD);
            meshtastic_decrypt(packet.data, payload_len, &hdr, key);
        }
        usleep(10000);
    }

    close(lora_fd);
    return 0;
}
