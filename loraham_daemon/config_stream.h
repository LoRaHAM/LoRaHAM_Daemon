#ifndef LORAHAM_CONFIG_STREAM_H
#define LORAHAM_CONFIG_STREAM_H

#include <stddef.h>
#include <stdint.h>

/* --- CONFIG stream framing ---------------------------------------------- */

#define CONFIG_STREAM_LINE_LEN 256

typedef struct {
    char line[CONFIG_STREAM_LINE_LEN];
    size_t len;
} ConfigStreamBuffer;

typedef void (*ConfigStreamLineFn)(const char *line, void *user);

void config_stream_init(ConfigStreamBuffer *stream);
void config_stream_init_all(ConfigStreamBuffer *streams, int count);
int config_stream_feed(ConfigStreamBuffer *stream,
                       const uint8_t *buf,
                       size_t len,
                       ConfigStreamLineFn on_line,
                       void *user);
int config_stream_flush(ConfigStreamBuffer *stream,
                        ConfigStreamLineFn on_line,
                        void *user);

#endif
