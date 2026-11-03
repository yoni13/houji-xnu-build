# houji-xnu-build

CI for the Xiaomi 14 (houji) XNU kernel of [yoni13/UniXNU-houji](https://github.com/yoni13/UniXNU-houji), a fork of
[lonzzi/UniXNU-houji](https://github.com/lonzzi/UniXNU-houji). The kernel sources live in that fork; this repository only builds them.

- `.github/workflows/kernel.yml` (manual): checks out the fork at `source_ref`, prepares XNU 8796.141.3 with blacktop/darwin-xnu-build,
  builds the UniXNU Ath12kWifi module and the kernel on macOS, and uploads `kernel.development.sweet`.
- `.github/workflows/fork-checks.yml` (manual): runs the fork's own source and host checks on arm64 Linux and macOS.
- `.github/workflows/gpu.yml`: the Metal userland pieces under `gpu/`.

Inputs specific to one phone stay here, not in the fork: `fwheaders.tar.gz.enc` (GPU firmware headers generated from that phone's
partitions, encrypted; key in the `FW_KEY` secret), `retarget-layout.py` (its GPT identities), and `private/` (its log file
allocation and GPU handoff trial identity). Someone building for another phone generates their own, as described in the fork's
`docs/fork.md`.

## Build time

A source change rebuilds in about 2 min 45 s (7 min before caching):
- ccache through XNU's own `CCACHE` make hook (`KCC = $(CCACHE) $(CC)`); about 99.9 % hits on a small change. Files using
  `__DATE__`/`__TIME__` are never cached, so a booted kernel's build stamp is always that build's. A new cache entry is saved only
  when a build had 20 or more misses.
- The Wi-Fi module is cached on the Ath12kWifi revision plus the fork's patches and build script.
- The KDK is not installed: the SWEET kernel links nothing from it.
- `make -j8` on the 3-core runner: a warm build is about 4500 short I/O-bound steps (header installs, cache hits, MIG).
- The ~100 MB of compiler warnings go to `kernel-build.log.gz` in the artifact (timestamped per line), not the job log.
