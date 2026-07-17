#include <json_builder.h>
#include <string.h>
#include <stdio.h>

const char* facility_name = "";
const char* facility_id = "";

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
    "  \"accessCode\": \"%s\",\r\n"
    "  \"facilityName\": \"Your Facility Name\",\r\n"
    "  \"facilityId\": \"FAC001\"\r\n"
    "}",
    pin_buffer);
    if(err < 0){
        return err;
    }
    return strlen(buffer);
}
