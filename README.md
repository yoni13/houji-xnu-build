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
