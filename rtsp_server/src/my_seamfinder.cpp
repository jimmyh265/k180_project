#include "my_seamfinder.hpp"
#include <opencv2/core/cuda.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>

using namespace cv;
using namespace mycv::detail;

GraphCutSeamFinderGpu::GraphCutSeamFinderGpu(int cost_type, float terminal_cost,
                                             float bad_region_penalty)
    : cost_type_(cost_type),
      terminal_cost_(terminal_cost),
      bad_region_penalty_(bad_region_penalty)
{
}

void GraphCutSeamFinderGpu::find(const std::vector<UMat> &src,
                                 const std::vector<Point> &corners,
                                 std::vector<UMat> &masks)
{
    // 簡化版：先只打印 debug 訊息，未實作完整 graphcut
    std::cout << "[GraphCutSeamFinderGpu] find() called with "
              << src.size() << " images." << std::endl;

    for (size_t i = 0; i < src.size(); ++i)
    {
        // 暫時不處理 graphcut，僅保留原始 mask
        if (i < masks.size())
            masks[i] = masks[i].clone();
    }
}

