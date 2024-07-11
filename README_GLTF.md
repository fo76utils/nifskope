# NifSkope glTF 2.0 Import and Export v1.2

# glTF Import

glTF Import into NifSkope currently supports static and (with limitations) skinned meshes. Skeleton and material data are not imported.

# glTF Export

glTF export is currently supported on static and skinned Starfield meshes.

To view and export Starfield meshes, you must first:

1. Enable and add the path to your Starfield installation in Settings > Resources.
2. Add the Meshes archives or extracted folders containing `geometries` to Paths in Settings > Resources, under Starfield.

## Skinned meshes

### Pre-Export

If you do not desire the full posable skeleton, you may skip these steps.

If you do not paste in the COM skeleton, a flat skeleton will be reconstructed for you.
**Please note:** You will receive a warning after export that the skeleton has been reconstructed for you. This is fine.

If you desire the full posable skeleton, and the mesh does not have a skeleton with a `COM` or `COM_Twin` NiNode in its NIF:

1. Open the skeleton.nif for that skinned mesh (e.g. `meshes\actors\human\characterassets\skeleton.nif`)
2. Copy (Ctrl-C) the `COM` NiNode in skeleton.nif
3. Paste (Ctrl-V) the entire `COM` branch onto the mesh NIF's root NiNode (0)
4. Export to glTF

### Pre-Blender Import

As of Exporter v1.1 you should **no longer need to uncheck "Guess Original Bind Pose"**.

## Materials

glTF export includes a limited set of material settings, and textures from the first layer of the material, which are saved in the output .bin file in PNG format. Replacement colors are stored as 1x1 textures. Texture quality can be configured in the general settings with the 'Export texture mip level' option. A mip level of -1 disables texture export.

Import is limited to using the name of the material as a material path (name of the created BSLightingShaderProperty block), or the "Material Path" extra data if available.

## LOD

Exporting and importing LOD meshes is disabled by default, and can be enabled in the general settings. When enabled, LOD meshes use the MSFT\_lod glTF extension.

## Blender scripts

Blender scripts are provided for use with glTF exports. They may be opened and run from the Scripting tab inside Blender.

1. `gltf_lod_blender_script.py` is included in the `scripts` folder for managing LOD visibility in exported glTF.
