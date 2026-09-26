#ifndef LORAHAM_CONFIG_PARSER_H
#define LORAHAM_CONFIG_PARSER_H

#include <string>
#include <utility>
#include <vector>

/* --- Parsed SET command --- */

struct ConfigCommand {
    bool is_set;
    bool has_params;
    std::string text;
    std::string mode;
    /* Number of MODE tokens seen: MODE is extracted from the
     * token list, so plain duplicate-key detection never saw a second MODE
     * and silently applied last-one-wins. */
    int mode_count = 0;
    std::vector<std::pair<std::string, std::string>> tokens;
    std::vector<std::string> malformed_tokens;
};

/* --- SET KEY=VALUE tokenizer --- */

ConfigCommand config_parse_command(const char *cmd);

/* True when the line is a SET that would touch RF hardware (MODE or any
 * non-GETRSSI parameter) — the dispatcher defers such commands while queued
 * TX jobs exist. GET/flag-only/unknown lines return false. */
bool config_command_touches_radio(const char *line);

#endif
