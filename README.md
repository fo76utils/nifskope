# NifSkope 2.0.dev11

NifSkope is a tool for opening and editing the NetImmerse file format (NIF). NIF is used by video games such as Morrowind, Oblivion, Skyrim, Fallout 3/NV/4/76, Starfield, Civilization IV, and more.

This is an experimental fork of 2.0.dev9 with many fixes and improvements, including:

* Rendering support for Starfield materials.
* Updates for file format changes in the current versions of Fallout 4, Fallout 76 and Starfield.
* glTF export and import for Skyrim SE, Fallout 4, Fallout 76 and Starfield. Import is limited to geometry only (no textures), and does not support Skyrim skin partitions.
* Resource (texture and material) files can be extracted, either the textures and materials used by the model, or files selected in an archive browser.
* Material and texture choosers, with texture preview.
* Support for saving screenshots with transparency in DDS or PNG format.
* Improved archive browser, all mesh archives in a game folder can be opened and browsed at once.
* Triangle selection with Shift-clicking.
* Improvements to the UV editor and lighting widget.
* Fallout 4, Fallout 76 and (with limitations) Starfield material editing.
* Shading has been implemented again for Oblivion and Fallout 3/New Vegas (it was removed in 2.0.dev7), and improved for many of the games.
* Fallout 76 and Starfield rendering can use cube maps or Radiance HDR files for image based lighting, and as background (skybox).
* Several mesh spells and OBJ import now support BSTriShape geometry, and new spells (including a mesh simplifier) have been added.
* Batch processing of multiple NIF files, with a limited selection of spells (update bounds, remove unused/duplicate vertices, etc.).
* The source code has been updated to use Qt 6 and core profile OpenGL 4, but it can still be compiled with Qt 5.15.
* DPI scaling on high resolution displays.

See [CHANGELOG.md](https://github.com/fo76utils/nifskope/blob/develop/CHANGELOG.md) for details.

### Download

Binary packages for Windows and Linux can be downloaded from [Releases](https://github.com/fo76utils/nifskope/releases). The most up to date builds are automatically generated on any change to the source code, and are available as artifacts from GitHub workflows under [Actions](https://github.com/fo76utils/nifskope/actions). Note that downloading artifacts requires signing in to GitHub. Binaries have been built for CPUs with support for AVX2, or AVX and F16C (NifSkope\_noavx2.exe).

You can also download the latest official release from [niftools/nifskope](https://github.com/niftools/nifskope/releases), or development builds from [hexabits/nifskope](https://github.com/hexabits/nifskope/releases).

**Notes:**

Running NifSkope under Wayland on Linux may require setting the QT\_QPA\_PLATFORM environment variable to "xcb":

    QT_QPA_PLATFORM=xcb ./NifSkope

The resource manager in this version of NifSkope is optimized for PCs with solid-state drives. While hard disk drives generally also work, if the number of loose resources is large, load times can be significantly shorter on an SSD when the data is not cached yet by the operating system.

#### Command-line (headless) usage

NifSkope can be run without a GUI by passing the **-no-gui** flag. In this mode the application performs batch operations and exits, without opening any window.

    NifSkope -no-gui --ConvertToInternalGeometry <path>
    NifSkope -no-gui --ConvertToExternalGeometry <path>
    NifSkope -no-gui --ConvertToExternalGeometry <path> -o <folder>
    NifSkope -no-gui --RemoveUnusedStrings <path>
    NifSkope -no-gui --RemoveDuplicateVertices <path>
    NifSkope -no-gui --RemoveUnusedVertices <path>
    NifSkope -no-gui --GenerateMeshLODs <path>
    NifSkope -no-gui --OptimizeIndices <path>
    NifSkope -no-gui --AddTangentSpacesAndUpdate <path>
    NifSkope -no-gui --UpdateBounds <path>
    NifSkope -no-gui --CombineProperties <path>
    NifSkope -no-gui --RemoveBogusNodes <path>
    NifSkope -no-gui --ReorderBlocks <path>
    NifSkope -no-gui --SanitizeBeforeSave <path>

Running with **-no-gui** and no further option prints a usage summary and exits with a non-zero code.

| Option | Argument | Description |
|--------|----------|-------------|
| `--ConvertToInternalGeometry` | `<path>` | Convert all external `.mesh` geometry in Starfield NIF file(s) to internal geometry. `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. Non-Starfield files (BSVersion < 170) and files that already use internal geometry are skipped without modification. |
| `--ConvertToExternalGeometry` | `<path>` | Convert internal geometry in Starfield NIF file(s) to external `.mesh` files. `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. Non-Starfield files (BSVersion < 170) and files that already use external geometry are skipped without modification. Uses `-o` output folder when provided; otherwise falls back to the saved output directory from GUI `Convert to External Geometry` and reports an error if neither is available. |
| `-o`, `--OutputFolder` | `<folder>` | Optional output folder used by `--ConvertToExternalGeometry` for writing exported `.mesh` files. |
| `--RemoveUnusedStrings` | `<path>` | Remove any unreferenced strings from the NIF header. `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. |
| `--RemoveDuplicateVertices` | `<path>` | Remove duplicate vertices from all geometry blocks (BSTriShape, BSGeometry, NiTriShape, etc.). `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. **Warning:** for Starfield NIFs this operation may break any associated morph files; no morph file check is performed. |
| `--RemoveUnusedVertices` | `<path>` | Remove unused vertices from all geometry blocks (BSTriShape, BSGeometry, NiTriShape, etc.). `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. **Warning:** for Starfield NIFs this operation may break any associated morph files; no morph file check is performed. |
| `--GenerateMeshLODs` | `<path>` | Generate Starfield mesh LODs for internal BSGeometry blocks. `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. Non-Starfield files (BSVersion < 170) are skipped without modification. |
| `--OptimizeIndices` | `<path>` | Optimize triangle index ordering for vertex cache efficiency across all geometry blocks (BSTriShape, BSGeometry, NiTriShape, etc.). `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. |
| `--AddTangentSpacesAndUpdate` | `<path>` | Add missing tangent space arrays and update tangent/bitangent data where applicable. `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. |
| `--UpdateBounds` | `<path>` | Update bounding spheres/boxes for contained geometry where applicable. `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. |
| `--CombineProperties` | `<path>` | Combine duplicate shader properties (NiProperty, NiSourceTexture, BSShaderTextureSet) into a single shared block. `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. |
| `--RemoveBogusNodes` | `<path>` | Remove useless or incorrect block types for the target NIF version where applicable. `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. |
| `--ReorderBlocks` | `<path>` | Reorder blocks so the game can properly load them. `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. |
| `--SanitizeBeforeSave` | `<path>` | Fix minor errors (for example duplicate block names) before save. `<path>` may be a single `.nif` file or a folder; folders are searched recursively. Files are overwritten in-place. |

#### Building from source code (Qt 6)

Compiling NifSkope requires Qt 6.4 or newer, or Qt 5.15. On Windows, [MSYS2](https://www.msys2.org/) can be used for building. After running the MSYS2 installer, use the following commands in the MSYS2-UCRT64 shell to install required packages:

    pacman -S base-devel mingw-w64-ucrt-x86_64-gcc
    pacman -S mingw-w64-ucrt-x86_64-qt6-base
    pacman -S mingw-w64-ucrt-x86_64-qt6-imageformats mingw-w64-ucrt-x86_64-qt6-tools
    pacman -S git

Using the MSYS2-CLANG64 environment instead of UCRT64 is also supported, in this case, the **ucrt** in the package names needs to be replaced with **clang**.

All installed MSYS2 packages can be updated anytime later by running the command '**pacman -Syu**'. To download the complete NifSkope source code, use '**git clone**' as follows:

    git clone --recurse-submodules https://github.com/fo76utils/nifskope.git

Finally, run '**qmake6**' and then '**make**' in MSYS2-UCRT64 to build the source code (the -j 8 option sets the number of processes to run in parallel). The resulting binaries and required DLL files and resources are placed under '**release**'.

    cd nifskope
    qmake6 NifSkope.pro
    make -j 8

By default, code is generated for Intel Haswell or compatible CPUs, including the AMD Zen series or newer. Running qmake with the **noavx2=1** option reduces the requirement to Intel Ivy Bridge or AMD FX CPUs, and **nof16c=1** to Sandy Bridge. To build for even older hardware, use **noavx=1** or edit the compiler flags in NifSkope.pro.

Adding the **debug=1** option to the qmake command enables compiling a debug build of NifSkope.

##### Building from source code (Qt 5)

Compiling with Qt 5 is needed on Windows versions older than 10. The steps are similar to above, but **qt6** is replaced with **qt5** in all package names, and **qmake-qt5** should be run instead of **qmake6**.

### Issues

Anyone can report issues specific to this fork at [GitHub](https://github.com/fo76utils/nifskope/issues).


### Contribute

You can fork the latest source from [GitHub](https://github.com/fo76utils/nifskope). See [Fork A Repo](https://help.github.com/articles/fork-a-repo) on how to send your contributions upstream. To grab all submodules, make sure to use `--recursive` like so:

```
git clone --recursive git://github.com/<YOUR_USERNAME>/nifskope.git
```

For information about development:

- Refer to our [GitHub wiki](https://github.com/niftools/nifskope/wiki#wiki-development) for information on compilation.


### Miscellaneous

Refer to these other documents in your installation folder or at the links provided:


## [GLTF IMPORT/EXPORT](https://github.com/fo76utils/nifskope/blob/develop/README_GLTF.md)

## [TROUBLESHOOTING](https://github.com/fo76utils/nifskope/blob/develop/TROUBLESHOOTING.md)

## [CHANGELOG](https://github.com/fo76utils/nifskope/blob/develop/CHANGELOG.md)

## [CONTRIBUTORS](https://github.com/fo76utils/nifskope/blob/develop/CONTRIBUTORS.md)

## [LICENSE](https://github.com/fo76utils/nifskope/blob/develop/LICENSE.md)

