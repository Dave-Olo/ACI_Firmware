/*
 * W5500 SPI glue driver for STM32F4 HAL + WIZnet ioLibrary.
 *
 * Pinout:
 *   RESET -> PA3
 *   CS    -> PA4
 *   SPI1  -> SCK/MISO/MOSI (configured by CubeMX in main.c)
 */

#include "stm32f4xx_hal.h"
#include "wizchip_conf.h"
#include "w5500_spi.h"
#include <stdio.h>

extern SPI_HandleTypeDef hspi1;

static uint8_t SPIReadWrite(uint8_t data)
{
    __IO uint8_t dummy;

    /* Clear stale overrun before a new byte transaction. */
    if ((hspi1.Instance->SR & SPI_FLAG_OVR) == SPI_FLAG_OVR)
    {
        dummy = *(__IO uint8_t *)&hspi1.Instance->DR;
        dummy = *(__IO uint8_t *)&hspi1.Instance->SR;
        (void)dummy;
    }

    while ((hspi1.Instance->SR & SPI_FLAG_TXE) != SPI_FLAG_TXE)
    {
        __NOP();
    }
    *(__IO uint8_t *)&hspi1.Instance->DR = data;

    while ((hspi1.Instance->SR & SPI_FLAG_RXNE) != SPI_FLAG_RXNE)
    {
        __NOP();
    }
    data = *(__IO uint8_t *)&hspi1.Instance->DR;

    while ((hspi1.Instance->SR & SPI_FLAG_BSY) == SPI_FLAG_BSY)
    {
        __NOP();
    }

    return data;
}

static void wizchip_select(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
}

static void wizchip_deselect(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

static uint8_t wizchip_read(void)
{
    return SPIReadWrite(0x00);
}

static void wizchip_write(uint8_t wb)
{
    SPIReadWrite(wb);
}

static void wizchip_read_burst(uint8_t* pBuf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        *pBuf++ = SPIReadWrite(0x00);
    }
}

static void wizchip_write_burst(uint8_t* pBuf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        SPIReadWrite(*pBuf++);
    }
}

static void W5500IO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3 | GPIO_PIN_4, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_3 | GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void W5500_Init(void)
{
    /* 16KB RX/TX for socket 0 (TLS/HTTP), 0 for the rest. */
    uint8_t memsize[2][8] = {{16, 0, 0, 0, 0, 0, 0, 0}, {16, 0, 0, 0, 0, 0, 0, 0}};

    W5500IO_Init();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET); /* CS high by default */

    /* Hardware reset pulse: RESET low >=500us, then allow PHY/logic to settle. */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET);
    HAL_Delay(2);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET);
    HAL_Delay(50);

    reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
    reg_wizchip_spi_cbfunc(wizchip_read, wizchip_write);
    reg_wizchip_spiburst_cbfunc(wizchip_read_burst, wizchip_write_burst);

    if (ctlwizchip(CW_INIT_WIZCHIP, (void*)memsize) == -1)
    {
        printf("W5500 Initialization failed.\r\n");
        while (1)
        {
        }
    }
    printf("W5500 Initialization successful.\r\n");
}
