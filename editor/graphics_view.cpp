// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "graphics_view.h"

#include <algorithm>
#include <cstdio>

#include "ui/canvas.h"
#include "ui/containers.h"
#include "ui/lists.h"
#include "ui/loader.h"
#include "ui/renderer.h"
#include "ui/theme.h"
#include "ui/widgets.h"

using namespace ose::ui;
using ose::Vec2;

namespace bm::editor {

namespace {

constexpr float kLabelWidth = 64;

Color fromBgr555(uint16_t c)
{
  return {float(c & 31) / 31.0f, float((c >> 5) & 31) / 31.0f, float((c >> 10) & 31) / 31.0f, 1};
}

const Color kBgColours[4] = {{1, 0.35f, 0.35f, 1}, {0.35f, 1, 0.45f, 1}, {0.4f, 0.6f, 1, 1}, {1, 0.85f, 0.3f, 1}};

}  // namespace

// --- VramView ----------------------------------------------------------------------------

Vec2 VramView::computeMinSize()
{
  return {kLabelWidth + 256 * zoom, (kUnits / 32) * 8 * zoom};
}

int VramView::unitAt(Vec2 p) const
{
  float x = p.x - rect_.x - kLabelWidth, y = p.y - rect_.y;
  if (x < 0 || y < 0) return -1;
  int col = int(x / (8 * zoom)), row = int(y / (8 * zoom));
  if (col >= 32 || row >= kUnits / 32) return -1;
  return row * 32 + col;
}

bool VramView::event(const InputEvent &e)
{
  if (e.type == InputEvent::Type::MouseMove)
  {
    int u = unitAt(e.pos);
    if (u != hover_)
    {
      hover_ = u;
      if (hovered) hovered(u);
    }
    return true;
  }
  return false;
}

void VramView::mouseExited()
{
  hover_ = -1;
  if (hovered) hovered(-1);
}

void VramView::draw(Canvas &c)
{
  c.rect(rect_, parseColor("#303030"));
  if (!analysis || !texture) return;
  float x0 = rect_.x + kLabelWidth, y0 = rect_.y;
  float cell = 8 * zoom;
  c.image({x0, y0, 256 * zoom, (kUnits / 32) * cell}, texture);

  const VramAnalysis &a = *analysis;
  for (int u = 0; u < kUnits; u++)
  {
    uint16_t v = a.use[size_t(u)];
    Rect r{x0 + (u % 32) * cell, y0 + (u / 32) * cell, cell, cell};
    for (int n = 0; n < 4; n++)
      if (v & (1u << n))
      {
        Color k = kBgColours[n];
        k.w = 0.55f;
        c.rect(r, k);
      }
    bool shown = (v & (USE_TILES | USE_OBJ | USE_MAP)) != 0;
    if ((v & USE_NONZERO) && !shown) c.rect(r, {0, 0, 0, 0.45f});  // loaded, nothing shows it
    if (v & USE_OVERLAP) c.border(r, {1, 0, 0, 1}, 2);
  }

  // Screen blocks (2 KB = 2 rows) and char blocks (16 KB = 16 rows).
  const Font &f = font();
  float px = fontSize() * 0.85f;
  for (int row = 0; row <= kUnits / 32; row++)
  {
    float y = y0 + row * cell;
    if (row % 16 == 0)
    {
      c.rect({rect_.x, y, kLabelWidth + 256 * zoom, 2}, {1, 1, 1, 0.8f});
      if (row < kUnits / 32)
      {
        int block = row / 16;
        c.text({rect_.x + 4, y + 2}, "CB" + std::to_string(block), f, px, {1, 1, 1, 1});
        c.text({rect_.x + 4, y + 2 + px * 1.3f}, block < 4 ? "BG" : "OBJ", f, px, {0.8f, 0.8f, 0.8f, 1});
      }
    }
    else if (row % 2 == 0 && row < 64)
      c.rect({x0, y, 256 * zoom, 1}, {1, 1, 1, 0.18f});
    if (row % 2 == 0 && row < 64 && row % 16 != 0)
      c.text({rect_.x + 4, y + 1}, "SB" + std::to_string(row / 2), f, px * 0.9f, {0.75f, 0.75f, 0.75f, 1});
  }
  // Map labels at the first unit of each background's map.
  for (int n = 0; n < 4; n++)
  {
    const BgInfo &bg = a.bgs[n];
    if (!bg.enabled) continue;
    int unit = bg.screenBlock * 64;
    Rect r{x0 + 4, y0 + (unit / 32) * cell + 2, 256 * zoom - 8, px * 1.4f};
    c.rect({r.x - 2, r.y, 150, r.h}, {0, 0, 0, 0.6f});
    c.text({r.x, r.y}, "BG" + std::to_string(n) + " map", f, px, kBgColours[n]);
  }
  if (hover_ >= 0) c.border({x0 + (hover_ % 32) * cell, y0 + (hover_ / 32) * cell, cell, cell}, {1, 1, 1, 1}, 1);
}

// --- PaletteView -------------------------------------------------------------------------

bool PaletteView::event(const InputEvent &e)
{
  if (e.type == InputEvent::Type::MouseDown && e.button == MouseButton::Left)
  {
    int col = int((e.pos.x - rect_.x - 1) / cell), row = int((e.pos.y - rect_.y - 1) / cell);
    if (col < 0 || row < 0 || col > 15 || row > 15) return false;
    selected = row * 16 + col;
    if (picked) picked(selected);
    return true;
  }
  return false;
}

void PaletteView::draw(Canvas &c)
{
  c.rect(rect_, parseColor("#808080"));
  if (!colours) return;
  for (int bank = 0; bank < 16; bank++)
    for (int i = 0; i < 16; i++)
    {
      Rect r{rect_.x + 1 + i * cell, rect_.y + 1 + bank * cell, cell - 1, cell - 1};
      bool used = (usedBanks >> bank) & 1;
      if (i == 0 && !(backdrop && bank == 0))
      {
        c.rect(r, parseColor(used ? "#ffffff" : "#c0c0c0"));
        c.line({r.x, r.bottom()}, {r.right(), r.y}, parseColor("#a0a0a0"), 1);
      }
      else
      {
        Color k = fromBgr555(colours[bank * 16 + i]);
        if (!used) k = {k.x * 0.35f + 0.4f, k.y * 0.35f + 0.4f, k.z * 0.35f + 0.4f, 1};
        c.rect(r, k);
      }
      if (bank * 16 + i == selected)
      {
        c.border(r.shrink(-1), {0, 0, 0, 1}, 2);
        c.border(r, {1, 1, 1, 1}, 1);
      }
    }
}

// --- LineCostView ------------------------------------------------------------------------

void LineCostView::draw(Canvas &c)
{
  c.rect(rect_, parseColor("#202020"));
  if (!analysis) return;
  float scaleY = rect_.h / BM_PPU_HEIGHT;
  float budgetX = rect_.w * 0.8f;  // the hardware limit sits at 80% of the width
  for (int y = 0; y < BM_PPU_HEIGHT; y++)
  {
    int cycles = analysis->spriteCycles[y];
    if (!cycles) continue;
    float w = std::min(rect_.w, budgetX * float(cycles) / kSpriteCyclesPerLine);
    bool over = cycles > kSpriteCyclesPerLine;
    c.rect({rect_.x, rect_.y + y * scaleY, w, std::max(1.0f, scaleY)}, over ? Color{1, 0.2f, 0.2f, 1} : Color{0.3f, 0.8f, 0.4f, 1});
  }
  c.rect({rect_.x + budgetX, rect_.y, 1, rect_.h}, {1, 1, 1, 0.7f});
}

void registerGraphicsControls()
{
  registerControlType("VramView", [] { return std::make_unique<VramView>(); });
  registerControlType("PaletteView", [] { return std::make_unique<PaletteView>(); });
  registerControlType("LineCostView", [] { return std::make_unique<LineCostView>(); });
}

// --- GraphicsInspector -------------------------------------------------------------------

void GraphicsInspector::bind(Control &root, Renderer &renderer)
{
  renderer_ = &renderer;
  vram_ = root.findAs<VramView>("gfx_vram");
  bgPalette_ = root.findAs<PaletteView>("gfx_bg_palette");
  objPalette_ = root.findAs<PaletteView>("gfx_obj_palette");
  lineCost_ = root.findAs<LineCostView>("gfx_line_cost");
  screen_ = root.findAs<TextureRect>("gfx_screen");
  layers_ = root.findAs<ItemList>("gfx_layers");
  sprites_ = root.findAs<ItemList>("gfx_sprites");
  notes_ = root.findAs<ItemList>("gfx_notes");
  budgets_ = root.findAs<VBox>("gfx_budgets");
  source_ = root.findAs<Label>("gfx_source");
  hover_ = root.findAs<Label>("gfx_hover");
  layers_->tileSize = sprites_->tileSize = notes_->tileSize = 0;
  screen_->keepAspect = true;
  objPalette_->backdrop = false;
  vram_->hovered = [this](int unit) { hover_->text = unit >= 0 ? analysis_.describeUnit(unit) : std::string(); };
}

void GraphicsInspector::show(const BmPpuState &state, const std::string &source)
{
  if (!state_) state_ = std::make_unique<BmPpuState>();
  *state_ = state;
  analysis_ = analyseVideo(*state_);
  source_->text = source;

  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
  renderVram(*state_, analysis_, rgba, w, h);
  if (vram_->texture) renderer_->destroyTexture(vram_->texture);
  vram_->texture = renderer_->createTexture(w, h, rgba.data(), true);
  vram_->analysis = &analysis_;

  static uint16_t pixels[BM_PPU_WIDTH * BM_PPU_HEIGHT];
  bmPpuRender(state_.get(), pixels);
  std::vector<uint8_t> screen(size_t(BM_PPU_WIDTH) * BM_PPU_HEIGHT * 4);
  for (int i = 0; i < BM_PPU_WIDTH * BM_PPU_HEIGHT; i++)
  {
    auto x = [](unsigned v) { return uint8_t((v << 3) | (v >> 2)); };
    screen[size_t(i) * 4 + 0] = x(pixels[i] & 31);
    screen[size_t(i) * 4 + 1] = x((pixels[i] >> 5) & 31);
    screen[size_t(i) * 4 + 2] = x((pixels[i] >> 10) & 31);
    screen[size_t(i) * 4 + 3] = 255;
  }
  if (screen_->texture) renderer_->destroyTexture(screen_->texture);
  screen_->texture = renderer_->createTexture(BM_PPU_WIDTH, BM_PPU_HEIGHT, screen.data(), true);
  screen_->textureSize = {float(BM_PPU_WIDTH), float(BM_PPU_HEIGHT)};

  bgPalette_->colours = state_->palette;
  bgPalette_->usedBanks = analysis_.bgBanks;
  objPalette_->colours = state_->palette + 256;
  objPalette_->usedBanks = analysis_.objBanks;
  lineCost_->analysis = &analysis_;

  budgets_->clearChildren();
  for (const Budget &b : analysis_.budgets)
  {
    auto label = std::make_unique<Label>();
    std::string amount = b.unit == "B" ? std::to_string(b.used) + " of " + std::to_string(b.total) + " bytes"
                                       : std::to_string(b.used) + " of " + std::to_string(b.total) + (b.unit.empty() ? "" : " " + b.unit);
    label->clipText = true;
    label->text = b.name + ": " + amount + (b.detail.empty() ? "" : " (" + b.detail + ")");
    budgets_->addChild(std::move(label));
    auto bar = std::make_unique<ProgressBar>();
    bar->value = b.total > 0 ? std::min(1.0f, float(b.used) / float(b.total)) : 0;
    budgets_->addChild(std::move(bar));
  }

  layers_->items.clear();
  for (const BgInfo &bg : analysis_.bgs)
  {
    if (!bg.enabled) continue;
    std::string banks;
    for (int k = 0; k < 16; k++)
      if (bg.banks & (1u << k)) banks += (banks.empty() ? "" : ",") + std::to_string(k);
    layers_->items.push_back({"BG" + std::to_string(bg.index) + ": char block " + std::to_string(bg.charBlock) + ", map in SB " +
                                  std::to_string(bg.screenBlock) + (bg.screenBlocks > 1 ? "-" + std::to_string(bg.screenBlock + bg.screenBlocks - 1) : "") +
                                  ", " + std::to_string(bg.widthTiles) + "x" + std::to_string(bg.heightTiles) + (bg.bpp8 ? ", 256 colours" : ", 16 colours") +
                                  ", priority " + std::to_string(bg.priority) + ", scroll " + std::to_string(bg.scrollX) + "," +
                                  std::to_string(bg.scrollY) + ", " + std::to_string(bg.tilesUsed) + " tiles" +
                                  (banks.empty() ? "" : ", banks " + banks),
                              "th", {}, {}, {}, {}});
  }
  if (layers_->items.empty()) layers_->items.push_back({"No background enabled", "info", {}, {}, {}, {}});

  sprites_->items.clear();
  for (const SpriteInfo &sp : analysis_.sprites)
    sprites_->items.push_back({"#" + std::to_string(sp.index) + "  " + std::to_string(sp.width) + "x" + std::to_string(sp.height) + " at " +
                                   std::to_string(sp.x) + "," + std::to_string(sp.y) + ", tile " + std::to_string(sp.tile) +
                                   (sp.bpp8 ? ", 256 colours" : ", bank " + std::to_string(sp.bank)) + ", priority " +
                                   std::to_string(sp.priority) + (sp.affine ? ", affine" : "") + (sp.onScreen ? "" : ", off screen"),
                               sp.onScreen ? "image" : "eye-slash", {}, {}, {}, {}});
  if (sprites_->items.empty()) sprites_->items.push_back({"No sprites in OAM", "info", {}, {}, {}, {}});

  notes_->items.clear();
  for (const std::string &n : analysis_.notes) notes_->items.push_back({n, "warning", {}, {}, {}, {}});
  if (analysis_.forcedBlank) notes_->items.push_back({"Forced blank: the screen is switched off", "warning", {}, {}, {}, {}});
  if (notes_->items.empty()) notes_->items.push_back({"Nothing unusual", "check", {}, {}, {}, {}});
}

}  // namespace bm::editor
