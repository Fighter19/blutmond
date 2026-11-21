// SPDX-License-Identifier: BSD-2-Clause OR MIT
#include "aot.h"

#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <vector>

extern "C" {
#include "native.h"
#include "vm.h"
}

namespace bm::script {

namespace {

uint16_t rd16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

struct Fn
{
  uint32_t start = 0;
  uint16_t words = 0, cls = 0;
  uint8_t regs = 0, args = 0;
  bool stateCode = false;
  const uint8_t *code = nullptr;
  std::string name;
  std::vector<std::pair<uint16_t, uint16_t>> lines;
};

std::string identifier(const std::string &s)
{
  std::string out;
  for (char c : s) out += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : '_';
  return out;
}

std::string cString(const std::string &s)
{
  std::string out = "\"";
  for (char c : s)
  {
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out + "\"";
}

const struct { uint8_t argc; bool latent; } kApi[BM_SCRIPT_API_COUNT] = {
#define X(name, result, argc, a0, a1, a2, latent) {argc, latent != 0},
    BM_SCRIPT_API(X)
#undef X
};

}  // namespace

std::string emitC(const Module &module, uint32_t bytecodeCrc)
{
  if (!module.ok() || module.data.size() < BM_VM_HEADER_SIZE) return {};
  const uint8_t *p = module.data.data();
  uint16_t functionCount = rd16(p + 10);
  const uint8_t *functions = p + rd32(p + 20), *consts = p + rd32(p + 24), *code = p + rd32(p + 28);
  const uint8_t *lineTable = rd32(p + 36) ? p + rd32(p + 36) : nullptr;

  std::vector<Fn> fns(functionCount);
  for (uint16_t i = 0; i < functionCount; i++)
  {
    const uint8_t *f = functions + size_t(i) * BM_VM_FUNCTION_SIZE;
    Fn &fn = fns[i];
    fn.start = rd32(f);
    fn.words = rd16(f + 4);
    fn.regs = f[6];
    fn.args = f[7];
    fn.stateCode = (f[8] & BM_VM_FUNCTION_STATE_CODE) != 0;
    fn.cls = rd16(f + 10);
    fn.code = code + size_t(fn.start) * 2;
    std::string cls = fn.cls < module.classes.size() ? identifier(module.classes[fn.cls].name) : "class";
    fn.name = cls + (fn.stateCode ? "_state" : "_fn") + std::to_string(i);
    if (lineTable)
    {
      const uint8_t *l = p + rd32(lineTable + size_t(i) * 4);
      for (uint16_t k = 0; k < rd16(l + 2); k++) fn.lines.push_back({rd16(l + 4 + k * 4), rd16(l + 6 + k * 4)});
    }
  }

  std::ostringstream o;
  o << "// Made by the Mondskript compiler from the bytecode module with CRC " << std::hex << bytecodeCrc << std::dec << ". Do not edit.\n";
  o << "#include \"native.h\"\n\n";
  o << "#define FAILED(c) ((c)->pInstance->wait == BM_VM_FAILED)\n\n";
  for (const Fn &fn : fns)
  {
    if (fn.stateCode) continue;
    o << "static int32_t " << fn.name << "(BmVmNativeCtx *c";
    for (int a = 0; a < fn.args; a++) o << ", int32_t a" << a;
    o << ");\n";
  }
  o << "\n";

  for (uint16_t index = 0; index < functionCount; index++)
  {
    const Fn &fn = fns[index];
    auto word = [&](uint16_t pc) { return rd16(fn.code + size_t(pc) * 2); };

    // Where blocks start: jump targets, what follows a jump, and (in state code) where a
    // latent call continues.
    std::set<uint16_t> labels, resume, blocks{0};
    for (uint16_t pc = 0; pc < fn.words;)
    {
      uint8_t op = uint8_t(word(pc)), length = bmVmOpLength(op);
      if (op == BM_OP_JMP || op == BM_OP_JZ || op == BM_OP_JNZ)
      {
        labels.insert(word(uint16_t(pc + 1)));
        blocks.insert(word(uint16_t(pc + 1)));
        blocks.insert(uint16_t(pc + length));
      }
      if (op == BM_OP_CALLN && fn.stateCode && kApi[word(uint16_t(pc + 1)) & 0xFF].latent)
      {
        resume.insert(uint16_t(pc + length));
        blocks.insert(uint16_t(pc + length));
      }
      pc = uint16_t(pc + length);
    }
    // Instructions per block, for the budget.
    std::map<uint16_t, int> blockSize;
    {
      uint16_t current = 0;
      for (uint16_t pc = 0; pc < fn.words; pc = uint16_t(pc + bmVmOpLength(uint8_t(word(pc)))))
      {
        if (blocks.count(pc)) current = pc;
        blockSize[current]++;
      }
    }

    // Absolute, so a debugger finds the script whatever folder it is started in.
    std::string file = fn.cls < module.classes.size() ? module.classes[fn.cls].file : std::string();
    if (!file.empty())
    {
      std::error_code ec;
      std::filesystem::path full = std::filesystem::absolute(file, ec);
      if (!ec) file = full.lexically_normal().generic_string();
    }
    const char *leave = fn.stateCode ? "return 0xFFFF;" : "return 0;";
    // Gives the lines written since `startAt` the script line they belong to. Every line gets
    // its own directive: otherwise the C compiler would count on from the last one.
    auto stamp = [&](std::streamoff startAt, int line) {
      if (line == 0 || file.empty()) return;
      std::string all = o.str(), part = all.substr(size_t(startAt));
      all.resize(size_t(startAt));
      o.str(all);
      o.seekp(0, std::ios::end);
      size_t at = 0;
      while (at < part.size())
      {
        size_t end = part.find('\n', at);
        if (end == std::string::npos) end = part.size();
        o << "#line " << line << " " << cString(file) << "\n" << part.substr(at, end - at) << "\n";
        at = end + 1;
      }
    };
    const std::streamoff functionAt = std::streamoff(o.tellp());
    if (fn.stateCode) o << "static uint32_t " << fn.name << "(BmVmNativeCtx *c, uint32_t pc)\n{\n";
    else
    {
      o << "static int32_t " << fn.name << "(BmVmNativeCtx *c";
      for (int a = 0; a < fn.args; a++) o << ", int32_t a" << a;
      o << ")\n{\n";
    }
    o << "  BmVmInstance *const inst = c->pInstance;\n  const BmVmHost *const h = c->pHost;\n";
    for (int r = 0; r < fn.regs; r++) o << "  int32_t r" << r << " = " << (r < fn.args ? "a" + std::to_string(r) : std::string("0")) << ";\n";
    for (int r = 0; r < fn.regs; r++) o << "  (void)r" << r << ";\n";
    o << "  uint32_t at = 0;\n  (void)h;\n  (void)inst;\n";
    if (fn.stateCode)
    {
      o << "  const uint8_t stateBefore = inst->state;\n";
      o << "  switch (pc)\n  {\n  case 0: break;\n";
      for (uint16_t r : resume) o << "  case " << r << ": goto L" << r << ";\n";
      o << "  default: c->pRt->fail(c, BM_VM_FAIL_INSTRUCTION, " << index << ", pc); return 0xFFFF;\n  }\n";
    }

    stamp(functionAt, fn.lines.empty() ? 0 : fn.lines.front().second);
    size_t lineAt = 0;
    for (uint16_t pc = 0; pc < fn.words;)
    {
      uint16_t w0 = word(pc);
      uint8_t op = uint8_t(w0), a = uint8_t(w0 >> 8), length = bmVmOpLength(op);
      uint16_t w1 = length > 1 ? word(uint16_t(pc + 1)) : 0;
      int b = w1 & 0xFF, cc = w1 >> 8;
      while (lineAt < fn.lines.size() && fn.lines[lineAt].first <= pc) lineAt++;
      int line = lineAt > 0 ? fn.lines[lineAt - 1].second : 0;

      if (labels.count(pc) || resume.count(pc)) o << "L" << pc << ":;\n";
      const std::streamoff startAt = std::streamoff(o.tellp());
      if (blocks.count(pc))
      {
        int n = blockSize[pc];
        o << "  if ((c->budget -= " << n << ") < 0) { at = " << pc << "; goto Lbudget; }\n";
      }
      auto R = [](int r) { return "r" + std::to_string(r); };
      auto U = [&](int r) { return "(uint32_t)" + R(r); };
      switch (op)
      {
      case BM_OP_LOADI: o << "  " << R(a) << " = " << int16_t(w1) << ";\n"; break;
      case BM_OP_LOADK: o << "  " << R(a) << " = (int32_t)0x" << std::hex << rd32(consts + size_t(w1) * 4) << std::dec << "u;\n"; break;
      case BM_OP_MOVE: o << "  " << R(a) << " = " << R(w1) << ";\n"; break;
      case BM_OP_GETF: o << "  " << R(a) << " = inst->fields[" << w1 << "];\n"; break;
      case BM_OP_SETF:
        o << "  inst->fields[" << w1 << "] = " << R(a) << ";\n";
        o << "  if (h->fieldChanged) h->fieldChanged(h->pContext, inst, " << w1 << ");\n";
        break;
      case BM_OP_GETV: o << "  " << R(a) << " = h->getVar(h->pContext, inst, " << w1 << ");\n"; break;
      case BM_OP_SETV: o << "  h->setVar(h->pContext, inst, " << w1 << ", " << R(a) << ");\n"; break;
      case BM_OP_ADD: o << "  " << R(a) << " = (int32_t)(" << U(b) << " + " << U(cc) << ");\n"; break;
      case BM_OP_SUB: o << "  " << R(a) << " = (int32_t)(" << U(b) << " - " << U(cc) << ");\n"; break;
      case BM_OP_MUL: o << "  " << R(a) << " = (int32_t)(" << U(b) << " * " << U(cc) << ");\n"; break;
      case BM_OP_DIV: o << "  " << R(a) << " = c->pRt->div(" << R(b) << ", " << R(cc) << ");\n"; break;
      case BM_OP_MOD: o << "  " << R(a) << " = c->pRt->mod(" << R(b) << ", " << R(cc) << ");\n"; break;
      case BM_OP_FMUL: o << "  " << R(a) << " = (int32_t)(((int64_t)" << R(b) << " * " << R(cc) << ") >> 8);\n"; break;
      case BM_OP_FDIV: o << "  " << R(a) << " = c->pRt->fdiv(" << R(b) << ", " << R(cc) << ");\n"; break;
      case BM_OP_EQ: o << "  " << R(a) << " = " << R(b) << " == " << R(cc) << ";\n"; break;
      case BM_OP_NE: o << "  " << R(a) << " = " << R(b) << " != " << R(cc) << ";\n"; break;
      case BM_OP_LT: o << "  " << R(a) << " = " << R(b) << " < " << R(cc) << ";\n"; break;
      case BM_OP_LE: o << "  " << R(a) << " = " << R(b) << " <= " << R(cc) << ";\n"; break;
      case BM_OP_NEG: o << "  " << R(a) << " = (int32_t)(0u - " << U(w1) << ");\n"; break;
      case BM_OP_NOT: o << "  " << R(a) << " = !" << R(w1) << ";\n"; break;
      case BM_OP_JMP: o << "  goto L" << w1 << ";\n"; break;
      case BM_OP_JZ: o << "  if (" << R(a) << " == 0) goto L" << w1 << ";\n"; break;
      case BM_OP_JNZ: o << "  if (" << R(a) << " != 0) goto L" << w1 << ";\n"; break;
      case BM_OP_CALLN:
      {
        int api = b, first = cc;
        o << "  {\n    int32_t args[" << BM_SCRIPT_MAX_ARGS << "] = {";
        for (int i = 0; i < BM_SCRIPT_MAX_ARGS; i++) o << (i ? ", " : "") << (i < kApi[api].argc ? R(first + i) : std::string("0"));
        o << "}, result = 0;\n";
        o << "    bool waiting = h->call(h->pContext, inst, " << api << ", args, &result);\n";
        if (a != 0xFF) o << "    " << R(a) << " = result;\n";
        if (fn.stateCode) o << "    if (waiting) return " << pc + length << ";\n";
        else o << "    (void)waiting;\n";
        o << "    if (FAILED(c)) " << leave << "\n  }\n";
        break;
      }
      case BM_OP_CALL:
      {
        const Fn &callee = fns[w1];
        uint16_t first = word(uint16_t(pc + 2));
        o << "  if (c->depth >= BM_VM_MAX_DEPTH || c->registers + " << int(callee.regs) << " > BM_VM_STACK_REGISTERS) { c->pRt->fail(c, BM_VM_FAIL_DEPTH, "
          << index << ", " << pc << "); " << leave << " }\n";
        o << "  c->depth++;\n  c->registers += " << int(callee.regs) << ";\n  ";
        if (a != 0xFF) o << R(a) << " = ";
        o << callee.name << "(c";
        for (int i = 0; i < callee.args; i++) o << ", " << R(first + i);
        o << ");\n  c->depth--;\n  c->registers -= " << int(callee.regs) << ";\n";
        o << "  if (FAILED(c)) " << leave << "\n";
        break;
      }
      case BM_OP_RET:
        if (a != 0xFF) o << "  return " << R(a) << ";\n";
        else o << "  return 0;\n";
        break;
      case BM_OP_GOTOSTATE:
        o << "  inst->state = " << int(a) << ";\n  inst->pc = 0;\n  inst->timer = 0;\n  inst->wait = BM_VM_RUNNING;\n";
        if (fn.stateCode) o << "  return 0xFFFF;\n";
        break;
      case BM_OP_STOP:
        o << "  if (inst->state == stateBefore) inst->wait = BM_VM_STOPPED;\n  return 0xFFFF;\n";
        break;
      default: break;
      }
      stamp(startAt, line);
      pc = uint16_t(pc + length);
    }
    const std::streamoff trailerAt = std::streamoff(o.tellp());
    o << "  " << leave << "\nLbudget:\n  c->pRt->fail(c, BM_VM_FAIL_BUDGET, " << index << ", at);\n  " << leave << "\n}\n\n";

    // Events are run through an entry like state code.
    if (!fn.stateCode && fn.args == 0)
    {
      o << "static uint32_t " << fn.name << "_entry(BmVmNativeCtx *c, uint32_t pc)\n{\n  (void)pc;\n  " << fn.name << "(c);\n  return 0xFFFF;\n}\n\n";
    }
    stamp(trailerAt, fn.lines.empty() ? 0 : fn.lines.back().second);
  }

  o << "__attribute__((section(\".bmhead\"), used)) const struct { BmVmNativeHeader head; BmVmNativeEntry entries[" << (functionCount ? functionCount : 1)
    << "]; } bm_native_module = {\n  { BM_NATIVE_MAGIC, BM_NATIVE_VERSION, " << functionCount << ", 0x" << std::hex << bytecodeCrc << std::dec << "u },\n  {\n";
  for (const Fn &fn : fns)
  {
    if (fn.stateCode) o << "    " << fn.name << ",\n";
    else if (fn.args == 0) o << "    " << fn.name << "_entry,\n";
    else o << "    0,\n";
  }
  if (functionCount == 0) o << "    0,\n";
  o << "  }\n};\n";
  return o.str();
}

}  // namespace bm::script
