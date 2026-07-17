#ifndef __W5500_SPI_H__
#define __W5500_SPI_H__

#include <stdint.h>

/* Initialize W5500 chip: GPIO (CS/RESET), hardware reset, ioLibrary
 * callback registration and socket buffer configuration. */
void W5500_Init(void);

#endif /* __W5500_SPI_H__ */
