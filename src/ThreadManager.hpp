//
//  ThreadManager.hpp
//  zoomboard_server
//
//  Created by Ender Tekin on 8/7/19.
//

#ifndef ThreadManager_hpp
#define ThreadManager_hpp

#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>
#include <atomic>

/// @class Synchronization thread to signal end and capture exceptions from threads
class ThreadManager
{
private:
    mutable std::mutex mutex_;                      ///< mutex to use when adding exceptions
    std::atomic_bool doEnd_;                        ///< set to true to signal program should end
    std::vector<std::exception_ptr> exceptions_;    ///< list of stored exceptions
    std::vector<std::thread> threads_;              ///< list of running threads
public:
    /// Ctor
    ThreadManager();
    /// Dtor
    ~ThreadManager();
    /// Used to signal the program to end.
    void end() noexcept;
    /// @return true if the program has been signaled to end
    bool isEnded() const noexcept;
    /// Adds a thread to the list of managed threads
    /// @param[in] thread new thread to add.
    void addThread(std::thread&& thread);
    /// Used to log an exception from a thread. This also signals program end
    /// @param[in] an exception pointer. This will be stored and later logged at program end
    void addException(std::exception_ptr errPtr);
    /// @return true if there are logged exceptions
    bool hasExceptions() const noexcept;
    /// Signals for program to end and waits for threads to complete
    /// @return true if exited cleanly, false if there were exceptions logged
    void join();
};  //::<anon>::ThreadManager

#endif /* ThreadManager_hpp */
