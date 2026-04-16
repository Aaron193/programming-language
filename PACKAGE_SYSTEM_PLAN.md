# Mog Package System Plan

## Status

In progress.

Last updated: 2026-04-16.

Phase snapshot:

- Phase 1 local package-management baseline: mostly complete
- Phase 2 registry and publishing: partially implemented
- Phase 3 native artifact distribution: in progress
- Phase 4 enterprise and security features: not started beyond local
  `--locked` / `--offline` workflows

Phase 1 local package-management work is now substantially implemented in the
repository. The current codebase supports:

- `mog init`
- `mog add <local-package>`
- `mog install`
- `mog update`
- `mog run <file>`
- `mog validate-package <dir>`
- `--locked` installs and runs against an existing `mog.lock`
- `--offline` installs for local path/workspace dependency graphs
- generated project install metadata in `.mog/install/registry.toml`
- generated `mog.lock`
- distinct generated lockfile and install-registry schemas
- project-local package materialization under `.mog/install/packages/`
- durable cache metadata for reused local package installs, with a project-local
  cache fallback when the preferred user cache path is unavailable
- local installation for both source packages and native packages
- automatic install-on-run for projects with declared dependencies
- shared package resolution that prefers installed project metadata and falls
  back to legacy local scanning for compatibility
- workspace-root discovery for CLI/runtime/LSP package installs
- root manifest workspace metadata via `[workspace] members = [...]`
- dependency source-kind tracking and dependency-group tracking in generated
  lock/install metadata
- language-server auto-install for workspace projects with `mog.toml`
- Phase 2A static file-registry installs for published source packages using
  exact `x.y.z` versions
- Phase 2B semver-constrained published source-package installs using
  `x.y.z`, `^x.y.z`, and `~x.y.z`
- root-manifest registry configuration via `[registries.<name>]`
- registry-pinned lock/install metadata including registry identity and
  artifact digests
- cached `--offline` reinstalls for previously fetched registry source packages
- lockfile-first `mog install` / `mog run` behavior for current dependency
  graphs, with `mog update` as the explicit refresh path
- published-package support in `mog add`, including `namespace/name` and
  `namespace/name@constraint` specs
- source-package `mog publish` to the current static file-registry format,
  including exact dependency pinning and idempotent re-publish checks
- native-package `mog publish` / install through the current static
  file-registry format for the current host platform
- target-keyed native-package publish/install through the current static
  file-registry format, including `--target`, `--prefer-prebuilt`, and pinned
  selected-target metadata in `mog.lock` / `.mog/install/registry.toml`
- published native-package source artifacts and host-target source-build
  fallback through the current static file-registry format, including cached
  `--offline` reinstalls and `build_from_source` metadata
- package-manifest dependency parsing aligned with project manifests via
  `[dependencies]` inline tables, while retaining legacy compatibility for
  `dependencies = []`

Not implemented yet:

- authenticated publishing flows
- git dependency fetch/install
- alternate or networked registry transports beyond the current static
  file-registry format
- native system dependency diagnostics / build-toolchain diagnostics
- non-host source-build fallback for published native packages
- signed metadata or artifact verification
- enterprise policy workflows beyond the current local `--locked` / `--offline`
  support

This document describes the target package architecture for Mog, the required
user-facing workflows, internal invariants, security posture, and a phased
implementation plan. It is written against the current repository state, where
packages already support local installation and managed resolution, while
remote distribution, published-version dependency solving, and registry trust
features are not yet complete.

## Background

Mog already supports package-shaped imports and package metadata:

- bare imports such as `@import("window")` resolve as packages
- source-like imports such as `@import("./foo.mog")` resolve as source modules
- native packages are shared libraries loaded dynamically at runtime
- package metadata exists in `mog.toml`, `mog.lock`, `package.toml`, and
  `package.api.mog`
- official runtime-maintained packages currently use the reserved `mog:*`
  canonical package namespace internally

The current design is a strong starting point because it keeps optional runtime
features, such as SDL-backed windowing, outside the core interpreter binary.
That separation should be preserved. What is missing is a professional package
system around discovery, installation, versioning, reproducibility,
distribution, and trust.

## Goals

The package system must:

- keep the core interpreter/runtime lean
- support both source packages and native packages
- support official packages without hardwiring them into the interpreter binary
- provide deterministic dependency resolution and reproducible installs
- make lockfiles authoritative and machine-generated
- support local development, workspace packages, and published packages
- support secure remote distribution of package metadata and artifacts
- work well for CLI usage, CI, editor tooling, and future compiled workflows
- scale from hobby usage to professional multi-package projects

## Non-Goals

This plan does not require:

- embedding large optional dependencies such as SDL into the interpreter binary
- supporting arbitrary build systems in the first release
- solving cross-compilation for every platform in phase 1
- supporting decentralized trust models in the first release
- supporting dynamic dependency changes at runtime

## Design Principles

1. The interpreter is not the package manager.
The interpreter should consume resolved packages, not perform ad hoc dependency
solving during program execution.

2. Package identity must be stable and explicit.
Import names are not enough by themselves; canonical package identity must be
namespace-qualified and versioned in lockfiles and registries.

3. Source packages and native packages are first-class.
The package manager should handle both under one model, while preserving the
additional constraints of native code, ABI, platform, and toolchain.

4. Reproducibility is mandatory.
Two users with the same manifest and lockfile should get the same dependency
graph and the same package artifacts for a given target platform.

5. Security defaults matter.
Native packages can execute arbitrary code. Installation, validation, and trust
boundaries must reflect that reality.

6. Official packages remain separate deliverables.
`mog:window` and future official packages should be published and installed like
packages, not merged into the core language runtime.

## Terminology

### Import Name

The string written by users in `@import("window")`.

### Canonical Package ID

A fully qualified package identity such as `mog:window` or `acme:http`.

### Package Version

A semantic version identifying a published package release, for example
`1.4.2`.

### Package Kind

One of:

- `source`
- `native`

### Registry

A service or index that publishes package metadata, versions, checksums, and
artifact locations.

### Artifact

An installable payload associated with a package version. For source packages
this is typically a source archive. For native packages this may be a source
archive, a prebuilt binary artifact, or both.

### Lockfile

A machine-generated file recording the fully resolved dependency graph, package
versions, artifact digests, and platform-specific selections where required.

## High-Level Architecture

The package system should have five layers:

1. Project manifest layer
The root `mog.toml` declares direct dependencies, package sources, workspace
members, and project-level package settings.

2. Resolution layer
A package solver resolves direct and transitive dependencies into a complete,
versioned graph.

3. Registry and artifact layer
Registries expose package metadata and immutable artifacts. The package manager
downloads and caches those artifacts locally.

4. Build and validation layer
Source packages and source-distributed native packages may need to be built
locally. All packages, especially native ones, must be validated before use.

5. Runtime loading layer
The interpreter loads already-installed packages from the local package store.
It should not search arbitrary directories by default once the package manager
exists.

## User Experience

The package system should support the following workflows.

### Initialize a project

```bash
mog init
```

Creates:

- `mog.toml`
- project manifest only

Current implementation status:

- implemented
- does not yet scaffold source layout or workspace metadata

### Add a dependency

```bash
mog add window
mog add acme/http
mog add acme/http@^1.3
```

Behavior:

- updates `mog.toml`
- resolves local package dependencies
- updates `mog.lock`
- writes `.mog/install/registry.toml`
- validates installed packages

Current implementation status:

- implemented for local packages discoverable from repo/workspace `packages/`
- workspace members can also be discovered and written as `workspace = true`
  dependencies when declared in the root manifest
- manifest parsing now recognizes `git` and `registry` dependency fields
- `add` now supports published-package specs through configured static file
  registries, including `namespace/name` and `namespace/name@constraint`
- published source-package adds currently accept exact versions plus `^` and
  `~` semver constraints
- does not yet support git fetches, authenticated publishing, or alternate
  registry transports

### Install dependencies

```bash
mog install
```

Behavior:

- prefers the existing `mog.lock` when it is still compatible with `mog.toml`
- otherwise resolves from `mog.toml`
- rewrites `mog.lock` only when a fresh resolution is required
- rewrites `.mog/install/registry.toml`
- validates installed packages

Current implementation status:

- implemented for local path, workspace, published source packages, and
  published native packages from configured static file registries
- supports `--locked` and `--offline` for local graphs and for registry
  packages that are already cached locally
- published source-package resolution supports exact versions plus `^` and `~`
  semver constraints
- generated metadata now records registry identity, artifact path, and
  artifact digest in addition to existing manifest/API digests
- published native-package installs now select among platform-keyed native
  artifacts and fall back to local source builds when a matching prebuilt is
  unavailable on the current host target
- does not yet fetch from git/network API registries or perform non-host
  source-build fallback

### Update dependencies

```bash
mog update
mog update window
```

Behavior:

- re-resolves dependencies from `mog.toml`
- refreshes `mog.lock`
- upgrades published dependencies within the allowed semver constraints

Current implementation status:

- implemented for both local graphs and published source-package graphs
- now performs version upgrades across published source-package releases within
  supported semver constraints

### Run a program

```bash
mog run app.mog
```

Behavior:

- ensures declared local dependencies are installed before execution
- prefers the existing `mog.lock` when it is still compatible with `mog.toml`
- prefers installed project package metadata during resolution
- invokes the interpreter with the resolved package graph

Current implementation status:

- implemented
- follows the same lockfile-first install behavior as `mog install`
- legacy direct file execution (`mog file.mog`) still works as a compatibility
  path

### Publish a package

```bash
mog publish
```

Behavior:

- validates manifest and API metadata
- resolves published direct dependencies to exact pins
- copies package artifacts into the current static file-registry layout
- updates the registry index and records artifact digests

Current implementation status:

- implemented for source packages and current-host native packages published to
  configured static file registries
- rejects conflicting re-publishes of an existing `package_id@version`
- published native packages now emit both a generic source artifact and a
  host-target prebuilt artifact in the current registry format
- does not yet support authentication, hosted registries, or multi-host
  prebuilt release automation

## Package Layout

Every package should continue to have a package-local directory with explicit
metadata. The target layout should be:

```text
package-root/
  mog.toml
  package.api.mog
  src/
    main.mog
```

For native packages:

```text
package-root/
  mog.toml
  package.api.mog
  native/
    package.cpp
    CMakeLists.txt
  build/
    ...
```

The current `package.toml` file can be retired once `mog.toml` is authoritative
for package metadata, unless a strong compatibility reason remains.

## Manifest Requirements

The root project manifest and package manifest may share a file format, but
they serve different purposes.

### Required package manifest fields

- `kind = "source" | "native"`
- `namespace`
- `name`
- `version`
- `description`
- `license`

### Required for import ergonomics

- `import_name`

### Required for dependency resolution

- `[dependencies]`
- optional `[dev-dependencies]`
- optional `[build-dependencies]`

### Required for native packages

- `abi_version`
- `native.entry`
- `native.build`
- `native.targets`

### Recommended metadata

- `repository`
- `homepage`
- `documentation`
- `authors`
- `keywords`

### Native dependency metadata

Native packages must be able to declare external requirements explicitly. For
example, `mog:window` should declare SDL as a package requirement rather than
hiding it in build scripts.

Example:

```toml
[system-dependencies]
sdl2 = { version = ">=2.0.0", required = true }
```

This metadata is important for:

- install-time diagnostics
- CI automation
- package publishing validation
- future OS-specific installers

## Dependency Model

Dependencies should be version-constrained, not path-only.

Example:

```toml
[dependencies]
window = { package = "mog:window", version = "^0.2.0" }
http = { package = "acme:http", version = "^1.4.0" }
```

The manifest should also support:

- local path overrides for development
- git dependencies for advanced users
- workspace dependencies
- registry selection when more than one registry exists

### Dependency Rules

- import names are local aliases
- canonical package IDs are global identities
- lockfiles store canonical IDs, exact versions, and resolved sources
- transitive dependencies may not silently change import bindings in user code

## Lockfile Requirements

`mog.lock` should become a generated artifact owned by the package manager.

It must record:

- schema version
- generator version
- every resolved package canonical ID
- exact resolved version
- dependency edges
- artifact URLs or source identifiers
- content hashes
- package kind
- package API digest
- native ABI metadata when relevant
- target-platform selection for native artifacts

The lockfile should be:

- deterministic
- stable under re-generation
- safe to commit
- authoritative for CI and production installs

## Local Package Store

The package manager should install packages into a managed local store instead
of relying on repo-relative build directories.

Recommended locations:

- user cache for fetched immutable artifacts
- project-local install directory for resolved active artifacts

Example shape:

```text
~/.cache/mog/packages/
  registry.example/
    mog/window/0.2.1/...
    acme/http/1.4.3/...
```

The interpreter should receive explicit package roots from the package manager.
Long term, direct fallback scanning of arbitrary `packages/` directories should
be reduced or limited to explicit development modes.

## Registry Model

The system should support at least two registry classes:

- the default public Mog registry
- private enterprise registries

Each registry should provide:

- package metadata by canonical package ID
- version lists
- dependency metadata
- immutable artifact references
- checksums
- signing metadata or transparency data

A simple and practical first implementation is an index plus immutable artifact
hosting, rather than a complex stateful API.

## Source Package Distribution

Source packages should publish:

- manifest
- `package.api.mog`
- source archive
- optional docs metadata

Installation flow:

1. Resolve version from registry metadata.
2. Download source archive.
3. Verify checksum.
4. Unpack into cache/store.
5. Expose to compiler/interpreter through resolved package roots.

## Native Package Distribution

Native packages are the most important design constraint.

### Requirements

The system must support both:

- source-distributed native packages
- prebuilt native package artifacts

### Why both are needed

Source distribution is important for flexibility and early ecosystem growth.
Prebuilt artifacts are important for professional UX, especially for packages
like `window` that depend on nontrivial native dependencies.

### Artifact model

Each native package version may publish one or more artifacts keyed by:

- operating system
- CPU architecture
- ABI version
- Mog runtime compatibility range
- optional libc/toolchain constraints

Example:

```text
mog:window@0.2.1
  linux-x86_64-gnu
  macos-arm64
  macos-x86_64
```

### Native package installation flow

Preferred flow:

1. Resolve package version.
2. Attempt to download a matching prebuilt artifact.
3. Verify signature/checksum.
4. Validate exported package metadata and API.
5. Install into the local store.

Fallback flow:

1. Download source package.
2. Verify checksum.
3. Verify declared build backend and toolchain compatibility.
4. Build package in a controlled environment.
5. Validate the produced shared library.
6. Install into the local store.

## Official Packages

Official packages such as `mog:window` should be distributed through the same
package system as third-party packages, with two differences:

- the `mog` namespace remains reserved
- the project may publish official prebuilt artifacts directly alongside Mog
  releases

The interpreter binary should not embed official package code. Professional
distribution should instead look like:

- `mog` runtime package
- `mog:window` package
- future official packages such as `mog:http`, `mog:fs`, or `mog:crypto`

This preserves a small core while still allowing first-party support.

## SDL and Similar External Dependencies

Packages like `mog:window` should not force SDL into the core runtime.

The package system should support two deployment modes:

### Mode A: system dependency mode

The package manager installs the Mog package, but the host system provides SDL.
This is simpler and often appropriate for Linux development environments.

### Mode B: bundled artifact mode

The package registry publishes prebuilt `mog:window` artifacts that include the
needed dependency set for the target platform, subject to licensing and
platform rules.

This is the better user experience for mainstream users and official binary
distribution, but it should remain package-scoped, not interpreter-scoped.

## Security Requirements

Native packages are equivalent to arbitrary native code execution. The system
must treat them accordingly.

### Required controls

- checksum verification for all fetched artifacts
- lockfile pinning of artifact identity
- package validation before runtime use
- explicit distinction between source and native packages in user-facing output
- opt-in or policy-based trust controls for private registries

### Strongly recommended controls

- signed registry metadata
- signed artifacts
- provenance records for official builds
- `mog audit` support for known vulnerable package versions

### Runtime policy support

In professional environments, the package manager should support policies such
as:

- allow only approved registries
- allow only signed artifacts
- deny native packages except from approved namespaces
- require lockfile-only installs in CI

## Validation Requirements

The existing package validation work should be expanded and made mandatory in
the install and publish flows.

Validation should include:

- manifest schema validation
- canonical package ID validation
- dependency metadata validation
- `package.api.mog` parsing
- native export and ABI validation
- opaque handle metadata validation
- import-name consistency checks
- artifact hash verification
- version consistency checks

## CLI Surface

Recommended baseline commands:

- `mog init`
- `mog add`
- `mog install`
- `mog update`
- `mog run`
- `mog validate-package`
- `mog remove`
- `mog test`
- `mog build`
- `mog publish`
- `mog login`
- `mog registry`
- `mog cache`
- `mog audit`

Recommended supporting flags:

- `--locked`
- `--offline`
- `--registry`
- `--path`
- `--git`
- `--no-native-build`
- `--prefer-prebuilt`
- `--target`

Current implementation status:

- implemented commands: `init`, `add`, `install`, `update`, `publish`, `run`,
  `validate-package`
- implemented compatibility path: `mog <file>`
- implemented legacy flag compatibility: `--validate-package`
- implemented package-manager flags: `--locked`, `--offline`
- not implemented yet: `remove`, `test`, `build`, `login`, `registry`,
  `cache`, `audit`, and the remaining recommended package-manager flags

## Tooling Integration

The editor, language server, and CLI must share the same package resolution
model.

Requirements:

- language server reads the same manifest and lockfile
- hover, completion, and diagnostics use installed package metadata
- no duplicated resolution logic across tooling and runtime
- offline operation works when artifacts are already cached

Current implementation status:

- runtime and compiler/import resolution now prefer generated install metadata
- compatibility fallback to legacy package scanning still exists
- LSP now auto-installs project dependencies for workspace roots before
  analysis and then resolves through the same install metadata
- CLI/runtime/LSP project-root detection now walks to workspace roots when the
  active file lives under a declared workspace member
- compatibility fallback still exists for temp files, tests, and package
  authoring workflows outside managed project installs

## Runtime Resolution Model

Once the package manager exists, runtime resolution should change.

### Current behavior

The interpreter searches:

- `build/packages` relative to the binary
- user-specified `--package-path`
- `packages/` near the importer or current directory

### Target behavior

Normal execution should prefer:

- project lockfile
- project install metadata
- explicitly provided package store paths

Ad hoc directory scanning should be limited to:

- local development mode
- tests
- package authoring workflows

This reduces ambiguity and improves reproducibility.

Current implementation status:

- implemented: project install metadata is preferred when present
- implemented: automatic install-on-run for declared dependencies
- implemented: `mog run --locked` uses the existing lockfile/install contract
  instead of rewriting `mog.lock`
- still present for compatibility: fallback scanning of `build/packages`,
  `--package-path`, and nearby `packages/` directories

## Build System Strategy

The package manager should define a supported build contract for native
packages. Without one, publishing and installation will become unmanageable.

Recommended first-party native build strategy:

- standardize on CMake for first-party and reference packages
- require declared build entrypoints
- require noninteractive builds
- require a deterministic output artifact layout

Long term, alternative build backends can be supported, but only behind an
explicit contract in the manifest.

## Versioning and Compatibility

The package system must separate:

- package semantic version
- native ABI version
- Mog runtime compatibility

Rules:

- breaking public API changes require a package major-version bump
- breaking native ABI changes require an ABI-version bump
- registry metadata must state supported Mog runtime versions
- the installer must reject incompatible combinations early

## Workspaces

Professional development requires multi-package workspaces.

The system should support:

- a root workspace manifest
- local member packages
- path-based dependency overrides within the workspace
- a shared lockfile
- publish guards to prevent accidental publication of private packages

Current implementation status:

- partially implemented through local path dependencies and workspace/repo
  package discovery
- dedicated workspace metadata and publish guards are not implemented yet

## Migration from Current State

The package system should preserve today’s package concepts where practical:

- keep `@import("name")` for package imports
- keep canonical package IDs like `mog:window`
- keep `package.api.mog`
- keep native registration validation

Planned changes:

- `mog.lock` becomes generated and authoritative
- package installation moves from repo-local build artifacts to a managed store
- `package.toml` likely collapses into `mog.toml`
- runtime package scanning becomes secondary to resolved installs

## Phased Implementation Plan

### Phase 1: Stabilize Local Package Management

Deliver:

- authoritative manifest schema
- generated lockfile schema
- `mog install`
- `mog add`
- project-local and user-local package stores
- unified resolution for CLI, runtime, and LSP
- explicit install metadata for current repo-local packages

Do not deliver yet:

- remote publishing
- prebuilt native artifact distribution

Current status:

- mostly complete
- shipped:
  - `mog init`
  - `mog add`
  - `mog install`
  - `mog update`
  - `mog run`
  - generated `mog.lock`
  - generated `.mog/install/registry.toml`
  - distinct lockfile and install-registry schemas
  - dependency-group and source-kind tracking in generated lock/install
    metadata
  - install-time package validation for local packages
  - project-local package store materialization under `.mog/install/packages/`
  - durable local package cache metadata with project-local fallback when the
    preferred user cache path is unavailable
  - local source-package install support
  - explicit `--locked` and `--offline` flows for local dependency graphs
  - root manifest workspace metadata and workspace-root discovery
  - resolution that prefers installed project metadata
  - install-aware LSP workspace flow with automatic dependency installation
- not yet complete inside Phase 1:
  - complete manifest/source schema for git fetches and more advanced registry
    transports beyond the current file-registry model
  - richer dev-dependency consumers such as dedicated `mog test` / `mog build`
    workflows that use the recorded dependency-group metadata
  - CI/enterprise policy controls beyond the current local `--locked` /
    `--offline` behavior

### Phase 2: Registry and Publishing

Deliver:

- registry index format
- `mog publish`
- package authentication
- source package download/install
- lockfile checksum pinning

Current status:

- partially implemented
- shipped in the current Phase 2A/2B source-package slice:
  - static file-registry index format for published source packages
  - root manifest registry configuration via `[registries.<name>]`
  - exact-version and constrained-version published source-package resolution
    and install using `x.y.z`, `^x.y.z`, and `~x.y.z`
  - transitive published source-package installs through the registry index
  - registry artifact digest verification during install
  - lockfile/install metadata pinning for registry identity and artifact digest
  - cached offline reinstalls for previously fetched registry source packages
  - lockfile-first install/run behavior with explicit `mog update` refreshes
  - `mog add` support for published package specs
  - `mog publish` for source packages to the current file-registry format
- not yet complete inside Phase 2:
  - package authentication / login flows
  - git dependency fetch/install
  - non-file registry transports or hosted registry APIs
  - richer publish workflow concerns such as version/tag enforcement and
    registry-side authentication

### Phase 3: Native Artifact Distribution

Deliver:

- platform-keyed native artifacts
- `--prefer-prebuilt`
- source-build fallback
- system dependency diagnostics
- official package release pipeline for packages like `mog:window`

Current status:

- Phase 3B in progress
- shipped in the current Phase 3A/3B native-registry slice:
  - published native-package install through the current static file-registry
    format
  - published native-package `mog publish` using staged manifest/API/library
    artifacts plus a source artifact for local rebuilds
  - target-keyed native registry artifacts using normalized platform strings
  - native artifact selection for the host platform or explicit `--target`
    requests
  - `--prefer-prebuilt` and `--no-native-build` CLI surface
  - lockfile/install-metadata pinning of the selected native target
  - host-target source-build fallback when a matching prebuilt is unavailable
  - `build_from_source` lock/install/cache metadata for published native
    packages
  - cached `--offline` reinstalls for previously fetched registry native
    packages
  - lockfile-first `mog run --locked` reuse of cached native registry artifacts
- not yet complete inside Phase 3:
  - non-host source-build fallback and broader native toolchain policy
  - system dependency diagnostics
  - official package release automation for packages like `mog:window`

### Phase 4: Enterprise and Security Features

Deliver:

- private registries
- registry policy controls
- signed metadata and artifacts
- `mog audit`
- CI-focused locked and offline workflows

Current status:

- not started as a distinct feature slice beyond the local `--locked` /
  `--offline` baseline already shipped in earlier phases

## Recommended Immediate Decisions

The following decisions should be made before implementation starts:

1. `mog.toml` should be the single authoritative manifest file.
2. `mog.lock` should be generated only, never hand-edited.
3. Official packages remain separate from the interpreter binary.
4. Native packages are supported, but always clearly marked as trusted code
   boundaries.
5. The package manager, not the interpreter, owns dependency installation.
6. `mog:window` should evolve into an installable official package with prebuilt
   artifacts where practical.

## Acceptance Criteria

The package system is professionally viable when all of the following are true:

- a fresh machine can run `mog install` and `mog run` successfully for a locked
  project
- source and native packages use the same dependency workflow
- official packages do not increase the size or dependency footprint of the core
  interpreter unless installed
- lockfile-driven installs are deterministic in CI
- package validation failures are clear and actionable
- the LSP and CLI agree on dependency resolution
- official native packages can be distributed without requiring users to rebuild
  the interpreter

## Conclusion

The correct long-term architecture for Mog is:

- a small core interpreter/runtime
- official packages distributed as packages, not baked into the runtime
- deterministic manifests and lockfiles
- managed local package stores
- secure registry-based distribution
- first-class support for both source and native packages

That architecture fits the current codebase direction and avoids the central
failure mode you want to avoid: turning optional ecosystem features into core
runtime bloat.
