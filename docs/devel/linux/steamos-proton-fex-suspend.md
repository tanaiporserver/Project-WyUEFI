# Proton/FEX Cooperative Suspend and Game Freeze

Documents a startup freeze encountered when launching *Genshin Impact* via ARM64 Proton on SteamOS ARM64, along with the `STEAM_FEX_MULTIBLOCK=0` workaround. The issue is not caused by game files and does not vary across Proton/FEX versions.

## Symptoms

When launched through the official miHoYo launcher, the log stalls at:

```text
GameManager CreateNewGameWorld Home
Home LoadScene
```

The screen renders the health advisory or user agreement page, but does not advance automatically and buttons do not respond. One thread in the process continuously consumes roughly one CPU core while most other threads wait.

Tested on SteamOS ARM64 with Proton Experimental (ARM64) bleeding-edge `2610-25` and *Genshin Impact* 7.1.0 (CN server). The freeze reproduces across GE-Proton11-11, Proton 11.0-2e, and Proton Experimental, and swapping FEX versions does not change the outcome.

## Runtime State

Inspecting with `eu-stack` and `/proc/<pid>/task/<tid>/syscall` shows:

* The process main thread is blocked in host-side `NtSuspendThread`, waiting on a Wine in-process synchronization object;
* Several other threads are parked in `usr1_handler → wait_suspend` (already suspended);
* One thread continuously executes inside the FEX JIT region with its PC staying within the same JIT basic block.

The guest entry point of this spinning thread is `YuanShen.exe + 0x1693540` (image base `0x140000000`). The loop spans `+0x16936b0` to `+0x1693741`, with its core being:

```asm
lock cmpxchg dword ptr [rbx+0x28], ecx
...
pause
cmp eax, r13d            ; r13d = 0xffffffd6 (-42)
```

`-42` is the sentinel indicating that the lock is held by another thread. The thread retries inside this basic block until the flag changes.

## Root Cause

Thread suspension under Wine's ARM64EC is cooperative. When `usr1_handler` in `dlls/ntdll/unix/signal_arm64.c` sees that the current thread is emulating x86 code, it issues `SELECT_COOPERATIVE_SUSPEND` to wineserver; the target thread must then respond at a safepoint.

FEX emits its suspend check via `EmitSuspendInterruptCheck()` at the entry of a JIT basic block: it reads `CpuStateFrame::SuspendDoorbell` and executes `brk #0xCAFE` when non-zero. This lives in `FEXCore/Source/Interface/Core/JIT/JIT.cpp`.

If guest code loops entirely inside a single basic block (such as the spin-wait on the lock above), execution never returns to the block entry and the doorbell is never checked. As a result:

1. The game calls `SuspendThread` after loading the scene;
2. The main thread waits for the target thread to enter the suspended state;
3. The target thread spins inside the JIT block and never checks the doorbell;
4. `NtSuspendThread` never returns, freezing the main thread and the entire game.

Switching Proton or FEX versions therefore does not change the behavior. The freeze stems from the combination of cooperative suspend and an intra-block spin loop, not game assets or graphics drivers.

## Workaround

Set the following in the Proton environment used to launch the game:

```sh
STEAM_FEX_MULTIBLOCK=0
```

Proton writes this into `proton-fex-config.json` under the game's compatdata directory:

```json
{ "Config": { "Multiblock": "0" }, "ThunksDB": {} }
```

With multiblock disabled, loop back-edges are no longer merged into the same JIT block, so execution passes through block entries where the doorbell is checked and suspension completes. In testing, the game advances past `Home LoadScene`, displays the user agreement, and enters the game world.

This workaround changes only Proton/FEX runtime configuration and does not modify game files, the launcher, or anti-cheat components.

When launching from Steam, set the launch options to:

```text
STEAM_FEX_MULTIBLOCK=0 %command%
```

The environment variable is inherited by the launcher process and applies to `YuanShen.exe` spawned by the launcher as well. Since 2022, Steam parses `%command%` and leading environment variable assignments for non-Steam shortcuts too. When invoking `proton run` directly, export the variable before the command.

After launch, check whether `proton-fex-config.json` under the shortcut's compatdata contains `"Multiblock": "0"`; if `Config` is still empty, the variable was not passed to Proton.

Note that `STEAM_COMPAT_FEX_CONFIG` rewrites both `TSOEnabled` and `Multiblock`, disabling TSO as well if `TSOEnabled:1` is omitted; use `STEAM_FEX_MULTIBLOCK` when adjusting multiblock alone.

## Trade-offs and Verification

Disabling multiblock shrinks JIT basic blocks and increases block dispatch overhead, trading a small amount of performance for correctness. In current testing the game enters the world normally; frame rates and long-session stability have not yet been systematically benchmarked.

Verification: relaunch with the variable set. The log should proceed past `Home LoadScene` to subsequent entries such as `ProtocolDialog` and `submit shader load`, and the process should no longer show the main thread blocked in `NtSuspendThread` alongside a thread spinning at 100% inside a JIT block.

## References

* Wine `dlls/ntdll/unix/signal_arm64.c`: cooperative suspend branch in `usr1_handler`
* Wine `server/thread.c`: `stop_thread()` and `suspend_thread()`
* FEX `FEXCore/Source/Interface/Core/JIT/JIT.cpp`: `EmitSuspendInterruptCheck()`
* FEX `FEXCore/include/FEXCore/Core/CoreState.h`: `CpuStateFrame::SuspendDoorbell`
