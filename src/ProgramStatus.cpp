//
//  ProgramStatus.cpp
//  zoomboard_server
//
//  Created by Ender Tekin on 8/7/19.
//

#include "ProgramStatus.hpp"
#include <iostream>

namespace
{
    void logNestedException(log4cxx::LoggerPtr logger, const std::exception& e)
    {
        if (logger)
        {
            LOG4CXX_ERROR(logger, e.what() << ":");
        }
        else
        {
            std::cerr << e.what() << "\n";
        }
        try
        {
            std::rethrow_if_nested(e);
        }
        catch (const std::exception& nested)
        {
            logNestedException(logger, nested);
        }
    }
}   //::<anon>

// ---------------------------
// Program Status Definitions
// ---------------------------
ProgramStatus::ProgramStatus(log4cxx::LoggerPtr logger):
mutex_(),
doEnd_(false),
exceptions_(),
logger_(logger)
{
}

ProgramStatus::~ProgramStatus()
{
    if (logger_)
    {
        logExceptions();
    }
}

void ProgramStatus::end()
{
    doEnd_.store(true);
}

bool ProgramStatus::isEnded() const
{
    return doEnd_.load();
}

void ProgramStatus::addException(std::exception_ptr errPtr)
{
    std::lock_guard<std::mutex> lk(mutex_);
    exceptions_.push_back(errPtr);
}

bool ProgramStatus::hasExceptions() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return !exceptions_.empty();
}

void ProgramStatus::logExceptions()
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
            logNestedException(logger_, err);
        }
    }
    exceptions_.clear();
}
