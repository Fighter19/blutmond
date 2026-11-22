/*-
 * SPDX-License-Identifier: BSD-2-Clause OR MIT
 * Copyright (C) 2025 Blutmond contributors
 */
#pragma once

// Texts in several languages (docs/spec/pack-format.md §3.2, LANG). The pack's strings are the
// main language; each LANG chunk is one language with translations of some of them, by string
// id. Language 0 is the first LANG chunk (the main language's, with no translations), the
// others follow in pack order. A text without a translation in the language shown reads as
// in the main language. Texts are UTF-8.

#include "pack.h"
#include "shared.h"

/// Number of languages: the LANG chunks, at least 1.
uint8_t bmTextLanguageCount(const BmPack *pPack);

/// String ids of a language's code ("de") and name ("Deutsch"); 0xFFFF if there is none.
uint16_t bmTextLanguageCode(const BmPack *pPack, uint8_t language);
uint16_t bmTextLanguageName(const BmPack *pPack, uint8_t language);

/// The language with that code (string), or -1.
int32_t bmTextFindLanguage(const BmPack *pPack, const char *code);

/// The text with string id `id` in `language`: its translation, or the main language's text.
/// NULL if the id is not a string of the pack.
const char *bmText(const BmPack *pPack, uint8_t language, uint16_t id);

/// Reads one character (code point) of UTF-8 text and moves on; a broken byte reads as '?'.
uint32_t bmTextNextChar(const char **ppText);
