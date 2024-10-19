# TODOs

- [ ] Migrate update::version_number to a semver-compliant version class
- [ ] Allow dynamic version prefix with the GitHub API (not just 'v')
- [ ] Migrate to a non-header-only library with meaningful CMake targets
- [ ] Copy the updater-wrapper class from Music Presence here,
    which has features like periodic update checks,
    manual update checks, thread-safety, logging and more
- [ ] Copy the launcher executable code from Music Presence here,
    which abstracts away the process of creating a launcher
    and properly implementing the manager class with it.
    This should be available as an opt-in CMake target
    which is added to the main project of the user
    and produces a launcher executable to ship with.
    Abstracting this away is very desirable, especially because:
    the launcher comes with additional DLL dependencies on Windows
    which must be copied in addition to the launcher executable.
    Determining these runtime DLLs can be cumbersome for the user
    and something like `dumpbin` or `listpedeps` should be used by CMake
    to automatically determine these
