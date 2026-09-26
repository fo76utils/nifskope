# FR-017 local camera fixture

`skeleton.nif` is the user-supplied vanilla Starfield asset shared by Imperial
Sympathiser for camera-name regression testing. It was copied from the user's
Downloads folder on 2026-09-07. The original download and this fixture must not
be modified. The binary is kept locally and ignored by Git; it is not included
in the release package.

- Size: 53,194 bytes
- SHA-256: `a39578a45a1812c8cd657052f0daed300669a75eedc63f99aa1b97108e74e9da`
- Expected camera: block 252 (`NiCamera`), blank Name

Set `NIFSKOPE_CAMERA_FIXTURE` to the absolute path of this fixture, then run
`tst_autosanitize suppliedCameraFixture`. Without the variable, this optional
test is skipped so upstream builds do not require a proprietary game asset.

The test creates a temporary copy and checks three auto-sanitize/save/reload
cycles plus manual name repair/save/reload. Each check verifies the camera's
blank name and original raw name index. It deletes the disposable copy and
compares the fixture bytes with the original read. A temporary-directory guard
also cleans up on assertion failure. This checks file processing, not in-game
Starfield behavior.

## Verified 2026-09-07

Native Windows release test build: Qt 6.11.2, GCC 16.2.0, Windows 11.
The complete suite passed: **17 passed, 0 failed, 0 skipped**.

The supplied file contains 261 blocks and uses Bethesda stream version 173,
classified as Starfield by the existing game mapping. Block 252 is a NiCamera
with an empty name at string index 117. The name remained empty and index 117
remained unchanged through all three automatic cycles and the manual naming
cycle. The temporary test copy was deleted. SHA-256 checks before and after
testing confirmed that both the original download and local fixture match the
hash above.

Full native test output: `.build-autosanitize/fixture-test.log` at the repository
root. No desktop video was captured; native desktop recording was unavailable
in the test session. These results come from the real model and spell pipeline
in the native regression executable, rather than a manual GUI interaction or
an in-game test.

## Recorded native GUI verification

After enabling Computer Use, the release GUI was also tested on a disposable
copy on 2026-09-07. File > Auto Sanitize before Save was visibly checked. Save
completed, then File > Reload loaded the saved file from disk. Block 252 still
showed a blank Name at index 117. The saved file's SHA-256 also matched the
original fixture. The disposable copy was deleted after closing NifSkope.

OBS captured the completed save/reload test in
`.build-autosanitize/gui-test/FR017-camera-save-reload.mp4` (about two minutes).
The recording captures only the NifSkope window, with microphone and desktop
audio muted. An earlier attempt stopped at a Computer Use activation failure
on the Settings dialog; the completed recording covers the main-window
save/reload check after that dialog was manually closed. The exclusion editor
was not manually retested in this recording.
