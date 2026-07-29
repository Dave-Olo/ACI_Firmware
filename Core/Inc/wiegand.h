#ifndef WIEGAND_H
#define WIEGAND_H

#include "main.h"

#define TIMEOUT_PER_KEY_MS     80
#define SEQUENCE_TIMEOUT_MS  8000
#define DIGITS_REQUIRED        6
#define DIGIT_BITS             4

typedef enum {
    WIEGAND_OK = 0,
    WIEGAND_BUFFER_OVERFLOW = 1,
    WIEGAND_INVALID_BUFFER = 2,
    WIEGAND_UNSUPPORTED_FORMAT = 3,
    WIEGAND_INVALID_KEY = 4,
} WiegandKeyError;

void Wiegand_Init(void);
void Wiegand_DeInit(void);
WiegandKeyError collect_wiegand_keypress(char* key_buffer, int buffer_size);

#endif