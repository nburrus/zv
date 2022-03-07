//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Prefs.h"

#include "CppUserPrefs.h"

namespace zv
{

struct Prefs::Impl
{
    Impl()
    : prefs("zv")
    {}
    
    CppUserPrefs prefs;
    
    struct {
        bool _showHelpOnStartup;
    } cache;
};

Prefs::Prefs()
: impl (new Impl())
{
    impl->cache._showHelpOnStartup = impl->prefs.getBool("showHelpOnStartup", true);    
}

Prefs::~Prefs() = default;

Prefs* Prefs::instance()
{
    static Prefs prefs;
    return &prefs;
}

bool Prefs::showHelpOnStartup()
{
    return instance()->impl->cache._showHelpOnStartup;
}

void Prefs::setShowHelpOnStartupEnabled (bool enabled)
{
    if (instance()->impl->cache._showHelpOnStartup == enabled)
        return;

    instance()->impl->cache._showHelpOnStartup = enabled;
    instance()->impl->prefs.setBool("showHelpOnStartup", enabled);
    instance()->impl->prefs.sync();
}

} // zv
