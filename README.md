[![CMake Build and Test](https://github.com/nburrus/cppuserprefs/actions/workflows/cmake_build_and_test.yml/badge.svg)](https://github.com/nburrus/cppuserprefs/actions/workflows/cmake_build_and_test.yml)

# cppuserprefs

**Status:** the lib is early stage and was quickly developed for the needs of another project. Use at your own risk :)

Very simple C++ cross-platform library to store user preferences, emulating a
subset of the NSUserDefaults macOS API on all platforms.

It uses [tortellini](https://github.com/Qix-/tortellini) on Linux and Windows,
and NSUserDefaults on macOS.

It is not meant to be efficient at handling large set of preferences or complex
hierarchies of options, but it does the job for a simple cross-platform app that
just needs a few settings.
# Example

Writing:

    CppUserPrefs prefs ("MyApp");
    prefs.setBool("option1", true);
    prefs.setInt("Global", "version", 2);
    prefs.setString("Name", "MyName");
    prefs.sync(); // trigger a sync to file

Reading:

    CppUserPrefs prefs ("MyApp");
    bool option1 = prefs.getBool("option1", false /* default */);
    int version = prefs.getInt("version", -1);
    std::string name = prefs.getString("Name", "INVALID");
