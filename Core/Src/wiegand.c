#include "wiegand.h"
#include <stdio.h>
#include <string.h>

#define STABILITY_DELAY 5
#define FOUR_BIT_WIEGAND 4
#define ENTER_KEY_AS_HASH 11
#define CLEAR_KEY_AS_STAR 10
struct Wiegand {
    volatile uint64_t bits;
    volatile int bit_count;
    volatile unsigned long last_bit_time;
    int standard; // 26 or 34 bits
    int ready;    // 1 if a complete Wiegand code has been received
};

static struct Wiegand WiegandData = {.standard = FOUR_BIT_WIEGAND, .ready = 0}; // Initialize the Wiegand structure

volatile uint64_t rawWiegandData = 0;
volatile int bitCount = 0;
volatile unsigned long lastBitTime = 0;

static char pinBuffer[20] = "";
static unsigned long lastKeyPressTime = 0;

void Wiegand_Init(void) {
    pinBuffer[0] = '\0';
    printf("\r\n=============================================\r\n");
    printf("STM32F411 ASR1201 Keypad Decoder Ready\r\n");
    printf("Enter %d digits then press #\r\n", DIGITS_REQUIRED);
    printf("=============================================\r\n");
}

void Wiegand_DeInit(void) {
    pinBuffer[0] = '\0';
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    // Reset packet if a long gap occurs between scans (e.g., > 25ms)
    uint32_t current_time = HAL_GetTick();
    if(WiegandData.ready){
        // If a complete Wiegand code has been received, ignore further bits until processed
        return;
    }
    if ((current_time - WiegandData.last_bit_time) > 25) {
        WiegandData.bits = 0;
        WiegandData.bit_count = 0;
        WiegandData.ready = 0;
    }
    WiegandData.last_bit_time = current_time;

    // Shift in bits based on which line dropped low
    if (GPIO_Pin == D0_Pin) {        // D0 went LOW
        WiegandData.bits <<= 1;
    } 
    else if (GPIO_Pin == D1_Pin) {   // D1 went LOW
        WiegandData.bits = (WiegandData.bits << 1) | 1;
    }
    WiegandData.bit_count++;
    // Check if expected length is reached
    if ( WiegandData.bit_count == FOUR_BIT_WIEGAND) {
        WiegandData.standard = WiegandData.bit_count;
        WiegandData.ready = 1;
    }
}


// returns -1 in error situation, and number of digits collected in key_buffer otherwise
WiegandKeyError collect_wiegand_keypress(char* key_buffer, int buffer_size){
    printf("Waiting for Wiegand keypress...\r\n");
    if(key_buffer == NULL || buffer_size <= 0) {
        printf("ERROR: Invalid buffer provided to collect_wiegand_keypress\r\n");
        return WIEGAND_INVALID_BUFFER; // Invalid buffer
    }
    if(buffer_size < DIGITS_REQUIRED) {
        printf("ERROR: Buffer size too small. Minimum required is %d\r\n", DIGITS_REQUIRED);
        return WIEGAND_BUFFER_OVERFLOW; // Buffer too small
    }
    int number_of_key_pressed = 0;
    while (1){
        if (WiegandData.ready) {
            // Process the Wiegand data
            uint64_t wiegand_data = WiegandData.bits & 0x0F; // Extract the last 4 bits (the key pressed)
            int bit_count = WiegandData.bit_count;

            // Reset for next read
            WiegandData.bits = 0;
            WiegandData.bit_count = 0;
            WiegandData.ready = 0;

            // only handling 4-bit Wiegand for now
            if (bit_count != FOUR_BIT_WIEGAND) {
                printf("Unsupported Wiegand format: %d bits\r\n", bit_count);
                return WIEGAND_UNSUPPORTED_FORMAT;
            }

            if(wiegand_data == ENTER_KEY_AS_HASH){
                /* Reject short/incomplete entries instead of sending a
                 * stale/partial PIN to the backend. */
                if (number_of_key_pressed != DIGITS_REQUIRED) {
                    printf("\r\nIncomplete PIN (%d/%d digits) - resetting\r\n",
                           number_of_key_pressed, DIGITS_REQUIRED);
                    number_of_key_pressed = 0;
                    key_buffer[0] = '\0';
                    return WIEGAND_INVALID_KEY;
                }
                printf("\r\nCollected PIN: %s\r\n", key_buffer);
                return WIEGAND_OK;
            }
            if(wiegand_data == CLEAR_KEY_AS_STAR){
                number_of_key_pressed = 0;
                key_buffer[0] = '\0';
                printf("\r\n[Buffer Cleared]\r\n");
                continue; /* control key, not a digit - do not store it */
            }
            if(number_of_key_pressed >= (buffer_size - 1)) {
                printf("ERROR: Buffer overflow. Too many key presses.\r\n");
                return WIEGAND_BUFFER_OVERFLOW;
            }

            printf("Key Pressed: %hu\r\n", (uint8_t)wiegand_data);
            key_buffer[number_of_key_pressed] = (char)('0' + wiegand_data);
            number_of_key_pressed++;
        }
    }
}
