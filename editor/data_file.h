// SPDX-License-Identifier: BSD-2-Clause OR MIT
// A record file (.bmdata, docs/plans/structures.md §3) as the editor changes it: as lines of
// text, so that comments, order and spacing stay as the person wrote them. Changing a value
// rewrites only the line of that value (or adds one before the record's closing brace);
// records are added at the end and removed with their lines. The cooker reads the result.
//
// What it understands is the form the files have: `table Name`, then records
//
//   record Name : Base {
//     key = value      # a comment
//   }
//
// with the header on one line and the closing brace on its own line.
#pragma once

#include <string>
#include <vector>

namespace bm::editor {

struct DataValue
{
  std::string key, value;   // value as written ("text" with its quotes, #name, 3, Fire)
  int line = 0;
};

struct DataRecord
{
  std::string name, base;
  int first = 0, last = 0;  // the header's line and the closing brace's
  std::vector<DataValue> values;
};

class DataFile
{
public:
  std::string path;
  std::string table;   // the structure
  std::vector<std::string> lines;
  std::vector<DataRecord> records;

  bool load(const std::string &path, std::string *pError);
  bool save(std::string *pError) const;
  /// Reads `lines` again into `table` and `records`.
  void parse();

  const DataRecord *find(const std::string &name) const;
  /// The value the record itself sets for `key` (as written), or "".
  std::string own(const std::string &record, const std::string &key) const;
  /// The record's value or, if it sets none, its base's (in this file), and so on; "" if none.
  std::string value(const std::string &record, const std::string &key) const;
  /// Sets the record's own value (as written); an empty value removes the line.
  void set(const std::string &record, const std::string &key, const std::string &value);
  /// A new record at the end, a copy of `base` if given. False if the name is taken or no name.
  bool add(const std::string &name, const std::string &base, std::string *pError);
  /// Removes the record's lines. False if there is none.
  bool remove(const std::string &name);
};

/// Whether `name` can be a record's name: letters, digits and _, not starting with a digit.
bool validRecordName(const std::string &name);

/// A text as a .bmdata value: in quotes, with " and \ escaped; and back (anything not in
/// quotes comes back as it is).
std::string quoteText(const std::string &text);
std::string unquoteText(const std::string &value);

}  // namespace bm::editor
