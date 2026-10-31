#pragma once

#include <string>
#include <cstdio>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

class Bytes;

/*
 * Provides logging functions for different log levels
 * Debug logging can be enabled by defining DEBUG
 */
namespace Log
{
    std::string formatBytes(const Bytes &bytes);
    std::string formatLog(std::string level, std::string message);

    inline void init()
    {
#if defined(_WIN32)
        setvbuf(stdout, nullptr, _IONBF, 0);
#else
        setlinebuf(stdout);
#endif
    }

    inline void debug(std::string message)
    {
        #ifdef DEBUG
        std::string output = formatLog("DEBUG", message);
#if defined(_WIN32)
        OutputDebugStringA(output.c_str());
#endif
        std::fputs(output.c_str(), stdout);
        #endif
    }

    template<typename... Args>
    inline void debug(std::string message, Args... args)
    {
        #ifdef DEBUG
        std::string output = formatLog("DEBUG", message);
        char buf[1024];
        snprintf(buf, sizeof(buf), output.c_str(), args...);
#if defined(_WIN32)
        OutputDebugStringA(buf);
#endif
        std::fputs(buf, stdout);
        #endif
    }

    inline void info(std::string message)
    {
        std::string output = formatLog("INFO", message);
#if defined(_WIN32)
        OutputDebugStringA(output.c_str());
#endif
        std::fputs(output.c_str(), stdout);
    }

    template<typename... Args>
    inline void info(std::string message, Args... args)
    {
        std::string output = formatLog("INFO", message);
        char buf[1024];
        snprintf(buf, sizeof(buf), output.c_str(), args...);
#if defined(_WIN32)
        OutputDebugStringA(buf);
#endif
        std::fputs(buf, stdout);
    }

    inline void error(std::string message)
    {
        std::string output = formatLog("ERROR", message);
#if defined(_WIN32)
        OutputDebugStringA(output.c_str());
#endif
        std::fputs(output.c_str(), stderr);
    }

    template<typename... Args>
    inline void error(std::string message, Args... args)
    {
        std::string output = formatLog("ERROR", message);
        char buf[1024];
        snprintf(buf, sizeof(buf), output.c_str(), args...);
#if defined(_WIN32)
        OutputDebugStringA(buf);
#endif
        std::fputs(buf, stderr);
    }
}
