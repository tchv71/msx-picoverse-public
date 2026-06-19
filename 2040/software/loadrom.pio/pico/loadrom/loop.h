#pragma once
#include <stdint-gcc.h>

static const uint16_t FT245R = 0xBFF8;//0xFFF8; //FT245R = 0x94u;
static const uint8_t FT245R_RXEMPTY = 1; // MASK FOR RX BUFFER EMPTY
static const uint8_t FT245R_TXFULL = 2; // MASK FOR TX BUFFER FULL

#ifdef __cplusplus
extern "C"
{
#endif
    void getSerial();
    void __not_in_flash_func(putSerial)(uint8_t c);
    bool isSerialIn(uint8_t *pCh);
    void flushSerial();
    void fillSerial(const uint8_t* buf, uint8_t len);
#ifdef __cplusplus
}
#endif
