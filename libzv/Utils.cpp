//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Utils.h"

#include <libzv/Platform.h>

#include <sstream>
#include <chrono>
#include <cstdarg>
#include <thread>
#include <iostream>
#include <algorithm>
#include <unordered_map>

#if PLATFORM_UNIX
// getpid
# include <sys/types.h>
# include <unistd.h>
#else
# define NOMINMAX
# include <windows.h>
#endif

#include <filesystem>
namespace fs = std::filesystem;

namespace zv
{
    std::string formatted (const char* fmt, ...)
    {
        char buf [2048];
        buf[2047] = '\0';
        va_list args;
        va_start(args, fmt);
        vsnprintf (buf, 2047, fmt, args);
        va_end (args);
        return buf;
    }

    void handle_assert_failure(const char* cond, const char* fileName, int line, const char* commentFormat, ...)
    {
        char buf [2048];
        buf[2047] = '\0';
        va_list args;
        va_start(args, commentFormat);
        vsnprintf (buf, 2047, commentFormat, args);
        va_end (args);
        
        fprintf (stderr, "ASSERT failure: %s. Condition %s failed (%s:%d)", buf, cond, fileName, line);
        abort();
    }

    void consoleMessage (const char* fmt, ...)
    {
#if PLATFORM_WINDOWS
        char buf [2048];
        buf[2047] = '\0';
        va_list args;
        va_start(args, fmt);
        vsnprintf (buf, 2047, fmt, args);
        va_end (args);
        OutputDebugString (buf);
#else
        va_list args;
        va_start(args, fmt);
        vfprintf (stderr, fmt, args);
        va_end (args);
#endif
    }
}

namespace zv
{

    double currentDateInSeconds ()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    std::string getUserId ()
    {
#if PLATFORM_UNIX
        return std::to_string (getuid ());
#else
        char buffer[256];
        DWORD n = 256;
        GetUserNameA (buffer, &n);
        return std::string (buffer, n);
#endif
    }

    void ScopeTimer :: start ()
    {
        _startTime = currentDateInSeconds();
    }
    
    void ScopeTimer :: stop ()
    {
        if (_startTime < 0)
            return;

        const auto endTime = currentDateInSeconds();
        const auto deltaTime = endTime - _startTime;
        
        fprintf(stderr, "[TIME] elasped in %s: %.1f ms\n", _label.c_str(), deltaTime*1e3);

        _startTime = -1.0;
    }

    Profiler::Profiler (const char* label)
    : _label (label)
    {
        _startTime = currentDateInSeconds();
        _lastTime = _startTime;
    }

    void Profiler::lap (const char* label)
    {
        double now = currentDateInSeconds();
        double elapsed = now - _lastTime;
        _laps += formatted(" [%s %.1fms]", label, elapsed*1e3);
        _lastTime = now;
    }

    void Profiler::stop ()
    {
        if (_startTime < 0)
            return;
        double now = currentDateInSeconds();
        double elapsed = now - _startTime;
        consoleMessage ("[PROFILER] [%s] %.1fms%s\n", _label.c_str(), elapsed*1e3, _laps.c_str());
        _startTime = -1.0;
    }

    void RateLimit::sleepIfNecessary(double targetDeltaTime)
    {
        const double nowTs = zv::currentDateInSeconds();
        const double timeToWait = targetDeltaTime - (nowTs - _lastCallTs);
        if (!std::isnan(timeToWait) && timeToWait > 0)
        {
            std::this_thread::sleep_for(std::chrono::duration<double>(timeToWait));
        }
        _lastCallTs = nowTs;
    }

    std::string currentThreadId ()
    {
        std::ostringstream ss;
        ss << std::this_thread::get_id();
        return ss.str();
    }    

} // zv

namespace zv
{

    struct WordTree
    {
        WordTree (const std::vector<fs::path>& paths)
         : paths (paths)
        {
            pathIterators.reserve (paths.size());
            pathIndices.reserve (paths.size());
        }

        const std::vector<fs::path>& paths;
        std::unordered_map<std::string, std::unique_ptr<WordTree>> edges;
        std::vector<fs::path::const_iterator> pathIterators;
        std::vector<int> pathIndices;
    
        void build ()
        {
            for (int i = 0; i < paths.size(); ++i)
            {
                pathIndices.push_back (i);
                pathIterators.push_back (--paths[i].end());
            }

            buildEdges ();
        }

        void dump (int indent = 0)
        {
            std::string indent_str = std::string(indent*4, ' ');
            fprintf (stderr, "%s[%d] ", indent_str.c_str(), (int)pathIndices.size());
            for (int i = 0; i < pathIndices.size(); ++i)
            {                
                fprintf (stderr, "%d ", pathIndices[i]);
            }
            fprintf (stderr, "\n");

            for (const auto& edgeNode: edges)
            {
                fprintf (stderr, "%s\\_ %s\n", indent_str.c_str(), edgeNode.first.c_str());
                edgeNode.second->dump (indent + 1);
            }
        }

        std::vector<std::string> buildUniquePrettyNames ()
        {
            std::vector<std::string> names (paths.size());

            // Initialize with the filename.
            for (int i = 0; i < pathIterators.size(); ++i)
            {
                const int pathIdx = pathIndices[i];
                names[pathIdx] = pathIterators[i]->string();
            }

            for (auto& edge: edges)
            {
                edge.second->buildUniquePrettyNames (names, false /* parent not skipped */);
            }
            return names;
        }

        static void prepend (std::string& s, const std::string& prefix)
        {
            s = prefix + s;
        }

        void buildUniquePrettyNames (std::vector<std::string>& names, bool parentSkipped)
        {
            // Reach the beginning of the path.
            if (edges.size() == 0)
            {
                return;
            }

            // Only one item left, no need to add the entire remaining path.
            if (pathIndices.size() == 1)
            {
                if (!parentSkipped)
                    prepend (names[pathIndices[0]], "...");
                return;
            }

            // Multiple paths, but only one edge, means all the paths share that same component.
            if (edges.size() == 1)
            {
                if (!parentSkipped)
                {
                    for (const auto& pathIdx : pathIndices)
                        prepend (names[pathIdx], ".../");
                }

                for (auto& edge: edges)
                {
                    edge.second->buildUniquePrettyNames(names, true /* parentSkipped */);
                }

                return;
            }

            // Multiple edges, need to include them guy.
            {
                for (auto& edge: edges)
                {
                    for (const auto& pathIdx : edge.second->pathIndices)
                        prepend (names[pathIdx], edge.first + "/");
                    edge.second->buildUniquePrettyNames(names, false /* parent not skipped */);
                }
            }
        }

    private:
        void buildEdges ()
        {
            for (int i = 0; i < pathIndices.size(); ++i)
            {
                auto wordIt = pathIterators[i];
                int pathIdx = pathIndices[i];
                
                if (wordIt == paths[pathIdx].begin())
                {
                    continue;
                }

                const std::string& word = wordIt->string();
                auto edge_it = edges.find (word);
                if (edge_it == edges.end())
                {
                    edge_it = edges.insert (std::make_pair(word, std::make_unique<WordTree>(paths))).first;
                }
                
                WordTree& edgeNode = *edge_it->second;
                edgeNode.pathIndices.push_back (pathIndices[i]);
                edgeNode.pathIterators.push_back (--wordIt);
            }

            for (const auto& edgeNode: edges)
            {
                edgeNode.second->buildEdges ();
            }
        }
    };

    /*
        The goal is to shorten lists of paths like
        /common/folderA/same/file1.png
        /common/folderB/same/file1.png
        /common/folderA/same/file2.png
        /common/folderB/same/file2.png

        into
        folderA/.../file1.png
        folderA/.../file2.png
        folderB/.../file1.png
        folderB/.../file2.png

        by removing redundancies while making sure the final pretty
        names are still unique.

        It works by splitting the paths into their components, and
        building a tree that progressively split the paths into smaller
        and smaller groups. Path components that do not split the groups
        are then skipped and replaced with ...
    */
    std::vector<std::string> uniquePrettyNames(const std::vector<std::string>& pathStrs)
    {
        std::vector<fs::path> paths (pathStrs.size());
        for (int i = 0; i < pathStrs.size(); ++i)
            paths[i] = pathStrs[i];

        WordTree tree (paths);
        tree.build ();
        // tree.dump ();

        return tree.buildUniquePrettyNames();
    }

} // zv