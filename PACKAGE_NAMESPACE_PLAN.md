# Package Namespace Plan

## Summary

Add namespaced package specifiers so imports can distinguish official packages
from third-party packages.

Recommended syntax:

```expr
import window from "mog:window";
import input from "mog:input";
import math from "acme:math";
```

In this model:

- `mog` is the reserved core namespace for packages maintained by the language
  project
- other namespaces belong to external authors or organizations
- relative and absolute file imports continue to work unchanged

This is the right direction if you want package provenance, clearer ownership,
and fewer naming collisions.

## Goals

- clearly mark which packages are official
- avoid package-name collisions between core and community packages
- make room for multiple distributors, organizations, or authors
- keep import syntax simple
- avoid changing source-module imports

## Import Model

There should be three import categories:

### 1. Source file imports

These keep the current behavior:

```expr
import math from "./math.expr";
import util from "../shared/util.expr";
```

Resolution rule:

- if the specifier looks like a path, resolve it as a source module

### 2. Namespaced native package imports

These become the preferred package form:

```expr
import window from "mog:window";
import audio from "mog:audio";
import gamepad from "community:gamepad";
```

Resolution rule:

- if the specifier matches `namespace:name`, resolve it as a native package ID

### 3. Bare native package imports

Recommended short-term policy:

- keep bare imports temporarily for backward compatibility
- treat them as legacy
- eventually deprecate them in favor of explicit namespaces

Example:

```expr
import nativeMath from "example_math";
```

Future recommendation:

- migrate this to `examples:math` or similar

## Syntax Rules

Recommended package ID grammar:

```text
namespace:name
```

Constraints:

- exactly one `:`
- namespace and name are both required
- namespace characters: lowercase letters, digits, `_`, `-`
- package name characters: lowercase letters, digits, `_`, `-`
- reserve `mog` for official packages only

Examples that should be valid:

- `mog:window`
- `mog:input`
- `acme:math`
- `user_tools:json`

Examples that should be invalid:

- `mog:`
- `:window`
- `mog:window:extra`
- `mog/window`

## Namespace Policy

### Official namespace

Reserve `mog` as a protected namespace.

Meaning:

- only core language maintainers can publish `mog:*` packages
- these packages are considered official
- they should have stricter compatibility and documentation requirements

Examples:

- `mog:window`
- `mog:input`
- `mog:fs`
- `mog:time`

### Foreign namespaces

Allow third-party packages under other namespaces.

Examples:

- `sdl:wrapper`
- `acme:physics`
- `community:gamepad`
- `jdoe:net`

Recommended policy:

- the runtime only reserves `mog`
- all other namespaces are user-defined unless you later add registry rules

## Package Metadata Changes

Each package should eventually declare:

- `namespace`
- `name`
- `version`
- `author`
- `description`
- `abi_version`

Recommended canonical ID:

```text
namespace:name
```

Recommended install path:

```text
packages/<namespace>/<name>/package.so
```

Examples:

```text
packages/mog/window/package.so
packages/mog/input/package.so
packages/acme/math/package.so
```

## Resolver Changes

The import resolver should work in this order:

1. If the specifier looks like a relative or absolute path, resolve as a source
   module.
2. Else if the specifier matches `namespace:name`, resolve as a namespaced
   native package.
3. Else treat it as a legacy bare package import.

For namespaced native packages:

- parse namespace and name
- map them to the package install path
- produce a canonical import ID such as:

```text
native:<resolved library path>
```

Recommended internal metadata to preserve:

- original specifier
- namespace
- package name
- canonical ID
- resolved library path

## Loader Validation Rules

When loading a namespaced package:

- the package registration metadata must include namespace and name
- the declared package ID must match the import target
- `mog:*` packages must be rejected if not loaded from an official/trusted
  package location, if you choose to enforce that later

Minimum validation for the first implementation:

- parsed specifier is valid
- shared library exists
- ABI version matches
- registration metadata matches requested package ID

## Recommended Migration Strategy

### Phase 1

- add support for namespaced imports
- keep legacy bare imports working
- update docs and examples to prefer namespaced imports

### Phase 2

- add metadata fields for namespace and package name
- validate that registration metadata matches the requested import
- convert built-in/example packages to namespaced IDs

### Phase 3

- warn on bare imports
- reserve `mog` formally
- add stronger tooling around namespace ownership

### Phase 4

- optionally remove bare imports entirely

## Recommended First Official Packages

Good first `mog:*` packages:

- `mog:window`
- `mog:input`
- `mog:time`
- `mog:fs`

For SDL-based work, you could start with:

- `mog:platform`

That package could cover:

- window creation
- event polling
- keyboard state
- mouse state

Later, if it gets too broad, split it into:

- `mog:window`
- `mog:input`
- `mog:graphics`

## Implementation Plan

### Step 1. Extend import target parsing

Add resolver support for `namespace:name` specifiers.

Required changes:

- detect namespaced package IDs in the import resolver
- parse namespace and package name
- reject malformed IDs with clear compile-time errors

### Step 2. Extend package descriptor metadata

Add namespace-aware metadata to the package ABI or package manifest.

Required fields:

- namespace
- package name
- full package ID

The loader should validate that imported `mog:window` is actually registered as
`mog:window`.

### Step 3. Change package layout

Move package resolution from:

```text
packages/example_math/package.so
```

to:

```text
packages/examples/math/package.so
packages/mog/window/package.so
```

### Step 4. Reserve `mog`

Add a runtime and tooling rule:

- `mog` is reserved
- third-party packages cannot claim it

If you later build a registry, this becomes a registry rule too.

### Step 5. Update tests and examples

Add tests for:

- valid `mog:*` imports
- valid third-party namespaced imports
- malformed specifiers
- metadata mismatch
- reserved namespace misuse
- legacy bare import compatibility, if kept

## Recommended Decisions

- Yes, use namespaced package IDs.
- Yes, reserve `mog` for official packages.
- No, do not use `*` in real syntax.
- Yes, keep file imports separate from package imports.
- Yes, keep bare imports only as a temporary compatibility layer.

## Best Immediate Next Step

The next concrete change should be:

1. update the resolver to recognize `namespace:name`
2. add namespace/name fields to package metadata
3. rename the example package to a namespaced ID
4. convert future official packages to `mog:*`

If you want the cleanest long-term design, treat namespaced package IDs as the
real package model and bare package names as transitional only.
