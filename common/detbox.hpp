#ifndef __DETBOX_H_INCLUDE__
#define __DETBOX_H_INCLUDE__

#include <vector>

#define MAX_DETBOX  (512)
#define DETBOX_PORT (7777)


struct alignas(float) DetectionR
{
    // same as Detection
    float bbox[4];
    float conf;  
    float class_id;
};

class DetboxInfo
{
public:
    uint32_t id;
    uint16_t len;
    std::vector<DetectionR> detbox;
};

#endif  //__DETBOX_H_INCLUDE__