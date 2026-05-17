#include "config_apply.h"

#include <ctype.h>
#include <math.h>

/* --- CONFIG helpers ------------------------------------------------------ */

static std::string config_trim_ascii(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() && isspace((unsigned char)value[begin]))
        begin++;

    size_t end = value.size();
    while (end > begin && isspace((unsigned char)value[end - 1]))
        end--;

    return value.substr(begin, end - begin);
}

static std::string config_lower_ascii(std::string value)
{
    for (size_t i = 0; i < value.size(); i++)
        value[i] = (char)tolower((unsigned char)value[i]);

    return value;
}

static bool config_float_equal(float value, float expected)
{
    return fabsf(value - expected) < 0.0001f;
}

static bool config_parse_float_exact(const std::string &value, float *out)
{
    if (!out)
        return false;

    char *end = NULL;
    const char *start = value.c_str();
    float parsed = strtof(start, &end);

    if (end == start)
        return false;

    while (*end != '\0' && isspace((unsigned char)*end))
        end++;

    if (*end != '\0')
        return false;

    *out = parsed;
    return true;
}

static bool parse_fsk_shaping(const std::string &val,
                              uint8_t *shaping,
                              const char **label)
{
    if (!shaping || !label)
        return false;

    std::string norm = config_lower_ascii(config_trim_ascii(val));

    if (norm == "off" || norm == "none") {
        *shaping = RADIOLIB_SHAPING_NONE;
        *label = "0.0";
        return true;
    }

    float value = 0.0f;
    if (!config_parse_float_exact(norm, &value))
        return false;

    if (config_float_equal(value, 0.0f)) {
        *shaping = RADIOLIB_SHAPING_NONE;
        *label = "0.0";
        return true;
    }

    if (config_float_equal(value, 0.3f)) {
        *shaping = RADIOLIB_SHAPING_0_3;
        *label = "0.3";
        return true;
    }

    if (config_float_equal(value, 0.5f)) {
        *shaping = RADIOLIB_SHAPING_0_5;
        *label = "0.5";
        return true;
    }

    if (config_float_equal(value, 0.7f)) {
        *shaping = RADIOLIB_SHAPING_0_7;
        *label = "0.7";
        return true;
    }

    if (config_float_equal(value, 1.0f)) {
        *shaping = RADIOLIB_SHAPING_1_0;
        *label = "1.0";
        return true;
    }

    return false;
}

/* --- CONFIG parameter apply --------------------------------------------- */

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
        // Gauss-Filter Shaping: 0.0=aus, 0.3, 0.5, 0.7, 1.0
        uint8_t sh = 0;
        const char *sh_label = NULL;
        if(parse_fsk_shaping(val, &sh, &sh_label)) {
            state = radio.setDataShaping(sh);
            if(state == 0) printf(" SHAPING=\033[92m%s\033[0m", sh_label);
            else           printf(" SHAPING=\033[91;5m%s\033[0m", sh_label);
        } else {
            printf(" SHAPING=\033[91;5m%s\033[0m", val.c_str());
        }
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
        uint8_t sh = 0;
        const char *sh_label = NULL;
        if(parse_fsk_shaping(val, &sh, &sh_label)) {
            state = radio.setDataShaping(sh);
            if(state == 0) printf(" SHAPING=\033[92m%s\033[0m", sh_label);
            else           printf(" SHAPING=\033[91;5m%s\033[0m", sh_label);
        } else {
            printf(" SHAPING=\033[91;5m%s\033[0m", val.c_str());
        }
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
