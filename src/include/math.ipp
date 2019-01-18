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
#include "log.hpp"
//#include "ski/type_traits.hpp"
#include <type_traits>
#include <vector>
#include <cassert>

namespace mymath
{
    namespace detail
    {
        template <typename T1> struct SumTypes<T1>
        {
            typedef T1 type;
        };
        template <typename T1, typename... Ts>
        struct SumTypes<T1, Ts...>
        {
            typedef typename SumTypes<Ts...>::type RhsType;
            typedef decltype( std::declval<T1>() + std::declval<RhsType>() ) type;
        };

        template <typename T1> struct ProductTypes<T1>
        {
            typedef T1 type;
        };
        template <typename T1, typename... Ts>
        struct ProductTypes<T1, Ts...>
        {
            typedef typename SumTypes<Ts...>::type RhsType;
            typedef decltype( std::declval<T1>() * std::declval<RhsType>() ) type;
        };

    }

    template<typename T>
    constexpr T sum(const T& val)
    {
        return val;
    }

    //Note that this function does not exist for Ts... = NULL by SFINAE since SumTypes<T>::type is undefined
    template<typename T, typename... Ts>
    constexpr auto sum(const T& val, const Ts&... vals) -> typename detail::SumTypes<T, Ts...>::type
    {
        return val + sum(vals...);
    }

    template<typename T>
    constexpr T product(const T& val)
    {
        return val;
    }
    
    //Note that this function does not exist for Ts... = NULL by SFINAE since SumTypes<T>::type is undefined
    template<typename T, typename... Ts>
    constexpr auto product(const T& val, const Ts&... vals) -> typename detail::ProductTypes<T, Ts...>::type
    {
        return val * product(vals...);
    }

    namespace detail
    {
        template<typename T, bool hasInfinity>
        struct MaxValueHelper
        {
            static const T value = std::numeric_limits<T>::infinity();
            operator T() const {return value; }
        };
        
        template<typename T>
        struct MaxValueHelper<T, false>
        {
            static const T value = std::numeric_limits<T>::max();
            operator T() const {return value; }
        };

        template<typename T, bool hasInfinity>
        struct MinValueHelper
        {
            static const T value = -std::numeric_limits<T>::infinity();
            operator T() const {return value; }
        };
        
        template<typename T>
        struct MinValueHelper<T, false>
        {
            static const T value = std::numeric_limits<T>::min();
            operator T() const {return value; }
        };
        
        template <bool shouldBeRounded>
        struct cast_selector
        {
            template <typename T1, typename T2>
            inline static T2 cast(T1 val) {return (shouldBeRounded ? floor(val + 0.5): val); }
        };

    }
    
    /**
     * returns the maximum number of a given type.
     * @return maximum value of a given type.
     */
    template<typename T>
    constexpr T MaxValue()
    {
        return detail::MaxValueHelper<T, std::numeric_limits<T>::has_infinity>::value;
    }

    /**
     * returns the minimum number of a given type.
     * @return minimum value of a given type.
     */
    template<typename T>
    constexpr T MinValue()
    {
        return detail::MinValueHelper<T, std::numeric_limits<T>::has_infinity>::value;
    }
    
    ///@return true if an integral value is odd
    template<typename T>
    constexpr typename std::enable_if<std::is_integral<T>::value, bool>::type isOdd(const T& aVal)
    {
        return (aVal & 1);
    }

    ///@return true if an integral value is even
    template<typename T>
    constexpr typename std::enable_if<std::is_integral<T>::value, bool>::type isEven(const T& aVal)
    {
        return !isOdd(aVal);
    }

    /**
     * Power
     * @param[in] number integer to take power of.
     * @param[in] exponent exponent
     * @return aNumber ^ aPow.  Note that integral numbers can easily overflow.  To avoid overflow, use a float or double version.
     */
    template<typename T>
    constexpr int pow(const T number, unsigned const exponent)
    {
        return (exponent == 0 ? 1 : number * pow(number, exponent-1) );
    }
    
    /**
     * Round
     * @param[in] a value to round
     * @return value rounded towards infinity
     */
    inline double round( double value ) {return floor( value + 0.5 ); }
    
//    template <bool shouldBeRounded>
//    struct cast_selector
//    {
//        template <typename T1, typename T2>
//        inline static T2 cast(T1 val) {return (shouldBeRounded ? floor(val + 0.5): val); }
//    };
//    
//    template <typename T1, typename T2>
//    inline T2 cast(T1 val) {return detail::cast_selector<should_be_rounded<T1,T2>::value>::cast(val);  }
//    
    template <typename T>
    inline T cast (T val) { return val; }
    
    namespace detail
    {
        ///Template class to use the correct method.
        template <bool isImplemented>
        struct method_selector
        {
            template <class T>
            inline static double norm(const T& obj)
            {
                return std::abs(obj);
            }
            
            template <class T>
            inline static double distance(const T& obj1, const T& obj2)
            {
                return norm(obj1-obj2);
            }
        };
        
        //Uses the object's norm method if the object is designated as having implemented norm()
        template <>
        struct method_selector<true>
        {
            template <class T>
            inline static double norm(const T& obj)
            {
                return obj.norm();
            }
            
            template <class T>
            inline static double distance(const T& obj1, const T& obj2)
            {
                return obj1.distance(obj2);
            }
        };
        
        //TODO: Implement as norm
    }
    
    template <typename T, typename std::enable_if<std::is_pod<T>::value>::type=0 >
    inline double norm(T val)
    {
        LOGW("Why here???");
        return std::abs((double) val);
    }
        
    /**
     * Distance between two objects
     * @param[in] a object 1.
     * @param[in] b object 2.
     * If T is a complex class, the objects must implement a.norm(), b.norm() and the difference operator.
     * @return distance between a and b.
     */
    template <class T>
    inline double distance(const T &a, const T &b)
    {
#if DEBUG > 1
        if ( distance(b, a) != distance(a, b) )
        {
            LOGW("Distance is not symmetric between these two objects!");
        }
#endif
        return norm(b-a);
    }
    
    namespace detail
    {
        template<typename T, bool isFloatingPoint=false>
        struct isEqualHelper
        {
            static bool Compare(T val1, T val2)
            {
                return (val1 == val2);
            }
        };

        template<typename T>
        struct isEqualHelper<T, true>
        {
            constexpr static T epsilon = std::numeric_limits<T>::epsilon();
            static bool Compare(T val1, T val2)
            {
                if (val1 == val2)
                    return true;
                else
                {
                    if (val1 == 0)
                        return ( abs(val2) < epsilon );
                    else if (val2 == 0)
                        return ( abs(val1) < epsilon );
                    else
                        return ( abs(val1 - val2) < epsilon * (abs(val1) + abs(val2)) );
                }
   
            }
        };
    }   //end namespace detail

    template<typename T>
    bool isEqual(T val1, T val2)
    {
        return detail::isEqualHelper<T, std::is_floating_point<T>::value>::Compare(val1, val2);
    }
    
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
    template<typename Iterator, typename result_type, typename>
    inline void calculateLowerOrderStatistics(Iterator begin, const Iterator end, result_type& mean, result_type& var)
    {
        mean = var = 0;
        result_type val = 0;
        auto size = std::distance(begin, end);
        while (begin != end)
        {
            val = *begin;
            mean += val;
            var += val * val;
            ++begin;
        }
        mean /= size;
        var /= size;
        var -= (mean * mean);
        if (0.f > var)
        {
            var = 0.f;
        }
    }
    
    /**
     * Returns an iterator to the median value of an array, which is modified in place.
     * For speed, does not return the true median if n is even, but the element right after the median.
     */
    template<typename Iterator, typename Compare>
    Iterator calculateMedian(Iterator begin, const Iterator end, const Compare compare)
    {
        assert( begin != end );
        auto size = std::distance(begin, end);
        auto median = begin;
        std::advance(median, (size >> 1) );
        std::nth_element(begin, median, end, compare);
        return median;
    }
    
    template<typename Iterator>
    Iterator calculateMedian(Iterator begin, const Iterator end)
    {
        return calculateMedian( begin, end, std::less<typename std::iterator_traits<Iterator>::value_type>() );
    }

    namespace detail
    {
        template<typename T, bool B = false>
        struct UniformRandomNumberGenerator;

        template<typename T>
        struct UniformRandomNumberGenerator<T, true>
        {
            T operator()(T minVal, T maxVal) const
            {
                return ( minVal + ( std::rand() % (maxVal - minVal + 1) ) );
            }
        };

        template<typename T, typename Generator>
        T generateUniformRandomNumberHelper(T minVal, T maxVal, Generator gen)
        {
            return gen(minVal, maxVal);
        }

    }
    
    template<typename T>
    T generateUniformRandomNumber(T minVal, T maxVal)
    {
        assert(minVal <= maxVal);
        return generateUniformRandomNumberHelper(minVal, maxVal, detail::UniformRandomNumberGenerator<T, std::is_integral<T>::value>());
    }

    template<typename T>
    T generateUniformRandomNumber(T range)
    {
        return generateUniformRandomNumber((T) 0, range);
    }

    /*
    inline TUInt generateUniformRandomNumber(TUInt range)
    {
        // advanced
//         static mt19937 generator( chrono::system_clock::now().time_since_epoch().count() );
//         static uniform_int_distribution<TUInt> distribution;
//         uniform_int_distribution<TUInt>::param_type params(0, range);
//         distribution.param(params);
//         return distribution(generator);
        return (std::rand() % (range + 1) );
    };
     */
    namespace detail
    {
        template<typename RAIterator, typename T>
        void generateRandomSelections(RAIterator begin, RAIterator end, T range)
        {
            auto nValues = std::distance(begin, end);
            assert(nValues <= range);
            //Generate nComparisons using the Floyd algorithm
            //See http://stackoverflow.com/questions/1608181/unique-random-numbers-in-an-integer-array-in-the-c-programming-language1
            //SPEEDUP: if nPermutations > range / 2, it is faster to generate range-nPermutations and remove this from 0...range-1

            bool isUsed[range];
            std::memset(isUsed, false, range * sizeof(bool));
            T r = range - (T) nValues;
            assert(r >= 0);
            for (size_t n = 0; r < range; ++r, ++n)
            {
                //draw
                T val = generateUniformRandomNumber(r);
                if ( isUsed[val] )
                    val = r;
                isUsed[val] = true; //val is now r
                //Adjust range
                *(begin + n) = val;
                //shuffle - see http://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle#The_.22inside-out.22_algorithm
                size_t loc = generateUniformRandomNumber(n);
                if (loc != n)
                {
                    std::iter_swap(begin + n, begin + loc);
                }
            }
        }
        
        template<typename RAIterator, typename T>
        void generateRandomOrderedSelections(RAIterator begin, RAIterator end, T range)
        {
            auto nValues = std::distance(begin, end);
            assert(nValues <= range);
            //Generate nComparisons using the Floyd algorithm
            //See http://stackoverflow.com/questions/1608181/unique-random-numbers-in-an-integer-array-in-the-c-programming-language1
            //SPEEDUP: if nPermutations > range / 2, it is faster to generate range-nPermutations and remove this from 0...range-1

            bool isUsed[range];
            std::memset(isUsed, false, range * sizeof(bool));
            T r = range - (T) nValues;
            assert(r >= 0);
            for (size_t n = 0; r < range; ++r, ++n)
            {
                //draw
                T val = generateUniformRandomNumber(r);
                if ( isUsed[val] )
                    val = r;
                isUsed[val] = true; //val is now r
                //Adjust range
                *(begin + n) = val;
            }
            //Sort
            std::sort(begin, end);
        }

        template<typename RAIterator, typename T>
        void generateRandomComparisons(RAIterator begin, RAIterator end, const T range)
        {
            auto nComparisons = std::distance(begin, end);
            assert(2 * nComparisons <= range);
            //Generate nComparisons using the Floyd algorithm
            //See http://stackoverflow.com/questions/1608181/unique-random-numbers-in-an-integer-array-in-the-c-programming-language1
            //SPEEDUP: if nPermutations > range / 2, it is faster to generate range-nPermutations and remove this from 0...range-1

            bool isUsed[range];
            std::memset(isUsed, false, range * sizeof(bool));

            T r = range - 2 * (T) nComparisons;
            assert(r >= 0);
            for (size_t n = 0; r < range; ++r, ++n)
            {
                //draw 2 unique numbers
                T val1 = generateUniformRandomNumber(r);
                if ( isUsed[val1] )
                    val1 = r;
                isUsed[val1] = true; //val1 is now r
                T val2 = generateUniformRandomNumber(++r);
                if ( isUsed[val2] )
                    val2 = r;
                isUsed[val2] = true; //val2 is now r
                *(begin + n) = std::make_pair(val1, val2);
                //shuffle - see http://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle#The_.22inside-out.22_algorithm
                size_t loc = generateUniformRandomNumber(n);
                if (loc != n)
                {
                    std::iter_swap(begin + n, begin + loc);
                }
            }
        }

    }
    
    template<typename RAIterator, typename T, typename>
    void generateRandomSelections(RAIterator begin, RAIterator end, T range)
    {
        assert (range > 0);
        //FIXME: static_assert(std::is_same<decltype(std::iterator_traits<RAIterator>::iterator_category()), std::random_access_iterator_tag>());
        detail::generateRandomSelections( begin, end, range );
    }

    template<typename RAIterator, typename T, typename>
    void generateRandomOrderedSelections(RAIterator begin, RAIterator end, T range)
    {
        assert (range > 0);
        //FIXME: static_assert(std::is_same<decltype(std::iterator_traits<RAIterator>::iterator_category()), std::random_access_iterator_tag>());
        detail::generateRandomOrderedSelections( begin, end, range);
    }

    template<typename RAIterator, typename T, typename>
    void generateRandomComparisons(RAIterator begin, RAIterator end, T range)
    {
        typedef typename std::iterator_traits<RAIterator>::value_type PairType;
        //FIXME: static_assert(std::is_same<decltype(std::iterator_traits<RAIterator>::iterator_category()), std::random_access_iterator_tag>());
        static_assert(std::is_same<PairType, std::pair<T, T>>::value, "Underlying pair type must have same integral type members.");
        detail::generateRandomComparisons(begin, end, range);
    }

    
    template<typename SrcIterator, typename DestIterator>
    void normalizePatch(SrcIterator begin, const SrcIterator end, DestIterator dest)
    {
        float mean, var;
        calculateLowerOrderStatistics(begin, end, mean, var);
        static_assert(std::is_floating_point<typename std::iterator_traits<DestIterator>::value_type>::value, "Destination must be a floating point type");
        if (var > 0.f)
        {
            const float normConst = 1.f / sqrt(var);
            for (; begin != end; ++dest, ++begin)
                *dest = ( ((float) *begin) - mean ) * normConst;
        }
        else    //uniform patch
        {
            for(; begin != end; ++dest, ++begin)
                *dest = 0.f;
        }
    }
    
    template<typename Iterator1, typename Iterator2>
    float calculateCrossCorrelation(Iterator1 it1, const Iterator1 end1, Iterator2 it2)
    {
        //Using CV_TM_CCOEFF_NORMED, See http://docs.opencv.org/modules/imgproc/doc/object_detection.html?highlight=matchtemplate
        typedef typename std::iterator_traits<Iterator1>::value_type value_type;
        static_assert(std::is_same<value_type, typename std::iterator_traits<Iterator2>::value_type>::value, "Source and destination iterators must have the same underlying data type");
        float cc = 0;
        auto n = std::distance(it1, end1);
        while (it1 != end1)
        {
            cc += (float) (*it1) * (float) (*it2);
            ++it1;
            ++it2;
        }
        return cc / n;
    }

    template<typename Iterator1, typename Iterator2>
    float calculateNormalizedCrossCorrelation(Iterator1 it1, const Iterator1 end1, Iterator2 it2)
    {
        //Using CV_TM_CCOEFF_NORMED, See http://docs.opencv.org/modules/imgproc/doc/object_detection.html?highlight=matchtemplate
        if (it1 == end1)
            return 0.f;
        typedef typename std::iterator_traits<Iterator1>::value_type value_type;
        static_assert(std::is_same<value_type, typename std::iterator_traits<Iterator2>::value_type>::value, "Source and destination iterators must have the same underlying data type");
        float ncc = 0.f, m1 = 0.f, m2 = 0.f, v1 = 0.f, v2 = 0.f;
        auto n = std::distance(it1, end1);
        while (it1 != end1)
        {
            m1 += (*it1);
            m2 += (*it2);
            ncc += (float)(*it1) * (float)(*it2);
            v1 += (float)(*it1) * (float)(*it1);
            v2 += (float)(*it2) * (float)(*it2);
            ++it1;
            ++it2;
        }
        ncc -= (m1 * m2 / n);
        v1 -= (m1 * m1 / n);
        v2 -= (m2 * m2 / n);
        float v = v1 * v2;
        return ( v <= 0.f ? 0.f : ncc / sqrt(v) );
    }

    template<typename T, const T minVal,const T maxVal>
    typename std::enable_if<std::is_signed<T>::value && std::is_integral<T>::value, T>::type
    saturate_add(T val1, T val2)
    {
        if ( (val1 > 0) && (val2 > maxVal - val1) )    //overflow
        {
            return maxVal;
        }
        else if ( (val1 < 0) && (val2 < minVal - val1) )   //underflow
        {
            return minVal;
        }
        return val1 + val2;
    }
    
    template<typename T>
    typename std::enable_if<std::is_unsigned<T>::value && std::is_integral<T>::value, T>::type
    saturate_add(T val1, T val2, const T maxVal)
    {
        if ( val2 > maxVal - val1 )    //overflow
        {
            return maxVal;
        }
        return val1 + val2; //unsigned cannot underflow
    }
    
    template<typename T>
    typename std::enable_if<std::is_floating_point<T>::value, T>::type
    saturate_add(T val1, T val2, const T minVal, const T maxVal)
    {
        if ( val1 + val2 > maxVal )
        {
            return maxVal;
        }
        else if ( val1 + val2 < minVal )
        {
            return minVal;
        }
        return val1 + val2;
    }

}	//::mymath
