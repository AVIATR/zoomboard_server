//
//  ProgramStatus.hpp
//  zoomboard_server
//
//  Created by Ender Tekin on 8/7/19.
//

#ifndef ProgramStatus_hpp
#define ProgramStatus_hpp

#include <mutex>
#include <vector>
#include <stdexcept>
#include <log4cxx/logger.h>

/// @class Synchronization thread to signal end and capture exceptions from threads
class ProgramStatus
{
private:
    mutable std::mutex mutex_;                      ///< mutex to use when adding exceptions
    std::atomic_bool doEnd_;                        ///< set to true to signal program should end
    std::vector<std::exception_ptr> exceptions_;    ///< list of stored exceptions
    log4cxx::LoggerPtr logger_;                     ///< Logger used for logging the stored exceptions
public:
    /// Ctor
    /// @param[in] logger logger to use when logging exceptions
    ProgramStatus( log4cxx::LoggerPtr logger = nullptr);
    /// Dtor
    ~ProgramStatus();
    /// Used to signal the program to end.
    void end();
    /// @return true if the program has been signaled to end
    bool isEnded() const;
    /// Used to log an exception from a thread. This also signals program end
    /// @param[in] an exception pointer. This will be stored and later logged at program end
    void addException(std::exception_ptr errPtr);
    /// @return true if there are logged exceptions
    bool hasExceptions() const;
    /// Logs all the exceptions. Once logged, all exceptions are cleared.
    void logExceptions();
};  //::<anon>::ProgramStatus

#endif /* ProgramStatus_hpp */
