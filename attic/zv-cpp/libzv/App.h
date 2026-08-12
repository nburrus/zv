//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <memory>
#include <functional>
#include <vector>
#include <string>

namespace zv
{

class Viewer;

class App
{
public:
    App();
    ~App();
    
    // Call it once, calls glfwInit, etc.
    bool initialize (int argc, const char *const argv[]);
    bool initialize (const std::vector<std::string>& args = {"zv"});
    
    void shutdown ();

    int numViewers () const;
    std::vector<std::string> viewerNames () const;
    
    Viewer* createViewer (const std::string& name);
    Viewer* getViewer (const std::string& name = "default");
    void removeViewer (const std::string& name);

    void updateOnce (double minDuration = 0.0);

    void run ();
   
private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
