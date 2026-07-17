#ifndef __JSON_BUILDER_H__
#define __JSON_BUILDER_H__
#include <stdlib.h>
// builds json payload from pin
// returns size of json string or -1 if some goes wrong

int build_json_from_pin(char* buffer, size_t size_of_buffer, char* pin_buffer, size_t size_of_pin_buffer);

#endif


