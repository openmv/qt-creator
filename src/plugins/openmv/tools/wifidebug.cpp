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
// the config dict above, brings up the link, advertises "omv-<serial>.local", and bridges a UDP
// socket to the on-cam protocol engine so the IDE can debug over the network.
//
// NOTE (first draft): this wires the transport + discovery so the existing (stdin-based) IDE can
// connect over wifi. A "Run" still triggers the C soft reset (which drops wifi briefly); boot.py
// re-runs and re-establishes everything, and the IDE re-discovers via mDNS. Eliminating that reset
// (run scripts in-place from a Python control channel) is the next iteration.
static const char *kAgentBody = R"PY(
import json, network, socket, struct, time, machine

try:
    import protocol
except ImportError:
    protocol = None

_cfg = json.loads(_WIFI_DEBUG_CONFIG)

_DEBUG_PORT = 0xABD1            # UDP port the IDE connects to (fixed; matches the IDE)
_MDNS_GROUP = "224.0.0.251"
_MDNS_PORT  = 5353
_HOSTNAME   = ("omv-" + _cfg.get("serial", "000000000000"))[:32]


def _nic():
    return network.LAN() if _cfg.get("interface") == "ethernet" else network.WLAN(network.STA_IF)


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
        if sec == "auto":
            nic.connect(wifi.get("ssid", ""), wifi.get("password", ""))
        else:
            nic.connect(wifi.get("ssid", ""), wifi.get("password", ""),
                        security=getattr(network.WLAN, "SEC_" + sec.upper()))

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


class _UDPTransport:
    # Backs protocol channel 0 (PHYSICAL): IDE datagrams in, our replies out.
    def __init__(self, port):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setblocking(False)
        self._sock.bind(("0.0.0.0", port))
        self._peer = None
        self._rx = bytearray()

    def _pump(self):
        try:
            while True:
                data, addr = self._sock.recvfrom(1500)
                if not data:
                    break
                self._peer = addr
                self._rx += data
        except OSError:
            pass

    def is_active(self):
        return True

    def size(self):
        self._pump()
        return len(self._rx)

    def read(self, offset, size):
        self._pump()
        chunk = bytes(self._rx[:size])
        del self._rx[:size]
        return chunk

    def write(self, offset, data):
        if self._peer is not None:
            try:
                self._sock.sendto(bytes(data), self._peer)
            except OSError:
                return 0
        return len(data)


def _start_wifi_debug():
    if _cfg.get("interface") == "disabled":
        return   # WiFi debugging turned off -- the cam stays on USB debugging
    nic = _bring_up()
    ip = nic.ifconfig()[0]

    if protocol is not None:
        protocol.init(poll_ms=5)   # registers the C stdin/stdout/stream channels, no transport
        transport = _UDPTransport(_DEBUG_PORT)
        protocol.register(name="wifi", backend=transport, flags=protocol.CHANNEL_FLAG_PHYSICAL)

    # Re-announce our A record periodically (under the IDE's ~20s retire window) so a late-starting
    # IDE finds us and the entry stays fresh.
    ann = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pkt = _mdns_packet(_HOSTNAME, ip)

    def _announce(_t):
        try:
            ann.sendto(pkt, (_MDNS_GROUP, _MDNS_PORT))
        except OSError:
            pass

    _announce(None)
    machine.Timer(-1, period=5000, callback=_announce)


_start_wifi_debug()
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
