### Invisible Starfield meshes, or missing textures on models from other games

This issue is most often caused by incorrect resource settings. Make sure that the game is enabled (Options/Settings.../Resources/Games), and that data paths are set up for it on the Paths tab. Any number of archives and/or folders can be added as paths, and folders are loaded recursively, so the simplest configuration consists of just the data path of the game (e.g. K:/SteamLibrary/steamapps/common/Starfield/Data). If the same resource can be found under multiple paths, then the one listed first has the highest precedence.

### Very long load times

Initializing the asset database can potentially take a long time if the number of loose resource files is unusually large. On Windows, performance is impacted much more by the number of resource folders than the number of files, so the problem is most often caused by having all Starfield geometry data (over 350,000 folders, each containing one .mesh file) added as loose resources. In this case, if the geometries are under the main game data path, it is recommended to add archives manually under Options/Settings.../Resources/Paths, and additional data folders only when they are really needed, instead of the simple configuration that recursively loads all data. Note that the 'Add Archive or File' button allows selecting multiple files at once, and that it is allowed to add sub-folders like K:/SteamLibrary/steamapps/common/Starfield/Data/geometries/MyMod as data paths.

### Rendering issues on Linux

Running NifSkope under Wayland on Linux currently requires setting the QT\_QPA\_PLATFORM environment variable to "xcb":

    QT_QPA_PLATFORM=xcb ./NifSkope

Additionally, there may be issues with anti-aliasing (MSAA) settings above 4x. For this reason, MSAA is limited to a maximum of 4x on Linux by default, but this can be disabled by adding the following to the configuration file (NifTools/NifSkope 2.0.conf) under Settings:

    Render\General\Antialiasing%20Limit=4
