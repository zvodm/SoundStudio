#pragma once
#include <string>
#include <vector>

// Loads every .lua script in AppPaths::PluginsDir() into its own isolated
// Lua state, binds a small drawing/widget/engine API (see PluginManager.cpp)
// under the global `ss` table, and gives each script its own docked ImGui
// panel every frame.
//
// A plugin script is just a .lua file that optionally defines:
//   PLUGIN_NAME       -- string, the panel's title (defaults to the filename)
//   function Init()   -- called once, right after the script loads
//   function Update(dt)
//   function DrawUI() -- called every frame, between this plugin's own
//                        ImGui::Begin()/End() -- draw widgets via ss.*
//
// Two default plugins (a Wave Editor and a colored master Oscilloscope)
// ship as embedded Lua source (see plugin/DefaultPlugins.h) and are
// written into Plugins/ on first run if not already present there --
// exactly like the default imgui.ini layout, they're then fully normal,
// user-editable files from that point on.
class PluginManager
{
public:
    // Scans PluginsDir() for *.lua and loads each one. Safe to call again
    // later (e.g. a "Reload Plugins" button) -- unloads whatever was
    // previously loaded first.
    void LoadAll();

    void Update(float dt); // calls each plugin's optional Update(dt)
    void DrawAll();         // opens each plugin's window and calls its optional DrawUI()

    void Shutdown(); // closes every Lua state; safe to call even if LoadAll() was never called

    int PluginCount() const { return (int)plugins_.size(); }

private:
    struct LoadedPlugin
    {
        std::string name;
        std::string filePath;
        struct lua_State* L = nullptr;
    };

    void LoadPlugin(const std::string& path, const std::string& defaultName);

    std::vector<LoadedPlugin> plugins_;
};
