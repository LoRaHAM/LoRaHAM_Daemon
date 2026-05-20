#ifndef LORAHAM_CONFIG_APPLY_H
#define LORAHAM_CONFIG_APPLY_H

#include "radio_channel.h"
#include <vector>
#include <utility>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <RadioLib.h>
#include "config_parser.h"
#include "config_value.h"
#include "config_validate.h"

/* --- CONFIG apply callback --- */

template<typename RadioT>
using ConfigApplyFn = void (*)(RadioT& radio,
                               const char *tag,
                               const char *cmd,
                               volatile RadioMode_t& mode,
                               volatile bool& getrssi_active);

/* --- Low-level CONFIG parameter apply functions --- */

void apply_lora_param(SX1278 &radio, const char *tag,
                      const std::string &key, const std::string &val);
void apply_lora_param(RFM95 &radio, const char *tag,
                      const std::string &key, const std::string &val);
void apply_fsk_param(SX1278 &radio, const char *tag,
                     const std::string &key, const std::string &val);
void apply_fsk_param(RFM95 &radio, const char *tag,
                     const std::string &key, const std::string &val);

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
static inline void config_apply_print_prefix(const char *tag, bool *printed)
{
    if (!*printed) {
        printf("[%s]", tag);
        *printed = true;
    }
}

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

    ConfigValidationResult validation;
    if(!config_validate_command(parsed, mode_flag, &validation)) {
        printf("[%s] CONFIG rejected: %s=%s (%s)\n",
               tag,
               validation.key.c_str(),
               validation.value.c_str(),
               validation.reason.c_str());
        return;
    }

    bool printed = false;

    // --- 1. Pass: MODE= zuerst, GETRSSI= direkt, Rest sammeln ---
    std::vector<std::pair<std::string,std::string>> tokens;
    std::string mode_val = parsed.mode;

    for(auto &kv : parsed.tokens) {
        const std::string &key = kv.first;
        const std::string &val = kv.second;

        if(key == "GETRSSI") {
            int v = 0;
            config_apply_print_prefix(tag, &printed);
            if(config_value_parse_bool01_exact(val, &v)) {
                getrssi_flag = (v != 0);
                if(v == 1) printf(" GETRSSI=[92m1[0m");
                else       printf(" GETRSSI=[92m0[0m");
            } else {
                printf(" GETRSSI=[91;5m%s[0m", val.c_str());
            }
        } else {
            tokens.push_back(kv);
        }
    }

    // --- MODE= auswerten und Radio ggf. neu initialisieren ---
    if(!mode_val.empty()) {
        config_apply_print_prefix(tag, &printed);

        if(mode_val == "FSK") {
            printf(" MODE=FSK -> beginFSK()");
            int state = radio.beginFSK();
            if(state == RADIOLIB_ERR_NONE) {
                mode_flag = RADIO_MODE_FSK;
                printf(" \033[92mOK\033[0m");
            } else {
                printf(" \033[91;5mFEHLER:%d\033[0m", state);
            }
        } else if(mode_val == "LORA") {
            printf(" MODE=LORA -> begin()");
            int state = radio.begin();
            if(state == RADIOLIB_ERR_NONE) {
                mode_flag = RADIO_MODE_LORA;
                printf(" \033[92mOK\033[0m");
            } else {
                printf(" \033[91;5mFEHLER:%d\033[0m", state);
            }
        } else {
            printf(" MODE=\033[91;5m%s\033[0m (unbekannt, ignoriert)", mode_val.c_str());
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

template<typename RadioT>
static void config_apply_command(RadioT& radio,
                                 const char *tag,
                                 const char *cmd,
                                 volatile RadioMode_t& mode,
                                 volatile bool& getrssi_active)
{
    parse_and_apply_config_generic<RadioT>(radio, tag, cmd, mode, getrssi_active);
}

#endif
