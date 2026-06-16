#include <string.h>
#include <pico/stdlib.h>
#include "CircBuffer.h"
#include "hardware/pio.h"
#include "loadrom.h"
#include "loop.h"
#include "tusb.h"



CircBuffer<10 * 1024> bufIn;
CircBuffer<1024> bufOut;

static volatile bool bFlushOutBuffer = false;
static volatile uint8_t outLength = 0xff;
bool bSerialEstablished = false;

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
#if 0
    if (serial.available() /* && ((currentStatus & TXFULL) == 0) */ /* && pStreamInBufPtr == pStreamInBufEnd */)
    {
        bSerialEstablished = true;
        while (serial.available())
        {
            bufIn.put(serial.read());
        }
        outLength = 0xFF;
    }
#else
    tuh_task();
    tud_task();
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
  uint8_t buf[64 + 1]; // +1 for extra null character
  uint32_t const bufsize = sizeof(buf) - 1;

  // forward cdc interfaces -> console
  uint32_t count = tud_cdc_n_read(itf, buf, bufsize);
  buf[count] = 0;
  bufIn.put(buf, (uint8_t)count);
  // printf("%s", (char *)buf);
}

static inline void cdc_write(const uint8_t *buf, size_t len)
{
  // loop over all mounted interfaces
  for (uint8_t idx = 0; idx < CFG_TUD_CDC; idx++)
  {
    //if (tud_cdc_n_connected(idx))
    {
      // console --> cdc interfaces
      if (len)
      {
        tud_cdc_n_write(idx, buf, len);
        tud_cdc_n_write_flush(idx);
      }
    }
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
            //serial.write(bufOut.getPtr(), bufOut.getSize());
            //serial.flush();
            cdc_write(bufOut.getPtr(), bufOut.getSize());
            bufOut.setCurToEnd();
        }
        bFlushOutBuffer = false;
    }
}

bool isSerialIn(uint8_t* pCh)
{
    if (bufIn.isEmpty())
        return false;
    if (pCh)
        *pCh = bufIn.getByte();
    return true;
}
