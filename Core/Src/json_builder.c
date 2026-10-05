#include <json_builder.h>
#include <string.h>
#include <stdio.h>

int build_json_from_pin(char* buffer, size_t size_of_buffer, char* pin_buffer, size_t size_of_pin_buffer){

if(!buffer || !pin_buffer){
    printf("invalid buffer");
    return -1;
}

if(size_of_buffer < size_of_pin_buffer){
    return -1;
}

int err = snprintf(buffer, size_of_buffer,
    "{\r\n"
    "  \"pin\": \"%s\"\r\n"
    "}",
    pin_buffer);
    if(err < 0){
        return err;
    }
    return strlen(buffer);
}
