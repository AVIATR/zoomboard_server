//
//  correct_perspective.cpp
//  zoomboard_server
//
//  Created by Ender Tekin on 8/7/19.
//

#include "correct_perspective.hpp"
#include <vector>
#include <log4cxx/logger.h>
#include <opencv2/highgui.hpp>
#include "opencv2/imgproc.hpp"
#include "opencv2/aruco.hpp"
#include "libav2opencv.hpp"
#include "ThreadManager.hpp"

extern ThreadManager g_ThreadMan;

namespace
{
    static const std::string INPUT_WINDOW_NAME = "Input", OUTPUT_WINDOW_NAME = "Warped";
    static const cv::Scalar FIXED_COLOR = cv::Scalar(0,0,255), DRAGGED_COLOR = cv::Scalar(255, 0, 0);

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
    float getAspectRatio(std::vector<cv::Point2f>& corners, const cv::Size& imSize)
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

    class UserDirectedBoardFinder
    {
    private:
        int draggedPt_;                      ///< index of the point currently being dragged. -1 if none
        std::vector<cv::Point2f> corners_;   ///< Currently selected coordinates of the corners

        /// Mouse callback for the window, replacing cv::MouseCallback
        static void OnMouse(int event, int x, int y, int flags, void *userdata)
        {
            UserDirectedBoardFinder* pC = static_cast<UserDirectedBoardFinder*>(userdata);
            assert(pC->draggedPt_ < (int) pC->corners_.size());
            cv::Point2f curPt(x,y);
            switch(event)
            {
                case cv::MouseEventTypes::EVENT_LBUTTONDOWN:
                    //See if we are near any existing markers
                    assert(pC->draggedPt_ < 0);
                    for (int i = 0; i < pC->corners_.size(); ++i)
                    {
                        if (cv::norm(curPt - pC->corners_[i]) < 10.f)
                        {
                            pC->draggedPt_ = i;  //start dragging
                            break;
                        }
                    }
                    break;
                case cv::MouseEventTypes::EVENT_MOUSEMOVE:
                    if ((flags & cv::MouseEventFlags::EVENT_FLAG_LBUTTON) && (pC->draggedPt_ >= 0))
                    {
                        pC->corners_[pC->draggedPt_] = curPt;
                    }
                    break;
                case cv::MouseEventTypes::EVENT_LBUTTONUP:
                    if (pC->draggedPt_ >= 0)
                    {
                        pC->corners_[pC->draggedPt_] = curPt;
                        pC->draggedPt_ = -1; //end dragging
                    }
                    else if (pC->corners_.size() < 4)
                    {
                        pC->corners_.emplace_back(x,y);
                    }
                    break;
                default:
                    break;
            }
            LOG4CXX_DEBUG(logger, "Marked " << pC->corners_.size() << " points.");
        }
    public:

        /// Ctor
        UserDirectedBoardFinder():
        draggedPt_(-1)
        {
            cv::setMouseCallback(INPUT_WINDOW_NAME, OnMouse, this);
        }

        std::vector<cv::Point2f> getCorners(const cv::Mat& img)
        {
            return corners_;
        }

        void draw(cv::Mat& img)
        {
            for (int i = 0; i < corners_.size(); ++i)
            {
                auto color = (draggedPt_ == i ? DRAGGED_COLOR : FIXED_COLOR);
                cv::drawMarker(img, corners_[i], color, cv::MarkerTypes::MARKER_SQUARE, 5);
                cv::putText(img, std::to_string(i+1), corners_[i], cv::FONT_HERSHEY_SIMPLEX, 0.5, color);
            }
        }

    };  // UserDirectedBoardFinder

    class MarkerDirectedBoardFinder
    {
    private:
        std::vector< std::vector<cv::Point2f> > corners_;   ///< detected corners of the markers
        std::vector<int> ids_;                              ///< id's of the detected markers
        cv::Mat cameraMatrix_;                              ///< camera matrix
        cv::Mat distCoeffs_;                                ///< distortion coefficients
        cv::Ptr<cv::aruco::Dictionary> pDict_;              ///< pointer to the dictionary of aruco markers

        std::vector<cv::Point2f> getOuterCorners() const
        {
            std::vector<cv::Point2f> outerCorners(4);
            assert(4 == corners_.size());
            assert(4 == ids_.size());
            for (int i = 0; i < 4; ++i)
            {
                int c = ids_[i] < 2 ? ids_[i] : 5-ids_[i];
                outerCorners[c]= (corners_[i][c]);
            }
//            std::swap(outerCorners[2], outerCorners[3]);
            return outerCorners;
        }
    public:

        MarkerDirectedBoardFinder(const std::string& calibrationFile):
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

        std::vector<cv::Point2f> getCorners(const cv::Mat& img)
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
            int n = (int) ids_.size();
            assert( corners_.size() == n );
            if (n != 4)
            {
                LOG4CXX_INFO(logger, "Cannot see four markers");
                return std::vector<cv::Point2f>();
            }

            return getOuterCorners();
        }

        void draw(cv::Mat& img)
        {
            const int nCorners = (int) corners_.size();
            auto color = (nCorners < 4 ? DRAGGED_COLOR : FIXED_COLOR);
            cv::aruco::drawDetectedMarkers(img, corners_, ids_, color);
            if (nCorners == 4)
            {
                auto outerCorners = getOuterCorners();
                assert(outerCorners.size() == nCorners);
                for (int i = 0; i < 4; ++i)
                {
                    cv::drawMarker(img, outerCorners[i], DRAGGED_COLOR, cv::MarkerTypes::MARKER_STAR, 10);
                }
//#ifndef NDEBUG
//                static int nFrame =0;
//                cv::imwrite("marker_img" + std::to_string(nFrame++) + ".jpg", img);
//#endif
            }
        }
    };  // MarkerDirectedBoardFinder
}

template<class T>
cv::Mat_<double> getPerspectiveTransformationMatrix(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame, T boardFinder)
{
    cv::startWindowThread();
    cv::Mat_<double> trfMatrix;
    cv::Mat inputImg, warpedImg;
    std::vector<cv::Point2f> TGT_CORNERS;
    avtools::Frame intermediateFrame;
    cv::namedWindow(OUTPUT_WINDOW_NAME);

    //Initialize
    {
        auto ppFrame = pFrame.lock();
        if (!ppFrame)
        {
            throw std::runtime_error("Invalid frame provided to calculate perspective transform");
        }
        auto lock = ppFrame->getReadLock();
        intermediateFrame = ppFrame->clone();
        warpedImg = cv::Mat((*ppFrame)->height, (*ppFrame)->width, CV_8UC3);
    }
    //Start the loop
    std::vector<cv::Point2f> corners;
    while ( !g_ThreadMan.isEnded() && (cv::waitKey(20) < 0) )    //wait for key press
    {
        auto ppFrame = pFrame.lock();
        if (!ppFrame)
        {
            break;
        }
        cv::Rect2f roi(cv::Point2f(), warpedImg.size());
        // See if a new input image is available, and copy to intermediate frame if so
        {
            auto lock = ppFrame->tryReadLock(); //non-blocking attempt to lock
            assert((*ppFrame)->format == PIX_FMT);
            if ( lock && ((*ppFrame)->best_effort_timestamp != AV_NOPTS_VALUE) )
            {
                LOG4CXX_DEBUG(logger, "Transformer received frame with pts: " << (*ppFrame)->best_effort_timestamp);
            }
            ppFrame->clone(intermediateFrame);
        }
        // Display intermediateFrame and warpedFrame if four corners have been chosen
        inputImg = getImage(intermediateFrame);
        if (inputImg.total() > 0)
        {
            const float IMG_ASPECT = (float) inputImg.cols / (float) inputImg.rows;
            corners = boardFinder.getCorners(inputImg);
            int nCorners = (int) corners.size();
            LOG4CXX_DEBUG(logger, "Detected " << nCorners << " corners.");
            if (nCorners == 4)
            {
                //Calculate aspect ratio:
                float aspect = getAspectRatio(corners, inputImg.size());
                LOG4CXX_DEBUG(logger, "Calculated aspect ratio is: " << aspect);
                if (aspect > IMG_ASPECT)
                {
                    assert(roi.width == (float) warpedImg.cols);
                    assert(roi.x == 0.f);
                    roi.height = (float) warpedImg.cols / aspect;
                    roi.y = (warpedImg.rows - roi.height) / 2.f;
                    assert(roi.y >= 0.f);
                }
                else
                {
                    assert(roi.height == (float) warpedImg.rows);
                    assert(roi.y == 0.f);
                    roi.width = (float) warpedImg.rows * aspect;
                    roi.x = (warpedImg.cols - roi.width) / 2.f;
                    assert(roi.x >= 0.f);
                }
                TGT_CORNERS = {roi.tl(), cv::Point2f(roi.x + roi.width, roi.y), roi.br(), cv::Point2f(roi.x, roi.y + roi.height)};

                trfMatrix = cv::getPerspectiveTransform(corners, TGT_CORNERS);

#ifndef NDEBUG
                LOG4CXX_DEBUG(logger, "Need to transform \n" << corners << " to \n" << TGT_CORNERS);
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
                warpedImg = 0;
                cv::warpPerspective(inputImg, warpedImg, trfMatrix, warpedImg.size(), cv::InterpolationFlags::INTER_LINEAR);
                cv::imshow(OUTPUT_WINDOW_NAME, warpedImg);
                for (int i = 0; i < 4; ++i)
                {
                    cv::line(inputImg, corners[i], corners[(i+1) % 4], FIXED_COLOR);
                }
            }
            boardFinder.draw(inputImg);
            cv::imshow(INPUT_WINDOW_NAME, inputImg);
        }
    }
    if (!g_ThreadMan.isEnded())
    {
        if (corners.size() == 4)
        {
            assert(trfMatrix.total() > 0);
            LOG4CXX_DEBUG(logger, "Current perspective transform is :" << trfMatrix);
        }
        else
        {
            LOG4CXX_ERROR(logger, "Perspective transform requires 4 points, given only " << corners.size());
            trfMatrix = cv::Mat();
        }
    }
    cv::setMouseCallback(INPUT_WINDOW_NAME, nullptr, nullptr);
    cv::destroyAllWindows();
    cv::waitKey(10);
    return trfMatrix;
}

cv::Mat_<double> getPerspectiveTransformationMatrixFromUser(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame)
{
    cv::namedWindow(INPUT_WINDOW_NAME);
    return getPerspectiveTransformationMatrix(pFrame, UserDirectedBoardFinder());
}

cv::Mat_<double> getPerspectiveTransformationMatrixFromMarkers(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame, const std::string& calibrationFile)
{
    cv::namedWindow(INPUT_WINDOW_NAME);
    return getPerspectiveTransformationMatrix(pFrame, MarkerDirectedBoardFinder(calibrationFile));
}
