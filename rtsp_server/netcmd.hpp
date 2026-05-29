#ifndef __NETCMD_HPP__
#define __NETCMD_HPP__
// https://docs.google.com/spreadsheets/d/1-g68I0IAczEkQdfPpCTjipyjwvMdb_fQD1h4GlJKCX0/edit#gid=204709773
// https://docs.google.com/spreadsheets/d/1XqeDaL8tAHm301-EY6tS0j9lKARQW-OudqbbN2BAtGo/edit#gid=1120235129

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <fmt/format.h>

#define DEBUG       (0)
#if DEBUG == 1
#define DBG(format, ...)       fmt::print("[{}]: " format, __func__, __VA_ARGS__)
#else
#define DBG(format, ...)       do{}while(0)
#endif
#define MSK_TRACK_TAR_ON    (0x00000001)

class Netcmd
{
    static const int OUT_PORT = 50003;
    static const int IN_PORT = 50002;

private:
    std::string m_dst_ip;
    int m_sock_tx, m_sock_rx;
    struct sockaddr_in m_server_addr;

    uint8_t GetCheckSum(std::vector<uint8_t> bytes, int start = -1, int end = -1)
    {
        uint8_t checksum = 0x00;

        if (start == -1) start = 0;
        if (end == -1) end = bytes.size();

        for (int i = start; i < end; i++)
        {
            checksum += bytes[i];
        }
        return checksum;
    }

    uint8_t GetCheckSum(uint8_t *bytes, uint32_t len)
    {
        uint8_t checksum = 0x00;

        for (uint32_t i = 0; i < len; i++)
        {
            checksum += bytes[i];
            // fmt::print("{:02X}, chksum={:02X}\n", bytes[i], checksum);
        }
        return checksum;
    }

    std::vector<uint8_t> CreateCmd(std::vector<uint8_t> cmd)
    {
        int dataLength = cmd.size() + (3+2+1);  // 3: 0xE0, 2: len, 1: chksum
        std::vector<uint8_t> data(cmd.size() + (3+2+1));

        // header: 3 bytes
        data[0] = 0xE0;
        data[1] = 0xE0;
        data[2] = 0xE0;

        // length: 2bytes
        data[4] = (uint8_t)((dataLength >> 8) & 0xFF);
        data[3] = (uint8_t)(dataLength & 0xFF);

        // body
        // cmd.CopyTo(data, 5);
        for(size_t i = 0; i < cmd.size(); i++)
        {
            data[5+i] = cmd[i];
        }

        // checksum
        data[data.size() - 1] = GetCheckSum(data, 0, data.size() - 1);
        return data;
    }

public:
    Netcmd(std::string dst_ip): m_dst_ip(dst_ip)
    {
        int yes = 1;
        struct sockaddr_in server_addr = {0}, myaddr = {0};
        struct hostent *host = (struct hostent *) gethostbyname((char *)m_dst_ip.c_str());

        // init m_sock_tx
        if ((m_sock_tx = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
        {
            perror("m_sock_tx");
            exit(1);
        }

        memset(&m_server_addr, 0x00, sizeof(m_server_addr));
        m_server_addr.sin_family = AF_INET;
        m_server_addr.sin_port = htons(OUT_PORT);
        m_server_addr.sin_addr = *((struct in_addr *)host->h_addr);

        if (bind(m_sock_tx,(struct sockaddr *)&server_addr, sizeof(struct sockaddr)) < 0)
        {
            perror("m_sock_tx bind");
            exit(1);
        }

        // init m_sock_rx
        if ((m_sock_rx = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
        {
            perror("m_sock_rx");
            exit(1);
        }

        myaddr.sin_family = AF_INET;
        myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        myaddr.sin_port = htons(IN_PORT);

        if (bind(m_sock_rx, (struct sockaddr *)&myaddr, sizeof(struct sockaddr)) < 0) {
            perror ("m_sock_rx bind\n");
            exit(1);
        }

        if (setsockopt(m_sock_rx, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) 
        {
            perror ("m_sock_rx setsockopt SO_REUSEADDR\n");
            exit(1);
        }

        if (setsockopt(m_sock_rx, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) < 0) 
        {
            perror ("m_sock_rx setsockopt SO_REUSEPORT\n");
            exit(1);
        }

        #if 0   // block mode: set timeout
        struct timeval timeout; 
        timeout.tv_sec = 0;
        timeout.tv_usec = 1 * 1000;    // 1 ms
        setsockopt(m_sock_rx, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(struct timeval));
        #else
        fcntl(m_sock_rx, F_SETFL, O_NONBLOCK); 
        #endif
    }

    ~Netcmd()
    {

    }

    void send1208(bool tracking_onoff)
    {
        int ret __attribute__((unused));
        std::vector<uint8_t> cmd = {0x08, 0x12, 0x06, 0x00, 0x00, 0x00};    // big endian, 0x1208, 0x0006, 0x000x
        std::vector<uint8_t> packet;

        if(tracking_onoff)
        {
            cmd[4] |= MSK_TRACK_TAR_ON;
        }

        packet = CreateCmd(cmd);

        ret = sendto(m_sock_tx, packet.data(), sizeof(packet[0])*packet.size(), 0, (struct sockaddr *)&m_server_addr, sizeof(struct sockaddr));
        DBG("ret={}, len={}, onoff={}\n", ret,  sizeof(packet[0])*packet.size(), tracking_onoff);
    }

    void send1209(uint16_t x, uint16_t y, bool isEO)
    {
        int ret __attribute__((unused));
        std::vector<uint8_t> cmd = {0x09, 0x12, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00};    // big endian, 0x1209, 0x0008, X, Y
        std::vector<uint8_t> packet;

        x = x>1279 ? 1279 : x;
        y = y>959  ? 959  : y;

        if(isEO)
        {
            x = x * 0.5;
            y = y * 0.5;
        }

        cmd[4] = (x >> 0) & 0xFF;
        cmd[5] = (x >> 8) & 0xFF;
        cmd[6] = (y >> 0) & 0xFF;
        cmd[7] = (y >> 8) & 0xFF;

        packet = CreateCmd(cmd);
        #if DEBUG == 1
        fmt::print("[{}] DUMP 1209 packet:\n", __func__);
        for(size_t i = 0; i < packet.size(); i++)
        {
            fmt::print("0x{:02X}, ", packet[i]);
            if(((i+1)%8) == 0) fmt::print("\n");
        }
        fmt::print("\n");
        #endif
        ret = sendto(m_sock_tx, packet.data(), sizeof(packet[0])*packet.size(), 0, (struct sockaddr *)&m_server_addr, sizeof(struct sockaddr));

        DBG("ret={}, len={}, x,y=({:4},{:4})\n", ret,  sizeof(packet[0])*packet.size(), x, y);
    }

    int recv21AA()
    {
        int nbytes, ret = -1;
        struct sockaddr_in client_addr;
        socklen_t rxlen = sizeof(client_addr);
        uint8_t rxbuf[128];

        if ((nbytes = recvfrom(m_sock_rx, rxbuf, sizeof(rxbuf), 0, (struct sockaddr*)&client_addr, &rxlen)) <0)
        {
            // perror ("could not read datagram!!");
        }

        #if DEBUG == 1
        if(nbytes > 0)
        {
            fmt::print("[{}] DUMP rxbuf:\n", __func__);
            for(int i = 0; i < nbytes; i++)
            {
                fmt::print("0x{:02X}, ", rxbuf[i]);
                if(((i+1)%8) == 0) fmt::print("\n");
            }
            fmt::print("cksum = {:02X}\n", GetCheckSum(rxbuf, nbytes-1));
        }
        #endif

        if((nbytes == 46) && (rxbuf[5] == 0xAA) && (rxbuf[6] == 0x21) && (GetCheckSum(rxbuf, nbytes-1) == rxbuf[45]))
        {
            ret = 0;
        }

        DBG("nbytes={}, ret={}\n", nbytes, ret);
        return ret;
    }
};



#endif
