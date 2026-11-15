#!/usr/bin/env python3
"""USCL shared overworld for Pokemon HeartGold (IPKE, USA rev 0) in melonDS.

One agent per player. It talks to that player's melonDS through melonDS's built-in GDB stub (ARM9 port),
reads where the player is, sends it to the other agents over UDP, and draws every other player who is on
the same map as an overworld NPC ("puppet") in this player's game.

    python hgss_world.py --gdb 3333 --listen 127.0.0.1:47600 --peer 127.0.0.1:47601 --name A
    python hgss_world.py --gdb 3343 --listen 127.0.0.1:47601 --peer 127.0.0.1:47600 --name B
    python hgss_world.py --selftest

Addresses come from pret/pokeheartgold (it targets exactly this ROM: sha1 4fcded0e...). See
games/IPKE.md for how each one was found. Other ROM revisions or regions need their own table.
"""
import argparse, json, os, re, socket, struct, subprocess, sys, threading, time

# --- HeartGold USA rev 0 (IPKE, sha1 4fcded0e2713dc03929845de631d0932ea2b5a37) ---
FIELD_SYS_PTR = 0x021D4158   # static FieldSystem *sFieldSysPtr (field_system.c)
FIELD_CONTROL = 0x0203E15C   # FieldSystem_Control: runs once per frame while the player can move (safe call point)
MAPOBJ_CREATE = 0x0205E294   # MapObject_Create(manager, x, z, dir, sprite, movement, mapNo) -> LocalMapObject *
MAPOBJ_REMOVE = 0x0205E3CC   # MapObject_Remove(object)
FS_LOCATION, FS_MAPOBJ_MAN, FS_PLAYER = 0x20, 0x3C, 0x40   # FieldSystem fields
AVATAR_MAPOBJ = 0x30                                         # PlayerAvatar.mapObject
# LocalMapObject fields
MO_FLAGS, MO_ID, MO_MAP, MO_SPRITE, MO_FACING = 0x00, 0x08, 0x0C, 0x10, 0x28
MO_PREV_XYZ, MO_CUR_XYZ, MO_POS, MO_MOVE_CMD = 0x58, 0x64, 0x70, 0xA4   # MO_MOVE_CMD: movementCmd, movementStep
MO_ACTIVE, MO_HELD, MO_HELD_DONE = 0x01, 0x10, 0x20   # MapObject_SetHeldMovement (0x0206214C) sets HELD, clears DONE
STEP_WALK, STEP_RUN, STEP_MAX = 12, 16, 0x71          # movement commands come in groups of 4: up, down, left, right
SPRITE_MAX = 1050            # highest real sprite id in include/constants/sprites.h is 1049
PUPPET_ID_BASE = 0xF0        # object ids no map uses; marks our puppets
MOVEMENT_STILL = 0           # most common object-event movement (item balls, standing NPCs)
# The game's own keyboard ("Your friend's name?") as the session-code box
FS_TASKMAN = 0x10                  # FieldSystem.taskman
FS_FOLLOW_MON, FOLLOW_ACTIVE = 0xE4, 0x16   # FieldSystem.followMon (mapObject first), FollowMon.active
MO_PARAMS = 0x38                   # LocalMapObject.param[3]: a follower's species, form/shiny bits
MON_SUFFIX = "/mon"                # remote[name + MON_SUFFIX]: that player's walking Pokemon
MO_PAUSED = 0x40                   # MAPOBJECTFLAG_MOVEMENT_PAUSED: its own movement type (wander, look around) stops
NPC_EVERY = 3                      # NPC sync every 3rd tick (~10/s)
CREATE_TASK = 0x020504F0           # FieldSystem_CreateTask(fs, func, env) -> TaskManager *
TASK_DONE = 0x0203E3A8             # Battle_Init: `return TRUE;` (a task ends only on exactly TRUE, task.c)
CALLTASK_NAMING = 0x0203F6E0       # CallTask_NamingScreen(taskman, type, species, maxLen, partyIdx, defaultStr, retVar)
NAMING_DELETE_ARGS = 0x0208311C    # NamingScreen_DeleteArgs(args): the typed text is still in args here
BEGIN_FADE = 0x0200FA24            # BeginNormalPaletteFade(mode, typeMain, typeSub, color, steps, framesPerStep, heap)
FADE_IN_FROM_BLACK = [0, 1, 1, 0, 6, 1, 4]   # both screens, brightness in, black, as scripts do after NicknameInput
NAME_SCREEN_FRIEND = 7             # prompt "Your friend's name?"; this type saves nothing
NAMING_FLAT = 0x1C                 # NamingScreenArgs.nameInputFlat (u16[20], 0xFFFF-terminated)
KEYINPUT, KEY_R, KEY_L = 0x04000130, 1 << 8, 1 << 9   # DS buttons, 0 = pressed
# Talking to another player: the game's own script engine runs our menu script with the Union Room's text
CREATE_SCRIPT_CTX = 0x020400AC     # CreateScriptContext(fs, script) -> ScriptContext *
SCRIPT_CTX_READY = 0x0203FD68      # sub_0203FD68(ctx, taskman): context loaded, no command run yet
DESTROY_SCRIPT_CTX = 0x02040044    # DestroyScriptContext(ctx): script ended, its ScriptEnvironment still alive
START_SCENE_SCRIPT = 0x0203FE74    # StartMapSceneScript(fs, script, lastInteracted)
RUN_SCRIPT_CMD = 0x0203FD6C        # RunScriptCommand(ctx): first call comes right after the text buffers exist
GET_PROFILE = 0x02028E9C           # Save_PlayerData_GetProfile(save) -> PlayerProfile (trainer name: u16[8] at +0)
FS_SAVE = 0x0C                     # FieldSystem.saveData
ENV_MSGFMT, MSGFMT_FIELDS = 0x44, 0x08   # ScriptEnvironment.msgfmt, MessageFormat.fields
UNION_TEXT = 738
# Union Room text (msg_0738): the question the other player gets, what they see after YES / NO, what the asker sees
# after their YES / NO ("Whoops! Sorry, but I have to do something else.")
REQUEST_TEXT = {"battle": (21, 22, 23, 22, 201), "trade pokemon": (26, 27, 28, 27, 201),
                "trade item": (26, 27, 28, 27, 201)}
# Battles and trades go through the game's own link rooms: warp both players to the desk, the attendant's menu opens,
# back home after leaving the room. Trades: the Union Room (talk to each other there, TRADE).
# shortcut: always Cherrygrove's Pokemon Center; a nearer one (same layout) when players are elsewhere
COLOSSEUM_DESK = (69, 11, 5, 0)    # MAP_CHERRYGROVE_POKECENTER_1F, in front of std_wireless_colosseum at (11, 4), facing up
UNION_DESK = (69, 6, 5, 0)         # same map, in front of std_union_room at (6, 4), facing up
LINK_MAPS = {2, 4, 5}              # MAP_UNION, MAP_WIFI_SINGLE_BATTLE_AREA, MAP_WIFI_MULTI_BATTLE_AREA: the game shows
                                   # the linked player itself, so no puppets there
LOAD_SCRIPT_BANKS = 0x02040168     # LoadScriptsAndMessagesParameterized(fs, ctx, scriptBank, msgBank): we swap msgBank
MO_SCRIPT = 0x20                   # LocalMapObject.scriptId
PUPPET_SCRIPT = 2000               # _std_misc: loads the large scr_seq_0003 (room for our bytecode); marks a puppet talk
MAN_OBJECTS, MO_SIZE = 0x124, 0x12C   # MapObjectManager.objects (objectCount at +4), sizeof(LocalMapObject)
STD_COLOSSEUM = 9001               # std_wireless_colosseum: the Colosseum attendant's own script
STD_UNION_ROOM = 9003              # std_union_room: the Union Room attendant's own script
# shortcut: Gen 4 has no item trades; ITEM goes to the Union Room too (a traded Pokemon keeps its held item)
LINK_DESKS = {"battle": (COLOSSEUM_DESK, STD_COLOSSEUM, "the Colosseum"),
              "trade pokemon": (UNION_DESK, STD_UNION_ROOM, "the Union Room"),
              "trade item": (UNION_DESK, STD_UNION_ROOM, "the Union Room")}
CTX_SCRIPT_PTR, CTX_MSGDATA, CTX_MAP_SCRIPTS = 0x08, 0x78, 0x7C           # ScriptContext fields
TASK_ENV, ENV_LAST_INTERACTED, ENV_RESULT = 0x0C, 0x2C, 0xA4              # TaskManager.env, ScriptEnvironment fields
# (text bank, prompt, [(message, choice)]): Union Room "Talking to ...": "BATTLE", "TRADE", "CANCEL";
# then GTS text "SELECT": "POKeMON", "ITEM", "CANCEL"
MENUS = {"main": (738, 199, [(192, "battle"), (193, "trade"), (198, None)]),
         "trade": (775, 69, [(61, "trade pokemon"), (73, "trade item"), (89, None)])}

def popup_script(msg, yesno=False):
    """NPCMsg msg, then YesNo VAR_SPECIAL_RESULT (0 = yes) or WaitButton, CloseMsg, ReleaseAll, End."""
    wait = struct.pack("<HH", 63, 0x800C) if yesno else struct.pack("<H", 50)
    return struct.pack("<HB", 45, msg) + wait + struct.pack("<HHH", 53, 97, 2)


def warp_script(map_id, x, z, facing):
    """FadeScreen 6,1,0,black; WaitFade; Warp map,0,x,z,facing; FadeScreen 6,1,1,black; WaitFade; End
    (as the game's own scripts warp, e.g. scr_seq_0012 _01D4)."""
    fade = lambda d: struct.pack("<HHHHHH", 174, 6, 1, d, 0, 175)
    return fade(0) + struct.pack("<HHHHHH", 176, map_id, 0, x, z, facing) + fade(1) + struct.pack("<H", 2)


def menu_script(prompt, items):
    """Script bytecode (asm/macros/script.inc), as the game's own menus do it (scr_seq_0003_023): NPCMsg prompt,
    TouchscreenMenuHide, MenuInit 1,1,0,1,VAR_SPECIAL_RESULT, MenuItemAdd per item, MenuExec, TouchscreenMenuShow,
    CloseMsg, ReleaseAll, End. Text comes from the script's own message bank. Without the message or the hidden
    touchscreen menu the menu never shows and takes no input. No LockAll: it waits for every object to stop, and a
    puppet paused mid-step never does (the game froze)."""
    b = struct.pack("<HBH", 45, prompt, 746) + struct.pack("<HBBBBH", 750, 1, 1, 0, 1, 0x800C)
    for i, (msg, _) in enumerate(items):
        b += struct.pack("<HHHH", 751, msg, 255, i)
    return b + struct.pack("<HHHHH", 752, 747, 53, 97, 2)

def encode(text):
    """ASCII digits/letters -> the game's character codes (tools/msgenc/charmap.txt), 0xFFFF-terminated."""
    out = []
    for c in text:
        out.append(0x121 + ord(c) - 48 if c.isdigit() else 0x12B + ord(c.upper()) - 65)
    return struct.pack("<%dH" % (len(out) + 1), *out, 0xFFFF)

def decode(codes):
    out = ""
    for v in codes:
        if v == 0xFFFF:
            break
        out += chr(48 + v - 0x121) if 0x121 <= v <= 0x12A else chr(65 + v - 0x12B) if 0x12B <= v <= 0x144             else chr(65 + v - 0x145) if 0x145 <= v <= 0x15E else "?"
    return out

# melonDS GDB register list: r0-r15, cpsr, then sp/lr per mode (src/debug/GdbArch.h). CPU mode -> index of its sp.
BANKED_SP_LR = {0x10: 17, 0x1F: 17, 0x11: 24, 0x12: 26, 0x13: 28, 0x17: 30, 0x1B: 32}

def ram(p):
    return 0x02000000 <= p < 0x02400000


class Gdb:
    """Minimal GDB remote-protocol client for melonDS's stub (ack mode, ARM9)."""

    def __init__(self, port, host="127.0.0.1"):
        self.s = socket.create_connection((host, port), timeout=5)
        self.s.settimeout(None)       # melonDS answers only while emulating: a paused emulator just makes us wait
        self.s.sendall(b"+")          # melonDS waits for this first ack (GdbStub::Poll)
        self.buf, self.replies, self.stops, self.handlers, self.halted = b"", [], [], {}, False
        self.send("c")                # a new connection halts the CPU (GdbStub::Enter); let the game run

    def _byte(self):
        if not self.buf:
            self.buf = self.s.recv(65536)
            if not self.buf:
                raise ConnectionError("melonDS closed the GDB connection")
        b, self.buf = self.buf[:1], self.buf[1:]
        return b

    def _packet(self):
        """Read one packet after its '$', ack it, file it as a stop report (T/S/W/X) or a command reply.
        Stop reports come whenever the CPU stops, also in the middle of another exchange."""
        data = b""
        while (c := self._byte()) != b"#":
            data += c
        self._byte(); self._byte()  # checksum
        self.s.sendall(b"+")        # melonDS waits for this ack before it does anything else
        d = data.decode()
        (self.stops if d and d[0] in "TSWX" else self.replies).append(d)

    def send(self, payload):
        p = payload.encode() if isinstance(payload, str) else payload
        self.s.sendall(b"$" + p + b"#%02x" % (sum(p) & 0xFF))
        while (b := self._byte()) != b"+":
            if b == b"$":
                self._packet()

    def _wait(self, queue, timeout):
        self.s.settimeout(timeout)
        try:
            while not queue:
                if self._byte() == b"$":
                    self._packet()
        finally:
            self.s.settimeout(None)
        return queue.pop(0)

    def recv(self, timeout=None):
        """The reply to the last command."""
        return self._wait(self.replies, timeout)

    def stop(self, timeout=None):
        """Wait until the CPU stops (breakpoint or halt)."""
        return self._wait(self.stops, timeout)

    def cmd(self, payload):
        self.send(payload)
        return self.recv()

    def halt(self):
        """Stop the CPU. A breakpoint we handle that it was already stopped at gets handled first."""
        self.stops.clear()
        self.s.sendall(b"\x03")
        self.stop()
        self.halted = True
        r = self.regs()
        h = self.handlers.get(r[15])
        if h:
            h(r)

    def resume(self):
        self.stops.clear()            # anything queued is from before this stop
        self.halted = False
        self.send("c")                # no reply until the next stop

    def poll(self):
        """Between commands: handle breakpoints the CPU stopped at by itself (see `handlers`), then let it run."""
        self.s.setblocking(False)
        try:
            while True:
                if not self.buf:
                    try:
                        self.buf = self.s.recv(65536)
                    except (BlockingIOError, socket.timeout):
                        break
                    if not self.buf:
                        raise ConnectionError("melonDS closed the GDB connection")
                b, self.buf = self.buf[:1], self.buf[1:]
                if b == b"$":
                    self.s.settimeout(None)
                    self._packet()
                    self.s.setblocking(False)
        finally:
            self.s.settimeout(None)
        if self.stops:
            self.stops.clear()
            r = self.regs()
            h = self.handlers.get(r[15])
            if h:
                h(r)
            self.resume()

    def set_handler(self, addr, fn):
        """Keep a breakpoint at `addr`; fn(registers) runs with the CPU stopped there. Only while stopped: when
        melonDS sends a stop report it reads the next bytes as its ack, eating any command in flight, so whoever
        arms a breakpoint must talk to the stub only while the CPU is stopped (see loop)."""
        if self.cmd("Z0,%x,2" % addr) != "OK":
            raise IOError("breakpoint refused (is the JIT off?)")
        self.handlers[addr] = fn

    def clear_handler(self, addr):
        """Only while the CPU is stopped."""
        if self.handlers.pop(addr, None):
            self.cmd("z0,%x,2" % addr)

    def run_until(self, addr, timeout=None):
        """Let the CPU run until it stops at `addr` (a breakpoint must be set there), handling our other
        breakpoints on the way. Returns the registers there, or None (timeout or another stop); then it is stopped."""
        while True:
            self.resume()
            try:
                self.stop(timeout)
            except socket.timeout:
                self.stops.clear()
                self.s.sendall(b"\x03")
                self.stop()
            self.halted = True
            r = self.regs()
            if r[15] == addr:
                return r
            h = self.handlers.get(r[15])
            if not h:
                return None
            h(r)

    def stop_at(self, addr, timeout=None):
        """Stopped now: run to `addr` and stay stopped there."""
        if self.cmd("Z0,%x,2" % addr) != "OK":
            raise IOError("breakpoint refused")
        try:
            return self.run_until(addr, timeout)
        finally:
            self.cmd("z0,%x,2" % addr)

    def inject(self, steps, data=b""):
        """Stopped at a breakpoint: call Thumb game functions there, one after another, returning to the same
        breakpoint, then put every register back. `steps` is [(func, args)], args may be a function (previous r0,
        address of `data`) -> args; `data` (up to 32 bytes) goes to scratch stack memory. Returns the last r0."""
        saved = self.regs()
        at = saved[15]
        sp = (saved[13] - 0x40) & ~7      # [sp, sp+0x20): stack arguments, [sp+0x20, sp+0x40): data
        changed = False
        try:
            if data:
                self.write(sp + 0x20, data)
            ret = None
            for func, args in steps:
                a = args(ret, sp + 0x20) if callable(args) else args
                if len(a) > 4:
                    self.write(sp, b"".join(struct.pack("<I", v) for v in a[4:]))
                new = list(saved)
                new[0:len(a[:4])] = a[:4]
                new[13], new[14], new[15] = sp, at | 1, func | 1
                bank = BANKED_SP_LR.get(saved[16] & 0x1F)
                if bank:   # 'G' also writes the banked copy of the current mode, which would undo sp/lr
                    new[bank], new[bank + 1] = new[13], new[14]
                changed = True
                self.set_regs(new)
                r = self.run_until(at, 3.0)
                if r is None:           # never came back: say where it is stuck, leave it stopped there
                    changed = False
                    r = self.regs()
                    raise IOError("call %08x did not return: pc %08x lr %08x sp %08x cpsr %08x (entry sp %08x)"
                                  % (func, r[15], r[14], r[13], r[16], saved[13]))
                ret = r[0]
            return ret
        finally:
            if changed:
                saved[15] |= 1          # Thumb: JumpTo() switches mode by bit 0
                self.set_regs(saved)

    def read(self, addr, n):
        out = b""
        while n > 0:
            k = min(n, 0x200)
            r = self.cmd("m%x,%x" % (addr, k))
            if r.startswith("E"):
                raise IOError("read %08x: %s" % (addr, r))
            out += bytes.fromhex(r); addr += k; n -= k
        return out

    def write(self, addr, data):
        r = self.cmd("M%x,%x:%s" % (addr, len(data), data.hex()))
        if r != "OK":
            raise IOError("write %08x: %s" % (addr, r))

    def u32(self, addr):
        return struct.unpack("<I", self.read(addr, 4))[0]

    def regs(self):
        r = self.cmd("g")
        return [struct.unpack("<I", bytes.fromhex(r[i:i + 8]))[0] for i in range(0, len(r), 8)]

    def set_reg(self, n, v):
        if self.cmd("P%x=%s" % (n, struct.pack("<I", v & 0xFFFFFFFF).hex())) != "OK":
            raise IOError("P%x refused" % n)

    def set_regs(self, regs):
        r = self.cmd("G" + "".join(struct.pack("<I", v & 0xFFFFFFFF).hex() for v in regs))
        if r != "OK":
            raise IOError("G: " + r)

    def call(self, func, args, trap=FIELD_CONTROL, timeout=2.0):
        """Call one Thumb game function on the game's own thread; returns r0 (see call_chain)."""
        return self.call_chain([(func, args)], b"", trap, timeout)

    def call_chain(self, steps, data=b"", trap=FIELD_CONTROL, timeout=2.0):
        """Run to `trap` (a function entry that is safe to run game code at), call the functions there (see
        `inject`) and let the game go on as if nothing happened. None when the game never reached `trap`.
        Called while stopped, it leaves the CPU stopped (at `trap`, registers as they were)."""
        was = self.halted
        self.halt()                   # set the breakpoint while stopped: no race with the CPU reaching it
        try:
            if self.cmd("Z0,%x,2" % trap) != "OK":
                raise IOError("breakpoint refused (is the JIT off?)")
            if self.run_until(trap, timeout) is None:
                return None
            return self.inject(steps, data)
        finally:
            self.cmd("z0,%x,2" % trap)
            if not was:
                self.resume()

    def wait_for(self, addr):
        """Run until the game reaches `addr` (blocks), return the registers there, then let it run on."""
        was = self.halted
        self.halt()
        try:
            return self.stop_at(addr)
        finally:
            if not was:
                self.resume()


class Menus:
    """Our scripts in the game's own script engine: the talk menu, its TRADE submenu, the YES/NO question a request
    shows the other player, and notices. The game starts script PUPPET_SCRIPT (talking to a puppet, or `pump` starts
    it); we swap in our bytecode and text bank, put the other player's trainer name in text buffer 0 (the
    "{STRVAR_1 3, 0, 0}" in the Union Room lines), and read the result when the script ends. Results end up in `todo`
    for the main loop."""

    def __init__(self, game, who):
        self.game, self.g, self.who = game, game.g, who   # who(obj) -> (player name, trainer name) of a puppet
        self.queue, self.spec, self.ctx, self.cur, self.obj, self.todo = [], None, None, None, None, None

    @staticmethod
    def menu(kind, tname):
        bank, prompt, items = MENUS[kind]
        return {"kind": kind, "bank": bank, "script": menu_script(prompt, items), "items": items, "name": tname}

    @staticmethod
    def warp(map_id, x, z, facing):
        return {"kind": "warp", "bank": UNION_TEXT, "script": warp_script(map_id, x, z, facing), "name": None}

    @staticmethod
    def talk(script_id):
        """The game's own script of the object that has it, as if the player talked to that object."""
        return {"kind": "std", "run": script_id}

    @staticmethod
    def popup(msg, tname, yesno=False, **extra):
        return dict(kind="ask" if yesno else "notice", bank=UNION_TEXT, script=popup_script(msg, yesno), name=tname,
                    **extra)

    def where(self):
        """Debug: how far into our script the game is (byte offset), while one is up."""
        return self.g.u32(self.ctx + CTX_SCRIPT_PTR) - self.g.u32(self.ctx + CTX_MAP_SCRIPTS)

    def busy(self):
        return self.ctx is not None or self.spec is not None or bool(self.queue)

    def show(self, spec, obj):
        self.queue.append((spec, obj))

    def pump(self):
        """While the talk breakpoint is armed: start the next queued script, as if the player talked to `obj`."""
        if not self.queue or self.ctx is not None or self.spec is not None:
            return
        spec, obj = self.queue[0]
        fs = self.g.u32(FIELD_SYS_PTR)
        if self.g.u32(fs + FS_TASKMAN):
            return       # one field task at a time: the last script's task still finishing would drop ours
        if spec["kind"] == "std":      # the game's script, unchanged: nothing to swap in
            obj = self.game.find_object(spec["run"])
            if not obj:
                print("no object with script %d here" % spec["run"], flush=True)
            elif self.g.call(START_SCENE_SCRIPT, [fs, spec["run"], obj]) is None:
                return
            self.queue.pop(0)
            return
        self.spec = spec
        if self.g.call(START_SCENE_SCRIPT, [fs, PUPPET_SCRIPT, obj]) is None:
            self.spec = None          # not in the overworld right now: try again next tick
        else:
            self.queue.pop(0)

    def _env(self):
        task = self.g.u32(self.g.u32(FIELD_SYS_PTR) + FS_TASKMAN)
        return self.g.u32(task + TASK_ENV)

    def _created(self, r):
        if r[1] != PUPPET_SCRIPT:
            return
        obj = self.g.u32(self._env() + ENV_LAST_INTERACTED)
        spec, self.spec = self.spec or self.menu("main", self.who(obj)[1]), None
        self.g.stop_at(LOAD_SCRIPT_BANKS)
        self.g.set_reg(3, spec["bank"])   # the game loads our text bank itself (loading it ourselves aborted)
        ctx = self.g.stop_at(SCRIPT_CTX_READY)[0]
        scripts = self.g.u32(ctx + CTX_MAP_SCRIPTS)    # the loaded bank: freed by the game when the script ends
        self.g.write(scripts, spec["script"])
        self.g.write(ctx + CTX_SCRIPT_PTR, struct.pack("<I", scripts))
        if spec.get("name"):
            self.g.stop_at(RUN_SCRIPT_CMD)            # the text buffers exist from the first command on
            fmt = self.g.u32(self._env() + ENV_MSGFMT)
            s = self.g.u32(self.g.u32(fmt + MSGFMT_FIELDS) + 4)       # fields[0].msg: a String
            name = spec["name"][:struct.unpack("<H", self.g.read(s, 2))[0] - 1]
            self.g.write(s + 2, struct.pack("<H", len(name)))
            self.g.write(s + 8, struct.pack("<%dH" % (len(name) + 1), *name, 0xFFFF))
        self.ctx, self.cur, self.obj, self.since = ctx, spec, obj, time.time()
        self.g.cmd("Z0,%x,2" % DESTROY_SCRIPT_CTX)      # stopped: set it directly
        self.g.handlers[DESTROY_SCRIPT_CTX] = self._ended

    def _ended(self, r):
        if r[0] != self.ctx:
            return
        value = struct.unpack("<H", self.g.read(self._env() + ENV_RESULT, 2))[0]
        self.g.clear_handler(DESTROY_SCRIPT_CTX)
        self.todo, self.ctx = (self.cur, value, self.obj), None


class Game:
    def __init__(self, gdb):
        self.g = gdb

    def player(self):
        """Local player's state, or None outside the overworld."""
        fs = self.g.u32(FIELD_SYS_PTR)
        if not ram(fs):
            return None
        loc, man, avatar = struct.unpack("<III", self.g.read(fs + FS_LOCATION, 4) + self.g.read(fs + FS_MAPOBJ_MAN, 8))
        if not (ram(loc) and ram(man) and ram(avatar)):
            return None
        obj = self.g.u32(avatar + AVATAR_MAPOBJ)
        if not ram(obj):
            return None
        o = self.g.read(obj, MO_MOVE_CMD + 4)
        return {
            "cmd": struct.unpack_from("<I", o, MO_MOVE_CMD)[0],
            "map": self.g.u32(loc),
            "sprite": struct.unpack_from("<I", o, MO_SPRITE)[0],
            "dir": struct.unpack_from("<I", o, MO_FACING)[0],
            "tile": list(struct.unpack_from("<III", o, MO_CUR_XYZ)),
            "pos": list(struct.unpack_from("<iii", o, MO_POS)),
            "mon": self.follower(fs),
            "_man": man,
        }

    def follower(self, fs):
        """The Pokemon walking behind the player (HeartGold's followers), or None."""
        f = self.g.read(fs + FS_FOLLOW_MON, 0x20)
        obj = struct.unpack_from("<I", f, 0)[0]
        if not (f[FOLLOW_ACTIVE] and ram(obj)):
            return None
        o = self.g.read(obj, MO_MOVE_CMD + 4)
        if not struct.unpack_from("<I", o, MO_FLAGS)[0] & MO_ACTIVE:
            return None
        return {"sprite": struct.unpack_from("<I", o, MO_SPRITE)[0],
                "dir": struct.unpack_from("<I", o, MO_FACING)[0],
                "tile": list(struct.unpack_from("<III", o, MO_CUR_XYZ)),
                "pos": list(struct.unpack_from("<iii", o, MO_POS)),
                "step": struct.unpack_from("<I", o, MO_MOVE_CMD)[0],
                "params": list(struct.unpack_from("<3i", o, MO_PARAMS))}

    def trainer_name(self):
        """The player's own trainer name, in the game's character codes."""
        save = self.g.u32(self.g.u32(FIELD_SYS_PTR) + FS_SAVE)
        profile = self.g.call(GET_PROFILE, [save])
        if profile is None:
            return None
        name = list(struct.unpack("<8H", self.g.read(profile, 16)))
        return name[:name.index(0xFFFF)] if 0xFFFF in name else name

    def npcs(self, man, slots=None):
        """The map's own objects (NPCs; not the player, followers or our puppets): {id: (address, state)}. With
        `slots` (addresses from an earlier call) only those are read; else all 64 slots (38 GDB packets)."""
        out = {}
        if slots is None:
            base = self.g.u32(man + MAN_OBJECTS)
            raw = self.g.read(base, min(self.g.u32(man + 4), 64) * MO_SIZE)
            objs = [(base + i, raw[i:i + MO_SIZE]) for i in range(0, len(raw), MO_SIZE)]
        else:
            objs = [(a, self.g.read(a, MO_MOVE_CMD + 4)) for a in slots]
        for a, o in objs:
            flags, _, oid = struct.unpack_from("<III", o, 0)
            if flags & MO_ACTIVE and oid < PUPPET_ID_BASE:     # the player is 255, a follower 253
                out[oid] = (a, {"tile": list(struct.unpack_from("<III", o, MO_CUR_XYZ)),
                                "pos": list(struct.unpack_from("<iii", o, MO_POS)),
                                "dir": struct.unpack_from("<I", o, MO_FACING)[0],
                                "step": struct.unpack_from("<I", o, MO_MOVE_CMD)[0], "flags": flags})
        return out

    def set_paused(self, obj, on):
        flags = self.g.u32(obj)     # called while the CPU is stopped: no race with the game
        self.g.write(obj, struct.pack("<I", flags | MO_PAUSED if on else flags & ~MO_PAUSED))

    def busy(self):
        """A field task (script, warp, menu) is running."""
        return self.g.u32(self.g.u32(FIELD_SYS_PTR) + FS_TASKMAN) != 0

    def find_object(self, script_id):
        """The first active map object on the current map whose script is script_id, or None."""
        man = self.g.u32(self.g.u32(FIELD_SYS_PTR) + FS_MAPOBJ_MAN)
        count, base = self.g.u32(man + 4), self.g.u32(man + MAN_OBJECTS)
        for i in range(min(count, 128)):
            obj = base + i * MO_SIZE
            if struct.unpack("<I", self.g.read(obj, 4))[0] & MO_ACTIVE and self.g.u32(obj + MO_SCRIPT) == script_id:
                return obj
        return None

    def puppet_ok(self, obj, pid, map_id):
        if not ram(obj):
            return False
        flags, _, oid, omap = struct.unpack("<IIII", self.g.read(obj, 16))
        return bool(flags & MO_ACTIVE) and oid == pid and omap == map_id

    def spawn(self, man, peer, pid, map_id):
        x, _, z = peer["tile"]
        obj = self.g.call(MAPOBJ_CREATE, [man, x, z, peer["dir"], peer["sprite"], MOVEMENT_STILL, map_id])
        if obj and ram(obj):
            self.g.write(obj + MO_ID, struct.pack("<I", pid))
            self.g.write(obj + MO_SCRIPT, struct.pack("<I", PUPPET_SCRIPT))   # talking to it starts our menu
            if "params" in peer:   # a follower: species, form, shiny (shortcut: set after the sprite may have loaded)
                self.g.write(obj + MO_PARAMS, struct.pack("<3i", *peer["params"]))
            return obj
        return None

    def remove(self, obj):
        self.g.call(MAPOBJ_REMOVE, [obj])

    def follow(self, obj, peer):
        """Walk the puppet after the other player with the game's own step animations; teleport if far behind."""
        o = self.g.read(obj, MO_POS)
        flags, facing = struct.unpack_from("<I", o, 0)[0], struct.unpack_from("<I", o, MO_FACING)[0]
        if flags & MO_HELD and not flags & MO_HELD_DONE:
            return                                  # still taking the last step
        x, _, z = struct.unpack_from("<III", o, MO_CUR_XYZ)
        dx, dz = peer["tile"][0] - x, peer["tile"][2] - z
        if dx == dz == 0:
            if facing != peer["dir"]:
                self.g.write(obj + MO_FACING, struct.pack("<II", peer["dir"], peer["dir"]))
            return
        if abs(dx) + abs(dz) > 3:
            self.place(obj, peer)
            return
        d = (3 if dx > 0 else 2) if dx else (1 if dz > 0 else 0)
        step = peer.get("step", STEP_WALK)          # the other player's own last step command (walk, run, bike...)
        cmd = step if STEP_WALK <= step < STEP_MAX and step % 4 == d else STEP_WALK + d
        if abs(dx) + abs(dz) > 1 and cmd < STEP_RUN:
            cmd = STEP_RUN + d                      # behind: run to catch up
        self.g.write(obj + MO_MOVE_CMD, struct.pack("<II", cmd, 0))
        # shortcut: read-modify-write of flags across two GDB packets; fine while only we touch the puppet's flags
        self.g.write(obj, struct.pack("<I", (flags | MO_HELD) & ~MO_HELD_DONE))

    def ask_code(self, shown):
        """Open the game's keyboard with `shown` filled in; returns what the player typed (upper case)."""
        fs = self.g.u32(FIELD_SYS_PTR)
        ok = self.g.call_chain([(CREATE_TASK, [fs, TASK_DONE | 1, 0]),
                                (CALLTASK_NAMING, lambda task, text: [task, NAME_SCREEN_FRIEND, 0, 6, 0, text, 0])],
                               encode(shown))
        if ok is None:
            return None
        args = self.g.wait_for(NAMING_DELETE_ARGS)[0]     # blocks while the player types
        typed = decode(struct.unpack("<20H", self.g.read(args + NAMING_FLAT, 40)))
        for _ in range(5):            # the overworld comes back black: fade it in once the player can move again
            if self.g.call(BEGIN_FADE, FADE_IN_FROM_BLACK) is not None:
                break
        return typed

    def keys_held(self, mask):
        return (~struct.unpack("<H", self.g.read(KEYINPUT, 2))[0]) & mask == mask

    def place(self, obj, peer):
        tile = struct.pack("<III", *peer["tile"])
        self.g.write(obj + MO_PREV_XYZ, tile + tile + struct.pack("<iii", *peer["pos"]))
        self.g.write(obj + MO_FACING, struct.pack("<II", peer["dir"], peer["dir"]))


CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"   # uscl-bridge session codes (tunnel.rs session_code)


class Session:
    """The player's Steam session through uscl-bridge: host our own (bridge preset `hgss` + the relay) until the
    player types a friend's code, then join theirs."""

    def __init__(self, bridge, relay_port, name):
        self.bridge, self.relay_port, self.name = bridge, relay_port, name
        self.code, self.procs, self.role = None, [], None   # role: "host" / "join" once the bridge says so
        self.host()

    def _start(self, *argv):
        p = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        threading.Thread(target=self._read, args=(p,), daemon=True).start()
        self.procs.append(p)

    def _read(self, p):
        for line in p.stdout:
            print("[%s] %s" % (self.name, line.rstrip()), flush=True)
            m = re.search(r"code ([A-Z0-9]{6}) \((hosting|joined)\)", line)
            if m:
                self.code, self.role = m.group(1), "host" if m.group(2) == "hosting" else "join"

    def stop(self):
        for p in self.procs:
            p.terminate()
            p.wait(10)
        self.procs, self.code, self.role = [], None, None

    def host(self):
        self.stop()
        self._start(sys.executable, "-I", os.path.abspath(__file__), "--relay", str(self.relay_port))
        self._start(self.bridge, "host", "hgss")

    def join(self, code):
        self.stop()   # the relay and our lobby go; the bridge now carries 127.0.0.1:<relay port> to the friend's host
        self._start(self.bridge, "join", code)


def run(args):
    game = Game(Gdb(args.gdb))
    f0 = game.g.u32(0x027FFC3C)      # NitroSDK VBlank counter; the stub answers at one point per frame, so not VCOUNT
    time.sleep(0.5)
    frames = game.g.u32(0x027FFC3C) - f0
    print("[%s] connected to melonDS on %d, game %s (%d frames in 0.5 s)" % (args.name, args.gdb, "running" if frames else "HALTED", frames), flush=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    h, p = args.listen.rsplit(":", 1)
    sock.bind((h, int(p)))
    sock.setblocking(False)
    peers = [(a.rsplit(":", 1)[0], int(a.rsplit(":", 1)[1])) for a in args.peer]
    remote = {}     # name -> (state, time received)
    puppets = {}    # name -> (object address, puppet id, sprite)
    seq, last_line, step = 0, None, STEP_WALK
    session = Session(args.bridge, int(args.peer[0].rsplit(":", 1)[1]), args.name) if args.bridge else None
    try:
        loop(args, game, sock, peers, remote, puppets, session)
    finally:
        if session:
            session.stop()


def facing(me, remote, puppets):
    """Is the player facing a shown puppet on the next tile?"""
    x, _, z = me["tile"]
    dx, dz = {0: (0, -1), 1: (0, 1), 2: (-1, 0), 3: (1, 0)}.get(me["dir"], (0, 0))
    return any(remote[n][0]["tile"][0] == x + dx and remote[n][0]["tile"][2] == z + dz for n in puppets if n in remote)


def loop(args, game, sock, peers, remote, puppets, session):
    def send(fields):
        data = json.dumps(dict({"v": 1, "name": args.name}, **fields)).encode()
        for a in peers:
            sock.sendto(data, a)

    seq, last_line, step = 0, None, STEP_WALK
    lr_since, lr_released = None, True
    def who(obj):
        n = next((n for n, (o, _, _) in puppets.items() if o == obj), None)
        if n and n.endswith(MON_SUFFIX):
            n = n[:-len(MON_SUFFIX)]       # talking to someone's Pokemon talks to them
        return n, remote[n][0].get("tname") if n in remote else None
    menus = Menus(game, who)
    tname = None
    quiet_until, last_map = 0.0, None   # no puppet creation while our own map is being torn down / rebuilt
    trip, last_me = None, None          # link trip: {"home": (map, x, z, facing), "fought": bool (been in the room)}

    def start_trip(req):
        nonlocal trip
        desk, script, room = LINK_DESKS[req]
        if last_me and not (trip and trip["fought"]):
            if not trip:              # asked again before entering: same home, warp and attendant again
                x, _, z = last_me["tile"]
                trip = {"home": (last_me["map"], x, z, last_me["dir"]), "fought": False}
            menus.show(menus.warp(*desk), 0)
            menus.show(menus.talk(script), 0)      # her menu opens by itself after the fade
            print("[%s] %s: to %s desk, attendant's menu next" % (args.name, req, room), flush=True)
    link = None        # the wireless link role melonDS was last told (patched melonDS: monitor uscl-link)
    ticks, rate_since = 0, time.time()
    # NPCs: on each map the player whose name sorts first moves them for everyone (lead); the others copy
    npc_cache, npc_scanned, npc_out, npc_from, paused = {}, 0.0, None, {}, {}
    if args.warp:      # one-off: move the player (e.g. out from behind a Pokemon Center counter)
        menus.show(menus.warp(*args.warp), 0)

    while True:
        t0 = time.time()
        me = None
        want = session.role if session else args.link
        if want and want != link:
            link = want
            ok = game.g.cmd("qRcmd," + ("uscl-link " + want).encode().hex()) == "OK"
            print("[%s] wireless link: %s" % (args.name, want if ok else "needs the USCL melonDS build (uscl-melonds/patches)"), flush=True)
        if args.stop and os.path.exists(args.stop):
            # breakpoints outlive us in melonDS: with no debugger left, the game would stop at one for good
            if game.g.handlers:
                game.g.halt()
                for a in list(game.g.handlers):
                    game.g.clear_handler(a)
                game.g.resume()
            print("[%s] stopped cleanly" % args.name, flush=True)
            return
        # one stop per tick: a running melonDS answers one GDB packet per frame (a tick is ~15 packets, ~4 ticks/s:
        # choppy puppets), a stopped one answers at once; also, with a breakpoint armed, talk only while stopped
        game.g.halt()
        ticks += 1
        if time.time() - rate_since > 30:
            print("[%s] %.1f ticks/s" % (args.name, ticks / (time.time() - rate_since)), flush=True)
            ticks, rate_since = 0, time.time()
        try:
            if menus.todo:
                spec, value, obj = menus.todo
                menus.todo = None
                name, their = who(obj)
                if spec["kind"] in MENUS:
                    items = spec["items"]
                    choice = items[value][1] if value < len(items) else None   # B cancels
                    print("[%s] menu %s with %s: %s" % (args.name, spec["kind"], name, choice or "cancelled"), flush=True)
                    if choice == "trade":
                        menus.show(menus.menu("trade", their), obj)
                    elif choice and name:
                        send({"to": name, "req": choice})
                elif spec["kind"] == "ask":
                    yes = value == 0
                    print("[%s] %s %s's %s" % (args.name, "accepted" if yes else "declined", spec["who"], spec["req"]), flush=True)
                    send({"to": spec["who"], "ans": spec["req"], "ok": yes})
                    if yes:
                        start_trip(spec["req"])
                    text = REQUEST_TEXT[spec["req"]]
                    menus.show(menus.popup(text[1] if yes else text[2], None), obj)
            if CREATE_SCRIPT_CTX in game.g.handlers:
                menus.pump()
            if menus.ctx and time.time() - menus.since > 3:
                menus.since = time.time()
                print("[%s] menu still open, script at byte %d" % (args.name, menus.where()), flush=True)
            me = game.player()
            if me:
                last_me = me
            if me and trip and not menus.busy():
                if me["map"] in LINK_MAPS:
                    trip["fought"] = True
                elif trip["fought"] and me["map"] == COLOSSEUM_DESK[0]:    # both desks are on this map
                    # the game's own return script runs first (walks out from behind the counter, clears flag 0x966).
                    # Warping during it leaves 0x966 set: Continue then puts the player behind the counter, stuck.
                    if game.busy():
                        trip["idle"] = None
                    elif not trip.get("idle"):
                        trip["idle"] = time.time()
                    elif time.time() - trip["idle"] > 1.5:
                        print("[%s] link room left: back home %s" % (args.name, trip["home"]), flush=True)
                        menus.show(menus.warp(*trip["home"]), 0)
                        trip = None
            if me and me["map"] != last_map:
                last_map, quiet_until = me["map"], time.time() + 2
                npc_cache, npc_scanned, paused = {}, 0.0, {}     # the old map's objects are gone
            if me and tname is None:
                tname = game.trainer_name()
            if me:
                seq += 1
                if STEP_WALK <= me["cmd"] < STEP_MAX:
                    step = me["cmd"]
                msg = json.dumps({"v": 1, "name": args.name, "seq": seq, "step": step, "tname": tname,
                                  **({"npcs": npc_out} if npc_out else {}),
                                  **{k: v for k, v in me.items() if not k.startswith("_")}}).encode()
                npc_out = None
                for a in peers:
                    sock.sendto(msg, a)
            while True:
                try:
                    data, _ = sock.recvfrom(65536)
                except (BlockingIOError, ConnectionResetError):
                    break   # Windows reports ICMP port-unreachable as ConnectionResetError
                try:
                    st = json.loads(data)
                    if st.get("v") == 1 and (st.get("req") or st.get("ans")):
                        if st.get("to") != args.name:
                            pass
                        elif st.get("req") in REQUEST_TEXT:
                            print("[%s] %s asks you: %s" % (args.name, st["name"], st["req"]), flush=True)
                            their = remote[st["name"]][0].get("tname") if st["name"] in remote else None
                            menus.show(menus.popup(REQUEST_TEXT[st["req"]][0], their, yesno=True, who=st["name"],
                                                   req=st["req"]), puppets.get(st["name"], (0,))[0])
                        elif st.get("ans") in REQUEST_TEXT:
                            ok = bool(st.get("ok"))
                            print("[%s] %s %s your %s" % (args.name, st["name"], "accepted" if ok else "declined", st["ans"]), flush=True)
                            text = REQUEST_TEXT[st["ans"]]
                            menus.show(menus.popup(text[3] if ok else text[4], None), puppets.get(st["name"], (0,))[0])
                            if ok:
                                start_trip(st["ans"])
                    elif st.get("v") == 1 and st.get("name") != args.name:
                        old = remote.get(st["name"])
                        same = old and (old[0]["map"], old[0]["sprite"]) == (st["map"], st["sprite"])
                        st["_since"] = old[0]["_since"] if same else time.time()   # map + sprite unchanged since
                        remote[st["name"]] = (st, time.time())
                        if st.get("npcs"):
                            npc_from[st["name"]] = (st.pop("npcs"), time.time())
                        mname, mon = st["name"] + MON_SUFFIX, st.get("mon")
                        old = remote.get(mname)
                        if mon:      # their follower: a puppet of its own, walking like a player
                            mon = dict(mon, map=st["map"], name=mname)
                            same = old and (old[0]["map"], old[0]["sprite"]) == (mon["map"], mon["sprite"])
                            mon["_since"] = old[0]["_since"] if same else time.time()
                            remote[mname] = (mon, time.time())
                        elif old:    # put away: map -1 removes its puppet
                            remote[mname] = (dict(old[0], map=-1), time.time())
                except (ValueError, KeyError):
                    pass
            if me:
                # hold L+R for a second: the game's keyboard, showing our session code; type a friend's code to join them
                if game.keys_held(KEY_L | KEY_R):
                    lr_since = lr_since or time.time()
                    if lr_released and time.time() - lr_since > 1:
                        lr_released = False
                        own = session.code if session and session.code else ""
                        typed = game.ask_code(own)
                        print("[%s] code box: %r (ours %r)" % (args.name, typed, own), flush=True)
                        if session and typed and typed != own and all(c in CODE_ALPHABET for c in typed) and len(typed) == 6:
                            print("[%s] joining %s" % (args.name, typed), flush=True)
                            if link:   # our hosted link holds 127.0.0.1:7064, which the joining bridge listens on
                                game.g.cmd("qRcmd," + b"uscl-link off".hex())
                                link = None
                            session.join(typed)
                        continue
                else:
                    lr_since, lr_released = None, True
                for i, (name, (st, ts)) in enumerate(sorted(remote.items())):
                    pid = PUPPET_ID_BASE + i
                    here = st["map"] == me["map"] and time.time() - ts < 2 and me["map"] not in LINK_MAPS
                    obj, _, sprite = puppets.get(name, (0, pid, None))
                    ok = game.puppet_ok(obj, pid, me["map"])
                    if ok and (not here or sprite != st["sprite"]):
                        game.remove(obj); ok = False
                    if not ok and name in puppets:
                        puppets.pop(name, None)
                        quiet_until = time.time() + 2   # it vanished by itself: our map is changing (a warp)
                    if here and (not args.arm or os.path.exists(args.arm)):
                        # only a settled state: right after a map change or menu the sprite can be in flux, and
                        # MapObject_Create during a warp crashes the game
                        if not ok and time.time() - st["_since"] > 1 and st["sprite"] < SPRITE_MAX                                 and time.time() > quiet_until:
                            print("[%s] creating %s: tile %s dir %d sprite %d" % (args.name, name, st["tile"], st["dir"], st["sprite"]), flush=True)
                            obj = game.spawn(me["_man"], st, pid, me["map"])
                            if obj:
                                puppets[name] = (obj, pid, st["sprite"])
                                print("[%s] %s appeared on map %d at %s (object %08x)" % (args.name, name, me["map"], st["tile"], obj), flush=True)
                        if obj and name in puppets:
                            game.follow(obj, st)
                here = [n for n, (s, ts) in remote.items()
                        if not n.endswith(MON_SUFFIX) and s["map"] == me["map"] and time.time() - ts < 2]
                lead = min(here + [args.name])
                theirs = npc_from.get(lead, (None, 0))
                follow_npcs = lead != args.name and theirs[0] and time.time() - theirs[1] < 2                     and theirs[0]["map"] == me["map"]
                if paused and not follow_npcs:      # we lead now, or the lead left: our NPCs move by themselves again
                    for a in paused.values():
                        game.set_paused(a, False)
                    paused = {}
                if ticks % NPC_EVERY == 0 and here and me["map"] not in LINK_MAPS and time.time() > quiet_until                         and not game.busy():
                    if time.time() - npc_scanned > 2:
                        npc_cache, npc_scanned = game.npcs(me["_man"]), time.time()
                    else:
                        npc_cache = game.npcs(me["_man"], [a for a, _ in npc_cache.values()])
                    if lead == args.name:
                        npc_out = {"map": me["map"], "objs": [[i, s["tile"], s["dir"], s["step"]]
                                                              for i, (_, s) in sorted(npc_cache.items())]}
                    elif follow_npcs:
                        for oid, tile, d, stp in theirs[0]["objs"]:
                            if oid not in npc_cache:
                                continue    # not on our map (an event flag hides it in our save)
                            a, own = npc_cache[oid]
                            if oid not in paused:
                                game.set_paused(a, True); paused[oid] = a
                            # their pixel position from the tile; height stays ours (same map)
                            pos = [tile[0] * 0x10000 + 0x8000, own["pos"][1], tile[2] * 0x10000 + 0x8000]
                            game.follow(a, {"tile": tile, "dir": d, "step": stp, "pos": pos})
                line = "me map %d tile %s | " % (me["map"], me["tile"]) + ", ".join(
                    "%s map %d tile %s%s" % (n, s["map"], s["tile"], " (shown)" if n in puppets else "")
                    for n, (s, _) in sorted(remote.items()))
                if line != last_line:
                    print("[%s] %s" % (args.name, line), flush=True)
                    last_line = line
        finally:
            # arm the talk breakpoint only while the player faces another player (or one of our menus is up)
            want = menus.busy() or (me is not None and facing(me, remote, puppets))
            armed = CREATE_SCRIPT_CTX in game.g.handlers
            if want != armed and not menus.ctx:
                if not game.g.halted:
                    game.g.halt()
                if want:
                    game.g.set_handler(CREATE_SCRIPT_CTX, menus._created)
                else:
                    game.g.clear_handler(CREATE_SCRIPT_CTX)
            if game.g.halted:
                game.g.resume()
        time.sleep(max(0.0, 1 / args.hz - (time.time() - t0)))


def relay(port, host="127.0.0.1"):
    """Host side: pass every player's state to all other players (each agent sends here; over Steam the bridge's
    `hgss` preset carries this port)."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    print("relay on %s:%d" % (host, port), flush=True)
    clients = {}    # address -> last time heard
    while True:
        try:
            data, src = sock.recvfrom(65536)
        except ConnectionResetError:
            continue    # Windows: an earlier send hit a closed port
        now = time.time()
        clients[src] = now
        for a, t in list(clients.items()):
            if now - t > 10:
                del clients[a]
            elif a != src:
                sock.sendto(data, a)


# ---------------------------------------------------------------------------------------------------------
def selftest():
    """Drive the agent against a fake melonDS GDB stub that holds a fake FieldSystem in memory."""
    mem = bytearray(0x400000)
    B = 0x02000000
    def w32(a, v): struct.pack_into("<I", mem, a - B, v)
    FS, LOC, MAN, AV, PLAYER, OBJS = 0x02100000, 0x02100100, 0x02100200, 0x02100300, 0x02100400, 0x02101000
    w32(FIELD_SYS_PTR, FS); w32(FS + FS_LOCATION, LOC); w32(FS + FS_MAPOBJ_MAN, MAN); w32(FS + FS_PLAYER, AV)
    w32(AV + AVATAR_MAPOBJ, PLAYER); w32(LOC, 412)
    w32(PLAYER + MO_SPRITE, 0); w32(PLAYER + MO_FACING, 1)
    struct.pack_into("<III", mem, PLAYER + MO_CUR_XYZ - B, 7, 0, 9)
    struct.pack_into("<iii", mem, PLAYER + MO_POS - B, 7 * 16 * 4096 + 8 * 4096, 0, 9 * 16 * 4096 + 8 * 4096)
    regs = [0] * 39; regs[13], regs[15], regs[16] = 0x023FF000, 0x02000100, 0x3F   # Thumb, SYS mode
    regs[17], regs[18] = regs[13], regs[14]                                         # sp_usr/lr_usr = sp/lr in SYS
    bps, calls, naming, shown, fades, conn, loaded = set(), [], [], [], [], [], []
    srv = socket.socket(); srv.bind(("127.0.0.1", 0)); srv.listen(1)

    def stub():
        c, _ = srv.accept()
        conn.append(c)
        assert c.recv(1) == b"+"
        buf = b""
        def pkt(p):
            c.sendall(b"$" + p.encode() + b"#%02x" % (sum(p.encode()) & 0xFF))
        while True:
            while (b"#" not in buf or len(buf) < buf.index(b"#") + 3) and b"\x03" not in buf.lstrip(b"+")[:1]:
                d = c.recv(4096)
                if not d:
                    return
                buf += d
            buf = buf.lstrip(b"+")
            if buf[:1] == b"\x03":
                buf = buf[1:]; pkt("S02"); continue
            s, e = buf.index(b"$"), buf.index(b"#")
            p, buf = buf[s + 1:e].decode(), buf[e + 3:]
            c.sendall(b"+")
            if p[0] == "m":
                a, n = (int(x, 16) for x in p[1:].split(",")); pkt(mem[a - B:a - B + n].hex())
            elif p[0] == "M":
                h, d = p[1:].split(":"); a, n = (int(x, 16) for x in h.split(","))
                mem[a - B:a - B + n] = bytes.fromhex(d); pkt("OK")
            elif p[0] == "g":
                pkt("".join(struct.pack("<I", v).hex() for v in regs))
            elif p[0] == "P":
                n, v = p[1:].split("="); regs[int(n, 16)] = struct.unpack("<I", bytes.fromhex(v))[0]; pkt("OK")
            elif p[0] == "G":
                regs[:] = [struct.unpack("<I", bytes.fromhex(p[1 + i:9 + i]))[0] for i in range(0, len(p) - 1, 8)]
                regs[15] &= ~1
                regs[13], regs[14] = regs[17], regs[18]   # like melonDS in SYS mode: sp_usr/lr_usr land in sp/lr
                pkt("OK")
            elif p[0] in "Zz":
                (bps.add if p[0] == "Z" else bps.discard)(int(p.split(",")[1], 16)); pkt("OK")
            elif p[0] == "c":
                if regs[15] == MAPOBJ_CREATE:          # "run" the injected function
                    sp = regs[13]
                    calls.append(regs[0:4] + list(struct.unpack_from("<III", mem, sp - B)))
                    obj = OBJS + 0x12C
                    struct.pack_into("<IIII", mem, obj - B, MO_ACTIVE, 0, 0, regs[0] and calls[-1][6])
                    regs[0], regs[15] = obj, regs[14] & ~1
                elif regs[15] == MAPOBJ_REMOVE:
                    struct.pack_into("<I", mem, regs[0] - B, 0); regs[15] = regs[14] & ~1
                elif regs[15] == CREATE_SCRIPT_CTX and LOAD_SCRIPT_BANKS in bps:  # misc bank 3, text 40
                    regs[0:4] = [FS, 0x02105000, 3, 40]; regs[15] = LOAD_SCRIPT_BANKS
                elif regs[15] == LOAD_SCRIPT_BANKS and SCRIPT_CTX_READY in bps:   # the context gets loaded
                    loaded.append(regs[3])
                    regs[0], regs[15] = 0x02105000, SCRIPT_CTX_READY
                elif regs[15] == SCRIPT_CTX_READY and RUN_SCRIPT_CMD in bps:      # first script command
                    regs[0], regs[15] = 0x02105000, RUN_SCRIPT_CMD
                elif regs[15] == BEGIN_FADE:
                    fades.append(regs[0:4] + list(struct.unpack_from("<III", mem, regs[13] - B)))
                    regs[15] = regs[14] & ~1
                elif regs[15] == CREATE_TASK:
                    assert regs[1] == TASK_DONE | 1
                    regs[0], regs[15] = 0x02102000, regs[14] & ~1
                elif regs[15] == CALLTASK_NAMING:
                    stack = list(struct.unpack_from("<III", mem, regs[13] - B))
                    shown.append(stack[1]); stack[1] = 0
                    naming.append(regs[0:4] + stack)
                    mem[0x02103000 + NAMING_FLAT - B:0x02103000 + NAMING_FLAT + 14 - B] = encode("hello2")
                    regs[15] = regs[14] & ~1
                elif NAMING_DELETE_ARGS in bps:           # the player finished typing
                    regs[0], regs[15] = 0x02103000, NAMING_DELETE_ARGS
                elif FIELD_CONTROL in bps:
                    regs[15] = FIELD_CONTROL
                else:
                    continue                            # running: no reply until a stop
                if regs[15] in bps:
                    pkt("T05swbreak:;")

    threading.Thread(target=stub, daemon=True).start()
    g = Gdb(srv.getsockname()[1]); game = Game(g)
    me = game.player()
    assert me["map"] == 412 and me["tile"] == [7, 0, 9] and me["dir"] == 1, me
    peer = dict(me, tile=[10, 0, 9], pos=[10 * 65536 + 32768, 0, 9 * 65536 + 32768], dir=2)
    obj = game.spawn(me["_man"], peer, PUPPET_ID_BASE, me["map"])
    assert calls == [[MAN, 10, 9, 2, 0, MOVEMENT_STILL, 412]], calls
    assert regs[15] == FIELD_CONTROL and regs[13] == 0x023FF000, "registers not restored"
    assert not bps, "breakpoint left behind"
    assert game.puppet_ok(obj, PUPPET_ID_BASE, 412)
    game.place(obj, peer)
    assert struct.unpack_from("<III", mem, obj + MO_CUR_XYZ - B) == (10, 0, 9)
    assert struct.unpack_from("<i", mem, obj + MO_POS - B)[0] == 10 * 65536 + 32768
    game.follow(obj, dict(peer, tile=[11, 0, 9], step=STEP_RUN + 3))       # one tile right, other player runs
    assert struct.unpack_from("<I", mem, obj + MO_MOVE_CMD - B)[0] == STEP_RUN + 3
    flags = struct.unpack_from("<I", mem, obj - B)[0]
    assert flags & MO_HELD and not flags & MO_HELD_DONE
    game.follow(obj, dict(peer, tile=[11, 0, 9], step=STEP_RUN + 3))       # mid-step: leave it alone
    struct.pack_into("<I", mem, obj + MO_MOVE_CMD - B, 0xFF)
    game.follow(obj, dict(peer, tile=[11, 0, 9]))
    assert struct.unpack_from("<I", mem, obj + MO_MOVE_CMD - B)[0] == 0xFF, "issued a step while one was running"
    struct.pack_into("<I", mem, obj - B, flags | MO_HELD_DONE)             # the game finished the step
    struct.pack_into("<III", mem, obj + MO_CUR_XYZ - B, 11, 0, 9)
    game.follow(obj, dict(peer, tile=[11, 0, 7]))                          # two tiles up, walking: runs to catch up
    assert struct.unpack_from("<I", mem, obj + MO_MOVE_CMD - B)[0] == STEP_RUN + 0
    struct.pack_into("<I", mem, obj - B, MO_ACTIVE | MO_HELD | MO_HELD_DONE)
    game.follow(obj, dict(peer, tile=[30, 0, 9]))                          # far away: teleport
    assert struct.unpack_from("<III", mem, obj + MO_CUR_XYZ - B) == (30, 0, 9)
    assert decode(struct.unpack("<7H", encode("ab3XY9"))) == "AB3XY9"
    ARGS = 0x02103000
    typed = game.ask_code("K7Q2ZP")
    assert naming == [[0x02102000, NAME_SCREEN_FRIEND, 0, 6, 0, 0, 0]], naming
    assert decode(struct.unpack_from("<7H", mem, shown[0] - B)) == "K7Q2ZP"
    assert typed == "HELLO2", typed
    assert fades == [FADE_IN_FROM_BLACK], fades
    # talking to a puppet: the game stops at CreateScriptContext(fs, PUPPET_SCRIPT) by itself
    CTX, TASK, ENV = 0x02105000, 0x02108000, 0x02109000
    w32(FS + FS_TASKMAN, TASK); w32(TASK + TASK_ENV, ENV); w32(ENV + ENV_LAST_INTERACTED, obj)
    w32(CTX + CTX_MAP_SCRIPTS, 0x02106000); w32(CTX + CTX_MSGDATA, 0x02107000)
    FMT, FIELDS, STR = 0x0210A000, 0x0210A100, 0x0210A200
    w32(ENV + ENV_MSGFMT, FMT); w32(FMT + MSGFMT_FIELDS, FIELDS); w32(FIELDS + 4, STR)
    struct.pack_into("<H", mem, STR - B, 64)                            # String.maxsize
    menus = Menus(game, lambda o: ("B", [0x12B, 0x12C]))             # trainer "AB"
    g.halt(); g.set_handler(CREATE_SCRIPT_CTX, menus._created); g.resume()   # what loop does while facing a puppet
    assert CREATE_SCRIPT_CTX in bps
    regs[0], regs[1], regs[15] = FS, PUPPET_SCRIPT, CREATE_SCRIPT_CTX
    conn[0].sendall(b"$T05swbreak:;#%02x" % (sum(b"T05swbreak:;") & 0xFF))
    time.sleep(0.2)
    g.halt(); g.resume()                                   # loop's tick while armed: the stop gets handled
    script = menu_script(*MENUS["main"][1:])
    assert struct.unpack_from("<HHII", mem, STR - B)[:2] == (64, 2), "name not written"
    assert struct.unpack_from("<3H", mem, STR + 8 - B) == (0x12B, 0x12C, 0xFFFF)
    assert bytes(mem[0x02106000 - B:0x02106000 - B + len(script)]) == script
    assert struct.unpack_from("<II", mem, CTX + CTX_SCRIPT_PTR - B) == (0x02106000, 0x02106000) or         struct.unpack_from("<I", mem, CTX + CTX_SCRIPT_PTR - B)[0] == 0x02106000
    assert loaded == [738], "text bank not swapped: %s" % loaded
    assert DESTROY_SCRIPT_CTX in bps and SCRIPT_CTX_READY not in bps, bps
    # the player picked TRADE (item 1): the script ends
    struct.pack_into("<H", mem, ENV + ENV_RESULT - B, 1)
    regs[0], regs[15] = CTX, DESTROY_SCRIPT_CTX
    conn[0].sendall(b"$T05swbreak:;#%02x" % (sum(b"T05swbreak:;") & 0xFF))
    time.sleep(0.2)
    g.halt(); g.resume()
    assert menus.todo[0]["kind"] == "main" and menus.todo[1:] == (1, obj), menus.todo
    assert warp_script(69, 11, 5, 0)[12:24] == struct.pack("<6H", 176, 69, 0, 11, 5, 0)
    assert popup_script(21, yesno=True) == bytes.fromhex("2d00153f000c8035006100020" + "0")
    assert DESTROY_SCRIPT_CTX not in bps
    g.halt(); g.clear_handler(CREATE_SCRIPT_CTX); g.resume()
    assert not bps, "breakpoints left behind: %s" % [hex(x) for x in bps]
    rport = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); rport.bind(("127.0.0.1", 0))
    port = rport.getsockname()[1]; rport.close()
    threading.Thread(target=relay, args=(port,), daemon=True).start(); time.sleep(0.2)
    a, b = (socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2))
    for s_ in (a, b): s_.settimeout(2); s_.bind(("127.0.0.1", 0))
    a.sendto(b"hello from a", ("127.0.0.1", port)); time.sleep(0.1)
    b.sendto(b"hello from b", ("127.0.0.1", port))
    assert a.recvfrom(100)[0] == b"hello from b"
    a.sendto(b"again", ("127.0.0.1", port))
    assert b.recvfrom(100)[0] == b"again"
    game.remove(obj)
    assert not game.puppet_ok(obj, PUPPET_ID_BASE, 412)
    ATT = 0x02108000                                   # the attendant: third object in a separate array
    w32(MAN + 4, 3); w32(MAN + MAN_OBJECTS, ATT - 2 * MO_SIZE)
    w32(ATT, MO_ACTIVE); w32(ATT + MO_SCRIPT, STD_COLOSSEUM)
    assert game.find_object(STD_COLOSSEUM) == ATT and game.find_object(1234) is None
    w32(ATT + MO_ID, 3); w32(ATT + MO_FACING, 1); struct.pack_into("<III", mem, ATT + MO_CUR_XYZ - B, 11, 0, 4)
    npcs = game.npcs(MAN)                              # only the attendant is active: the map's one NPC
    assert list(npcs) == [3] and npcs[3][0] == ATT and npcs[3][1]["tile"] == [11, 0, 4], npcs
    assert game.npcs(MAN, [ATT]) == npcs
    game.set_paused(ATT, True); assert mem[ATT - B] & MO_PAUSED
    game.set_paused(ATT, False); assert not mem[ATT - B] & MO_PAUSED and mem[ATT - B] & MO_ACTIVE
    assert set(LINK_DESKS) == set(REQUEST_TEXT)       # every accepted request has a desk to go to
    assert game.player()["mon"] is None               # no follower out
    MON = 0x02103800
    w32(FS + FS_FOLLOW_MON, MON); mem[FS + FS_FOLLOW_MON + FOLLOW_ACTIVE - B] = 1
    w32(MON, MO_ACTIVE); w32(MON + MO_SPRITE, 581); w32(MON + MO_PARAMS, 155)
    struct.pack_into("<III", mem, MON + MO_CUR_XYZ - B, 7, 0, 10)
    mon = game.player()["mon"]
    assert (mon["sprite"], mon["tile"], mon["params"][0]) == (581, [7, 0, 10], 155), mon
    w32(FS + FS_FOLLOW_MON, 0)
    menus.queue = [(menus.talk(STD_COLOSSEUM), 0)]
    menus.pump()                                       # a field task is still running: wait
    assert len(menus.queue) == 1
    print("selftest PASS")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--gdb", type=int, default=3333, help="melonDS ARM9 GDB port")
    ap.add_argument("--listen", default="127.0.0.1:47600")
    ap.add_argument("--peer", action="append", default=[], help="another agent's host:port (repeatable)")
    ap.add_argument("--name", default="player")
    ap.add_argument("--hz", type=float, default=30)
    ap.add_argument("--bridge", help="uscl-bridge executable: host our own Steam session, join a friend's by code")
    ap.add_argument("--stop", help="exit cleanly (breakpoints removed) once this file exists; kill an agent only this way")
    ap.add_argument("--relay", type=int, metavar="PORT", help="run the host's relay on 127.0.0.1:PORT instead of an agent")
    ap.add_argument("--link", choices=["host", "join"], help="wireless link role without --bridge (one-PC tests)")
    ap.add_argument("--warp", type=lambda v: tuple(int(n) for n in v.split(",")), metavar="MAP,X,Z,DIR",
                    help="warp the player there once at start (dir 0 up, 1 down, 2 left, 3 right)")
    ap.add_argument("--arm", help="only draw other players while this file exists (testing: save the game first)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    selftest() if a.selftest else relay(a.relay) if a.relay else run(a)
