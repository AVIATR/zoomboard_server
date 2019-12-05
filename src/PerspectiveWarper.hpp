//
//  PerspectiveWarper.hpp
//  zoomboard_server
//
//  Created by Ender Tekin on 11/13/19.
//

#ifndef PerspectiveWarper_h
#define PerspectiveWarper_h
#include <string>
#include <memory>
#include "LibAVWrappers.hpp"

namespace avtools
{
    class PerspectiveAdjustor
    {
    public:
        /// Ctor
        /// @param[in] calibrationFile calibration file that has information re: the camera matrix and markers to use
        PerspectiveAdjustor(const std::string& calibrationFile);

        /// Corrects the perspective of an input frame based on detected aruco markers
        /// Defined in @ref correct_perspective.cpp
        /// @param[in] inFrame input frame
        /// @return reference to transformed output frame
        const Frame& correctPerspective(const Frame& inFrame);

        /// Dtor
        ~PerspectiveAdjustor();

    private:
        class Implementation;                           ///< Implementation class declaration
        std::unique_ptr<Implementation> pImpl_;         ///< ptr to implementation
    };
}   //::avtools

#endif /* PerspectiveWarper_h */
