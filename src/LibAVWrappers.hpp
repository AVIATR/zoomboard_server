//
//  LibAVWrappers.hpp
//  C++ Wrappers around some common libav structures
//  rtmp_server
//
//  Created by Ender Tekin on 2/8/19.
// TODO: Create a base class for get(), ->() and bool() using CRTP to reduce codebase
//

#ifndef LibAVWrappers_hpp
#define LibAVWrappers_hpp

#include <string>
#include <iostream>

#include "Media.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/parseutils.h>
}

struct AVPacket;
struct AVFrame;
struct AVCodecContext;
struct AVCodecParameters;
struct AVFormatContext;
struct AVDictionary;
struct SwsContext;

namespace avtools
{
    // Helper functions

    /// Initialized the data buffers for a new video frame. The frame should be allocated previously
    /// via av_alloc_frame() or similar.
    /// @param[in] pFrame frame to fill in the metadata and allocate the data buffers for
    /// @param[in] width frame width in pixels
    /// @param[in] height frame height in pixels
    /// @param[in] format frame pixel layout format
    /// @param[in] cs colorspace
    void initVideoFrame(AVFrame* pFrame, int width, int height, AVPixelFormat format, AVColorSpace cs=AVColorSpace::AVCOL_SPC_RGB);

    class CodecParameters;  //forward declaraion

    /// @class Wrapper around AVFrame
    class Frame
    {
    protected:
        AVFrame* pFrame_;                                       ///< ptr to wrapped frame
    public:
        AVMediaType type;                                       ///< type of frame if data buffers are initialized
        TimeBaseType timebase;                                  ///< timebase of the frame

        /// Ctor that wraps around an frame
        /// @param[in] pFrame ptr to a preallocated frame, or nullptr if one should be internally allocated
        /// @param[in] type type of the data that this frame is expected to contain.
        /// @param[in] tb timebase of the frame if known
        /// @throw MediaError if there is a problem allocating a frame of the given type.
        Frame(AVFrame* pFrame=nullptr, AVMediaType type=AVMEDIA_TYPE_UNKNOWN, TimeBaseType tb=TimeBaseType{});
        
        /// Ctor that allocates a frame according the provided codec parameters
        /// @param[in] codecPar codec parameters
        /// @param[in] tb timebase of the frame if known
        /// @throw StreamError if the constructor was unable to allocate a frame
        Frame(const AVCodecParameters& codecPar, TimeBaseType tb=TimeBaseType{});

        /// Ctor that allocates a frame according the provided codec parameters
        /// @param[in] codecPar codec parameters
        /// @param[in] tb timebase of the frame if known
        /// @throw StreamError if the constructor was unable to allocate a frame
        Frame(const CodecParameters& codecPar, TimeBaseType tb=TimeBaseType{});

        /// Allocates a video frame with a given size, format and sample rate.
        /// Basically combines av_frame_alloc() and initVideoFrame()
        /// @param[in] width how wide the image is in pixels
        /// @param[in] height how high the image is in pixels
        /// @param[in] format pixel format
        /// @param[in] tb timebase of the frame if known
        /// @param[in] cs color space type
        /// @return pointer to a new frame, nullptr if a frame could not be allocated
        Frame(int width, int height, AVPixelFormat format, TimeBaseType tb=TimeBaseType{}, AVColorSpace cs=AVColorSpace::AVCOL_SPC_RGB);

        /// Copy ctor
        /// @param[in] frame source frame. Note that the data buffers are not cloned, but this frame refers
        /// to the same buffers as frame
        Frame(const Frame& frame);
        
        /// Dtor
        virtual ~Frame();
        
        ///@return the raw pointer to the wrapped AVFrame
        inline AVFrame* get() noexcept { return pFrame_;}
        
        ///@return the raw pointer to the wrapped AVFrame
        inline const AVFrame* get() const noexcept { return pFrame_;}

        ///@return a chainable ptr to the AVFrame
        inline AVFrame* operator->() {return pFrame_;}

        ///@return a chainable ptr to the AVFrame
        inline const AVFrame* operator->() const {return pFrame_;}

        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (pFrame_ != nullptr); }

        /// Copy operator. This only adds references to the source frame, does not clone the underlying data
        /// @param[in] frame source frame.
        /// @return a reference to this frame.
        Frame& operator=(const Frame& frame);

        /// Clone operator.
        /// @return a new frame that has the same data and metadata as this one
        Frame clone() const;

        /// Clone operator for a pre-allocated frame
        /// @param[in] frame preallocated frame that has the same dimensions, format & colorspace as this frNE
        void clone(Frame& frame) const;

        /// Provides a brief information string regarding the underlying frame.
        /// @param[in] indent number of indentation tabs in the returned string
        /// @return a string with information about the frame.
        std::string info(int indent=0) const;
    }; //avtools::Frame

    /// @class wrapper around AVPacket
    class Packet
    {
    private:
        AVPacket* pPkt_;
    public:
        /// Wrapping ctor
        /// @param[in] pPkt ptr to a packet. If nullptr, packet is allocated internally. Otherwise, it is cloned
        /// so that the buffers are referred to by the new packet
        Packet(const AVPacket* pPkt=nullptr);

        /// Copy ctor
        /// @param[in] source packet. Note that this packet references the same data as pkt.
        Packet(const Packet& pkt);

        /// Ctor that initializes a packet referring to existing data
        Packet(std::uint8_t* data, int len);

        /// Dtor
        ~Packet();
        
        /// @return a pointer to the wrapped AVPacket
        inline AVPacket* get() noexcept { return pPkt_;}
        
        /// @return a pointer to the wrapped AVPacket
        inline const AVPacket* get() const noexcept { return pPkt_;}

        ///@return a chainable ptr to the AVFrame
        inline AVPacket* operator->() {return pPkt_;}

        ///@return a chainable ptr to the AVFrame
        inline const AVPacket* operator->() const {return pPkt_;}

        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (pPkt_ != nullptr); }

        /// Unreferences the data buffers references by the packet
        void unref();

        /// Provides information re: the packet
        /// @param[in] indent number of tabs to indent the returned string with
        /// @return an informational string re: the packet's contents
        std::string info(int indent=0) const;
    };  //avtools::Packet
    
    /// @class Wrapper around AVDictionary
    class Dictionary
    {
    private:
        AVDictionary* pDict_;                                   ///< actual dictionary
    public:
        /// Ctor
        /// @param[in] pDict a previously allocated dictionary, or nullptr if one should be allocated
        Dictionary(const AVDictionary* pDict=nullptr);
        /// Ctor
        /// @param[in] dict a dictionary to clone. All entries are copied here as a separate dictionary
        inline Dictionary(const Dictionary& dict): Dictionary(dict.get()) {};
        /// Dtor
        ~Dictionary();
        ///< @return pointer to the wrapper dictionary
        inline AVDictionary*& get() {return pDict_;}
        ///< @return pointer to the wrapper dictionary
        inline const AVDictionary* get() const {return pDict_;}
        ///< @return pointer to the wrapper dictionary
        inline AVDictionary* operator->() {return pDict_;}
        ///< @return pointer to the wrapper dictionary
        inline const AVDictionary* operator->() const {return pDict_;}

        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (pDict_ != nullptr); }

        /// Adds a key/value pair to the dictionary
        /// @param[in] key new key to add
        /// @param[in] value value of the key
        /// @throw std::runtime_error if there was an error adding the key
        void add(const std::string& key, const std::string& value);
        
        /// Adds a key/value pair to the dictionary
        /// @param[in] key new key to add
        /// @param[in] value value of the key
        /// @throw std::runtime_error if there was an error adding the key
        void add(const std::string& key, TimeType value);

        /// Adds a key/value pair to the dictionary
        /// @param[in] key new key to add
        /// @param[in] value value of the key
        /// @throw std::runtime_error if there was an error adding the key
        void add(const std::string& key, TimeBaseType value);

        /// Adds a key/value pair to the dictionary
        /// @param[in] key new key to add
        /// @param[in] value value of the key
        /// @throw std::runtime_error if there was an error adding the key
        void add(const std::string& key, AVPixelFormat value);

        /// Returns the value of a key in the dictionary.
        /// @param[in] key key to search for
        /// @return value of the key
        /// @throw std::runtime_error if there was an error retrieving the value
        template<typename T>
        T at(const std::string& key) const;
        
        /// Returns the value of a key in the dictionary.
        /// @param[in] key key to search for
        /// @return value of the key
        /// @throw std::runtime_error if there was an error retrieving the value
        std::string operator[](const std::string& key) const;

        /// Sets the value of an existing key in the dictionary.
        /// @param[in] key key to search for
        /// @param[in] value new value of the key, old value is overwritten
        /// @throw std::out_of_range if key was not found in dictionary
        /// @throw std::runtime_error if there was a problem setting the value
        void set(const std::string& key, const std::string& value);

        /// Sets the value of an existing key in the dictionary.
        /// @param[in] key key to search for
        /// @param[in] value new value of the key, old value is overwritten
        /// @throw std::out_of_range if key was not found in dictionary
        /// @throw std::runtime_error if there was a problem setting the value
        void set(const std::string& key, TimeType value);

        /// Sets the value of an existing key in the dictionary.
        /// @param[in] key key to search for
        /// @param[in] value new value of the key, old value is overwritten
        /// @throw std::out_of_range if key was not found in dictionary
        /// @throw std::runtime_error if there was a problem setting the value
        void set(const std::string& key, TimeBaseType value);

        /// Sets the value of an existing key in the dictionary.
        /// @param[in] key key to search for
        /// @param[in] value new value of the key, old value is overwritten
        /// @throw std::out_of_range if key was not found in dictionary
        /// @throw std::runtime_error if there was a problem setting the value
        void set(const std::string& key, AVPixelFormat value);

        /// Checks to see if the dictionary has a particular key
        /// @param[in] key to check for
        /// @return true if the dictionary has this key, false otherwise.
        bool has(const std::string& key) const;

        /// String cast operator
        operator std::string() const;

        /// @return size of the dictionary
        int size() const;

        /// @return true if dictionary is empty
        bool empty() const;

        Dictionary& operator=(const Dictionary& dict);
    };  //avtools::Dictionary

    inline std::ostream& operator<<(std::ostream& stream, const Dictionary& dict)
    {
        return ( stream << (std::string) dict );
    }

    /// @class Wrapper around AVCodecContext
    class CodecContext
    {
    private:
        AVCodecContext* pCC_;
    public:
        /// Ctor
        /// @param[in] pCodec pointer to the codec to use
        explicit CodecContext(const AVCodec* pCodec);

        /// Ctor
        /// @param[in] pCodec pointer to the codec to use
        explicit CodecContext(const AVCodecContext* pCodec);

        /// Ctor
        /// @param[in] param codec parameters to use
        CodecContext(const CodecParameters& param);

        /// Copy ctor
        /// @param[in] cc source codec context. Entries are cloned
        CodecContext(const CodecContext& cc);

        /// Dtor
        ~CodecContext();
        
        /// @return a pointer to the wrapped AVCodecContext
        inline AVCodecContext*& get() noexcept { return pCC_;}
        
        /// @return a pointer to the wrapped AVCodecContext
        inline const AVCodecContext* get() const noexcept { return pCC_;}
        
        /// @return a pointer to the wrapped AVCodecContext
        inline AVCodecContext* operator->() noexcept { return pCC_;}

        /// @return a pointer to the wrapped AVCodecContext
        inline const AVCodecContext* operator->() const noexcept { return pCC_;}

        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (pCC_ != nullptr); }

        /// @return true if the codec is opened
        bool isOpen() const;
        
        /// Provides a brief information string regarding the underlying codec context.
        /// @param[in] indent number of indentation tabs in the returned string
        /// @return a string with information about the codec.
        std::string info(int indent=0) const;

    };  //avtools::CodecContext
    
    /// @class Wrapper around AVCodecParameters
    class CodecParameters
    {
    private:
        AVCodecParameters* pParam_;
    public:
        /// Ctor
        /// @param[in] pCC pointer to a codec context
//        explicit CodecParameters(const AVCodecContext* pCC = nullptr);

        /// Ctor
        /// @param[in] pCC pointer to a codec context
        explicit CodecParameters(const AVCodecParameters* pParam=nullptr);

        /// Ctor
        /// @param[in] cc codec context to initialize parameters from
        CodecParameters(const CodecContext& cc);
        
        /// Copy ctor
        /// @param[in] cp codec parameters to copy
        CodecParameters(const CodecParameters& cp);
        
        /// Dtor
        ~CodecParameters();
        
        /// @return a reference to the pointer to the wrapped AVCodecParameters
        inline AVCodecParameters*& get() noexcept { return pParam_;}
        
        /// @return a const pointer to the wrapped AVCodecParameters
        inline const AVCodecParameters* get() const noexcept { return pParam_;}
        
        /// @return a pointer to the wrapped AVCodecParameters
        inline AVCodecParameters* operator->() noexcept { return pParam_;}

        /// @return a pointer to the wrapped AVCodecParameters
        inline const AVCodecParameters* operator->() const noexcept { return pParam_;}

        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (pParam_ != nullptr); }

        /// Provides a brief information string regarding the underlying codec context.
        /// @param[in] indent number of indentation tabs in the returned string
        /// @return a string with information about the codec.
        std::string info(int indent=0) const;
    };  //avtools::CodecParameters
    
    /// @class Wrapper around AVFormatContext
    class FormatContext
    {
    private:
        AVFormatContext* pCtx_;
    public:
        /// Type enum
        enum Type
        {
            INPUT = 0,
            OUTPUT = 1
        };
        
        const Type type;
        /// Ctor
        FormatContext(Type type);

        inline FormatContext(const FormatContext& fc) = delete;

        /// Dtor
        ~FormatContext();
        
        /// @return a reference to the pointer to the wrapped AVFormatContext
        inline AVFormatContext*& get() noexcept { return pCtx_;}
        
        /// @return a pointer to the wrapped AVFormatContext
        inline const AVFormatContext* get() const noexcept { return pCtx_;}
        
        /// @return a pointer to the wrapped AVFormatContext
        inline AVFormatContext* operator->() noexcept { return pCtx_;}

        /// @return a pointer to the wrapped AVFormatContext
        inline const AVFormatContext* operator->() const noexcept { return pCtx_;}

        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (pCtx_ != nullptr); }

        /// @return number of streams in the format context
        int nStreams() const noexcept;
        
        /// @return pointer to a stream in the format context
        AVStream* getStream(int n);
        
        /// @return pointer to a stream in the format context
        const AVStream* getStream(int n) const;
        
        /// Dumps info for a particular stream to stderr
        /// @param[in] n index to the stream
        void dumpStreamInfo(int n);
        
        /// Dumps info for the container stream to stderr
        /// @param[in] type INPUT for an input context, OUTPUT for an output context.
        void dumpContainerInfo();
        
        /// Returns info re: a stream in the format context
        /// @param[in] n index of the stream in the format context
        /// @param[in] indent number of indentation tabs in the returned string
        /// @param[in] isVerbose if true, more details are returned.
        /// @return a string with information about the stream.
        std::string getStreamInfo(int n, int indent=0, int isVerbose=false) const;
        
    };  //avtools::FormatContext

    /// @class Character buffer
    class CharBuf
    {
    private:
        char* p_;                                   ///< Ptr to a character buffer
    public:
        inline CharBuf(): p_(nullptr) {}            ///< Ctor
        /// Ctor that initializes a buffer
        /// @param[in] n length of buffer
        inline CharBuf(size_t n): p_((char*) av_malloc_array(n, sizeof(char))) {}
        inline CharBuf(const CharBuf& cb): p_(av_strdup(cb.get())) {};
        inline void free() {if (p_) av_freep(&p_);}
        inline ~CharBuf() {free();}    ///< Dtor
        inline char*& get() {return p_;}            ///< @return a reference to the underlying ptr
        inline const char* get() const {return p_;} ///< @return the underlying ptr
        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (p_ != nullptr); }

    };  // avtools::CharBuf

    /// @class ImageConversionContext
    class ImageConversionContext
    {
    private:
        SwsContext* pConvCtx_;                      ///< Scaling context used for image scaling and format conversion
    public:
        /// Ctor
        /// @param[in] inParam codec parameters for incoming frames
        /// @param[in] outParam codec parameters for outgoing (converted) frames
        ImageConversionContext(const AVCodecParameters& inParam, const AVCodecParameters& outParam);

        /// Ctor
        /// @param[in] inW width of incoming frames
        /// @param[in] inH height of incoming frames
        /// @param[in] inFmt pixel format of incoming frames
        /// @param[in] outW width of outgoing frames
        /// @param[in] outH height of outgoing frames
        /// @param[in] outFmt pixel format of outgoing frames
        ImageConversionContext(int inW, int inH, AVPixelFormat inFmt, int outW, int outH, AVPixelFormat outFmt);

        inline ImageConversionContext(const ImageConversionContext&) = delete;

        /// Dtor
        /// @param[in] inParam codec parameters for incoming frames
        /// @param[in] outParam codec parameters for outgoing (converted) frames
        ~ImageConversionContext();

        inline SwsContext*& get() {return pConvCtx_;}            ///< @return a reference to the underlying ptr
        inline const SwsContext* get() const {return pConvCtx_;} ///< @return the underlying ptr
        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (pConvCtx_ != nullptr); }

        /// Converts an input video frame to an output video frame with the same format this context was initialized with
        /// @param[in] inFrame input video frame
        /// @param[in] outFrame output video frame
        void convert(const Frame& inFrame, Frame& outFrame);

    };  // avtools::ImageConversionContext

    // Template definitions for Dictionary

    /// Retrieve value at the key as an integer
    /// @param[in] key dictionary key
    /// @return the value for the given key as an integer
    /// @throw runtime_error if there is a problem retrieving the value or parsing it (they are internally stored as strings)
    template<>
    inline int Dictionary::at<int>(const std::string& key) const
    {
        return std::stoi(this->operator[](key));
    }

    /// Retrieve value at the key as a string, same as [key]
    /// @param[in] key dictionary key
    /// @return the value for the given key as a string
    /// @throw runtime_error if there is a problem retrieving the value
    template<>
    inline std::string Dictionary::at<std::string>(const std::string& key) const
    {
        return this->operator[](key);
    }

    template<>
    inline TimeType Dictionary::at<TimeType>(const std::string& key) const
    {
        return std::stol(this->operator[](key));
    }

    template<>
    inline AVRational Dictionary::at<AVRational>(const std::string& key) const
    {
        std::string value = this->operator[](key);
        TimeBaseType parsedVal;
        int ret = av_parse_video_rate(&parsedVal, value.c_str());
        if (ret < 0)
        {
            throw MediaError("Unable to parse " + value + " to a rational number", ret);
        }
        return parsedVal;
    }

    template<>
    inline std::pair<int,int> Dictionary::at<std::pair<int,int>>(const std::string& key) const
    {
        std::string value = this->operator[](key);
        std::pair<int,int> parsedVal;
        int ret = av_parse_video_size(&parsedVal.first, &parsedVal.second, key.c_str());
        if (ret < 0)
        {
            throw MediaError("Unable to parse " + value + " to a pair if integers separated by x", ret);
        }
        return parsedVal;
    }

    template<>
    inline AVPixelFormat Dictionary::at<AVPixelFormat>(const std::string& key) const
    {
        return av_get_pix_fmt(this->operator[](key).c_str());
    }


} //::avtools

#endif /* LibAVWrappers_hpp */
