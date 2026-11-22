// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Blutmond editor (first version): opens Tiled rooms, cooks them for a profile and shows what
// the hardware gets: the room as the runtime renders it, the GBA screen at the camera, the
// tiles, the palette banks, the hardware budget and the cooker's report. Rooms reload when
// the file changes, so it can be used next to Tiled.
//
// Run (F6) plays all rooms: on the PC in the game's window, or on a GBA over the Xboo cable
// (editor/runner.h). With "Live" on, every cook while it runs is pushed in as a hot reload.
//
//   bmeditor [project.bmproj | room.tmx...] [--profile gba|snes] [--theme classic|light|dark] [--target pc|gba] [--frames N --screenshot out.ppm] [--tab N] [--action name]
//            [--game path] [--bmlink path] [--gba-image path] [--xboo device] [--reset none|xboo]
//            [--port N] [--gba-rom path]
//
// Export (Ctrl+E) cooks all rooms and writes one GBA image with them (editor/rom_export.h),
// to play in an emulator or from a flash cartridge. In the browser it is downloaded.
//
// Without arguments it opens assets/test.bmproj.

#include <GLES2/gl2.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "platform/sdl3/ui_sdl3.h"
#include "animation_panel.h"
#include "code_edit.h"
#include "dialogs.h"
#include "graphics_view.h"
#include "palette_panel.h"
#include "project.h"
#include "project_panel.h"
#include "rom_export.h"
#include "runner.h"
#include "script_panel.h"
#include "sound_panel.h"
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

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
// Hands a file of the page's file system to the browser as a download.
EM_JS(void, bmDownloadFile, (const char *path, const char *name), {
  const data = FS.readFile(UTF8ToString(path));
  const url = URL.createObjectURL(new Blob([data], { type: 'application/octet-stream' }));
  const a = document.createElement('a');
  a.href = url;
  a.download = UTF8ToString(name);
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

// Run in the browser: the game (game.html?editor) in a frame in the corner of this page; the
// pack is sent with postMessage once the game says it is ready. mode 0: start from the
// beginning, 3: reload keeping the game going (BmLiveMode).
EM_JS(void, bmWebGameRun, (const char *path, int mode), {
  const data = FS.readFile(UTF8ToString(path));
  let g = window.bmGame;
  if (!g || !document.body.contains(g.box)) {
    const box = document.createElement('div');
    box.style.cssText = 'position:fixed;right:16px;bottom:16px;z-index:10;background:#202228;border:1px solid #555;' +
                        'box-shadow:0 4px 16px rgba(0,0,0,.5);font:13px system-ui,sans-serif;color:#ddd;' +
                        'display:flex;flex-direction:column;width:480px;height:350px;';
    const bar = document.createElement('div');
    bar.style.cssText = 'display:flex;align-items:center;gap:8px;padding:4px 6px 4px 10px;cursor:move;user-select:none;';
    const title = document.createElement('span');
    title.textContent = 'Game - click into it to play';
    title.style.flex = '1';
    const close = document.createElement('button');
    close.textContent = '×';
    close.title = 'Stop';
    close.style.cssText = 'border:0;background:none;color:inherit;font-size:16px;cursor:pointer;';
    close.onclick = () => box.remove();
    bar.append(title, close);
    const frame = document.createElement('iframe');
    frame.src = 'game.html?editor' + (window.BM_VERSION ? '&v=' + window.BM_VERSION : '');
    frame.style.cssText = 'display:block;flex:1;width:100%;min-height:0;border:0;';
    frame.allow = 'autoplay; gamepad';
    // A grip in the bottom right corner resizes the frame; the game keeps its aspect inside.
    const grip = document.createElement('div');
    grip.title = 'Drag to resize';
    grip.style.cssText = 'position:absolute;right:0;bottom:0;width:16px;height:16px;cursor:nwse-resize;z-index:1;' +
                         'background:linear-gradient(135deg,transparent 50%,#888 50%,#888 60%,transparent 60%,transparent 70%,#888 70%,#888 80%,transparent 80%);';
    box.append(bar, frame, grip);
    document.body.appendChild(box);
    // While the pointer is held, the page gets its moves (not the game's frame under it).
    const anchorTopLeft = () => {
      const r = box.getBoundingClientRect();
      box.style.left = r.left + 'px';
      box.style.top = r.top + 'px';
      box.style.right = box.style.bottom = 'auto';
      return r;
    };
    // Drag the frame by its title bar.
    bar.onpointerdown = (e) => {
      if (e.target === close) return;
      const r = anchorTopLeft(), dx = e.clientX - r.left, dy = e.clientY - r.top;
      bar.setPointerCapture(e.pointerId);
      bar.onpointermove = (m) => {
        box.style.left = Math.min(Math.max(0, m.clientX - dx), window.innerWidth - 40) + 'px';
        box.style.top = Math.min(Math.max(0, m.clientY - dy), window.innerHeight - 24) + 'px';
      };
      bar.onpointerup = () => { bar.onpointermove = null; };
    };
    grip.onpointerdown = (e) => {
      e.preventDefault();
      const r = anchorTopLeft(), x0 = e.clientX, y0 = e.clientY;
      const barHeight = bar.getBoundingClientRect().height;
      grip.setPointerCapture(e.pointerId);
      grip.onpointermove = (m) => {
        box.style.width = Math.max(240, Math.min(r.width + m.clientX - x0, window.innerWidth - r.left)) + 'px';
        box.style.height = Math.max(160 + barHeight, Math.min(r.height + m.clientY - y0, window.innerHeight - r.top)) + 'px';
      };
      grip.onpointerup = () => { grip.onpointermove = null; };
    };
    g = window.bmGame = { box: box, frame: frame, ready: false, queue: [] };
    if (!window.bmGameListening) {
      window.bmGameListening = true;
      window.addEventListener('message', (e) => {
        const game = window.bmGame;
        if (!game || e.source !== game.frame.contentWindow || !e.data || e.data.type !== 'blutmond-ready') return;
        game.ready = true;
        for (const m of game.queue) game.frame.contentWindow.postMessage(m, '*');
        game.queue = [];
        game.frame.focus();
      });
    }
    mode = 0;   // a new game starts from the beginning
  }
  const message = { type: 'blutmond-pack', data: data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength), mode: mode };
  if (g.ready) g.frame.contentWindow.postMessage(message, '*');
  else g.queue.push(message);
});

EM_JS(int, bmWebGameOpen, (void), {
  return window.bmGame && document.body.contains(window.bmGame.box) ? 1 : 0;
});

EM_JS(void, bmWebGameStop, (void), {
  if (window.bmGame) window.bmGame.box.remove();
  window.bmGame = null;
});
#endif

// What the editor was built from (cmake/version.cmake writes them): Help > Version.
extern "C" {
extern const char *const bm_build_commit;
extern const int bm_build_changed;
extern const char *const bm_build_date;
}

using namespace ose::ui;
using namespace bm::editor;

namespace {

const char *kLayout = R"XML(<Panel theme-type="Background" anchors="full"><VBox anchors="full" separation="0">
  <MenuBar>
    <Menu title="File">
      <Item action="project.open"/><Item action="project.save_as"/>
      <Item action="room.new"/><Item action="room.add"/><Item action="room.remove"/>
      <Item action="sprite.new"/>
      <Item action="script.new"/>
      <Item action="song.new"/>
      <Item action="asset.import"/><Item action="asset.open"/><Item action="asset.delete"/>
      <Item action="room.reload"/><Item action="project.export"/><Item action="app.quit"/>
    </Menu>
    <Menu title="Run"><Item action="run.start"/><Item action="run.pc"/><Item action="run.gba"/><Item action="run.stop"/><Item action="gfx.capture"/>
      <Item action="sync.off"/><Item action="sync.gba_pc"/><Item action="sync.pc_gba"/></Menu>
    <Menu title="View"><Item action="view.collision"/><Item action="view.objects"/><Item action="view.grid"/>
      <Item action="theme.classic"/><Item action="theme.light"/><Item action="theme.dark"/></Menu>
    <Menu title="Help"><Item action="help.version"/></Menu>
  </MenuBar>
  <PanelContainer theme-type="Toolbar"><Flow>
    <Label text="Profile"/>
    <OptionButton id="profile" items="GBA;SNES;Host"/>
    <Button text="Cook" action="room.reload"/>
    <Button text="Run" action="run.start"/>
    <OptionButton id="run_target" items="PC;GBA (Xboo)"/>
    <Button text="Stop" action="run.stop"/>
    <Button text="Export" action="project.export" tooltip="Write the game as one GBA image for an emulator or a flash cartridge"/>
    <CheckBox id="live" text="Live" pressed="1"/>
    <Label text="  Sync"/>
    <Button id="sync_off" text="Off" toggle="1" pressed="1" action="sync.off" tooltip="The GBA and the PC game each play on their own"/>
    <Button id="sync_gba_pc" text="GBA > PC" toggle="1" action="sync.gba_pc" tooltip="Play on the GBA; the PC game plays along"/>
    <Button id="sync_pc_gba" text="PC > GBA" toggle="1" action="sync.pc_gba" tooltip="Play in the PC window; the GBA plays along"/>
    <CheckBox id="collision" text="Collision" pressed="1"/>
    <CheckBox id="objects" text="Objects" pressed="1"/>
    <CheckBox id="grid" text="Grid"/>
    <Label id="camera" text="Camera 0,0" clip-text="1"/>
  </Flow></PanelContainer>
  <Scroll expand="1" horizontal="1" vertical="1"><HSplit fixed="300">
    <PanelContainer min-width="160"><TabContainer>
      <VBox title="Project">
        <Label id="project_name" bold="1" clip-text="1"/>
        <Scroll expand="1"><Tree id="project_tree"/></Scroll>
        <Flow>
          <Button text="New room" action="room.new"/>
          <Button text="Add room" action="room.add"/>
          <Button text="Remove room" action="room.remove"/>
          <Button text="Import" action="asset.import"/>
          <Button text="Open" action="asset.open"/>
          <Button text="Delete" action="asset.delete"/>
          <Button text="Rescan" action="project.refresh"/>
        </Flow>
        <Scroll min-size="0 60"><ItemList id="asset_info"/></Scroll>
      </VBox>
      <VBox title="State">
        <Label id="state_totals" text="" clip-text="1"/>
        <Scroll expand="1"><ItemList id="state"/></Scroll>
      </VBox>
    </TabContainer></PanelContainer>
    <VSplit fixed-end="150">
      <TabContainer id="center" expand="1">
        <HSplit title="Room" fixed-end="330">
          <RoomOverview id="overview" expand="1"/>
          <PanelContainer min-width="260"><Scroll><VBox>
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
            <Scroll min-size="0 80"><ItemList id="colour_sources"/></Scroll>
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
        </HSplit>
        <VBox title="Animation" separation="0">
          <PanelContainer theme-type="Toolbar"><HBox>
            <Label text="Sprite"/>
            <OptionButton id="anim_sprite"/>
            <Button text="New sprite" action="sprite.new"/>
            <Label id="anim_info" text="" expand="1" clip-text="1"/>
          </HBox></PanelContainer>
          <HSplit expand="1" fixed="340">
            <PanelContainer min-width="220"><Scroll><VBox>
              <Label text="Sheet: click a frame" bold="1"/>
              <Scroll expand="1"><SheetView id="anim_sheet"/></Scroll>
              <HBox>
                <Label text="Grid"/><SpinBox id="anim_grid_w" expand="1"/><Label text="x"/><SpinBox id="anim_grid_h" expand="1"/>
                <Button id="anim_slice" text="Slice into frames"/>
              </HBox>
              <Label id="anim_frame_title" text="No frame selected" bold="1"/>
              <HBox>
                <Label text="X"/><SpinBox id="anim_fx" expand="1"/><Label text="Y"/><SpinBox id="anim_fy" expand="1"/>
                <Label text="W"/><SpinBox id="anim_fw" expand="1"/><Label text="H"/><SpinBox id="anim_fh" expand="1"/>
              </HBox>
              <HBox>
                <Label text="Anchor X"/><SpinBox id="anim_ax" expand="1"/><Label text="Y"/><SpinBox id="anim_ay" expand="1"/>
              </HBox>
              <CheckBox id="anim_own_hitbox" text="This frame has its own hitbox"/>
              <HBox>
                <Label text="Hitbox X"/><SpinBox id="anim_hx" expand="1"/><Label text="Y"/><SpinBox id="anim_hy" expand="1"/>
                <Label text="W"/><SpinBox id="anim_hw" expand="1"/><Label text="H"/><SpinBox id="anim_hh" expand="1"/>
              </HBox>
              <Label text="Boxes: green hurt, red hit, blue guard" bold="1"/>
              <CheckBox id="anim_box_sprite" text="The sprite's boxes (frames without their own)"/>
              <Scroll min-size="0 70"><ItemList id="anim_boxes"/></Scroll>
              <HBox>
                <OptionButton id="anim_box_kind" items="Hurt;Hit;Guard" expand="1"/>
                <Button id="anim_box_add" text="Add"/><Button id="anim_box_delete" text="Delete"/>
              </HBox>
              <HBox>
                <Label text="X"/><SpinBox id="anim_bx" expand="1"/><Label text="Y"/><SpinBox id="anim_by" expand="1"/>
                <Label text="W"/><SpinBox id="anim_bw" expand="1"/><Label text="H"/><SpinBox id="anim_bh" expand="1"/>
              </HBox>
              <HBox><Label text="Name"/><LineEdit id="anim_box_name" expand="1"/></HBox>
            </VBox></Scroll></PanelContainer>
            <HSplit fixed-end="300">
            <VBox expand="1">
              <HBox>
                <Button id="anim_play" text="Pause"/>
                <CheckBox id="anim_show_hitbox" text="Hitbox" pressed="1"/>
                <CheckBox id="anim_show_boxes" text="Boxes" pressed="1"/>
                <CheckBox id="anim_show_pieces" text="Hardware sprites" pressed="1"/>
              </HBox>
              <AnimPreview id="anim_preview" expand="1"/>
              <Label id="anim_cost" text="" wrap="1"/>
            </VBox>
            <PanelContainer min-width="200"><Scroll><VBox>
              <Label text="Animations" bold="1"/>
              <Scroll min-size="0 150"><ItemList id="anim_list"/></Scroll>
              <HBox>
                <LineEdit id="anim_name" expand="1"/>
                <CheckBox id="anim_loop" text="Loop"/>
              </HBox>
              <HBox><Button id="anim_add" text="New animation"/><Button id="anim_delete" text="Delete"/></HBox>
              <Label text="Steps" bold="1"/>
              <Scroll expand="1" min-size="0 120"><ItemList id="anim_steps"/></Scroll>
              <HBox><Label text="Shown for"/><SpinBox id="anim_duration" expand="1"/><Label text="frames"/></HBox>
              <Flow>
                <Button id="anim_step_add" text="Add selected frame"/>
                <Button id="anim_step_remove" text="Remove"/>
                <Button id="anim_step_up" text="Up"/>
                <Button id="anim_step_down" text="Down"/>
              </Flow>
            </VBox></Scroll></PanelContainer>
            </HSplit>
          </HSplit>
        </VBox>
        <VBox title="Script" separation="0">
          <PanelContainer theme-type="Toolbar"><HBox>
            <Label text="Script"/>
            <OptionButton id="script_file"/>
            <Button text="New script" action="script.new"/>
            <Label id="script_status" text="" expand="1" clip-text="1"/>
          </HBox></PanelContainer>
          <HSplit expand="1" fixed-end="330">
            <VBox expand="1">
              <CodeEdit id="script_code" expand="1"/>
              <PanelContainer><VBox>
                <Label text="Problems (click to go there)" bold="1"/>
                <Scroll min-size="0 64"><ItemList id="script_problems"/></Scroll>
              </VBox></PanelContainer>
            </VBox>
            <PanelContainer min-width="200"><Scroll><VBox>
              <Label text="Class" bold="1"/>
              <Scroll expand="1" min-size="0 120"><ItemList id="script_outline"/></Scroll>
              <Label text="What scripts can use" bold="1"/>
              <Scroll expand="1" min-size="0 120"><ItemList id="script_reference"/></Scroll>
            </VBox></Scroll></PanelContainer>
          </HSplit>
        </VBox>
        <VBox title="Sound" separation="0">
          <PanelContainer theme-type="Toolbar"><Flow>
            <Label text="Song"/>
            <OptionButton id="snd_song"/>
            <Button text="New song" action="song.new"/>
            <Button id="snd_play" text="Play"/>
            <Label text="Tempo"/><SpinBox id="snd_bpm" min-size="56 0"/>
            <Label text="Bars"/><SpinBox id="snd_bars" min-size="44 0"/>
            <CheckBox id="snd_loop" text="Loop"/>
            <CheckBox id="snd_effect" text="Sound effect"/>
            <Label text="Echo ms"/><SpinBox id="snd_echo" min-size="48 0"/>
            <Label text="Feedback"/><SpinBox id="snd_feedback" min-size="48 0"/>
            <Label id="snd_info" text="" clip-text="1"/>
          </Flow></PanelContainer>
          <HSplit expand="1" fixed="270">
            <PanelContainer min-width="200"><Scroll><VBox>
              <Label text="Tracks" bold="1"/>
              <Scroll min-size="0 110"><ItemList id="snd_tracks"/></Scroll>
              <HBox><Button id="snd_track_add" text="Add track"/><Button id="snd_track_remove" text="Remove"/></HBox>
              <Label text="Instrument of the track" bold="1"/>
              <HBox><Label text="Name"/><LineEdit id="snd_track_name" expand="1"/></HBox>
              <HBox><Label text="Wave"/><OptionButton id="snd_wave" expand="1"/><Button id="snd_test" text="Test"/></HBox>
              <HBox><Label text="Sample"/><OptionButton id="snd_sample" expand="1"/></HBox>
              <HBox><Label text="Root key"/><SpinBox id="snd_root" expand="1"/><CheckBox id="snd_sample_loop" text="Loops"/></HBox>
              <Grid columns="4">
                <Label text="Attack"/><SpinBox id="snd_attack" expand="1"/>
                <Label text="Decay"/><SpinBox id="snd_decay" expand="1"/>
                <Label text="Sustain"/><SpinBox id="snd_sustain" expand="1"/>
                <Label text="Release"/><SpinBox id="snd_release" expand="1"/>
                <Label text="Volume"/><SpinBox id="snd_volume" expand="1"/>
                <Label text="Pan"/><SpinBox id="snd_pan" expand="1"/>
              </Grid>
              <Label text="Times in frames (1/60 s); sustain and volume 0-255; pan -64 to 63" wrap="1" dim="1"/>
              <Label text="What it costs" bold="1"/>
              <Label id="snd_voices" text="" clip-text="1"/><ProgressBar id="snd_voices_bar"/>
              <Label id="snd_cpu" text="" clip-text="1"/><ProgressBar id="snd_cpu_bar"/>
              <Label id="snd_size" text="" clip-text="1"/><ProgressBar id="snd_size_bar"/>
            </VBox></Scroll></PanelContainer>
            <VBox expand="1">
              <PianoRoll id="snd_roll" expand="1"/>
              <PanelContainer><Flow>
                <Label text="New notes"/><OptionButton id="snd_note_length"/>
                <Label text="Selected note: velocity"/><SpinBox id="snd_velocity" min-size="56 0"/>
                <Label text="length (ticks, 24 per beat)"/><SpinBox id="snd_length" min-size="56 0"/>
                <Label text="Click: add or pick; drag: move; right end: length; right click: remove; wheel: scroll" clip-text="1" dim="1"/>
              </Flow></PanelContainer>
            </VBox>
          </HSplit>
        </VBox>
        <VBox title="Graphics" separation="0">
          <PanelContainer theme-type="Toolbar"><HBox>
            <Label text="Show"/>
            <OptionButton id="gfx_from" items="Editor preview;Running game"/>
            <Button text="Capture" action="gfx.capture"/>
            <CheckBox id="gfx_follow" text="Follow"/>
            <Label id="gfx_source" text="" expand="1" clip-text="1"/>
          </HBox></PanelContainer>
          <HSplit expand="1" fixed-end="400">
            <Scroll expand="1"><VramView id="gfx_vram"/></Scroll>
            <Scroll min-width="260"><VBox>
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
          </HSplit>
          <PanelContainer theme-type="StatusBar"><HBox><Label id="gfx_hover" text="Point at video memory to see what it holds" clip-text="1"/></HBox></PanelContainer>
        </VBox>
      </TabContainer>
      <HSplit ratio="0.5">
        <PanelContainer><VBox>
          <Label text="Report" bold="1"/>
          <Scroll expand="1" min-size="0 40"><ItemList id="report"/></Scroll>
        </VBox></PanelContainer>
        <PanelContainer><VBox>
          <Label text="Messages" bold="1"/>
          <Scroll expand="1" min-size="0 40"><ItemList id="runlog"/></Scroll>
        </VBox></PanelContainer>
      </HSplit>
    </VSplit>
  </HSplit></Scroll>
  <PanelContainer theme-type="StatusBar"><HBox><Label id="status" text="Ready" expand="1" clip-text="1"/><Label id="run_state" text="Not running"/></HBox></PanelContainer>
</VBox></Panel>)XML";

// The editor's themes by name; anything else gives the classic one.
std::shared_ptr<Theme> makeTheme(const std::string &name)
{
  if (name == "dark") return Theme::makeDark(OSE_UI_FONT_DIR);
  if (name == "light") return Theme::makeLight(OSE_UI_FONT_DIR);
  return Theme::makeClassic(OSE_UI_FONT_DIR);
}

// The theme chosen in the View menu is kept for the next start.
std::string themeSettingPath()
{
  char *pref = SDL_GetPrefPath("blutmond", "editor");
  std::string path = pref ? std::string(pref) + "theme" : std::string();
  SDL_free(pref);
  return path;
}

std::string loadThemeName()
{
  std::string name;
  std::ifstream in(themeSettingPath());
  if (in) in >> name;
  return name == "dark" || name == "light" ? name : "classic";
}

struct Editor
{
  explicit Editor(sdl3::App &a) : app(a) {}
  std::string themeName = "classic";
  void setTheme(const std::string &name)
  {
    themeName = name;
    app.ui().setTheme(makeTheme(name));
    std::ofstream out(themeSettingPath());
    if (out) out << name << "\n";
  }
  sdl3::App &app;
  Project project;
  RoomOverview *overview = nullptr;
  TextureRect *screen = nullptr, *tiles = nullptr;
  ItemList *report = nullptr;
  ProjectPanel projectPanel;
  ProjectFile projectFile;  // as loaded at start
  PalettePanel palettePanel;
  AnimationPanel animationPanel;
  ScriptPanel scriptPanel;
  SoundPanel soundPanel;
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
  Button *syncOff = nullptr, *syncGbaPc = nullptr, *syncPcGba = nullptr;
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

  // Whose keys the other one uses. Needs the latest cook on both, so it cooks first.
  void setSync(SyncMode mode)
  {
    std::string path;
    if (mode != SyncMode::Off && !cookLevel(path)) return;
    runner.setSync(mode, path);
  }

  // Export: the game's cartridge image with all rooms cooked into it, as one .gba file.
  std::string exportRuntime() const
  {
    std::string rom = runner.settings.gbaRom;
    const std::string mb = "_mb.gba";
    std::string image = runner.settings.gbaImage;
    if (rom.empty() && image.size() > mb.size() && image.compare(image.size() - mb.size(), mb.size(), mb) == 0)
      rom = image.substr(0, image.size() - mb.size()) + ".gba";
    return rom;
  }

  std::string exportName() const
  {
    std::string name = projectPanel.file().name.empty() ? "game" : projectPanel.file().name;
    for (char &c : name)
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    return name + ".gba";
  }

  void exportRom()
  {
    std::string runtime = exportRuntime();
    std::error_code ec;
    if (runtime.empty() || !std::filesystem::exists(runtime, ec))
    {
      runLine("Export needs the game's cartridge image (game.gba of a GBA build): set gba-rom in "
              "~/.config/blutmond/editor.conf or pass --gba-rom", true);
      return;
    }
    std::string pack;
    if (!cookLevel(pack)) return;
#ifdef __EMSCRIPTEN__
    // In the browser the image is written into the page's file system and downloaded.
    std::string out = "/tmp/" + exportName();
    std::string error;
    if (!exportRomFiles(runtime, pack, out, &error))
    {
      runLine("Export failed: " + error, true);
      return;
    }
    bmDownloadFile(out.c_str(), exportName().c_str());
    runLine("exported " + exportName() + " (" + std::to_string(std::filesystem::file_size(out, ec) / 1024) + " KB)", false);
#else
    std::string dir = projectPanel.file().path.empty() ? std::filesystem::current_path(ec).string()
                                                       : std::filesystem::path(projectPanel.file().path).parent_path().string();
    std::string suggestion = (std::filesystem::path(dir) / exportName()).string();
    openForm(app.ui(), "Export GBA ROM", {{"File", suggestion, FormField::Kind::SaveFile, {}, 0, 0, "GBA ROM;gba"}}, "Export",
             [this, runtime, pack](const std::vector<std::string> &v, std::string *pError) {
               std::string out = v[0];
               if (std::filesystem::path(out).extension() != ".gba") out += ".gba";
               if (!exportRomFiles(runtime, pack, out, pError)) return false;
               std::error_code e;
               runLine("exported " + out + " (" + std::to_string(std::filesystem::file_size(out, e) / 1024) + " KB)", false);
               return true;
             });
#endif
  }

  void startRun(RunTarget t)
  {
    std::string path;
    if (!cookLevel(path)) return;
#ifdef __EMSCRIPTEN__
    // In the browser the game runs in a frame of this page; a GBA cannot be reached from here.
    if (t == RunTarget::Gba)
    {
      runLine("Run on the GBA needs the desktop editor (the browser cannot reach the Xboo cable)", true);
      return;
    }
    bmWebGameRun(path.c_str(), 0);
    runLine("the game runs in the frame at the bottom right: click into it to play" + std::string(live ? "; saved changes are sent to it" : ""), false);
#else
    runner.run(t, path);
#endif
  }

  // Whether a game runs that Live pushes go to.
  bool gameRunning()
  {
#ifdef __EMSCRIPTEN__
    return bmWebGameOpen() != 0;
#else
    return runner.active();
#endif
  }

  // After a cook asked for by the user or a changed file: into the running game.
  void pushLive()
  {
    if (!live || !gameRunning()) return;
    std::string path;
    if (!cookLevel(path)) return;
#ifdef __EMSCRIPTEN__
    bmWebGameRun(path.c_str(), 3);
#else
    runner.pushHot(path);
#endif
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

  // The set of sprite files may have changed: for the level pack and the Animation tab.
  void spritesChanged()
  {
    std::vector<std::string> sprites = spritePaths(projectPanel.catalog());
    project.setSprites(sprites);
    animationPanel.setSprites(sprites);
    std::vector<std::string> scripts = scriptPaths(projectPanel.catalog());
    project.setScripts(scripts);
    scriptPanel.setScripts(scripts);
    std::vector<std::string> songs = songPaths(projectPanel.catalog());
    project.setSongs(songs);
    project.setData(dataPaths(projectPanel.catalog()));
    project.setFonts(fontPaths(projectPanel.catalog()));
    project.setLanguages(languagePaths(projectPanel.catalog()));
    soundPanel.setSongs(songs);
    if (!songs.empty()) soundPanel.songDir = std::filesystem::path(songs[0]).parent_path().string();
    else if (!projectPanel.file().assetDirs.empty())
      soundPanel.songDir = (std::filesystem::path(projectPanel.file().assetDirs[0]) / "music").string();
    // New scripts go next to the existing ones, or into "scripts" in the first asset folder.
    if (!scripts.empty()) scriptPanel.scriptDir = std::filesystem::path(scripts[0]).parent_path().string();
    else if (!projectPanel.file().assetDirs.empty())
      scriptPanel.scriptDir = (std::filesystem::path(projectPanel.file().assetDirs[0]) / "scripts").string();
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
    ui.actions().add({"project.export", "Export GBA ROM...", "share-square-o", "Ctrl+E", "Write the game as one GBA image for an emulator or a flash cartridge",
                      [this] { exportRom(); }, nullptr, nullptr});
    ui.actions().add({"run.start", "Run", "play", "F6", "Play all rooms on the chosen target", [this] { startRun(target); }, nullptr, nullptr});
    ui.actions().add({"run.pc", "Run on the PC", "play", "", "", [this] { startRun(RunTarget::Pc); }, nullptr, nullptr});
    ui.actions().add({"run.gba", "Run on the GBA (Xboo)", "gamepad", "F7", "", [this] { startRun(RunTarget::Gba); }, nullptr, nullptr});
    ui.actions().add({"run.stop", "Stop", "stop", "Shift+F6", "",
                      [this] {
                        runner.stop();
#ifdef __EMSCRIPTEN__
                        bmWebGameStop();
#endif
                      },
                      [this] { return gameRunning(); }, nullptr});
    ui.actions().add({"sync.off", "Sync off", "unlink", "", "", [this] { setSync(SyncMode::Off); }, nullptr, [this] { return runner.sync() == SyncMode::Off; }});
    ui.actions().add({"sync.gba_pc", "Sync: the PC plays along with the GBA", "link", "", "", [this] { setSync(SyncMode::GbaToPc); }, nullptr,
                      [this] { return runner.sync() == SyncMode::GbaToPc; }});
    ui.actions().add({"sync.pc_gba", "Sync: the GBA plays along with the PC", "link", "", "", [this] { setSync(SyncMode::PcToGba); }, nullptr,
                      [this] { return runner.sync() == SyncMode::PcToGba; }});
    ui.actions().add({"app.quit", "Quit", "", "Ctrl+Q", "", [this] { app.quit(); }, nullptr, nullptr});
    ui.actions().add({"view.collision", "Collision", "", "C", "", [this] { overview->showCollision = !overview->showCollision; }, nullptr, nullptr});
    ui.actions().add({"view.objects", "Objects", "", "O", "", [this] { overview->showObjects = !overview->showObjects; }, nullptr, nullptr});
    ui.actions().add({"help.version", "Version", "", "", "", [this] {
      openMessage(app.ui(), "Version", std::string("Blutmond editor\n\nCommit: ") + bm_build_commit + (bm_build_changed ? " (with changes not committed)" : "") +
                                     "\nBuilt: " + bm_build_date);
    }, nullptr, nullptr});
    ui.actions().add({"view.grid", "Grid", "", "G", "", [this] { overview->showGrid = !overview->showGrid; }, nullptr, nullptr});

    for (const char *name : {"classic", "light", "dark"})
    {
      std::string n = name, label = n == "classic" ? "Classic theme" : n == "light" ? "Light theme" : "Dark theme";
      ui.actions().add({"theme." + n, label, "", "", "", [this, n] { setTheme(n); }, nullptr, [this, n] { return themeName == n; }});
    }

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
    registerAnimationControls();
    registerControlType("CodeEdit", [] { return std::make_unique<CodeEdit>(); });
    registerControlType("PianoRoll", [] { return std::make_unique<PianoRoll>(); });
    ui.actions().add({"song.new", "New song or sound effect...", "music", "", "", [this] { soundPanel.newSong(); }, nullptr, nullptr});
    ui.actions().add({"script.new", "New script...", "terminal", "", "", [this] { scriptPanel.newScript(); }, nullptr, nullptr});
    ui.actions().add({"sprite.new", "New sprite...", "film", "", "", [this] { projectPanel.newSprite(); }, nullptr, nullptr});
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
    animationPanel.bind(*root, app.renderer());
    animationPanel.log = projectPanel.log;
    animationPanel.changed = [this] {
      // The sprite file was saved: into the running game.
      pushLive();
    };
    projectPanel.spritePicked = [this](const std::string &path) {
      if (animationPanel.path() != path) animationPanel.open(path);
      if (auto *center = app.ui().root() ? app.ui().root()->findAs<TabContainer>("center") : nullptr) center->setCurrent(1);
    };
    projectPanel.assetsChanged = [this] { spritesChanged(); };
    scriptPanel.bind(*root, ui);
    scriptPanel.log = projectPanel.log;
    scriptPanel.changed = [this] { pushLive(); };
    scriptPanel.created = [this] {
      projectPanel.refresh();
      spritesChanged();
    };
    soundPanel.bind(*root, ui);
    soundPanel.log = projectPanel.log;
    soundPanel.changed = [this] { pushLive(); };
    soundPanel.created = [this] {
      projectPanel.refresh();
      spritesChanged();
    };
    projectPanel.songPicked = [this](const std::string &path) {
      if (soundPanel.path() != path) soundPanel.open(path);
      if (auto *center = app.ui().root() ? app.ui().root()->findAs<TabContainer>("center") : nullptr) center->setCurrent(3);
    };
    projectPanel.scriptPicked = [this](const std::string &path) {
      if (scriptPanel.path() != path) scriptPanel.open(path);
      if (auto *center = app.ui().root() ? app.ui().root()->findAs<TabContainer>("center") : nullptr) center->setCurrent(2);
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
    syncOff = root->findAs<Button>("sync_off");
    syncGbaPc = root->findAs<Button>("sync_gba_pc");
    syncPcGba = root->findAs<Button>("sync_pc_gba");
    runner.log = [this](const std::string &line, bool error) { runLine(line, error); };
    root->findAs<OptionButton>("run_target")->select(int(target), false);   // --target
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
    spritesChanged();
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
    animationPanel.update(dt);
    scriptPanel.update(dt);
    soundPanel.update(dt);
    reloadTimer += dt;
    if (reloadTimer > 1.0f)
    {
      reloadTimer = 0;
      if (project.dataChangedOnDisk() | project.changedOnDisk())
      {
        cook();
        pushLive();
      }
    }
    runner.update();
    // The three sync buttons show which one is on (also after it ended by itself).
    syncOff->pressed = runner.sync() == SyncMode::Off;
    syncGbaPc->pressed = runner.sync() == SyncMode::GbaToPc;
    syncPcGba->pressed = runner.sync() == SyncMode::PcToGba;
    followTimer += dt;
    if (graphicsFromGame && graphicsFollow && runner.active() && !runner.busy() && followTimer > 1.0f)
    {
      followTimer = 0;
      capture();
    }
#ifdef __EMSCRIPTEN__
    runState->text = gameRunning() ? std::string("Running in this page") + (live ? " (live)" : "") : std::string("Not running");
#else
    runState->text = runner.busy() ? runner.state() : runner.state() + (runner.active() && live ? " (live)" : "");
#endif
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
  std::string themeName = loadThemeName();
  int frames = 0;
  const char *screenshot = nullptr;
  std::vector<std::string> actions;   // menu actions to run at the start (help.version, ...)
  int tab = 0;
  RunTarget startTarget = RunTarget::Pc;
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
#ifdef BM_EDITOR_GBA_ROM
  run.gbaRom = BM_EDITOR_GBA_ROM;
#endif
  run.load();
  for (int i = 1; i < argc; i++)
  {
    if (std::strcmp(argv[i], "--profile") == 0 && i + 1 < argc) profile = argv[++i];
    else if (std::strcmp(argv[i], "--dark") == 0) themeName = "dark";
    else if (std::strcmp(argv[i], "--light") == 0) themeName = "light";
    else if (std::strcmp(argv[i], "--theme") == 0 && i + 1 < argc) themeName = argv[++i];
    else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
    else if (std::strcmp(argv[i], "--tab") == 0 && i + 1 < argc) tab = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--action") == 0 && i + 1 < argc) actions.push_back(argv[++i]);
    else if (std::strcmp(argv[i], "--target") == 0 && i + 1 < argc) startTarget = std::strcmp(argv[++i], "gba") == 0 ? RunTarget::Gba : RunTarget::Pc;
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
  desc.theme = makeTheme(themeName);
  std::string error;
  if (!app.init(desc, &error))
  {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }

#ifdef __EMSCRIPTEN__
  // In the browser the editor takes the whole page and follows its size.
  int windowCount = 0;
  if (SDL_Window **windows = SDL_GetWindows(&windowCount))
  {
    if (windowCount > 0) SDL_SetWindowFillDocument(windows[0], true);
    SDL_free(windows);
  }
#endif

  Editor editor(app);
  editor.themeName = themeName == "dark" || themeName == "light" ? themeName : "classic";
  editor.runner.settings = run;
  editor.target = startTarget;
  editor.projectFile = projectFile;
#ifdef BM_EDITOR_ENGINE_DIR
  editor.projectPanel.engineDir = BM_EDITOR_ENGINE_DIR;
#endif
  editor.project.setProfile(profile);
  editor.build();
  if (auto *center = app.ui().root()->findAs<TabContainer>("center")) center->setCurrent(tab);
  for (const std::string &a : actions)
    if (!app.ui().actions().run(a)) std::fprintf(stderr, "no action %s\n", a.c_str());

  if (frames > 0)
  {
    for (int i = 0; i < frames; i++)
      if (!app.frame([&](float dt) { editor.update(dt); })) break;
    return screenshot ? writeScreenshot(screenshot) : 0;
  }
  app.run([&](float dt) { editor.update(dt); });
  return 0;
}
