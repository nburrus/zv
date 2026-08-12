//
// Copyright (c) 2021, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <memory>
#include <functional>

namespace zv
{

class Prefs
{
private:
    Prefs();
    ~Prefs();
    
public:
    static bool showHelpOnStartup();
    static void setShowHelpOnStartupEnabled (bool enabled);
    
private:
    static Prefs* instance();
    
private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
