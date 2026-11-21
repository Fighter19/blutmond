#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""A debugging session against the game in mGBA, speaking GDB's remote protocol to bmlink's
bridge the way GDB does (docs/spec/link-protocol.md section 8):

  gdb_session.py <bmlink> <image.gba> <image.elf> <scripts.elf> <script.ms> <line>

Stops the game, puts a breakpoint on a script line (found through the native module's debug
information, as GDB would after add-symbol-file), runs to it, looks at registers and memory,
steps, interrupts, detaches; bmlink then plays the game to its end and checks it."""

import re
import socket
import subprocess
import sys
import time


def tool(name, *args):
    return subprocess.run(["arm-none-eabi-" + name, *args], check=True, capture_output=True, text=True).stdout


class Gdb:
    def __init__(self, port):
        deadline = time.time() + 20
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=30)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.05)
        self.buffer = b""

    def send(self, text):
        data = text.encode()
        self.sock.sendall(b"$" + data + b"#" + ("%02x" % (sum(data) & 0xFF)).encode())

    def receive(self):
        while True:
            m = re.search(rb"\$([^#]*)#..", self.buffer)
            if m:
                self.buffer = self.buffer[m.end():]
                self.sock.sendall(b"+")
                return m.group(1).decode()
            data = self.sock.recv(4096)
            if not data:
                raise RuntimeError("the bridge closed the connection")
            self.buffer += data

    def ask(self, text):
        self.send(text)
        return self.receive()

    def registers(self):
        raw = bytes.fromhex(self.ask("g"))
        assert len(raw) == 168, len(raw)
        regs = [int.from_bytes(raw[i * 4:i * 4 + 4], "little") for i in range(16)]
        return regs, int.from_bytes(raw[164:168], "little")

    def memory(self, address, length):
        return bytes.fromhex(self.ask("m%x,%x" % (address, length)))


def expect(what, condition):
    if not condition:
        print("FAIL:", what)
        sys.exit(1)
    print("ok  ", what)


def main():
    bmlink, image, image_elf, scripts_elf, script, line = sys.argv[1:7]
    line = int(line)

    # Where the module is loaded, and where the script line is inside it.
    slot = int(re.search(r"^([0-9a-f]+) \w bm_native_slot$", tool("nm", image_elf), re.M).group(1), 16)
    offsets = [int(m.group(1), 16) for m in re.finditer(r"^%s\s+%d\s+0x([0-9a-f]+)" % (re.escape(script.split("/")[-1]), line),
                                                         tool("readelf", "--debug-dump=decodedline", scripts_elf), re.M)]
    expect("the script line is in the module's debug information", offsets)
    address = slot + min(offsets)
    original = subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", "-j", ".text", scripts_elf, "/dev/stdout"],
                              check=True, capture_output=True).stdout[min(offsets):min(offsets) + 4]

    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    stats = image_elf + ":bm_game_stats"
    # Play into the second room (where the bat is), debug, then play on to the end.
    link = subprocess.Popen([bmlink, "--mgba", image, "run", "760", "hello", "gdb", str(port), "run", "700",
                             "check", stats + "+8=0", "check", stats + "+12=1", "check", stats + "+56=0", "check", stats + "+24"])
    try:
        gdb = Gdb(port)
        expect("the game is stopped when GDB connects", gdb.ask("?") == "S02")
        gdb.ask("qSupported:multiprocess+")
        regs, cpsr = gdb.registers()
        expect("stopped in the runtime's Thumb code, with a stack in IWRAM", cpsr & 0x20 and 0x03000000 <= regs[13] < 0x03008000)
        expect("the module's code is in its slot", gdb.memory(address, 4) == original)
        expect("no breakpoints in Thumb code or in ROM", gdb.ask("Z0,%x,2" % regs[15]) == "E01" and gdb.ask("Z0,8000100,4") == "E01")
        expect("memory outside what the device has is refused", gdb.ask("m1000000,4") == "E01")

        expect("breakpoint on %s:%d at 0x%08x" % (script, line, address), gdb.ask("Z0,%x,4" % address) == "OK")
        expect("the game runs to the breakpoint", gdb.ask("c") == "S05")
        regs, cpsr = gdb.registers()
        expect("it stopped at the script line, in ARM code", regs[15] == address and not cpsr & 0x20)
        first = regs
        expect("removing the breakpoint puts the instruction back", gdb.ask("z0,%x,4" % address) == "OK" and gdb.memory(address, 4) == original)

        # Single steps: each lands on an instruction inside the module. A call into the
        # runtime (Thumb code, reached with bx) is stepped over as a whole.
        seen = [address]
        calls = 0
        for _ in range(30):
            insn = int.from_bytes(gdb.memory(seen[-1], 4), "little")
            if gdb.ask("s") != "S05":
                expect("step", False)
            regs, cpsr = gdb.registers()
            if insn & 0x0FFFFFF0 == 0x012FFF10:
                expect("a call into the runtime is stepped over", regs[15] == seen[-1] + 4)
                calls += 1
            seen.append(regs[15])
        expect("the steps passed a call into the runtime", calls >= 1)
        print("     steps went to", " ".join("%08x" % a for a in seen))
        expect("the steps moved on inside the module", len(set(seen)) == len(seen) and all(slot <= a < slot + 0x2000 for a in seen))
        expect("the stack pointer is the program's own", abs(regs[13] - first[13]) < 256)

        # Registers and memory can be changed. The unused end of the module's slot is free
        # memory (below the stack pointer is not: the stopped runtime works there).
        scratch = slot + 0x1FF0
        expect("memory can be written", gdb.ask("M%x,4:deadbeef" % scratch) == "OK" and gdb.memory(scratch, 4) == bytes.fromhex("deadbeef"))
        expect("a register can be written", gdb.ask("P1=78563412") == "OK" and gdb.registers()[0][1] == 0x12345678)
        expect("and put back", gdb.ask("P1=" + regs[1].to_bytes(4, "little").hex()) == "OK" and gdb.registers()[0][1] == regs[1])

        # The same breakpoint again: the next frame's tick stops there.
        expect("breakpoint again", gdb.ask("Z0,%x,4" % address) == "OK")
        expect("the next frame stops there too", gdb.ask("c") == "S05" and gdb.registers()[0][15] == address)
        expect("breakpoint removed", gdb.ask("z0,%x,4" % address) == "OK")

        # Run on, interrupt (Ctrl-C).
        gdb.send("c")
        time.sleep(0.2)
        gdb.sock.sendall(b"\x03")
        expect("Ctrl-C stops the running game", gdb.receive() == "S02")
        expect("detach", gdb.ask("D") == "OK")
    except Exception:
        link.kill()
        raise
    code = link.wait(timeout=120)
    expect("after the session the game plays to its end without the scripts failing", code == 0)


if __name__ == "__main__":
    main()
