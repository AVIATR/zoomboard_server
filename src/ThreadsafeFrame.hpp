//
//  ThreadsafeFrame.hpp
//  zoomboard_server
//
//  Created by Ender Tekin on 4/12/19.
//

#ifndef ThreadsafeFrame_hpp
#define ThreadsafeFrame_hpp

#include "LibAVWrappers.hpp"
#include <shared_mutex>
#include <condition_variable>

namespace avtools
{
    /// @class Thread-safe frame wrapper
    /// See https://stackoverflow.com/questions/39516416/using-weak-ptr-to-implement-the-observer-pattern
    /// This frame employes a single writer/multiple reader paradigm
    /// Only one thread can write at a time, using update()
    /// Multiple threads can read at the same time, accessing the underlying frame via get()
    /// Threads subscribe to this frame via subscribe(). When a new frame is available (After an update()),
    /// all subscribers are called with notify() to give them access to the underlying frame.
    /// Also see https://en.cppreference.com/w/cpp/thread/shared_mutex for single writer/multiple reader example
    class ThreadsafeFrame: public avtools::Frame, std::enable_shared_from_this<ThreadsafeFrame>
    {
    private:
        SwsContext* pConvCtx_;                                              ///< Image conversion context used if the update images are different than the declared frame dimensions or format

        /// Ctor
        /// @param[in] width width of the frame
        /// @param[in] height of the frame
        /// @param[in] format frame format
        ThreadsafeFrame(int width, int height, AVPixelFormat format);
        mutable std::shared_timed_mutex mutex;                              ///< Mutex used for single writer/multiple reader access TODO: Update to c++17 and std::shared_mutex
    public:
        typedef std::shared_lock<std::shared_timed_mutex> read_lock_t;      ///< Read lock type, allows for multi-threaded read
        typedef std::unique_lock<std::shared_timed_mutex> write_lock_t;     ///< Write lock type, only one thread can write

        mutable std::condition_variable_any cv;                             ///< Condition variable to let observers know when a new frame has arrived
        /// Dtor
        virtual ~ThreadsafeFrame();
        /// Updates the frame in a thread-safe manner
        /// @param[in] frame new frame that will replace the existing frame
        void update(const avtools::Frame& frame);

        /// @return a read lock, blocks until it is acquired
        inline read_lock_t getReadLock() const { return read_lock_t(mutex); }
        /// @return an attempted read lock. The receiver must test to see if the returned lock is acquired
        inline read_lock_t tryReadLock() const { return read_lock_t(mutex, std::try_to_lock); }
        /// @return a write lock, blocks until it is acquired
        inline write_lock_t getWriteLock() { return write_lock_t(mutex); }
        /// @return an attempted write lock. The receiver must test to see if the returned lock is acquired
        inline write_lock_t tryWriteLock() { return write_lock_t(mutex, std::try_to_lock); }

        /// Factory method
        /// @param[in] width width of the frame
        /// @param[in] height of the frame
        /// @param[in] format frame format
        /// @return a shared pointer to an instance of ThreadsafeFrame
        inline static std::shared_ptr<ThreadsafeFrame> Get(int width, int height, AVPixelFormat fmt)
        {
            return std::shared_ptr<ThreadsafeFrame>(new ThreadsafeFrame(width, height, fmt));
        }

        /// @return a shared pointer to this object
        inline std::shared_ptr<ThreadsafeFrame> getPtr()
        {
            return shared_from_this();
        }

        /// @return a shared pointer to this object
        inline std::shared_ptr<const ThreadsafeFrame> getPtr() const
        {
            return shared_from_this();
        }
    }; //::<anon>::ThreadsafeFrame

}   //::avtools
#endif /* ThreadsafeFrame_hpp */
