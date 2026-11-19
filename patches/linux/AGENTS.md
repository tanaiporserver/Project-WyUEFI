# Downstream Linux Kernel Patch Maintenance Requirements

All Linux kernel patches are categorized, ordered, and sequentially numbered as follows. Within the same category, order patches by dependency. Necessary upstream prerequisite patches required by an upstream patch must be backported together and placed before the patch that depends on them; do not rely on subsequent local patches to satisfy their build prerequisites.

1. Patches accepted into Linux mainline, a stable/LTS maintenance tree, or the corresponding subsystem maintainer's integration tree use the filename format `00xx-UPSTREAM-TITLE.patch`. Include a `Link:` tag pointing to the upstream commit with its full commit hash, and preserve any existing relevant links. Accepted status must be backed by an actual integration commit; `Reviewed-by`, `Acked-by`, or a reply intending to apply the patch does not count as accepted.
2. Patches posted to Linux-related mailing lists but not yet accepted upstream use the filename format `00xx-MAILLIST-TITLE.patch`. Include a `Link:` tag pointing to the exact mailing list message of the version used—preferring a `lore.kernel.org` Message-ID link—and do not use search results pages or links pointing only to the cover letter.
3. Patches from other project repositories use the filename format `00xx-EXTERNAL-TITLE.patch`. Include a `Link:` tag pointing to the specific commit in the original repository; do not use mutable branch links.
4. Patches authored by this project that have not yet been submitted or accepted upstream use the filename format `00xx-LOCAL-TITLE.patch`.

Category prefixes are used only in patch filenames and must not appear in commit subjects. Third-party patches retain their original commit subjects; patches authored by this project follow the subject format and conventions of the corresponding Linux subsystem.

Patch maintenance follows these rules:

1. If any part of a downstream fix already has an equivalent implementation accepted into mainline, a stable/LTS maintenance tree, or the corresponding subsystem maintainer's integration tree, and the target kernel does not yet contain it, you must adopt the official upstream implementation instead of maintaining an unnecessary parallel implementation. Necessary backport adjustments for the target kernel are permitted, but the rationale and differences from upstream must be documented. Any remaining issues to be fixed on top of it must be placed in separate downstream patches. \
   When maintaining a downstream branch based on a stable or LTS release, prefer the official backport already present in that stable branch, treating it as the direct upstream commit for this backport while preserving traceability to the original mainline commit. Only when no backport exists in the corresponding stable branch—or when its backport is incompatible with tree-specific code or features—should you evaluate adapting the original mainline commit. \
   Before backporting a single commit, inspect preceding patches and related fixes in its patch series and include the series as a whole when judging applicability and dependencies. Pull in only necessary prerequisite dependencies; do not pull in unrelated refactorings, formatting changes, or feature changes just to avoid context conflicts.

2. When backporting, inspect modified functions, structs, and their call contexts in both the source and target branches to check whether differences affect patch semantics. When function names, parameters, or interfaces change, do not merely perform mechanical renaming; verify parameter semantics, return values, error paths, locking, and resource lifetimes, and check the implementation and behavior of called APIs along the relevant call chains. \
   When modifying function interfaces, inline functions, macros, or structs in shared headers, audit all users across the entire target kernel source tree, including vendor or downstream code that does not exist or is no longer maintained upstream. Check every call site, field access, initialization, and piece of code depending on struct layout, and update affected users together.

3. When a similar implementation already exists on a mailing list or in another repository, evaluate reusing it first and add separate downstream patches as needed. Decide whether to adopt an existing implementation based on correctness, mainline direction, target design, and dependency cost—not mechanically by diff size. \
   Do not force-port an implementation that is incompatible with the target design. When writing an alternative implementation, explain the concrete reasons for not adopting the existing one. Whenever third-party code is reused, preserve its provenance and author attribution; heavy refactoring does not turn it into purely original work.

4. All third-party patches must preserve the original author, original `Signed-off-by` tags, and required copyright and license notices. Do not re-attribute someone else's patch to a project author, and do not add a `Signed-off-by` on behalf of an original author who did not provide one. \
   Backporters or adapters who can satisfy the DCO may add their own `Signed-off-by` and note the adjustments made. Do not squash a multi-author patch series into an aggregate patch attributed to a single author.

5. Each patch must contain a single logical change, split according to Linux mainline and subsystem conventions. Large amounts of code should trigger a split review, but do not split mechanically by line count. \
   The main body of a new driver or module may be large, yet Device Tree bindings, driver code, Device Tree updates, and independent features should still be split sensibly. "Adding a new module" is not an exception for mixing multiple unrelated changes. The patch series must remain buildable at every step without introducing known breakage, making regression tracking straightforward.

6. Defects in the original functionality of patches authored by this project that have not yet been accepted upstream must be fixed directly in the original patch; stacking "patches that fix local patches" is prohibited. Independent new features should be split separately. \
   Patches already submitted to a mailing list should form a new version with version traceability preserved when revised. Defects in commits already accepted upstream must be addressed with standalone fixes carrying a proper `Fixes:` tag. \
   Modifying an existing `.patch` file still produces a normal new commit in this project repository; this rule does not require rewriting published Git history.

7. Update patch categories as their status changes. Once a project patch is posted to a mailing list, move it to `MAILLIST`; once officially accepted, adopt the official upstream version and move it to `UPSTREAM`. Each patch must match its referenced version, with necessary backport adaptations explicitly documented. \
   When upgrading the kernel baseline, drop patches already included in the baseline or superseded by official upstream implementations, and re-check downstream dependencies. Re-evaluate patches if their source commit is reverted, replaced, or its applicability changes—do not retain a patch permanently just because it was once accepted.

8. Patch descriptions must completely describe the problem, impact, rationale, provenance, and verification method; do not rely solely on external links to explain necessity. Each patch must apply cleanly on top of the declared kernel baseline and preceding patches. \
   When per-patch compile verification is needed, use the target configuration and toolchain to compile only the actually affected objects via Kbuild instead of rebuilding the entire kernel for every patch. The compile scope must include objects indirectly affected by header, interface, or struct changes, not just files directly modified by the patch. \
   For changes affecting cross-directory interfaces, symbol exports, Kconfig, or build dependencies, add the relevant configuration and necessary link checks; once the patch series is complete, perform a full build with the target configuration and run on-device verification as needed.

---

This document follows [Cryolitia's guidelines](https://gist.github.com/Cryolitia/c6580a12ed5981d5ee64fa70f98a2125/8471dcb30e0eca6ac8c3555cb41a90c50ceb3723). For project goals, parallel work division, and delivery rules, see the [root AGENTS.md](../../AGENTS.md).
