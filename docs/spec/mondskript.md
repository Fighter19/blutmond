# Mondskript

Version 0.1, draft.

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
  runtime only through the API table. It is compiled with `arm-none-eabi-gcc` (`-mthumb`, or
  `-marm` for IWRAM) and linked at address 0 with `--emit-relocs`. The packer keeps only
  `R_ARM_ABS32` relocations and rejects any other; the module (magic `BMMN`) contains code,
  data, the relocation list and the same line table. The loader copies it into a slot and
  patches the relocations. `#line` directives in the C make GDB show script lines.
- Both forms produce identical results; tests compare state hashes per frame.

## History

- 0.1: first draft.
