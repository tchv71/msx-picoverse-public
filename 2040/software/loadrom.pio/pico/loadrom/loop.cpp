#include <string.h>
#include <pico/stdlib.h>
#include "CircBuffer.h"
#include "hardware/pio.h"
#include "loadrom.h"
#include "loop.h"
#include "tusb.h"



CircBuffer<2 * 10 * 1024> bufIn;
CircBuffer<1024> bufOut;

static volatile bool bFlushOutBuffer = false;
static volatile uint8_t outLength = 0xff;
volatile bool bSerialEstablished = false;

void __not_in_flash_func(putSerial)(uint8_t c)
{
    bufOut.put(c);
    uint8_t pos = bufOut.getSize();
    if (pos == 2)
        outLength = c + 3;
    if (pos == outLength)
    {
        bFlushOutBuffer = true;
    }
}

void getSerial()
{

    tuh_task();
    tud_task();
#if 1
    uint32_t avail = tud_cdc_available();
    if (avail)
    {
        uint8_t buf[64];
        uint32_t const bufsize = sizeof(buf);

        int32_t circbuf_avail = bufIn.getMaxSize() / 2 - bufIn.getSize();
        if (circbuf_avail >= bufsize)
        {
            uint32_t count = tud_cdc_read(buf, bufsize);
            bufIn.put(buf, (uint8_t)count);
        }
        else
        {
            tud_task();
        }
    }
#endif
}

//--------------------------------------------------------------------+
// TinyUSB callbacks
//--------------------------------------------------------------------+

extern "C"
{
void tud_cdc_rx_cb(uint8_t itf);
}

// Invoked when received new data
void tud_cdc_rx_cb(uint8_t itf)
{
#if 0
  uint8_t buf[64 + 1]; // +1 for extra null character
  uint32_t const bufsize = sizeof(buf) - 1;

  if ((bufIn.getMaxSize() - bufIn.getSize())<bufsize)
    return;
  // forward cdc interfaces -> console
  uint32_t count = tud_cdc_n_read(itf, buf, bufsize);
  buf[count] = 0;
  bufIn.put(buf, (uint8_t)count);
  //tud_cdc_read_flush(); // Drain RX
  // printf("%s", (char *)buf);
#else
    (void)itf;
#endif
}

static inline void cdc_write(const uint8_t *buf, size_t len)
{
    // console --> cdc interfaces
    if (len)
    {
        tud_cdc_write(buf, len);
        tud_cdc_write_flush();
    }
}

void flushSerial()
{
    if (bFlushOutBuffer && bufOut.getSize() > 0)
    {
        uint16_t len;
        if (!bSerialEstablished)
        {
            bufOut.clear();
        }
        else
        {
            cdc_write(bufOut.getPtr(), bufOut.getSize());
            bufOut.setCurToEnd();
        }
        bFlushOutBuffer = false;
    }
}

bool __not_in_flash_func(bufIsEmpty)()
{
    return bufIn.isEmpty();
}


uint8_t __not_in_flash_func(bufGetByte)()
{
    return bufIn.getByte();
}

