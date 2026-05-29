#ifndef __COMMON_HPP__
#define __COMMON_HPP__

#include <stdint.h>
#include <string.h>

#define NNN (3)         // # of packets combined from gatway. must be factor of 8694

// defines packetEO from A to B
class Packet
{
private:
    uint16_t _get_idx(void)
    {
        return ((header[0])&0x7F) << 8 | header[1];
    }

    uint8_t _get_frmchk(void)
    {
        return (header[0] & 0x80) == 0x80 ? 1 : 0;
    }

public:
    Packet()
    {
    }

    ~Packet()
    {
    }

    void from_raw(const uint8_t *raw, uint16_t len)
    {
        memcpy(&data[0], &raw[42], /*2+432*/len-42);
        idx = _get_idx();
        frmchk = _get_frmchk();
        isEO = len == 476;
    }

    union
    {
        uint8_t data[2+432];
        struct {
            uint8_t header[2];
            uint8_t pixels[432];
        };
    };

    uint16_t idx;
    uint8_t frmchk;
    bool isEO;
};

// defines packets from B to BC
class Packet3
{
private:
    uint16_t _get_idx(void)
    {
        return ((header[0])&0x7F) << 8 | header[1];
    }

    uint8_t _get_frmchk(void)
    {
        return (header[0] & 0x80) == 0x80 ? 1 : 0;
    }

public:
    Packet3()
    {
    }

    ~Packet3()
    {
    }

    void from_raw(const uint8_t *raw, uint16_t hlen)
    {
        header[0] = raw[42 + 0];
        header[1] = raw[42 + 1];
        if(hlen == 42+(2+432*NNN)+4)
        {
            id = *(uint32_t*)&raw[42+(2+432*NNN)+0];
        }
        if(unsigned(hlen-42) <= sizeof(data))
        {
            memcpy(&data[0], &raw[42], hlen-42);
        }
        idx = _get_idx();
        frmchk = _get_frmchk();
    }

    void set_idx(uint8_t frmchk, uint16_t idx)
    {
        header[0] = frmchk | ((idx & 0x7F00) >> 8) ;
        header[1] = (idx & 0x00FF) >> 0;
    }

    union
    {
        uint8_t data[2 + 432 * 3 + 4];
        struct {
            uint8_t header[2];
            uint8_t pixels[3][432];
            uint32_t id;
        } __attribute__((packed));
    };

    uint16_t idx;
    uint8_t frmchk;
};
#endif