# Mondskript

Version 0.3, draft.

Mondskript is Blutmond's scripting language. It is modelled on the old UnrealScript (classes
with declared fields, states with latent functions, `defaultproperties`) and avoids its known
problems (run-time strings, floats, everything virtual, `None` crashes, deep inheritance). The
reasons are in [../PLAN.md](../PLAN.md), section 10.

Design rules:

- Every field is known at compile time, so the state layout is fixed ([state.md](state.md)).
- Results are bit exact on every target: no floats, defined overflow, defined division.
- Nothing allocates at run time.

## 1. Lexical

- Source is UTF-8. Identifiers: `[A-Za-z_][A-Za-z0-9_]*`, case sensitive.
- Comments: `// ...` and `/* ... */` (not nested).
- Literals: integers (`42`, `0x2A`, `0b101010`, `_` as separator), fixed point (`1.5`; a literal
  with a `.` is `fix`), booleans (`true`, `false`), names (`#Idle`), strings (`"..."`, only
  where a string ID is expected).
- Keywords: `class extends has var const function virtual final static event state begin
  gotostate if else while for in loop break continue return switch case default match
  defaultproperties enum struct true false null self`.

## 2. Types

| Type | Size | Notes |
|---|---|---|
| `bool` | 1 | |
| `byte`, `short`, `int` | 1, 2, 4 | signed; `ubyte`, `ushort`, `uint` unsigned |
| `fix` | 4 | 24.8 fixed point |
| `fix16` | 4 | 16.16 fixed point |
| `name` | 2 | ID assigned at compile time (`#Idle`) |
| `text` | 2 | string ID into the string table; no operations except passing it on |
| `vec` | 8 | two `fix` (`x`, `y`) |
| `enum E { ... }` | 1 or 2 | |
| `struct S { ... }` | sum of fields | value type |
| `T[n]` | n × T | fixed size, `n` is a constant |
| `C` (class name) | 4 | handle to an object of class `C` or a subclass; never null |
| `C?` | 4 | optional handle; MUST be checked before use (§6) |

Conversions between `int` and `fix` are explicit (`fix(i)`, `int(f)` truncates toward zero).
Literals adapt to the expected type. Arithmetic wraps on overflow. `fix * fix` and `fix / fix`
keep 8 fraction bits. Division or modulo by zero yields 0 and, in debug builds, reports through
the link.

## 3. Classes

```
class Bat extends Enemy;
has Health, Flight;               // components

var(Combat) int Damage = 1;       // category "Combat" in the editor
var(Flight) fix Speed  = 1.5;
var save      bool Defeated;
var transient int  Blink;
const int MAX_HITS = 3;
```

- A class extends at most one class. Engine base classes: `Object` (no position), `Actor`
  (position, room, persistent ID), and those built on it.
- `has` adds components. A component is a class without a base class whose fields and
  functions become part of the owner; name clashes are an error.
- `var` declares a field. Specifiers:
  - `(Category)`: shown in the editor and settable from Tiled object properties;
  - storage flags `save`, `link`, `transient`, `derived` (see [state.md](state.md) §2).
- `const` declares a compile-time constant.

## 4. Functions and events

```
function int damage_for(Player p) { return Damage * 2; }
virtual function attack() { ... }
static function vec clamp_to_room(vec p) { ... }
event touch(Actor other) { ... }
```

- Functions are final by default; `virtual` allows overriding in subclasses (one indirect call).
- `static` functions have no `self`.
- `event` functions are called by the runtime. Engine events: `spawn()`, `tick()`,
  `touch(Actor)`, `hit(Actor, int)`, `dies()`, `enter_room()`, `leave_room()`,
  `use(Actor)`. An event that is not defined does nothing.

## 5. States and latent functions

```
state Sleeping {
  event sees(Player p) { gotostate Attacking; }
}

state Attacking {
begin:
  play_anim(#WakeUp);
  finish_anim();
  loop {
    move_toward(player().pos, Speed);
    sleep(2);
  }
}
```

- A state can override events and functions; the class's versions apply outside states.
- State code (after `begin:`) runs as a coroutine. **Latent functions** (`sleep(frames)`,
  `finish_anim()`, `move_to(vec)`, `wait_until(expr)`, and those marked `latent` in the runtime
  API) may only be called in state code; anywhere else they are a compile error.
- The compiler turns state code into a state machine. The current state and the resume point
  are ordinary fields of the object (flag `snapshot`, plus `save`/`link` if the class says so),
  so save states, link transfer and live reloads work in the middle of state code.
- `gotostate S;` leaves the current state at the end of the current event or latent step.
- The initial state is set with `start_state` in `defaultproperties`.

## 6. Handles and queries

```
var Enemy? target;

if e := target {          // e is Enemy (not optional) inside the block
  e.hit(self, Damage);
}

for e in room.query(Bat, within = 64) { ... }   // fixed capacity, at most 16 results
```

- Using a `C?` without checking it is a compile error.
- A handle that has become stale (object gone) fails the check; it never refers to another
  object.
- Queries go through runtime indices (room, class, group, spatial grid). There is no
  "iterate over everything".

## 7. Statements

`if`/`else`, `while`, `for x in a..b` (b exclusive), `for x in query`, `loop`, `break`,
`continue`, `return`, `switch`/`case`/`default` on integers, enums and names, `match` on
optional handles. Loops outside state code SHOULD be bounded; the compiler warns about loops
it cannot bound.

## 8. defaultproperties

```
defaultproperties {
  Health.max  = 3;
  start_state = Sleeping;
  hitbox      = (w = 12, h = 10, x = -6, y = -10);
}
```

Initial values of the class's fields, including inherited and component fields. Values from
Tiled object properties override them per object. The runtime resets an object to these values
on `restart` (see [runtime-abi.md](runtime-abi.md) §5).

## 9. Text and strings

There is no string type at run time. `"..."` in source is placed in the string table and
becomes a `text` value. Dialogue and UI text come from text assets with IDs and are translated
there.

## 10. Compilation

```
source → parser → AST → type check → IR ─┬─► bytecode module ('MODB')
                                         └─► C ─► gcc ─► native module ('MODN')
```

- Text and node views in the editor show the same AST; the text is what is stored, node
  positions are kept in a side file keyed by stable node IDs.
- **Bytecode**: register based, 16-bit instructions. A module contains: header (magic `BMMB`,
  version, ABI version, class IDs it defines), constant pool, functions (register count,
  code), line table (instruction → source line). The loader **verifies** it before use: every
  jump target is an instruction start, every register index is below the function's register
  count, every field access is inside the object's layout, every runtime call is a known API
  index with the right argument count. A module that fails verification is rejected.
- **Native**: the generated C uses no global variables (only a context pointer) and calls the
  runtime only through that context. It is compiled with `arm-none-eabi-gcc` and linked at
  address 0 with `--emit-relocs`; the packer keeps the list of absolute addresses and the
  loader copies the module into a slot and patches them. `#line` directives in the C make GDB
  show script lines. Section 12 has the details.
- Both forms produce identical results; tests compare the game state between them.

## 11. What version 0.2 implements

The compiler (`tools/mondskript`), the interpreter (`src/script/vm.h`) and the game's scripted
objects (`src/world/actors.h`) implement a first part of the above. Script files end in `.ms`
and hold one class each.

| Area | Implemented | Not yet |
|---|---|---|
| Types | `bool`, `int`, `fix`, `name`, `text`; every variable takes 4 bytes | `byte`/`short` and unsigned types, `fix16`, `vec`, enums, structs, arrays, handles (`C`, `C?`) |
| Classes | `class Name extends Actor;`, `var` with `(Category)` and `save`/`link`/`transient`/`derived`, `const`, at most 8 variables | other base classes, inheritance between script classes, components (`has`) |
| Functions | functions with up to 3 parameters and a result, calls nested up to 16 deep; all are final | `virtual`, `static` |
| Events | `spawn()`, `tick()`, `touch()` (the player starts touching the object), in the class and per state | parameters; `hit`, `dies`, `enter_room`, `leave_room`, `use` |
| States | `state` with events and state code after `begin:`, `gotostate`, `start_state`; latent `sleep(frames)`, `finish_anim()`, `wait_until(condition)` | functions overridden per state; locals and `for` in state code (their values would not survive a wait: use variables of the class) |
| Statements | `var` locals, `if`/`else`, `while`, `loop`, `for x in a..b`, `break`, `continue`, `return`, assignment with `=`, `+=`, `-=`, `*=`, `/=` | `switch`, `match`, queries |
| Expressions | `+ - * / %`, comparisons, `&& || !` (short circuit), `fix(...)`, `int(...)`; integer literals become `fix` where `fix` is expected; mixing `int` and `fix` is an error | |
| defaultproperties | values of variables, `start_state`, `sprite = "name"` | component and struct fields |
| Run time | 8 scripted objects per room, 16 values of `save` variables kept across rooms and in the save game, 4 items | larger limits need state transfer in several packets |

What scripts can call is one table shared by compiler, verifier and runtime
(`src/script/script_api.h`); an entry's position is its index in bytecode, so entries are only
added at the end. This version: `sleep`, `finish_anim`, `play_anim(name)`, `anim_done`,
`player_x`, `player_y`, `player_distance`, `move_toward_player(fix)`, `move_toward(fix, fix,
fix)`, `hurt_player(int)`, `enter_door`, `hide`, `show`, `face_player`, `log(int)`,
`random(int)`, `touching_player`, `give_item(name)`, `has_item(name)`, `abs(int)`,
`play_sound(name)`, `play_music(name)`; and the
actor variables `x`, `y`, `facing` and (read only) `home_x`, `home_y`.

Objects of a room whose Tiled class is a script class are scripted; the engine then leaves them
alone, also for engine classes (a class `Door` in a script decides when the door opens and
calls `enter_door()`). An object property named like a `var(Category)` variable sets it for
that object.

### Bytecode module (`MODB`)

```
0   "BMMB"   u16 version (1)   u16 API entries the module was compiled against
8   u16 classes   u16 functions   u16 constants   u16 events per class (3)
16  u32 offset of: classes, functions, constants, code;  u32 code words;  u32 offset of the line table

class (24 bytes):    u16 name, u16 sprite (string ids; 0xFFFF: none), u8 variables, u8 states,
                     u8 start state (0xFF: none), u8 0, u16 function of each event (0xFFFF: none),
                     u16 0, u32 offset of its variables, u32 offset of its states
variable (8 bytes):  u16 name, u8 type, u8 flags (1 save, 2 link, 4 transient, 8 set per object), s32 default
state (12 bytes):    u16 name, u16 function with its code, u16 function of each event, u16 0
function (12 bytes): u32 first code word, u16 code words, u8 registers, u8 parameters,
                     u8 flags (1: state code), u8 0, u16 class
constants:           s32 each
code:                u16 words, followed by one spare word
line table:          per function a u32 offset to: u16 file (string id), u16 n, n × (u16 word, u16 line)
```

An instruction is `op | a << 8` followed by its operand words:

| Instruction | Words | Effect |
|---|---|---|
| `LOADI a, imm` / `LOADK a, k` | 2 | register a = sign-extended word / constant k |
| `MOVE a, b`, `NEG a, b`, `NOT a, b` | 2 | |
| `GETF a, f` / `SETF a, f` | 2 | variable f of the object |
| `GETV a, v` / `SETV a, v` | 2 | actor variable v |
| `ADD SUB MUL DIV MOD FMUL FDIV EQ NE LT LE a, b, c` | 2 (b and c in one word) | division and modulo by zero give 0; `FMUL`/`FDIV` keep 8 fraction bits |
| `JMP t`, `JZ a, t`, `JNZ a, t` | 2 | to word t of the function |
| `CALLN a, api, first` | 2 | runtime function with arguments in the registers from `first`; a = result |
| `CALL a, function, first` | 3 | a function of the same class |
| `RET a` | 1 | a = 0xFF: no result |
| `GOTOSTATE s`, `STOP` | 1 | `STOP` ends state code |

The verifier refuses a module unless: every table lies inside the module; every instruction is
known and complete; registers are below the function's count, variables inside the class,
constants, states, functions and API entries exist; arguments fit; jumps land on instructions;
no function can run off its end; latent functions and `STOP` only appear in state code and
`RET` only outside; events and state code are functions of their class without parameters.
At run time an instruction budget (4000 per call) stops endless loops and a depth limit stops
endless recursion; the object then stops running scripts and the error names the source line.

## 12. Native modules (`MODN`)

A pack can hold the scripts a second time as machine code. The bytecode module stays: it has
the class tables, and it is what runs wherever the native code cannot (the PC, a pack pushed
from the editor, a module too big for its slot). `bmcook --native arm-none-eabi-gcc` makes
both; `--native-elf file` keeps the linked module with debug information, `--script-c file`
the generated C.

**Translation.** The C back end (`tools/mondskript/aot.cpp`) translates the *bytecode*, one C
function per bytecode function, registers as local variables. So native code is by
construction the interpreter's behaviour, not a second implementation of the language:

- State code is a function `(context, word) → word`: it starts with a `switch` over the
  bytecode word to continue at, and returns the word after a latent call that started
  waiting. The place an object waits at is therefore the same number in both forms, and game
  state (save states, state sent over the link) made with one runs on with the other.
- Events are functions without parameters behind the same kind of entry; other functions are
  called directly, with the interpreter's depth limit (16) and register limit.
- The instruction budget is counted per block of straight-line code instead of per
  instruction. A script that runs into the budget is stopped with the same error, but it may
  have run up to one block less far than in the interpreter. That is the only difference.
- Division, modulo and fixed-point division go through the runtime (`BmVmNativeRt`), so they
  are the same code as in the interpreter and the module needs no library.
- `#line` directives name the script file and line of every instruction.

**Building.** `-marm -mcpu=arm7tdmi -mthumb-interwork -O2 -ffreestanding -fno-pic`, linked
with a script that puts the module header (`BmVmNativeHeader`, section `.bmhead`) at address
0, followed by code and constants. The packer (`tools/cooker/native_module.cpp`) reads the ELF
file and refuses it if it has writable or zero-initialised data (a module has no variables of
its own: everything is in the object and the context), or a relocation it cannot handle.
`R_ARM_ABS32` places go into the address list; relative ones (`R_ARM_CALL`, `R_ARM_JUMP24`,
`R_ARM_REL32`, `R_ARM_PREL31`, `R_ARM_V4BX`, the Thumb branches) need nothing, since distances
inside the module do not change when it is loaded.

```
chunk:  "BMNA"   u16 version (1)   u16 machine (1: ARMv4T, little endian)
        u32 image size (a multiple of 4)   u32 number of addresses
        image
        u32 offset in the image of each address, ascending

image:  "BMNA"   u16 version   u16 functions   u32 CRC-32 of the bytecode module
        u32 entry of each function (0: not an entry; else an offset, relocated like any address)
        code and constants
```

**Loading** (`src/script/native.h`). When a pack's bytecode module is opened, the runtime
looks for a `MODN` chunk for its machine, checks it in the pack (sizes, every address inside
the image, the CRC of the bytecode it was made from, every entry a relocated address behind
the table), copies the image into its slot (8 KB of IWRAM on the GBA) and adds the slot's
address to every listed word. From then on `bmVmEvent` and `bmVmStep` call the entry of a
function instead of interpreting it. A chunk that is refused counts as a script error; the
scripts then run interpreted. A new pack without native code (a live push) switches back to
the interpreter at the same frame boundary as any reload; no native function is running then,
because waits return to the runtime.

Native code is **not verified**: it is trusted as much as the runtime itself, which is why
scripts run as bytecode during development ([runtime-abi.md](runtime-abi.md)).

**Measured** (mGBA, `apps/script_bench`, loops with integer and fixed-point arithmetic and
calls): from the cartridge the native module takes about 35 % of the interpreter's time, in
the multiboot image about 44 % (there the runtime's division helpers run from slow RAM). The
test level's three classes are 304 bytes of bytecode and 3.3 KB of ARM code. Not done yet:
Thumb modules for slower memory, several slots with reference counts (one module per pack
for now), per-class modules loaded on room changes.

## History

- 0.1: first draft.
- 0.2: the implemented subset, the API table, the bytecode module and its verification.
- 0.3: native modules (section 12).
