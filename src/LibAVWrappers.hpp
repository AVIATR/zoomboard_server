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
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
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
    /// @class Wrapper around AVFrame
    class Frame
    {
    private:
        AVFrame* pFrame_;                                       ///< ptr to wrapped frame
        AVMediaType type_;                                      ///< type of frame if data buffers are initialized
    public:
        /// Ctor that wraps around an frame
        /// @param[in] pFrame ptr to a preallocated frame, or nullptr if one should be internally allocated
        /// @param[in] type type of the data that this frame is expected to contain.
        /// @throw MediaError if there is a problem allocating a frame of the given type.
        Frame(AVFrame* pFrame=nullptr, AVMediaType type=AVMEDIA_TYPE_UNKNOWN);
        
        /// Ctor that allocates a frame according the provided codec parameters
        /// @param[in] codecPar codec parameters
        /// @throw StreamError if the constructor was unable to allocate a frame
        Frame(const AVCodecParameters& codecPar);
        
        /// Allocates a video frame with a given size, format and sample rate.
        /// Basically combines av_frame_alloc() and initVideoFrame()
        /// @param[in] width how wide the image is in pixels
        /// @param[in] height how high the image is in pixels
        /// @param[in] format pixel format
        /// @param[in] cs color space type
        /// @return pointer to a new frame, nullptr if a frame could not be allocated
        Frame(int width, int height, AVPixelFormat format, AVColorSpace cs=AVColorSpace::AVCOL_SPC_RGB);

        /// Copy ctor
        /// @param[in] frame source frame. Note that the data buffers are not cloned, but this frame refers
        /// to the same buffers as frame
        Frame(const Frame& frame);
        
        /// Dtor
        ~Frame();
        
        ///@return the raw pointer to the wrapped AVFrame
        inline AVFrame* get() noexcept { return pFrame_;}
        
        ///@return the raw pointer to the wrapped AVFrame
        inline const AVFrame* get() const noexcept { return pFrame_;}

        ///@return a chainable ptr to the AVFrame
        inline AVFrame* operator->() {return pFrame_;}

        ///@return a chainable ptr to the AVFrame
        inline const AVFrame* operator->() const {return pFrame_;}

        /// @return the type of the frame if buffers are initialized. If not, returns AVMEDIA_TYPE_UNKNOWN
        inline AVMediaType type() const noexcept {return type_;}

        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (pFrame_ != nullptr); }

        /// Copy operator. This only adds references to the source frame, does not clone the underlying data
        /// @param[in] frame source frame.
        /// @return a reference to this frame.
        Frame& operator=(const Frame& frame);
        
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
    };  //avtools::Packet
    
    /// @class Wrapper around AVDictionary
    class Dictionary
    {
    private:
        AVDictionary* pDict_;                                   ///< actual dictionary
    public:
        /// Ctor
        /// @param[in] pDict a previously allocated dictionary, or nullptr if one should be allocated
        inline Dictionary(AVDictionary* pDict=nullptr): pDict_(pDict) {};
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
        void add(const std::string& key, std::int64_t value);
        
        /// Returns the value of a key in the dictionary.
        /// @param[in] key key to search for
        /// @return value of the key
        /// @throw std::runtime_error if there was an error retrieving the value
        std::string at(const std::string& key) const;
        
        /// Returns the value of a key in the dictionary.
        /// @param[in] key key to search for
        /// @return value of the key
        /// @throw std::runtime_error if there was an error retrieving the value
        std::string operator[](const std::string& key) const;

        /// Checks to see if the dictionary has a particular key
        /// @param[in] key to check for
        /// @return true if the dictionary has this key, false otherwise.
        bool has(const std::string& key) const;
        
        /// Clones a dictionary. Any entries in this dictionary are lost.
        /// @param[in] dict source dictionary
        Dictionary& operator=(const Dictionary& dict);
    };  //avtools::Dictionary
    
    class CodecParameters;
    /// @class Wrapper around AVCodecContext
    class CodecContext
    {
    private:
        AVCodecContext* pCC_;
    public:
        /// Ctor
        /// @param[in] pCodec pointer to the codec to use
        CodecContext(const AVCodec* pCodec=nullptr);
        
        /// Ctor
        /// @param[in] param codec parameters to use
        CodecContext(const CodecParameters& param);
        
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
        CodecParameters();
        
        /// Ctor
        /// @param[in] pCC pointer to a codec context
        CodecParameters(const AVCodecContext* pCC);
        
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

        /// Clones codec parameters
        /// @param[in] cp source parameters
        CodecParameters& operator=(const CodecParameters& cp);
        
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
        inline ~CharBuf() {if (p_) av_free(p_);}    ///< Dtor
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

        /// Dtor
        /// @param[in] inParam codec parameters for incoming frames
        /// @param[in] outParam codec parameters for outgoing (converted) frames
        ~ImageConversionContext();

        inline SwsContext*& get() {return pConvCtx_;}            ///< @return a reference to the underlying ptr
        inline const SwsContext* get() const {return pConvCtx_;} ///< @return the underlying ptr
        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (pConvCtx_ != nullptr); }

    };  // avtools::ImageConversionContext
} //::avtools

#endif /* LibAVWrappers_hpp */
