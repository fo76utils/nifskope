# Auto-sanitize exclusions (FR-017)

Auto-sanitize can skip selected block types for individual modifying operations.
Open **Settings > Auto-Sanitize**, select an operation and game, then add a block
type. **Include derived types** also matches subclasses (including the selected
type itself). Apply or Save writes the configuration; Cancel discards unapplied
changes. **Reset Custom Exclusions** clears all custom rules when applied.
**Reload File** reads external edits and offers to discard unapplied changes.

The panel displays the active configuration path and any configuration errors.
If the file changes externally while the editor has unapplied changes, saving is
refused until it is reloaded. Failed writes leave the dialog open for correction.

## Built-in Starfield camera protection

`NiCamera` inherits from `NiAVObject`. The existing name sanitizer checks duplicate
names across `NiAVObject` blocks, including empty names, and can generate a name
for the second unnamed camera. It can also generate names for invalid string
indices and names equal to `BSX`.

Starfield cameras and their subclasses are always excluded from **Fix Invalid
Block Names**, including manual invocation. This protection requires no config
file and cannot be removed in the editor. Existing names and name indices are
preserved; this is prevention of sanitizer-induced changes, not a repair of an
already named or otherwise invalid camera. Older games retain their existing
default behavior. Runtime compatibility with those games has not been asserted.

## File location

The first existing file below is used, without merging the two files:

1. `NifTools/NifSkope/autosanitize.ini` under Qt's user configuration directory
   (`QStandardPaths::GenericConfigLocation`).
2. `autosanitize.ini` beside the executable, for portable installations.

When neither exists, the editor creates the user configuration file. The location
is independent of the application's GUI/headless application name. Place a file
beside the executable before opening Settings to use portable configuration when
no user file exists. The editor writes back to the displayed path; a read-only
portable file results in a visible save error, not a silent switch of location.

The configuration is read once at the start of each `SpellBook::sanitize` call.
GUI saves, headless `sanitizeBeforeSave`, and batch sanitize all use this pipeline.
An edit therefore takes effect on the next sanitize invocation, without restarting.
The existing **Auto Sanitize before Save** switch is unchanged.

## INI format

Section names and block type names are case-sensitive and are not translated.
`BlockTypes` matches exact types. `DerivedBlockTypes` matches a type and its
subclasses. Comma-separated lists use Qt's INI syntax.

```ini
[fix-invalid-block-names]
BlockTypes=NiCamera
starfield\DerivedBlockTypes=NiNode

[collapse-link-arrays]
BlockTypes=NiNode, BSFadeNode

[reorder-link-arrays]
BlockTypes=NiNode, BSFadeNode
```

Unprefixed keys apply to all games; a game prefix restricts the rule to that game.
The example's first rule preserves camera names in **all** games, extending the
built-in Starfield protection. The second preserves Starfield node names,
including derived node types. The remaining rules preserve link arrays owned by
the two specified exact block types during both operations that can collapse them.

Supported game prefixes: `other`, `morrowind`, `oblivion`, `fallout-3-nv`, `skyrim`,
`skyrim-se`, `fallout-4`, `fallout-76`, `starfield`. Detection uses the existing
`GameManager::get_game` version mapping, not the configured resource paths.

| Operation section | Excluded processing |
| --- | --- |
| `fix-invalid-block-names` | Name repair and the root-material repair within that spell |
| `reorder-link-arrays` | Child-link sorting and empty-child pruning |
| `collapse-link-arrays` | Empty link removal in supported arrays |
| `adjust-texture-sources` | Texture source path and format preference adjustment |
| `fix-geometry-data-names` | Fallout 3/NV geometry-data Group ID repair |

Rules protect fields owned by the matching block. They do not freeze references
to that block stored in other blocks. Operations are independent: excluding a
node from Collapse Link Arrays alone does not stop Reorder Link Arrays from
pruning empty children. Validation remains enabled, including Check Links and
the existing error checkers. Reorder Blocks remains outside auto-sanitize.

Custom rules do not apply to individually invoked spells. The built-in Starfield
camera protection does apply to the manually invoked naming spell.

Unknown keys and block types are reported and ignored by processing. The editor
preserves unknown keys and shows unknown types in their corresponding lists so
they can be removed. Malformed/unreadable files disable custom rules for that
invocation while retaining built-in protection; the editor refuses to overwrite
a malformed file. Correct it externally and reload. Qt may reformat the INI file
on save; comments and original whitespace are not preserved.

## Regression tests

The optional `autosanitize_tests` qmake configuration builds the regression suite
with the application's real model, spells, and settings page. It requires Qt Test
in addition to the normal build dependencies and initialized submodules.

```sh
mkdir -p .build-autosanitize
cd .build-autosanitize
qmake6 ../NifSkope.pro CONFIG+=autosanitize_tests noavx2=1
make -j8
QT_QPA_PLATFORM=offscreen ./release/tst_autosanitize
```

The tests create synthetic NIFs and temporary configuration, checking camera
preservation through sanitize/save/reload, older-game behavior, custom rule
matching, operation isolation, malformed configuration, and editor persistence.
These tests do not establish in-game crash behavior; verify with a representative
Starfield camera asset and game runtime before making that compatibility claim.
