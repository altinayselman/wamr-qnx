# wamr-qnx: the WebAssembly Micro Runtime on QNX 8.0

This repository ports the [WebAssembly Micro Runtime (WAMR)](https://github.com/bytecodealliance/wasm-micro-runtime)
to **QNX 8.0**. It also adds a QNX-native API so that a sandboxed WebAssembly module can take part in
QNX message passing: it can be a server, a client, or a periodic real-time task.

- **Demo video (2:15):** https://youtu.be/nnS_JjgbGbk
- **Upstream:** https://github.com/bytecodealliance/wasm-micro-runtime, tag `WAMR-2.4.5`. The upstream
  tree is fetched by URL; this repository does not copy it and is not a fork.
- **QNX version:** QNX SDP 8.0 (QNX OS 8.0.0, kernel build 2026/06/05), free non-commercial licence.
- **Target:** QNX 8.0 x86_64 on QEMU (image built with the SDP's `mkqnximage --type=qemu`). Results
  below come from that target.
- **Toolchain:** `qcc`/`q++` from SDP 8.0 (GCC 12) through `cmake/qnx-toolchain.cmake`, CMake + Ninja on
  a Windows 11 host. Wasm modules are built with wasi-sdk 34 and AOT-compiled with `wamrc` 2.4.5.

## Why WebAssembly on QNX

Software-defined vehicles, robots and industrial controllers increasingly need logic that is updated
after shipping: a filter, a control rule, a fleet-specific feature. Shipping that logic as native code
means re-qualifying a process that has full access to its address space and to the system.

WebAssembly adds a second isolation layer inside a process. A module sees only its own linear memory and
the functions the host hands it. QNX already isolates processes with its microkernel and makes message
passing the way components talk to each other. Combining the two gives:

- **Two walls instead of one.** A faulty or hostile module traps in the Wasm sandbox; the host process
  survives, and the host process is itself isolated by QNX.
- **IPC as the only door.** The QNX API in this port exposes message passing, pulses, timers and
  priority, and nothing else. Which names a module may use is a host-side allowlist.
- **Deterministic scheduling for sandboxed code.** A Wasm task woken by a QNX timer pulse runs at the
  pulse's priority, so it keeps its real-time priority under load (measured below).
- **One binary, many targets.** The same `.wasm` file runs unchanged on x86_64 and AArch64 QNX targets,
  in the interpreter, or as AOT-compiled native code for speed.

## What is in this repository

| Path | What it is |
|---|---|
| `patches/0001-Add-QNX-8.0-platform-support.patch` | The port itself, against `WAMR-2.4.5` |
| `patches/0002-libc-builtin-print-p-with-0x-on-QNX.patch` | `%p` in the built-in libc prints like the other platforms |
| `patches/0003-unit-tests-run-on-QNX.patch` | Lets the upstream unit tests find their data and run on QNX |
| `qnx-ipc/` | `libwamr_qnx_ipc.so`: QNX message passing for Wasm modules, loaded with `iwasm --native-lib` |
| `qnx-ipc/include/wasm_qnx_ipc.h` | Guest-side API (include it in Wasm code) |
| `qnx-ipc/include/wamr_qnx_ipc_proto.h` | Wire format shared with native QNX programs |
| `examples/sensor-filter/` | A Wasm module that is a QNX server, plus a native client (`sensor_sim`) |
| `examples/rt-cyclic/` | A Wasm control task woken every 1 ms by a QNX timer pulse |
| `examples/sandbox-trap/` | A deliberately faulty module: out-of-bounds load, store and stack overflow |
| `tests/unit-qnx/` | Builds the upstream WAMR unit tests that need no LLVM, for QNX |
| `scripts/` | Fetch and patch upstream, build for QNX, and on-target test scripts |
| `cmake/qnx-toolchain.cmake` | CMake toolchain file for `qcc` (x86_64 or aarch64le) |
| `scripts/vm/` | mkqnximage image build (with the sshd fix) and the QEMU/KVM launch line |
| `tests/probes/qnx_commit_probe.c` | Measures QNX's memory commit limit (explains the 4 failing upstream tests) |
| `results/` | Console logs from the QNX target |

## The port (what had to change for QNX)

| File (upstream tree) | Change | Why |
|---|---|---|
| `core/shared/platform/qnx/*` | New platform: `BH_PLATFORM_QNX`, the POSIX layer, `link_libraries(socket m)` | QNX keeps the BSD socket API in `libsocket`; pthreads, `dlopen` and clocks are in libc |
| `core/shared/platform/common/posix/posix_thread.c` | Native stack bounds from `__tls()->__stackaddr/__stacksize` | QNX has no `pthread_getattr_np()`. Without the boundary, WAMR cannot place stack guard pages and native stack overflow checks are off |
| `core/shared/platform/common/posix/posix_memmap.c` | Low-2 GB mappings with QNX `MAP_BELOW` instead of `MAP_32BIT` | x86-64 AOT code must be mapped below 2 GB; QNX has no `MAP_32BIT`, but its `MAP_BELOW` treats the hint as an upper bound |
| `core/shared/platform/common/posix/posix_memmap.c` | `os_mprotect()` with length 0 is a no-op on QNX | QNX fails `mprotect(len=0)` with `EINVAL`, where Linux succeeds. Every module whose memory starts at **0 pages** failed to instantiate (`allocate linear memory failed`) until this was fixed; the upstream tests found it |
| `core/shared/platform/common/posix/posix_file.c` | WASI `fd_readdir` file types via `fstatat()`; no `preadv`/`pwritev` | QNX's `struct dirent` has no `d_type`, and libc has no `preadv`/`pwritev` (the existing fallback is used) |
| `core/iwasm/libraries/libc-builtin/libc_builtin_wrapper.c` | `%p` printed as `0x...` | QNX's `printf("%p")` omits the `0x` prefix that the module (and the upstream test) expect |
| `product-mini/platforms/qnx/` | `iwasm` for QNX, symbols exported for `--native-lib` | So that native libraries such as `libwamr_qnx_ipc.so` can call the runtime API |

Hardware bounds checking (the SIGSEGV-based sandbox with guard regions) is **enabled**, for both the
interpreter and AOT code.

## The QNX API for Wasm modules

```c
#include "wasm_qnx_ipc.h"     /* compile with --target=wasm32-wasip1 */

int32_t ch = qnx_attach("wasm_filter");                /* name_attach */
int32_t kind = qnx_receive(ch, buf, sizeof buf,        /* MsgReceive  */
                           &reply, &size, &code, &value);
qnx_reply(reply, 0, &result, sizeof result);           /* MsgReply    */

int32_t co = qnx_connect("some_server");               /* name_open   */
qnx_send(co, &req, sizeof req, &resp, sizeof resp);    /* MsgSendv    */

qnx_set_priority(50);                                  /* SCHED_FIFO, capped by the host */
int32_t t = qnx_timer_pulse(ch, 1000000, 1);           /* timer_create + SIGEV_PULSE */
uint64_t now = qnx_clock_ns();                         /* CLOCK_MONOTONIC */
```

The host enforces the rules; the module cannot change them:

- `WAMR_QNX_IPC_ALLOW`: a comma-separated list of names a module may attach or connect to (`name*` is a
  prefix). Unset means nothing is allowed.
- `WAMR_QNX_MAX_PRIO`: the highest SCHED_FIFO priority a module may request (default 10).
- A module gets small table handles, never raw `chid`/`coid`/`rcvid` values. It can only reply to
  messages it received, and only send on connections it opened.
- Every guest pointer is bounds-checked against the module's linear memory before use.

## Results on QNX 8.0 (x86_64, QEMU)

All numbers come from the logs in `results/`. The VM is QEMU 8.2 with KVM, running inside WSL2 on a
Windows 11 laptop, with 2 vCPUs and 1 GB RAM. Read latency tails with that in mind (see Limitations).

**Sandbox (`scripts/target/sandbox-tests.sh`): 8 of 8 pass.** In both the interpreter and AOT, an
out-of-bounds load, an out-of-bounds store and a runaway recursion each end as a Wasm trap
(`Exception: out of bounds memory access` / `wasm operand stack overflow`). The process exits cleanly
and the system is unaffected. A well-behaved run exits 0.

**Wasm as a QNX server (`scripts/target/ipc-demo.sh`, AOT):**

- `filter.aot` registers `/dev/name/local/wasm_filter` and serves 2,000 `MsgSend` calls from the native
  `sensor_sim`.
- It flagged 21 of the 21 injected spikes.
- Round trip native → Wasm → native: median 2.9 µs (AOT) and 3.1 µs (interpreter), minimum 2.1 µs.
- With an empty allowlist, `qnx_attach` is refused (`-EPERM`) and the module exits.

**Real-time task (`scripts/target/rt-demo.sh`, AOT, 1 ms period, 5,000 activations):** jitter is
\|interval − period\|.

| Scenario (AOT) | Median jitter | p99 |
|---|---|---|
| Idle system, Wasm task at SCHED_FIFO 50 | 47 µs | 0.40 ms |
| 4 CPU hogs at priority 10, Wasm task at **50** | **27 µs** | 0.73 ms |
| 4 CPU hogs at priority 10, Wasm task at **10** | **999 µs** (a full period behind) | 7.0 ms |

The interpreter shows the same pattern (66 µs / 25 µs / 999 µs); both runs are in `results/rt-demo.txt`.

The priority the module asked for is what keeps it on time under load. One QNX detail mattered: a
thread that receives a pulse runs at the pulse's priority. The first version created the timer pulse
with an inherited priority and lost the boost (median 999 µs even at priority 50). The pulse now carries
the caller's priority.

**Upstream unit tests on the QNX target (`tests/unit-qnx`, `scripts/target/unit-tests.sh`,
`results/unit-tests.txt`): 133 of 137 pass.**

| Suite (upstream `tests/unit`) | Tests | Passed |
|---|---|---|
| `wasm-vm`, `interpreter`, `wasm-c-api`, `libc-builtin`, `shared-utils`, `tid-allocator` (wasi-threads), `gc` | 121 | **121** |
| `linear-memory-wasm` (with and without HW bounds checks) | 8 | 6 |
| `linear-memory-aot` (with and without HW bounds checks) | 8 | 6 |
| **Total** | **137** | **133** |

The 4 failures are one case seen four ways: a module that needs a **4 GiB linear memory** (65,535 initial
pages, or growing to 65,536). The target has 1 GiB of RAM, and QNX does not overcommit memory.
`tests/probes/qnx_commit_probe.c` measures this on the target (`results/qnx-commit-probe.txt`): making
even 2 GiB of private memory writable fails with `ENOMEM`, with or without `MAP_LAZY`, so these tests
pass only on Linux-style overcommit. Before the port's `mprotect` fix, the same page-count test also
failed on the five modules whose memory starts at 0 pages (`wasm_mem_page_01/05/08/10/12`).
Suites that need LLVM or wasi-sdk at build time (`aot`, `compilation`, `custom-section`,
`running-modes`, `memory64`, `shared-heap`, `runtime-common`, `aot-stack-frame`) were not built.

## Codelab: build and run it yourself

### 1. Prerequisites

- A myQNX account with the free QNX Everywhere licence, and QNX SDP 8.0 installed with QNX Software
  Center. Command line:
  `qnxsoftwarecenter_clt -installBaseline com.qnx.qnx800 -destination <dir>`.
- CMake ≥ 3.21, Ninja, Git and bash (Git Bash on Windows works).
- [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) 34 and the `wamrc` 2.4.5 binary from the
  [WAMR releases](https://github.com/bytecodealliance/wasm-micro-runtime/releases/tag/WAMR-2.4.5), used to
  build the example modules.
- QEMU with KVM for the target. This repository used QEMU 8.2 in WSL2 (Ubuntu 24.04); a Linux host works
  the same way.

### 2. Fetch and patch upstream WAMR

```sh
scripts/fetch-upstream.sh            # clones WAMR-2.4.5 into upstream/ and applies patches/
```

### 3. Build for QNX

```sh
source <sdp>/qnxsdp-env.sh           # Windows: run qnxsdp-env.bat, or export QNX_HOST/QNX_TARGET
scripts/build-qnx.sh x86_64          # iwasm, libwamr_qnx_ipc.so, sensor_sim
```

Build the Wasm modules and their AOT versions (wasi-sdk's `clang`, the `wamrc` 2.4.5 release binary):

```sh
CFLAGS="--target=wasm32-wasip1 -O2 -I qnx-ipc/include"
clang $CFLAGS -I examples/sensor-filter examples/sensor-filter/filter.c -o filter.wasm
clang $CFLAGS examples/rt-cyclic/cyclic.c -o cyclic.wasm
clang --target=wasm32-wasip1 -O1 examples/sandbox-trap/oob.c -o oob.wasm
for m in filter cyclic oob; do
  wamrc --target=x86_64 --target-abi=gnu --cpu=x86-64-v2 -o $m.aot $m.wasm
done
```

> On Windows, pass `--target-abi=gnu` to `wamrc`. Its default produces a COFF-flavoured AOT file, which
> the QNX runtime rejects with `invalid data relocation section name`.

### 4. Make a QEMU image

```sh
ssh-keygen -t ed25519 -N "" -f ~/.ssh/qnxvm            # key for root logins on the VM
scripts/vm/make-image.sh ./vm ~/.ssh/qnxvm.pub         # wraps mkqnximage --type=qemu --arch=x86_64 ...
```

On Windows, run `make-image.sh` from a shell where `qnxsdp-env.bat` has been applied, because
mkqnximage uses the SDP's own bash.

> With the current SDP 8.0 target packages, OpenSSH needs `/usr/libexec/sshd-auth`, but the standard
> mkqnximage system build does not ship it, so `sshd` refuses connections. `scripts/vm/ifs_files.custom`
> adds it to the image, and `make-image.sh` installs that snippet and rebuilds with `--build`.

Boot it under KVM (Linux, or WSL2 on Windows) with SSH forwarded to `localhost:2222`:

```sh
scripts/vm/run-qemu.sh ./vm      # qemu-system-x86_64 -enable-kvm ... -daemonize
```

### 5. Run on QNX

```sh
B=build/x86_64
SSH="-i $HOME/.ssh/qnxvm -o StrictHostKeyChecking=no"
ssh -p 2222 $SSH root@127.0.0.1 mkdir -p /data/wamr
scp -O -P 2222 $SSH $B/iwasm/iwasm $B/qnx/libwamr_qnx_ipc.so $B/qnx/sensor_sim \
    *.wasm *.aot scripts/target/*.sh root@127.0.0.1:/data/wamr/
ssh -p 2222 $SSH root@127.0.0.1
cd /data/wamr
sh sandbox-tests.sh                       # faulty modules trap, the system keeps running
sh ipc-demo.sh /data/wamr aot 2000        # Wasm server + native client over QNX messages
sh rt-demo.sh /data/wamr aot 1000 5000 50 # 1 ms Wasm control task, idle vs. CPU hogs
```

### 6. Upstream unit tests (optional)

```sh
cmake -S tests/unit-qnx -B build/unit -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake \
      -DQNX_ARCH=x86_64 -DWAMR_BUILD_TARGET=X86_64
cmake --build build/unit -- -k 0      # the post-build test discovery step fails on the host: expected,
                                      # the test binaries are QNX executables
```

Copy each suite's test binary and its data into `/data/wamr/unit/<suite>/` and run
`sh unit-tests.sh /data/wamr/unit`. The data comes from upstream: the `*.wasm` files in
`tests/unit/linear-memory-wasm/wasm_files`, `tests/unit/wasm-vm/wasm-apps/app4` and `tests/unit/gc/wasm-apps`.
The `linear-memory-aot` suite also needs `.aot` files: build them from the `.wast` files in that directory
with `wat2wasm` and `wamrc`, as its `build_aot.sh` does.

## Limitations (stated plainly)

- **Tested target:** QNX 8.0 x86_64 under QEMU only. The AArch64 build (iwasm, the IPC library and the
  examples) compiles cleanly but has not been run (`results/aarch64-build-check.txt`). Not yet run on a
  Raspberry Pi or other hardware.
- **Latency tails come from the host, not QNX.** The target is a VM inside another VM (QEMU in WSL2 on
  Windows), so millisecond tails reflect host scheduling. Medians and the priority comparison are the
  meaningful part. On real hardware the tails would be expected to be far smaller, but that was not
  measured.
- **Unit tests:** only the upstream suites that need neither LLVM (the AOT compiler library) nor
  wasi-sdk at build time are built. The AOT data for `linear-memory-aot` comes from the `wamrc` 2.4.5
  release binary and `wat2wasm` on the host. 4 of 137 fail for the memory reason above.
- **Not ported:** WAMR's LLVM JIT and Fast JIT, and the WAMR debugger (LLDB).
- **AOT and executable memory:** on this image (security policy off) AOT works for root and non-root
  users. A system that locks `PROCMGR_AID_PROT_EXEC` would need that ability for AOT; the interpreter
  needs no executable memory. This was not tested.
- **Upstream contribution:** the platform patch is written to be offered upstream. No pull request has
  been opened yet.

## Licence

Apache License 2.0, see `LICENSE`. The patches modify WAMR and are offered under WAMR's own licence
(Apache-2.0 WITH LLVM-exception), see `NOTICE`. No QNX SDP files are included.
