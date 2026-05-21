#ifndef TNC2_H
#define TNC2_H

#include <stddef.h>
#include "ax25.h"

#ifdef __cplusplus
extern "C" {
#endif

int tnc2_parse_line(const char *line, ax25_frame_t *frame);

int tnc2_format_line(const ax25_frame_t *frame,
                     char *out,
                     size_t out_size,
                     size_t *out_len);

int tnc2_strip_eol(char *line);

#ifdef __cplusplus
}
#endif

#endif
