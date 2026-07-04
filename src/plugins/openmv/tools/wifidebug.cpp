/* Copyright (C) 2023-2026 OpenMV, LLC.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Any redistribution, use, or modification in source or binary form
 *    is done solely for personal benefit and not for any commercial
 *    purpose or for monetary gain. For commercial licensing options,
 *    please contact openmv@openmv.io
 *
 * THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT
 * OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "wifidebug.h"
#include "wifiscan.h"

#include "openmvtr.h"

#include <QtWidgets>
#include <QtCore>

#include <coreplugin/icore.h>
#include <utils/hostosinfo.h>
#include <utils/utilsicons.h>

namespace OpenMV {
namespace Internal {

// ---------------------------------------------------------------------------
// Config model + boot.py text format
// ---------------------------------------------------------------------------

// The dialog edits this; it round-trips to/from the JSON block at the top of boot.py.
struct WifiDebugConfig
{
    int version = 1;
    QString serial;                          // baked hostname id -> "omv-<serial>.local"
    QString iface = QStringLiteral("wifi");  // "wifi" | "ethernet"
    QString ssid;
    QString password;
    QString security = QStringLiteral("auto"); // network.WLAN SEC_* suffix, or "auto" (let connect pick)
    QString ipMode = QStringLiteral("dhcp"); // "dhcp" | "static"
    QString address;
    QString netmask = QStringLiteral("255.255.255.0");
    QString gateway;
    QString dns;
};

// Marker lines that bound the managed region. Everything from the top of the file through the user
// line is regenerated on edit; everything after it is the user's and is preserved verbatim.
static const char *kConfigBegin = "# >>> OPENMV WIFI DEBUG CONFIG (auto-generated) >>>";
static const char *kConfigEnd   = "# <<< OPENMV WIFI DEBUG CONFIG <<<";
static const char *kUserLine     = "# ===== OPENMV WIFI DEBUG: YOUR CODE BELOW (preserved across edits) =====";

// The auto-generated agent body (everything between the config block and the user line). It reads
// the config dict above, brings up the link, advertises "omv-<serial>.local", and bridges the
// hybrid transport (TCP control + UDP frames) to the on-cam protocol engine so the IDE can debug
// over the network. This is the device mirror of the IDE's OMVNetworkPort and the C simulator.
//
// Scripts run in boot.py's own foreground loop via a dynamic "stdin" shadow channel, NOT through the
// firmware's stdin-exec path (which soft-resets the cam after every run -- over the network that
// would tear down WiFi and drop the IDE). boot.py never returns, so that reset path never runs, and
// Stop is delivered as an ordinary KeyboardInterrupt so the VM stays alive between runs.
static const char *kAgentBody = R"PY(
import json, network, socket, struct, time, machine, errno, micropython, sys, gc, select

try:
    import protocol
except ImportError:
    protocol = None

_cfg = json.loads(_WIFI_DEBUG_CONFIG)

_DEBUG_PORT = 0xABD1            # port the IDE connects to (fixed; matches the IDE). TCP (control)
_MDNS_GROUP = "224.0.0.251"    # and UDP (frame data) both bind it -- independent port spaces.
_MDNS_PORT  = 5353
_HOSTNAME   = ("omv-" + _cfg.get("serial", "000000000000"))[:32]

# Protocol wire header byte offsets: SYNC[2] SEQ[1] CHAN[1] FLAGS[1] OPCODE[1] LEN[2] HCRC[2].
_OP_CHANNEL_READ = 0x26
_FLAG_EVENT      = 0x20
# Channels whose CHANNEL_READ responses are the bulk, readp-backed (zero-copy) sources -- camera
# frames and profiler dumps. Only their read responses go over UDP (fire-and-forget, high
# throughput; a dropped frame just skips). Everything else -- control, stdin, and the copying
# stdout reads -- stays on the reliable TCP connection.
_BULK_CHANNELS   = (3, 4)      # stream, profile

# Socket errnos that mean "would block / try again" rather than a dead link. EWOULDBLOCK aliases
# EAGAIN on these builds; the WINC1500's offloaded stack reports a full transmit buffer as ENOBUFS.
_RETRY_ERRNOS = (errno.EAGAIN, getattr(errno, "ENOBUFS", errno.EAGAIN))

# stdin channel ioctl commands (mirror omv_channel_ioctl_stdin_t) -- the IDE's Run/Stop/Reset.
_STDIN_STOP  = 0x01
_STDIN_EXEC  = 0x02
_STDIN_RESET = 0x03


def _nic():
    if _cfg.get("interface") == "ethernet":
        # Not every cam has an Ethernet interface (e.g. the Alif build has no network.LAN). Fail
        # with a clear message instead of a raw AttributeError; the caller's guard keeps USB alive.
        if not hasattr(network, "LAN"):
            raise OSError("this camera has no Ethernet interface")
        return network.LAN()
    return network.WLAN(network.STA_IF)


def _bring_up():
    # Hostname must be set before the interface comes up so mDNS/DHCP pick it up.
    try:
        network.hostname(_HOSTNAME)
    except Exception:
        pass

    nic = _nic()
    nic.active(True)

    if _cfg.get("interface") != "ethernet":
        wifi = _cfg.get("wifi", {})
        sec = wifi.get("security", "auto")
        # Map the config's security name to the driver's security constant. The CYW43 radios name
        # these network.WLAN.SEC_* while the WINC1500 names them network.WLAN.OPEN / WPA_PSK. "open"
        # MUST resolve to the open constant -- otherwise the WINC falls back to its WPA/WPA2 default
        # and can't join a passwordless network -- so try both names (and test "is None", since the
        # open constant can be 0). Any mode a radio doesn't define (WEP/WPA3 on the WINC) stays None
        # and lets connect() pick, rather than raising AttributeError and aborting boot.
        if sec == "auto":
            sec_const = None
        elif sec == "open":
            sec_const = getattr(network.WLAN, "SEC_OPEN", None)
            if sec_const is None:
                sec_const = getattr(network.WLAN, "OPEN", None)   # WINC1500 naming
        else:
            sec_const = getattr(network.WLAN, "SEC_" + sec.upper(), None)
        if sec_const is None:
            nic.connect(wifi.get("ssid", ""), wifi.get("password", ""))
        else:
            nic.connect(wifi.get("ssid", ""), wifi.get("password", ""), security=sec_const)

    ip = _cfg.get("ip", {})
    if ip.get("mode") == "static":
        nic.ifconfig((ip.get("address", "0.0.0.0"),
                      ip.get("netmask", "255.255.255.0"),
                      ip.get("gateway", "0.0.0.0"),
                      ip.get("dns", "0.0.0.0")))

    for _ in range(200):       # up to ~20s for association + DHCP
        try:
            if nic.isconnected() and nic.ifconfig()[0] != "0.0.0.0":
                break
        except Exception:
            pass
        time.sleep_ms(100)
    return nic


def _mdns_packet(name, ip):
    # Minimal mDNS response: one A record for <name>.local -> ip.
    pkt = struct.pack(">HHHHHH", 0, 0x8400, 0, 1, 0, 0)
    for label in (name, "local"):
        pkt += bytes([len(label)]) + label.encode()
    pkt += b"\x00"
    pkt += struct.pack(">HHIH", 1, 0x8001, 120, 4)   # type A, class IN (flush), ttl, rdlen
    pkt += bytes(int(o) for o in ip.split("."))
    return pkt


class _NetworkTransport:
    # Backs protocol channel 0 (PHYSICAL). All control rides an accepted TCP connection; bulk-read
    # responses (frames, profiler) go out over UDP to the IDE's frame endpoint, which IS the TCP
    # peer address -- the IDE binds its frame socket to its TCP local port, so no separate handshake
    # is needed. One port serves both planes (TCP and UDP port spaces are independent).
    @staticmethod
    def _bound_socket(socktype, port, backlog=0):
        # A non-blocking socket bound to port, preferring SO_REUSEADDR (helps rebind after a reset
        # on lwIP). Some offloaded stacks -- the WINC1500 -- CLOSE the socket when handed an
        # unsupported option, so if setsockopt raises we start over without it rather than binding a
        # dead socket. backlog > 0 makes it a TCP listener.
        for with_reuse in (True, False):
            s = socket.socket(socket.AF_INET, socktype)
            s.setblocking(False)
            if with_reuse:
                try:
                    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                except OSError:
                    continue       # option closed the socket -> retry once without it
            s.bind(("0.0.0.0", port))
            if backlog:
                s.listen(backlog)
            return s

    def __init__(self, port):
        self._lsock = self._bound_socket(socket.SOCK_STREAM, port, backlog=1)
        self._usock = self._bound_socket(socket.SOCK_DGRAM, port)
        self._conn = None
        self._peer = None      # IDE UDP frame endpoint == its TCP peer address
        self._rx = bytearray()
        self._tx = bytearray()
        self._poll = select.poll()   # used to tell a dead _conn (peer close/reset) from a merely-idle one

    def _drop(self):
        if self._conn is not None:
            try:
                self._poll.unregister(self._conn)
            except Exception:
                pass
            try:
                self._conn.close()
            except Exception:
                pass
        self._conn = None
        self._peer = None
        # Keep _rx: bytes received before the close are valid protocol data. The IDE sends
        # SYS_RESET and closes immediately, so the command and the EOF arrive within the same
        # 50ms poll tick -- clearing _rx here would discard the reset before the engine reads it.
        self._tx = bytearray()

    # is_active() runs the accept: the C protocol engine polls the transport only while it reports
    # active, so a first connection must be taken here (size()/read() are never reached otherwise).
    def is_active(self):
        if self._conn is None:
            try:
                conn, addr = self._lsock.accept()
                conn.setblocking(False)
                try:
                    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                except Exception:
                    pass
                self._conn = conn
                self._peer = addr
                self._poll.register(self._conn, select.POLLIN)
            except OSError:
                pass           # no pending connection yet
        # Stay active while received data remains to be drained: the C engine only calls
        # size()/read() on an active transport, and a command that arrived just before a
        # disconnect (SYS_RESET) must still be delivered and processed.
        return self._conn is not None or len(self._rx) > 0

    def _recv(self):
        while self._conn is not None:
            try:
                data = self._conn.recv(1500)
            except OSError as e:
                if e.args[0] != errno.EAGAIN:
                    self._drop()   # real error (reset/broken pipe) -> tear down for reconnect
                break              # EAGAIN just means nothing more is pending right now
            if not data:
                # recv() returns b"" for BOTH "nothing pending" and a peer close on this stack. Poll
                # to tell them apart: an idle connected socket isn't readable, but a closed/reset one
                # polls readable (EOF) or HUP/ERR. Only tear the socket down when it's actually dead --
                # closing it on a plain idle read is what broke the first connection.
                if self._poll.poll(0):
                    self._drop()
                break
            self._rx += data

    def size(self):
        self._recv()
        return len(self._rx)

    def read(self, offset, size):
        chunk = bytes(self._rx[:size])
        self._rx = self._rx[size:]
        return chunk

    def write(self, offset, data):
        # send_packet() writes header/payload/crc as separate calls then flush()es once; coalesce
        # here and route the whole packet on flush().
        self._tx += bytes(data)
        return len(data)

    def flush(self):
        # send_packet() writes header/payload/crc as separate writes then flush()es once. Peek the
        # coalesced packet's header and route it: a bulk (readp-channel) frame/profiler read
        # response goes out as one UDP datagram (fire-and-forget); everything else -- control,
        # stdin echo, stdout, events -- must arrive intact, so it rides the TCP connection.
        if not self._tx:
            return 0
        buf = bytes(self._tx)
        self._tx = bytearray()

        bulk = (len(buf) >= 6 and buf[5] == _OP_CHANNEL_READ
                and buf[3] in _BULK_CHANNELS and not (buf[4] & _FLAG_EVENT))
        if bulk and self._peer is not None:
            try:
                self._usock.sendto(buf, self._peer)
            except OSError:
                pass           # datagram dropped: a lost frame just skips (best-effort by design)
            return 0

        if self._conn is None:
            return 0           # no control link yet -> drop it (nothing sends before the IDE connects)

        # Reliable control: the whole packet must reach the IDE or the byte stream desyncs, so a full
        # send buffer is backpressure to retry, not an error. lwIP signals that with EAGAIN; the
        # WINC1500 signals it with ENOBUFS or by returning 0 bytes sent. Only a genuine socket error,
        # or the buffer staying wedged for 2s (peer gone), tears the link down.
        mv = memoryview(buf)
        sent = 0
        deadline = time.ticks_add(time.ticks_ms(), 2000)
        while sent < len(buf):
            try:
                n = self._conn.send(mv[sent:])
            except OSError as e:
                if e.args[0] not in _RETRY_ERRNOS:
                    self._drop()   # reset/broken pipe -> tear down; the IDE reconnects
                    return -1
                n = 0              # backpressure -> fall through to the wait below
            if n:
                sent += n
                continue
            if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
                self._drop()       # send buffer wedged full for 2s -> treat the peer as gone
                return -1
            time.sleep_ms(1)       # let the send buffer drain, then retry
        return 0


class _ScriptChannel:
    # A dynamically-registered shadow of the built-in C "stdin" channel. The IDE resolves the
    # Run/Stop channel by name and prefers this dynamic one, so scripts are driven through here and
    # never touch the C stdin channel's resetting exec path.
    def __init__(self):
        self._buf = bytearray()
        self._pending = None     # script text handed to the foreground loop
        self._running = False    # reported to the IDE as scriptState (poll bit + events)
        self.handle = None       # ProtocolChannel handle (set after register) for send_event()

    def write(self, offset, data):
        if offset == 0:
            self._buf = bytearray()
        self._buf += bytes(data)
        return len(data)

    def poll(self):
        return self._running

    def ioctl(self, cmd, length, arg):
        r = 0
        if cmd == _STDIN_EXEC:
            if not self._buf:
                r = -1
            else:
                self._pending = bytes(self._buf)  # the foreground loop picks this up and runs it
        elif cmd == _STDIN_STOP:
            if self._running:
                # Deliver a KeyboardInterrupt to the foreground script. The scheduled callback is a
                # C function: it sets the VM's pending exception and runs no Python bytecode after,
                # so it survives the protected scheduler call and fires in the script's own frame,
                # where _run_one catches it. (A Python callback that raises dies in the scheduler.)
                micropython.schedule(micropython.keyboard_interrupt, 0)
        elif cmd == _STDIN_RESET:
            self._buf = bytearray()
        return r

    def _set_running(self, running):
        self._running = running
        if self.handle is not None:
            try:
                self.handle.send_event(1 if running else 0)   # drives the IDE's scriptState
            except Exception:
                pass


def _run_one(ch, script):
    # Run one script to completion in the foreground. print()/tracebacks reach the IDE via the C
    # stdout channel and frames via the stream channel -- both serviced by the background protocol
    # poll, which keeps running between this script's bytecodes.
    ch._set_running(True)
    try:
        # Fresh namespace each run so globals don't leak between runs (hardware state persists).
        exec(compile(script, "<script>", "exec"), {"__name__": "__main__"})
    except KeyboardInterrupt:
        pass                          # Stop pressed in the IDE
    except Exception as e:
        sys.print_exception(e)        # show the traceback in the IDE terminal
    finally:
        ch._set_running(False)
        gc.collect()


def _serve_scripts(ch):
    # Own the foreground forever. Returning would let the firmware main loop take over and soft-reset
    # the cam on the next run. main.py is run once here at power-up (the firmware would normally do
    # that) before serving IDE Run/Stop.
    try:
        with open("main.py") as f:
            main_py = f.read()
    except OSError:
        main_py = None
    if main_py:
        _run_one(ch, main_py)

    while True:
        script = ch._pending
        if script is None:
            time.sleep_ms(20)         # idle: let the background poll service frames/stdout/control
            continue
        ch._pending = None
        _run_one(ch, script)


class _SafeIface:
    # Wraps the live WLAN/LAN so a debug script's own network setup can't tear down the link carrying
    # the session. Config calls are ignored; queries report the live state so setup code proceeds.
    def __init__(self, real):
        self._real = real

    def active(self, *a):
        return True

    def connect(self, *a, **k):
        pass

    def disconnect(self, *a):
        pass

    def isconnected(self, *a):
        return True

    def ifconfig(self, *a):
        return self._real.ifconfig()          # report the live config; ignore any set

    def config(self, *a, **k):
        if len(a) == 1 and not k:
            return self._real.config(a[0])     # allow reads like config("mac")
        return None                            # ignore sets (e.g. pm=PM_NONE)

    def __getattr__(self, name):
        return getattr(self._real, name)       # status(), scan(), PM_NONE, ... pass through


class _IfaceFactory:
    # Callable stand-in for network.WLAN / network.LAN that also exposes the class constants (SEC_*,
    # PM_NONE, ...) so scripts referencing e.g. network.WLAN.SEC_WPA_WPA2 keep working.
    def __init__(self, real_cls):
        self._cls = real_cls

    def __call__(self, *a, **k):
        return _SafeIface(self._cls(*a, **k))

    def __getattr__(self, name):
        return getattr(self._cls, name)


class _SafeNetwork:
    # Drop-in for the `network` module seen by user scripts: interface constructors return guarded
    # wrappers and hostname() ignores sets, while everything else (STA_IF, SEC_*, ...) passes through.
    def __init__(self, real):
        self._real = real
        if hasattr(real, "WLAN"):
            self.WLAN = _IfaceFactory(real.WLAN)
        if hasattr(real, "LAN"):
            self.LAN = _IfaceFactory(real.LAN)

    def hostname(self, *a):
        return self._real.hostname() if not a else None   # ignore sets (would move mDNS)

    def __getattr__(self, name):
        return getattr(self._real, name)


def _start_wifi_debug():
    if _cfg.get("interface") == "disabled":
        return   # WiFi debugging turned off -- leave the USB debug transport in place
    if protocol is None:
        return   # no protocol module on this build -> nothing to bridge

    nic = _bring_up()
    ip = nic.ifconfig()[0]

    # Re-initialize the protocol with a faster poll for the network link. Requires re-init support
    # in omv_protocol_init() (deinit-first) -- on older firmware this corrupts the poll soft-timer
    # heap. Drops the USB transport from channel 0 (replaced by our network transport below) and
    # re-registers the stdin/stdout/stream channels.
    protocol.init(crc=True, seq=True, ack=True, events=True, poll_ms=10)
    micropython.kbd_intr(-1)   # keep the C stdin EXEC/STOP ioctls from soft-resetting the cam

    transport = _NetworkTransport(_DEBUG_PORT)
    protocol.register(name="network", backend=transport, flags=protocol.CHANNEL_FLAG_PHYSICAL)

    # Shadow "stdin": the IDE prefers this dynamically-registered channel over the built-in one, so
    # Run/Stop come here instead of the C stdin channel (whose exec path resets the cam). Scripts run
    # in our foreground loop (_serve_scripts) below.
    script_ch = _ScriptChannel()
    script_ch.handle = protocol.register(name="stdin", backend=script_ch,
                                         flags=protocol.CHANNEL_FLAG_WRITE)

    # Re-announce our A record periodically (under the IDE's ~20s retire window) so a late-starting
    # IDE finds us and the entry stays fresh. Bind the source to our interface IP so the multicast
    # egresses the active network interface rather than lwIP's default one.
    ann = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        ann.bind((ip, 0))
    except OSError:
        pass
    pkt = _mdns_packet(_HOSTNAME, ip)

    def _announce(_t):
        try:
            ann.sendto(pkt, (_MDNS_GROUP, _MDNS_PORT))
        except OSError:
            pass

    _announce(None)
    machine.Timer(-1, period=5000, callback=_announce)

    # Shield the debug link: a user script's own WiFi setup (active/connect/ifconfig/hostname) must
    # not reconfigure or tear down the interface the debug session rides on. Swap in a guarded
    # `network` so those calls no-op while queries report the live state; sockets are untouched. Our
    # agent keeps its real `network` (captured at import), so this only affects code that imports it
    # later -- i.e. user scripts.
    try:
        sys.modules["network"] = _SafeNetwork(network)
    except Exception:
        pass

    _serve_scripts(script_ch)   # owns the foreground forever -- never returns (see the function)


try:
    _start_wifi_debug()
except Exception as _e:
    # A setup failure must never brick the cam. Because the USB debug transport is only replaced on
    # success (after the link is up, just before returning), any error here leaves USB debugging
    # working -- so the IDE can still connect over the cable to fix the configuration.
    print("OpenMV WiFi debug: setup failed:", _e)
)PY";

// ---------------------------------------------------------------------------
// JSON round-trip
// ---------------------------------------------------------------------------

static QByteArray configToJson(const WifiDebugConfig &c)
{
    QJsonObject wifi;
    wifi[QStringLiteral("ssid")] = c.ssid;
    wifi[QStringLiteral("password")] = c.password;
    wifi[QStringLiteral("security")] = c.security;

    QJsonObject ip;
    ip[QStringLiteral("mode")] = c.ipMode;
    if(c.ipMode == QStringLiteral("static"))
    {
        ip[QStringLiteral("address")] = c.address;
        ip[QStringLiteral("netmask")] = c.netmask;
        ip[QStringLiteral("gateway")] = c.gateway;
        ip[QStringLiteral("dns")] = c.dns;
    }

    QJsonObject root;
    root[QStringLiteral("version")] = c.version;
    root[QStringLiteral("serial")] = c.serial;
    root[QStringLiteral("interface")] = c.iface;
    root[QStringLiteral("wifi")] = wifi;
    root[QStringLiteral("ip")] = ip;

    return QJsonDocument(root).toJson(QJsonDocument::Indented).trimmed();
}

static WifiDebugConfig configFromJson(const QByteArray &json)
{
    WifiDebugConfig c;
    const QJsonObject root = QJsonDocument::fromJson(json).object();

    c.version = root.value(QStringLiteral("version")).toInt(1);
    c.serial = root.value(QStringLiteral("serial")).toString();
    c.iface = root.value(QStringLiteral("interface")).toString(QStringLiteral("wifi"));

    const QJsonObject wifi = root.value(QStringLiteral("wifi")).toObject();
    c.ssid = wifi.value(QStringLiteral("ssid")).toString();
    c.password = wifi.value(QStringLiteral("password")).toString();
    c.security = wifi.value(QStringLiteral("security")).toString(QStringLiteral("wpa2_psk"));

    const QJsonObject ip = root.value(QStringLiteral("ip")).toObject();
    c.ipMode = ip.value(QStringLiteral("mode")).toString(QStringLiteral("dhcp"));
    c.address = ip.value(QStringLiteral("address")).toString();
    c.netmask = ip.value(QStringLiteral("netmask")).toString(QStringLiteral("255.255.255.0"));
    c.gateway = ip.value(QStringLiteral("gateway")).toString();
    c.dns = ip.value(QStringLiteral("dns")).toString();

    return c;
}

// Compose the full boot.py: warning header, config block, agent body, user line, user code.
static QString generateBootPy(const WifiDebugConfig &c, const QString &userCode)
{
    const QString header =
        QStringLiteral("# OpenMV WiFi debug boot.py -- generated by the OpenMV IDE.\n"
                       "#\n"
                       "# Everything from here down to the \"YOUR CODE BELOW\" line is auto-generated.\n"
                       "# Re-running \"Edit boot.py for WiFi Debugging\" rewrites it -- DO NOT edit it by\n"
                       "# hand. Put your own code below that line; it is preserved across edits.\n\n");

    QString out = header;
    out += QString::fromUtf8(kConfigBegin) + QChar('\n');
    out += QStringLiteral("_WIFI_DEBUG_CONFIG = \"\"\"\n");
    out += QString::fromUtf8(configToJson(c)) + QChar('\n');
    out += QStringLiteral("\"\"\"\n");
    out += QString::fromUtf8(kConfigEnd) + QChar('\n');
    out += QString::fromUtf8(kAgentBody);
    out += QChar('\n') + QString::fromUtf8(kUserLine) + QChar('\n');
    out += userCode.isEmpty() ? QStringLiteral("\n# Your application code goes here.\n") : userCode;

    return out;
}

// Pull the config JSON (between the config markers) and the user code (after the user line) out of an
// existing boot.py. Missing/unparseable pieces fall back to defaults so editing never hard-fails.
static void parseBootPy(const QString &text, WifiDebugConfig *config, QString *userCode)
{
    const int beginIdx = text.indexOf(QString::fromUtf8(kConfigBegin));
    const int endIdx = text.indexOf(QString::fromUtf8(kConfigEnd));

    if((beginIdx >= 0) && (endIdx > beginIdx))
    {
        const QString block = text.mid(beginIdx, endIdx - beginIdx);
        const int q1 = block.indexOf(QStringLiteral("\"\"\""));
        const int q2 = (q1 >= 0) ? block.indexOf(QStringLiteral("\"\"\""), q1 + 3) : -1;

        if(q2 > q1)
        {
            *config = configFromJson(block.mid(q1 + 3, q2 - (q1 + 3)).toUtf8());
        }
    }

    const int userIdx = text.indexOf(QString::fromUtf8(kUserLine));
    if(userIdx >= 0)
    {
        const int nl = text.indexOf(QChar('\n'), userIdx);
        *userCode = (nl >= 0) ? text.mid(nl + 1) : QString();
    }
}

// ---------------------------------------------------------------------------
// Dialog (built in code -- no .ui)
// ---------------------------------------------------------------------------

class WifiDebugDialog : public QDialog
{
public:

    explicit WifiDebugDialog(const WifiDebugConfig &initial, bool editingExisting, QWidget *parent = nullptr)
        // Pass flags to the QDialog ctor (it adds Qt::Dialog when no window type is set); calling
        // setWindowFlags() with hints-only after construction would demote it to a Qt::Widget.
        : QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                          (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint))
        , m_config(initial)
        , m_editingExisting(editingExisting)
    {
        setWindowTitle(m_editingExisting ? Tr::tr("Edit WiFi Debugging") : Tr::tr("Set Up WiFi Debugging"));
        buildUi();
        loadFrom(initial);
        updateEnabledState();
    }

    WifiDebugConfig config() const { return m_config; }

    void accept() override
    {
        m_config.iface = m_wifiRadio->isChecked() ? QStringLiteral("wifi")
                       : m_ethernetRadio->isChecked() ? QStringLiteral("ethernet")
                       : QStringLiteral("disabled");
        m_config.ssid = m_ssidCombo->currentText().trimmed();
        m_config.password = m_passwordEdit->text();
        m_config.security = m_securityCombo->currentData().toString();
        m_config.ipMode = m_staticRadio->isChecked() ? QStringLiteral("static") : QStringLiteral("dhcp");
        m_config.address = m_addressEdit->text().trimmed();
        m_config.netmask = m_netmaskEdit->text().trimmed();
        m_config.gateway = m_gatewayEdit->text().trimmed();
        m_config.dns = m_dnsEdit->text().trimmed();

        if(m_wifiRadio->isChecked() && m_config.ssid.isEmpty())
        {
            QMessageBox::warning(this, windowTitle(), Tr::tr("Please enter or select a WiFi network name (SSID)."));
            return;
        }

        QDialog::accept();
    }

private:

    void buildUi()
    {
        QVBoxLayout *root = new QVBoxLayout(this);

        QLabel *intro = new QLabel(m_editingExisting
            ? Tr::tr("Editing this camera's WiFi debugging setup -- this updates the boot.py already on "
                     "the camera, keeping any code you added to it. It brings the network up on "
                     "power-up and advertises the camera to the IDE, so you can connect without a USB "
                     "cable. While WiFi Debugging is active, USB debugging is disabled -- choose "
                     "\"Disabled\" below to turn it off and go back to USB.")
            : Tr::tr("Set up this camera for debugging over the network. This writes a boot.py to the "
                     "camera that brings up its WiFi or Ethernet on power-up and advertises it to the "
                     "IDE, so you can connect to it without a USB cable. While WiFi Debugging is "
                     "active, USB debugging is disabled -- choose \"Disabled\" below to turn it off "
                     "and go back to USB."), this);
        intro->setWordWrap(true);
        root->addWidget(intro);

        // Interface selection.
        QGroupBox *ifaceBox = new QGroupBox(Tr::tr("Connection"), this);
        QHBoxLayout *ifaceLayout = new QHBoxLayout(ifaceBox);
        m_wifiRadio = new QRadioButton(Tr::tr("WiFi"), ifaceBox);
        m_ethernetRadio = new QRadioButton(Tr::tr("Ethernet"), ifaceBox);
        m_disabledRadio = new QRadioButton(Tr::tr("Disabled (USB debugging)"), ifaceBox);
        ifaceLayout->addWidget(m_wifiRadio);
        ifaceLayout->addWidget(m_ethernetRadio);
        ifaceLayout->addWidget(m_disabledRadio);
        ifaceLayout->addStretch();
        root->addWidget(ifaceBox);

        // WiFi settings.
        m_wifiBox = new QGroupBox(Tr::tr("WiFi Network"), this);
        QFormLayout *wifiLayout = new QFormLayout(m_wifiBox);

        m_ssidCombo = new QComboBox(m_wifiBox);
        m_ssidCombo->setEditable(true);
        m_ssidCombo->setInsertPolicy(QComboBox::NoInsert);
        m_ssidCombo->setMinimumWidth(240);
        m_ssidCombo->lineEdit()->setClearButtonEnabled(true);
        m_scanButton = new QPushButton(Tr::tr("Scan"), m_wifiBox);
        QHBoxLayout *ssidRow = new QHBoxLayout;
        ssidRow->setContentsMargins(0, 0, 0, 0);
        ssidRow->addWidget(m_ssidCombo, 1);
        ssidRow->addWidget(m_scanButton);
        wifiLayout->addRow(Tr::tr("Network (SSID):"), ssidRow);

        // Password field matching the Settings Editor: a clear (x) button plus a trailing eye that
        // toggles the masked text.
        m_passwordEdit = new QLineEdit(m_wifiBox);
        m_passwordEdit->setEchoMode(QLineEdit::Password);
        m_passwordEdit->setClearButtonEnabled(true);
        QAction *reveal = m_passwordEdit->addAction(Utils::Icons::EYE_CLOSED_TOOLBAR.icon(), QLineEdit::TrailingPosition);
        reveal->setToolTip(Tr::tr("Show text"));
        connect(reveal, &QAction::triggered, m_passwordEdit, [edit = m_passwordEdit, reveal] {
            const bool hidden = (edit->echoMode() == QLineEdit::Password);
            edit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
            reveal->setIcon(hidden ? Utils::Icons::EYE_OPEN_TOOLBAR.icon() : Utils::Icons::EYE_CLOSED_TOOLBAR.icon());
            reveal->setToolTip(hidden ? Tr::tr("Hide text") : Tr::tr("Show text"));
        });
        wifiLayout->addRow(Tr::tr("Password:"), m_passwordEdit);

        // network.WLAN.SEC_* values (CYW43). "Automatic" omits the security arg so connect() picks
        // it (open if no key, else WPA/WPA2). See network.WLAN docs.
        m_securityCombo = new QComboBox(m_wifiBox);
        m_securityCombo->addItem(Tr::tr("Automatic"), QStringLiteral("auto"));
        m_securityCombo->addItem(Tr::tr("Open (no password)"), QStringLiteral("open"));
        m_securityCombo->addItem(Tr::tr("WPA / WPA2"), QStringLiteral("wpa_wpa2"));
        m_securityCombo->addItem(Tr::tr("WPA3"), QStringLiteral("wpa3"));
        m_securityCombo->addItem(Tr::tr("WPA2 / WPA3"), QStringLiteral("wpa2_wpa3"));
        m_securityCombo->addItem(Tr::tr("WEP (legacy)"), QStringLiteral("wep"));
        wifiLayout->addRow(Tr::tr("Security:"), m_securityCombo);

        root->addWidget(m_wifiBox);

        // IP settings.
        m_ipBox = new QGroupBox(Tr::tr("IP Address"), this);
        QVBoxLayout *ipLayout = new QVBoxLayout(m_ipBox);
        QHBoxLayout *ipModeRow = new QHBoxLayout;
        m_dhcpRadio = new QRadioButton(Tr::tr("Automatic (DHCP)"), m_ipBox);
        m_staticRadio = new QRadioButton(Tr::tr("Static"), m_ipBox);
        ipModeRow->addWidget(m_dhcpRadio);
        ipModeRow->addWidget(m_staticRadio);
        ipModeRow->addStretch();
        ipLayout->addLayout(ipModeRow);

        m_staticWidget = new QWidget(m_ipBox);
        QFormLayout *staticLayout = new QFormLayout(m_staticWidget);
        staticLayout->setContentsMargins(0, 0, 0, 0);
        m_addressEdit = newIpEdit(m_staticWidget);
        m_netmaskEdit = newIpEdit(m_staticWidget);
        m_gatewayEdit = newIpEdit(m_staticWidget);
        m_dnsEdit = newIpEdit(m_staticWidget);
        staticLayout->addRow(Tr::tr("Address:"), m_addressEdit);
        staticLayout->addRow(Tr::tr("Netmask:"), m_netmaskEdit);
        staticLayout->addRow(Tr::tr("Gateway:"), m_gatewayEdit);
        staticLayout->addRow(Tr::tr("DNS:"), m_dnsEdit);
        ipLayout->addWidget(m_staticWidget);

        root->addWidget(m_ipBox);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        root->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_scanButton, &QPushButton::clicked, this, &WifiDebugDialog::scan);
        connect(m_ssidCombo, &QComboBox::textActivated, this, [this] (const QString &ssid) {
            // Picking a network from the list resets the password to that network's saved one (or
            // clears it if the host has none -- so we never carry another network's password over).
            m_passwordEdit->setText(savedWifiPassword(ssid));
        });
        connect(m_wifiRadio, &QRadioButton::toggled, this, &WifiDebugDialog::updateEnabledState);
        connect(m_ethernetRadio, &QRadioButton::toggled, this, &WifiDebugDialog::updateEnabledState);
        connect(m_disabledRadio, &QRadioButton::toggled, this, &WifiDebugDialog::updateEnabledState);
        connect(m_dhcpRadio, &QRadioButton::toggled, this, &WifiDebugDialog::updateEnabledState);
        connect(m_securityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WifiDebugDialog::updateEnabledState);
    }

    static QLineEdit *newIpEdit(QWidget *parent)
    {
        QLineEdit *edit = new QLineEdit(parent);
        // Loose IPv4 validator: four 0-255 octets. (Empty is allowed; required fields are checked
        // by the agent at runtime -- a too-strict validator just frustrates editing.)
        const QString octet = QStringLiteral("(25[0-5]|2[0-4][0-9]|1?[0-9]?[0-9])");
        edit->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("^%1\\.%1\\.%1\\.%1$").arg(octet)), edit));
        edit->setMinimumWidth(160);
        return edit;
    }

    void loadFrom(const WifiDebugConfig &c)
    {
        m_ethernetRadio->setChecked(c.iface == QStringLiteral("ethernet"));
        m_disabledRadio->setChecked(c.iface == QStringLiteral("disabled"));
        m_wifiRadio->setChecked((!m_ethernetRadio->isChecked()) && (!m_disabledRadio->isChecked()));

        if(!c.ssid.isEmpty())
        {
            m_ssidCombo->addItem(c.ssid);
        }
        m_ssidCombo->setCurrentText(c.ssid);
        m_passwordEdit->setText(c.password);
        const int secIdx = m_securityCombo->findData(c.security);
        m_securityCombo->setCurrentIndex((secIdx >= 0) ? secIdx : 0);

        m_dhcpRadio->setChecked(c.ipMode != QStringLiteral("static"));
        m_staticRadio->setChecked(c.ipMode == QStringLiteral("static"));
        m_addressEdit->setText(c.address);
        m_netmaskEdit->setText(c.netmask);
        m_gatewayEdit->setText(c.gateway);
        m_dnsEdit->setText(c.dns);
    }

    void updateEnabledState()
    {
        const bool wifi = m_wifiRadio->isChecked();
        const bool networked = wifi || m_ethernetRadio->isChecked(); // i.e. not "Disabled"
        m_wifiBox->setEnabled(wifi);
        // Open networks don't take a password.
        m_passwordEdit->setEnabled(wifi && (m_securityCombo->currentData().toString() != QStringLiteral("open")));
        m_ipBox->setEnabled(networked);
        m_staticWidget->setEnabled(networked && m_staticRadio->isChecked());
    }

    void scan()
    {
        const QString current = m_ssidCombo->currentText().trimmed();
        const QStringList networks = scanWifiNetworks();

        m_ssidCombo->clear();
        m_ssidCombo->addItems(networks);

        if(!current.isEmpty())
        {
            m_ssidCombo->setCurrentText(current); // keep what the user already chose/typed
        }
        else if(!networks.isEmpty())
        {
            // Nothing chosen yet -> default to the network the PC is on (else the first found).
            const QString connected = connectedWifiSsid();
            const int idx = connected.isEmpty() ? -1 : networks.indexOf(connected);
            m_ssidCombo->setCurrentIndex((idx >= 0) ? idx : 0);
            m_passwordEdit->setText(savedWifiPassword(m_ssidCombo->currentText()));
        }

        if(networks.isEmpty())
        {
            QMessageBox::information(this, windowTitle(),
                Tr::tr("No WiFi networks were found (this PC may have no WiFi adapter). "
                       "You can type the network name in by hand."));
        }
    }

    WifiDebugConfig m_config;
    bool m_editingExisting = false;

    QRadioButton *m_wifiRadio = nullptr;
    QRadioButton *m_ethernetRadio = nullptr;
    QRadioButton *m_disabledRadio = nullptr;
    QGroupBox *m_wifiBox = nullptr;
    QComboBox *m_ssidCombo = nullptr;
    QPushButton *m_scanButton = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QComboBox *m_securityCombo = nullptr;
    QGroupBox *m_ipBox = nullptr;
    QRadioButton *m_dhcpRadio = nullptr;
    QRadioButton *m_staticRadio = nullptr;
    QWidget *m_staticWidget = nullptr;
    QLineEdit *m_addressEdit = nullptr;
    QLineEdit *m_netmaskEdit = nullptr;
    QLineEdit *m_gatewayEdit = nullptr;
    QLineEdit *m_dnsEdit = nullptr;
};

// ---------------------------------------------------------------------------
// Public actions
// ---------------------------------------------------------------------------

void editWifiDebugBootPy(const QString &drivePath, const QString &serialNumber, const ConfigFileWriter &writer)
{
    const QString path = QDir::cleanPath(drivePath + QStringLiteral("/boot.py"));

    WifiDebugConfig config;
    QString userCode;

    QFile file(path);
    const bool existed = file.open(QIODevice::ReadOnly | QIODevice::Text);
    if(existed)
    {
        const QString existing = QString::fromUtf8(file.readAll());
        file.close();

        if(existing.indexOf(QString::fromUtf8(kConfigBegin)) >= 0)
        {
            // Our boot.py: pull the config + the user's code back so we rewrite only the managed region.
            parseBootPy(existing, &config, &userCode);
        }
        else
        {
            // A plain boot.py: keep all of it as the user's code and prepend our setup above it.
            userCode = existing;
        }
    }
    // No boot.py yet -> config stays default and userCode empty: we create a fresh one.

    // Always have a serial to bake: an existing config keeps its own; otherwise use the live cam's.
    if(config.serial.isEmpty())
    {
        config.serial = serialNumber;
    }

    WifiDebugDialog dialog(config, existed, Core::ICore::dialogParent());
    if(dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    QString err;
    if(!writer(path, generateBootPy(dialog.config(), userCode).toUtf8(), &err))
    {
        QMessageBox::critical(Core::ICore::dialogParent(), Tr::tr("Edit boot.py"),
            Tr::tr("Failed to write boot.py to the camera:\n\n%1").arg(err));
    }
}

} // namespace Internal
} // namespace OpenMV
