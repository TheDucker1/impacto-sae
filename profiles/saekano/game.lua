-- The native UI and script coordinates are converted into this design aspect.
root.DesignWidth = 960;
root.DesignHeight = 544;
root.WindowName = "Saekano";
root.OpenGLCompatibility = true;

root.Saekano = {
    GameDataPath = root.BasePaths.RootGamedataDir .. "/saekano",
    LauncherAssetsPath = root.BasePaths.RootInstallDir .. "/saekano-launcher",
};

include(root.BasePaths.RootProfilesDir .. "/saekano/files.lua");
include(root.BasePaths.RootProfilesDir .. "/saekano/hud/titlemenu.lua");
