#include "CppUserPrefs.h"

#include <iostream>

int main ()
{
    {
        CppUserPrefs prefs ("MyApp");
        prefs.setBool("Option1", true);
        prefs.setInt("Version", 2);
        prefs.setString("Name", "MyUserName");
        prefs.sync();
    }

    {
        CppUserPrefs prefs ("MyApp");
        bool option1 = prefs.getBool ("Option1", false);
        if (option1 != true)
        {
            std::cerr << "ERROR: option1 is wrong" << std::endl;
            return 1;
        }

        int version = prefs.getInt("Version");
        if (version != 2)
        {
            std::cerr << "FAIL: version is wrong, got " << version << std::endl;
            return 1;
        }

        std::string name = prefs.getString("Name", "NoNameWasSet");
        if (name != "MyUserName")
        {
            std::cerr << "FAIL: name is wrong, got " << name << std::endl;
            return 1;
        }
    }
}
