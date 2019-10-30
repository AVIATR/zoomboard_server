//
//  correct_perspective.cpp
//  zoomboard_server
//
//  Created by Ender Tekin on 8/7/19.
//

//#include "correct_perspective.hpp"
#include <vector>
#include <log4cxx/logger.h>
#ifndef NDEBUG
#include <opencv2/highgui.hpp>
#endif
#include "opencv2/imgproc.hpp"
#include "opencv2/aruco.hpp"
#include "libav2opencv.hpp"
#include "ThreadManager.hpp"
#include "ThreadsafeFrame.hpp"

extern ThreadManager g_ThreadMan;

namespace
{
    static const cv::Scalar BORDER_COLOR = cv::Scalar(0,0,255);
    static constexpr float MAX_MARKER_MOVEMENT = 16.f;

    // Initialize logger
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd.perspective"));

    /// Calculates the aspect ratio (width / height) of the board from the four corners
    /// Reference:
    /// Zhengyou Zhang & Li-wei He, "Whiteboard Scanning and Image Enhancement", Digital Signal Processing, April 2007
    /// https://www.microsoft.com/en-us/research/publication/whiteboard-scanning-image-enhancement
    /// http://dx.doi.org/10.1016/j.dsp.2006.05.006
    /// Implementation examples:
    /// https://stackoverflow.com/questions/1194352/proportions-of-a-perspective-deformed-rectangle/1222855#1222855
    /// https://stackoverflow.com/questions/38285229/calculating-aspect-ratio-of-perspective-transform-destination-image
    /// @param[in] corners the four corners of the board
    /// @param[in] imSize size of the input images (where the corners belong)
    /// @return the estimated aspect ratio
    /// @throw runtime_error if the corners do not have sufficient separation to calculate the aspect ratio
    float getAspectRatio(const std::vector<cv::Point2f>& corners, const cv::Size& imSize)
    {
        // We will assume that corners contains the corners in the order top-left, top-right, bottom-right, bottom-left
        assert(corners.size() == 4);
        for (int i = 0; i < 4; ++i)
        {
            if (cv::norm(corners[i] - corners[(i+1)%4]) < 1)
            {
                LOG4CXX_ERROR(logger, "The provided corners " << corners[i] << " and " << corners[(i+1)%4] << "are too close.")
                throw std::runtime_error("Unable to calculate aspect ratio");
            }
        }
        cv::Point2f center(imSize.width / 2.f, imSize.height / 2.f);
        cv::Point2f m1 = corners[0] - center;   //tl
        cv::Point2f m2 = corners[1] - center;   //tr
        cv::Point2f m3 = corners[3] - center;   //bl
        cv::Point2f m4 = corners[2] - center;   //br

        //TODO: Check k2 & k3 for numeric stability
        float k2 = ((m1.y-m4.y) * m3.x - (m1.x - m4.x) * m3.y + m1.x * m4.y - m1.y * m4.x) \
        / ((m2.y - m4.y) * m3.x - (m2.x - m4.x) * m3.y + m2.x * m4.y - m2.y * m4.x);

        float k3 = ((m1.y-m4.y) * m2.x - (m1.x - m4.x) * m2.y + m1.x * m4.y - m1.y * m4.x) \
        / ((m3.y - m4.y) * m2.x - (m3.x - m4.x) * m2.y + m3.x * m4.y - m3.y * m4.x);

        if ( (std::abs(k2 - 1.f) < 1E-8) || (std::abs(k3 - 1.f) < 1E-8) )
        {
            LOG4CXX_DEBUG(logger, "parallel? k2 = " << k2 << ", k3 = " << k3);
            return std::sqrt( ( std::pow(m2.y - m1.y, 2) + std::pow(m2.x - m1.x, 2) )
                             / (std::pow(m3.y - m1.y, 2) + std::pow(m3.x - m1.x, 2)) );
        }
        else
        {
            float fSqr = std::abs( ((k3 * m3.y - m1.y) * (k2 * m2.y - m1.y) + (k3 * m3.x - m1.x) * (k2 * m2.x - m1.x)) \
                                  / ((k3 - 1) * (k2 - 1)) );
            LOG4CXX_DEBUG(logger, "Calculated f^2 = " << fSqr << "[k2 = " << k2 << ", k3=" << k3 <<"]");
            return std::sqrt( ( std::pow(k2 - 1.f, 2) + (std::pow(k2 * m2.y - m1.y, 2) + std::pow(k2 * m2.x - m1.x, 2)) / fSqr )
                             / (std::pow(k3 - 1.f, 2) + (std::pow(k3 * m3.y - m1.y, 2) + std::pow(k3 * m3.x - m1.x, 2)) / fSqr ) );
        }
    }

    class BoardFinder
    {
    private:
        std::vector< std::vector<cv::Point2f> > corners_;   ///< detected corners of the markers
        std::vector<int> ids_;                              ///< id's of the detected markers
        cv::Mat cameraMatrix_;                              ///< camera matrix
        cv::Mat distCoeffs_;                                ///< distortion coefficients
        cv::Ptr<cv::aruco::Dictionary> pDict_;              ///< pointer to the dictionary of aruco markers

    public:

        BoardFinder(const std::string& calibrationFile):
        pDict_(nullptr)
        {
            corners_.reserve(16);
            ids_.reserve(4);
            cv::Mat markers;
            int markerSz=0;
            try
            {
                cv::FileStorage fs(calibrationFile, cv::FileStorage::READ);
                fs["markers"] >> markers;
                fs["marker_size"] >> markerSz;
                if ( !fs["camera_matrix"].empty() )
                {
                    LOG4CXX_DEBUG(logger, "Initializing camera matrix");
                    fs["camera_matrix"] >> cameraMatrix_;
                }
                else
                {
                    assert(cameraMatrix_.empty());
                }
                if ( !fs["distortion_coefficients"].empty() )
                {
                    fs["distortion_coefficients"] >> distCoeffs_;
                }
                else
                {
                    assert(distCoeffs_.empty());
                }
            }
            catch (std::exception& err)
            {
                std::throw_with_nested( std::runtime_error("Unable to read calibration information from " + calibrationFile) );
            }
            assert( !markers.empty() && (markerSz > 0) );
            pDict_ = cv::makePtr<cv::aruco::Dictionary>(markers, markerSz);
        }

        /// Finds and returns the corners of the aruco markers seen in the image
        /// @param[in] img input image to search for markers
        /// @return a vector of markers
        /// There should be 4 markers, and the returned vector _corners_ is always of size 4
        /// The markers are sorted such that _corners_[i] always corresponds to the i'th marker.
        /// If a marker i is not visible, than _corners_[i] is empty.
        std::vector< std::vector<cv::Point2f> > getCorners(const cv::Mat& img)
        {
            //Find markers
            corners_.clear();
            ids_.clear();
            // detect markers
            cv::aruco::detectMarkers(img, pDict_, corners_, ids_,
                                     cv::aruco::DetectorParameters::create(), cv::noArray(),
                                     cameraMatrix_.empty() ? cv::noArray() : cameraMatrix_,
                                     distCoeffs_.empty() ? cv::noArray() : distCoeffs_
                                     );

            /// Sort markers
            std::vector< std::vector<cv::Point2f> > sortedCorners(4);
            int nMarkersFound = (int) corners_.size();
            assert(nMarkersFound == ids_.size());
            for (int n = 0; n < nMarkersFound; ++n)
            {
                sortedCorners[ids_[n]] = corners_[n];
            }
            return sortedCorners;
        }
    };  // BoardFinder

    /// Returns the relevant outer corners of the markers.
    /// @param[in] corners detected marker corners
    /// Assumes that the markers are ordered in terms of id (i.e., corners[i] are the corners for marker with id i).
    /// @return the outermost corners of the markers, corresponding to the boundaries of the area to rectify
    std::vector<cv::Point2f> getOuterCorners(const std::vector< std::vector<cv::Point2f> >& corners)
    {
        std::vector<cv::Point2f> outerCorners(4);
        assert(4 == corners.size());
        for (int i = 0; i < 4; ++i)
        {
            if (corners[i].empty()) //this marker was not found
            {
                return std::vector<cv::Point2f>();  //return empty matrix
            }
            int c = i < 2 ? i : 5-i;
            outerCorners[c]= (corners[i][c]);
        }
        assert(outerCorners.size() == 4);
        return outerCorners;
    }

    /// Uses Aruco markers on the four corners of the board to calculate the  perspective transform.
    /// This can then be used with cv::warpPerspective to correct the perspective of the video.
    /// also @see https://docs.opencv.org/3.1.0/da/d54/group__imgproc__transform.html
    /// @param[in] pFrame input frame that will be updated by the reader thread
    /// @param[in] calibrationFile calibration file that contains info re: camera calibration and aruco markers
    /// @return a perspective transformation matrix
    cv::Mat_<double> getPerspectiveTransformationMatrix(const std::vector<cv::Point2f>& corners, const cv::Size& imgSize)
    {
        cv::Mat_<double> trfMatrix;
        std::vector<cv::Point2f> targetCorners;
        assert(imgSize.height > 0);
        const float IMG_ASPECT = (float) imgSize.width / (float) imgSize.height;
        assert( 4 == corners.size() );
        //Calculate aspect ratio:
        float aspect = getAspectRatio(corners, imgSize);
        LOG4CXX_DEBUG(logger, "Calculated aspect ratio is: " << aspect);
        assert(aspect > 0.f);
        if (aspect > IMG_ASPECT)
        {
            float h = (float) imgSize.width / aspect;
            targetCorners = {
                cv::Point2f(0.f,            0.5f * (imgSize.height - h)),
                cv::Point2f(imgSize.width,  0.5f * (imgSize.height - h)),
                cv::Point2f(imgSize.width,  0.5f * (imgSize.height + h)),
                cv::Point2f(0.f,            0.5f * (imgSize.height + h))
            };
        }
        else
        {
            float w = (float) imgSize.height * aspect;
            targetCorners = {
                cv::Point2f(0.5f * (imgSize.width - w), 0.f),
                cv::Point2f(0.5f * (imgSize.width + w), 0.f),
                cv::Point2f(0.5f * (imgSize.width + w), imgSize.height),
                cv::Point2f(0.5f * (imgSize.width - w), imgSize.height)
            };
        }

        trfMatrix = cv::getPerspectiveTransform(corners, targetCorners);

#ifndef NDEBUG
        LOG4CXX_DEBUG(logger, "Need to transform \n" << corners << " to \n" << targetCorners);
        auto warpPt = [](const cv::Point2f& pt, cv::Mat_<float> trfMat)
        {
            float scaleFactor = trfMat[2][0] * pt.x + trfMat[2][1] * pt.y + trfMat[2][2];
            return cv::Point2f(
                               (trfMat[0][0] * pt.x + trfMat[0][1] * pt.y + trfMat[0][2]) / scaleFactor,
                               (trfMat[1][0] * pt.x + trfMat[1][1] * pt.y + trfMat[1][2]) / scaleFactor
                               );
        };
        LOG4CXX_DEBUG(logger, "Calculated matrix \n" << trfMatrix << " will transform "
                      << corners[0] << " -> " << warpPt(corners[0], trfMatrix) << ", "
                      << corners[1] << " -> " << warpPt(corners[1], trfMatrix) << ","
                      << corners[2] << " -> " << warpPt(corners[2], trfMatrix) << ","
                      << corners[3] << " -> " << warpPt(corners[3], trfMatrix) << "."
                      );
#endif
        return trfMatrix;
    }

    /// Calculates the approximate movement of the markers. If this is above a threshold, we should try to re-calculate the perspective transform
    /// @param[in] prevCorners previous locations of the marker corners
    /// @param[in] corners most recently seen corners
    /// @return normalized approximate motion in pixels
    float calculateMarkerMovement(const std::vector< std::vector<cv::Point2f> >& prevCorners, const std::vector< std::vector<cv::Point2f> >& corners)
    {
        float motion = 0.f;
        assert( (prevCorners.size() == 4) && (corners.size() == 4) );
        int nMarkers = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (!prevCorners[i].empty() && !corners[i].empty())
            {
                ++nMarkers;
                assert ((prevCorners[i].size() == 4) && (corners[i].size() == 4));
                for (int j = 0; j < 4; ++j)
                {
                    motion += 0.25f * (cv::norm(corners[i][j] - prevCorners[i][j]));
                }
            }
        }
        motion = (nMarkers > 0 ? motion / nMarkers : FLT_MAX); //normalize motion per matching point
        LOG4CXX_DEBUG(logger, "Calculated motion: " << motion);
        return motion;
    }
} //::<anon>


std::thread threadedWarp(std::weak_ptr<const avtools::ThreadsafeFrame> pInFrame, std::weak_ptr<avtools::ThreadsafeFrame> pWarpedFrame, const std::string& calibrationFile)
{
    return std::thread([pInFrame, pWarpedFrame, calibrationFile](){
        try
        {
            log4cxx::MDC::put("threadname", "warper");
            // Read board information & create board finder
            BoardFinder boardFinder(calibrationFile);
            std::vector< std::vector<cv::Point2f> > prevCorners(4);    //previously detected marker corners
            cv::Mat_<double> trfMatrix; //perspective transform matrix
            // Start the loop - every frame gets checked for markers
            // If all markers are visible, a new perspective transform is calculated.
            // If not all markers are visible, but the detected ones are in approximately the same location as before, then the previously calculated transform is used.
            // If the detected markers are in different locations than before, then no transform is applied
            avtools::TimeType ts = AV_NOPTS_VALUE;
            while (!g_ThreadMan.isEnded())
            {
                auto ppInFrame = pInFrame.lock();
                if (!ppInFrame)
                {
                    if (g_ThreadMan.isEnded())
                    {
                        break;
                    }
                    throw std::runtime_error("Warper received null frame.");
                }
                const auto& inFrame = *ppInFrame;
                {
                    auto rLock = inFrame.getReadLock();
                    inFrame.cv.wait(rLock, [&inFrame, ts](){return g_ThreadMan.isEnded() ||  (inFrame->best_effort_timestamp > ts);});    //wait until fresh frame is available
                    if (g_ThreadMan.isEnded())  //if the wait ended because program ended, quit
                    {
                        break;
                    }
                    ts = inFrame->best_effort_timestamp;
                    auto ppWarpedFrame = pWarpedFrame.lock();
                    if (!ppWarpedFrame )
                    {
                        throw std::runtime_error("Warper output frame is null");
                    }
                    auto& warpedFrame = *ppWarpedFrame;
                    {
                        auto wLock = warpedFrame.getWriteLock();
                        assert(warpedFrame->best_effort_timestamp < ts);
                        assert( (av_cmp_q(warpedFrame.timebase, inFrame.timebase) == 0) && (warpedFrame.type == AVMediaType::AVMEDIA_TYPE_VIDEO) && (inFrame.type == AVMediaType::AVMEDIA_TYPE_VIDEO) );
                        cv::Mat inImg = getImage(inFrame);
                        //Look for markers in this frame
                        auto corners = boardFinder.getCorners(inImg);
                        // See if corners have moved since last time
                        if (calculateMarkerMovement(prevCorners, corners) > MAX_MARKER_MOVEMENT)
                        {
                            auto boundary = getOuterCorners(corners);
                            if (boundary.empty())   // do not have all markers visible to calculate trf matrix
                            {
                                trfMatrix = cv::Mat_<double>();
                            }
                            else
                            {
                                trfMatrix = getPerspectiveTransformationMatrix(boundary, inImg.size());
                                prevCorners = corners;
                            }
                        }

                        cv::Mat outImg = getImage(warpedFrame);
                        if (trfMatrix.empty())
                        {
                            inImg.copyTo(outImg);
                        }
                        else
                        {
                            LOG4CXX_DEBUG(logger, "Warper using transformation matrix: " << trfMatrix );
                            cv::warpPerspective(inImg, outImg, trfMatrix, outImg.size(), cv::InterpolationFlags::INTER_LANCZOS4);
                        }
                        int ret = av_frame_copy_props(warpedFrame.get(), inFrame.get());
                        if (ret < 0)
                        {
                            throw avtools::MediaError("Unable to copy frame properties", ret);
                        }
                        LOG4CXX_DEBUG(logger, "Warped frame info: \n" << warpedFrame.info(1));
                    }
                    warpedFrame.cv.notify_all();    //need to call this manually, normally update() would cll this
                }
            }
        }
        catch (std::exception& err)
        {
            try
            {
                std::throw_with_nested( std::runtime_error("Warper thread error") );
            }
            catch (...)
            {
                g_ThreadMan.addException(std::current_exception());
                g_ThreadMan.end();
            }
        }
        LOG4CXX_DEBUG(logger, "Exiting thread: isEnded=" << std::boolalpha << g_ThreadMan.isEnded());
    });
}
