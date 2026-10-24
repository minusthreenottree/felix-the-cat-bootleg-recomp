# Framework patches

These patches are the two upstream fixes required by this port. They are
already applied in the working tree (`sdk/` and its `m68k-recomp-core`
submodule). They are kept here so a fresh pinned checkout can be restored
reproducibly.

| Patch | Applies in | Fix |
|---|---|---|
| `0001-recompiler-empty-level-modes.patch` | `sdk/` | Emit `.level_modes = { 0 }` when the TOML array is empty; MSVC rejects `{ }`. |
| `0002-m68k-core-clr-predecrement.patch` | `sdk/external/m68k-recomp-core/` | Make `CLR -(An)` use the non-RMW store path and account for the stack slot, so `clr.l -(a7)` decrements A7. |

Apply from the repository root:

```powershell
git -C sdk apply ..\patches\0001-recompiler-empty-level-modes.patch
git -C sdk/external/m68k-recomp-core apply ..\..\..\patches\0002-m68k-core-clr-predecrement.patch
```

The third defect fixed for this port (swapped V-int/H-int callbacks) is in the
game-owned `felix_spec.c` and is already part of the project tree; it is not a
framework patch.
