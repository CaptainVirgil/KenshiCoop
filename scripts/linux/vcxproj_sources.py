#!/usr/bin/env python3
"""List the ClCompile sources of a .vcxproj for one configuration.

The vcxproj is authoritative for the source list on BOTH build paths, exclusions
included. `test/Scenario*.cpp` and `game/EngineProbe.cpp` carry

    <ExcludedFromBuild Condition="'$(Configuration)'=='Release'">true</ExcludedFromBuild>

and the Release DLL is expected not to contain them -- EngineProbe in particular
installs raw-RVA detour trampolines that have no business in a player build. A
shell glob over Include= attributes silently ignores that and produces a Release
DLL that differs from the MSBuild one, which is the bug this exists to prevent.

    usage: vcxproj_sources.py <project.vcxproj> <Configuration> [Platform]

Paths are emitted project-relative with forward slashes, one per line.
"""
import re
import sys
import xml.etree.ElementTree as ET

MSBUILD_NS = "{http://schemas.microsoft.com/developer/msbuild/2003}"


def condition_matches(condition, config, platform):
    """True if an MSBuild Condition holds for this configuration/platform.

    Only the equality forms the project actually uses are understood; anything
    else raises rather than being silently treated as inactive, so a future
    project edit fails loudly here instead of quietly changing what ships.
    """
    if not condition:
        return True

    expanded = (condition
                .replace("$(Configuration)", config)
                .replace("$(Platform)", platform))

    match = re.fullmatch(r"\s*'([^']*)'\s*==\s*'([^']*)'\s*", expanded)
    if match:
        return match.group(1) == match.group(2)

    match = re.fullmatch(r"\s*'([^']*)'\s*!=\s*'([^']*)'\s*", expanded)
    if match:
        return match.group(1) != match.group(2)

    raise SystemExit("vcxproj_sources.py: unsupported Condition: %s" % condition)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    project, config = sys.argv[1], sys.argv[2]
    platform = sys.argv[3] if len(sys.argv) > 3 else "x64"

    root = ET.parse(project).getroot()

    for item in root.iter(MSBUILD_NS + "ClCompile"):
        include = item.get("Include")
        if not include:
            continue  # ItemDefinitionGroup ClCompile: settings, not a source

        excluded = False
        for flag in item.findall(MSBUILD_NS + "ExcludedFromBuild"):
            if not condition_matches(flag.get("Condition"), config, platform):
                continue
            excluded = (flag.text or "").strip().lower() == "true"
        if excluded:
            continue

        print(include.replace("\\", "/"))


if __name__ == "__main__":
    main()
