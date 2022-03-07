//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <cstdio>
#include <string>
#include <cmath>

#define ZV_MULTI_STATEMENT_MACRO(X) do { X } while(0)

#ifndef NDEBUG
#define zv_dbg(...) ZV_MULTI_STATEMENT_MACRO ( zv::consoleMessage ("DEBUG: "); zv::consoleMessage (__VA_ARGS__); zv::consoleMessage ("\n"); )
#else
#define zv_dbg(...)
#endif // !DEBUG

#ifndef NDEBUG
#define dl_assert(cond, ...) ZV_MULTI_STATEMENT_MACRO ( if (!(cond)) zv::handle_assert_failure(#cond, __FILE__, __LINE__, __VA_ARGS__); else {} )
#else
#define dl_assert(...)
#endif // !DEBUG

namespace zv
{
        
    std::string formatted (const char* fmt, ...);

    void handle_assert_failure(const char* cond, const char* fileName, int line, const char* fmt, ...);

    void consoleMessage (const char* fmt, ...);
        
} // dl

namespace zv
{

    double currentDateInSeconds ();

    std::string getUserId ();
    
    struct ScopeTimer
    {
        ScopeTimer (const char* label) : _label (label)
        {
            start ();
        }
        
        ~ScopeTimer () { stop (); }
        
        void start ();
        void stop ();
        
    private:
        std::string _label;
        double _startTime = -1;
    };

    struct RateLimit
    {
    public:
        void sleepIfNecessary(double targetDeltaTime);

    private:
        double _lastCallTs = NAN;
    };

    std::string currentThreadId ();

} // dl
