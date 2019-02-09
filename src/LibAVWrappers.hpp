//
//  LibAVWrappers.hpp
//  C++ Wrappers around some common libav structures
//  rtmp_server
//
//  Created by Ender Tekin on 2/8/19.
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

namespace avtools
{
    /// @class Wrapper around AVFrame
    class Frame
    {
    private:
        AVFrame* pFrame_;                                       ///< ptr to wrapped frame
        AVMediaType type_;                                      ///< type of frame if data buffers are initialized
    public:
        /// Default ctor
        /// @throw StreamError if the constructor was unable to allocate a frame
        Frame();
        
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
        
        /// @return the type of the frame if buffers are initialized. If not, returns AVMEDIA_TYPE_UNKNOWN
        inline AVMediaType type() const noexcept {return type_;}
        
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
        /// Ctor
        Packet();
        
        /// Dtor
        ~Packet();
        
        /// @return a pointer to the wrapped AVPacket
        inline AVPacket* get() noexcept { return pPkt_;}
        
        /// @return a pointer to the wrapped AVPacket
        inline const AVPacket* get() const noexcept { return pPkt_;}
        
        /// Unreferences the data buffers references by the packet
        void unref();
    };  //avtools::Packet
    
    /// @class Wrapper around AVCodecContext
    class CodecContext
    {
    private:
        AVCodecContext* pCC_;
    public:
        /// Ctor
        /// @param[in] pointer to the codec to use
        CodecContext(const AVCodec* pCodec=nullptr);
        
        /// Dtor
        ~CodecContext();
        
        /// @return a pointer to the wrapped AVCodecContext
        inline AVCodecContext* get() noexcept { return pCC_;}
        
        /// @return a pointer to the wrapped AVCodecContext
        inline const AVCodecContext* get() const noexcept { return pCC_;}
        
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
        
        /// @return a pointer to the wrapped AVCodecParameters
        inline AVCodecParameters* get() noexcept { return pParam_;}
        
        /// @return a pointer to the wrapped AVCodecParameters
        inline const AVCodecParameters* get() const noexcept { return pParam_;}
        
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
        
        /// Ctor
        FormatContext();
        
        /// Dtor
        ~FormatContext();
        
        /// @return a pointer to the wrapped AVFormatContext
        inline AVFormatContext* get() noexcept { return pCtx_;}
        
        /// @return a pointer to the wrapped AVFormatContext
        inline const AVFormatContext* get() const noexcept { return pCtx_;}
        
        /// @return number of streams in the format context
        int nStreams() const noexcept;
        
        /// @return pointer to a stream in the format context
        AVStream* getStream(int n);
        
        /// @return pointer to a stream in the format context
        const AVStream* getStream(int n) const;
        
        /// Dumps info for a particular stream to stderr
        /// @param[in] n index to the stream
        /// @param[in] type INPUT for an input context, OUTPUT for an output context.
        void dumpStreamInfo(int n, FormatContext::Type type);
        
        /// Dumps info for the container stream to stderr
        /// @param[in] type INPUT for an input context, OUTPUT for an output context.
        void dumpContainerInfo(FormatContext::Type type);
        
        /// Returns info re: a stream in the format context
        /// @param[in] n index of the stream in the format context
        /// @param[in] indent number of indentation tabs in the returned string
        /// @param[in] isVerbose if true, more details are returned.
        /// @return a string with information about the stream.
        std::string getStreamInfo(int n, int indent=0, int isVerbose=false) const;
        
    };  //avtools::FormatContext
    
    /// @class Wrapper around AVDictionary
    class Dict
    {
    private:
        AVDictionary* pDict_;                                   ///< actual dictionary
    public:
        inline Dict(): pDict_(nullptr) {}                       ///< Ctor
        ~Dict();                                                ///< Dtor
        inline AVDictionary* get() {return pDict_;}             ///< @return pointer to the wrapepr dictionary
        inline const AVDictionary* get() const {return pDict_;} ///< @return pointer to the wrapepr dictionary
        
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
        
        /// Clones a dictionary. Any entries in this dictionary are lost.
        /// @param[in] dict source dictionary
        Dict& operator=(const Dict& dict);
    };  //avtools::Dict
} //::avtools

#endif /* LibAVWrappers_hpp */
