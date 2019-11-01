//
//  ThreadManager.cpp
//  zoomboard_server
//
//  Created by Ender Tekin on 8/7/19.
//

#include "ThreadManager.hpp"
#include <iostream>
#include <log4cxx/logger.h>
#include <cassert>

namespace
{
    auto logger = log4cxx::Logger::getLogger("zoombrd");

    void logNestedException(const std::exception& e)
    {
        LOG4CXX_ERROR(logger, e.what() << ":");
        try
        {
            std::rethrow_if_nested(e);
        }
        catch (const std::exception& nested)
        {
            logNestedException(nested);
        }
    }
}   //::<anon>

// ---------------------------
// Program Status Definitions
// ---------------------------
ThreadManager::ThreadManager():
mutex_(),
doEnd_(false),
exceptions_()
{
}

ThreadManager::~ThreadManager()
{
    end();
    join();
    // Log any outstanding exceptions
    std::lock_guard<std::mutex> lk(mutex_); //probably don't need this since all threads have joined
    for (auto const& e : exceptions_)
    {
        try
        {
            std::rethrow_exception(e);
        }
        catch (const std::exception& err)
        {
            logNestedException(err);
        }
    }
    exceptions_.clear();

}

void ThreadManager::end() noexcept
{
    doEnd_.store(true);
}

bool ThreadManager::isEnded() const noexcept
{
    return doEnd_.load();
}

void ThreadManager::addThread(std::thread &&thread)
{
    threads_.push_back(std::move(thread));
}

void ThreadManager::addException(std::exception_ptr errPtr)
{
    std::lock_guard<std::mutex> lk(mutex_);
    assert(errPtr);
    LOG4CXX_ERROR(logger, "Adding exception from " << std::this_thread::get_id());
    exceptions_.push_back(errPtr);

//    std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
//    if (lk.owns_lock())
//    {
//        exceptions_.push_back(errPtr);
//    }
}

bool ThreadManager::hasExceptions() const noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    return !exceptions_.empty();
}

void ThreadManager::join()
{
    // Wait for threads to end
    for (auto& thread: threads_)
    {
        if (thread.joinable())
        {
            LOG4CXX_DEBUG(logger, "Joining thread " << thread.get_id());
            thread.join();  //wait for all threads to finish
        }
    }
    threads_.clear();
}
