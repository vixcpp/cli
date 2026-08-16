# `vix install` compatibility contract

Vix guarantees the Git-hosted CMake dependency models covered by these offline
contracts: repositories consumable with `add_subdirectory()` which expose valid
CMake targets and describe their own usage requirements.

Covered: exact tag/branch/revision locking, cache materialization and no-op
reuse; header-only, static, shared, alias and interface targets; nested source
directories; public CMake options; generated headers; C++ compile features;
and transitive include, compile-definition and link requirements.

Not guaranteed: non-CMake build systems, arbitrary custom generators, and
repositories whose required dependency graph is not expressible by CMake target
properties. Real-world network matrices are deliberately separate from this
offline release contract.
