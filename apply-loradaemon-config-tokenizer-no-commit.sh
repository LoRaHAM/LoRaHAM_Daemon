#!/usr/bin/env bash
set -euo pipefail

REF_DIR="loradaemon_refactor"
DAEMON="$REF_DIR/loradaemon_320_108.cpp"
BUILD_SH="$REF_DIR/build.sh"
PARSER_H="$REF_DIR/config_parser.h"
PARSER_CPP="$REF_DIR/config_parser.cpp"

run_tx=false
if [[ "${1:-}" == "--TX" || "${1:-}" == "--tx" ]]; then
  run_tx=true
elif [[ $# -gt 0 ]]; then
  echo "Usage: $0 [--TX]" >&2
  exit 2
fi

if [[ ! -d .git ]]; then
  echo "ERROR: run this from the git repository root." >&2
  exit 1
fi

for f in "$DAEMON" "$BUILD_SH"; do
  if [[ ! -f "$f" ]]; then
    echo "ERROR: missing file: $f" >&2
    exit 1
  fi
done

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "ERROR: tracked files have uncommitted changes." >&2
  echo "Commit/stash them first, then run this script again." >&2
  exit 1
fi

echo "[1/5] Add config parser module"

cat > "$PARSER_H" <<'EOF'
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
EOF

cat > "$PARSER_CPP" <<'EOF'
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
EOF

echo "[2/5] Patch daemon to use config parser"

python3 - <<'PY'
from pathlib import Path
import re

path = Path("loradaemon_refactor/loradaemon_320_108.cpp")
text = path.read_text()

if '#include "config_parser.h"' not in text:
    text = text.replace(
        '#include "client_set.h"\n',
        '#include "client_set.h"\n#include "config_parser.h"\n'
    )

pattern = re.compile(
    "    std::string s\\(cmd\\);\\n.*?    // --- MODE= auswerten",
    re.DOTALL
)

replacement = """    ConfigCommand parsed = config_parse_command(cmd);

    if(!parsed.is_set) {
        printf("[%s] Unbekannter Befehl: %s\\n", tag, parsed.text.c_str());
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
                if(v == 1) printf(" GETRSSI=\\033[92m1\\033[0m");
                else       printf(" GETRSSI=\\033[92m0\\033[0m");
            } else {
                printf(" GETRSSI=\\033[91;5m%d\\033[0m", v);
            }
        } else {
            tokens.push_back(kv);
        }
    }

    // --- MODE= auswerten"""

text, count = pattern.subn(replacement, text, count=1)
if count != 1:
    raise SystemExit("ERROR: parser block replacement failed")

path.write_text(text)
PY

echo "[3/5] Patch build.sh"

python3 - <<'PY'
from pathlib import Path

path = Path("loradaemon_refactor/build.sh")
text = path.read_text()

needle = '    "$SCRIPT_DIR/client_set.cpp" \\\n'
replacement = (
    '    "$SCRIPT_DIR/client_set.cpp" \\\n'
    '    "$SCRIPT_DIR/config_parser.cpp" \\\n'
)

if '"$SCRIPT_DIR/config_parser.cpp"' not in text:
    if needle not in text:
        raise SystemExit("ERROR: could not find client_set.cpp line in build.sh")
    text = text.replace(needle, replacement)

path.write_text(text)
PY

echo "[4/5] Show diff"

git diff -- "$PARSER_H" "$PARSER_CPP" "$DAEMON" "$BUILD_SH"

echo "[5/5] Run tests"

./loradaemon_refactor/run_tests.sh

if [[ "$run_tx" == true ]]; then
  ./loradaemon_refactor/run_tests.sh --TX --rx-seconds 5
else
  echo
  echo "INFO: RF TX test not run."
  echo "Run this script with --TX to include it:"
  echo "  $0 --TX"
fi

echo
echo "Done. No files were staged or committed."
echo
echo "Review:"
echo "  git diff"
echo
echo "Commit separately:"
echo "  git add loradaemon_refactor/config_parser.h \\"
echo "          loradaemon_refactor/config_parser.cpp \\"
echo "          loradaemon_refactor/loradaemon_320_108.cpp \\"
echo "          loradaemon_refactor/build.sh"
echo "  git commit -m \"Extract loradaemon config tokenizer\""
echo "  git push origin hardening/daemon-tests"
