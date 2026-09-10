#!/usr/bin/env python3
"""Exercise real Finder/Launch Services events with a recording viewer.

Run on macOS with a logged-in GUI session:
    python3 debug-scripts/test-macos-app.py
No /Applications installation or privacy grants are changed.
"""
import json
import os
from pathlib import Path
import plistlib
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(args, check=True)


def wait_for(predicate, message):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(0.1)
    raise AssertionError(message)


def main():
    (ROOT / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="macos-app-", dir=ROOT / "tmp") as directory:
        work = Path(directory)
        bundle = work / "Zv Test.app"
        macos = bundle / "Contents/MacOS"
        macos.mkdir(parents=True)
        # A native recorder lets us check exact argv and direct parentage.
        recorder = work / "viewer with spaces & 日本語"
        source = work / "recorder.m"
        source.write_text(r'''
#import <Foundation/Foundation.h>
#include <unistd.h>
int main(int argc, const char **argv) {
    @autoreleasepool {
        NSMutableArray *args = [NSMutableArray array];
        for (int i = 1; i < argc; i++) [args addObject:@(argv[i])];
        NSDictionary *record = @{@"args": args, @"pid": @(getpid()), @"parent": @(getppid())};
        NSString *directory = [@(argv[0]) stringByDeletingLastPathComponent];
        NSString *path = [directory stringByAppendingPathComponent:[NSString stringWithFormat:@"%d.json", getpid()]];
        [[NSJSONSerialization dataWithJSONObject:record options:0 error:NULL] writeToFile:path atomically:YES];
        sleep(60);
    }
}
''')
        run("/usr/bin/xcrun", "--sdk", "macosx", "clang", "-fobjc-arc", "-framework", "Foundation", str(source), "-o", str(recorder))
        run("/usr/bin/xcrun", "--sdk", "macosx", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Wno-unused-parameter", "-framework", "AppKit", str(ROOT / "src/macos_app/launcher.m"), "-o", str(macos / "Zv"))
        info = plistlib.loads((ROOT / "src/macos_app/Info.plist").read_bytes())
        info.update(CFBundleIdentifier=f"com.nburrus.zv.smoketest.{os.getpid()}", ZvExecutable=str(recorder), CFBundleShortVersionString="0.2.0")
        (bundle / "Contents/Info.plist").write_bytes(plistlib.dumps(info))
        run("/usr/bin/codesign", "--force", "--sign", "-", str(bundle))

        def records():
            return [json.loads(p.read_text()) for p in work.glob("*.json")]

        first = work / "-日本語 & ' image.png"
        second = work / "two images.png"
        third = work / "later.png"
        for path in (first, second, third):
            path.touch()
        try:
            run("/usr/bin/open", "-n", "-a", str(bundle), str(first), str(second))
            initial = wait_for(records, "initial Finder opening did not launch a viewer")
            time.sleep(0.5)
            assert len(records()) == 1, "initial file opening also created an empty viewer"
            assert initial[0]["args"] == ["--", str(first), str(second)], initial
            parent = initial[0]["parent"]
            command = subprocess.check_output(["/bin/ps", "-p", str(parent), "-o", "comm="], text=True).strip()
            assert command == str(macos / "Zv"), command
            run("/usr/bin/open", "-a", str(bundle), str(third))
            wait_for(lambda: len(records()) == 2, "subsequent Finder opening was lost")
            later = [r for r in records() if r["pid"] != initial[0]["pid"]][0]
            assert later["args"] == ["--", str(third)], later
            assert later["parent"] == parent, "repeat opening should use the living launcher"
            run("/usr/bin/open", "-n", str(bundle))
            wait_for(lambda: len(records()) == 3, "plain launch did not open an empty viewer")
            assert sum(r["args"] == ["--"] for r in records()) == 1
            print("PASS: initial multi-file open, exact argv, direct parentage, repeat open, plain launch")
        finally:
            parents = set()
            for record in records():
                parents.add(record["parent"])
                try:
                    os.kill(record["pid"], signal.SIGTERM)
                except ProcessLookupError:
                    pass
            for parent in parents:
                wait_for(lambda parent=parent: subprocess.run(["/bin/kill", "-0", str(parent)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0, "launcher did not quit when its viewers exited")
            subprocess.run(["/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister", "-u", str(bundle)], check=False)


if __name__ == "__main__":
    main()
