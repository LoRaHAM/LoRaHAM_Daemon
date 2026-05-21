#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * Minimal key=value config loader.
 * Only '#' comments are supported.
 */

static char *trim(char *s)
{
    char *end;

    if (!s) {
        return s;
    }

    while (*s && isspace((unsigned char)*s)) {
        s++;
    }

    if (*s == '\0') {
        return s;
    }

    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return s;
}

static int copy_string(char *dst, size_t dst_size, const char *src)
{
    int ret;

    if (!dst || dst_size == 0 || !src) {
        return LHKT_ERR;
    }

    ret = snprintf(dst, dst_size, "%s", src);
    if (ret < 0 || (size_t)ret >= dst_size) {
        return LHKT_ERR_NOSPACE;
    }

    return LHKT_OK;
}

static int parse_long_value(const char *text, long min, long max, long *out)
{
    char *end;
    long value;

    if (!text || !out || text[0] == '\0') {
        return LHKT_ERR_FORMAT;
    }

    errno = 0;
    value = strtol(text, &end, 0);

    if (errno != 0) {
        return LHKT_ERR_FORMAT;
    }

    end = trim(end);
    if (*end != '\0') {
        return LHKT_ERR_FORMAT;
    }


    if (value < min || value > max) {
        return LHKT_ERR_FORMAT;
    }

    *out = value;
    return LHKT_OK;
}

static int parse_double_value(const char *text, double min, double max, double *out)
{
    char *end;
    double value;

    if (!text || !out || text[0] == '\0') {
        return LHKT_ERR_FORMAT;
    }

    errno = 0;
    value = strtod(text, &end);

    if (errno != 0) {
        return LHKT_ERR_FORMAT;
    }

    end = trim(end);
    if (*end != '\0') {
        return LHKT_ERR_FORMAT;
    }

    if (!isfinite(value)) {
        return LHKT_ERR_FORMAT;
    }

    if (value < min || value > max) {
        return LHKT_ERR_FORMAT;
    }

    *out = value;
    return LHKT_OK;
}

static int parse_bool_value(const char *text, int *out)
{
    long value;

    if (!text || !out) {
        return LHKT_ERR;
    }

    if (strcasecmp(text, "true") == 0 ||
        strcasecmp(text, "yes") == 0 ||
        strcasecmp(text, "on") == 0) {
        *out = 1;
        return LHKT_OK;
    }

    if (strcasecmp(text, "false") == 0 ||
        strcasecmp(text, "no") == 0 ||
        strcasecmp(text, "off") == 0) {
        *out = 0;
        return LHKT_OK;
    }

    if (parse_long_value(text, 0, 1, &value) != LHKT_OK) {
        return LHKT_ERR_FORMAT;
    }

    *out = (int)value;
    return LHKT_OK;
}

static int parse_optional_freq(const char *value,
                               double *freq,
                               int *have_freq)
{
    double parsed;

    if (!value || !freq || !have_freq) {
        return LHKT_ERR;
    }

    if (value[0] == '\0') {
        *freq = 0.0;
        *have_freq = 0;
        return LHKT_OK;
    }

    if (parse_double_value(value, 0.001, 10000.0, &parsed) != LHKT_OK) {
        return LHKT_ERR_FORMAT;
    }

    *freq = parsed;
    *have_freq = 1;
    return LHKT_OK;
}


/*
 * Parse one config line.
 * Empty lines and lines starting with '#' are ignored.
 * Inline '#' comments are also removed.
 */
int lhkt_config_parse_line(lhkt_config_t *cfg, char *line, unsigned int line_no)
{
    char *comment;
    char *eq;
    char *key;
    char *value;
    long lval;
    double dval;
    int bval;
    int ret;

    (void)line_no;

    if (!cfg || !line) {
        return LHKT_ERR;
    }

    comment = strchr(line, '#');
    if (comment) {
        *comment = '\0';
    }

    key = trim(line);
    if (*key == '\0') {
        return LHKT_OK;
    }

    eq = strchr(key, '=');
    if (!eq) {
        return LHKT_ERR_FORMAT;
    }

    *eq = '\0';
    value = eq + 1;

    key = trim(key);
    value = trim(value);

    if (*key == '\0') {
        return LHKT_ERR_FORMAT;
    }

    if (strcmp(key, "kiss_host") == 0) {
        return copy_string(cfg->kiss_host, sizeof(cfg->kiss_host), value);
    }

    if (strcmp(key, "data_socket") == 0) {
        return copy_string(cfg->data_socket, sizeof(cfg->data_socket), value);
    }

    if (strcmp(key, "conf_socket") == 0) {
        return copy_string(cfg->conf_socket, sizeof(cfg->conf_socket), value);
    }


    if (strcmp(key, "kiss_port") == 0) {
        ret = parse_long_value(value, 1, 65535, &lval);
        if (ret == LHKT_OK) {
            cfg->kiss_port = (int)lval;
        }
        return ret;
    }

    if (strcmp(key, "rx_only") == 0) {
        ret = parse_bool_value(value, &bval);
        if (ret == LHKT_OK) {
            cfg->rx_only = bval;
        }
        return ret;
    }

    if (strcmp(key, "verbose") == 0) {
        ret = parse_bool_value(value, &bval);
        if (ret == LHKT_OK) {
            cfg->verbose = bval;
        }
        return ret;
    }

    if (strcmp(key, "stats_interval") == 0) {
        ret = parse_long_value(value, 0, 86400, &lval);
        if (ret == LHKT_OK) {
            cfg->stats_interval = (int)lval;
        }
        return ret;
    }

    if (strcmp(key, "tx_settle_ms") == 0) {
        ret = parse_long_value(value, 0, 60000, &lval);
        if (ret == LHKT_OK) {
            cfg->tx_settle_ms = (int)lval;
        }
        return ret;
    }

    if (strcmp(key, "tx_return_ms") == 0) {
        ret = parse_long_value(value, 0, 60000, &lval);
        if (ret == LHKT_OK) {
            cfg->tx_return_ms = (int)lval;
        }
        return ret;
    }

    if (strcmp(key, "rx_freq") == 0) {
        return parse_optional_freq(value, &cfg->rx_freq, &cfg->have_rx_freq);
    }

    if (strcmp(key, "tx_freq") == 0) {
        return parse_optional_freq(value, &cfg->tx_freq, &cfg->have_tx_freq);
    }

    if (strcmp(key, "sf") == 0) {
        ret = parse_long_value(value, 7, 12, &lval);
        if (ret == LHKT_OK) {
            cfg->sf = (int)lval;
        }
        return ret;
    }

    if (strcmp(key, "bw") == 0) {
        ret = parse_double_value(value, 1.0, 1000.0, &dval);
        if (ret == LHKT_OK) {
            cfg->bw = dval;
        }
        return ret;
    }

    if (strcmp(key, "cr") == 0) {
        ret = parse_long_value(value, 5, 8, &lval);
        if (ret == LHKT_OK) {
            cfg->cr = (int)lval;
        }
        return ret;
    }

    if (strcmp(key, "crc") == 0) {
        ret = parse_bool_value(value, &bval);
        if (ret == LHKT_OK) {
            cfg->crc = bval;
        }
        return ret;
    }

    if (strcmp(key, "preamble") == 0) {
        ret = parse_long_value(value, 1, 65535, &lval);
        if (ret == LHKT_OK) {
            cfg->preamble = (int)lval;
        }
        return ret;
    }

    if (strcmp(key, "syncword") == 0) {
        ret = parse_long_value(value, 0, 255, &lval);
        if (ret == LHKT_OK) {
            cfg->syncword = (int)lval;
        }
        return ret;
    }

    if (strcmp(key, "ldro") == 0) {
        if (strcmp(value, "AUTO") == 0 || strcmp(value, "auto") == 0) {
            cfg->ldro_auto = 1;
            return LHKT_OK;
        }

        ret = parse_bool_value(value, &bval);
        if (ret == LHKT_OK) {
            cfg->ldro = bval;
            cfg->ldro_auto = 0;
        }
        return ret;
    }

    if (strcmp(key, "power") == 0) {
        ret = parse_long_value(value, 0, 20, &lval);
        if (ret == LHKT_OK) {
            cfg->power = (int)lval;
        }
        return ret;
    }

    return LHKT_ERR_FORMAT;
}

int lhkt_config_load_file(lhkt_config_t *cfg, const char *path)
{
    FILE *fp;
    char line[LHKT_TNC2_MAX_LINE];
    unsigned int line_no;
    int ret;

    if (!cfg || !path || path[0] == '\0') {
        return LHKT_ERR;
    }

    fp = fopen(path, "r");
    if (!fp) {
        return LHKT_ERR;
    }

    line_no = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_no++;

        if (!strchr(line, '\n') && !feof(fp)) {
            fclose(fp);
            return LHKT_ERR_LONG;
        }

        ret = lhkt_config_parse_line(cfg, line, line_no);
        if (ret != LHKT_OK) {
            fclose(fp);
            return ret;
        }
    }

    fclose(fp);
    return LHKT_OK;
}
