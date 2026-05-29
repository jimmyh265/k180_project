#ifndef __NV_CMD__HPP__
#define __NV_CMD__HPP__

#include <cstdint>

#define NVCMD_PORT  (6666)

enum nvcmd_t
{
    NVCMD_DETECT   = 0xFF80,
    NVCMD_TRACKING = 0xFF82,
};

class NV_CMD
{
public:
    NV_CMD()
    {
        memset(&payload, 0x00, sizeof(payload));        
    }

    NV_CMD(nvcmd_t cmd, uint8_t onoff)
    {
        payload.cmd = cmd;
        payload.onoff = onoff;
    }

    NV_CMD(nvcmd_t cmd, uint8_t onoff, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
    {
        payload.cmd = cmd;
        payload.onoff = onoff;
        payload.x = x;
        payload.y = y;
        payload.w = w;
        payload.h = h;
    }

    struct 
    {
       uint16_t cmd;    //  2 bytes
       uint16_t onoff;  //  2 bytes
       union            // 12 bytes
       {
            uint8_t dummy1[12];
            struct 
            {
                uint16_t x;
                uint16_t y;
                uint16_t w;
                uint16_t h;
                uint32_t dummy2[1];
            }__attribute__((packed));
       };
    } payload __attribute__((packed));
};

#endif //__NV_CMD__HPP__