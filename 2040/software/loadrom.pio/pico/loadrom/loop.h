#pragma once
#include <stdint-gcc.h>

static const uint16_t FT245R = 0x20;//0xBFF8;//0xFFF8; //FT245R = 0x94u;

static const uint16_t FT245RM = 0xBFF8;
static const uint8_t FT245R_Subslot = 2;
static const uint8_t FT245R_Magic = 0x34;

static const uint8_t FT245R_RXEMPTY = 1; // MASK FOR RX BUFFER EMPTY
static const uint8_t FT245R_TXFULL = 2; // MASK FOR TX BUFFER FULL

#ifdef __cplusplus
extern "C"
{
#endif
    void getSerial();
    void __not_in_flash_func(putSerial)(uint8_t c);
    void flushSerial();
    void fillSerial(const uint8_t* buf, uint8_t len);
    bool __not_in_flash_func(bufIsEmpty)();
    uint8_t __not_in_flash_func(bufGetByte)();
#ifdef __cplusplus
}
#endif


