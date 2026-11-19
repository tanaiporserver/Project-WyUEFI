# Documentation Writing Style

This page is a reference for documentation authors and agents: how to write, which words to avoid, and how to revise prose. Before writing, read the first two sections of `README.md`, [UEFI Fastboot](../user/fastboot.md), and [Recovery](../user/recovery.md), and match their density and tone.

## Audience

The documentation serves three groups of readers: users who want to run Linux on the tablet, developers who want to build from source, and contributors who want to understand the implementation. Decide who a page is for before you start writing. `docs/user/` is for users; `docs/devel/` is for developers and contributors. Keep work-in-progress logs in Git history. Avoid adding new files directly under `docs/`.

## Writing Rules

1. Lead with the conclusion or the required action. Do not write "This document introduces/describes/aims to..." or "In order to ensure...".
2. Cover one topic per paragraph. Use prose paragraphs for connected explanations, numbered lists for sequential steps, tables for parallel options or statuses, and a `tree` layout for directory structures. Do not split a single paragraph into a string of short bullet points.
3. Reserve bold text for UI button or menu names and for a single critical warning sentence on a page. Do not bold a leading term followed by a colon at the start of every bullet point.
4. Use GitHub Alerts (`> [!WARNING]`, `> [!NOTE]`, `> [!TIP]`) for callouts, at most two per page, and restrict warnings to actions that can cause data loss, bricking, or a white screen.
5. Use only four status labels: Working, Partially working (state what is missing), Not implemented, and Unverified. When you need to distinguish between "driver loaded", "builds cleanly", and "works on hardware", say so in one sentence.
6. Document each limitation in full in only one place, and link to it from other pages. Collect all user-facing limitations in [Known Issues](../user/known-issues.md).
7. Do not include test run numbers (such as `test85`), relative time phrasing ("this round", "previously", "this run"), build directory names (such as dated `build/...` directories), or `private/` paths. When historical data is genuinely needed, write "Measured X on <date> with <version>".
8. Keep memory addresses, registers, and source paths out of user documentation unless the user has to type them. Developer documentation may include them, but explain their purpose before giving the details.
9. Briefly explain acronyms (ABL, ESP, SMMU, GDSC, FBE) in a short clause on their first appearance on each page.
10. The opening of `README.md` may carry a personal voice—like a maintainer talking plainly about what they built, without marketing slogans. Keep all other pages plain and direct.
11. Do not end pages with summaries, grand takeaways, or pleasantries. If further reading is needed, include only a list of links.
12. Keep terminology consistent. Use one name for each concept throughout the documentation: Combined BOOT, Boot Helper, UEFI Fastboot, `sunuefi_esp`, and `sunuefi_root`. In installation guides, refer to ESP as "boot image (ESP)" and root as "system image (root)", placing the technical name in parentheses.

## What to Keep

Commands, paths, numeric values, registers, offsets, SHA hashes, partition names, firmware names, and kernel versions must stay accurate and complete—they are the core value of the documentation. Cut narrative repetition, process logs, and empty emphasis instead. When unsure whether a statement still holds, check the source code and `config/` before deciding. If old documentation conflicts with the source code, follow the source code and update the documentation.

## Vocabulary

The replacements below apply to prose only. Keep code identifiers, filenames, and log strings (such as `PianoDisplayClockLease.c` and `NOT_READY`) verbatim.

| Avoid | Use instead |
| --- | --- |
| contract | State the actual constraint directly: input format, interface convention, or configuration requirement |
| lease / borrow | reference count, temporary hold, or "keep enabled" |
| observer | "reads and logs X" or "read-only check on X" |
| owner (in a lifecycle sense) | "the module that initializes and tears down X" or "code holding X" |
| producer | "code that generates X" or "module that supplies X data" |
| bounded | State the exact limit: "at most 256 bytes", "reads 64 KiB at a time" |
| handoff | "jumping to the kernel", "passing control to Linux", or "parameters passed to the kernel" |
| canonical source | "source code" or "hand-written source" (distinguish only when contrasting with generated files) |
| pinned | "fixed at commit abc1234" or "locked to version X" |
| fixture | test data |
| fence / typed / guarded | Drop the modifier and state what it does, e.g., "must not be called after ExitBootServices" |
| admission | "conditions that allow X" |
| sign-off / closed-loop / open-loop | "verified working", "not finished yet" |
| candidate | State the actual status: "draft", "off-by-default option", or "not enabled yet" |
| audit (for reading code) | "verify" or "check" |
| evidence | Name the concrete item: logs, readback values, or screenshots |
| authoritative | "X takes precedence" or "follow X" |
| fail-closed | "stop on error instead of continuing" |
| posture / surface / orchestration | Delete, or write "state / interface / boot order" |
| Stacked disclaimers ("does not imply... is not equivalent to... cannot be taken as...") | Keep the one limitation that actually matters and delete the rest |
| actual, real, strict, exact, sole (as empty emphasis) | Keep only when contrasting what is written in config with what is read from hardware |
| "at this stage", "this round", "previously" | Give an explicit date, or delete |

`owner` and `rail` (power rail) are standard terminology in parts of Linux and UEFI. The test is whether the sentence becomes easier to understand when you remove the word.

## Rewrite Examples

These examples illustrate tone and editing choices only. For actual documentation, verify facts against the source code.

### Explaining what a command does

Before: It explicitly runs product contracts, log size bounds, simulated ADB responses for RAM log collection, build freshness, DMA logs, the USB diagnostic read model, and runner self-tests. Each file displays results and skip counts; skips in portable are also treated as failures.

After: This command checks product configuration, log size limits, RAM log collection using simulated ADB, stale build outputs, DMA logs, USB diagnostic reads, and the test runner itself. The output lists pass/fail results and skip counts for each test file separately. Any skip in the `portable` group fails the run; GitHub CI runs this same command.

### Explaining what a check proves

Before: Host checks prove parsing, contracts, or simulated behavior; device conclusions separately record source/image identity, boot path, actual screen/input/data outcomes, and recovery status. Neither compilation, simulated DMA, nor protocol presence substitutes for real hardware transfers.

After: Host checks only prove that parsing and simulation logic work. To claim in a PR that a feature works on hardware, state the image version, the boot method, and what happened on the tablet—such as what appeared on screen, whether input responded, and whether you could boot back into Android.

### Describing the build process

Before: The main build pipeline is canonical source and pinned inputs → platform preparation → compilation → identity verification → packaging. Git tracking status and "whether generated by a tool" are two separate dimensions; not all tracked files can simply be treated as hand-written source.

After: The build has five steps: fetch locked versions of the source and inputs, prepare the platform, compile, verify the outputs, and package. Some files in the repository are generated by scripts; the "Source" column in the table below marks which files you can edit directly and which ones are overwritten on the next build.

### Introducing a table

Before: On-demand input reference table. A clone does not include captures, third-party checkouts, environments, or artifacts in [ignored directories]; the entries below record script requirements and do not imply that they have been rebuilt or signed off in this round.

After: A full build requires several files extracted from the stock system on your device, which are not stored in the repository. The table below lists which inputs each build target needs, where to get them, and which script checks them.

### Recording test results on hardware

Before: Deployed `build/boot-repack-recovery-20261008/roundtrip-wrapped.img` with the Recovery-first selector on 2026-10-08; normal Android boot completed. Saved Linux target2/sequence1 natively from Android and rebooted normally; confirmed g086 Linux, `/dev/sda36` ext4 rw root, and GDM/ADSP/sensors active over SSH.

After: After saving the Linux boot route in Android and rebooting, the tablet booted into Linux on internal storage. SSH confirmed a read-write ext4 root filesystem with GDM, ADSP, and sensor services running.

### Explaining a boot parameter

Before: The exact, sole `bootmonitor.bootmode=recovery` returns the unmodified stock GKI entry before reading the NEXT request page, preserves the ABL-provided DTB/initrd, and does not clear the Linux preference.

After: When the kernel command line contains `bootmonitor.bootmode=recovery`, the selector skips reading the boot request and jumps straight to the stock kernel entry. The DTB and initrd passed by ABL are kept intact, and the saved Linux boot route is not cleared.

### Explaining a memory convention

Before: The candidate design contract is occupied before EBS with no Conventional allocation; after EBS, Linux treats LoaderData+WB as System RAM while the same DT reserved-memory/CMA continues to reserve and manage it.

After: Before ExitBootServices, these two CMA regions are marked as occupied (EfiLoaderData) so UEFI will not hand them out as free memory. Once Linux boots, the kernel continues to manage them as CMA according to the Device Tree `reserved-memory` nodes. This behavior has not yet been verified in the memory map on hardware.

### Explaining remaining work

Before: The next implementation must produce two connected contracts: a cold descriptor/owner contract before the first MemoryPeim, then a live EFI allocation/CPU-source contract used by the Linux session. Current Core memory validators remain NOT_READY.

After: Two pieces are still missing: recording which component holds each DDR region during cold boot before the first `MemoryPeim` call, and checking the memory actually allocated by EFI during Linux boot. Neither is implemented yet, and the core memory checks still return `NOT_READY`.

### Writing keyboard instructions for users

Before: On the official keyboard, **Right Ctrl + `1`–`0`, `-`, `=`** map to **F1–F12**; see [Keyboard Shortcuts and UEFI Integration Progress](../user/keyboard.md).

After: The official keyboard has no function keys. Hold Right Ctrl and press the top number row instead: `1` through `0` are F1 through F10, `-` is F11, and `=` is F12. See [Keyboard Guide](../user/keyboard.md).
