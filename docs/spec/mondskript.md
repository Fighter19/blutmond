# Mondskript

Version 0.6, draft.

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
  defaultproperties enum struct is true false null self`.

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
- `gotostate S;` in state code leaves the state at once; in an event or a function the rest
  of it still runs, and the new state's code starts at the object's next step (section 11,
  "An object and its state code").
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
  version, counts), classes, states, variables, functions, constant pool, code, line table.
  The loader **verifies** it before use and rejects a module that fails. The layout, the
  instructions and the verifier's rules are in section 11.
- **Native**: the generated C uses no global variables (only a context pointer) and calls the
  runtime only through that context. It is compiled with `arm-none-eabi-gcc` and linked at
  address 0 with `--emit-relocs`; the packer keeps the list of absolute addresses and the
  loader copies the module into a slot and patches them. `#line` directives in the C make GDB
  show script lines. Section 12 has the details.
- Both forms produce identical results; tests compare the game state between them.

## 11. What is implemented

The compiler (`tools/mondskript`), the interpreter (`src/script/vm.h`) and the game's scripted
objects (`src/world/actors.h`) implement a first part of the above. Script files end in `.ms`
and hold enums and structures, then at most one class. What a file declares can be used in
every other file of the same module.

| Area | Implemented | Not yet |
|---|---|---|
| Types | `bool`, `int`, `fix`, `name`, `text`, enums; every variable takes 4 bytes | `byte`/`short` and unsigned types in classes, `fix16`, `vec`, structs as variables, arrays, handles (`C`, `C?`) |
| Enums | `enum E { A, B }` before the class or in a file of its own; variables and parameters of type `E`; values written `E.A`, or just `A` where the type is known (defaults, `defaultproperties`); `==` and `!=` between values of the same enum. An enum is not a number: assigning one to `int` or to another enum is an error | `switch`, conversion to `int` |
| Structures | `struct S { ... }` and `struct S is Base { ... }` declared and laid out for records: field types, alignment, defaults, sizes, see "Structures" below | structures as variables of a class |
| Records | `record<S>` variables, parameters and results, `#Name` as a record, field access (`.field`, `.inner.field`, `.array[2]`, through `record<T>` fields), `for x in records(S)`; [data-format.md](data-format.md) §5 | a dynamic array index |
| Classes | `class Name extends Actor;`, `var` with `(Category)` and `save`/`link`/`transient`/`derived`, `const`, at most 8 variables | other base classes, inheritance between script classes, components (`has`) |
| Functions | functions with up to 3 parameters and a result, calls nested up to 16 deep; all are final | `virtual`, `static` |
| Events | `spawn()`, `tick()`, `touch()` (the player starts touching the object), in the class and per state | parameters; `hit`, `dies`, `enter_room`, `leave_room`, `use` |
| States | `state` with events and state code after `begin:`, `gotostate`, `start_state`; latent `sleep(frames)`, `finish_anim()`, `wait_until(condition)` | functions overridden per state; locals and `for` in state code (their values would not survive a wait: use variables of the class) |
| Statements | `var` locals, `if`/`else`, `while`, `loop`, `for x in a..b`, `break`, `continue`, `return`, assignment with `=`, `+=`, `-=`, `*=`, `/=` | `switch`, `match`, queries |
| Expressions | `+ - * / %`, comparisons, `&& || !` (short circuit), `fix(...)`, `int(...)`; integer literals become `fix` where `fix` is expected; mixing `int` and `fix` is an error | |
| defaultproperties | values of variables, `start_state`, `sprite = "name"` | component and struct fields |
| Run time | 8 scripted objects per room, 16 values of `save` variables kept across rooms and in the save game, 4 items | larger limits need state transfer in several packets |

### Structures

```
enum Element { None, Fire, Ice }
struct Item { text title = "Nothing"; name icon; short price = 10; ubyte max_stack = 1; }
struct Weapon is Item { int attack = 1; Element element = Fire; record<Weapon> upgrade; short[3] combo = 7; }
```

- Field types (`src/data/data_types.h`): `bool`, `byte`, `ubyte`, `enum` (1 byte); `short`,
  `ushort`, `name`, `text`, `sprite`, `sound`, `attach` (2); `int`, `uint`, `fix`,
  `record<S>` (4); another structure, inline; and fixed arrays `T[n]` of any of them.
- Fields are laid out in declaration order, each aligned to its size (an inner structure to
  its largest field); `is Base` puts the base's fields first, at the same offsets. The size
  is rounded up to a multiple of 4. At most 32 fields and 64 bytes.
- Defaults: numbers (checked against the type's range), `true`/`false`, `#Name` for `name`
  and `record<S>`, `"text"` for `text`, `"name"` or `#name` for `sprite` and `sound`, a class
  name for `attach`, an enum value; an array takes one value for every element. Without a
  default a field is 0, and `sprite`, `sound` and `attach` are none (0xFFFF). An inner
  structure has the defaults of its own fields.
- A record is identified by the FNV-1a hash of its name (`nameKey`), as items are.
- The compiler hands the layouts to the cooker (`Module::structs`), which reports the size of
  every structure. They reach the pack with the records as `DTAB` chunks
  ([data-format.md](data-format.md)).

What scripts can call is one table shared by compiler, verifier and runtime
(`src/script/script_api.h`); an entry's position is its index in bytecode, so entries are only
added at the end. This version: `sleep`, `finish_anim`, `play_anim(name)`, `anim_done`,
`player_x`, `player_y`, `player_distance`, `move_toward_player(fix)`, `move_toward(fix, fix,
fix)`, `hurt_player(int)`, `enter_door`, `hide`, `show`, `face_player`, `log(int)`,
`random(int)`, `touching_player`, `give_item(name)`, `has_item(name)`, `abs(int)`,
`play_sound(name)`, `play_music(name)`, and for records `record_get`, `record_count`,
`record_at` (written by the compiler, not by hand); and the
actor variables `x`, `y`, `facing` and (read only) `home_x`, `home_y`. What each of these does,
exactly, is in [runtime-core.md](runtime-core.md) §8.

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

Everything is little endian. Variable types: 0 void, 1 bool, 2 int, 3 fix, 4 name, 5 text
(names and texts are string ids), 6 enum (a value of an enum, numbered from 0 in declaration
order; which enum is known only to the compiler), 7 record (a record's key). Events in the order of their numbers: 0 `spawn`, 1 `tick`,
2 `touch`. The line table's offset may be 0 (none).

#### Instructions

The first word of an instruction is `op | a << 8`. The numbers:

| # | Instruction | Words | Operands | Effect |
|---|---|---|---|---|
| 0 | `LOADI` | 2 | w1: value | `r[a]` = w1 as a signed 16-bit number |
| 1 | `LOADK` | 2 | w1: constant | `r[a]` = constant w1 |
| 2 | `MOVE` | 2 | w1: register | `r[a] = r[w1]` |
| 3 | `GETF` | 2 | w1: variable | `r[a]` = the object's variable w1 |
| 4 | `SETF` | 2 | w1: variable | the object's variable w1 = `r[a]`; a `save` variable is also kept ([runtime-core.md](runtime-core.md) §8) |
| 5 | `GETV` | 2 | w1: actor variable | `r[a]` = actor variable w1 |
| 6 | `SETV` | 2 | w1: actor variable | actor variable w1 = `r[a]` |
| 7 | `ADD` | 2 | w1 = `b \| c << 8` | `r[a] = r[b] + r[c]`, wrapping at 32 bits |
| 8 | `SUB` | 2 | same | `r[b] - r[c]`, wrapping |
| 9 | `MUL` | 2 | same | `r[b] * r[c]`, wrapping |
| 10 | `DIV` | 2 | same | `r[b] / r[c]` truncating toward zero; 0 if `r[c]` is 0, and 0 for the smallest number divided by -1 |
| 11 | `MOD` | 2 | same | the remainder of that division (sign of `r[b]`); 0 if `r[c]` is 0 or -1 |
| 12 | `FMUL` | 2 | same | `(int64(r[b]) * r[c]) >> 8`, arithmetic shift |
| 13 | `FDIV` | 2 | same | `(int64(r[b]) * 256) / r[c]` truncating; 0 if `r[c]` is 0 |
| 14 | `EQ` | 2 | same | 1 if equal, else 0 |
| 15 | `NE` | 2 | same | 1 if different, else 0 |
| 16 | `LT` | 2 | same | 1 if `r[b] < r[c]` (signed), else 0 |
| 17 | `LE` | 2 | same | 1 if `r[b] <= r[c]` (signed), else 0 |
| 18 | `NEG` | 2 | w1: register | `r[a] = 0 - r[w1]`, wrapping |
| 19 | `NOT` | 2 | w1: register | `r[a]` = 1 if `r[w1]` is 0, else 0 |
| 20 | `JMP` | 2 | w1: target | continue at word w1 of this function |
| 21 | `JZ` | 2 | w1: target | ... if `r[a]` is 0 |
| 22 | `JNZ` | 2 | w1: target | ... if `r[a]` is not 0 |
| 23 | `CALLN` | 2 | w1 = `api \| first << 8` | calls runtime function `api` with the registers from `first` as arguments; `r[a]` = its result unless `a` is 0xFF |
| 24 | `CALL` | 3 | w1: function, w2: first | calls a function of the same class with the registers from `first` as arguments; `r[a]` = its result unless `a` is 0xFF |
| 25 | `RET` | 1 | | returns `r[a]`, or nothing if `a` is 0xFF |
| 26 | `GOTOSTATE` | 1 | | the object's state becomes `a` (below) |
| 27 | `STOP` | 1 | | the end of state code (below) |

The runtime functions and the actor variables, by number, are in
[runtime-core.md](runtime-core.md) §8. The compiler writes `wait_until(condition)` as a loop
around `sleep(1)`; it is not a runtime function.

#### Running a function

- A function has its own registers (as many as its record says, at most 32), all 0 at the
  start except the first ones, which hold the arguments. State code starts with all registers
  0 every time it runs on, which is why it cannot keep local values across a wait.
- A call puts the callee's registers behind the caller's. At most 16 calls inside each other
  and 128 registers in all; more is the error "calls nested too deep".
- A run executes at most 3999 instructions; the next one is the error "the script runs too
  long". (Native code counts per block of straight-line code and may stop up to one block
  earlier.)
- After an error the object's wait becomes "failed" and it runs no script again.
- `RET` in the outermost function ends the run.

#### An object and its state code

Per object: class, state (0xFF: none), wait, place (a word in the state's code), timer, and 8
variables. Wait values: 0 running, 1 sleeping, 2 waiting for its animation, 3 stopped (its
state code reached `STOP`), 4 failed.

- **New object**: variables to the defaults of its class, the class's start state, wait 0,
  place 0.
- **Event** (`spawn`, `tick`, `touch`): runs the state's function for that event if the state
  has one, else the class's, else nothing. Not for a failed object; a stopped one still gets
  events.
- **Step**, once per frame:
  - failed or stopped: nothing;
  - sleeping: if the timer is above 1, it is decremented and that is all; else it becomes 0
    and the code runs on. So `sleep(n)` runs on n frames later;
  - waiting for its animation: runs on once the animation is done
    ([runtime-core.md](runtime-core.md) §8);
  - no state, or a state without code: nothing (a state without code makes the object
    stopped);
  - else the state's code runs from the object's place, with wait set to running.
- **A latent function** (`sleep`, `finish_anim`; only in state code) sets wait and timer and
  ends the run; the place becomes the word after the call, unless the state changed meanwhile.
- **`GOTOSTATE`** sets the state, place 0, timer 0, wait running. In state code itself the run
  ends there, and the new state's code starts at the next step. In an event, or in a function
  called from state code, execution goes on to the end of what is running.
- **`STOP`** ends the run; the object becomes stopped unless its state changed during this
  run.

#### The verifier

A module is refused unless all of this holds:

- header: `BMMB`, version 1, events per class 3, and not more API entries than the runtime
  has;
- every table lies inside the module, and one word of space follows the code;
- a class has at most 8 variables; its start state exists or is 0xFF;
- a function is not empty, at most 512 words, has at most 32 registers, at most 3 parameters
  and not more parameters than registers, and belongs to a class that exists;
- every instruction is known and complete; registers are below the function's count;
  variables inside the class; constants, states and API numbers exist; arguments lie inside
  the registers;
- every jump lands on the first word of an instruction of the same function;
- the last instruction of a function is `RET`, `STOP` or `JMP`;
- latent functions and `STOP` only in state code, `RET` only outside it;
- `CALL` only to a function of the same class that is not state code;
- an event's function and a state's code belong to that class, have no parameters, and are
  (not) state code as their use says.

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
- 0.4: instruction numbers and encodings, how functions and state code run, the verifier's
  rules; what the runtime functions do moved to runtime-core.md.
- 0.5: enums, and structures laid out for records; variable type 6 (enum).
- 0.6: `record<S>`, field access, `for x in records(S)`; variable type 7 (record); runtime
  functions 22 to 24.
