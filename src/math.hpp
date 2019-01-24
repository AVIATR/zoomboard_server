/*
 Copyright (c) 2012, The Smith-Kettlewell Eye Research Institute
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 * Neither the name of the The Smith-Kettlewell Eye Research Institute nor
 the names of its contributors may be used to endorse or promote products
 derived from this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE SMITH-KETTLEWELL EYE RESEARCH INSTITUTE BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * Math header for general use
 * Ender Tekin
 */

#ifndef SKI_MATH_H_
#define SKI_MATH_H_

#include <cmath>
//#include "ski/types.h"
#include <cstdint>
#include <vector>
#include <stdarg.h>
#undef max
#undef min
#undef MAXINT
#undef MININT
#undef MAXUINT
#include <limits>
#include <algorithm>
#include <cstring>

namespace mymath
{
    
    const double PI=(4*atan(1.0));
    const double INF = std::numeric_limits<double>::infinity();
    const double NEGINF = -INF;
    static const int MAXINT = (std::numeric_limits<int>::max)();
    static const int MININT = (std::numeric_limits<int>::min)();
    static const std::uint32_t MAXUINT = (std::numeric_limits<std::uint32_t>::max)();
    static const int VERYLARGE = MAXINT/16; //more formal in future.

    namespace detail
    {
        //Metafunctions to calculate the results of operations of two different types
        //Built upon http://stackoverflow.com/questions/16663866/variadic-templates-sum-operation-left-associative
        template <typename...> struct SumTypes;
        template <typename...> struct ProductTypes;
    }
    
    ///Adds a set of numbers in a left-associative manner
    ///@param val value
    ///@return returns val
    template<typename T>
    constexpr T sum(const T& val);
    
    ///Adds a set of numbers in a left-associative manner
    ///@param val1 first value
    ///@param val2 second value
    ///@params vals... further values
    ///@return returns sum of val1 + val2 + vals...
    template<typename T, typename... Ts>
    constexpr auto sum(const T& val1, const Ts&... vals) -> typename detail::SumTypes<T, Ts...>::type;

    ///Multiplies a set of numbers in a left-associative manner
    ///@param val value
    ///@return returns val
    template<typename T>
    constexpr T product(const T& val);
    
    ///Multiplies a set of numbers in a left-associative manner
    ///@param val1 first value
    ///@param val2 second value
    ///@params vals... further values
    ///@return returns sum of val1 + val2 + vals...
    template<typename T, typename... Ts>
    constexpr auto product(const T& val1, const Ts&... vals) -> typename detail::ProductTypes<T, Ts...>::type;

    /**
     * returns the maximum number of a given type.
     * @return maximum value of a given type.
     */
    template<typename T>
    constexpr T MaxValue();

    /**
     * returns the minimum number of a given type.
     * @return minimum value of a given type.
     */
    template<typename T>
    constexpr T MinValue();
    
    ///@return true if an integral value is odd
    template<typename T>
    constexpr typename std::enable_if<std::is_integral<T>::value, bool>::type isOdd(const T& aVal);
    
    ///@return true if an integral value is even
    template<typename T>
    constexpr typename std::enable_if<std::is_integral<T>::value, bool>::type isEven(const T& aVal);

    /**
     * Power
     * @param[in] number integer to take power of.
     * @param[in] exponent exponent
     * @return aNumber ^ aPow.  Note that integral numbers can easily overflow.  To avoid overflow, use a float or double version.
     */
    template<typename T>
    constexpr int pow(const T number, unsigned const exponent);
    
    /**
     * Round
     * @param[in] a value to round
     * @return value rounded towards infinity
     */
    inline double round( double value );
    
    template <typename T1, typename T2>
    inline T2 cast(T1 val);
    
    template <typename T>
    inline T cast (T val);
    
    template <typename T>
    inline double norm(T val);
        
    /**
     * Distance between two objects
     * @param[in] a object 1.
     * @param[in] b object 2.
     * If T is a complex class, norm<T>(T) must be defined.
     * @return distance between a and b.
     */
    template <class T>
    inline double distance(const T &a, const T &b);

    ///
    ///Returns true if two values are equal.
    ///If T is a floating point type, than the values must be equal up to a tolerance
    template<typename T>
    bool isEqual(T val1, T val2);
    
    /**
     * Calculates the mean and variance of an array
     * mean is calculated as @f[ mean = \frac{1}{n}\sum_{i=1}^{n} array[i] @f].
     * variance is calculated as @f[ mean = \frac{1}{n}\sum_{i=1}^{n} (array[i]-mean)^2 = \frac{1}{n} \sum_{i=1}^n array[i]^2 - mean^2 @f].
     * TODO: implement for containers with iterators
     * TODO: implement form that also calculates a streaming median using the selection algorithm @url http://en.wikipedia.org/wiki/Selection_algorithm
     * calculating the median of medians
     * @param[in] begin beginning of the array to process
     * @param[in] end end of the the array to process
     * @param[out] mean contains the calculated mean upon return
     * @param[out] var contains the calculated variance upon return
     */
    template<typename Iterator, typename result_type=float, typename=typename std::enable_if<std::is_floating_point<result_type>::value>::type>
    inline void calculateLowerOrderStatistics(Iterator begin, const Iterator end, result_type &mean, result_type &var);

    /**
     * Returns an iterator to the median value of an array, which is modified in place.
     * For speed, does not return the true median if n is even, but the element right after the median.
     */
    template<typename Iterator, typename Compare>
    Iterator calculateMedian(Iterator begin, const Iterator end, const Compare compare);
    
    /**
     * Returns an iterator to the median value of an array, which is modified in place.
     * For speed, does not return the true median if n is even, but the element right after the median.
     */
    template<typename Iterator>
    Iterator calculateMedian(Iterator begin, const Iterator end);
    
    /**
     * Generates a uniform random number
     * @param range range of the random number
     * @return a number chosen uniformly in \f[[minVal, maxVal]\f].
     */
    template<typename T>
    inline T generateUniformRandomNumber(T minVal, T maxVal);
    
    /**
     * Generates a uniform random number
     * @param range range of the random number
     * @return a number chosen uniformly in \f[[0, range]\f].
     */
    template<typename T>
    inline T generateUniformRandomNumber(T range);

    
    //Fills an array with unique randomly generated numbers in [0, range].
    /// @param[in] RAIterator begin iterator to beginning of array, must be a random access iterator
    /// @param[in] RAIterator end iterator to end of array, must be a random access iterator
    /// @param[in] range range of random values
    template<typename RAIterator, typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    void generateRandomSelections(RAIterator begin, RAIterator end, T range);

    //Fills an array with unique randomly generated numbers in [0, range]. The selections will be ordered in ascending order.
    /// @param[in] RAIterator begin iterator to beginning of array, must be a random access iterator
    /// @param[in] RAIterator end iterator to end of array, must be a random access iterator
    /// @param[in] range range of random values
    template<typename RAIterator, typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    void generateRandomOrderedSelections(RAIterator begin, RAIterator end, T range);

    //Fills an array with unique randomly generated comparisons, each member of which is uniquely chosend in [0, range].
    /// @param[in] RAIterator begin iterator to beginning of array, must be a random access iterator
    /// @param[in] RAIterator end iterator to end of array, must be a random access iterator
    /// @param[in] range range of random values
    template<typename RAIterator, typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    void generateRandomComparisons(RAIterator begin, RAIterator end, T range);
    
    /**
     * Normalizes a range so that its total energy is 1, and mean is zero (unless all the elements are the same, in which case the destination is all zeros).
     * @param[in] begin iterator pointing to the beginning of the source data
     * @param[in] end iterator pointing to the end of the source data
     * @param[in] dest iterator pointing to the beginning of the destination data
     * @return
     */
    template<typename SrcIterator, typename DestIterator>
    void normalizePatch(SrcIterator begin, const SrcIterator end, DestIterator dest);
    
    template<typename Iterator1, typename Iterator2>
    float calculateCrossCorrelation(Iterator1 it1, const Iterator1 end1, Iterator2 it2);
    
    template<typename Iterator1, typename Iterator2>
    float calculateNormalizedCrossCorrelation(Iterator1 it1, const Iterator1 end1, Iterator2 it2);

    /// Addition with clipping for signed integer types
    /// http://codereview.stackexchange.com/questions/37177/simpler-method-to-detect-int-overflow
    /// @param[in] val1 first value to add
    /// @param[in] val2 second value to add
    /// @return summation of val1 & val2, clipped.
    template<typename T,
    const T minVal=std::numeric_limits<T>::min(),
    const T maxVal=std::numeric_limits<T>::max()>
    typename std::enable_if<std::is_signed<T>::value && std::is_integral<T>::value, T>::type
    saturate_add(T val1, T val2);
    
    /// Addition with clipping for unsigned integer types
    /// http://codereview.stackexchange.com/questions/37177/simpler-method-to-detect-int-overflow
    /// @param[in] val1 first value to add
    /// @param[in] val2 second value to add
    /// @return summation of val1 & val2, clipped.
    template<typename T>
    typename std::enable_if<std::is_unsigned<T>::value && std::is_integral<T>::value, T>::type
    saturate_add(T val1, T val2, const T maxVal = std::numeric_limits<T>::max());
    
    /// Addition with clipping for floating types. The limits are assumed to be \$[ \pm 1 \$]
    /// http://codereview.stackexchange.com/questions/37177/simpler-method-to-detect-int-overflow
    /// @param[in] val1 first value to add
    /// @param[in] val2 second value to add
    /// @return summation of val1 & val2, clipped.
    template<typename T>
    typename std::enable_if<std::is_floating_point<T>::value, T>::type
    saturate_add(T val1, T val2, const T minVal=-1, const T maxVal=1);

}	//::mymath


#include "math.ipp"

#endif // SKI_MATH_H_
