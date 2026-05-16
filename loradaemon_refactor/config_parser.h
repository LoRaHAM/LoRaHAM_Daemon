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
    std::vector<std::pair<std::string, std::string>> tokens;
};

/* --- SET KEY=VALUE tokenizer --- */

ConfigCommand config_parse_command(const char *cmd);

#endif
