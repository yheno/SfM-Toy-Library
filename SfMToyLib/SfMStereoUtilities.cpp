/*
 * SfMStereoUtilities.cpp
 *
 *  Created on: May 27, 2016
 *      Author: roy_shilkrot
 * 
 *  Copyright @  2016
 */

#include "SfMStereoUtilities.h"

#include <iostream>

#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/imgproc/imgproc.hpp>

using namespace cv;
using namespace std;

namespace sfmtoylib {

const double RANSAC_THRESHOLD = 2.5f; // RANSAC inlier threshold

SfMStereoUtilities::SfMStereoUtilities() {

}

SfMStereoUtilities::~SfMStereoUtilities() {
}

int SfMStereoUtilities::findHomographyInliers(
        const Features& left,
        const Features& right,
        const Matching& matches) {

    Features alignedLeft;
    Features alignedRight;
    GetAlignedPointsFromMatch(left, right, matches, alignedLeft, alignedRight);

    Mat inlierMask;
    Mat homography;
    if(matches.size() >= 4) {
        homography = findHomography(alignedLeft.points, alignedRight.points,
                                    cv::RANSAC, RANSAC_THRESHOLD, inlierMask);
    }

    if(matches.size() < 4 || homography.empty()) {
        return 0;
    }

    return countNonZero(inlierMask);
}

bool SfMStereoUtilities::findCameraMatricesFromMatch(
        const Intrinsics&   intrinsics,
        const Matching&     matches,
        const Features&     featuresLeft,
        const Features&     featuresRight,
        Features&           prunedLeft,
        Features&           prunedRight,
        cv::Matx34f&        Pleft,
        cv::Matx34f&        Pright) {

    if (intrinsics.K.empty()) {
        cerr << "Intrinsics matrix (K) must be initialized." << endl;
        return false;
    }

    double focal = intrinsics.K.at<float>(0, 0); //Note: assuming fx = fy
    cv::Point2d pp(intrinsics.K.at<float>(0, 2), intrinsics.K.at<float>(1, 2));

    Features alignedLeft;
    Features alignedRight;
    GetAlignedPointsFromMatch(featuresLeft, featuresRight, matches, alignedLeft, alignedRight);

    Mat E, R, t, mask;
    E = findEssentialMat(alignedLeft.points, alignedRight.points, focal, pp, RANSAC, 0.999, 1.0, mask);
    recoverPose(E, alignedLeft.points, alignedRight.points, R, t, focal, pp, mask);

    //TODO: stratify over Pleft
    Pleft = Matx34f::eye();
    Pright = Matx34f(R.at<double>(0,0), R.at<double>(0,1), R.at<double>(0,2), t.at<double>(0),
                     R.at<double>(1,0), R.at<double>(1,1), R.at<double>(1,2), t.at<double>(1),
                     R.at<double>(2,0), R.at<double>(2,1), R.at<double>(2,2), t.at<double>(2));

    //populate pruned points
    PruneFeaturesWithMask(alignedLeft,  mask, prunedLeft);
    PruneFeaturesWithMask(alignedRight, mask, prunedRight);

    return true;
}

bool SfMStereoUtilities::triangulateViews(
        const Intrinsics&  intrinsics,
        const ImagePair    imagePair,
        const Matching&    matches,
        const Features&    featuresLeft,
        const Features&    featuresRight,
        const cv::Matx34f& Pleft,
        const cv::Matx34f& Pright,
        PointCloud&        pointCloud) {

    //get aligned features left-right, with back reference to original indexing
    vector<int> leftBackReference;
    vector<int> rightBackReference;
    Features alignedLeft;
    Features alignedRight;
    GetAlignedPointsFromMatch(
            featuresLeft,
            featuresRight,
            matches,
            alignedLeft,
            alignedRight,
            leftBackReference,
            rightBackReference);

    Mat normalizedLeftPts;
    Mat normalizedRightPts;
    undistortPoints(alignedLeft.points,  normalizedLeftPts,  intrinsics.K, Mat());
    undistortPoints(alignedRight.points, normalizedRightPts, intrinsics.K, Mat());

    Mat points3dHomogeneous;
    triangulatePoints(Pleft, Pright, normalizedLeftPts, normalizedRightPts, points3dHomogeneous);

    Mat points3d;
    convertPointsFromHomogeneous(points3dHomogeneous.t(), points3d);

    //todo: cheirality check (all points z > 0)

    for (size_t i = 0; i < points3d.rows; i++) {
        Point3DInMap p;
        p.p = Point3f(points3d.at<float>(i, 0),
                      points3d.at<float>(i, 1),
                      points3d.at<float>(i, 2)
                      );

        //use back reference to point to original features in images
        p.originatingViews[imagePair.left]  = leftBackReference [i];
        p.originatingViews[imagePair.right] = rightBackReference[i];

        pointCloud.push_back(p);
    }

    return true;
}

bool SfMStereoUtilities::findCameraPoseFrom2D3DMatch(
        const Intrinsics& intrinsics,
        const Image2D3DMatch& match,
        cv::Matx34f& cameraPose) {

    //Recover camera pose using 2D-3D correspondence
    Mat rvec, tvec;
    Mat inliers;
    solvePnPRansac(
             match.points3D,
             match.points2D,
             intrinsics.K,
             intrinsics.distortion,
             rvec,
             tvec,
             false,
             100,
             RANSAC_THRESHOLD,
             0.99,
             inliers
             );

    //check inliers ratio and reject if too small
    if (((float)countNonZero(inliers) / (float)match.points2D.size()) < POSE_INLIERS_MINIMAL_RATIO) {
        cerr << "Inliers ratio is too small: " << countNonZero(inliers) << " / " << match.points2D.size() << endl;
        return false;
    }

    Mat rotMat;
    Rodrigues(rvec, rotMat); //convert to a rotation matrix

    rotMat.copyTo(Mat(3, 4, CV_32FC1, cameraPose.val)(ROT));
    tvec.copyTo(Mat(3, 4, CV_32FC1, cameraPose.val)(TRA));

    return true;
}

} /* namespace sfmtoylib */
