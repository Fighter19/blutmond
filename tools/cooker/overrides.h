// SPDX-License-Identifier: BSD-2-Clause OR MIT
// Overrides next to a source asset (docs/spec/asset-model.md): platform-specific changes kept
// in "<asset file>.bmasset", keyed by profile, so the source asset itself stays as it is.
// Version 1 holds colour replacements for images:
//
//   <asset version="1">
//     <override profile="gba">                     (or "*": every profile)
//       <colour from="#a8c8e8" to="#b8d0f8"/>      source colour -> colour to cook instead
//     </override>
//   </asset>
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace bm {

struct AssetOverrides
{
  /// profile ("gba", "snes", "*") -> source RGB (0xRRGGBB) -> replacement RGB
  std::map<std::string, std::map<uint32_t, uint32_t>> colours;

  bool empty() const;
  /// The replacement for `rgb` under `profile` (a profile's own entry wins over "*").
  bool colourFor(const std::string &profile, uint32_t rgb, uint32_t *pOut) const;
};

std::string overridesPath(const std::string &assetPath);

/// Reads the overrides of an asset; none if the file is missing. Throws on a broken file.
AssetOverrides loadOverrides(const std::string &assetPath);
/// Writes them (removes the file when there are none). False if it cannot be written.
bool saveOverrides(const std::string &assetPath, const AssetOverrides &overrides);

std::string rgbHex(uint32_t rgb);         // "#rrggbb"
bool parseRgbHex(const std::string &s, uint32_t *pRgb);

}  // namespace bm
