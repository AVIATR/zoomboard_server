//
//  ThreadManager.cpp
//  zoomboard_server
//
//  Created by Ender Tekin on 8/7/19.
//

#include "ThreadManager.hpp"
#include <iostream>
#include <log4cxx/logger.h>

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
    auto logger = log4cxx::Logger::getLogger("zoombrd");
    LOG4CXX_DEBUG(logger, "Thread Manager received end signal");

    end();
    join();
    // Log any outstanding exceptions
    logExceptions();
}

void ThreadManager::end()
{
    doEnd_.store(true);
}

bool ThreadManager::isEnded() const
{
    return doEnd_.load();
}

void ThreadManager::addThread(std::thread &&thread)
{
    threads_.push_back(std::move(thread));
}

void ThreadManager::addException(std::exception_ptr errPtr)
{
    LOG4CXX_DEBUG(log4cxx::Logger::getLogger("zoombrd"), "Adding exception from " << std::this_thread::get_id());
    std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
    if (lk.owns_lock())
    {
        exceptions_.push_back(errPtr);
    }
}

bool ThreadManager::hasExceptions() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return !exceptions_.empty();
}

void ThreadManager::logExceptions()
{
    std::lock_guard<std::mutex> lk(mutex_);
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

void ThreadManager::join()
{
//    std::lock_guard<std::mutex> lk(mutex_);
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
