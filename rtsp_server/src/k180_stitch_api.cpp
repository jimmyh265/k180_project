#include "k180_stitch_api.h"
// #define LOGLN(msg)  
// #define LOG(msg) std::cout << msg
// #define LOGLN(msg) std::cout << msg << std::endl
#define LOG(msg)  
#define LOGLN(msg)  

using namespace std;
using namespace cv;
using namespace cv::detail;
int64 t;
namespace k180::runtime::stitchapi {
	
void saveMasks(const vector<UMat>& masks_warped, const string& folder_path) {
    for (size_t i = 0; i < masks_warped.size(); ++i) {
        string filename = folder_path + "/mask_" + to_string(i) + ".png";
        Mat tmp;
        masks_warped[i].copyTo(tmp); 
        imwrite(filename, tmp);
    }
}

vector<Mat> loadMasks(const string& folder_path, size_t num_images) {
    vector<Mat> loaded_masks;
    for (size_t i = 0; i < num_images; ++i) {
        string filename = folder_path + "/mask_" + to_string(i) + ".png";
        Mat mask;
        imread(filename, IMREAD_GRAYSCALE).copyTo(mask);
        loaded_masks.push_back(mask);
    }
    return loaded_masks;
}

// ---------- 儲存與讀取 vector<Point> ----------
void saveCorners(const std::string& filename, const std::vector<cv::Point>& corners) {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    fs << "corners" << corners;
    fs.release();
}

void loadCorners(const std::string& filename, std::vector<cv::Point>& corners) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    fs["corners"] >> corners;
    fs.release();
}

// ---------- 儲存與讀取 vector<Size> ----------
void saveSizes(const std::string& filename, const std::vector<cv::Size>& sizes) {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    fs << "sizes" << sizes;
    fs.release();
}

void loadSizes(const std::string& filename, std::vector<cv::Size>& sizes) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    fs["sizes"] >> sizes;
    fs.release();
}

void saveWarpedImageScale(const std::string& filename, float warped_image_scale) {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    fs << "warped_image_scale" << warped_image_scale;
    fs.release();
}

void loadWarpedImageScale(const std::string& filename, float& warped_image_scale) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    fs["warped_image_scale"] >> warped_image_scale;
    fs.release();
}

void saveCameraParams(const std::string& folder, const std::vector<CameraParams>& cameras) {
    char filename[256];
    for (size_t i = 0; i < cameras.size(); ++i) {
        sprintf(filename, "%s/camera_%zu.yml", folder.c_str(), i);
        FileStorage fs(filename, FileStorage::WRITE);
        fs << "K" << cameras[i].K();
        fs << "R" << cameras[i].R;
        fs << "t" << cameras[i].t;
        fs << "ppx" << cameras[i].ppx;
        fs << "ppy" << cameras[i].ppy;
        fs << "focal" << cameras[i].focal;
        fs << "aspect" << cameras[i].aspect;
        fs.release();
    }
}

void loadCameraParams(const std::string& folder, std::vector<CameraParams>& cameras, int num_images) {
    cameras.resize(num_images);
    char filename[256];
    for (int i = 0; i < num_images; ++i) {
        sprintf(filename, "%s/camera_%d.yml", folder.c_str(), i);
        FileStorage fs(filename, FileStorage::READ);

        Mat K, R, t;
        double ppx, ppy, focal, aspect;

        fs["K"] >> K;
        fs["R"] >> R;
        fs["t"] >> t;
        fs["ppx"] >> ppx;
        fs["ppy"] >> ppy;
        fs["focal"] >> focal;
        fs["aspect"] >> aspect;

        cameras[i].K() = K;
        cameras[i].R = R;
        cameras[i].t = t;
        cameras[i].ppx = ppx;
        cameras[i].ppy = ppy;
        cameras[i].focal = focal;
        cameras[i].aspect = aspect;
		// cameras[img_idx].K() = (Mat)KK;
		// cameras[img_idx].R = RR;
		// cameras[img_idx].t = TT;
		// cameras[img_idx].ppx = (double)ppx;
		// cameras[img_idx].ppy = (double)ppy;
		// cameras[img_idx].focal = (double)focal;
		// cameras[img_idx].aspect = (double)aspect;
        fs.release();
    }
}

vector<ImageFeatures> computeFeaturesFromImages(const std::vector<cv::Mat>& images) {	

	Mat full_img, img;
	auto& s = k180::runtime::rt().sdp;
	int num_images = static_cast<int>(images.size());
	// cout << "ss  " << num_images << endl;
    LOGLN("Finding features...");
#if ENABLE_LOG
    t = getTickCount();
#endif
    Ptr<Feature2D> finder;
    if (s.features_type == "orb")
    {
        finder = ORB::create();
    }
    else if (s.features_type == "akaze")
    {
        finder = AKAZE::create();
    }
// #ifdef HAVE_OPENCV_XFEATURES2D
    // else if (s.features_type == "surf")
    // {
        // finder = xfeatures2d::SURF::create();
    // }
// #endif
    else if (s.features_type == "sift")
    {
		// int max_features = 2000;
        // finder = SIFT::create(max_features);
        finder = SIFT::create();
    }
	
	vector<Mat> full_img_with_keypoints(num_images);
	vector<ImageFeatures> features(num_images);

    for (int i = 0; i < num_images; ++i)
    {
        full_img = images[i];
        // full_img_sizes[i] = full_img.size();
        if (full_img.empty())
        {
            // LOGLN("Can't open image " << img_names[i]);
            LOGLN("Can't open image " );
        }
		resize(full_img, img, Size(), s.work_scale, s.work_scale, INTER_LINEAR_EXACT);
        
		// /*
		if( i == 0){
			// cv::Rect right_half_rect(img.cols / 4, 0, img.cols - (img.cols / 4), img.rows);
			cv::Rect left_half_rect(0, 0, img.cols - (img.cols / 4), img.rows);
			img(left_half_rect) = cv::Scalar(0, 0, 0);
		} else { // i==1
			cv::Rect right_half_rect(img.cols / 4, 0, img.cols - (img.cols / 4), img.rows);
			// cv::Rect left_half_rect(0, 0, img.cols - (img.cols / 4), img.rows);
			img(right_half_rect) = cv::Scalar(0, 0, 0);
		}
		// */
        computeImageFeatures(finder, img, features[i]);
        features[i].img_idx = i;
        LOGLN("Features in image #" << i + 1 << ": " << features[i].keypoints.size());
/*
		if( i == 0){
			cv::drawKeypoints(img, features[i].keypoints, full_img_with_keypoints[i], cv::Scalar(0, 255, 0), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
			imwrite( "f0.jpg", full_img_with_keypoints[i]);
		} else {
			cv::drawKeypoints(img, features[i].keypoints, full_img_with_keypoints[i], cv::Scalar(0, 255, 0), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
			imwrite( "f1.jpg", full_img_with_keypoints[i]);
		}
		*/
    }
	LOGLN("Finding features, time: " << ((getTickCount() - t) / getTickFrequency()) << " sec");
    return features;
}

vector<MatchesInfo> computepairwisematches(vector<ImageFeatures>& features) {	
	
	auto& s = k180::runtime::rt().sdp;
	LOG("Pairwise matching");
#if ENABLE_LOG
    t = getTickCount();
#endif
    vector<MatchesInfo> pairwise_matches;
    Ptr<FeaturesMatcher> matcher;
    if (s.matcher_type == "affine")
        matcher = makePtr<AffineBestOf2NearestMatcher>(false, s.try_cuda, s.match_conf);
    else if (s.range_width == -1)
        matcher = makePtr<BestOf2NearestMatcher>(s.try_cuda, s.match_conf);
    else
        matcher = makePtr<BestOf2NearestRangeMatcher>(s.range_width, s.try_cuda, s.match_conf);
    (*matcher)(features, pairwise_matches);
    matcher->collectGarbage();
	LOGLN("Pairwise matching, time: " << ((getTickCount() - t) / getTickFrequency()) << " sec");
	return pairwise_matches;
}

void estimatorCamera( vector<ImageFeatures>& features, 
							vector<MatchesInfo>& pairwise_matches,
							vector<CameraParams>& cameras ){
	auto& s = k180::runtime::rt().sdp;
	LOG("estimatorCamera");
#if ENABLE_LOG
    t = getTickCount();
#endif
    Ptr<Estimator> estimator;
    if (s.estimator_type == "affine")
        estimator = makePtr<AffineBasedEstimator>();
    else
        estimator = makePtr<HomographyBasedEstimator>();

    if (!(*estimator)(features, pairwise_matches, cameras))
    {
        cout << "Homography estimation failed.\n";
        return ;
    }
    for (size_t i = 0; i < cameras.size(); ++i)
    {
        Mat R;
        cameras[i].R.convertTo(R, CV_32F);
        cameras[i].R = R;
        LOGLN("Initial camera intrinsics #" << i + 1 << ":\nK:\n"
                                            << cameras[i].K() << "\nR:\n"
                                            << cameras[i].R);
		LOGLN("focal #" << cameras[i].focal );
    }
	LOGLN("estimatorCamera, time: " << ((getTickCount() - t) / getTickFrequency()) << " sec");
}

bool adjusterCamera( vector<ImageFeatures>& features, 
					vector<MatchesInfo>& pairwise_matches,
					vector<CameraParams>& cameras,
					float& warped_image_scale){
	auto& s = k180::runtime::rt().sdp;
	LOG("adjusterCamera");
#if ENABLE_LOG
    t = getTickCount();
#endif
    Ptr<detail::BundleAdjusterBase> adjuster;
    if (s.ba_cost_func == "reproj")
        adjuster = makePtr<detail::BundleAdjusterReproj>();
    else if (s.ba_cost_func == "ray")
        adjuster = makePtr<detail::BundleAdjusterRay>();
    else if (s.ba_cost_func == "affine")
        adjuster = makePtr<detail::BundleAdjusterAffinePartial>();
    else if (s.ba_cost_func == "no")
        adjuster = makePtr<NoBundleAdjuster>();
    else
    {
        cout << "Unknown bundle adjustment cost function: '" << s.ba_cost_func << "'.\n";
        return false;
    }
    adjuster->setConfThresh(s.conf_thresh);
    Mat_<uchar> refine_mask = Mat::zeros(3, 3, CV_8U);
    if (s.ba_refine_mask[0] == 'x')
        refine_mask(0, 0) = 1;
    if (s.ba_refine_mask[1] == 'x')
        refine_mask(0, 1) = 1;
    if (s.ba_refine_mask[2] == 'x')
        refine_mask(0, 2) = 1;
    if (s.ba_refine_mask[3] == 'x')
        refine_mask(1, 1) = 1;
    if (s.ba_refine_mask[4] == 'x')
        refine_mask(1, 2) = 1;
    adjuster->setRefinementMask(refine_mask);

	try {
		(*adjuster)(features, pairwise_matches, cameras);
	} catch (const cv::Exception& e) {
		LOGLN("Bundle adjustment failed: " << e.what() << std::endl);
		return false;
	}

    // Find median focal length
    vector<double> focals;
    for (size_t i = 0; i < cameras.size(); ++i)
    {
        LOGLN("Camera #" << i + 1 << ":\nK:\n"
                         << cameras[i].K() << "\nR:\n"
                         << cameras[i].R);
        focals.push_back(cameras[i].focal);
		LOGLN("focal #" << cameras[i].focal );
    }
    sort(focals.begin(), focals.end());
    
    if (focals.size() % 2 == 1){
        warped_image_scale = static_cast<float>(focals[focals.size() / 2]);
	}
    else {
        warped_image_scale = static_cast<float>(focals[focals.size() / 2 - 1] + focals[focals.size() / 2]) * 0.5f;
	}
	
	LOGLN("warped_image_scale # " <<  (warped_image_scale));
	LOGLN("adjusterCamera, time: " << ((getTickCount() - t) / getTickFrequency()) << " sec");
	return true;
}

}