# EDM-015 design

Approved scope: adopt dependency tooling and qualified offline CI without product behavior or API changes.
Implementation baseline: `b1022c9297712878114ba3d827f970d7b16f3ce6`.
Umbrella checkpoint: `5438cecea111cd54de33b6bdb1bf477555707811`.
The original audit baseline remains in SPEC.md.

## Discovery and preparation

The standalone bundle and module declarations equal the canonical umbrella copies (2.0.0).
CMake applies approved-prefix lookup before the first internal package lookup: no internal
packages for Core, HolonightQt plus its transitive Config for Quick, and all six providers for the application.
External Qt >=6.11, toml++ >=3.4, shared QMarkdown >=1.1 and installed GTest >=1.14 CONFIG
use normal discovery or HOLONIGHT_EXTERNAL_PREFIX. Tests disabled do not require GTest.
The QMarkdown QML public/private metadata is checked before application construction.

Files-local command adapters retain component selection, Qt library-tool preference and
explicit tidy compiler-context selection (including the three fresh installed consumers) while leaving the canonical bundle unmodified.
Only task deps prepares supplied local source providers, including an explicitly supplied
QMARKDOWN_SOURCE. Ordinary commands validate installed artifacts. Core-only Task commands
filter all internal providers; Quick includes Config as Qt's exported transitive dependency.

## Qualified CI

Task ci:prepare pulls the qualified full Arch and immutable REUSE images, clones the six
exact published revisions, and prepares installed providers unprivileged and offline.
System Services keeps Storage enabled and Audio disabled. Local --provider-source-root
inputs are cloned at the fixed commits; dirty working-tree content is excluded.
Preparation records source, toolchain, package, options, image and artifact hashes.
Acceptance rejects stale or tampered preparation, mounts it read-only and uses
--pull=never --network=none. Incompatible preparation is archived. An intact prior provider
build may seed explicit preparation; canonical source/toolchain/options signatures must
still match before reuse. No incompatible or tampered install tree is reused.

Build and application checks run unprivileged. Namespace tests retain a separate disposable
root setup, existing seccomp/AppArmor policy and mandatory no-skip isolation coverage.
The launcher never installs host policy. Hosted disposable runner policy setup is retained.
Runtime image assembly is offline atop the same immutable qualified base, staging Files
and internal providers only. Its package-manager-installed QMarkdown remains separately
owned. Runtime fixtures mutate only disposable containers and run verification unprivileged.

## Acceptance

Preserve build/isolation/licensing lanes, task check, C.UTF-8 and en_US.UTF-8, installed
Core/Quick/QML-plugin consumers, Debug/Release, format/strict tidy, QML checks, REUSE,
staged installation, installed startup/desktop launch, permissions and search-path checks.
Focused regressions precede clean acceptance. No performance campaign, pointer automation,
publication or umbrella pin update is included.
