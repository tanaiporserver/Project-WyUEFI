# Project SunUEFI: AI Collaboration Guidelines
# **IMPORTANT**: Every agent must read and follow these guidelines in full unless your user explicitly instructs otherwise. Failing to do so violates assistant service standards.

This document defines what outcomes the project wants, which hard boundaries must never be crossed, and which approaches the maintainer has already rejected.

The maintainer's requests express intent, not literal acceptance checklists. Completing a request to the letter while producing an outcome that makes the maintainer frown means you misunderstood the requirement.

Before reading the rest of this file, remember these three rules—they take precedence over every detailed rule that follows:

1. **Write for a human.** The maintainer is a single person sitting in front of the tablet who has not seen your internal reasoning. They have to read every sentence you write; whenever something is unclear and forces them to stop and ask, you are wasting their time.
2. **Verify as soon as you finish.** If the tablet is connected, test directly on the device—do not use "not authorized yet" as an excuse to avoid taking action.
3. **Cite a source for every restriction.** If you cannot point to this document, something the maintainer explicitly said, or a concrete risk, just do the work instead of announcing "I won't do this."

These three rules are repeated at the end of this file. Whenever any detailed rule in between conflicts with them, these three rules win.

## Project Goal

Make Piano behave like a normal Linux laptop, not just make SunUEFI's prebuilt images boot.
On any mainstream Linux distribution, installing the matching kernel, firmware, and necessary distro-native packages should allow the standard desktop and applications to use the hardware through standard interfaces. Prebuilt images may preinstall that same set of packages and ship sensible desktop defaults, but hardware capabilities must not depend on custom rootfs-assembly scripts, resident daemons, virtual cameras, or bespoke control interfaces.

You may modify Linux, the Device Tree, libcamera, and genuinely broken standard dependencies, and deliver patches through our own signed package repository before upstream merges them. Pick the right layer by responsibility: drivers expose hardware capabilities, libcamera orchestrates cameras, and the desktop/logind handle power policy. The goal is not to avoid replacing any packages, nor to push all logic into the kernel, nor to reimplement old helper gestures.

Before each batch of work, state what problem the user hits, why it belongs at this layer, and which on-device result proves it works. If an approach technically satisfies the rules without improving the actual user experience, change the approach. Hold the data and boot safety boundaries; make all other technical trade-offs autonomously, verify them, and move to the next batch.

## Four Questions Before Acting

1. **Who will see this?** End users, the maintainer, or only logs? Picture their reaction the very first time they see it.
2. **Does the repository already have something that solves this?** Reuse what exists instead of building a parallel mechanism. Before modifying a subsystem, read the applicable parent and local `AGENTS.md` files and follow their ownership boundaries and constraints.
3. **What new things exist in the repository when you finish?** Files, names, flags, scripts, docs—every addition needs a clear reason. Less is better, and removing dead weight along the way is even better.
4. **If something goes wrong, does the user have a way out?** They must be able to return to Android, understand what happened, and know what to do next.

If following instructions literally would produce a result nobody wants, say so directly and propose an alternative. Do not implement the bad version and explain afterward, and do not implement both versions.

For product-level decisions where you are unsure (user-visible names, defaults, flows), state your choice and reasoning, then keep moving—the maintainer can ask for changes if they disagree. Only stop to ask when a decision meets all three criteria: it changes user-visible behavior, has multiple reasonable options, and is hard to reverse. Never ask questions that reading the code or documentation can answer.

Do not add anything nobody asked for: disclaimers, extra entry points, extra toggles, extra notices, or extra documentation pages.

## 1. User Experience

* **Defaults are the product.** It should work out of the box: animations enabled, Chinese UI, Chromium, default account, and working Bluetooth. Almost anything users would otherwise have to turn on manually should be enabled by default.
* **One concept, one entry point.** One real quad-channel speaker, one microphone. Internal backends and debug entry points must not appear in user-facing UIs.
* **Keep copy honest, short, and user-facing.** Labels must accurately describe what they do—a label like "Return to Android (Reboot)" that only performs a reboot misleads users. "Boot Linux" comes first and is selected by default, with no internal suffixes like `Piano`, `Stable`, or `(SunUEFI)`. In system info, use model codename `piano` and chassis type `tablet`. Name storage to leave room for Windows—for example, "dedicated storage partition", not "Linux partition".
* **Speak plain language on failure.** Error screens and logs must clearly state which step failed, the likely cause, and what the user can do (such as holding the power button to return to Android), with status codes appended only as supplemental info. A screen showing only `MENU failed 7545ms 0x000...07` leaves both users and maintainers unable to act. Long waits must show progress feedback rather than leaving the user staring at a blank gray or white screen.
* **Leave an escape hatch before touching the boot chain.** Before any change that alters the boot path, verify that a failed boot still has a working path back to Android (holding the power button, or pressing Volume Up in the early selector to pick Android).
* **Follow the existing design language.** The Boot Helper has three full pages—Boot, Storage, and Maintenance—with left-side navigation in landscape mode and the pixel sun logo. Place new features in the appropriate existing page; do not alter the layout, add hierarchy levels, change button semantics, or invent new icons and monogram logos.
* **Design for a touch device.** This is a 3200×2136, 3:2 tablet: buttons and touch targets must be large enough, type hierarchy must be clear, and desktop-sized micro-controls must be avoided. Inspect actual rendered output for UI and boot-screen changes (exported BMPs via `fastboot oem screenshot` or previews)—a clean build does not prove the visuals are right.
* **Never pass debugging context onto end users.** Landing pages and user docs must never contain phrases like "to be verified", "theoretically usable", "currently uncertain", or "pending confirmation from X". If it works, state that it works; if there is an impactful limitation, state its concrete consequence.
* **Every piece of copy a human might see must follow these rules.** Text is written for real people—think carefully about who might read anything you write (passersby, the project maintainer, technical or non-technical users), what perspective they bring, and how they will react (unless the text is strictly agent-to-agent). Especially when communicating in Chinese, if you are unsure whether something you wrote or are about to post (such as an issue or PR summary) might put people off (e.g., robotic phrasing, awkward or confusing wording, heavy AI tone), ask a model with strong copywriting ability (preferring Gemini, Claude, or DeepSeek) to review whether your draft fits the audience and why.

## 2. Repository & Code Structure

**One mechanism per problem.** To track the A/B slot, read the platform's existing GPT active-slot attributes instead of creating another state file. To hide internal audio devices, solve it in PipeWire instead of adding virtual entries. Put hardware capabilities in the kernel and Device Tree behind standard interfaces (BlueZ, ALSA/UCM, IIO/SensorProxy, DRM/KMS, UPower, libinput, logind) so a clean distro works without our custom scripts.

**Align hardware enablement with mainline Linux first.** Use mainline device models, bindings, and userspace interfaces. Fix generic software bugs in their respective upstreams. New interfaces and matching userspace implementations not yet accepted upstream are transitional measures and do not count as completed standard-interface enablement.

**Honest capability synthesis is allowed; faking capabilities is not.** For a fixed internal panel without an EDID, the driver or firmware may synthesize a valid standard capability descriptor such as an EDID for generic desktop compatibility; "not read directly from the panel" is not by itself a reason to forbid it. The prerequisites are: the model matches, fields are backed by hardware and stock documentation, declared modes match actual output processing, and mode switching, recovery, and error paths are verified on real hardware. Equivalent high bit depth achieved via FRC / temporal dithering is a valid enablement target and does not require the panel to natively accept that many bits; however, you must distinguish native bit depth, transport bit depth, and equivalent bit depth, and must not populate link bit-depth fields like `max bpc` directly with an equivalent value. PQ content may also be converted inside the driver into the panel's native encoding rather than requiring the bare panel to accept PQ directly. Never advertise unimplemented or unverified processing capabilities just to make a UI toggle appear; treat unsupported or undocumented fields conservatively. Implementation and developer docs must state honestly that the descriptor is synthesized rather than pretending the panel supplied it directly.

**Single entry point.** `./build.sh` is the entry point for building, packaging, and checking. Prefer adding new functionality as flags or subcommands of existing scripts. Any new script must be wired into `./build.sh`, CI, or tests; do not add standalone scripts with no entry point or callers.

**Interactive and scripted invocations do the same thing.** Whenever you add an interactive flow, also provide non-interactive flags expressing the same choices. When running non-interactively, report exactly which flag is missing instead of silently guessing a value. Reuse shared flag definitions instead of shadowing them in subcommands; honor unified output settings for machine-readable output, color, and progress display.

**Put things where they belong.**

* Put new subsystems in their own directory following `uefi/components/<name>/` instead of dumping more flat files into `uefi/core/`. When five or more files share a prefix, they belong in a directory.
* Place new scripts in an existing subdirectory (`tools/build/`, `tools/desktop/`) if one fits; otherwise create a purpose-named subdirectory instead of adding more files to the `tools/` root.
* Never give a new directory the same name as an existing top-level directory (historically `tools/android` collided with `android/` and `tools/patches` collided with `patches/`; do not repeat that).
* Linux patches must follow the [kernel patch maintenance requirements](patches/linux/AGENTS.md). Split other patches by independent logical change as well; keep only one copy of a given fix, maintained by upstream version range rather than duplicated per distro. Adapt across versions by rebasing, and delete backport patches once the baseline includes the fix. Do not create pseudo-version directories for rolling-release distro snapshots.
* Name tests after the module they test (`test_<module>.py`, `<Module>Test.c`). Delete tests when removing the feature. Do not mix helper files in with test files.
* Never commit generated outputs (`build/`, `artifacts/`, generated platform directories).

**Names describe what they do.** One term means one concept—do not reuse the same word across different modules, runtime stages, or storage formats with different meanings. When adding a prefix or suffix to an existing root makes the meaning clear, do not invent a synonym; when responsibilities differ, name the concrete responsibility. Do not use user-facing UI labels directly as internal type or storage field names. Never include development stages, test numbers, or status words: `test85`, `candidate`, `draft`, `current`, `new`, `final`, `v2`. Avoid invented metaphors like `Lease`, `Observer`, `Owner`, `Producer`, or `Contract`—write straightforward names like "function that reads X" or "module managing X's lifecycle".

**Code and comments each do their own job.** Follow each module's existing formatter; do not cram long initializer lists or complex branches onto a single line. Choose names so call sites are self-explanatory; if comments end up translating code line by line, rename or refactor first. Comments explain preconditions, outcomes, edge cases, and non-obvious rationale—explain any given fact in one place only. Describe only the current implementation; do not keep a changelog of past fixes in comments (that belongs in Git history). Write English comments as short, direct technical sentences; avoid vague abbreviations and rhetorical flourishes.

**Keep output at the operational boundary.** Internal modules return results and errors; the CLI command or UI decides how to present them. Do not build parallel logging and reporting APIs for the same event. Log key operations, stages, decision reasons, state transitions, and outcomes; put high-frequency details in debug output. Report unrecovered errors centrally at the entry point, and only log success or recovery after the operation has actually succeeded.

**Record versions and sources in one place.** Keep upstream commits, firmware hashes, and similar pins in `.gitmodules`, `sources.lock.json`, and `config/*.json`—never hardcode 40-character commit hashes inside scripts.

**Keep experiments out of product paths.** Never turn a one-off captured machine state into a hardcoded rule, and never let an unconfigured optional feature block firmware boot. When an optional resource is missing, continue with read-only storage or defaults.

**Clean up when finished.** Once a feature is complete, remove its scaffolding in the same pass: obsolete scripts, tests for retired profiles, stale `.gitignore` entries, and outdated docs. Unadopted drafts do not belong on the main branch—keep them in branches or Issues. Working artifacts (test numbers, capture analyses, one-off reports) do not belong in the repository. Maintain a single UEFI core rather than leaving a profile per experiment; if there is no concrete reason to keep something, delete it so neither users nor the maintainer suffer unnecessary cognitive load.

**Delete what your change supersedes by default.** Do not keep old code paths around for marginal compatibility. If your change leaves scripts, patches, tests, docs, or build references without callers, delete them in the same change and update all references. Never leave "just in case" compatibility wrappers or commented-out code, and never create `old/`, `legacy/`, or `backup/` directories. Adding new code while leaving the superseded path behind means the task is incomplete. Verify with search and builds that no references remain before deleting. Do not use a feature change as an excuse to sweep unrelated legacy parts of the repo—do cleanup passes separately. Stop and confirm first before anything that touches user data, Android partitions, or irreversible operations.

**Keep upstream clean.** Pin `upstream/` to clean upstream commits; keep local changes in `patches/`, `uefi/`, and build tools, and apply patches onto a copy in `build/firmware-workspace/` during product builds.

**Do repository cleanups separately.** The repository still carries historical baggage—do not imitate it, and do not bundle cleanup into feature changes:

* `tools/` and `uefi/core/` have over a hundred flat files, some no longer called.
* `patches/linux/` has older patches in its root directory, and `drafts/` landed on the main branch.
* Some pins are still written inside scripts.
* Committed template platforms (`pianoPkg`, `pianoProbePkg`) coexist with generated platforms.

When cleanup is needed, make a dedicated move/delete-only commit: use `git mv`, update all references in that same commit (`build.sh`, `.github/workflows/` path filters, tests, docs, `.gitignore`), and mix in zero behavioral changes.

## 3. Hard Boundaries

* Critical stock UFS partitions remain read-only in code over standard Block I/O. SunUEFI Fastboot only flashes the dedicated `sunuefi_esp` and `sunuefi_root` partitions. UEFI variables are written only to `persist/sunuefi/nvram-a.bin`, `persist/sunuefi/nvram-b.bin`, and required filesystem metadata. Dirty volumes are recovered by Android; UEFI mounts them read-only.
  There is one exception for power-off charging: on the USB-attached Linux power-off path only, Linux sets the little-endian 32-bit `power_off_mode` at offset `0x1000` of the stock `charger` partition to `1`, leaving all other bytes untouched.
* Before booting Linux, performing a software reboot, or shutting down, clocks, SMMU, and the USB controller must be safely quiesced and released, and `ExitBootServices` must complete. If DMA quiescence cannot be confirmed, retain resources and refuse handoff or reset, preserving the hardware power-button path back to Android.
* Preserve the user's Android data, existing Linux root, and installed games. Unless explicitly requested, do not reinstall, repartition, or recopy.
* Do not push or rewrite Git history unless explicitly asked.
* When reporting progress or communicating with the user during work, clearly state the current stage, status, next step, and why you are doing it. Never write empty updates like "working on X" or "finished Y", and never write "theoretically usable", "to be verified", or "pending confirmation from X". The maintainer reads your updates while looking at the tablet and has no bandwidth to guess what you left out.

## 4. Previously Rejected Approaches

When facing similar situations, follow the "Do This Instead" column directly.

| Past Approach | Problem | Do This Instead |
| --- | --- | --- |
| Requiring GPT to match a specific captured snapshot during firmware boot | UEFI must not enforce historical layouts; missing that region hangs boot | Perform only standard GPT structure, CRC, and bounds checks; check only our own range for writable areas |
| Adding a virtual audio entry to fix channels while keeping the raw entry visible | Users saw two speakers and two microphones | Expose one real entry and duplicate channels internally |
| Tracking A/B slots in `boot-request.json` | Adds an unreliable secondary source of state | Read the GPT active-slot attributes directly |
| A dozen profiles where each feature only worked in one test image | No single cohesive product | One core, one `PianoUEFI-product.img` |
| Menu items named `Boot Linux (Piano Stable)` and `Return to Android (Reboot)` | Internal jargon in names and labels that misrepresent behavior | Put "Boot Linux" first and selected by default; label actual behavior |
| Adding captions like "Photo provided by project maintainer... not intended for estimating frame rates" | Nobody asked for it; readers do not care | Show only the image and a title |
| Writing "Bluetooth headset support" or "Chromium status uncertain" on the landing page | Readers want to know what actually works | State what works; for limitations, state the concrete consequence |
| Requiring manual post-install setup for animations, browser, and default account | Poor user experience | Bake them in as defaults at build time |
| Creating one `piano-*.md` per bug | 97 fragmented files with no hierarchy | Put conclusions in the right `docs/devel/` page and investigation details in commit messages |
| CI publishing lots of miscellaneous downloads | Confuses end users | Publish the module, `fastboot boot` image, ESP, per-distro root images, and `logs.zip` |
| Fixing hardware issues in userspace scripts | Breaks on any other distro | Fix it in the kernel or Device Tree |
| Error screens showing only stage name and status code | Neither users nor maintainers can act on it | State the cause and next step; include status codes only as supplemental info |
| Keeping experiment scripts after the feature finishes | Leaves dead, uncalled scripts in `tools/` | Clean them up when finishing the feature |
| Switching kernel versions without telling the maintainer | What the maintainer sees on-device disagrees with what you report | Announce any switch affecting device state immediately |
| Saying "I won't proactively..." or "I won't do this without authorization" when nobody set that rule | The maintainer cannot tell what you are refusing or why; this project defaults to aggressive forward progress | If you cannot cite a source for the restriction, do it. If caution is your own choice, own it as your choice—never disguise it as a project restriction |

## 5. Verification

Finish the requested change first, then choose the smallest check that addresses the actual risk. Do not add tests for simple, reversible edits; do not re-run passing checks when nothing relevant changed; do not write tests that only test mocks.

When the tablet is connected, test directly on the hardware once your change is ready. Do not substitute piles of mock tests after implementing a feature, and do not stop to ask permission first—tell the user what you are about to do, and proceed unless interrupted.

To verify the user download and install experience, flash the full product image and inspect first boot rather than hot-swapping only a running kernel or service. When the maintainer explicitly asks for a clean reflash without preserving Linux data, flash the specified targets without extra backups; Android data and partition layout still remain protected by the hard boundaries.

Distinguish "passes literally" from "actually makes sense". Prefer the project's existing end-to-end verification paths; base tests on expected behavior and safety invariants rather than mirroring the current implementation in assertions, and never assume existing code is right. Once tests pass, ask yourself again: would someone holding the tablet be happy with this? Distinguish "driver registered", "compiled cleanly", and "hardware actually works"—never claim something works without evidence.

Conclusions come from source code and logs. For hardware enablement, compare against stock Android's final Device Tree, public kernel source, and live runtime state first, then reference upstream implementations, recording sources and differences. Check primary sources actively and cross-verify with disassembly and Android logs when needed. Compare behavior against stock curves, latencies, and timings on the same device, and keep sensor readings authentic.

If a change only takes effect after a reboot or re-login, reboot and verify the post-reboot behavior. Hot-patching is only for rapid debugging; final changes must land back in source code and build artifacts.

## 6. Documentation

There are three audiences: users who want to run Linux on the tablet, developers who want to build it themselves, and contributors who want to understand the implementation. Decide who a page is for before writing it. Follow the style of Asahi Linux and ArchWiki: state what it is and how to do it first, and put details later.

After writing, ask yourself: Can a user who just downloaded the image finish the task using only this page? Can someone who hasn't read our prior conversation understand this sentence? If I delete this sentence, what information does the reader lose? Is this term only intelligible to someone who read the source code?

* Lead with the conclusion or the action to take. Use prose paragraphs for connected explanations, numbered lists for ordered procedures, tables for parallel information, and `tree` for directory layouts. Do not shatter prose into fragmented bullets, and do not bold a buzzword at the start of every bullet.
* Use ordinary technical vocabulary. Replace invented concepts like "contract", "lease", "observer", "owner", or "bounded" with what the thing actually does.
* Keep commands, paths, numbers, and version strings exact and complete. What should be cut are repetitive hedging clauses and debugging narratives.
* Explain each limitation clearly in one place and link to it elsewhere. Keep useful prerequisites, examples, and recovery steps; do not parrot `--help` output or obvious source-code behavior. Debugging logs, test numbers, and chronological date logs do not belong in formal docs.
* The README is the front door—keep it short. Introductory copy can have personality; technical pages should stay plain and matter-of-fact. On a reader's first encounter with Combined BOOT, boot route memory, UEFI Fastboot, or the single UEFI core, explain what it does in one or two sentences. Write installation steps around "which button to tap"; introduce ESP as "boot image (ESP)" and root as "system image (root)" with the technical name in parentheses. State the default login `piano` / `piano123456` plainly as login instructions.
* `docs/user/` is for end users; `docs/devel/` is for developers. Leave process records in Git history and commit messages rather than creating separate archive docs. Avoid adding new files to the `docs/` root.
* Implementation docs must reflect current source code and explain module responsibilities, state, data flow, safety constraints, and failure recovery; mention internal paths and symbols only when needed for explanation or search, without exhaustive listings. Never put internal implementation pages in user installation entry points, and never create duplicate user tutorials. Whenever implementation or user behavior changes, update the corresponding docs and existing navigation/search entries in the same pass.
* Embed images directly with a title; never move or rename images in `docs/assets/gallery/`.
* `docs/user/install-from-artifact.md` is watched by CI—editing it triggers a product build. `docs/user/install-from-computer.md` is packaged as `INSTALL.md` inside release bundles and must never contain relative links.

See [`docs/devel/writing-style.md`](docs/devel/writing-style.md) for more style guidelines and examples.

## 7. Build Products

CI's Build products provide: the Android Boot Helper module, a standalone UEFI image for `fastboot boot`, a sparse ESP image shared across distros, one sparse root image per distro, and a combined `logs.zip`. Keep filenames short and free of commit hashes. There is only one UEFI core, `PianoUEFI-product.img`, shared by Combined BOOT, `fastboot boot`, and the installer.

When adding or adjusting artifacts, think from the user's perspective: filenames should reveal their purpose at a glance, downloads should contain exactly what is needed, and intermediate files, debug artifacts, and unrelated scripts must be excluded.

## 8. Git Commits & Communication

Autonomously advance work that moves the current goal forward. Do not repeatedly re-confirm existing authorization, and never present the assistant's own conservative habits as project restrictions.

**Run independent work in parallel.** Give subagents clear deliverables, owned files, and acceptance criteria. Split implementation tasks by file or use isolated worktrees; never edit the same patch list or build entry point concurrently, and never operate on the tablet or push in parallel. The primary agent continues driving the critical path and handles integration, key verification, on-device operations, commits, and pushes. Serialize only work that strictly depends on the previous step's output.

**Never frame unstated restrictions as project rules.** Any claim like "I won't proactively...", "I won't do this without authorization...", or "I am holding off on deleting this to avoid..." must cite its source: this file, something the maintainer explicitly said, or a specific concrete risk. If you cannot cite one, just do the work and skip the disclaimer. This applies equally in brand-new conversations—caution baked into model training is not a restriction of this project.

Post-compaction summaries are pointers, not substitutes for the original conversation. Whenever you are unsure about task scope, user intent, authorization, or prior conclusions—or when a summary disagrees with current evidence—read the pre-compaction user messages and assistant replies from the same session and cross-check against source code and logs. Read only the relevant message bodies rather than reloading entire tool calls, execution outputs, or reasoning blocks so you do not undo compaction, and never treat paraphrased summary lines as verbatim user quotes.

* Use Conventional Commits: `type(scope): description` (descriptions may be in Chinese). Valid types are `feat`, `fix`, `refactor`, `docs`, `test`, `build`, `ci`, and `chore`; pick `scope` by module and omit it for cross-module changes.
* The subject line must state both what changed and what capability or fix that achieves, so someone who has not seen the diff understands it in one read. Subjects that state only an action without its effect carry no information. Use plain, accessible, product-minded phrasing—keep complex debug jargon and obscure terms out of commit messages while remaining technically accurate.

  | Do Not Write | Write Instead |
  | --- | --- |
  | Read Bluetooth address from board NVM | Fix unstable Bluetooth pairing caused by failed board-NVM address reads |
  | Refactor EDID synthesis logic | Synthesize internal panel EDID so desktops detect 3200×2136 resolution |
  | Improve charging policy | Enforce stock third-party PPS input budget to prevent overcurrent charging |

  Keep commit bodies concise as well: cover only the rationale and behavioral changes that did not fit in the subject line. Never include debugging play-by-plays, "to be verified", or "theoretically". Mark breaking changes with `!` after the type and document `BREAKING CHANGE:` plus migration steps in the body.
* Scope each commit to one cohesive feature or fix, committing code, tests, and corresponding docs together—do not split commits by individual file edits and do not mix in unrelated tasks. Commit promptly once a cohesive group is complete and verified so task ownership stays clear in Git history. Cleanup commits only move and delete files. Stay on the user's specified branch. When authorized to push, push per completed feature batch—do not wait for the entire backlog to finish, and do not trigger CI on every tiny edit.
* Authorized PR and repository operations may be completed via an authenticated browser; no single tool is mandated. After pushing, check the actual remote CI build results and keep fixing if CI fails—local checks passing do not substitute for green CI.
* In conversation, lead with the result, then give rationale and impact, matching response length to task complexity. State what changed, why, how it was verified, and what limitations remain. Explicitly list every product decision you made on the maintainer's behalf (names, defaults, trade-offs) so they can easily veto any of them. Progress updates should report concrete evidence gathered so far and the next problem to solve—do not repeat "fixed".

**Write so a human can read it.** Every sentence you write is for the maintainer to read, including subagent prompts and your own progress summaries. Every single sentence in the main conversation is human-facing, with no exceptions. Only subagent tool-call payloads may omit background context, and even those must be complete sentences with proper spaces, function words, and punctuation. Compressed, space-stripped, punctuation-free shorthand is a machine-to-machine format; when the maintainer sees it in the main conversation, they have to stop and decipher it word by word.

A progress update passes the bar when the maintainer can read it without asking follow-up questions and immediately know where things are stuck, what happens next, and what finishing that step achieves. Before sending, do a quick mental check: if you handed this paragraph verbatim to someone who has not seen this conversation, which sentence would make them point and ask "What does this mean?" Rewrite that sentence before sending.

All of the examples below actually happened—people failed to understand them not because they lacked technical background, but because the sentences themselves were incomplete:

| Actually Written | Problem | Rewrite As |
| --- | --- | --- |
| 4.0.6 has no consumer | "consumer" is internal compiler jargon; readers do not know what it refers to | No code in 4.0.6 reads this field |
| Main workspace full entry hot hit measured ~2.1s each | "hot hit" is raw cache jargon | With the cache warm, each full entry-point run takes ~2.1s |
| Avoid turning a "net deletion" into a hardware failure | "net deletion" lacks context; readers do not know what is being deleted | Existing code still calls the old driver; deleting it now breaks the fingerprint sensor |
| scripts fromValve often expectsteamos | Missing spaces make it read like a run-on identifier | Valve's scripts usually assume the username is steamos |
| Do not race-edit config | Does not say who set the rule or why | The maintainer is currently editing config, so I am leaving it alone until they finish |

* Use the project's established terminology. Do not translate English jargon literally into Chinese or coin new terms: say "still referenced by code" instead of `consumer`, "build combinations" instead of raw `matrix`, and "cache hit" instead of `hot hit`. When an original English technical term is genuinely needed in Chinese prose, write the Chinese term followed by the English term in parentheses on first mention.
* Express "not verified" as which concrete step is missing and who will run it, never as a passive disclaimer. Replace "not yet verified on hardware" with "not flashed to the tablet yet; touch input can only be confirmed after flashing".
* Do not make definitive assertions when you are uncertain. Verify before stating a fact as certain; if you have not checked, say you have not checked—never declare something confidently and retract it later.
* Every refusal must cite a source. Every "won't do X" or "holding off on Y" must specify whether it comes from this document, something the maintainer said, or a concrete risk. If you cannot cite one, delete the sentence.

**Re-read the opening three rules.** Write for a human, verify as soon as you finish, and cite a source for every restriction. Whenever any rule above conflicts with these three, these three win. The time the maintainer spends deciphering your output costs far more than the time you spend writing two clear sentences.
