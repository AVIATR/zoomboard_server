#ifndef LOG_HPP
#define LOG_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <cxxabi.h>

#ifdef _WIN32
#include <windows.h>  // for WinAPI
#define _NO_OLDNAMES  // for MinGW compatibility
#endif //_WIN32


template<typename T1, typename T2>
std::ostream& operator<<(std::ostream& stream, const std::pair<T1,T2>& val)
{
    return (stream << "(" << val.first << "," << val.second << ")");
}

namespace logging
{
	enum SeverityType
	{
        none = 0,   //this is just standard output
        error,      //errors
        warning,    //warnings
		debug      //debugging information
	};
    
    namespace TerminalFormatting
    {
        static const char BOLD[] = "\x1b[1m";
        static const char UNDERLINE[] = "\x1b[4m";
        static const char RESET[] = "\x1b[0m";

        static const char DEFAULT_FG[] = "\x1b[39m";
        static const char BLACK_FG[] = "\x1b[30m";
        static const char RED_FG[] = "\x1b[31m";
        static const char GREEN_FG[] = "\x1b[32m";
        static const char YELLOW_FG[] = "\x1b[33m";
        static const char BLUE_FG[] = "\x1b[34m";
        static const char WHITE_FG[] = "\x1b[37m";
        
        static const char DEFAULT_BG[] = "\x1b[49m";
        static const char BLACK_BG[] = "\x1b[40m";
        static const char RED_BG[] = "\x1b[41m";
        static const char GREEN_BG[] = "\x1b[42m";
        static const char YELLOW_BG[] = "\x1b[43m";
        static const char BLUE_BG[] = "\x1b[44m";
        static const char WHITE_BG[] = "\x1b[47m";
    }
    
    enum AnsiColor
    {
        BLACK = 0,
        BLUE,
        GREEN,
        CYAN,
        RED,
        MAGENTA,
        BROWN,
        GREY,
        DARK_GRAY,
        LIGHT_BLUE,
        LIGHT_GREEN,
        LIGHT_CYAN,
        LIGHT_RED,
        LIGHT_MAGENTA,
        YELLOW,
        WHITE
    };
    
    enum Format
    {
        BOLD = 0,
        UNDERLINE = 1,
        RESET = 2
    };
    
    namespace detail
    {
#if defined(_WIN32)
        inline void setFGColor(std::ostream& stream, AnsiColor color)
        {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, (WORD)color);
        }
        
        inline void setFormat(std::ostream& stream, Format format)
        {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, (WORD) AnsiColor::GREY);
        }
        
        inline void resetFormat(std::ostream& stream)
        {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, (WORD) AnsiColor::GREY);
        }
#else
        inline std::string getFGAnsiColorText(AnsiColor color)
        {
            switch (color)
            {
                case BLACK:     return TerminalFormatting::BLACK_FG;
                case BLUE:      return TerminalFormatting::BLUE_FG;
                case GREEN:     return TerminalFormatting::GREEN_FG;
                case YELLOW:    return TerminalFormatting::YELLOW_FG;
                case RED:       return TerminalFormatting::RED_FG;
                case WHITE:     return TerminalFormatting::WHITE_FG;
                default:        return TerminalFormatting::DEFAULT_FG;
            }
        }
        
        inline std::string getBGAnsiColorText(AnsiColor color)
        {
            switch (color)
            {
                case BLACK:     return TerminalFormatting::BLACK_BG;
                case BLUE:      return TerminalFormatting::BLUE_BG;
                case GREEN:     return TerminalFormatting::GREEN_BG;
                case YELLOW:    return TerminalFormatting::YELLOW_BG;
                case RED:       return TerminalFormatting::RED_BG;
                case WHITE:     return TerminalFormatting::WHITE_BG;
                default:        return TerminalFormatting::DEFAULT_BG;
            }
        }
        
        inline std::string getFormatText(Format format)
        {
            switch (format)
            {
                case BOLD:      return TerminalFormatting::BOLD;
                case UNDERLINE: return TerminalFormatting::UNDERLINE;
                case RESET:     return TerminalFormatting::RESET;
            }
        }
        
        inline void setFGColor(std::ostream& stream, AnsiColor color)
        {
            stream << getFGAnsiColorText(color);
        }
        
        inline void setFormat(std::ostream& stream, Format format)
        {
            stream << getFormatText(format);
        }

        inline void resetFormat(std::ostream& stream)
        {
            setFormat(stream, Format::RESET);
        }
#endif

        inline void write(std::ostream& stream)
        {
            stream << std::endl;
        }

        template<typename T, typename... Rest>
        void write(std::ostream& stream, T&& val, Rest&&... rest)
        {
            stream << std::forward<T>(val);
            write(stream, std::forward<Rest>(rest)...);
        }

    }   //::logging::detail

    inline std::string makeBoldText(const std::string& string)
    {
        return detail::getFormatText(BOLD) + string + detail::getFormatText(RESET);
    }
    
    inline std::string makeUnderlinedText(const std::string& string)
    {
        return detail::getFormatText(UNDERLINE) + string + detail::getFormatText(RESET);
    }

    /// @class Logger functor
    template<SeverityType severity>
    struct Logger;

    template<>
    struct Logger<SeverityType::none>
    {
        template<typename... Args>
        static void Log(const Args&... args)
        {
            detail::write(std::cerr, args...);
        }
    };
    template<>
    struct Logger<SeverityType::warning>
    {
        template<typename... Args>
        static void Log(const Args&... args)
        {
            detail::setFGColor(std::cerr, AnsiColor::YELLOW);
            detail::setFormat(std::cerr, Format::BOLD);
            detail::write(std::cerr, args...);
            detail::resetFormat(std::cerr);
        }
    };
    
    template<>
    struct Logger<SeverityType::error>
    {
        template<typename... Args>
        static void Log(const Args&... args)
        {
            detail::setFGColor(std::cerr, AnsiColor::RED);
            detail::setFormat(std::cerr, Format::BOLD);
            detail::write(std::cerr, args...);
            detail::resetFormat(std::cerr);
        }
    };
    
    template<>
    struct Logger<SeverityType::debug>
    {
        template<typename... Args>
        static void Log(const Args&... args)
        {
            detail::write(std::cout, args...);
        }
    };
    
    template<class T>
    std::string getTypeName(const T& _=std::declval<T>())
    {
        std::string typeName;
        int status = 0;
        char* demangledName = abi::__cxa_demangle(typeid(T).name(), 0, 0, &status);
        typeName = demangledName;
        free(demangledName);
        return typeName;
    }

    static const char LINE_SINGLE [] = "--------------------------------------------------------------";
    static const char LINE_DOUBLE [] = "==============================================================";
    
    static const int MAX_EXCEPTION_LEVEL = 10;
    
    /// Prints set of nested exceptions
    /// @param[in] e exception to print. may contain nested exceptions
    /// @param[in] level. Outermost exception is level 0.
    template<int level=0>
    void print_exception(const std::exception& e)
    {
        logging::Logger< logging::SeverityType::error >::Log(std::string(level, ' '), "Exception: ", e.what() );
        try {
            std::rethrow_if_nested(e);
        } catch(const std::exception& err) {
            print_exception<level+1>(err);
        } catch(...) {}
    }
    
    template<>
    inline void print_exception<MAX_EXCEPTION_LEVEL>(const std::exception& err)
    {
        logging::Logger< logging::SeverityType::error >::Log( "Reached maximum exception level (", MAX_EXCEPTION_LEVEL, ")." );
    }
}   //::logging

// NDEBUG => LOG, LOGW, LOGE -> term, LOGV, LOGD -> null



#define LOG logging::Logger< logging::SeverityType::none >::Log
#define LOGE logging::Logger< logging::SeverityType::error >::Log

// Code outputs warnings unless IGNORE_WARNINGS is defined
#ifdef IGNORE_WARNINGS
#define LOGW(...)
#else
#define LOGW logging::Logger< logging::SeverityType::warning >::Log
#endif

//Release code does not have any debug or verbose-level log messages
#ifdef NDEBUG
#define LOGD(...)
#else
#define LOGD logging::Logger< logging::SeverityType::debug >::Log
#endif

//Indicate that a function is not yet implemented
#define LOGX do {\
    LOGE(__func__, " is not yet implemented"); \
    exit(EXIT_FAILURE); \
    } while(false)


#endif  //LOG_HPP
