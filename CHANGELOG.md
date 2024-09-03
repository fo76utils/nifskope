 == CHANGELOG ==

* Fixed vertex selection being slow when the shape has a large number of vertices.
* Fixed error in the UV editor with Starfield meshes using external geometry data.

#### NifSkope-2.0.dev9-20240825

* New render setting for the mouse wheel zoom speed in the main window and UV editor.
* Implemented the 'Startup Direction' render setting that was previously disabled.
* Skyrim and Fallout 4 shader property data is no longer moved to a sub-structure of BSLightingShaderProperty or BSEffectShaderProperty.
* Shapes with alpha blending are now considered fully transparent for the purpose of vertex selection. Note that with alpha tested materials (e.g. vegetation), clicking on a vertex will often still not work because the selection mode ignores transparency.
* Fixed the geometry of docked widgets not being restored.
* Fixed bug in vertex selection with more than 256 shapes.
* Fixed invisible Oblivion models with Mesa (and possibly other) OpenGL drivers.
* Fixed error messages on macOS about unsupported GLSL version in the shaders.
* Fixed Fallout 3 and Oblivion vertices not being highlighted in vertex selection mode.
* Various other minor bug fixes.
* Windows binaries are now also built with Clang.

#### NifSkope-2.0.dev9-20240818

* Implemented high DPI scaling support for displays with higher than 1920x1080 resolution. Setting the QT\_SCALE\_FACTOR environment variable to a value other than 1.0 can be used to change the default scaling. Dynamic scale factors (due to multiple monitors with different DPI, or changing the resolution while NifSkope is running) are not supported yet.
* Added support for selecting Starfield LOD meshes in the UV editor, using the LOD slider on the main window.
* macOS binary packages are now available, built with GitHub Actions workflow contributed by DigitalBox98.
* Fixes to silhouette mode when used with Starfield models and/or cube map background enabled. Shaders also remain active for all games, this is more expensive to render, but it allows for correct transparency.
* Improved handling of transparency in PNG screenshots, the alpha channel is now calculated from a second image that is rendered in silhouette mode. The previous method of saving the alpha directly from the OpenGL framebuffer is still available with DDS format screenshots.
* When saving screenshots with transparency, the grid and axes are temporarily disabled. The skybox remains active if it was enabled, but its opacity has been changed to 0.
* PNG screenshots can be compressed, the compression level is controlled by the 'JPEG Quality' setting (0: none, 100: maximum).
* Added batch versions of the spells 'Face Normals' and 'Smooth Normals'.
* The maximum cube map resolution for image based lighting has been increased to 2048x2048. Note: this setting is generally only recommended for high resolution skyboxes with 8K HDR files, and should be used with low sample counts like 64.
* Removed support for the TGA screenshot format, as the plugin did not actually work on Windows. Instead of TGA, there is a new option to save the image as an uncompressed (ARGB32 pixel format) DDS file.

#### NifSkope-2.0.dev9-20240811

* Vertex selection has been implemented for Starfield, and the maximum number of shapes supported by vertex selection has been increased from 256 to 32768 for all games.
* Fixed the 'Add Tangent Spaces and Update' spell for Starfield.
* Importing OBJ files to Skyrim SE, Fallout 4 and Fallout 76 models now creates BSTriShape geometry instead of NiTriShape.
* Added support for vertex colors in exported and imported OBJ files (BSTriShape only).
* Fixes to OBJ import.
* Improvements to image based lighting (Fallout 76 and Starfield), including a new option for tone mapping Radiance HDR format images, which reduces importance sampling artifacts around strong lights.
* Disabled some of the spells for newer games where they are not implemented or should not be applicable (e.g. Fallout 4 and newer do not use triangle strips).

#### NifSkope-2.0.dev9-20240804

* Implemented the 'Remove Duplicate Vertices' spell for Starfield, and fixed warnings about missing triangle data for older games.
* Restored support for Oblivion, Fallout 3 and New Vegas shading.
* The UV editor now allows selecting Oblivion and Fallout 3/NV texture slots.
* New render setting for the number of importance samples to use at 512x512 and higher PBR cube map resolutions. The default is 1024, lower values reduce pre-filtering time at the cost of image quality.
* The view is automatically centered after importing a glTF file.
* Fixed saving screenshots in WebP, BMP and TGA formats.
* Fixed crash on double clicking block names in the block list.
* Fix to issue https://github.com/hexabits/nifskope/issues/68
* Minor optimizations in the resource manager and renderer.

#### NifSkope-2.0.dev9-20240724

* Added a new spell (Material/Clone and Copy to Clipboard) that is available in the right click menu on the name of a Starfield BSLightingShaderProperty, and copies the material to the clipboard in JSON (.mat) format, but with randomly generated new resource IDs. This is useful when creating a new material using an existing one as the base.
* The 'Material/Save as New...' spell also generates new resource IDs, but it saves the material as a file instead of copying it to the clipboard. Additionally, it replaces the old material name in object names (BSComponentDB::CTName) to match the new path. **Note:** material files should be saved under 'materials' in a resource path, and the asset database needs to be cleared in NifSkope (Alt+Q) to ensure that any new or modified materials are loaded and used.
* New spell (Material/Extract All...) to extract all Starfield materials.
* Starfield NIF files are now automatically converted to internal geometry on load. This is often useful because most geometry editing functionality is not implemented for external .mesh files, but it can be disabled in the general settings under 'NIF'.
* New 'Material' spell menu for BSGeometry, BSTriShape (Fallout 4 and 76), BSLightingShaderProperty and BSEffectShaderProperty blocks, with shortcuts to material spells.
* The lighting settings can be saved as the default in the Render menu.
* Trying to edit Fallout 76 or Starfield material data loaded from external files now results in a warning message, instead of the edits being silently reverted.
* Fixed some mesh and material spells not working when cast on data fields of the parent block.
* Bug fixes in drawing the normals or tangents of a selected Starfield shape, the Starfield grid scale, and simplifying skinned meshes.

#### NifSkope-2.0.dev9-20240716

* Implemented Starfield glTF import, fixes and improvements to glTF export.
* Basic material information and textures can be exported in glTF format. The resolution of the exported textures is reduced according to the mip level in the general settings, which defaults to 1 (half resolution). Setting the mip level to -1 disables texture export.
* Exporting and importing LOD meshes from/to glTF files can be disabled in the general settings under NIF.
* New spell for simplifying Starfield meshes (LOD generation), using meshopt\_simplify() from [meshoptimizer](https://github.com/zeux/meshoptimizer). It can be configured under NIF in the general settings: the algorithm tries to reduce the triangle count to the original number multiplied by 'Target count', but not less than 'Min. triangles' per shape, while keeping error under the specified value. A target count of 0.0 and/or maximum error of 1.0 disables LOD generation for all remaining levels. Note that internal geometry is required, and that using the spell globally processes all shapes as one large mesh (this is less efficient, but preserves topology across shapes and can reduce seams).
* The 'Flip Faces', 'Flip UV', 'Prune Triangles' and 'Remove Unused Vertices' spells have been implemented for Starfield (internal geometry only). Flip UV also allows for swapping the two sets of texture coordinates.
* New render setting for using the Fallout 76 or Starfield PBR cube map as a skybox. Setting this to a negative value disables the effect, while positive numbers control the amount of blur.
* Starfield bone bounding spheres are now drawn when the 'Skin' field of a BSGeometry block is selected.
* The maximum PBR cube map resolution in the render settings has been increased to 1024x1024 (uses importance sampling at mip levels 1 and 2).

#### NifSkope-2.0.dev9-20240630

* Implemented support for Starfield BSGeometry blocks that use internal mesh data instead of .mesh files. This format allows for value editing and visualization of the data, including vertices, triangles, normals/tangents, and meshlets.
* New mesh spells for converting between external and internal Starfield geometry data.
* Face Normals, Flip Normals, Normalize, Smooth Normals and Update Tangent Space have been implemented for Starfield internal geometry.
* The 'Generate Meshlets and Update Bounds' spell updates meshes (internal geometry only) to the new version 2 format introduced by Starfield 1.11.33.0, and generates meshlets using code from [meshoptimizer](https://github.com/zeux/meshoptimizer) or [DirectXMesh](https://github.com/microsoft/DirectXMesh). The algorithm can be selected in the general settings.
* Update Bounds now also updates the bone bounding spheres of Starfield skinned meshes, and the bounding boxes of meshlets (version 2 and internal geometry only).
* Added new spell (Batch/Copy and Rename all Meshes) by 1OfAKindMods to copy and rename all .mesh files used by a Starfield model. Note that the spell expects a loose NIF file, if the model is from an archive, the renamed mesh files are written under the NifSkope installation directory.
* The right click menu on header strings that end with .bgsm, .bgem or .mat has a new option to browse materials. Updating the view (Alt+U) may be needed for material path changes to take effect.
* Added limited support for rendering translucent Starfield materials (thin objects only).
* Implemented support for rendering Starfield layered materials with Additive and Skin blend modes, the latter is currently interpreted as Linear.
* Fixed the contrast parameter of Starfield blenders that is inverted in the material data.
* The default height scale for Starfield parallax occlusion mapping is now 0.0 in the render settings, as height textures do not actually work in the game.

#### NifSkope-2.0.dev9-20240616

* Starfield material rendering improvements, including support for layered materials with PositionContrast blend mode. CharacterCombine mode is also rendered, but it is interpreted as linear blending.
* Implemented support for Starfield detail blend mask settings, layered edge falloff and opacity component.
* Fixed mouse wheel sensitivity in walk mode and in the UV editor.
* Added render setting for Starfield parallax mapping height offset.
* The game manager no longer displays "unexpected change to size of loose file" error messages, if such change is detected, resources are now automatically closed and reloaded instead.
* Fixes to reading JSON format Starfield materials, import support, and workaround to invalid parent objects.
* Updates for Starfield version 1.12.30.

#### NifSkope-2.0.dev9-20240608

* The UV editor now supports exporting Starfield .mesh files. Note: currently only the first available LOD can be edited, and to be able to render the model with the modifications, the file needs to be saved to a path where it is found by NifSkope as a resource, and the NIF reloaded (Alt+X) after closing resources (Alt+Q).
* Implemented the "Flip UV" and "Remove Unused Vertices" spells for BSTriShape geometry. Note that skinned meshes are not supported yet by the latter.
* Fixed the "Search Other Games if no match" resource setting, and the edit and browse path widgets not being enabled/disabled according to the game status.
* SizedString16 data fields are now shown and can be edited as strings.
* Anti-aliasing above 4x can be enabled on Linux by adding "Render\\General\\Antialiasing%20Limit=4" to the configuration file (NifTools/NifSkope 2.0.conf) under Settings.
* Minor Starfield rendering optimization.

#### NifSkope-2.0.dev9-20240602

* Added limited (read only) Starfield support to the UV editor.
* Implemented texture slot selection in the UV editor for Fallout 3 and newer games.
* Fixed textures not being rendered in the UV editor.
* Implemented the Prune Triangles mesh spell for games using BSTriShape geometry.
* Layered emissivity settings are now shown in Starfield BSLightingShaderProperty blocks.
* The resource manager has been overhauled to allow for multiple loose NIF files to use local data paths when opened in separate windows. GameManager functions can now also be accessed from NifModel.
* Starfield rendering fixes.

#### NifSkope-2.0.dev9-20240521

* The screenshot dialog now saves the image format and path selected as settings.
* Added limited support for rendering Starfield flipbooks and layered emissivity, and for the use vertex color as tint material setting. Flipbooks are currently animated only during movement, or while the M key is pressed.
* Fixed bugs in rendering ordered nodes.
* Fixed the axes not being correctly drawn depending on the OpenGL settings from the last shape.
* Fixes in the lighting only and textures disabled rendering modes.
* Fixed the hidden flag being ignored on Starfield shapes.
* Optimizations in the archive manager.
* Improvements and fixes to Starfield material support. **Note:** loading .mat files from archives is currently disabled for materials that also exist in CDB format. Base game materials can only be replaced with loose .mat files.

#### NifSkope-2.0.dev9-20240505

* Updated for Starfield version 1.11.33.0.
* Added support for saving screenshots in PNG format with transparency.
* Fixed U and V scale and offset controllers on Fallout 76 effect materials.
* Fixed bug in restoring the viewport size after saving supersampled screenshots.
* Shading is no longer disabled on Starfield models when textures are turned off.

#### NifSkope-2.0.dev9-20240428

* Reworked archive loading, added new file menu option to browse a game or archive folder.
* The archive browser now displays uncompressed file sizes from BA2 archives.
* Added support for version 7 and 8 BA2 archives used by the Fallout 4 next gen update.
* The glass settings added to version 21 Fallout 76 effect materials are now displayed under the material properties.
* Fixed unused BSShaderTextureSet blocks in Fallout 76 models being incorrectly added to the footer as root links.
* Fixed reading unknown fields from version 21 Fallout 76 effect materials.

#### NifSkope-2.0.dev9-20240422

* Implemented the Update Bounds and Update All Bounds mesh spells for Starfield. Casting these also updates the vertex and triangle counts on meshes.
* Updating the bounds of Fallout 76 shapes now also recalculates the bounding box.
* Fixed duplicate "Update Bounds" entries in the spells menu with models from Skyrim or newer games.
* Improved bounding sphere calculation, using code from [Miniball](https://github.com/hbf/miniball) by Martin Kutz, Kaspar Fischer and Bernd Gärtner.
* Implemented rendering Starfield and Fallout 76 bounding boxes and spheres.
* Added support for reading version 21 Fallout 76 material files. The new data fields (two texture paths and an unknown byte in effect materials) are currently ignored.
* Fixed emissive multiple controller on Fallout 76 materials.
* The "Max Filepath" header field is now read as an array of unknown bytes from Starfield NIF files, to avoid converting binary data to UTF-8 on saving the model.

#### NifSkope-2.0.dev9-20240412

* New spell for exporting the resource (geometry, material and texture) files used by the currently loaded model. It is available as an option in the right click menu on file paths, this extracts a single file, and in the main Spells menu to extract all resources required by the model. Note that files are extracted in the correct sub-folders (textures/, materials/, etc.) under the selected destination directory, and Starfield materials are automatically converted to JSON .mat format.
* New spell to replace all occurrences of a regular expression in resource paths that match a filter regular expression. It currently does not work on paths stored as header strings (like materials), and on abstract data loaded from external files.
* Resource settings now default to all games being enabled.
* The planar angle slider of the lighting widget can now be used in frontal light mode to rotate the environment map used by image based lighting.
* The PBR cube map resolution setting has a new maximum quality option that disables importance sampling at 512x512 resolution per face. Note: changes between the '512x512' and 'max. quality' modes may only take effect after restarting NifSkope if the texture is already cached.
* Fixed mip level calculation bug in importance sampled cube map filtering.
* Anti-aliasing is limited to a maximum of 4x on Linux to work around compatibility issues. Additionally, the calculation of sample count has been fixed so that it is set to 2, 4, 8 or 16 as displayed, instead of 1, 4, 9 or 16. The same change has also been applied to anisotropic filtering.
* Binary packages now include "noavx2" executables for better compatibility with older CPUs, including Ivy Bridge and AMD FX series.
* Fixed shader compilation error in fo4\_effectshader.frag.

#### NifSkope-2.0.dev9-20240331

Changes compared to the dev9 release from Sep 29, 2023:

* Support for rendering Starfield materials, with limitations: multiple layers are mostly unsupported, and translucency and many other advanced effects are not implemented. Materials can be loaded both from the compiled database (materialsbeta.cdb) and from loose .mat files, the latter is experimental.
* Image based lighting for Starfield and Fallout 76, can use DDS cube maps or Radiance HDR format images (available for example at [Poly Haven](https://polyhaven.com/hdris), place the .hdr files in textures/cubemaps/ under a folder configured as a data path) as input. The resolution of the pre-filtered cube map and the paths to the textures can be set in the render options.
* Starfield height maps are implemented using parallax occlusion mapping, with new render settings for the height scale and maximum number of steps.
* Various rendering fixes for Fallout 76, and some for older games.
* Skyrim, Fallout 4, Fallout 76 and Starfield shaders have all been modified to use view space vectors instead of tangent space, similarly to the change described [here](https://github.com/Candoran2/nifskope/commit/7e4c6e127e3f2f569d2417bd04cbb7a6bb1bd822). Skyrim parallax mapping has been updated as well to work correctly with this change.
* Improved physically based rendering for Fallout 76 (also used for Starfield), based on the models by [Brian Karis](https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf) and [Brent Burley](https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf).
* Starfield and Fallout 76 material data loaded from external files is added as abstract data to the NIF model.
* New option in the Render menu (Alt+U) to flush cached textures, reload shaders, and update the display.
* The integer data (CRC32 of the material path) associated with Starfield materials is now recalculated on changes to the material path. Note that editing the path in the header strings may not automatically update this value and the display, use Update View (Alt+U) or toggle the grid on/off with G to force the update.
* The Edit String Index spell has a new button for browsing Starfield, Fallout 76 and Fallout 4 materials. Note: using this to swap materials leaves the previous paths as unused strings in the header, those can be cleaned up with 'Spells/Optimize/Remove Unused Strings' and then updating the view (Alt+U).
* The choose texture spell now browses all texture files available as resources, instead of the file system.
* New spell to export Starfield materials, it copies the material data from the selected BSLightingShaderProperty to the clipboard in JSON format.
* Improved lighting widget, with separate controls for the overall brightness and the intensity of directional and ambient light, and new sliders for tone mapping and the color temperature of the light source. There is also a new button for browsing the environment map (.dds or .hdr) used by Fallout 76 and Starfield image based lighting.
* Reworked resource manager, Fallout 3 and New Vegas have been merged in the resource settings to a single game mode, and removed the Archives tab. Resource data paths can now be folders (from which all archives and supported file types are loaded), single archives and loose files, and paths listed first have higher precedence. Note that a very large number of loose files may result in long load times.
* The location of the currently opened NIF file is automatically added as a temporary data path. Resources associated with the model can be in archives, or in loose files under the correct sub-directories (textures/, geometries/, etc.).
* Currently mapped resources can be released with Close Archives (Alt+Q) in the file menu. If a temporary data path is in use, only that is closed first.
* Fixed bugs in the screenshot dialog, including potential crash due to invalid memory access, and incorrect gamma on Fallout 76 and Starfield screenshots. JPEG files are also written with optimization and in progressive format, slightly reducing the file size at the same quality.
* Various other bug fixes, including to https://github.com/hexabits/nifskope/issues/51, https://github.com/hexabits/nifskope/issues/56, https://github.com/hexabits/nifskope/issues/61, https://github.com/hexabits/nifskope/issues/62, https://github.com/hexabits/nifskope/issues/64, https://github.com/hexabits/nifskope/issues/65, https://github.com/hexabits/nifskope/issues/67#issuecomment-1858428809 and https://github.com/hexabits/nifskope/issues/74.

### Official releases

**NOTE: This changelog is not maintained for 2.0**

You may view the 2.0 changes here: https://github.com/hexabits/nifskope/releases


This is version 1.1.3 of NifSkope.

changes since 1.1.2:
* Fix accidently broken xml for Fallout 3 (contributed by ttl269).
* Qmake getting git hash no longer relies on git executable.
* Code refactor: xml for ui of about form.
* Fix for registry entry proliferation (niftools issue #3584193, reported by neomonkeus).

changes since 1.1.1:
* When combining material properties, keep original material names as much as possible (reported by koniption).
* Fix display of transformed havok shapes (reported by koniption).
* Skyrim material updates (contributed by ttl269).
* Skyrim body part updates.
* Document sane defaults for Skyrim bhkRigidBody "Unknown 7 Shorts" (reported by ttl296).
* Rename: binormals are now more accurately called bitangents.
* Collada export improvements (contributed by mharj).
* Support node culling by regular expression in collada/obj export (contributed by mharj).
* Fix block order of bhkCompressedMeshShapeData (reported by ttl269).

changes since 1.1.0:
* Fix unsigned int enum issue.

changes since 1.1.0-RC8:
* Collada export fixes for Skyrim and Blender (contributed by mharj).
* Various small bug fixes in the xml.
* Fix uv editor for Skyrim nifs.

changes since 1.1.0-RC7:
* Collada export (contributed by mharj).
* Mac compile fixes.
* Hierarchy fix for BSShaderProperty and derived blocks.
* Hierarchy fix for NiParticlesData and derived blocks.
* Support for Qt 4.8.
* Qhull update.
* Material fix for pack strips spell (issue #3413668).
* Fix for property attach spell for Skyrim (issue #3451054).
* Fix for resource files that could not be removed (issue #3452880).
* Fix for node names in ControllerLink blocks (issue #3453556).
* Add: protective check to increase robustness
* Fix: fixed #3525690 NifSkope Crash: MoppBvTree Triangles
* Fix: blocked invalid memory access when "triangles" is empty
* Fix: stopped triangle reordering on .obj import into "FO:NV" "NiTriShape"
* nif.xml: bhkCompressedMeshShape: merged "skyfox", "ttl269" and probably other peoples work

changes since 1.1.0-RC6:
* Added initial support for "BSLODTriShape" - its being rendered
* Fixed a lot of stuff regarding .obj import when BSShaderPPLightingProperty is present
* Fix: Handling of spaces in .mtl files filename

changes since 1.1.0-RC5:
* Fix: .kf attaching for 10.0.1.0 - renaming the fields in "nif.xml" works as well
* Fix: double_sided rendering issue
* Add: SF_Double_Sided handling for "BSEffectShaderProperty"
* Fix #3468080: property "paste" now works for non-NiProperty blocks
* Fix #3471254: temporary set global template for some properties
* Fix: rendering of double-sided + alpha unsorted triangles issue
* Add: "BSEffectShaderProperty" - initial support
* Add: unknown properties are now reported by the property factory

changes since 1.1.0-RC4:
 * Added support for "BSLightingShaderProperty" in the UV editor and exporter

changes since 1.1.0-RC3:
 * Fixed two regressions

changes since 1.1.0-RC2:
 * Fix: "havok material" no longer resets to "HAV_MAT_STONE" when packing strips
 * Modified "havok" axes visibility
 * Enabled double-sided rendering when specific flag specifies it
 * Initial handling of unknown (possibly animation) alpha component of
   NiTriShape vertex colors
 * Fix: two regressions
 * Added View Toolbar and new action - "reset block details"
 * Fix: restoring the main window no longer resets "block details"
 * Automatic vertex weighting initial support
 * BSTreeNode initial support

changes since 1.1.0-RC1:
 * block number is visible
 * made material editor dialog window modal
 * Update for new qhull
 * Fix: quat. rotation keys linear interpolation issue
 * Fix for Qt 4.8.0: naming clash with dds_swap
 * Fix: added handling of invalid "Triangles" indexes
 * Fix: Skyrim textures are visible
 * Fixed NvTriStripObjects build error on gcc
 * Fix: File->Quit was not storing the program settings
 * Fix: bhkPackedNiTriStripsShape highlighting now works
 * Fix: furniture below shapes is now correctly mouse
 * Fix: transpaarency no longer hides selected fragments
 * Fix: highlighting of bhkListShape. Also, group highlighting now works
 * Changed interactive selection approach
 * Fix for user version in big endian nifs
 * Require OpenGL 2.0 (this fixes a crash on older hardware)
 * fixed nifskope crash on mouse left click in the opengl context window
 * many more ...

changes since 1.0.22:
 * updated to Qt 4.6.2
 * reworked BSA code for Qt 4.6 changes
 * import window state and lighting settings from previous versions
 * better support for big-endian files
 * increased the number of decimal places for rotations (suggested by Ghogiel)
 * Split "Adjust Link Arrays" spell:
  - "Reorder Link Arrays" which now works for versions 20.0.0.4 onwards and
    removes empty links (fixes issue #3037165)
  - "Collapse Link Arrays" which removes all empty links and works for all
    versions
 * fix for corrupt version write
 * highlight subshape selection in bhkPackedNiTriStripsShape
 * add Fallout New Vegas to game texture auto-detection
 * updated to GLee 5.4.0
 * nif.xml updates for Divinity 2 and Fallout New Vegas
 * OpenGL fix for ATI/AMD graphics cards (contributed by corwin)
 * bounds calculations for OpenGL view now take BSBounds and old style
   bounding boxes into account

changes since 1.0.21:
 * updated to Qt 4.5.3
 * added support for NiPersistentSrcTextureRendererData textures (alternate
   form of NiPixelData)
 * allow hexadecimal values in nif.xml enumeration options
 * added "String Palette->Replace Entries" spell for NiSequence blocks, and
   "Animation->Edit String Palettes" helper
 * fixed display of Fallout 3 morph animations (NiGeomMorpherController)
 * support reading nifs which use the JMI extension and header
 * added "Node->Attach Parent Node" (suggested by vurt2)
 * fix display of multiple decal textures (reported by Axel) - note that the
   number of texture units in your graphics card may limit how many textures
   can be displayed simultaneously
 * UV editor:
  - added coordinate set selection / duplication to context menu
  - added rotation and scaling / translation of selected vertices to context
    menu with respective shortcuts (suggested by psymoniser and snowfox)
  - increased maximum zoom level, reworked grid lines and cursors
 * updated BSXFlags bit descriptions
 * re-add NiPixelData texture import ("Texture->Embed" spell)
 * load .texcache (Empire Earth II / III external NiPixelData files) and allow
   direct texture export; installer also registers .texcache and .pcpatch
   (unknown 8-bit 1-dimensional textures?) extensions
 * added "Havok->Create Convex Shape" using Qhull and old code by wz

changes since 1.0.20:
 * added support for displaying 8-bit palettised DDS textures
 * added variable mipmap filtering display for NiTexturingProperty
 * added "Mesh->Flip Faces" spell
 * added "Texture->Add Flip Controller", "Texture->Edit Flip Controller"
   spells
 * added support for reading NeoSteam headers
 * "Combine Properties" spell will not combine properties that inherit
   BSShaderProperty since they need to be unique (reported by Saiden)
 * fixed A -> B spell for bhkHingeConstraint (issue #2835485 reported by
   nexekho)
 * selection highligting fixes for bhkPackedNiTriStripsShape (triangles and
   normals)
 * added support for KrazyRain and Zorsis blocks (control characters etc.)
 * synced block order algorithm with pyffi (fixes the "falling signs" bug in
   Oblivion)
 * allow UV editing of meshes without a base texture

changes since 1.0.19:
 * updated to Qt 4.5.0
 * fix tooltips display for nif.xml annotations
 * Paste Block/Branch spells now set Controller and Target links when pasting
   controller blocks, and updates Effects array when pasting an effect block
 * Paste At End spell added to paste a branch without parenting
 * Fix display of Stencil and Z Buffer properties for versions >= 20.1.0.3
 * Flag spell updated for Vertex Color, Stencil Buffer, and Z Buffer properties
 * bug fix for file selection widget ("Load" and "Save As") textbox styling
 * performance improvements when attaching animation
 * set sequence controller NiMultiTargetTransformController when importing kfs
 * display of NiUVController for UV transformations
 * Add "Move Down" option to Texture Folders list
 * display of NiVisController
 * fixed display of NiMaterialColorController for versions < 10.1.0.0 and
   added flags editor
 * display of BSBounds (displayed with Havok), tweaks to display of Bounding
   Box, added a "Edit Bounds" spell
 * highlighting of strips
 * UV editing of textures other than the base texture
 * re-add hiding of rows not applicable to the current version

changes since 1.0.18:
 * Spells:
  - fixed "Multi Apply Mode" spell [ niftools-Bugs-2475705 ]
  - shortcut keys for "Copy Branch" and "Paste Branch"
  - new "Duplicate" and "Duplicate Branch" spells
  - new "Flip Normals" spell and option to scale normals in "Scale Vertices"
  - new "Mirror armature" spell for Morrowind
  - add flag editing for NiBillboardNode
  - automate NiTextureEffect attachment (found under Node->Attach Light)
  - adding a new texture now presents a file selection dialog
  - new "Collapse Array" spell to selectively remove empty links
  - re-add texture export: NiPixelData can be exported to TGA or DDS (default)
  - texture chooser pathing tweaks
  - new "Blocks->Sort By Name" spell
  - "Attach .KF" can handle multiple non-conflicting .kf files
 * Add support for UV editing of embedded textures and external nif textures
 * "failed to load file header" message now gives hexadecimal and string
   values instead of decimal
 * nif.xml handling:
  - read nif.xml from current directory in preference
  - fix handling of signed values
  - allow enumeration defaults to be specified by name
  - enumerations now have their underlying data type displayed in tooltip
 * Installer:
  - file types for KF and KFM changed to NetImmerse/Gamebryo Animation and
    NetImmerse/Gamebryo Animation Manager respectively
  - location now checks for an existing installation
 * ignore "QAccessibleWidget::rect: This implementation does not support
 subelements!" message
 * prevent warning message when attempting to load an empty texture filename
 * fix crash on exit if no file opened and inspect window not opened
 * Shortcut keys for "Load" and "Save As"

changes since 1.0.17:
 * fixed corruption of BSShaderNoLightingProperty file names when using texture chooser
 * fixed rendering settings which sometimes broke texture rendering when shader not used
 * added settings page for selecting displayed user interface language
 * fixed Binormals and Tangents swapped in Fallout 3 files [ niftools-Bugs-2466995 ]
 * added Block | Convert Spell for cleanly changing node type
 * set default stencil property flags to 19840 for Fallout 3 (suggested by Saiden)

changes since 1.0.16:
 * force updateHeader and updateFooter to be called before save
 * introduce bitflag data type
 * add editor for bitflag types
 * update santize spells to better cleanup Fallout3 NIFs during export
 * custom Fallout 3 sanitize spell forcing NiGeometryData blocks to have names
 * more support for internationalization
 * bug fixes in the renderer which completely disabled shaders on common hardware
 * bug fix for showing BSDismemberedSkinInstance highlighting in viewer when partitions were selected
 * fix to make Fallout 3 normal map shader renderer work

changes since 1.0.15:
 * add Transform Inspection window
 * add Fallout 3 to game texture auto detection
 * nif.xml updates for Fallout 3
 * add support for Fallout 3 BSA files
 * add material color override in settings
 * new version condition evaluation engine to handle Fallout
 * using GLee, so nifskope now also compiles on mac
 * update tangent and binormal update script for Fallout as well as rendering

changes since 1.0.14:
 * fixed issues with attaching kf controller with nif/kf version >= 20.1.0.3
 * updated mopp code generation to use subshape materials
 * updated for Qt 4.4.3
 * support reading nifs which use the NDSNIF header used in Atlantica
 * new block types added from Atlantica, Florensia, Red Ocean

changes since 1.0.13:
 * fixed bhkRigidBodyT transform
 * fixed (innocent but annoying) error message on blob type
 * fixed Oblivion archive support for BSA files for use with textures
 * fixed having wrong texture in render window under certain conditions

changes since 1.0.12:
 * workaround for Qt annoyance: QFileSystemWatcher no longer barfs
 * installer also registers kfm and nifcache extensions
 * remove empty modifiers from NiParticleSystem when sanitizing
 * fixed value column in hierarchy view when switching from list view
 * new mopp code generator spell (windows only), using havok library
 * some small nif.xml updates
 * warn user when exporting skinned mesh as .OBJ that skin weights will not be
   exported
 * updated skin partition spell to work also on NiTriStrips
 * when inserting a new NiStencilProperty block, its draw mode is set to
   3 (DRAW_BOTH) by default (issue #2033534)
 * update block size when fixing headers on v 20.2 and later
 * updated for Qt 4.4.1
 * add support for embedded textures and external nif textures
 * display revision number in about box
 * new blob type to make large byte arrays more efficient
 * fixed bounding box location in opengl window

changes since 1.0.11:
 * fixed animation slider and animation group selector being grayed out

changes since 1.0.10:
 * added support for nif version 10.1.0.101 (used for instance by Oblivion
   furniture markers in some releases of the game)
 * fixed code to compile with Qt 4.4.0 and worked around some Qt 4.4.0 bugs
 * creating new BSXFlags block sets name automatically to BSX (issue #1955870)
 * darker background for UV editor to ease editing of UV map (issue #1971002)
 * fixed bug which caused texture file path not to be stored between
   invokations of the texture file selector in certain circumstances (issue
   #1971132)
 * new "crop to branch" spell to remove everything in a nif file except for a
   single branch
 * new "Add Bump Map" and "Add Decal 0 Map" spells for NiTexturingProperty
   blocks (issue #1980709)
 * load mipmaps from DDS file rather than recalculating them from the first
   level texture (issue #1981056)
 * toolbar whitespace fix for linux

changes since 1.0.9:
 * fixed bsa file compression bug for Morrowind
 * fixed havok block reorder sanitize spell (replaced with a global block
   reorder spell)

changes since 1.0.8:
 * synced DDS decompression with upstream (nvidia texture tools revision 488)
 * fixed nif.xml for 10.2.0.0 Oblivion havok blocks
 * fixed DXT5 alpha channel corruption

changes since 1.0.7:
 * Use software DXT decompression also on Windows to work around texture
   corruption bug

changes since 1.0.6:
 * Fixes for the MinGW build, the build now works on all Windows versions
   (also older versions that are no longer supported by Microsoft).
 * Updated build to use Qt 4.3.4
 * Added + and - to expression parser.
 * Updates to nif and kfm format.
 * Cleaned up kfm xml format.

changes since 1.0.5:
 * Stylesheet for the linux version.
 * Activated update tangent space spell for 20.0.0.4 nifs
 * Temporarily disabled removing of the old unpacked strips when calling the
   pack strip spell as this crashes nifskope; remove the branch manually
   instead until this bug is fixed.
 * Texture path used for selecting new textures is saved.
 * Shortcuts in texture selection file dialog are now actually followed.

changes since 1.0.4:
 * Fixed block deletion bug.
 * Updated to compile with MSVC Express 2008 and Qt 4.3.3
 * Updated installer to check for the MSVC 2008 dll.
 * Registry settings between different versions of nifskope are no longer
   shared to avoid compatibility problems if multiple versions of nifskope
   are used on the same system.
 * Non-binary registry settings are copied from older versions of nifskope,
   if a newer version of nifskope is run for the first time.
 * Some minor xml updates:
   - NiMeshPSysData fixed and simplified
   - new version 20.3.0.2 from emerge demo
   - replaced Target in NiTimeController with unknown int to cope with invalid
     pointers in nif versions <= 3.1

changes since 1.0.3:
 * XML update to fix the 'array "Constraints" much too large ... failed to load
   block number X (bhkRigidBodyT) previous block was bhkMoppBvTreeShape'
   problem.
 * Software DXT decompression for platforms that do not have the S3TC opengl
   extension such as linux with vanilla xorg drivers, so DDS textures show up
   even when S3TC extension is not supported in the driver (code ported from
   nvidia texture tools project).
 * Started adding doxygen-style documentation in some source files.
 * Added nifcache and texcache to nif file extension list (used by Empire Earth III)

changes since 1.0.2:
 * Installer
  - fixed link where user can download MSVC redistributables

changes since 1.0.1:
 * User Interface
  - another attempt at fixing the delete block crashes

changes since 1.0:
 * User Interface
  - visualize skin partitions for versions < 10.2.0.0
  - fixed \r \n issue.  (Embedded carriage returns can be added using the Shift+Enter or Alt+Enter keys.)
  - Avoid crashing when using XML Checker on 20.1 nifs with unknown file formats
  - Add ByteMatrix data type for NiPixelData
  - extra space in text editor to ensure that single line strings are nicely displayed on edit
  - Allow for multiple file filters in the file open and save dialogs.
  - Force C locale so there is at least some consistency somewhere in the app.
      Currently some editors use system locale but Qt forces C locale on some string operations.
  - Fix sync bug when using delete branch
  - Add multiline editor for text fields.  Also sets uniform row height on tree.
  - fixed display of multiline strings (without affecting the actual data)
  - syncing strip pack spell with xml

 * NIF Compatibility
  - fixes for oblivion skeleton.nif files
  - Fixes for Emerge Demo and Megami Tensei: Imagine and the NiBoneLODController and related items.
  - Fixes for the following: Loki, Guild 2, Warhammer, PCM 2007,
      1. Added skeleton PhysX blocks from copetech examples
      2. Fixed NiSortNode
      3. Fixed NiPixelData to better match file format
      4. Fixed NiBoneLODController
  - fixes for ffvt3r 10.1.0.0 nifs (capsule NiCollisionData)
  - fixed NiGeometryData for 10.1.0.0 nifs
  - Fixes for NiCollisionObject and Multiline text in nifskope.
  - number of vertices in oblivion sub shapes

changes since 0.9.8:
 * User Interface
  - Expanded help menu and renamed "Reference Browser" to "Interactive Help" and made it come up when F1 is pressed.
  - Merged autodetect buttons into a single button which looks for every supported game so you can auto-detect all our games at the same time.
  - The custom paths you've specified will no longer be deleted by the autodetect button.
  - File names displayed in the load and save box should now always have the full path and show long file names on Windows.  They should also contain back slashes instead of forward slashes.
  - Added Vertex and Triangle Selection to bhkPackedNiTriStripsShape
  - Linux file select fix
  - The BSXFlags selection box works again
  - Improved FloatEdit to accept "<float_min>" and "<float_max>".
  - Added Draw Constraints option.
  - Added check if array is even enabled to "Update Array" spell
  - Created new icons for "Follow Link" and "Flags" spells just for fun.
  - Created an icon for "Array Update" and made the spell instant, so now arrays can be updated just by clicking the icon.
  - Made the Matrix4 edit spell "instant" so that the icon would appear next to the Matrix and the user won't have to drill down into the right-click menu.
  - Created some more amusing icons for the various view toolbar buttons.
  - Reformatted the insert block menu to make a little more sense.  "Havok" instead of "BHK," grouped other Bethesda and Firaxis nodes, etc.
  - Added version matching to XML checker.
  - Changed link editing so if you clear the value and press enter, the link changes to "None."  This way the user won't have to know that "-1" has a special meaning.
  - Fixed block-matching so it works for all versions, though it won't be any faster for old versions since early rejection can't be used.
  - Fixed NiControllerSequence links so that 0x0000FFFF also counts as "empty" instead of showing up as "invalid index"
  - Added the ability to specify what NIF version the new "blank" NIF is when NifSkope starts.
  - Changed relative path specifier to './'
  - Fixed issue where links in old files weren't mapped if the file didn't load completely.
  - The XML checker window's Block choosing button now has the same new menu structure as the Block > Insert spell.


 * 3D View
  - Added ability to center the view on the selected node or shape.  Select the node/shape and press the C key.
  - Made it so you can zoom in and out by right-dragging left and right as well as up and down.
  - Added Text Rendering when "Triangles" is selected on the bhkNiTriStripsShape data.  If the top is selected all triangles are numbered if a specific one is selected then only that triangle index is shown.
  - Removed triangle sorting of alpha meshes because none of the games or official scene viewers actually seem to do this.  Improves display of Oblivion hair meshes.
  - Fixed, added, or improved several Havok Constraint visuals.
  - Added support for displaying textures in NiTextureProperty (Nif ver. 3.x)
  - Made selection lines look much less flickery by increasing the line width to 1.5 before drawing them.
  - Added visualization for old style collision boxes.  Lumped it in with "Havok" since it's the same idea as a Havok box shape.
  - A warning will appear if S3TC texture compression is not supported by the OpenGL driver.

 * Spells
  - Update Center/Radius" spell now respects CF_VOLATILE and Oblivion simple center searching methods.
  - Enhanced LimitedHingeHelper spell to every constraint
  - "Edit UV" is applicable only when there are texture coordinates present and fixed bug which could lead to crashing.
  - The Optimize > Combine Shapes spell now issues warnings when shapes don't count as matching due to the presence of unsupported NiObjects, such as NiSkinInstance.
  - The Multi Apply Mode spell is now on the "Batch" page of the spell book, and only appears in the Spell menu, never in the right-click menu.
  - Edit UV is now only applicable to NiTriShape or NiTriStrips. It didn't work for NiTriShapeData or NiTriStripsData, but would show up in the right-click menu anyway.
  - Added the ability to not stripify and pad newly created NiSkinPartition objects in the "Make Partition" spell.  Still needs more work to make proper partitions for games which require 4 bones per partion, however.

 * Import/Export
  - 3ds and OBJ import updated so that all the NiTriShapes will be attached to whatever NiNode is selected in the tree view, similar to the old right-click->Import method.
  - Added the ability to import the first mesh from an OBJ or 3DS file over the top of the selected NiTriShape object.
  - Added the ability to export starting from the specified node, or only a specific mesh, depending on what is selected.
  - Import and export now warn the user what they are about to do, based on the selection, giving them a chance to cancel the operation if it isn't what they wanted.
  - Fixed 3ds import for shapes with multiple materials.
  - Optimized 3ds import a bit so that group nodes will only be created when there is more than one material in a shape.
  - Updated Import 3ds so that it creates a "Scene Root" node which all object nodes are attached to, rather than creating a NIF file with many roots.
  - Fixed a the same bug in 3ds import as was fixed in OBJ import where "Has UV" was not being set properly.
  - Fixed OBJ and 3ds import and export so that NiImage and NiTexture are created in 3.x files, and are detected during OBJ export.
  - Fixed a bug that was causing the "Has UV" bool not to be set correctly when an OBJ file was imported.

 * NIF Compatibility
  - Added partial support for the following new games: Loki, Pro Cycling Manager, Shin Megami Tensei
  - Fixed a bug that made NifSkope fail to load the 3.3.0.13 file.
  - Fixed problem with version 3.03 conditions in the XML.  Version 3.1 files can be read again.
  - XML changes which allow NiSkinPartition objects that won't crash Morrowind and Freedom Force to be created.

 * Misc.
  - GCC build fixes.
  - Added check for empty QFileSystemWatcher, this removes the nasty qWarning
  - Uninstall now correctly removes all folders.

changes since 0.9.7:
  - Bugfix Re-Release
  - added docs by default and fixed RefBrowser bugs
  - changed relative path identifier from "$NIFDIR" to the standard "./"

changes since 0.9.6:
  - texture layout editor
  - change textures via drag&drop
  - support for single texture properties from StarTrek:BridgeCommander
  - import .3ds
  - pack havok strips
  - experimental visuals for stiff spring and ragdoll constraints
  - reference browser

changes since 0.9.5:
  - Fixed bug that would crash NifSkope in FlipUVs
  - Improved stability
  - Copy filename buttons
  - Visual load/save feedback
  - Localization system

changes since 0.9.4:
  - oblivion glow map shaders
  - anisotropic texture filtering (check the antialias checkbox in the render
    settings dialog)
  - fixed another issue with update tangent space, should look better now
  - the havok strips are now drawn like they appear in the tescs
    if you encounter the spider web effect use the new unstich strips spell
    on the NiTriStripsData block
  - the skin partition spell now also accepts nifs below 20.x.x.x
  - added an option to save and restore the 3d view's rotation, position
    and distance
  - rotations can now also be edited as axis angle pairs
  - added a widget style sheet (style.qss)
    if you want you can customize the look and feel of NifSkope through this
    style sheet
  - fixed an issue with the ipc socket
    NifSkope should open now at least one window regardless of any personal
    firewall settings

changes since 0.9.3:
  - support for nifs below version 4 (StarTrek: Bridge Commander)
  - comprehensive render settings dialog
  - texture folder auto setup
  - highlighting of individual vertices, normals, triangles, etc.
  - added a spell to combine equal properties and another spell to make the properties
    unique again
  - improved skin partitioning
  - fixed calculation and rendering of binormals, under some circumstances the
    direction of the binormals was flipped if you encounter this problem updating
    the tangent space again should solve it
  - fixed flag spell
  - fixed LOD switching
  - fixed Morrowind morph animations
  - template column integrated into type column
  - switched to Qt 4.2


changes since 0.9.2:
  - for enumerated values (eg: havok material) nifskope now displays the option name
    instead of an integer value
    also the type column's tooltip now displays the type's description and all the
    available options
  - the version tag syntax in the xml description was slighty changed to be more intuitive
  - clicking in the 3d view now cycles through the items below the mouse


changes since 0.9.1:
  - texture paths are now converted to windows notation
    because otherwise Oblivion doesn't find textures located within .bsa archives
  - stripifier now outputs single strips
    because CivIV draws only the first strip


changes since 0.8.8:
  - now plays embedded animation sequences (good examples are the doors
    and some traps from oblivion)
  - thanks to tapzn for implementing bspline animation interpolators
  - new spell: attach .kf
    - this spell takes an external animation sequence and attaches it
      to the nif
    - this way it's now possible to view the creature and character animations
      in NifSkope
  - fixed havok sorter: now sorts constraints and bhkNiTriStripsShape too
  - updated the skin partitioner: now automatically reduces vertex influences
    and splits large partitions into smaller parts
  - some small tweaks:
    - paste branch now automatically parents ninode branches to the selected ninode
    - ipc over udp ensures that new NifSkope windows are allways opened in the same
      application instance (this makes the 'new window' workaround obsolete when
      copying and pasting between nifs on windows platform)
      note that this works by opening a local udp server socket. communication on
      this socket is limited to local host only. so it shouldn't be a security issue.


changes since 0.8.7:
  - havok blocks will be ordered automatically
  - some hot keys for the spells
  - fixed obj import/export (textures,collision meshes)
  - new spell allows editing of string offsets
  - new spell for removing blocks by id
  - new spell to upload morph frames


changes since 0.8.6:
  - gundalf contributed a new feature: oblivion furniture
    animation markers
  - Skin Partitions made with NifSkope should now work properly
    on nvidia cards too


changes since 0.8.5:
  - fixed compability with older graphic cards
  - fixed animation controllers
  - fixed triangle sorting
  - fixed an issue with Make Skin Partion on meshes with less than four bones

changes since 0.8.2:
  - two oblivion style shaders displaying normal and parallax mapping
  - automatically reorders the children link arrays for oblivion
    (trishapes first, ninodes last)
  - new spell: Mesh/Make Skin Partition
  - many cosmetic changes in nif.xml

changes since 0.8.1:
  - multi shape .obj export/import
  - remove branch spell
  - make normals and smooth normals spells
  - added an option to show the havok shapes
  - tool tip descriptions for all blocks and items

changes since 0.8:
  - XML updated
  - create texture templates
  - .OBJ import/export

changes since 0.7:
  - Billboards
  - LOD switching
  - Model walk (walk around the nifs using the wasd key combo)
  - Perspective / Orthographic projection in normal view mode
  - improved vertex color lighting
  - improved boundary scan
  - hide non textured meshes (daoc compability)
  - hide nodes by name
  - new debug spell: thread load

changes since 0.6:
  - lights
  - particles (80% of the old particle systems from morrowind)
  - xml: template types + optimized conditions
  - fixed texturing again:
    - NIF folder is now searched for textures too
    - smart cache flush control (reloads textures if needed)
    - fixed RGBA32 TGAs which have their alphaBitCnt set to zero
    - fixed TGA RLE decompression (was much too slow)
    - added support for TGA L8 and LA16 (greyscale)
    - fixed bit stuffed DDS formats (RGBA 5551 565 4444 ...)
    - alternative extension test (nifs from mournhold and solstheim)
    - fixed tex folder deselect
    - fixed filenames containing \r\n\0

changes since 0.5:
  - new flexible context menu system (spells)
  - texture spells ( choose + pack + export + add glow/base map )
  - stripify spell converts NiTriShape to NiTriStrip
  - gl view now uses filtered mipmaps
  - various fixes and adjustments

changes since 0.4:
  - more animation controllers: flip, morph, vis, uv
  - NiPixelData textures are now displayed too
  - open gl 3d view is now in sync with the nif model:
    - changes to the data structure show up immediately
    - (no more double click to update)
  - some internal changes:
    - NifValue replaces QVariant
    - NifValue uses hardcoded streaming class

changes since 0.3:
  - switched to Qt 4.1
  - new main window layout
  - multiple main windows
  - copy/paste blocks and compounds
  - draw bones/nodes
  - keyframe animations
  - draw hidden meshes
  - nif version check on load
  - fixed various texture issues
  - vector, matrix, quat are now internal types

changes since 0.2:
  - opengl view rewritten:
  - fixed display of better body meshes
  - fixed model center on load
  - improved alpha blending
  - improved material colors and lighting
  - fixed performance issue with textures on cdrom
  - application icon by Shon
  - changed source code license to BSD

changes since 0.1:
  - display block hirarchy
  - display abstract ancestors inline
  - display block names at top level
  - display link target name/type
  - follow link (available through context menu)
  - multi lined strings fixed (line breaks converted to and from "\r\n")
  - fixed compressed tga textures (made with the gimp)
  - proper display of skinned meshes
  - center model on load
