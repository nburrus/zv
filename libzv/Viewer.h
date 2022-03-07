//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <memory>
#include <functional>

namespace zv
{

class Viewer
{
public:
    Viewer();
    ~Viewer();
    
    // Call it once per app, calls glfwInit, etc.
    // Sets up the callback on keyboard flags
    // Sets up the hotkeys
    bool initialize ();
    
    void runOnce ();
    
    void helpRequested ();
       
    void shutdown ();
    
private:
    void onDisplayLinkUpdated ();
    
private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
