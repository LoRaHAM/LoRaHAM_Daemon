#include "config_parser.h"

#include <ctype.h>

/* --- String helpers --- */

static void uppercase_in_place(std::string &s)
{
    for (char &c : s)
        c = toupper((unsigned char)c);
}

/* --- SET KEY=VALUE tokenizer --- */

ConfigCommand config_parse_command(const char *cmd)
{
    ConfigCommand result;
    result.is_set = false;
    result.has_params = false;
    result.text = cmd ? cmd : "";

    while(!result.text.empty() &&
          (result.text.back() == '\n' || result.text.back() == '\r')) {
        result.text.pop_back();
    }

    if(result.text.rfind("SET", 0) != 0)
        return result;

    result.is_set = true;

    size_t pos = result.text.find(' ');
    if(pos == std::string::npos)
        return result;

    result.has_params = true;
    std::string rest = result.text.substr(pos + 1);

    size_t start = 0;
    while(start < rest.size()) {
        size_t end = rest.find(' ', start);
        if(end == std::string::npos)
            end = rest.size();

        std::string token = rest.substr(start, end - start);
        if(!token.empty()) {
            size_t eq = token.find('=');
            if(eq != std::string::npos && eq > 0 && eq + 1 < token.size()) {
                std::string key = token.substr(0, eq);
                std::string val = token.substr(eq + 1);

                uppercase_in_place(key);

                if(key == "MODE") {
                    uppercase_in_place(val);
                    result.mode = val;
                } else {
                    result.tokens.push_back({key, val});
                }
            }
        }

        start = end + 1;
    }

    return result;
}
