import os

from platformio import fs

Import("env")

platform = env.PioPlatform()
framework_roots = [
    fs.to_unix_path(platform.get_package_dir("framework-arduinoespressif32")),
    fs.to_unix_path(platform.get_package_dir("framework-arduinoespressif32-libs")),
]
is_integration_dump = env.IsIntegrationDump()


def shorten_framework_includes(env, node):
    """Shortens Arduino framework and prebuilt IDF include arguments during normal builds on Windows."""
    if is_integration_dump:
        return node

    includes = [fs.to_unix_path(include) for include in env.get("CPPPATH", [])]
    generic_includes = []
    shortened_by_root = {root: [] for root in framework_roots if root}

    for include in includes:
        matched_root = None
        for root in shortened_by_root:
            if include.startswith(root + "/") or include == root:
                matched_root = root
                break

        if matched_root:
            relative_include = fs.to_unix_path(os.path.relpath(include, matched_root))
            shortened_by_root[matched_root].append("-iwithprefix/" + relative_include)
        else:
            generic_includes.append(include)

    compact_flags = []
    for root, shortened_includes in shortened_by_root.items():
        if shortened_includes:
            compact_flags.extend(["-iprefix", root])
            compact_flags.extend(shortened_includes)

    return env.Object(
        node,
        CPPPATH=generic_includes,
        CCFLAGS=env["CCFLAGS"] + compact_flags
    )


env.AddBuildMiddleware(shorten_framework_includes)
