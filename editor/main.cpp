// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Blutmond editor (first version): opens Tiled rooms, cooks them for a profile and shows what
// the hardware gets: the room as the runtime renders it, the GBA screen at the camera, the
// tiles, the palette banks, the hardware budget and the cooker's report. Rooms reload when
// the file changes, so it can be used next to Tiled.
//
// Run (F6) plays all rooms: on the PC in the game's window, or on a GBA over the Xboo cable
// (editor/runner.h). With "Live" on, every cook while it runs is pushed in as a hot reload.
//
//   bmeditor [project.bmproj | room.tmx...] [--profile gba|snes] [--dark] [--frames N --screenshot out.ppm] [--tab N]
//            [--game path] [--bmlink path] [--gba-image path] [--xboo device] [--reset none|xboo]
//            [--port N]
//
// Without arguments it opens assets/test.bmproj.

#include <GLES2/gl2.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "platform/sdl3/ui_sdl3.h"
#include "dialogs.h"
#include "graphics_view.h"
#include "palette_panel.h"
#include "project.h"
#include "project_panel.h"
#include "runner.h"
#include "ui/containers.h"
#include "ui/lists.h"
#include "ui/loader.h"
#include "ui/menus.h"
#include "ui/widgets.h"
#include "views.h"

extern "C" {
#include "game.h"
#include "hal_host.h"
#include "state.h"
}

using namespace ose::ui;
using namespace bm::editor;

namespace {

const char *kLayout = R"XML(<Panel theme-type="Background" anchors="full"><VBox anchors="full" separation="0">
  <MenuBar>
    <Menu title="File">
      <Item action="project.open"/><Item action="project.save_as"/>
      <Item action="room.new"/><Item action="room.add"/><Item action="room.remove"/>
      <Item action="asset.import"/><Item action="asset.open"/><Item action="asset.delete"/>
      <Item action="room.reload"/><Item action="app.quit"/>
    </Menu>
    <Menu title="Run"><Item action="run.start"/><Item action="run.pc"/><Item action="run.gba"/><Item action="run.stop"/><Item action="gfx.capture"/></Menu>
    <Menu title="View"><Item action="view.collision"/><Item action="view.objects"/><Item action="view.grid"/></Menu>
  </MenuBar>
  <PanelContainer theme-type="Toolbar"><HBox>
    <Label text="Profile"/>
    <OptionButton id="profile" items="GBA;SNES;Host"/>
    <Button text="Cook" action="room.reload"/>
    <Button text="Run" action="run.start"/>
    <OptionButton id="run_target" items="PC;GBA (Xboo)"/>
    <Button text="Stop" action="run.stop"/>
    <CheckBox id="live" text="Live" pressed="1"/>
    <CheckBox id="collision" text="Collision" pressed="1"/>
    <CheckBox id="objects" text="Objects" pressed="1"/>
    <CheckBox id="grid" text="Grid"/>
    <Control expand="1"/>
    <Label id="camera" text="Camera 0,0" clip-text="1"/>
  </HBox></PanelContainer>
  <HBox expand="1" separation="2">
    <PanelContainer min-width="300"><TabContainer>
      <VBox title="Project">
        <Label id="project_name" bold="1" clip-text="1"/>
        <Tree id="project_tree" expand="1"/>
        <Flow>
          <Button text="New room" action="room.new"/>
          <Button text="Add room" action="room.add"/>
          <Button text="Remove room" action="room.remove"/>
          <Button text="Import" action="asset.import"/>
          <Button text="Open" action="asset.open"/>
          <Button text="Delete" action="asset.delete"/>
          <Button text="Rescan" action="project.refresh"/>
        </Flow>
        <ItemList id="asset_info" min-size="0 120"/>
      </VBox>
      <VBox title="State">
        <Label id="state_totals" text="" clip-text="1"/>
        <ItemList id="state" expand="1"/>
      </VBox>
    </TabContainer></PanelContainer>
    <VBox expand="1">
      <TabContainer id="center" expand="1">
        <HBox title="Room" separation="2">
          <RoomOverview id="overview" expand="1"/>
          <PanelContainer min-width="330"><Scroll><VBox>
            <Label text="GBA screen" bold="1"/>
            <TextureRect id="screen" min-size="240 160"/>
            <Label text="Palette: click a colour to change it" bold="1"/>
            <PaletteView id="room_palette"/>
            <HBox>
              <ColorRect id="colour_swatch" min-size="28 28"/>
              <Label id="colour_title" text="" expand="1" clip-text="1"/>
            </HBox>
            <HBox>
              <Label text="R"/><SpinBox id="colour_r" expand="1"/>
              <Label text="G"/><SpinBox id="colour_g" expand="1"/>
              <Label text="B"/><SpinBox id="colour_b" expand="1"/>
            </HBox>
            <HBox>
              <OptionButton id="colour_scope" expand="1" items="Only for this profile;For all profiles"/>
              <Button id="colour_reset" text="Reset"/>
            </HBox>
            <ItemList id="colour_sources" min-size="0 80"/>
            <Label text="Hardware (this room, cooked)" bold="1"/>
            <Label id="hw_tiles" text="Tiles"/>
            <ProgressBar id="hw_tiles_bar"/>
            <Label id="hw_banks" text="Palette banks"/>
            <ProgressBar id="hw_banks_bar"/>
            <Label id="hw_maps" text="Maps"/>
            <ProgressBar id="hw_maps_bar"/>
            <Label id="hw_layers" text="Layers"/>
            <Label id="hw_pack" text="Pack"/>
            <Label text="Tiles" bold="1"/>
            <TextureRect id="tiles"/>
          </VBox></Scroll></PanelContainer>
        </HBox>
        <VBox title="Graphics" separation="0">
          <PanelContainer theme-type="Toolbar"><HBox>
            <Label text="Show"/>
            <OptionButton id="gfx_from" items="Editor preview;Running game"/>
            <Button text="Capture" action="gfx.capture"/>
            <CheckBox id="gfx_follow" text="Follow"/>
            <Label id="gfx_source" text="" expand="1" clip-text="1"/>
          </HBox></PanelContainer>
          <HBox expand="1" separation="2">
            <Scroll expand="1"><VramView id="gfx_vram"/></Scroll>
            <Scroll min-width="400"><VBox>
              <Label text="Screen" bold="1"/>
              <TextureRect id="gfx_screen" min-size="240 160"/>
              <Label text="Budget" bold="1"/>
              <VBox id="gfx_budgets"/>
              <Label text="Notes" bold="1"/>
              <ItemList id="gfx_notes" min-size="0 60"/>
              <Label text="Backgrounds" bold="1"/>
              <ItemList id="gfx_layers" min-size="0 80"/>
              <Label text="Sprites (OAM)" bold="1"/>
              <ItemList id="gfx_sprites" min-size="0 120"/>
              <HBox>
                <VBox><Label text="BG palette" bold="1"/><PaletteView id="gfx_bg_palette"/></VBox>
                <VBox><Label text="OBJ palette" bold="1"/><PaletteView id="gfx_obj_palette"/></VBox>
              </HBox>
              <Label text="Sprite time per line (white line = hardware limit)" bold="1"/>
              <LineCostView id="gfx_line_cost"/>
            </VBox></Scroll>
          </HBox>
          <PanelContainer theme-type="StatusBar"><HBox><Label id="gfx_hover" text="Point at video memory to see what it holds" clip-text="1"/></HBox></PanelContainer>
        </VBox>
      </TabContainer>
      <HBox separation="2">
        <PanelContainer expand="1"><VBox>
          <Label text="Report" bold="1"/>
          <ItemList id="report" min-size="0 110"/>
        </VBox></PanelContainer>
        <PanelContainer expand="1"><VBox>
          <Label text="Messages" bold="1"/>
          <ItemList id="runlog" min-size="0 110"/>
        </VBox></PanelContainer>
      </HBox>
    </VBox>
  </HBox>
  <PanelContainer theme-type="StatusBar"><HBox><Label id="status" text="Ready" expand="1" clip-text="1"/><Label id="run_state" text="Not running"/></HBox></PanelContainer>
</VBox></Panel>)XML";

struct Editor
{
  explicit Editor(sdl3::App &a) : app(a) {}
  sdl3::App &app;
  Project project;
  RoomOverview *overview = nullptr;
  TextureRect *screen = nullptr, *tiles = nullptr;
  ItemList *report = nullptr;
  ProjectPanel projectPanel;
  ProjectFile projectFile;  // as loaded at start
  PalettePanel palettePanel;
  Label *status = nullptr, *camera = nullptr;
  Label *hwTiles = nullptr, *hwBanks = nullptr, *hwMaps = nullptr, *hwLayers = nullptr, *hwPack = nullptr;
  ProgressBar *hwTilesBar = nullptr, *hwBanksBar = nullptr, *hwMapsBar = nullptr;
  ItemList *state = nullptr;
  Label *stateTotals = nullptr;
  ItemList *runLog = nullptr;
  Label *runState = nullptr;
  Runner runner;
  GraphicsInspector graphics;
  bool graphicsFromGame = false, graphicsFollow = false;
  float followTimer = 0;
  std::string previewSource;

  std::string snapshotPath() const
  {
    std::error_code ec;
    return (std::filesystem::temp_directory_path(ec) / "blutmond-editor-snapshot.bmps").string();
  }

  // The Graphics tab shows the preview's video state or a snapshot of the running game.
  void capture()
  {
    std::string path = snapshotPath();
    bool started = runner.capture(path, [this, path](bool ok) {
      if (!ok) return;
      std::vector<uint8_t> data;
      if (FILE *f = std::fopen(path.c_str(), "rb"))
      {
        data.resize(bmPpuSnapshotSize());
        data.resize(std::fread(data.data(), 1, data.size(), f));
        std::fclose(f);
      }
      static BmPpuState state;
      if (graphicsFromGame && bmPpuSnapshotRead(&state, data.data(), data.size()))
      {
        time_t t = time(nullptr);
        char when[32];
        std::strftime(when, sizeof(when), "%H:%M:%S", std::localtime(&t));
        graphics.show(state, std::string("The running game (") + (runner.gameRunning() ? "PC" : "GBA") + "), captured at " + when);
      }
    });
    if (!started && graphicsFromGame) graphicsSourceLabel("Nothing runs: press Run (F6), then Capture");
  }

  void graphicsSourceLabel(const std::string &text)
  {
    if (auto *l = app.ui().root()->findAs<Label>("gfx_source")) l->text = text;
  }

  RunTarget target = RunTarget::Pc;
  bool live = true;

  std::string runPackPath() const
  {
    std::error_code ec;
    return (std::filesystem::temp_directory_path(ec) / "blutmond-editor-run.bmpk").string();
  }

  void runLine(const std::string &line, bool error)
  {
    std::fprintf(stderr, "run: %s\n", line.c_str());
    // Newest first, so the latest line is always in view.
    runLog->items.insert(runLog->items.begin(), {line, error ? "error" : "info", {}, {}, {}, {}});
    if (runLog->items.size() > 200) runLog->items.pop_back();
  }

  bool cookLevel(std::string &path)
  {
    path = runPackPath();
    std::string error;
    if (project.cookLevel(path, &error)) return true;
    runLine("cannot cook the level: " + error, true);
    return false;
  }

  void startRun(RunTarget t)
  {
    std::string path;
    if (!cookLevel(path)) return;
    runner.run(t, path);
  }

  // After a cook asked for by the user or a changed file: into the running game.
  void pushLive()
  {
    if (!live || !runner.active()) return;
    std::string path;
    if (cookLevel(path)) runner.pushHot(path);
  }

  // The state map (docs/spec/state.md section 4): every field of the engine's game state with
  // its type, size and flags, and how much a save game, a link transfer and a snapshot take.
  void fillStateMap()
  {
    static BmGame game;
    BmStateBlock block = bmGameStateBlock(&game);
    static const char *types[] = {"u8", "s8", "u16", "s16", "u32", "s32", "bool", "fix"};
    for (uint16_t i = 0; i < block.fieldCount; i++)
    {
      const BmStateField &f = block.pFields[i];
      std::string flags;
      if (f.flags & BM_STATE_SAVE) flags += " save";
      if (f.flags & BM_STATE_LINK) flags += " link";
      if (f.flags & BM_STATE_SNAPSHOT) flags += " snapshot";
      if (f.flags & BM_STATE_TRANSIENT) flags += " transient";
      if (f.flags & BM_STATE_DERIVED) flags += " derived";
      bool unflagged = f.flags == 0;
      std::string text = std::string(block.name) + "." + f.name + "  " + types[f.type] + "  " +
                         std::to_string(bmStateTypeSize(f.type) * f.count) + " B " + (unflagged ? " (no flag: snapshot only)" : flags);
      state->items.push_back({text, unflagged ? "warning" : (f.flags & BM_STATE_SAVE) ? "floppy-o" : "circle-o", {}, {}, {}, {}});
    }
    auto bytes = [&](uint8_t mask) { return std::to_string(bmStateSize(&block, 1, mask) - BM_STATE_HEADER_SIZE); };
    stateTotals->text = "Save game " + bytes(BM_STATE_SAVE) + " B, link " + bytes(BM_STATE_LINK) + " B, snapshot " +
                        bytes(BM_STATE_SNAPSHOT) + " B";
  }
  float reloadTimer = 0;

  void replaceTexture(TextureHandle &h, const Rgba &img)
  {
    if (h) app.renderer().destroyTexture(h);
    h = img.width ? app.renderer().createTexture(img.width, img.height, img.pixels.data(), true) : TextureHandle{};
  }

  void updateScreen()
  {
    int cx = 0, cy = 0;
    Rgba img = project.renderScreen(overview->cameraX, overview->cameraY, &cx, &cy);
    overview->cameraX = cx;
    overview->cameraY = cy;
    replaceTexture(screen->texture, img);
    previewSource = "Editor preview: " + (project.currentRoom() >= 0 ? project.rooms()[size_t(project.currentRoom())] : std::string()) +
                    " at " + std::to_string(cx) + "," + std::to_string(cy) + " (backgrounds only; capture the running game for sprites)";
    if (!graphicsFromGame && img.width) graphics.show(*bmHalHostGetPpu(), previewSource);
    screen->textureSize = {float(img.width), float(img.height)};
    camera->text = "Camera " + std::to_string(cx) + "," + std::to_string(cy);
  }

  // The room list of the project changed: keep the room that was shown, if it is still there.
  void roomsChanged()
  {
    std::string current = project.currentRoom() >= 0 ? project.rooms()[size_t(project.currentRoom())] : std::string();
    project.setRooms(projectPanel.file().rooms);
    for (size_t i = 0; i < project.rooms().size(); i++)
      if (project.rooms()[i] == current) project.selectRoom(int(i));
    projectPanel.showRoom(project.currentRoom());
    cook();
  }

  void cook()
  {
    bool ok = project.cook();
    palettePanel.refresh();
    replaceTexture(overview->texture, project.roomImage());
    replaceTexture(tiles->texture, project.tileSheet());
    tiles->textureSize = {float(project.tileSheet().width * 2), float(project.tileSheet().height * 2)};
    tiles->customMinSize = tiles->textureSize;
    updateScreen();

    report->items.clear();
    int errors = 0, warnings = 0;
    for (const auto &e : project.report().entries)
    {
      const char *icon = e.level == bm::ReportEntry::Level::Error ? "error" : e.level == bm::ReportEntry::Level::Warning ? "warning" : "info";
      errors += e.level == bm::ReportEntry::Level::Error;
      warnings += e.level == bm::ReportEntry::Level::Warning;
      report->items.push_back({e.kind + ": " + e.message, icon, {}, {}, e.asset, {}});
    }
    if (!project.error().empty()) report->items.push_back({project.error(), "error", {}, {}, {}, {}});

    const HardwareUse &hw = project.hardware();
    auto ratio = [](int a, int b) { return b > 0 ? std::min(1.0f, float(a) / float(b)) : 0.0f; };
    hwTiles->text = "Tiles: " + std::to_string(hw.tiles) + " (" + std::to_string(hw.tileBytes) + " of " + std::to_string(hw.tileBytesMax) + " bytes)";
    hwTilesBar->value = ratio(hw.tileBytes, hw.tileBytesMax);
    hwBanks->text = "Palette banks: " + std::to_string(hw.paletteBanks) + " of " + std::to_string(hw.paletteBanksMax);
    hwBanksBar->value = ratio(hw.paletteBanks, hw.paletteBanksMax);
    hwMaps->text = "Hardware maps: " + std::to_string(hw.mapBytes / 1024) + " of " + std::to_string(hw.mapBytesMax / 1024) + " KB";
    hwMapsBar->value = ratio(hw.mapBytes, hw.mapBytesMax);
    hwLayers->text = "Layers: " + std::to_string(hw.layers) + " of " + std::to_string(hw.layersMax) + " (16 colours)";
    hwPack->text = "Pack: " + std::to_string(hw.packBytes) + " bytes";

    std::string room = project.currentRoom() >= 0 ? project.rooms()[size_t(project.currentRoom())] : "";
    status->text = (ok ? "Cooked " : "Cooking failed: ") + room + " for " + project.profile() + " (" +
                   std::to_string(errors) + " errors, " + std::to_string(warnings) + " warnings)";
  }

  void build()
  {
    Context &ui = app.ui();
    ui.actions().add({"room.reload", "Cook again", "refresh", "F5", "", [this] { cook(); pushLive(); }, nullptr, nullptr});
    ui.actions().add({"run.start", "Run", "play", "F6", "Play all rooms on the chosen target", [this] { startRun(target); }, nullptr, nullptr});
    ui.actions().add({"run.pc", "Run on the PC", "play", "", "", [this] { startRun(RunTarget::Pc); }, nullptr, nullptr});
    ui.actions().add({"run.gba", "Run on the GBA (Xboo)", "gamepad", "F7", "", [this] { startRun(RunTarget::Gba); }, nullptr, nullptr});
    ui.actions().add({"run.stop", "Stop", "stop", "Shift+F6", "", [this] { runner.stop(); }, [this] { return runner.active(); }, nullptr});
    ui.actions().add({"app.quit", "Quit", "", "Ctrl+Q", "", [this] { app.quit(); }, nullptr, nullptr});
    ui.actions().add({"view.collision", "Collision", "", "C", "", [this] { overview->showCollision = !overview->showCollision; }, nullptr, nullptr});
    ui.actions().add({"view.objects", "Objects", "", "O", "", [this] { overview->showObjects = !overview->showObjects; }, nullptr, nullptr});
    ui.actions().add({"view.grid", "Grid", "", "G", "", [this] { overview->showGrid = !overview->showGrid; }, nullptr, nullptr});

    registerControlType("RoomOverview", [] { return std::make_unique<RoomOverview>(); });
    ui.actions().add({"project.open", "Open project...", "folder-open", "Ctrl+O", "", [this] { projectPanel.openProject(); }, nullptr, nullptr});
    ui.actions().add({"project.save_as", "Save project as...", "save", "", "", [this] { projectPanel.saveProjectAs(); }, nullptr, nullptr});
    ui.actions().add({"project.refresh", "Rescan the asset folders", "refresh", "", "", [this] { projectPanel.refresh(); }, nullptr, nullptr});
    ui.actions().add({"room.new", "New room...", "plus", "Ctrl+N", "", [this] { projectPanel.newRoom(); }, nullptr, nullptr});
    ui.actions().add({"room.add", "Add room...", "map", "", "", [this] { projectPanel.addRoom(); }, nullptr, nullptr});
    ui.actions().add({"room.remove", "Remove room from the project", "minus", "", "", [this] { projectPanel.removeRoom(); }, nullptr, nullptr});
    ui.actions().add({"asset.import", "Import assets...", "copy", "Ctrl+I", "", [this] { projectPanel.importAssets(); }, nullptr, nullptr});
    ui.actions().add({"asset.open", "Open in its program", "pencil", "", "", [this] { projectPanel.openSelected(); }, nullptr, nullptr});
    ui.actions().add({"asset.delete", "Delete asset...", "trash", "", "", [this] { projectPanel.deleteSelected(); }, nullptr, nullptr});
    registerGraphicsControls();
    ui.actions().add({"gfx.capture", "Capture the running game's graphics", "eye", "F8", "", [this] {
                        graphicsFromGame = true;
                        app.ui().root()->findAs<OptionButton>("gfx_from")->select(1, false);
                        capture();
                      }, nullptr, nullptr});
    LayoutLoader loader(ui);
    auto root = loader.loadText(kLayout);
    for (const auto &e : loader.errors()) std::fprintf(stderr, "layout: %s\n", e.c_str());
    overview = root->findAs<RoomOverview>("overview");
    screen = root->findAs<TextureRect>("screen");
    tiles = root->findAs<TextureRect>("tiles");
    report = root->findAs<ItemList>("report");
    status = root->findAs<Label>("status");
    camera = root->findAs<Label>("camera");
    hwTiles = root->findAs<Label>("hw_tiles");
    hwBanks = root->findAs<Label>("hw_banks");
    hwMaps = root->findAs<Label>("hw_maps");
    hwLayers = root->findAs<Label>("hw_layers");
    hwPack = root->findAs<Label>("hw_pack");
    hwTilesBar = root->findAs<ProgressBar>("hw_tiles_bar");
    hwBanksBar = root->findAs<ProgressBar>("hw_banks_bar");
    hwMapsBar = root->findAs<ProgressBar>("hw_maps_bar");
    state = root->findAs<ItemList>("state");
    stateTotals = root->findAs<Label>("state_totals");
    graphics.bind(*root, app.renderer());
    projectPanel.bind(*root, ui);
    projectPanel.log = [this](const std::string &line, bool error) { runLine(line, error); };
    projectPanel.roomsChanged = [this] { roomsChanged(); };
    projectPanel.roomPicked = [this](int i) {
      projectPanel.showRoom(i);
      if (i == project.currentRoom()) return;
      project.selectRoom(i);
      cook();
    };
    palettePanel.bind(*root, project);
    palettePanel.catalog = &projectPanel.catalog();
    palettePanel.log = projectPanel.log;
    palettePanel.changed = [this] {
      cook();
      pushLive();
      projectPanel.refresh();
    };
    ui.filesDropped = [this](const std::vector<std::string> &files, ose::Vec2) { projectPanel.importFiles(files); };
    root->findAs<OptionButton>("gfx_from")->itemSelected.connect([this](int i) {
      graphicsFromGame = i == 1;
      if (graphicsFromGame) capture();
      else updateScreen();
    });
    root->findAs<CheckBox>("gfx_follow")->toggled.connect([this](bool on) { graphicsFollow = on; });
    runLog = root->findAs<ItemList>("runlog");
    runLog->tileSize = 0;
    runState = root->findAs<Label>("run_state");
    runner.log = [this](const std::string &line, bool error) { runLine(line, error); };
    root->findAs<OptionButton>("run_target")->itemSelected.connect([this](int i) { target = RunTarget(i); });
    root->findAs<CheckBox>("live")->toggled.connect([this](bool on) { live = on; });
    state->tileSize = 0;
    fillStateMap();

    report->tileSize = 0;
    tiles->keepAspect = false;
    overview->project = &project;
    screen->keepAspect = true;
    overview->cameraMoved = [this](int x, int y) {
      overview->cameraX = x;
      overview->cameraY = y;
      updateScreen();
    };
    projectPanel.setProject(projectFile);
    project.setRooms(projectFile.rooms);
    projectPanel.showRoom(project.currentRoom());
    auto *profile = root->findAs<OptionButton>("profile");
    profile->itemSelected.connect([this](int i) {
      static const char *names[] = {"gba", "snes", "host"};
      project.setProfile(names[i]);
      cook();
    });
    profile->select(project.profile() == "snes" ? 1 : project.profile() == "host" ? 2 : 0, false);
    root->findAs<CheckBox>("collision")->toggled.connect([this](bool on) { overview->showCollision = on; });
    root->findAs<CheckBox>("objects")->toggled.connect([this](bool on) { overview->showObjects = on; });
    root->findAs<CheckBox>("grid")->toggled.connect([this](bool on) { overview->showGrid = on; });
    ui.setRoot(std::move(root));
    cook();
  }

  void update(float dt)
  {
    pollFileDialogs();
    palettePanel.update(dt);
    reloadTimer += dt;
    if (reloadTimer > 1.0f)
    {
      reloadTimer = 0;
      if (project.changedOnDisk())
      {
        cook();
        pushLive();
      }
    }
    runner.update();
    followTimer += dt;
    if (graphicsFromGame && graphicsFollow && runner.active() && !runner.busy() && followTimer > 1.0f)
    {
      followTimer = 0;
      capture();
    }
    runState->text = runner.busy() ? runner.state() : runner.state() + (runner.active() && live ? " (live)" : "");
  }
};

int writeScreenshot(const char *path)
{
  GLint vp[4] = {};
  glGetIntegerv(GL_VIEWPORT, vp);
  std::vector<unsigned char> px(size_t(vp[2]) * vp[3] * 4);
  glReadPixels(0, 0, vp[2], vp[3], GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  FILE *f = std::fopen(path, "wb");
  if (!f) return 1;
  std::fprintf(f, "P6\n%d %d\n255\n", vp[2], vp[3]);
  for (int y = vp[3] - 1; y >= 0; y--)
    for (int x = 0; x < vp[2]; x++) std::fwrite(&px[(size_t(y) * vp[2] + x) * 4], 1, 3, f);
  std::fclose(f);
  return 0;
}

}  // namespace

int main(int argc, char **argv)
{
  std::vector<std::string> roomFiles;
  std::string projectPath;
  std::string profile = "gba";
  bool dark = false;
  int frames = 0;
  const char *screenshot = nullptr;
  int tab = 0;
  RunSettings run;
#ifdef BM_EDITOR_GAME
  run.game = BM_EDITOR_GAME;
#endif
#ifdef BM_EDITOR_BMLINK
  run.bmlink = BM_EDITOR_BMLINK;
#endif
#ifdef BM_EDITOR_GBA_IMAGE
  run.gbaImage = BM_EDITOR_GBA_IMAGE;
#endif
  run.load();
  for (int i = 1; i < argc; i++)
  {
    if (std::strcmp(argv[i], "--profile") == 0 && i + 1 < argc) profile = argv[++i];
    else if (std::strcmp(argv[i], "--dark") == 0) dark = true;
    else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
    else if (std::strcmp(argv[i], "--tab") == 0 && i + 1 < argc) tab = std::atoi(argv[++i]);
    else if (std::strncmp(argv[i], "--", 2) == 0 && i + 1 < argc && run.set(argv[i] + 2, argv[i + 1])) i++;
    else if (std::filesystem::path(argv[i]).extension() == ".bmproj") projectPath = argv[i];
    else roomFiles.push_back(argv[i]);
  }
#ifdef BM_EDITOR_DEFAULT_PROJECT
  if (roomFiles.empty() && projectPath.empty()) projectPath = BM_EDITOR_DEFAULT_PROJECT;
#endif

  // A project file, or rooms given on the command line (a project that is not saved yet;
  // their folders are its asset folders).
  ProjectFile projectFile;
  if (!projectPath.empty())
  {
    try
    {
      projectFile = ProjectFile::load(projectPath);
    }
    catch (const std::exception &e)
    {
      std::fprintf(stderr, "%s\n", e.what());
      return 1;
    }
  }
  else
  {
    for (const std::string &r : roomFiles)
    {
      projectFile.addRoom(r);
      std::string dir = normalPath(std::filesystem::path(normalPath(r)).parent_path().string());
      if (std::find(projectFile.assetDirs.begin(), projectFile.assetDirs.end(), dir) == projectFile.assetDirs.end())
        projectFile.assetDirs.push_back(dir);
    }
  }

  sdl3::App app;
  sdl3::AppDesc desc;
  desc.title = "Blutmond";
  desc.width = 1440;
  desc.height = 860;
  desc.theme = dark ? Theme::makeDark(OSE_UI_FONT_DIR) : Theme::makeClassic(OSE_UI_FONT_DIR);
  std::string error;
  if (!app.init(desc, &error))
  {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }

  Editor editor(app);
  editor.runner.settings = run;
  editor.projectFile = projectFile;
#ifdef BM_EDITOR_ENGINE_DIR
  editor.projectPanel.engineDir = BM_EDITOR_ENGINE_DIR;
#endif
  editor.project.setProfile(profile);
  editor.build();
  if (auto *center = app.ui().root()->findAs<TabContainer>("center")) center->setCurrent(tab);

  if (frames > 0)
  {
    for (int i = 0; i < frames; i++)
      if (!app.frame([&](float dt) { editor.update(dt); })) break;
    return screenshot ? writeScreenshot(screenshot) : 0;
  }
  app.run([&](float dt) { editor.update(dt); });
  return 0;
}
