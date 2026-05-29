#pragma once

#include <opencv2/core.hpp>
#include <opencv2/core/cuda.hpp>
#include <opencv2/stitching/detail/seam_finders.hpp>

namespace mycv {
namespace detail {

class GraphCutSeamFinderGpu : public cv::detail::SeamFinder
{
public:
    enum CostType { COST_COLOR, COST_COLOR_GRAD };

    GraphCutSeamFinderGpu(int cost_type = COST_COLOR, float terminal_cost = 10000.f,
                          float bad_region_penalty = 1000.f);

    void find(const std::vector<cv::UMat> &src, const std::vector<cv::Point> &corners,
              std::vector<cv::UMat> &masks) override;

private:
    int cost_type_;
    float terminal_cost_;
    float bad_region_penalty_;
};

}} // namespace mycv::detail

