# libicwmp_dm

Shared data model engine for icwmp, with BDK, MTK and UCI SDK backends.
Source renamed from `public/libs/libtr098/libtr098` in A1. The build continues to
produce **libtr098.so** with the existing ABI and package dependency.

- Autotools root: this `src/` directory. BDK wrappers are one level above it.
- Public headers: `<icwmp_dm/...>`. `tools/install-headers.sh` also installs
  `<libtr098/...>` forwarding headers for existing consumers.
- Configure: `./tools/sdk-scan.sh`, `autoreconf -fi`, `./configure --with-sdk=<sdk>`.
- SDK selection/pruning: `tools/sdk-prune.sh`. Model-independent pruning is **not
  implemented yet**. TR-181 still shares services with TR-098 in this baseline.
- A1 changes paths and packaging integration only. Model names, persisted paths,
  getter/setter behavior and library symbols are unchanged.

Both app and library require a clean SDK build after the source/include rename.
A1 has static validation only until the BDK and MTK builds and board tests pass.
