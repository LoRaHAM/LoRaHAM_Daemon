#include "config_stream.h"

#include <errno.h>

/* --- CONFIG stream framing ---------------------------------------------- */

void config_stream_init(ConfigStreamBuffer *stream)
{
    if (!stream)
        return;

    stream->len = 0;
    stream->line[0] = '\0';
}

void config_stream_init_all(ConfigStreamBuffer *streams, int count)
{
    if (!streams || count <= 0)
        return;

    for (int i = 0; i < count; i++)
        config_stream_init(&streams[i]);
}

static int config_stream_emit(ConfigStreamBuffer *stream,
                              ConfigStreamLineFn on_line,
                              void *user)
{
    if (!stream || !on_line) {
        errno = EINVAL;
        return -1;
    }

    stream->line[stream->len] = '\0';

    if (stream->len > 0)
        on_line(stream->line, user);

    config_stream_init(stream);
    return 0;
}

int config_stream_feed(ConfigStreamBuffer *stream,
                       const uint8_t *buf,
                       size_t len,
                       ConfigStreamLineFn on_line,
                       void *user)
{
    if (!stream || (len > 0 && !buf) || !on_line) {
        errno = EINVAL;
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t c = buf[i];

        if (c == '\r')
            continue;

        if (c == '\n') {
            if (config_stream_emit(stream, on_line, user) != 0)
                return -1;
            continue;
        }

        if (stream->len + 1 >= CONFIG_STREAM_LINE_LEN) {
            config_stream_init(stream);
            errno = EMSGSIZE;
            return -1;
        }

        stream->line[stream->len++] = (char)c;
    }

    return 0;
}

int config_stream_flush(ConfigStreamBuffer *stream,
                        ConfigStreamLineFn on_line,
                        void *user)
{
    if (!stream || !on_line) {
        errno = EINVAL;
        return -1;
    }

    if (stream->len == 0)
        return 0;

    return config_stream_emit(stream, on_line, user);
}
