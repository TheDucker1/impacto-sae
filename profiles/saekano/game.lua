root.LayerCount = 100;
root.GameFeatures = GameFeature.Renderer2D | GameFeature.Input | GameFeature.Audio | GameFeature.Video;
root.DesignWidth = 960;
root.DesignHeight = 544;
root.WindowName = "Saekano";

root.Saekano = {
    GameDataPath = root.BasePaths.RootGamedataDir .. "/saekano",
    LauncherAssetsPath = root.BasePaths.RootInstallDir .. "/saekano-launcher",
};

include(root.BasePaths.RootProfilesDir .. "/saekano/files.lua");
include(root.BasePaths.RootProfilesDir .. "/saekano/hud/titlemenu.lua");
