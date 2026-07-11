/* Copyright (C) 2023-2024 OpenMV, LLC.
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

#include "openmvplugin.h"
#include "openmvtr.h"
#include "openmvpluginconnect.h"

#include "app/app_version.h"
#include "tools/usbproblems.h"

#include <QDirIterator>

#include <utils/async.h>

#include <QFuture>
#include <QFutureWatcher>
#include <QPromise>

#include <QGuiApplication>

#include <limits>

namespace OpenMV {
namespace Internal {

static QString openmvServerUserAgent()
{
    // Identify honestly as the IDE. A bare "Mozilla/5.0 (X11; Linux x86_64)" (browser
    // impersonation with no engine tokens) trips Cloudflare bot-management in front of
    // upload.openmv.io, which returns a 403 challenge at the edge before the request
    // reaches the origin -- so the Linux license check silently never arrived.
    const QString version = QStringLiteral("OpenMV-IDE/%1.%2.%3")
        .arg(IDE_VERSION_MAJOR).arg(IDE_VERSION_MINOR).arg(IDE_VERSION_RELEASE);
    if (Utils::HostOsInfo::isWindowsHost())
        return version + QStringLiteral(" (Windows NT 10.0; Win64; x64)");
    if (Utils::HostOsInfo::isMacHost())
        return version + QStringLiteral(" (Macintosh; Intel Mac OS X 10_15_7)");
    return version + QStringLiteral(" (X11; Linux x86_64)");
}

static bool removeRecursively(const Utils::FilePath &path, QString *error)
{
    return path.removeRecursively(error);
}

static bool removeRecursivelyWrapper(const Utils::FilePath &path, QString *error)
{
    QEventLoop loop;
    QFutureWatcher<bool> watcher;
    QObject::connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(removeRecursively, path, error));
    loop.exec();
    return watcher.result();
}

static bool removeRecursivelyWrapper(const Utils::FilePath &path, const QList<QString> &subFolders, QString *error)
{
    bool ok = true;

    for (const QString &subFolder : subFolders)
    {
        ok = removeRecursivelyWrapper(path.pathAppended(subFolder), error);

        if(!ok)
        {
            break;
        }
    }

    return ok;
}

static bool extractAll(QByteArray *data, const QString &path)
{
    QBuffer buffer(data);
    QZipReader reader(&buffer);
    return reader.extractAll(path);
}

// The viewer ships (and updates) only the firmware, so extract just that folder
// from the full resources archive instead of everything (examples/html/models/...).
static bool extractFolder(QByteArray *data, const QString &path, const QString &folder)
{
    QBuffer buffer(data);
    QZipReader reader(&buffer);
    const QString prefix = folder + QStringLiteral("/");

    for(const QZipReader::FileInfo &info : reader.fileInfoList())
    {
        if((info.filePath != folder) && (!info.filePath.startsWith(prefix)))
        {
            continue;
        }

        const QString dest = path + QStringLiteral("/") + info.filePath;

        if(info.isDir)
        {
            if(!QDir().mkpath(dest))
            {
                return false;
            }
        }
        else if(info.isFile)
        {
            if(!QDir().mkpath(QFileInfo(dest).path()))
            {
                return false;
            }

            QFile file(dest);

            if(!file.open(QIODevice::WriteOnly))
            {
                return false;
            }

            const QByteArray fileData = reader.fileData(info.filePath);

            if(file.write(fileData) != fileData.size())
            {
                return false;
            }

            file.close();
        }
    }

    return true;
}

static bool extractFolderWrapper(QByteArray *data, const QString &path, const QString &folder)
{
    QEventLoop loop;
    QFutureWatcher<bool> watcher;
    QObject::connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(extractFolder, data, path, folder));
    loop.exec();
    return watcher.result();
}

// --- Development resource cache helpers --------------------------------------
// Dev examples/docs/firmware are published separately from the released resources
// and cached next to them with a "-dev" suffix (examples-dev, html-dev, firmware-dev)
// under allUsersResourcePath(). The IDE reads them when a development cam is attached.

static const char DEV_MANIFEST_URL[] = "https://download.openmv.io/studio/manifest.json";
static const char DEV_DOCS_RELEASE_API[] = "https://api.github.com/repos/openmv/openmv-doc/releases/tags/development";

// Path to the bundled python interpreter -- used to unpack .tar.xz archives (via its
// tarfile+lzma modules), which QZipReader cannot handle.
static Utils::FilePath bundledPythonBinary()
{
    if(Utils::HostOsInfo::isWindowsHost())
        return Core::ICore::resourcePath(QStringLiteral("python/win/python.exe"));
    if(Utils::HostOsInfo::isMacHost())
        return Core::ICore::resourcePath(QStringLiteral("python/mac/bin/python"));
    if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        return Core::ICore::resourcePath(QStringLiteral("python/linux-arm64/bin/python"));
    if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        return Core::ICore::resourcePath(QStringLiteral("python/linux-arm/bin/python"));
    return Core::ICore::resourcePath(QStringLiteral("python/linux-x86_64/bin/python"));
}

static QString sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

// Unpack an in-memory .tar.xz into destDir using the bundled python's tarfile module.
// `python` is resolved on the GUI thread and passed in (this runs on a worker thread).
static bool extractTarXzToDir(const QByteArray &data, const QString &destDir, const QString &python)
{
    if(python.isEmpty() || (!QFileInfo::exists(python)))
    {
        return false;
    }

    const QString archivePath = QDir::tempPath() + QStringLiteral("/openmv-dev-") + sha256Hex(data).left(16) + QStringLiteral(".tar.xz");

    {
        QFile file(archivePath);

        if((!file.open(QIODevice::WriteOnly)) || (file.write(data) != data.size()))
        {
            return false;
        }

        file.close();
    }

    QProcess process;
    process.start(python, QStringList()
        << QStringLiteral("-c")
        << QStringLiteral("import sys,tarfile\nt=tarfile.open(sys.argv[1],'r:xz')\nt.extractall(sys.argv[2])\nt.close()")
        << archivePath
        << destDir);

    bool ok = process.waitForStarted(30000) && process.waitForFinished(-1)
        && (process.exitStatus() == QProcess::NormalExit) && (process.exitCode() == 0);

    QFile::remove(archivePath);
    return ok;
}

// Unpack an in-memory .zip into destDir via QZipReader.
static bool extractZipToDir(const QByteArray &data, const QString &destDir)
{
    QByteArray copy = data;
    QBuffer buffer(&copy);
    QZipReader reader(&buffer);
    return reader.extractAll(destDir);
}

// Install a downloaded dev archive as allUsersResourcePath()/<devDirName>. The studio
// tarballs wrap their content in a single top-level dir (examples-dev/, firmware-dev/)
// and the docs zip wraps it in html/, so unpack into a scratch dir and move whatever
// single top-level directory it contains into place -- replacing any previous copy.
static bool installDevArchive(const QByteArray &data, bool isTarXz, const QString &devDirName, const QString &base, const QString &python)
{
    const QString scratch = base + QStringLiteral("/.") + devDirName + QStringLiteral("-tmp");

    QDir(scratch).removeRecursively();

    if(!QDir().mkpath(scratch))
    {
        return false;
    }

    bool ok = isTarXz ? extractTarXzToDir(data, scratch, python) : extractZipToDir(data, scratch);

    if(ok)
    {
        const QStringList dirs = QDir(scratch).entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        if(dirs.size() == 1)
        {
            const QString target = base + QStringLiteral("/") + devDirName;
            QDir(target).removeRecursively();
            ok = QDir().rename(scratch + QStringLiteral("/") + dirs.first(), target);
        }
        else
        {
            ok = false;
        }
    }

    QDir(scratch).removeRecursively();
    return ok;
}

// Blocking GET on the calling (worker) thread, reporting download progress and honouring
// cancellation through `promise`. A local event loop pumps the reply's events.
static QByteArray devDownloadSync(const QUrl &url, QPromise<DevSyncOutcome> &promise)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = manager.get(request);

    if(!reply)
    {
        return QByteArray();
    }

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [reply, &promise] (qint64 received, qint64 total) {
        if(promise.isCanceled())
        {
            reply->abort();
        }
        else if(total > 0)
        {
            promise.setProgressRange(0, static_cast<int>(total));
            promise.setProgressValue(static_cast<int>(received));
        }
    });

    loop.exec();

    QByteArray result = (reply->error() == QNetworkReply::NoError) ? reply->readAll() : QByteArray();
    reply->deleteLater();
    return result;
}

// The installed version of a dev cache lives on disk next to it (e.g.
// examples-dev.version beside examples-dev/), so the cache is self-describing and
// can't drift from a separately-stored stamp. Both helpers run on the worker thread.
static QString readDevCacheVersion(const QString &base, const QString &devDirName)
{
    QFile file(base + QStringLiteral("/") + devDirName + QStringLiteral(".version"));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

static void writeDevCacheVersion(const QString &base, const QString &devDirName, const QString &version)
{
    QFile file(base + QStringLiteral("/") + devDirName + QStringLiteral(".version"));

    if(file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(version.toUtf8());
    }
}

// Worker (runs on a thread-pool thread via Utils::asyncRun): download + extract the
// requested dev resources off the GUI thread, reporting progress via `promise`. `base`
// and `python` are resolved on the GUI thread and passed in so this never calls into
// Core::ICore. It records each installed version on disk and returns which resources
// it (re)installed (so the GUI thread can reload the example filters).
static void devSyncWorker(QPromise<DevSyncOutcome> &promise, int parts,
                          const QString &base, const QString &python)
{
    DevSyncOutcome out;
    promise.setProgressRange(0, 0);

    if((parts & OpenMVPlugin::DevExamples) && (!promise.isCanceled()))
    {
        promise.setProgressValueAndText(0, Tr::tr("Checking development examples..."));
        QJsonObject dev = QJsonDocument::fromJson(devDownloadSync(QUrl(QLatin1String(DEV_MANIFEST_URL)), promise)).object()
            .value(QStringLiteral("examples")).toObject().value(QStringLiteral("development")).toObject();
        const QString version = dev.value(QStringLiteral("version")).toString();
        const QString url = dev.value(QStringLiteral("url")).toString();
        const QString sha = dev.value(QStringLiteral("sha256")).toString();

        if((!url.isEmpty()) && ((!QFileInfo::exists(base + QStringLiteral("/examples-dev"))) || (version != readDevCacheVersion(base, QStringLiteral("examples-dev")))))
        {
            promise.setProgressValueAndText(0, Tr::tr("Downloading development examples..."));
            QByteArray data = devDownloadSync(QUrl(url), promise);

            if((!data.isEmpty()) && (sha.isEmpty() || (sha256Hex(data) == sha)) && installDevArchive(data, true, QStringLiteral("examples-dev"), base, python))
            {
                writeDevCacheVersion(base, QStringLiteral("examples-dev"), version);
                out.examplesVersion = version;
            }
        }
    }

    if((parts & OpenMVPlugin::DevDocs) && (!promise.isCanceled()))
    {
        promise.setProgressValueAndText(0, Tr::tr("Checking development documentation..."));
        QString url, stamp;

        for(const QJsonValue &asset : QJsonDocument::fromJson(devDownloadSync(QUrl(QLatin1String(DEV_DOCS_RELEASE_API)), promise)).object().value(QStringLiteral("assets")).toArray())
        {
            const QJsonObject assetObject = asset.toObject();

            if(assetObject.value(QStringLiteral("name")).toString() == QStringLiteral("openmv-doc-html.zip"))
            {
                url = assetObject.value(QStringLiteral("browser_download_url")).toString();
                stamp = assetObject.value(QStringLiteral("updated_at")).toString();
                break;
            }
        }

        if((!url.isEmpty()) && ((!QFileInfo::exists(base + QStringLiteral("/html-dev"))) || (stamp != readDevCacheVersion(base, QStringLiteral("html-dev")))))
        {
            promise.setProgressValueAndText(0, Tr::tr("Downloading development documentation..."));
            QByteArray data = devDownloadSync(QUrl(url), promise);

            if((!data.isEmpty()) && installDevArchive(data, false, QStringLiteral("html-dev"), base, python))
            {
                writeDevCacheVersion(base, QStringLiteral("html-dev"), stamp);
                out.docsStamp = stamp;
            }
        }
    }

    if((parts & OpenMVPlugin::DevFirmware) && (!promise.isCanceled()))
    {
        promise.setProgressValueAndText(0, Tr::tr("Checking development firmware..."));
        QJsonObject dev = QJsonDocument::fromJson(devDownloadSync(QUrl(QLatin1String(DEV_MANIFEST_URL)), promise)).object()
            .value(QStringLiteral("firmware")).toObject().value(QStringLiteral("development")).toObject();
        const QString version = dev.value(QStringLiteral("version")).toString();
        const QString url = dev.value(QStringLiteral("url")).toString();
        const QString sha = dev.value(QStringLiteral("sha256")).toString();

        if((!url.isEmpty()) && ((!QFileInfo::exists(base + QStringLiteral("/firmware-dev"))) || (version != readDevCacheVersion(base, QStringLiteral("firmware-dev")))))
        {
            promise.setProgressValueAndText(0, Tr::tr("Downloading the latest development firmware..."));
            QByteArray data = devDownloadSync(QUrl(url), promise);

            if((!data.isEmpty()) && (sha.isEmpty() || (sha256Hex(data) == sha)))
            {
                // Extraction gives no byte-level progress, so flip to an indeterminate
                // bar with an "unpacking" message instead of a frozen 100% download bar.
                // QFuture progress is monotonic, so the value must exceed the download's
                // last byte count or the label change would be dropped; the (0, 0) range
                // makes the bar indeterminate, so the number itself is never shown.
                promise.setProgressRange(0, 0);
                promise.setProgressValueAndText(std::numeric_limits<int>::max(), Tr::tr("Unpacking the latest development firmware..."));

                if(installDevArchive(data, true, QStringLiteral("firmware-dev"), base, python))
                {
                    writeDevCacheVersion(base, QStringLiteral("firmware-dev"), version);
                    out.firmwareVersion = version;
                }
            }
        }
    }

    promise.addResult(out);
}

QString OpenMVPlugin::devResourceFolder(const QString &name) const
{
    if(m_developmentCam)
    {
        const QString dev = name + QStringLiteral("-dev");

        if(Core::ICore::allUsersResourcePath(dev).exists())
        {
            return dev;
        }
    }

    return name;
}

// Kick the worker off the GUI thread. The worker records installed versions on disk
// itself, so nothing needs reading/writing here beyond the paths.
static QFuture<DevSyncOutcome> launchDevSync(int parts)
{
    return Utils::asyncRun(devSyncWorker, parts,
        Core::ICore::allUsersResourcePath().toString(),
        bundledPythonBinary().toString());
}

void OpenMVPlugin::applyDevSyncOutcome(const DevSyncOutcome &out)
{
    // The worker already persisted the versions; the only GUI-thread follow-up is to
    // reload the example filters when the examples were refreshed under a dev cam, so
    // the Examples menu reflects them right away.
    if((!out.examplesVersion.isEmpty()) && m_developmentCam)
    {
        loadExampleFilters(devResourceFolder(QStringLiteral("examples")));
    }
}

void OpenMVPlugin::backgroundSyncDevResources(int parts)
{
    QFuture<DevSyncOutcome> future = launchDevSync(parts);

    QFutureWatcher<DevSyncOutcome> *watcher = new QFutureWatcher<DevSyncOutcome>(this);
    connect(watcher, &QFutureWatcher<DevSyncOutcome>::finished, this, [this, watcher] {
        if((!watcher->future().isCanceled()) && watcher->future().resultCount())
        {
            applyDevSyncOutcome(watcher->result());
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);

    // Runs silently: examples/docs are swapped in once ready (no status-bar UI). The
    // user only waits when an action needs a resource (see syncDevFirmwareBlocking).
}

bool OpenMVPlugin::syncDevFirmwareBlocking()
{
    QFuture<DevSyncOutcome> future = launchDevSync(DevFirmware);

    // Installing dev firmware needs it present now, so show the worker's progress in a
    // modal dialog and wait.
    QProgressDialog dialog(Tr::tr("Downloading the latest development firmware..."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setAttribute(Qt::WA_ShowWithoutActivating);
    // The download hitting 100% would otherwise auto-close/reset the dialog before the
    // worker switches it to the indeterminate "unpacking" phase, leaving it hidden.
    dialog.setAutoClose(false);
    dialog.setAutoReset(false);
    QFutureWatcher<DevSyncOutcome> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<DevSyncOutcome>::finished, &loop, &QEventLoop::quit);
    connect(&watcher, &QFutureWatcher<DevSyncOutcome>::progressRangeChanged, &dialog, &QProgressDialog::setRange);
    connect(&watcher, &QFutureWatcher<DevSyncOutcome>::progressValueChanged, &dialog, &QProgressDialog::setValue);
    connect(&watcher, &QFutureWatcher<DevSyncOutcome>::progressTextChanged, &dialog, &QProgressDialog::setLabelText);
    connect(&dialog, &QProgressDialog::canceled, &watcher, &QFutureWatcher<DevSyncOutcome>::cancel);
    watcher.setFuture(future);

    if(!future.isFinished())
    {
        dialog.show();
        loop.exec();
    }

    dialog.close();

    if((!future.isCanceled()) && future.resultCount())
    {
        applyDevSyncOutcome(future.result());
    }

    return Core::ICore::allUsersResourcePath(QStringLiteral("firmware-dev")).exists();
}

static bool extractAllWrapper(QByteArray *data, const QString &path)
{
    QEventLoop loop;
    QFutureWatcher<bool> watcher;
    QObject::connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(extractAll, data, path));
    loop.exec();
    return watcher.result();
}

MyQSerialPortInfo createInfo(const QString &selectedPort)
{
    QSerialPortInfo info(selectedPort);

    if (info.isNull())
    {
        return MyQSerialPortInfo(); // WiFi Port
    }
    else
    {
        return MyQSerialPortInfo(info); // Serial Port
    }
}

// A network (UDP) port name ("omv-<serial>:ip:port") isn't a serial device, so QSerialPortInfo
// can't resolve it -- the same discriminator OMVPortFactory uses to choose the UDP transport.
bool isNetworkPort(const QString &portName)
{
    return (!portName.isEmpty()) && QSerialPortInfo(portName).isNull();
}

// IDE-local per-camera friendly name, stored in OpenMV/CameraAliases/<key> (key = serial, or the
// port name if the camera has no serial). Case-folded so it's stable; empty alias removes the entry.
QString cameraAlias(const QString &key)
{
    if(key.isEmpty())
    {
        return QString();
    }

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    return settings->value(Utils::keyFromString(QStringLiteral(SETTINGS_GROUP "/" CAMERA_ALIAS_GROUP "/") + key.toLower())).toString();
}

void setCameraAlias(const QString &key, const QString &alias)
{
    if(key.isEmpty())
    {
        return;
    }

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    const Utils::Key settingsKey = Utils::keyFromString(QStringLiteral(SETTINGS_GROUP "/" CAMERA_ALIAS_GROUP "/") + key.toLower());

    if(alias.isEmpty())
    {
        settings->remove(settingsKey);
    }
    else
    {
        settings->setValue(settingsKey, alias);
    }
}

// Find the USB serial port currently enumerating with this serial number (case-insensitive), or a
// null info if none. Ties a wifi-discovered cam back to its USB presence: that USB port is hidden
// from the connect list (its debug endpoint is dead), but the device is still physically here -- so
// we can read its VID/PID for a real board name, and flashing over USB/DFU remains possible.
MyQSerialPortInfo usbPortForSerial(const QString &serialNumber)
{
    if(serialNumber.isEmpty())
    {
        return MyQSerialPortInfo();
    }

    const QString target = serialNumber.toLower();

    for(const QSerialPortInfo &raw_port : QSerialPortInfo::availablePorts())
    {
        MyQSerialPortInfo info(raw_port);

        if(info.serialNumber().toLower() == target)
        {
            return info;
        }
    }

    return MyQSerialPortInfo();
}

bool matchVidPid(const QJsonObject &object, const QString &serialNumberFilter, const MyQSerialPortInfo &port, bool enableSerialNumberInverseFilter = false)
{
    if (port.isNull())
    {
        // This is a WiFi Port - Disable PID/VID matching.
        return true;
    }

    QStringList vidpid = object.value(QStringLiteral("boardVidPid")).toString().split(QStringLiteral(":"));
    int mask = object.value(QStringLiteral("boardPidMask")).toString().toInt(nullptr, 16);
    QStringList serialNumbersInverseFilters;

    for (const QJsonValue &value : object.value(QStringLiteral("boardSerialNumberInverseFilters")).toArray())
    {
        serialNumbersInverseFilters.append(value.toString());
    }

    if((vidpid.size() == 2)
    && (serialNumberFilter.isEmpty() || (serialNumberFilter == port.serialNumber().toUpper()))
    && port.hasVendorIdentifier()
    && port.vendorIdentifier() == vidpid.at(0).toInt(nullptr, 16)
    && port.hasProductIdentifier()
    && (port.productIdentifier() & mask) == vidpid.at(1).toInt(nullptr, 16))
    {
        return enableSerialNumberInverseFilter ? (serialNumbersInverseFilters.isEmpty() || (!serialNumbersInverseFilters.contains(port.serialNumber()))) : true;
    }

    return false;
}

bool validPort(const QJsonDocument &settings, const QString &serialNumberFilter, const MyQSerialPortInfo &port)
{
    for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())
    {
        if (matchVidPid(value.toObject(), serialNumberFilter, port, true))
        {
            return true;
        }
    }

    return false;
}

bool isBootloaderType(const QJsonDocument &settings, int testVid, int testPid, const QString &bootloaderType)
{
    bool match = false;

    for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())
    {
        QStringList list = value.toObject().value(QStringLiteral("boardVidPid")).toString().split(QStringLiteral(":"));

        if (list.size() == 2)
        {
            int vid = list.at(0).toInt(nullptr, 16);
            int pid = list.at(1).toInt(nullptr, 16);

            if ((testVid == vid)
            && ((testPid & value.toObject().value(QStringLiteral("boardPidMask")).toString().toInt(nullptr, 16)) == pid))
            {
                if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == bootloaderType)
                {
                    match = true;
                }
            }
        }

        if (match)
        {
            break;
        }
    }

    return match;
}

void OpenMVPlugin::packageUpdate()
{
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);

    connect(manager, &QNetworkAccessManager::finished, this, [this, manager] (QNetworkReply *reply) {

        QByteArray data = reply->readAll();

        if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
        {
            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^(\\d+)\\.(\\d+)\\.(\\d+)$")).match(QString::fromUtf8(data).trimmed());

            if(match.hasMatch())
            {
                int new_major = match.captured(1).toInt();
                int new_minor = match.captured(2).toInt();
                int new_patch = match.captured(3).toInt();

                int old_major = 0;
                int old_minor = 0;
                int old_patch = 0;
                QJsonObject resourcesSettings;

                QFile resourcesSettingsFile(Core::ICore::allUsersResourcePath(QStringLiteral("../%1.json").arg(Core::Constants::IDE_CASED_ID)).toString());

                if (resourcesSettingsFile.open(QFile::ReadOnly))
                {
                    resourcesSettings = QJsonDocument::fromJson(resourcesSettingsFile.readAll()).object();
                    resourcesSettingsFile.close();

                    old_major = resourcesSettings.value(QStringLiteral(RESOURCES_MAJOR)).toInt();
                    old_minor = resourcesSettings.value(QStringLiteral(RESOURCES_MINOR)).toInt();
                    old_patch = resourcesSettings.value(QStringLiteral(RESOURCES_PATCH)).toInt();
                }

                if((old_major < new_major)
                || ((old_major == new_major) && (old_minor < new_minor))
                || ((old_major == new_major) && (old_minor == new_minor) && (old_patch < new_patch)))
                {
                    const QString updateMessage =
                        Tr::tr("New %2 resources are available (e.g. examples, firmware, documentation, etc.). See the <a href=\"%L1\">release notes</a>.")
                            .arg(webChangelogUrl(QStringLiteral("ide"), new_major, new_minor, new_patch).toString())
                            .arg(QGuiApplication::applicationDisplayName());
                    QMessageBox box(QMessageBox::Information, Tr::tr("Update Available"), updateMessage, QMessageBox::Cancel, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    box.setTextFormat(Qt::RichText);
                    QPushButton *button = box.addButton(Tr::tr("Install"), QMessageBox::AcceptRole);
                    box.setDefaultButton(button);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    if(box.clickedButton() == button)
                    {
                        QProgressDialog *dialog = new QProgressDialog(Tr::tr("Downloading..."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                        dialog->setWindowModality(Qt::ApplicationModal);
                        dialog->setAttribute(Qt::WA_ShowWithoutActivating);
                        dialog->setAutoClose(false);

                        QNetworkAccessManager *manager2 = new QNetworkAccessManager(this);

                        connect(manager2, &QNetworkAccessManager::finished, this, [this, manager2, new_major, new_minor, new_patch, dialog] (QNetworkReply *reply2) {
                            QByteArray data2 = reply2->error() == QNetworkReply::NoError ? reply2->readAll() : QByteArray();

                            if((reply2->error() == QNetworkReply::NoError) && (!data2.isEmpty()))
                            {
                                dialog->setLabelText(Tr::tr("Installing..."));
                                dialog->setRange(0, 0);
                                dialog->setCancelButton(Q_NULLPTR);

                                QJsonObject resourcesSettings;

                                QFile resourcesSettingsFile(Core::ICore::allUsersResourcePath(QStringLiteral("../%1.json").arg(Core::Constants::IDE_CASED_ID)).toString());

                                if (resourcesSettingsFile.open(QFile::ReadOnly))
                                {
                                    resourcesSettings = QJsonDocument::fromJson(resourcesSettingsFile.readAll()).object();
                                    resourcesSettingsFile.close();
                                }

                                resourcesSettings[QStringLiteral(RESOURCES_MAJOR)] = 0;
                                resourcesSettings[QStringLiteral(RESOURCES_MINOR)] = 0;
                                resourcesSettings[QStringLiteral(RESOURCES_PATCH)] = 0;

                                bool ok = true;

                                if (resourcesSettingsFile.open(QFile::WriteOnly))
                                {
                                    QByteArray data = QJsonDocument(resourcesSettings).toJson();

                                    if (resourcesSettingsFile.write(data) != data.size())
                                    {
                                        QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing %1's application data and then restart %1!").arg(QGuiApplication::applicationDisplayName()));
                                        QApplication::quit();
                                        ok = false;
                                    }

                                    resourcesSettingsFile.close();
                                }
                                else
                                {
                                    QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing %1's application data and then restart %1!").arg(QGuiApplication::applicationDisplayName()));
                                    QApplication::quit();
                                    ok = false;
                                }

                                if (ok)
                                {
                                    QString error;

                                    if(!removeRecursivelyWrapper(Core::ICore::allUsersResourcePath(), m_resourceFoldersToDelete, &error))
                                    {
                                        QMessageBox::critical(Core::ICore::dialogParent(),
                                            QString(),
                                            error + Tr::tr("\n\nPlease close any programs that are viewing/editing %1's application data and then restart %1!").arg(QGuiApplication::applicationDisplayName()));

                                        QApplication::quit();
                                        ok = false;
                                    }
                                    else
                                    {
                                        if(!(m_viewerMode
                                            ? extractFolderWrapper(&data2, Core::ICore::allUsersResourcePath().toString(), QStringLiteral("firmware"))
                                            : extractAllWrapper(&data2, Core::ICore::allUsersResourcePath().toString())))
                                        {
                                            QMessageBox::critical(Core::ICore::dialogParent(),
                                                QString(),
                                                Tr::tr("Please close any programs that are viewing/editing %1's application data and then restart %1!").arg(QGuiApplication::applicationDisplayName()));

                                            QApplication::quit();
                                            ok = false;
                                        }
                                    }

                                    if(ok)
                                    {
                                        resourcesSettings[QStringLiteral(RESOURCES_MAJOR)] = new_major;
                                        resourcesSettings[QStringLiteral(RESOURCES_MINOR)] = new_minor;
                                        resourcesSettings[QStringLiteral(RESOURCES_PATCH)] = new_patch;

                                        if (resourcesSettingsFile.open(QFile::WriteOnly))
                                        {
                                            QByteArray data = QJsonDocument(resourcesSettings).toJson();

                                            if (resourcesSettingsFile.write(data) == data.size())
                                            {
                                                resourcesSettingsFile.close();

                                                Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

                                                // Keep backwards compatibility with old versions of OpenMV IDE.
                                                settings->setValue(
                                                    SETTINGS_GROUP "/" RESOURCES_MAJOR,
                                                    resourcesSettings.value(QStringLiteral(RESOURCES_MAJOR)).toInt());
                                                settings->setValue(
                                                    SETTINGS_GROUP "/" RESOURCES_MINOR,
                                                    resourcesSettings.value(QStringLiteral(RESOURCES_MINOR)).toInt());
                                                settings->setValue(
                                                    SETTINGS_GROUP "/" RESOURCES_PATCH,
                                                    resourcesSettings.value(QStringLiteral(RESOURCES_PATCH)).toInt());
                                                settings->sync();

                                                if (loadDocs(true, false))
                                                {
                                                    QMessageBox::information(Core::ICore::dialogParent(),
                                                        QString(),
                                                        Tr::tr("Installation Sucessful! Please restart %1.").arg(QGuiApplication::applicationDisplayName()));

                                                    Core::ICore::restart();
                                                }
                                                else
                                                {
                                                    QApplication::quit();
                                                }
                                            }
                                            else
                                            {
                                                resourcesSettingsFile.close();

                                                QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing %1's application data and then restart %1!").arg(QGuiApplication::applicationDisplayName()));
                                                QApplication::quit();
                                            }
                                        }
                                        else
                                        {
                                            QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing %1's application data and then restart %1!").arg(QGuiApplication::applicationDisplayName()));
                                            QApplication::quit();
                                        }
                                    }
                                }
                            }
                            else if((reply2->error() != QNetworkReply::NoError) && (reply2->error() != QNetworkReply::OperationCanceledError))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Package Update"),
                                    Tr::tr("Error: %L1!").arg(reply2->errorString()));
                            }
                            else if(reply2->error() != QNetworkReply::OperationCanceledError)
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Package Update"),
                                    Tr::tr("Cannot open the resources file \"%L1\"!").arg(reply2->request().url().toString()));
                            }

                            connect(reply2, &QNetworkReply::destroyed, manager2, &QNetworkAccessManager::deleteLater); reply2->deleteLater();
                            dialog->close();
                            dialog->deleteLater();
                        });

                        QNetworkRequest request2 = QNetworkRequest(QUrl(QStringLiteral("https://github.com/openmv/openmv-ide/releases/download/v%1.%2.%3/openmv-ide-resources-%1.%2.%3.zip").arg(new_major).arg(new_minor).arg(new_patch)));
                        QNetworkReply *reply2 = manager2->get(request2);
                        QPointer<QProgressDialog> dlg = dialog;

                        if(reply2)
                        {
                            connect(dialog, &QProgressDialog::canceled, reply2, &QNetworkReply::abort);
                            connect(reply2, &QNetworkReply::sslErrors, reply2, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
                            connect(reply2, &QNetworkReply::downloadProgress, dialog, [dlg] (qint64 bytesReceived, qint64 bytesTotal) {
                                if (!dlg) return;
                                dlg->setMaximum((bytesTotal > 0) ? bytesTotal : 0);
                                dlg->setValue(bytesReceived);
                            });

                            dialog->show();
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Package Update"),
                                Tr::tr("Network request failed \"%L1\"!").arg(request2.url().toString()));
                        }
                    }
                }
            }
        }

        connect(reply, &QNetworkReply::destroyed, manager, &QNetworkAccessManager::deleteLater); reply->deleteLater();
    });

    QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("https://raw.githubusercontent.com/openmv/openmv-ide-version/main/openmv-ide-resources-version-v2.txt")));
    QNetworkReply *reply = manager->get(request);

    if(reply)
    {
        connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
    }
}

void OpenMVPlugin::bootloaderClicked()
{
    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setWindowTitle(Tr::tr("Load Custom Firmware"));
    QFormLayout *layout = new QFormLayout(dialog);
    layout->setVerticalSpacing(0);

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

    Utils::PathChooser *pathChooser = new Utils::PathChooser();
    pathChooser->setExpectedKind(Utils::PathChooser::File);
    pathChooser->setPromptDialogTitle(Tr::tr("Firmware Path"));
    pathChooser->setPromptDialogFilter(Tr::tr("Firmware Binary (*.bin *.dfu *.img *.zip)"));
    pathChooser->setFilePath(Utils::FilePath::fromVariant(
        settings->value(SETTINGS_GROUP "/" LAST_FIRMWARE_PATH, QDir::homePath())));
    pathChooser->setHistoryCompleter(LAST_FIRMWARE_HISTORY, false);
    layout->addRow(Tr::tr("Firmware Path"), pathChooser);
    layout->addItem(new QSpacerItem(0, 6));

    QHBoxLayout *layout2 = new QHBoxLayout;
    layout2->setContentsMargins(0, 0, 0, 0);
    QWidget *widget = new QWidget;
    widget->setLayout(layout2);

    QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
    checkBox->setChecked(settings->value(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
    layout2->addWidget(checkBox);
    checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                "This does not erase files on any removable SD card (if inserted)."));

    QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Reset ROMFS file system"));
    checkBox2->setChecked(settings->value(SETTINGS_GROUP "/" LAST_DFU_RESET_ROM_FS_STATE, false).toBool());
    layout2->addWidget(checkBox2);
    checkBox2->setEnabled(!pathChooser->filePath().toString().endsWith(QStringLiteral(".img"), Qt::CaseInsensitive));
    checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be reset back to default."));

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *run = new QPushButton(Tr::tr("Run"));
    run->setEnabled(pathChooser->isValid());
    box->addButton(run, QDialogButtonBox::AcceptRole);
    layout2->addSpacing(80);
    layout2->addWidget(box);
    layout->addRow(widget);

    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(pathChooser, &Utils::PathChooser::validChanged, run, &QPushButton::setEnabled);
    connect(pathChooser, &Utils::PathChooser::rawPathChanged, this, [this, dialog, pathChooser, checkBox2] () {
        if(pathChooser->filePath().toString().endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
        {
            checkBox2->setEnabled(false);
        }
        else
        {
            checkBox2->setEnabled(true);
        }

        QTimer::singleShot(0, this, [dialog] { dialog->adjustSize(); });
    });

    if(dialog->exec() == QDialog::Accepted)
    {
        QString forceFirmwarePath = pathChooser->filePath().toString();
        bool flashFSErase = checkBox->isChecked();
        bool resetROMFS = checkBox2->isEnabled() ? checkBox2->isChecked() : false;

        if(QFileInfo::exists(forceFirmwarePath) && QFileInfo(forceFirmwarePath).isFile())
        {
            settings->setValue(SETTINGS_GROUP "/" LAST_FIRMWARE_PATH, forceFirmwarePath);
            settings->setValue(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, flashFSErase);
            settings->setValue(SETTINGS_GROUP "/" LAST_DFU_RESET_ROM_FS_STATE, resetROMFS);
            delete dialog;

            if(forceFirmwarePath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
            {
                // A firmware .zip is the build output for one board -- the firmware image(s) sit flat
                // at the zip root (firmware.bin, romfs0.img, ...), i.e. the contents of one arch dir
                // of the dev bundle. Unpack it flat and install via the dev-firmware code path, which
                // picks the image for the connected board and injects the AE3 .lst. Pass the unpacked
                // bundle dir as customFirmwareBundleDir so it flashes from the zip instead of
                // downloading.
                QFile zipFile(forceFirmwarePath);
                QByteArray zipData = zipFile.open(QIODevice::ReadOnly) ? zipFile.readAll() : QByteArray();
                const QString bundleDir = QDir::tempPath() + QStringLiteral("/openmv-custom-fw");
                QDir(bundleDir).removeRecursively();

                if((!zipData.isEmpty()) && QDir().mkpath(bundleDir) && extractZipToDir(zipData, bundleDir))
                {
                    // If the zip ships a romfs image it must be applied -- force the reset regardless
                    // of the checkbox (the user may forget it). Otherwise honor the checkbox, which
                    // then falls back to the released romfs.
                    QDirIterator romfsIt(bundleDir, QStringList{QStringLiteral("romfs*.img")},
                                         QDir::Files, QDirIterator::Subdirectories);
                    const bool zipHasRomfs = romfsIt.hasNext();

                    connectClicked(true, QString(), flashFSErase, false, true, false, QString(),
                                   (zipHasRomfs || resetROMFS) ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE,
                                   false, bundleDir);
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Bootloader"),
                        Tr::tr("Unable to unpack the firmware zip \"%L1\"!").arg(forceFirmwarePath));
                }
            }
            else
            {
                connectClicked(true, forceFirmwarePath, flashFSErase,
                               false, false, false, QString(), resetROMFS ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE);
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Bootloader"),
                Tr::tr("\"%L1\" is not a file!").arg(forceFirmwarePath));

            delete dialog;
        }
    }
    else
    {
        delete dialog;
    }
}

void OpenMVPlugin::installTheLatestDevelopmentRelease()
{
    // A third-party board pulls dev firmware from its own vendor's channel and
    // has no OpenMV changelog to scrape -- open the options dialog directly.
    if(!m_boardResourceRoot.isEmpty())
    {
        showDevelopmentReleaseDialog(QByteArray());
        return;
    }

    QProgressDialog *dialog = new QProgressDialog(Tr::tr("Downloading..."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setAttribute(Qt::WA_ShowWithoutActivating);
    dialog->setAutoClose(false);

    QNetworkAccessManager *manager2 = new QNetworkAccessManager(this);

    connect(manager2, &QNetworkAccessManager::finished, this, [this, manager2, dialog] (QNetworkReply *reply2) {
        // The download is finished, so dismiss this progress dialog now -- before the
        // changelog dialog (and the connect/firmware flow it launches) open on top of it.
        // Otherwise it lingers hidden behind them and only closes as the nested flow
        // unwinds, which looks like a stray dialog flashing closed.
        dialog->close();

        QByteArray data2 = reply2->error() == QNetworkReply::NoError ? reply2->readAll() : QByteArray();

        if((reply2->error() == QNetworkReply::NoError) && (!data2.isEmpty()))
        {
            int s = data2.indexOf("<div data-pjax=\"true\" data-test-selector=\"body-content\" data-view-component=\"true\" class=\"markdown-body my-3\">");
            if (s == -1) s = data2.indexOf("<div data-pjax=\"true\" data-test-selector=\"body-content\" data-view-component=\"true\" class=\"markdown-body tmp-my-3\">");

            int e = data2.indexOf("<div data-view-component=\"true\" class=\"Box-footer\">");

            QByteArray d;

            if((s != -1) and (e != -1))
            {
                d = data2.mid(s, e - s);
                int i = d.lastIndexOf("</div>");
                d = d.mid(0, i - 1);
            }

            showDevelopmentReleaseDialog(d);
        }
        else if((reply2->error() != QNetworkReply::NoError) && (reply2->error() != QNetworkReply::OperationCanceledError))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Error: %L1!").arg(reply2->errorString()));
        }
        else if(reply2->error() != QNetworkReply::OperationCanceledError)
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Cannot open the resources file \"%L1\"!").arg(reply2->request().url().toString()));
        }

        connect(reply2, &QNetworkReply::destroyed, manager2, &QNetworkAccessManager::deleteLater); reply2->deleteLater();
        dialog->deleteLater();
    });

    QNetworkRequest request2 = QNetworkRequest(QUrl(QStringLiteral("https://github.com/openmv/openmv/releases/tag/development")));
    QNetworkReply *reply2 = manager2->get(request2);
    QPointer<QProgressDialog> dlg = dialog;

    if(reply2)
    {
        connect(dialog, &QProgressDialog::canceled, reply2, &QNetworkReply::abort);
        connect(reply2, &QNetworkReply::sslErrors, reply2, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
        connect(reply2, &QNetworkReply::downloadProgress, dialog, [dlg] (qint64 bytesReceived, qint64 bytesTotal) {
            if (!dlg) return;
            dlg->setMaximum((bytesTotal > 0) ? bytesTotal : 0);
            dlg->setValue(bytesReceived);
        });

        dialog->exec();
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            Tr::tr("Network request failed \"%L1\"!").arg(request2.url().toString()));
    }
}

// The "Install the Latest Development Release" options dialog (erase FAT /
// update ROMFS / force bootloader), factored out so both the OpenMV flow (after
// scraping the changelog HTML, passed as changelogHtml) and a third-party board
// (empty changelogHtml, its dev firmware coming from the vendor's own channel)
// share it. On accept it runs the dev-firmware connect flow.
void OpenMVPlugin::showDevelopmentReleaseDialog(const QByteArray &changelogHtml)
{
    {
        const QByteArray d = changelogHtml;

        {
            QDialog *newDialog = new QDialog(Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            newDialog->setWindowTitle(Tr::tr("Bootloader"));
            QFormLayout *layout = new QFormLayout(newDialog);

            if(!d.isEmpty())
            {
                QTextEdit *edit = new QTextEdit(QString::fromUtf8(d));
                edit->setReadOnly(true);
                layout->addWidget(edit);
            }

            Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

            QHBoxLayout *layout2 = new QHBoxLayout;
            layout2->setContentsMargins(0, 0, 0, 0);
            QWidget *widget = new QWidget;
            widget->setLayout(layout2);

            QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
            checkBox->setChecked(settings->value(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
            layout2->addWidget(checkBox);
            checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                        "This does not erase files on any removable SD card (if inserted)."));

            QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Update ROMFS file system"));
            checkBox2->setChecked(settings->value(SETTINGS_GROUP "/" LAST_DFU_UPDATE_ROM_FS_STATE, false).toBool());
            layout2->addWidget(checkBox2);
            checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be updated to the latest development release."));

            QCheckBox *checkBox3 = new QCheckBox(Tr::tr("Force bootloader"));
            layout2->addWidget(checkBox3);
            checkBox3->setToolTip(Tr::tr("Force enter the OpenMV Cam bootloader. May result in the OpenMV Cam bootloader not automatically exiting on older boards."));

            for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
            {
                QJsonObject object = value.toObject();

                if ((m_boardType.toLower() == object.value(QStringLiteral("boardType")).toString().toLower()) &&
                    (!object.value(QStringLiteral("hidden")).toBool()) &&
                    (object.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu")))
                {
                    QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                    checkBox3->setChecked(bootloaderSettings.value(QStringLiteral("forceBootloaderDefaultSafe")).toBool());
                    break;
                }
            }

            if((m_major < OPENMV_FORCE_ROMFS_UPGRADE_MAJOR)
            || ((m_major == OPENMV_FORCE_ROMFS_UPGRADE_MAJOR) && (m_minor < OPENMV_FORCE_ROMFS_UPGRADE_MINOR))
            || ((m_major == OPENMV_FORCE_ROMFS_UPGRADE_MAJOR) && (m_minor == OPENMV_FORCE_ROMFS_UPGRADE_MINOR) && (m_patch < OPENMV_FORCE_ROMFS_UPGRADE_PATCH)))
            {
                checkBox->setChecked(true);
                checkBox->setEnabled(false);
                checkBox2->setChecked(true);
                checkBox2->setEnabled(false);
                checkBox3->setChecked(false);
                checkBox3->setEnabled(false);

                layout->addRow(new QLabel(Tr::tr("Warning: Upgrading to the new firmware version requires the FAT file system to be erased.")));
            }

            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
            QPushButton *run = new QPushButton(Tr::tr("Run"));
            box->addButton(run, QDialogButtonBox::AcceptRole);
            layout2->addSpacing(80);
            layout2->addWidget(box);
            layout->addRow(widget);

            connect(box, &QDialogButtonBox::accepted, newDialog, &QDialog::accept);
            connect(box, &QDialogButtonBox::rejected, newDialog, &QDialog::reject);

            if(newDialog->exec() == QDialog::Accepted)
            {
                bool flashFSErase = checkBox->isChecked();
                bool updateROMFS = checkBox2->isChecked();
                bool forceBootloaderEntry = checkBox3->isChecked();

                if (checkBox->isEnabled())
                    settings->setValue(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, flashFSErase);
                if (checkBox2->isEnabled())
                    settings->setValue(SETTINGS_GROUP "/" LAST_DFU_UPDATE_ROM_FS_STATE, updateROMFS);
                delete newDialog;

                connectClicked(true, QString(), flashFSErase, false, true,
                               false, QString(), updateROMFS ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE, forceBootloaderEntry);
            }
            else
            {
                delete newDialog;
            }
        }
    }
}

bool OpenMVPlugin::getTheLatestDevelopmentFirmware(const QString &arch, QString *path, const QString &firmwareFileName, const QString &originalFirmwareFolder, const QString &customBundleDir)
{
    // Stage into a private per-flash subdirectory instead of the shared %TEMP%
    // root. The bootloaders find the staged firmware by the full path returned
    // in *path and its romfs images via QFileInfo(path).path(), so an isolated
    // directory is transparent to them -- but it keeps the multi-minute flash's
    // loose firmware.bin/romfs*.img out of a directory the ROMFS editor also
    // dumps into (whose stale images the bootloader would otherwise prefer) and
    // that unrelated processes churn, and makes the romfs*.img wildcard-clear
    // below safe (it can only ever touch our own staged files). Recreated fresh
    // each flash so nothing stale survives.
    const QString stagingDir = QDir::cleanPath(QDir::tempPath() + QStringLiteral("/openmv-fw-staging"));
    QDir(stagingDir).removeRecursively();
    QDir().mkpath(stagingDir);

    const QString tempTarget = QDir::cleanPath(stagingDir + QDir::separator() + firmwareFileName);
    QFile::remove(tempTarget);

    // "Load Custom Firmware" with a local .zip unpacks it and passes the bundle dir as customBundleDir.
    // When set, flash from it and skip the network sync. The .lst branch below still pulls the listing
    // from the released firmware resources, so the IDE-injected AE3 .lst keeps working -- only its
    // binaries come from the zip.
    const bool useCustom = !customBundleDir.isEmpty();

    // A connected third-party board (its _resourceRoot became m_boardResourceRoot)
    // pulls development firmware from its own vendor's channel, not OpenMV's.
    const bool useThirdParty = (!useCustom) && (!m_boardResourceRoot.isEmpty());
    Utils::FilePath thirdPartyDevDir;

    // Both the .lst and single-file paths flash from the cached dev firmware bundle.
    // syncDevFirmwareBlocking() re-downloads the 54.7 MB bundle only when the dev version
    // actually changed (with a modal progress dialog), so repeat installs don't re-fetch.
    if(useThirdParty)
    {
        QString error;
        thirdPartyDevDir = OpenMVThirdParty::syncDevChannelBlocking(
            OpenMVThirdParty::repoForFirmwareRoot(m_boardResourceRoot), &error, Core::ICore::dialogParent());

        if(thirdPartyDevDir.isEmpty())
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                error.isEmpty() ? Tr::tr("Unable to download the latest development firmware!") : error);

            return false;
        }
    }
    else if((!useCustom) && (!syncDevFirmwareBlocking()))
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            Tr::tr("Unable to download the latest development firmware!"));

        return false;
    }

    Utils::FilePath cachedDir;

    if(useThirdParty)
    {
        cachedDir = thirdPartyDevDir.pathAppended(arch);
    }
    else if(useCustom)
    {
        // A custom .zip is the build output for one board. Tolerate both a flat zip (images at the
        // root, e.g. firmware.bin/romfs0.img) and a dev-bundle-style layout (images under <arch>/),
        // either of which may be wrapped in one top-level directory. Resolve the directory that
        // actually holds this board's images.
        const Utils::FilePath root = Utils::FilePath::fromString(customBundleDir);
        const QStringList subs = QDir(root.toString()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        if(root.pathAppended(arch).exists())
            cachedDir = root.pathAppended(arch);                                    // <root>/<arch>
        else if((subs.size() == 1) && root.pathAppended(subs.first()).pathAppended(arch).exists())
            cachedDir = root.pathAppended(subs.first()).pathAppended(arch);         // <root>/<wrapper>/<arch>
        else if(subs.size() == 1)
            cachedDir = root.pathAppended(subs.first());                            // <root>/<wrapper> (flat inside)
        else
            cachedDir = root;                                                       // flat at the root
    }
    else
    {
        cachedDir = Core::ICore::allUsersResourcePath(QStringLiteral("firmware-dev")).pathAppended(arch);
    }

    // A .lst is flashed as the set of binaries it names, which the bootloader resolves relative
    // to the .lst's own directory. The .lst itself isn't shipped in the dev bundle, so take it
    // from the released firmware (as before) -- but stage the dev binaries it references into the
    // same temp directory, or the bootloader finds the listing with none of its files (the old
    // code extracted the whole dev zip here; this restores that for the cached bundle).
    if(firmwareFileName.endsWith(QStringLiteral("lst")))
    {
        const QString tempDir = QFileInfo(tempTarget).path();

        if(cachedDir.exists())
        {
            for(const QFileInfo &info : QDir(cachedDir.toString()).entryInfoList(QDir::Files))
            {
                const QString dst = tempDir + QDir::separator() + info.fileName();
                QFile::remove(dst);

                if(!QFile(info.absoluteFilePath()).copy(dst))
                {
                    return false;
                }
            }
        }

        QFile::remove(tempTarget); // in case a dev binary shared the .lst's name
        *path = tempTarget;
        return QFile(firmwareResourcePath()
            .pathAppended(originalFirmwareFolder)
            .pathAppended(firmwareFileName).toString()).copy(tempTarget);
    }

    // Stage the bundle's romfs image(s) next to the firmware so the bootloader's romfs-reset
    // step -- which, for a dev/custom install, looks for romfsN.img in the firmware's own
    // directory -- finds them. This must happen for the dev bundle too, not just a custom .zip:
    // the ROMFS editor dumps the camera's romfs into the same temp directory, and without fresh
    // staging the bootloader "prefers" that stale full-partition dump over the downloaded image.
    // Clear any stale romfs images first for the same reason (also covers a bundle that ships no
    // romfs -- the bootloader then falls back to the released image instead of a stale dump).
    // (The .lst path above already stages everything; this covers the common single-firmware.bin
    // boards, whose bundle carries romfs0.img alongside firmware.bin.)
    {
        const QString tempDir = QFileInfo(tempTarget).path();

        for(const QFileInfo &info : QDir(tempDir).entryInfoList(QStringList{QStringLiteral("romfs*.img")}, QDir::Files))
        {
            QFile::remove(info.absoluteFilePath());
        }

        for(const QFileInfo &info : QDir(cachedDir.toString()).entryInfoList(QStringList{QStringLiteral("*.img")}, QDir::Files))
        {
            const QString dst = tempDir + QDir::separator() + info.fileName();
            QFile::remove(dst);
            QFile(info.absoluteFilePath()).copy(dst);
        }
    }

    const Utils::FilePath cached = cachedDir.pathAppended(firmwareFileName);

    if(!cached.exists())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            Tr::tr("The development firmware for this board is not available!"));

        return false;
    }

    // Copy into the temp file the caller flashes from (and may delete afterward),
    // so the cache itself is never consumed.
    *path = tempTarget;
    return QFile(cached.toString()).copy(tempTarget);
}

QString OpenMVPlugin::viewerModeFirmwareText(const QString &detailed, const QString &viewerAction) const
{
    if(!m_viewerMode)
    {
        return detailed;
    }

    return Tr::tr("Update complete!\n\n")
        + (viewerAction.isEmpty() ? QString() : (viewerAction + QStringLiteral("\n\n")))
        + Tr::tr("Please wait for the device to finish restarting. This can take a little while.");
}

QList<QPair<QString, QString> > OpenMVPlugin::querySerialPorts(const QStringList &portList)
{
    QList<QPair<QString, QString> > results;

    for (const QString &port : portList)
    {
        if(QSerialPortInfo(port).isNull())
        {
            // Network (wifi) port -- not a serial device, so don't open/probe it just to label the
            // dialog entry (that would stall on the UDP link). If the cam is also on USB, name it
            // from that (hidden) USB port's VID/PID so it reads like the real board; otherwise fall
            // back to a generic name (board identity still resolves over the protocol on connect).
            QString label = Tr::tr("Unknown Board (Wi-Fi)");

            for(const wifiPort_t &wifiPort : qAsConst(m_availableWifiPorts))
            {
                if(QStringLiteral("%1:%2").arg(wifiPort.name, wifiPort.addressAndPort) != port)
                {
                    continue;
                }

                MyQSerialPortInfo usb = usbPortForSerial(wifiPort.serialNumber);

                if(!usb.isNull())
                {
                    for(const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        if(matchVidPid(value.toObject(), QString(), usb))
                        {
                            label = Tr::tr("%1 (Wi-Fi)").arg(value.toObject().value(QStringLiteral("boardDisplayName")).toString());
                            break;
                        }
                    }
                }

                break;
            }

            results.append(QPair<QString, QString>(port, label));
            continue;
        }

        MyQSerialPortInfo tempInfo = createInfo(port);

        bool found = false;

        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            if(matchVidPid(value.toObject(), QString(), tempInfo))
            {
                results.append(QPair<QString, QString>(port, value.toObject().value(QStringLiteral("boardDisplayName")).toString()));
                found = true;
                break;
            }
        }

        if (found)
        {
            continue;
        }

        QString errorMessage2 = QStringLiteral("Timeout");
        QString *errorMessage2Ptr = &errorMessage2;

        QMetaObject::Connection conn = connect(m_ioport, &OpenMVPluginSerialPort::openResult,
            this, [errorMessage2Ptr] (const QString &errorMessage) {
            *errorMessage2Ptr = errorMessage;
        });

        QEventLoop loop;

        connect(m_ioport, &OpenMVPluginSerialPort::openResult,
                &loop, &QEventLoop::quit);

        m_ioport->open(port);

        QTimer::singleShot(100, &loop, &QEventLoop::quit);

        loop.exec();

        disconnect(conn);

        if(errorMessage2.isEmpty())
        {
            int major2 = int();
            int minor2 = int();
            int patch2 = int();

            int *major2Ptr = &major2;
            int *minor2Ptr = &minor2;
            int *patch2Ptr = &patch2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::firmwareVersion,
                this, [major2Ptr, minor2Ptr, patch2Ptr] (int major, int minor, int patch) {
                *major2Ptr = major;
                *minor2Ptr = minor;
                *patch2Ptr = patch;
            });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::firmwareVersion,
                    &loop, &QEventLoop::quit);

            m_iodevice->getFirmwareVersion();

            QTimer::singleShot(100, &loop, &QEventLoop::quit);

            loop.exec();

            disconnect(conn);

            if((!major2) && (!minor2) && (!patch2))
            {
                results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
                continue;
            }
            else if((major2 < 0) || (100 < major2) || (minor2 < 0) || (100 < minor2) || (patch2 < 0) || (100 < patch2))
            {
                results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
                continue;
            }

            if(((major2 > OLD_API_MAJOR)
            || ((major2 == OLD_API_MAJOR) && (minor2 > OLD_API_MINOR))
            || ((major2 == OLD_API_MAJOR) && (minor2 == OLD_API_MINOR) && (patch2 >= OLD_API_PATCH))))
            {
                QString arch2 = QString();
                QString *arch2Ptr = &arch2;

                QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::archString,
                    this, [arch2Ptr] (const QString &arch) {
                    *arch2Ptr = arch;
                });

                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::archString,
                        &loop, &QEventLoop::quit);

                m_iodevice->getArchString();

                QTimer::singleShot(100, &loop, &QEventLoop::quit);

                loop.exec();

                disconnect(conn);

                if(!arch2.isEmpty())
                {
                    QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();

                    bool found = false;

                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        if((value.toObject().value(QStringLiteral("boardArchString")).toString().toLower() == temp.toLower())
                        && matchVidPid(value.toObject(), QString(), tempInfo))
                        {
                            results.append(QPair<QString, QString>(port, value.toObject().value(QStringLiteral("boardDisplayName")).toString()));
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
                    }
                }
                else
                {
                    results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
                }
            }
            else
            {
                results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
            }
        }
        else
        {
            results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
        }

        // Cleanup
        {
            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                    &loop, &QEventLoop::quit);

            m_iodevice->close();

            loop.exec();
        }
    }

    return results;
}

// The name to show for a port in the connect list: the user's IDE-local alias for that camera if set
// (keyed by serial, or the port name if it has none), else the real port name. Only the *displayed*
// name changes -- the raw port string is still what actually gets opened.
QString OpenMVPlugin::portDisplayName(const QString &port)
{
    QString serial;

    if(isNetworkPort(port))
    {
        for(const wifiPort_t &wifiPort : qAsConst(m_availableWifiPorts))
        {
            if(QStringLiteral("%1:%2").arg(wifiPort.name, wifiPort.addressAndPort) == port)
            {
                serial = wifiPort.serialNumber;
                break;
            }
        }
    }
    else
    {
        serial = MyQSerialPortInfo(QSerialPortInfo(port)).serialNumber();
    }

    const QString alias = cameraAlias(serial.isEmpty() ? port : serial);
    return alias.isEmpty() ? port : alias;
}

// Prompt for an IDE-local friendly name for the connected camera (keyed by serial, or port name if
// it has no serial), then reflect it on the port button. Shown instead of the serial port name in
// the connect list and the toolbar -- e.g. so a classroom of identical cams is tellable apart.
void OpenMVPlugin::setPortAlias()
{
    if((!m_connected) || m_working)
    {
        return;
    }

    const QString key = m_portDriveSerialNumber.isEmpty() ? m_portName : m_portDriveSerialNumber;

    QDialog dialog(Core::ICore::dialogParent(), Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog.setWindowTitle(Tr::tr("Name Camera"));

    QVBoxLayout *root = new QVBoxLayout(&dialog);

    QFormLayout *form = new QFormLayout;
    form->addRow(Tr::tr("Serial port:"), new QLabel(m_portName, &dialog));

    QLineEdit *aliasEdit = new QLineEdit(cameraAlias(key), &dialog);
    aliasEdit->setClearButtonEnabled(true);
    aliasEdit->setMinimumWidth(220);
    form->addRow(Tr::tr("Name:"), aliasEdit);
    root->addLayout(form);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if(dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const QString alias = aliasEdit->text().trimmed();
    setCameraAlias(key, alias);
    m_portLabel->setText(Tr::tr("Serial Port: %L1").arg(alias.isEmpty() ? m_portName : alias));
}

QPair<QStringList, QStringList> filterPorts(const QJsonDocument &settings,
                                            const QString &serialNumberFilter,
                                            bool forceBootloader,
                                            const QList<wifiPort_t> &availableWifiPorts)
{
    QStringList stringList, dfuDevices;

    // Serials of cams currently reachable over wifi. A cam in wifi-debug mode still enumerates its
    // USB CDC port, but that debug endpoint is dead (the v2 protocol is single-interface), so we
    // hide it here -- matched by serial number. Skipped for bootloader ops, where the USB port must
    // stay available for flashing (and wifi ports aren't offered anyway).
    QStringList wifiSerials;
    if(!forceBootloader)
    {
        for(const wifiPort_t &port : availableWifiPorts)
        {
            if(!port.serialNumber.isEmpty())
            {
                wifiSerials.append(port.serialNumber.toLower());
            }
        }
    }

    for(const QSerialPortInfo &raw_port : QSerialPortInfo::availablePorts())
    {
        MyQSerialPortInfo port(raw_port);

        if(validPort(settings, serialNumberFilter, port))
        {
            if(wifiSerials.contains(port.serialNumber().toLower()))
            {
                continue; // this cam is in wifi-debug mode -- its USB debug port won't respond
            }

            stringList.append(port.portName());
        }
    }

    if(Utils::HostOsInfo::isMacHost())
    {
        stringList = stringList.filter(QStringLiteral("cu"), Qt::CaseInsensitive);
    }

    dfuDevices = picotoolGetDevices() + imxGetAllDevices(settings) + alifGetDevices(settings);

    for(const QString &device : getDevices())
    {
        dfuDevices.append(device);
        QStringList vidpid = device.split(QStringLiteral(",")).first().split(QStringLiteral(":"));

        for(QList<QString>::iterator it = stringList.begin(); it != stringList.end(); )
        {
            QSerialPortInfo raw_info = QSerialPortInfo(*it);
            MyQSerialPortInfo info(raw_info);

            if(info.hasVendorIdentifier()
            && info.vendorIdentifier() == vidpid.at(0).toInt(nullptr, 16)
            && info.hasProductIdentifier()
            && info.productIdentifier() == vidpid.at(1).toInt(nullptr, 16))
            {
                it = stringList.erase(it);
            }
            else
            {
                it++;
            }
        }
    }

    // Move known bootloader serial ports to dfuDevices.
    {
        for(QList<QString>::iterator it = stringList.begin(); it != stringList.end(); )
        {
            QSerialPortInfo raw_info = QSerialPortInfo(*it);
            MyQSerialPortInfo info(raw_info);

            bool vidpidMatch = false, altvidpidMatch = false;

            for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())
            {
                QJsonObject object = value.toObject();
                QString bootloaderType = object.value(QStringLiteral("bootloaderType")).toString();
                if ((bootloaderType == QStringLiteral("picotool")) || (bootloaderType == QStringLiteral("bossac")))
                {
                    QStringList vidpid = object.value(QStringLiteral("bootloaderVidPid")).toString().split(QStringLiteral(":"));
                    QJsonObject bootloaderSettings = object.value(QStringLiteral("bootloaderSettings")).toObject();
                    QStringList altvidpid = bootloaderSettings.value(QStringLiteral("altvidpid")).toString().split(QStringLiteral(":"));

                    if((vidpid.size() == 2)
                    && info.hasVendorIdentifier()
                    && info.vendorIdentifier() == vidpid.at(0).toInt(nullptr, 16)
                    && info.hasProductIdentifier()
                    && info.productIdentifier()== vidpid.at(1).toInt(nullptr, 16))
                    {
                        dfuDevices.append(QString(QStringLiteral("%1:%2").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0')).arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))));
                        vidpidMatch = true;
                    }

                    if((altvidpid.size() == 2)
                    && info.hasVendorIdentifier()
                    && info.vendorIdentifier() == altvidpid.at(0).toInt(nullptr, 16)
                    && info.hasProductIdentifier()
                    && info.productIdentifier()== altvidpid.at(1).toInt(nullptr, 16))
                    {
                        dfuDevices.append(QString(QStringLiteral("%1:%2").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0')).arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))));
                        altvidpidMatch = true;
                    }
                }
            }

            if(vidpidMatch || altvidpidMatch || info.isNull())
            {
                it = stringList.erase(it);
            }
            else
            {
                it++;
            }
        }
    }

    // Network ports are discovered via mDNS, not enumerated as serial ports. Append them
    // only after all the serial/bootloader filtering above so they are never run through the
    // QSerialPortInfo loops -- which would erase each one as a "null" serial port. Each entry is
    // "name:ip:port"; OMVPortFactory builds an OMVNetworkPort from it (a name that isn't a serial port).
    if(!forceBootloader)
    {
        for(const wifiPort_t &port : availableWifiPorts)
        {
            stringList.append(QStringLiteral("%1:%2").arg(port.name, port.addressAndPort));
        }
    }

    return QPair<QStringList, QStringList>(stringList, dfuDevices);
}

void OpenMVPlugin::connectClicked(bool forceBootloader,
                                  QString forceFirmwarePath,
                                  bool forceFlashFSErase,
                                  bool justEraseFlashFs,
                                  bool installTheLatestDevelopmentFirmware,
                                  bool waitForCamera,
                                  QString previousMapping,
                                  OpenMVROMFSAccess romfsAccess,
                                  bool forceBootloaderEntry,
                                  QString customFirmwareBundleDir)
{
    // Latch the operation before any early return so the flag is already set
    // while the internal pre-disconnect / disconnectDone trampoline runs,
    // closing the window auto-reconnect would otherwise race into.
    if(forceBootloader || installTheLatestDevelopmentFirmware || justEraseFlashFs)
    {
        m_firmwareUpdateInProgress = true;
    }

    if(!m_working)
    {
        if(m_connect_disconnect)
        {
            disconnect(m_connect_disconnect);
        }

        if(m_connected)
        {
            m_connect_disconnect = connect(this, &OpenMVPlugin::disconnectDone, this,
                [this, forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera, previousMapping, romfsAccess, forceBootloaderEntry, customFirmwareBundleDir] {
                QTimer::singleShot(0, this, [this, forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera, previousMapping, romfsAccess, forceBootloaderEntry, customFirmwareBundleDir] {
                    connectClicked(forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera, previousMapping, romfsAccess, forceBootloaderEntry, customFirmwareBundleDir);
                });
            });

            QTimer::singleShot(0, this, [this] {disconnectClicked();});

            return;
        }

        m_working = true;

        QStringList problems = usbProblemDeviceNames();

        if (!problems.isEmpty())
        {
            QMessageBox::warning(Core::ICore::dialogParent(),
                                 Tr::tr("Connect"),
                                 Tr::tr("USB problems were detected by your OS with the devices below on this system. "
                                        "Please fix or remove these devices as they will cause connection issues.\n\n%1").
                                 arg(problems.join(QStringLiteral("\n"))));
        }

        QStringList stringList;
        QElapsedTimer waitForCameraTimeout;
        waitForCameraTimeout.start();
        QStringList dfuDevices;

        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

        do
        {
            QPair<QStringList, QStringList> output = filterPorts(m_firmwareSettings, m_serialNumberFilter, forceBootloader, m_availableWifiPorts);
            stringList = output.first;
            dfuDevices = output.second;
            if(waitForCamera && stringList.isEmpty()) QApplication::processEvents();
        }
        while(waitForCamera && stringList.isEmpty() && (!waitForCameraTimeout.hasExpired(10000)));

        QApplication::restoreOverrideCursor();

        Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

        QString selectedPort;
        bool forceBootloaderBricked = false;
        bool previousMappingSet = !previousMapping.isEmpty();
        QString originalFirmwareFolder = QString();
        QString firmwarePath = forceFirmwarePath;
        int originalEraseFlashSectorStart = FLASH_SECTOR_START;
        int originalEraseFlashSectorEnd = FLASH_SECTOR_END;
        int originalEraseFlashSectorAllStart = FLASH_SECTOR_ALL_START;
        int originalEraseFlashSectorAllEnd = FLASH_SECTOR_ALL_END;
        QJsonObject originalFallbackBootloaderSettings = QJsonObject();
        QString originalDfuVidPid = QString();
        bool dfuNoDialogs = false;
        bool repairingBootloader = false;

        if(stringList.isEmpty())
        {
            if(forceBootloader && (!dfuDevices.isEmpty())
            && (m_autoUpdate != QStringLiteral("release"))
            && (m_autoUpdate != QStringLiteral("developement")))
            {
                forceBootloaderBricked = true;

                QMap<QString, QString> mappings;
                QMap<QString, QJsonObject> fallbackBootloaderMappings;
                QMap<QString, QString> vidpidMappings;

                for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                {
                    if (value.toObject().value(QStringLiteral("hidden")).toBool())
                    {
                        QString a = value.toObject().value(QStringLiteral("boardDisplayName")).toString();
                        mappings.insert(a, value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());
                        vidpidMappings.insert(a, value.toObject().value(QStringLiteral("bootloaderVidPid")).toString());

                        if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("internal"))
                        {
                            QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                            fallbackBootloaderMappings.insert(a, bootloaderSettings.value(QStringLiteral("fallbackBootloader")).toObject());
                        }
                        else if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
                        {
                            QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                            fallbackBootloaderMappings.insert(a, bootloaderSettings.value(QStringLiteral("fallbackBootloader")).toObject());
                        }
                        else if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("alif_tools"))
                        {
                            QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                            fallbackBootloaderMappings.insert(a, bootloaderSettings);
                        }
                        else
                        {
                            fallbackBootloaderMappings.insert(a, QJsonObject());
                        }
                    }
                }

                if(!mappings.isEmpty())
                {
                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        QJsonObject object = value.toObject();
                        QString bootloaderType = object.value(QStringLiteral("bootloaderType")).toString();

                        if (object.value(QStringLiteral("hidden")).toBool()
                            && ((bootloaderType == QStringLiteral("picotool")) || (bootloaderType == QStringLiteral("bossac"))))
                        {
                            QString boardFirmwareFolder = object.value(QStringLiteral("boardFirmwareFolder")).toString();
                            QJsonObject bootloaderSettings = object.value(QStringLiteral("bootloaderSettings")).toObject();
                            QString altvidpidDisplayName = bootloaderSettings.value(QStringLiteral("altvidpidDisplayName")).toString();
                            QString altvidpid = bootloaderSettings.value(QStringLiteral("altvidpid")).toString();
                            QString defaultFirmwareName = object.value(QStringLiteral("defaultFirmwareName")).toString();

                            mappings.insert(altvidpidDisplayName, boardFirmwareFolder);
                            fallbackBootloaderMappings.insert(altvidpidDisplayName, QJsonObject());
                            vidpidMappings.insert(altvidpidDisplayName, altvidpid);
                        }
                    }

                    for(QMap<QString, QString>::iterator it = mappings.begin(); it != mappings.end(); )
                    {
                        bool found = false;

                        for(const QString &device : qAsConst(dfuDevices))
                        {
                            if(device.split(QStringLiteral(",")).first().toLower() == vidpidMappings.value(it.key()).toLower())
                            {
                                found = true;
                                break;
                            }
                        }

                        if(!found)
                        {
                            fallbackBootloaderMappings.remove(it.key());
                            vidpidMappings.remove(it.key());
                            it = mappings.erase(it);
                        }
                        else
                        {
                            it++;
                        }
                    }

                    if(mappings.size())
                    {
                        int index = mappings.keys().indexOf(
                            settings->value(SETTINGS_GROUP "/" LAST_BOARD_TYPE_STATE).toString());

                        bool ok = mappings.size() == 1;
                        QString temp = (mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),
                            Tr::tr("Connect"), Tr::tr("Please select the board type"),
                            mappings.keys(), (index != -1) ? index : 0, false, &ok,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(ok)
                        {
                            settings->setValue(SETTINGS_GROUP "/" LAST_BOARD_TYPE_STATE, temp);

                            originalFirmwareFolder = mappings.value(temp);
                            originalFallbackBootloaderSettings = fallbackBootloaderMappings.value(temp);
                        }
                    }
                }
            }
            else
            {
                bool dfuDeviceResetToRelease = false;
                bool dfuDeviceEraseFlash = false;

                if(!dfuDevices.isEmpty())
                {
                    QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).
                            match(m_firmwareSettings.object().value(QStringLiteral("firmware_version")).toString());

                    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    dialog->setWindowTitle(Tr::tr("Connect"));
                    QFormLayout *layout = new QFormLayout(dialog);
                    layout->setVerticalSpacing(0);

                    layout->addWidget(new QLabel(Tr::tr("A board in DFU mode was detected. What would you like to do?")));
                    layout->addItem(new QSpacerItem(0, 6));

                    QComboBox *combo = new QComboBox();
                    combo->addItem(Tr::tr("Install the latest release firmware (v%L1.%L2.%L3)").arg(match.captured(1).toInt()).arg(match.captured(2).toInt()).arg(match.captured(3).toInt()));
                    combo->addItem(Tr::tr("Load a specific firmware"));
                    combo->addItem(Tr::tr("Just erase the internal FAT file system"));
                    combo->addItem(Tr::tr("Edit the ROM file system"));
                    combo->addItem(Tr::tr("Reset the ROM file system"));
                    combo->setCurrentIndex(settings->value(SETTINGS_GROUP "/" LAST_DFU_ACTION, 0).toInt());
                    layout->addWidget(combo);
                    layout->addItem(new QSpacerItem(0, 6));

                    QHBoxLayout *layout2 = new QHBoxLayout;
                    layout2->setContentsMargins(0, 0, 0, 0);
                    QWidget *widget = new QWidget;
                    widget->setLayout(layout2);

                    QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
                    checkBox->setChecked(
                        settings->value(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
                    layout2->addWidget(checkBox);
                    checkBox->setVisible(combo->currentIndex() == 0);
                    checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                                "This does not erase files on any removable SD card (if inserted)."));

                    QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Reset ROMFS file system"));
                    checkBox2->setChecked(
                        settings->value(SETTINGS_GROUP "/" LAST_DFU_RESET_ROM_FS_STATE, false).toBool());
                    layout2->addWidget(checkBox2);
                    checkBox2->setVisible(combo->currentIndex() == 0);
                    checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be reset back to default."));

                    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                    layout2->addSpacing(80);
                    layout2->addWidget(box);
                    layout->addRow(widget);

                    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
                    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
                    connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, dialog, checkBox, checkBox2] (int index) {
                        checkBox->setVisible(index == 0);
                        checkBox2->setVisible(index == 0);
                        QTimer::singleShot(0, this, [dialog] { dialog->adjustSize(); });
                    });

                    // If a board is in DFU mode we install the release firmware.
                    if((m_autoUpdate == QStringLiteral("release"))
                    || (m_autoUpdate == QStringLiteral("developement")))
                    {
                        combo->setCurrentIndex(0);
                        checkBox->setChecked(m_autoErase);
                    }

                    if((m_autoUpdate == QStringLiteral("release"))
                    || (m_autoUpdate == QStringLiteral("developement"))
                    || (dialog->exec() == QDialog::Accepted))
                    {
                        settings->setValue(SETTINGS_GROUP "/" LAST_DFU_ACTION, combo->currentIndex());
                        settings->setValue(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, checkBox->isChecked());
                        settings->setValue(SETTINGS_GROUP "/" LAST_DFU_RESET_ROM_FS_STATE, checkBox2->isChecked());

                        if(combo->currentIndex() == 0)
                        {
                            dfuDeviceResetToRelease = true;
                            dfuDeviceEraseFlash = checkBox->isChecked();
                            dfuNoDialogs = true;
                            romfsAccess = checkBox2->isChecked() ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE;
                        }
                        else if(combo->currentIndex() == 1)
                        {
                            QTimer::singleShot(0, m_bootloaderAction, &QAction::trigger);
                        }
                        else if(combo->currentIndex() == 2)
                        {
                            QTimer::singleShot(0, m_eraseAction, &QAction::trigger);
                        }
                        else if(combo->currentIndex() == 3)
                        {
                            QTimer::singleShot(0, this, [this] { editRomfsClicked(true); });
                        }
                        else if(combo->currentIndex() == 3)
                        {
                            QTimer::singleShot(0, this, [this] { resetRomfsClicked(); });
                        }
                    }

                    delete dialog;

                    if(!dfuDeviceResetToRelease)
                    {
                        CONNECT_END();
                    }
                }

                if((!forceBootloader) && (!dfuDeviceResetToRelease)) QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("No OpenMV Cams found!"));

                QMap<QString, QString> mappings;
                QMap<QString, QPair<int, int> > eraseMappings;
                QMap<QString, QPair<int, int> > eraseAllMappings;
                QMap<QString, QJsonObject> fallbackBootloaderMappings;
                QMap<QString, QString> vidpidMappings;
                QMap<QString, QString> defaultFirmwareNameMappings;
                QMap<QString, QString> resourceRootMappings; // Third-party board -> vendor firmware dir.

                for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                {
                    if (dfuDeviceResetToRelease || (!value.toObject().value(QStringLiteral("hidden")).toBool()))
                    {
                        QString a = value.toObject().value(QStringLiteral("boardDisplayName")).toString();
                        mappings.insert(a, value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());
                        vidpidMappings.insert(a, value.toObject().value(QStringLiteral("bootloaderVidPid")).toString());
                        defaultFirmwareNameMappings.insert(a, value.toObject().value(QStringLiteral("defaultFirmwareName")).toString());
                        resourceRootMappings.insert(a, value.toObject().value(QStringLiteral("_resourceRoot")).toString());

                        if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("internal"))
                        {
                            QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                            int eraseSectorStart = bootloaderSettings.value(QStringLiteral("eraseSectorStart")).toInt();
                            int eraseSectorEnd = bootloaderSettings.value(QStringLiteral("eraseSectorEnd")).toInt();
                            int eraseAllSectorStart = bootloaderSettings.value(QStringLiteral("eraseAllSectorStart")).toInt();
                            int eraseAllSectorEnd = bootloaderSettings.value(QStringLiteral("eraseAllSectorEnd")).toInt();
                            eraseMappings.insert(a, QPair<int, int>(eraseSectorStart, eraseSectorEnd));
                            eraseAllMappings.insert(a, QPair<int, int>(eraseAllSectorStart, eraseAllSectorEnd));
                            fallbackBootloaderMappings.insert(a, bootloaderSettings.value(QStringLiteral("fallbackBootloader")).toObject());
                        }
                        else if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
                        {
                            QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                            eraseMappings.insert(a, QPair<int, int>(0, 0));
                            eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                            fallbackBootloaderMappings.insert(a, bootloaderSettings.value(QStringLiteral("fallbackBootloader")).toObject());
                        }
                        else if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("alif_tools"))
                        {
                            QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                            eraseMappings.insert(a, QPair<int, int>(0, 0));
                            eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                            fallbackBootloaderMappings.insert(a, bootloaderSettings);
                        }
                        else
                        {
                            eraseMappings.insert(a, QPair<int, int>(0, 0));
                            eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                            fallbackBootloaderMappings.insert(a, QJsonObject());
                        }
                    }
                }

                if(!mappings.isEmpty())
                {
                    if(forceBootloader || dfuDeviceResetToRelease || (QMessageBox::question(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Do you have an OpenMV Cam connected and is it bricked?"),
                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
                    == QMessageBox::Yes))
                    {
                        if(dfuDeviceResetToRelease)
                        {
                            for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                            {
                                QJsonObject object = value.toObject();
                                QString bootloaderType = object.value(QStringLiteral("bootloaderType")).toString();

                                if ((!object.value(QStringLiteral("hidden")).toBool())
                                && ((bootloaderType == QStringLiteral("picotool")) || (bootloaderType == QStringLiteral("bossac"))))
                                {
                                    QString boardFirmwareFolder = object.value(QStringLiteral("boardFirmwareFolder")).toString();
                                    QJsonObject bootloaderSettings = object.value(QStringLiteral("bootloaderSettings")).toObject();
                                    QString altvidpidDisplayName = bootloaderSettings.value(QStringLiteral("altvidpidDisplayName")).toString();
                                    QString altvidpid = bootloaderSettings.value(QStringLiteral("altvidpid")).toString();
                                    QString defaultFirmwareName = object.value(QStringLiteral("defaultFirmwareName")).toString();

                                    mappings.insert(altvidpidDisplayName, boardFirmwareFolder);
                                    eraseMappings.insert(altvidpidDisplayName, QPair<int, int>(0, 0));
                                    eraseAllMappings.insert(altvidpidDisplayName, QPair<int, int>(0, 0));
                                    fallbackBootloaderMappings.insert(altvidpidDisplayName, QJsonObject());
                                    vidpidMappings.insert(altvidpidDisplayName, altvidpid);
                                    defaultFirmwareNameMappings.insert(altvidpidDisplayName, defaultFirmwareName);
                                    resourceRootMappings.insert(altvidpidDisplayName, object.value(QStringLiteral("_resourceRoot")).toString());
                                }
                            }

                            for(QMap<QString, QString>::iterator it = mappings.begin(); it != mappings.end(); )
                            {
                                bool found = false;

                                for(const QString &device : qAsConst(dfuDevices))
                                {
                                    if(device.split(QStringLiteral(",")).first().toLower() == vidpidMappings.value(it.key()).toLower())
                                    {
                                        found = true;
                                        break;
                                    }
                                }

                                if(!found)
                                {
                                    eraseMappings.remove(it.key());
                                    eraseAllMappings.remove(it.key());
                                    fallbackBootloaderMappings.remove(it.key());
                                    vidpidMappings.remove(it.key());
                                    defaultFirmwareNameMappings.remove(it.key());
                                    resourceRootMappings.remove(it.key());
                                    it = mappings.erase(it);
                                }
                                else
                                {
                                    it++;
                                }
                            }
                        }

                        if(mappings.size())
                        {
                            int index = mappings.keys().indexOf(
                                settings->value(SETTINGS_GROUP "/" LAST_BOARD_TYPE_STATE).toString());

                            bool previousMappingAvailable = previousMappingSet && mappings.contains(previousMapping);
                            bool ok = previousMappingAvailable || (mappings.size() == 1);
                            QString temp = previousMappingAvailable ? previousMapping :
                                ((mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),
                                Tr::tr("Connect"), Tr::tr("Please select the board type"),
                                mappings.keys(), (index != -1) ? index : 0, false, &ok,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint)));

                            if(ok)
                            {
                                if (!previousMappingAvailable)
                                    settings->setValue(SETTINGS_GROUP "/" LAST_BOARD_TYPE_STATE, temp);

                                QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                                dialog->setWindowTitle(Tr::tr("Connect"));
                                QFormLayout *layout = new QFormLayout(dialog);
                                layout->setVerticalSpacing(0);

                                layout->addWidget(new QLabel(Tr::tr("Upgrade options:")));
                                layout->addItem(new QSpacerItem(0, 6));

                                QHBoxLayout *layout2 = new QHBoxLayout;
                                layout2->setContentsMargins(6, 0, 0, 0);
                                QWidget *widget = new QWidget;
                                widget->setLayout(layout2);

                                QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
                                checkBox->setChecked(
                                    settings->value(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
                                layout2->addWidget(checkBox);
                                checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                                            "This does not erase files on any removable SD card (if inserted)."));

                                QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Reset ROMFS file system"));
                                checkBox2->setChecked(
                                    settings->value(SETTINGS_GROUP "/" LAST_DFU_RESET_ROM_FS_STATE, false).toBool());
                                layout2->addWidget(checkBox2);
                                checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be reset back to default."));

                                layout->addRow(widget);
                                layout->addItem(new QSpacerItem(0, 6));

                                QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                                layout->addWidget(box);

                                connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
                                connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

                                bool ok = (!forceFirmwarePath.isEmpty()) || justEraseFlashFs ||
                                    (forceBootloader && previousMappingSet) || dfuDeviceResetToRelease || (dialog->exec() == QDialog::Accepted);
                                bool forceFlashFSEraseTemp = justEraseFlashFs ||
                                    ((forceBootloader && previousMappingSet) ? forceFlashFSErase : (dfuDeviceResetToRelease ? dfuDeviceEraseFlash : checkBox->isChecked()));
                                OpenMVROMFSAccess romfsAccessTemp = justEraseFlashFs ? OPENMV_ROMFS_NONE :
                                    ((forceBootloader && previousMappingSet) ? romfsAccess : (dfuDeviceResetToRelease ? romfsAccess : (checkBox2->isChecked() ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE)));

                                if (ok)
                                {
                                    settings->setValue(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE,
                                                       checkBox->isChecked());
                                    settings->setValue(SETTINGS_GROUP "/" LAST_DFU_RESET_ROM_FS_STATE,
                                                       checkBox2->isChecked());
                                    previousMapping = temp;
                                    originalFirmwareFolder = mappings.value(temp);
                                    m_boardResourceRoot = resourceRootMappings.value(temp);
                                    firmwarePath = (resourceRootMappings.value(temp).isEmpty()
                                        ? Core::ICore::allUsersResourcePath(QStringLiteral("firmware"))
                                        : Utils::FilePath::fromString(resourceRootMappings.value(temp)))
                                        .pathAppended(originalFirmwareFolder)
                                        .pathAppended(defaultFirmwareNameMappings.value(temp)).toString();
                                    if (forceBootloader && (!forceFirmwarePath.isEmpty())) firmwarePath = forceFirmwarePath;
                                    forceBootloader = true;
                                    forceFlashFSErase = forceFlashFSEraseTemp;
                                    forceBootloaderBricked = true;
                                    originalEraseFlashSectorStart = eraseMappings.value(temp).first;
                                    originalEraseFlashSectorEnd = eraseMappings.value(temp).second;
                                    originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;
                                    originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;
                                    originalFallbackBootloaderSettings = fallbackBootloaderMappings.value(temp);
                                    originalDfuVidPid = vidpidMappings.value(temp);
                                    romfsAccess = romfsAccessTemp;
                                }

                                delete dialog;
                            }
                        }
                        else
                        {
                            bool end = false;

                            for(const QString &device : qAsConst(dfuDevices))
                            {
                                QStringList vidpid = device.split(QStringLiteral(",")).first().split(QStringLiteral(":"));

                                if(vidpid.size() == 2)
                                {
                                    if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID))
                                    {
                                        QMessageBox::critical(Core::ICore::dialogParent(),
                                            Tr::tr("Connect"),
                                            Tr::tr("Please update the bootloader to the latest version and install the SoftDevice to flash the OpenMV firmware. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                                        end = true;
                                        break;
                                    }

                                    if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID))
                                    {
                                        QMessageBox::critical(Core::ICore::dialogParent(),
                                            Tr::tr("Connect"),
                                            Tr::tr("Please short REC to GND and reset your board. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                                        end = true;
                                        break;
                                    }
                                }
                            }

                            if (!end)
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                                      Tr::tr("Connect"),
                                                      Tr::tr("No released firmware available for the attached board!"));
                            }
                        }
                    }
                }
            }
        }
        else if(stringList.size() == 1)
        {
            selectedPort = stringList.first();
            settings->setValue(SETTINGS_GROUP "/" LAST_SERIAL_PORT_STATE, selectedPort);
        }
        else
        {
            int index = stringList.indexOf(settings->value(SETTINGS_GROUP "/" LAST_SERIAL_PORT_STATE).toString());

            const QList<QPair<QString, QString> > prettyNames = querySerialPorts(stringList);

            QStringList stringList2;

            for (const QPair<QString, QString> &pair : prettyNames)
            {
                // Show the camera's alias (if any) in place of the port name; the board name stays.
                stringList2.append(QStringLiteral("%1: %2").arg(portDisplayName(pair.first), pair.second));
            }

            bool ok;
            QString temp = QInputDialog::getItem(Core::ICore::dialogParent(),
                Tr::tr("Connect"), Tr::tr("Please select a serial port"),
                stringList2, (index != -1) ? index : 0, false, &ok,
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

            if(ok)
            {
                // Map the selected pretty label back to its raw port string. Splitting on ":"
                // breaks network ports whose raw form is "name:ip:port" -- that would drop the
                // ip:port and OMVNetworkPort::open() (which needs all three parts) would fail.
                int selIndex = stringList2.indexOf(temp);
                selectedPort = (selIndex >= 0) ? prettyNames.at(selIndex).first : temp.split(QStringLiteral(":")).first();
                settings->setValue(SETTINGS_GROUP "/" LAST_SERIAL_PORT_STATE, selectedPort);
            }
        }


        if((!forceBootloaderBricked) && selectedPort.isEmpty())
        {
            CONNECT_END();
        }

        QString selectedDfuDevice;

        if(forceBootloaderBricked && (!dfuDevices.isEmpty()))
        {

            if(dfuDevices.size() == 1)
            {
                selectedDfuDevice = dfuDevices.first();
                settings->setValue(SETTINGS_GROUP "/" LAST_DFU_PORT_STATE, selectedDfuDevice);
            }
            else
            {
                int index = dfuDevices.indexOf(settings->value(SETTINGS_GROUP "/" LAST_DFU_PORT_STATE).toString());

                bool ok;
                QString temp = QInputDialog::getItem(Core::ICore::dialogParent(),
                    Tr::tr("Connect"), Tr::tr("Please select a DFU Device"),
                    dfuDevices, (index != -1) ? index : 0, false, &ok,
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                if(ok)
                {
                    selectedDfuDevice = temp;
                    settings->setValue(SETTINGS_GROUP "/" LAST_DFU_PORT_STATE, selectedDfuDevice);
                }
            }

            if(selectedDfuDevice.isEmpty())
            {
                CONNECT_END();
            }

        }

        bool isOldVidPid = false;
        bool isTinyUSBHSV1Protocol = false;
        bool isOpenMVDfu = false;
        bool isIMX = false;
        bool isAlif = false;
        bool isArduinoDFU = false;
        bool isBossac = false;
        bool isPicotool = false;
        bool isTTR = false;
        bool isPortenta = false;
        bool isNiclav = false;
        bool isGiga = false;
        bool isNRF = false;
        bool isRPIPico = false;

        if(!selectedPort.isEmpty())
        {
            QSerialPortInfo raw_arduinoPort = QSerialPortInfo(selectedPort);
            MyQSerialPortInfo arduinoPort(raw_arduinoPort);

            isTTR = isTouchToReset(m_firmwareSettings, arduinoPort);

            if (arduinoPort.hasVendorIdentifier() && arduinoPort.hasProductIdentifier())
            {
                isOldVidPid = (arduinoPort.vendorIdentifier() == OPENMVCAM_VID) && (arduinoPort.productIdentifier() == OPENMVCAM_PID);
                isTinyUSBHSV1Protocol = ((arduinoPort.vendorIdentifier() == OPENMVCAM_VID_NEW) && (arduinoPort.productIdentifier() == OPENMVCAM_RT1062_PID)) ||
                                        ((arduinoPort.vendorIdentifier() == OPENMVCAM_VID_NEW) && (arduinoPort.productIdentifier() == OPENMVCAM_AE3_PID));
                isArduinoDFU = isBootloaderType(m_firmwareSettings, arduinoPort.vendorIdentifier(), arduinoPort.productIdentifier(), QStringLiteral("arduino_dfu"));
                isBossac = isBootloaderType(m_firmwareSettings, arduinoPort.vendorIdentifier(), arduinoPort.productIdentifier(), QStringLiteral("bossac"));
                isPicotool = isBootloaderType(m_firmwareSettings, arduinoPort.vendorIdentifier(), arduinoPort.productIdentifier(), QStringLiteral("picotool"));

                // These need to be hardcoded to prevent disabling the license check.

                isPortenta = (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && ((arduinoPort.productIdentifier() == PORTENTA_APP_O_PID) ||
                                                                                   (arduinoPort.productIdentifier() == PORTENTA_APP_N_PID));
                isNiclav = (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && ((arduinoPort.productIdentifier() == NICLA_APP_O_PID) ||
                                                                                  (arduinoPort.productIdentifier() == NICLA_APP_N_PID));
                isGiga = (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (arduinoPort.productIdentifier() == GIGA_APP_PID);
                isNRF = (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (arduinoPort.productIdentifier() == NRF_APP_PID);
                isRPIPico = ((arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (arduinoPort.productIdentifier() == RPI_APP_PID)) ||
                          ((arduinoPort.vendorIdentifier() == RPI2040_VID) && (arduinoPort.productIdentifier() == RPI2040_PID));
            }
        }

        if(!selectedDfuDevice.isEmpty())
        {
            QStringList vidpid = selectedDfuDevice.split(QStringLiteral(",")).first().split(QStringLiteral(":"));

            isOpenMVDfu = dfuVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isAlif = alifVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isArduinoDFU = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("arduino_dfu"));
            isBossac = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("bossac"));
            isPicotool = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("picotool"));

            // These need to be hardcoded to prevent disabling the license check.

            isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);
            isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);
            isGiga = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == GIGA_LDR_PID);
            isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));
            isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||
                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));

            if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Please update the bootloader to the latest version and install the SoftDevice to flash the OpenMV firmware. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                CONNECT_END();
            }

            if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Please short REC to GND and reset your board. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                CONNECT_END();
            }
        }

        if((!originalDfuVidPid.isEmpty()) && selectedPort.isEmpty() && dfuDevices.isEmpty())
        {
            QStringList vidpid = originalDfuVidPid.split(QStringLiteral(":"));

            isOpenMVDfu = dfuVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isAlif = alifVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isArduinoDFU = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("arduino_dfu"));
            isBossac = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("bossac"));
            isPicotool = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("picotool"));

            // These need to be hardcoded to prevent disabling the license check.

            isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);
            isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);
            isGiga = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == GIGA_LDR_PID);
            isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));
            isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||
                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));

            if(isOpenMVDfu || isIMX || isAlif || isArduinoDFU || isBossac || isPicotool)
            {
                selectedDfuDevice = originalDfuVidPid + QStringLiteral(",NULL");
            }

            if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Please update the bootloader to the latest version and install the SoftDevice to flash the OpenMV firmware. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                CONNECT_END();
            }

            if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Please short REC to GND and reset your board. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                CONNECT_END();
            }
        }

        if (dfuNoDialogs && (!isOpenMVDfu) && (!isIMX) && (!isAlif) && (!isArduinoDFU) && (!isBossac) && (!isPicotool)) {
            firmwarePath = QFileInfo(firmwarePath).path() + QStringLiteral("/bootloader.bin");
            repairingBootloader = true;
        }

        // Open Port //////////////////////////////////////////////////////////

        m_iodevice->setHighSpeed(false);

        if(!forceBootloaderBricked)
        {
            QString errorMessage2 = QString();
            QString *errorMessage2Ptr = &errorMessage2;

            QMetaObject::Connection conn = connect(m_ioport, &OpenMVPluginSerialPort::openResult,
                this, [errorMessage2Ptr] (const QString &errorMessage) {
                *errorMessage2Ptr = errorMessage;
            });

            QProgressDialog dialog(Tr::tr("Connecting...\n\n(Hit cancel if this takes more than 5 seconds)."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
               Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
               (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
            dialog.setWindowModality(Qt::ApplicationModal);
            dialog.setAttribute(Qt::WA_ShowWithoutActivating);

            forever
            {
                QEventLoop loop;

                connect(m_ioport, &OpenMVPluginSerialPort::openResult,
                        &loop, &QEventLoop::quit);

                m_ioport->open(selectedPort);

                loop.exec();

                if(errorMessage2.isEmpty() || (Utils::HostOsInfo::isLinuxHost() && errorMessage2.contains(QStringLiteral("Permission Denied"), Qt::CaseInsensitive)))
                {
                    break;
                }

                dialog.show();

                QApplication::processEvents();

                if(dialog.wasCanceled())
                {
                    break;
                }
            }

            dialog.close();

            disconnect(conn);

            if(!errorMessage2.isEmpty())
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Error: %L1!").arg(errorMessage2));

                if(Utils::HostOsInfo::isLinuxHost() && errorMessage2.contains(QStringLiteral("Permission Denied"), Qt::CaseInsensitive))
                {
                    QMessageBox::information(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Try doing:\n\n") + QString(QStringLiteral("sudo adduser %L1 dialout\n\n")).arg(Utils::Environment::systemEnvironment().toDictionary().userName()) + Tr::tr("...in a terminal and then restart your computer."));
                }

                CONNECT_END();
            }
        }

        if(isTTR)
        {
            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                    &loop, &QEventLoop::quit);

            m_iodevice->close();

            loop.exec();

            QElapsedTimer elaspedTimer;
            elaspedTimer.start();

            while(!elaspedTimer.hasExpired(RESET_TO_DFU_SEARCH_TIME))
            {
                QApplication::processEvents();
            }

            RECONNECT_END();
        }

        // V2 Protocol ////////////////////////////////////////////////////////

        if(isNetworkPort(selectedPort))
        {
            // A network link is always V2 -- skip the V1/V2 detection probe. It's pointless here
            // (V1 was USB-only) and, on a lossy link, the probe's 5s no-resend read_timeout badly
            // stalls the connect. There's nothing to wait for: forceV2Protocol() sets V2 mode and
            // the queued port ops stay ordered, so the next V2 command runs after it regardless.
            m_iodevice->forceV2Protocol();
        }
        else if((!forceBootloaderBricked) && (!isOldVidPid))
        {
            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::protocolVersionDone,
                    &loop, &QEventLoop::quit);

            // For unknown reasons, sending the special check packet on mac does not work
            // correctly unless it's split on the highspeed cameras. The USB packet is indeed
            // sent on the bus, but, the camera will lockup. This workout exist for all high speed
            // capable cameras using the V1 protocol with TinyUSB.
            if (isTinyUSBHSV1Protocol && Utils::HostOsInfo::isMacHost())
            {
                m_iodevice->checkProtocolVerison(true);
            }
            else
            {
                m_iodevice->checkProtocolVerison(m_reconnects & 1);
            }

            loop.exec();
        }

        // Get Version ////////////////////////////////////////////////////////

        int major2 = int();
        int minor2 = int();
        int patch2 = int();

        if(!forceBootloaderBricked)
        {
            int *major2Ptr = &major2;
            int *minor2Ptr = &minor2;
            int *patch2Ptr = &patch2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::firmwareVersion,
                this, [major2Ptr, minor2Ptr, patch2Ptr] (int major, int minor, int patch) {
                *major2Ptr = major;
                *minor2Ptr = minor;
                *patch2Ptr = patch;
            });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::firmwareVersion,
                    &loop, &QEventLoop::quit);

            m_iodevice->getFirmwareVersion();

            loop.exec();

            disconnect(conn);

            if((!major2) && (!minor2) && (!patch2))
            {
                // Auto-retry is for transient USB failures (e.g. the cam still finishing a reset). A
                // network timeout means the link is bad, so don't burn more 10s attempts -- try once,
                // then go straight to the dialog.
                if((!isNetworkPort(selectedPort)) && (m_reconnects < RECONNECTS_MAX))
                {
                    m_reconnects += 1;
                    QThread::msleep(10);
                    CLOSE_RECONNECT_END();
                }

                m_reconnects = 0;

                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Timeout error while getting firmware version!"));

                if(!isNetworkPort(selectedPort)) // the green light is a USB-boot thing, not a network link
                {
                    QMessageBox::warning(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Do not try to connect while the green light on your OpenMV Cam is on!"));
                }

                if(QMessageBox::question(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Try to connect again?"),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
                == QMessageBox::Yes)
                {
                    CLOSE_RECONNECT_END();
                }

                CLOSE_CONNECT_END();
            }
            else if((major2 < 0) || (100 < major2) || (minor2 < 0) || (100 < minor2) || (patch2 < 0) || (100 < patch2))
            {
                CLOSE_RECONNECT_END();
            }

            m_reconnects = 0;

            if(((major2 > OLD_API_MAJOR)
            || ((major2 == OLD_API_MAJOR) && (minor2 > OLD_API_MINOR))
            || ((major2 == OLD_API_MAJOR) && (minor2 == OLD_API_MINOR) && (patch2 >= OLD_API_PATCH))))
            {
                QString arch2 = QString();
                QString *arch2Ptr = &arch2;

                QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::archString,
                    this, [arch2Ptr] (const QString &arch) {
                    *arch2Ptr = arch;
                });

                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::archString,
                        &loop, &QEventLoop::quit);

                m_iodevice->getArchString();

                loop.exec();

                disconnect(conn);

                if(!arch2.isEmpty())
                {
                    QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();
                    MyQSerialPortInfo tempInfo = createInfo(selectedPort);

                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        if((value.toObject().value(QStringLiteral("boardArchString")).toString().toLower() == temp.toLower())
                        && matchVidPid(value.toObject(), QString(), tempInfo))
                        {
                            m_iodevice->setHighSpeed(value.toObject().value(QStringLiteral("highSpeed")).toBool());

                            if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
                            {
                                isOpenMVDfu = true;
                                originalDfuVidPid = value.toObject().value(QStringLiteral("bootloaderVidPid")).toString();
                                break;
                            }

                            if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("imx"))
                            {
                                isIMX = true;
                                break;
                            }

                            break;
                        }
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Timeout error while getting board architecture!"));

                    CLOSE_CONNECT_END();
                }
            }
        }

        // Bootloader /////////////////////////////////////////////////////////

        if(forceBootloader)
        {
            if(!forceBootloaderBricked)
            {
                if((major2 < OLD_API_MAJOR)
                || ((major2 == OLD_API_MAJOR) && (minor2 < OLD_API_MINOR))
                || ((major2 == OLD_API_MAJOR) && (minor2 == OLD_API_MINOR) && (patch2 < OLD_API_PATCH)))
                {
                    if(firmwarePath.isEmpty()) firmwarePath = Core::ICore::allUsersResourcePath(QStringLiteral("firmware"))
                        .pathAppended(QStringLiteral(OLD_API_BOARD))
                        .pathAppended(QStringLiteral("firmware.bin")).toString();

                    if(installTheLatestDevelopmentFirmware)
                    {
                        if(!getTheLatestDevelopmentFirmware(QStringLiteral(OLD_API_BOARD), &firmwarePath, QStringLiteral("firmware"), OLD_API_BOARD, customFirmwareBundleDir))
                        {
                            CLOSE_CONNECT_END();
                        }
                    }
                }
                else
                {
                    QString arch2 = QString();
                    QString *arch2Ptr = &arch2;

                    QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::archString,
                        this, [arch2Ptr] (const QString &arch) {
                        *arch2Ptr = arch;
                    });

                    QEventLoop loop;

                    connect(m_iodevice, &OpenMVPluginIO::archString,
                            &loop, &QEventLoop::quit);

                    m_iodevice->getArchString();

                    loop.exec();

                    disconnect(conn);

                    if(!arch2.isEmpty())
                    {
                        QMap<QString, QString> mappings;
                        QMap<QString, QString> mappingsHumanReadable;
                        QMap<QString, QPair<int, int> > eraseMappings;
                        QMap<QString, QPair<int, int> > eraseAllMappings;
                        QMap<QString, QJsonObject> fallbackBootloaderMappings;
                        QMap<QString, QString> vidpidMappings;
                        QMap<QString, QString> defaultFirmwareNameMapping;
                        QMap<QString, QString> resourceRootMappings; // Third-party board -> vendor firmware dir.

                        MyQSerialPortInfo tempInfo = createInfo(selectedPort);

                        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                        {
                            if (matchVidPid(value.toObject(), QString(), tempInfo))
                            {
                                QString a = value.toObject().value(QStringLiteral("boardArchString")).toString();
                                mappings.insert(a, value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());
                                mappingsHumanReadable.insert(value.toObject().value(QStringLiteral("boardDisplayName")).toString(), a);
                                vidpidMappings.insert(a, value.toObject().value(QStringLiteral("bootloaderVidPid")).toString());
                                defaultFirmwareNameMapping.insert(a, value.toObject().value(QStringLiteral("defaultFirmwareName")).toString());
                                resourceRootMappings.insert(a, value.toObject().value(QStringLiteral("_resourceRoot")).toString());

                                if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("internal"))
                                {
                                    QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                                    int eraseSectorStart = bootloaderSettings.value(QStringLiteral("eraseSectorStart")).toInt();
                                    int eraseSectorEnd = bootloaderSettings.value(QStringLiteral("eraseSectorEnd")).toInt();
                                    int eraseAllSectorStart = bootloaderSettings.value(QStringLiteral("eraseAllSectorStart")).toInt();
                                    int eraseAllSectorEnd = bootloaderSettings.value(QStringLiteral("eraseAllSectorEnd")).toInt();
                                    eraseMappings.insert(a, QPair<int, int>(eraseSectorStart, eraseSectorEnd));
                                    eraseAllMappings.insert(a, QPair<int, int>(eraseAllSectorStart, eraseAllSectorEnd));
                                    fallbackBootloaderMappings.insert(a, bootloaderSettings.value(QStringLiteral("fallbackBootloader")).toObject());
                                }
                                else if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
                                {
                                    QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                                    eraseMappings.insert(a, QPair<int, int>(0, 0));
                                    eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                                    fallbackBootloaderMappings.insert(a, bootloaderSettings.value(QStringLiteral("fallbackBootloader")).toObject());
                                }
                                else if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("alif_tools"))
                                {
                                    QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                                    eraseMappings.insert(a, QPair<int, int>(0, 0));
                                    eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                                    fallbackBootloaderMappings.insert(a, bootloaderSettings);
                                }
                                else
                                {
                                    eraseMappings.insert(a, QPair<int, int>(0, 0));
                                    eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                                    fallbackBootloaderMappings.insert(a, QJsonObject());
                                }
                            }
                        }

                        QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();

                        if(!mappings.contains(temp))
                        {
                            int index = mappingsHumanReadable.keys().indexOf(
                                settings->value(SETTINGS_GROUP "/" LAST_BOARD_TYPE_STATE_2).toString());

                            bool ok = mappingsHumanReadable.size() == 1;
                            temp = (mappingsHumanReadable.size() == 1) ? mappingsHumanReadable.keys().first() : QInputDialog::getItem(Core::ICore::dialogParent(),
                                Tr::tr("Connect"), Tr::tr("Please select the board type"),
                                mappingsHumanReadable.keys(), (index != -1) ? index : 0, false, &ok,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                            if(ok)
                            {
                                settings->setValue(SETTINGS_GROUP "/" LAST_BOARD_TYPE_STATE_2, temp);
                            }
                            else
                            {
                                CLOSE_CONNECT_END();
                            }

                            temp = mappingsHumanReadable.value(temp); // Get mappings key.
                        }

                        if(firmwarePath.isEmpty())
                        {
                            previousMapping = temp;
                            originalFirmwareFolder = mappings.value(temp);
                            m_boardResourceRoot = resourceRootMappings.value(temp);
                            firmwarePath = (resourceRootMappings.value(temp).isEmpty()
                                ? Core::ICore::allUsersResourcePath(QStringLiteral("firmware"))
                                : Utils::FilePath::fromString(resourceRootMappings.value(temp)))
                                .pathAppended(originalFirmwareFolder)
                                .pathAppended(defaultFirmwareNameMapping.value(temp)).toString();
                            if (forceBootloader && (!forceFirmwarePath.isEmpty())) firmwarePath = forceFirmwarePath;
                            originalEraseFlashSectorStart = eraseMappings.value(temp).first;
                            originalEraseFlashSectorEnd = eraseMappings.value(temp).second;
                            originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;
                            originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;
                            originalFallbackBootloaderSettings = fallbackBootloaderMappings.value(temp);


                            if(installTheLatestDevelopmentFirmware)
                            {
                                if(!getTheLatestDevelopmentFirmware(mappings.value(temp), &firmwarePath, defaultFirmwareNameMapping.value(temp), originalFirmwareFolder, customFirmwareBundleDir))
                                {
                                    CLOSE_CONNECT_END();
                                }
                            }
                        }
                        else
                        {
                            previousMapping = temp;
                            originalFirmwareFolder = mappings.value(temp);
                            m_boardResourceRoot = resourceRootMappings.value(temp);
                            originalEraseFlashSectorStart = eraseMappings.value(temp).first;
                            originalEraseFlashSectorEnd = eraseMappings.value(temp).second;
                            originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;
                            originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;
                            originalFallbackBootloaderSettings = fallbackBootloaderMappings.value(temp);
                        }

                        QStringList vidpid = vidpidMappings.value(temp).split(QStringLiteral(":"));
                        isOpenMVDfu = dfuVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
                        isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
                        isAlif = alifVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
                        isArduinoDFU = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("arduino_dfu"));
                        isBossac = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("bossac"));
                        isPicotool = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("picotool"));

                        // These need to be hardcoded to prevent disabling the license check.

                        isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);
                        isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);
                        isGiga = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == GIGA_LDR_PID);
                        isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));
                        isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||
                                    ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));

                        QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(.+?)\\[(.+?):(.+?)\\]")).match(arch2);

                        if(match.hasMatch())
                        {
                            m_boardTypeFolder = originalFirmwareFolder;
                            m_boardResourceRoot = resourceRootMappings.value(temp);
                            m_fullBoardType = match.captured(1).trimmed();
                            m_boardType = match.captured(2);
                            m_boardId = match.captured(3);
                            m_boardVID = vidpid.at(0).toInt(nullptr, 16);
                            m_boardPID = vidpid.at(1).toInt(nullptr, 16);
                        }
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            Tr::tr("Timeout error while getting board architecture!"));

                        CLOSE_CONNECT_END();
                    }
                }

                if((!isOpenMVDfu)
                && (!isIMX)
                && (!isAlif)
                && (!isArduinoDFU)
                && (!isBossac)
                && (!isPicotool)
                && firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
                {
                    QEventLoop loop;

                    connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                            &loop, &QEventLoop::quit);

                    m_iodevice->close();

                    loop.exec();
                }
            }

            if ((!repairingBootloader)
            && (!isOpenMVDfu)
            && (!isIMX)
            && (!isAlif)
            && (!isArduinoDFU)
            && (!isBossac)
            && (!isPicotool)
            && (justEraseFlashFs ||
                firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive) ||
                firmwarePath.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive)))
            {
                QStringList vidpid = QString(selectedDfuDevice).split(QStringLiteral(",")).first().split(QStringLiteral(":"));

                if((vidpid.size() == 2) && (vidpid.at(0).toInt(nullptr, 16) == STM32_DFU_VID) && (vidpid.at(1).toInt(nullptr, 16) == STM32_DFU_PID))
                {
                    bool cubeProgrammer = originalFallbackBootloaderSettings.value(QStringLiteral("useSTCubeProgrammer")).toBool();

                    if (cubeProgrammer)
                    {
                        if (firmwarePath.endsWith(QStringLiteral("bootloader.bin")))
                        {
                            QTemporaryDir tempDir;

                            if (tempDir.isValid())
                            {
                                QString tempPath = tempDir.path();

                                QDir originalFirmwareDir(firmwareResourcePath()
                                    .pathAppended(originalFirmwareFolder).toString());

                                if (originalFirmwareDir.exists())
                                {
                                    bool ok = true;

                                    for (const QFileInfo &fileInfo : originalFirmwareDir.entryInfoList(QDir::Files))
                                    {
                                        ok = ok && QFile::copy(fileInfo.absoluteFilePath(), tempPath + QDir::separator() + fileInfo.fileName());
                                    }

                                    QFile::remove(tempPath + QDir::separator() + QStringLiteral("bootloader.bin"));
                                    ok = ok && QFile::copy(firmwarePath, tempPath + QDir::separator() + QStringLiteral("bootloader.bin"));

                                    if (ok)
                                    {
                                        openmvRepairingBootloader(forceFlashFSErase,
                                                                  previousMapping,
                                                                  originalDfuVidPid,
                                                                  true,
                                                                  tempPath + QDir::separator() + QStringLiteral("bootloader.bin"),
                                                                  false,
                                                                  true);
                                        return;
                                    }
                                    else
                                    {
                                        QMessageBox::critical(Core::ICore::dialogParent(),
                                            Tr::tr("Connect"),
                                            Tr::tr("Failed to copy firmware files to temporary directory!"));

                                        CONNECT_END();
                                    }
                                }
                                else
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        Tr::tr("Connect"),
                                        Tr::tr("Original firmware folder does not exist!"));

                                    CONNECT_END();
                                }
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Connect"),
                                    Tr::tr("Failed to create temporary directory!"));

                                CONNECT_END();
                            }
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                Tr::tr("Only loading bootloader.bin files is supported with the ST Cube Programmer!"));

                            CONNECT_END();
                        }
                    }

                    openmvRepairingBootloader(forceFlashFSErase,
                                              previousMapping,
                                              originalDfuVidPid,
                                              true,
                                              firmwarePath,
                                              false,
                                              cubeProgrammer);
                    return;
                }

                openmvInternalBootloader(forceFirmwarePath,
                                         forceFlashFSErase,
                                         justEraseFlashFs,
                                         installTheLatestDevelopmentFirmware,
                                         previousMapping,
                                         selectedPort,
                                         forceBootloaderBricked,
                                         previousMappingSet,
                                         originalFirmwareFolder,
                                         firmwarePath,
                                         originalEraseFlashSectorStart,
                                         originalEraseFlashSectorEnd,
                                         originalEraseFlashSectorAllStart,
                                         originalEraseFlashSectorAllEnd,
                                         originalFallbackBootloaderSettings,
                                         originalDfuVidPid,
                                         dfuNoDialogs,
                                         romfsAccess);
                return;
            }

            if(isOpenMVDfu)
            {
                if ((!firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive))
                && (!firmwarePath.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
                && (!firmwarePath.endsWith(QStringLiteral(".lst"), Qt::CaseInsensitive))
                && (!firmwarePath.isEmpty()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.bin and *.img files are supported for the internal bootloader!"));

                    CONNECT_END();
                }

                if (originalFallbackBootloaderSettings.value(QStringLiteral("type")) == QStringLiteral("internal"))
                {
                    QJsonObject fallbackSettings = originalFallbackBootloaderSettings.value(QStringLiteral("settings")).toObject();
                    QJsonObject dfuFallbackSettings;
                    dfuFallbackSettings.insert(QStringLiteral("vidpid"), originalDfuVidPid);
                    dfuFallbackSettings.insert(QStringLiteral("type"), QJsonValue::fromVariant(QStringLiteral("openmv_dfu")));

                    openmvInternalBootloader(forceFirmwarePath,
                                             forceFlashFSErase,
                                             justEraseFlashFs,
                                             installTheLatestDevelopmentFirmware,
                                             previousMapping,
                                             selectedPort,
                                             forceBootloaderBricked,
                                             previousMappingSet,
                                             originalFirmwareFolder,
                                             firmwarePath,
                                             fallbackSettings.value(QStringLiteral("eraseAllSectorStart")).toInt(),
                                             fallbackSettings.value(QStringLiteral("eraseAllSectorEnd")).toInt(),
                                             fallbackSettings.value(QStringLiteral("eraseSectorStart")).toInt(),
                                             fallbackSettings.value(QStringLiteral("eraseSectorEnd")).toInt(),
                                             dfuFallbackSettings,
                                             fallbackSettings.value(QStringLiteral("vidpid")).toString(),
                                             dfuNoDialogs,
                                             romfsAccess);
                }
                else
                {
                    openmvDFUBootloader(forceFlashFSErase,
                                        justEraseFlashFs,
                                        installTheLatestDevelopmentFirmware,
                                        firmwarePath,
                                        selectedDfuDevice,
                                        romfsAccess,
                                        QString(),
                                        forceBootloaderEntry);
                }

                return;
            }

            if(isIMX)
            {
                if ((!firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive))
                && (!firmwarePath.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
                && (!firmwarePath.isEmpty()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.bin and *.img files are supported for the IMX bootloader!"));

                    CONNECT_END();
                }

                openmvIMXBootloader(forceFirmwarePath,
                                    forceFlashFSErase,
                                    justEraseFlashFs,
                                    installTheLatestDevelopmentFirmware,
                                    firmwarePath,
                                    settings,
                                    forceBootloaderBricked,
                                    originalFirmwareFolder,
                                    selectedDfuDevice,
                                    romfsAccess,
                                    customFirmwareBundleDir);
                return;
            }

            if (isAlif)
            {
                if (firmwarePath.endsWith(QStringLiteral("bootloader.bin")))
                {
                    QTemporaryDir tempDir;

                    if (tempDir.isValid())
                    {
                        QString tempPath = tempDir.path();

                        QDir originalFirmwareDir(firmwareResourcePath()
                                                     .pathAppended(originalFirmwareFolder).toString());

                        if (originalFirmwareDir.exists())
                        {
                            bool ok = true;

                            for (const QFileInfo &fileInfo : originalFirmwareDir.entryInfoList(QDir::Files))
                            {
                                ok = ok && QFile::copy(fileInfo.absoluteFilePath(), tempPath + QDir::separator() + fileInfo.fileName());
                            }

                            QFile::remove(tempPath + QDir::separator() + QStringLiteral("bootloader.bin"));
                            ok = ok && QFile::copy(firmwarePath, tempPath + QDir::separator() + QStringLiteral("bootloader.bin"));

                            if (ok)
                            {
                                if(alifDownloadFirmware(selectedDfuDevice.split(QStringLiteral(",")).last(),
                                                        tempPath,
                                                        originalFallbackBootloaderSettings))
                                {
                                    if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                                        Tr::tr("Connect"),
                                        Tr::tr("Bootloader update complete!\n\n") +
                                        Tr::tr("Connect your OpenMV Cam now."));

                                    RECONNECT_WAIT_END();
                                }
                                else
                                {
                                    CONNECT_END();
                                }
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                                      Tr::tr("Connect"),
                                                      Tr::tr("Failed to copy firmware files to temporary directory!"));

                                CONNECT_END();
                            }
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                                  Tr::tr("Connect"),
                                                  Tr::tr("Original firmware folder does not exist!"));

                            CONNECT_END();
                        }
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                                              Tr::tr("Connect"),
                                              Tr::tr("Failed to create temporary directory!"));

                        CONNECT_END();
                    }
                }

                openmvAlifBootloader(forceFirmwarePath,
                                     forceFlashFSErase,
                                     justEraseFlashFs,
                                     settings,
                                     originalFirmwareFolder,
                                     selectedDfuDevice);
                return;
            }

            if (isArduinoDFU)
            {
                if ((!firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive))
                && (!firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
                && (!firmwarePath.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
                && (!firmwarePath.isEmpty()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.bin, *.dfu, and *.img files are supported for the Arduino bootloader!"));

                    CONNECT_END();
                }

                openmvArduinoDFUBootloader(forceFlashFSErase,
                                           justEraseFlashFs,
                                           installTheLatestDevelopmentFirmware,
                                           firmwarePath,
                                           selectedDfuDevice,
                                           romfsAccess);
                return;
            }

            if (isBossac)
            {
                if (!firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)
                && (!firmwarePath.isEmpty()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.bin files is supported for the Bossac bootloader!"));

                    CONNECT_END();
                }

                openmvBossacBootloader(forceFlashFSErase,
                                       justEraseFlashFs,
                                       firmwarePath,
                                       selectedDfuDevice,
                                       romfsAccess);
                return;
            }

            if (isPicotool)
            {
                if (!firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)
                && (!firmwarePath.isEmpty()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.bin files is supported for the Picotool bootloader!"));

                    CONNECT_END();
                }

                openmvPictotoolBootloader(forceFlashFSErase,
                                          justEraseFlashFs,
                                          firmwarePath,
                                          selectedDfuDevice,
                                          romfsAccess);
                return;
            }

            if (repairingBootloader ||
            firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
            {
                QStringList vidpid = QString(selectedDfuDevice).split(QStringLiteral(",")).first().split(QStringLiteral(":"));

                if((vidpid.size() == 2) && (vidpid.at(0).toInt(nullptr, 16) == STM32_DFU_VID) && (vidpid.at(1).toInt(nullptr, 16) == STM32_DFU_PID))
                {
                    if (QFileInfo(firmwarePath).baseName() != QStringLiteral("bootloader"))
                    {
                        if (QMessageBox::warning(Core::ICore::dialogParent(),
                                                 Tr::tr("Connect"),
                                                 Tr::tr("Note that loading the firmware.dfu or openmv.dfu (bootloader + firmware) "
                                                        "may not work on STM32H7 boards due to a bug in the chip's ROM bootloader!\n\n"
                                                        "OpenMV recommends only loading the bootloader.dfu to repair the bootloader."),
                                                 QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) != QMessageBox::Ok)
                        {
                            CONNECT_END();
                        }
                    }
                }

                for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                {
                    QJsonObject object = value.toObject();

                    if (object.value(QStringLiteral("hidden")).toBool()
                    && (previousMapping.toLower() == object.value(QStringLiteral("boardDisplayName")).toString().toLower()))
                    {
                        previousMapping = object.value(QStringLiteral("boardArchString")).toString();
                        break;
                    }
                }

                openmvRepairingBootloader(forceFlashFSErase,
                                          previousMapping,
                                          originalDfuVidPid,
                                          dfuNoDialogs,
                                          firmwarePath,
                                          repairingBootloader,
                                          originalFallbackBootloaderSettings.value(QStringLiteral("useSTCubeProgrammer")).toBool());
                return;
            }

            CONNECT_END();
        }

        // Check ID ///////////////////////////////////////////////////////////

        bool disableLicenseCheck = false;
        if(isPortenta || isNiclav || isGiga || isNRF || isRPIPico) disableLicenseCheck = true;
        m_sensorType = QString();

        if((major2 < OPENMV_DBG_PROTOCOL_CHNAGE_MAJOR)
        || ((major2 == OPENMV_DBG_PROTOCOL_CHNAGE_MAJOR) && (minor2 < OPENMV_DBG_PROTOCOL_CHNAGE_MINOR))
        || ((major2 == OPENMV_DBG_PROTOCOL_CHNAGE_MAJOR) && (minor2 == OPENMV_DBG_PROTOCOL_CHNAGE_MINOR) && (patch2 < OPENMV_DBG_PROTOCOL_CHNAGE_PATCH)))
        {
            m_iodevice->breakUpGetAttributeCommand(false);
            m_iodevice->breakUpSetAttributeCommand(false);
            m_iodevice->breakUpFBEnable(false);
            m_iodevice->breakUpJPEGEnable(false);
        }
        else
        {
            m_iodevice->breakUpGetAttributeCommand(true);
            m_iodevice->breakUpSetAttributeCommand(true);
            m_iodevice->breakUpFBEnable(true);
            m_iodevice->breakUpJPEGEnable(true);

            // Get Sensor Type
            {
                QList<int> ids2;
                QList<int> *ids2Ptr = &ids2;

                QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::sensorIdDone,
                    this, [ids2Ptr] (QList<int> ids) {
                    *ids2Ptr = ids;
                });

                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::sensorIdDone,
                        &loop, &QEventLoop::quit);

                m_iodevice->sensorId();

                loop.exec();

                disconnect(conn);

                if(ids2.isEmpty() || ids2.at(0) == 0xFF)
                {
                    disableLicenseCheck = true;
                    m_sensorType = Tr::tr("None");
                }
                else if(ids2.at(0))
                {
                    QList<QPair<QString, bool> > sensorTypeList;

                    for (int id : ids2)
                    {
                        QString sensorType;
                        bool mainSensor = false;
                        bool hidden = false;
                        bool found = false;

                        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("sensors")).toArray())
                        {
                            if (int(value.toObject().value(QStringLiteral("id")).toString().toLongLong(nullptr, 0)) == id)
                            {
                                sensorType = value.toObject().value(QStringLiteral("name")).toString();
                                mainSensor = value.toObject().value(QStringLiteral("main")).toBool();
                                hidden = value.toObject().value(QStringLiteral("hidden")).toBool();
                                found = true;
                                break;
                            }
                        }

                        if (!found)
                        {
                            sensorType = Tr::tr("Unknown");
                        }

                        QPair<QString, bool> pair(sensorType, hidden);
                        if (mainSensor) sensorTypeList.prepend(pair);
                        else if (!hidden || sensorTypeList.isEmpty()) sensorTypeList.append(pair);
                    }

                    for (int i = 0; i < sensorTypeList.size(); )
                    {
                        if (sensorTypeList.at(i).second) sensorTypeList.removeAt(i);
                        else i++;
                    }

                    QStringList sensorTypes;

                    for (const QPair<QString, bool> &pair : sensorTypeList)
                    {
                        sensorTypes.append(pair.first);
                    }

                    m_sensorType = sensorTypes.join(QStringLiteral(", "));
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Timeout error while getting sensor type!"));

                    disableLicenseCheck = true;
                    m_sensorType = Tr::tr("Unknown");
                }
            }
        }

        if((major2 < OPENMV_RGB565_BYTE_REVERSAL_FIXED_MAJOR)
        || ((major2 == OPENMV_RGB565_BYTE_REVERSAL_FIXED_MAJOR) && (minor2 < OPENMV_RGB565_BYTE_REVERSAL_FIXED_MINOR))
        || ((major2 == OPENMV_RGB565_BYTE_REVERSAL_FIXED_MAJOR) && (minor2 == OPENMV_RGB565_BYTE_REVERSAL_FIXED_MINOR) && (patch2 < OPENMV_RGB565_BYTE_REVERSAL_FIXED_PATCH)))
        {
            m_iodevice->rgb565ByteReservedEnable(true);
        }
        else
        {
            m_iodevice->rgb565ByteReservedEnable(false);
        }

        if((major2 < OPENMV_NEW_PIXFORMAT_MAJOR)
        || ((major2 == OPENMV_NEW_PIXFORMAT_MAJOR) && (minor2 < OPENMV_NEW_PIXFORMAT_MINOR))
        || ((major2 == OPENMV_NEW_PIXFORMAT_MAJOR) && (minor2 == OPENMV_NEW_PIXFORMAT_MINOR) && (patch2 < OPENMV_NEW_PIXFORMAT_PATCH)))
        {
            m_iodevice->newPixformatEnable(false);
        }
        else
        {
            m_iodevice->newPixformatEnable(true);
        }

        if((major2 < OPENMV_ADD_MAIN_TERMINAL_INPUT_MAJOR)
        || ((major2 == OPENMV_ADD_MAIN_TERMINAL_INPUT_MAJOR) && (minor2 < OPENMV_ADD_MAIN_TERMINAL_INPUT_MINOR))
        || ((major2 == OPENMV_ADD_MAIN_TERMINAL_INPUT_MAJOR) && (minor2 == OPENMV_ADD_MAIN_TERMINAL_INPUT_MINOR) && (patch2 < OPENMV_ADD_MAIN_TERMINAL_INPUT_PATCH)))
        {
            m_iodevice->mainTerminalInputEnable(false);
        }
        else
        {
            m_iodevice->mainTerminalInputEnable(true);
        }

        if((major2 < OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MAJOR)
        || ((major2 == OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MAJOR) && (minor2 < OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MINOR))
        || ((major2 == OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MAJOR) && (minor2 == OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MINOR) && (patch2 < OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_PATCH)))
        {
            m_iodevice->setGetStateVariableSize(false);
        }
        else
        {
            m_iodevice->setGetStateVariableSize(true);
        }

        m_boardTypeFolder = QString();
        m_boardResourceRoot = QString();
        m_boardVendor = QString();
        m_boardFirmwareFolderAlias = QString();
        m_fullBoardType = QString();
        m_boardType = QString();
        m_boardId = QString();
        m_boardVID = 0;
        m_boardPID = 0;

        QString boardTypeLabel = Tr::tr("Unknown");

        if(((major2 > OLD_API_MAJOR)
        || ((major2 == OLD_API_MAJOR) && (minor2 > OLD_API_MINOR))
        || ((major2 == OLD_API_MAJOR) && (minor2 == OLD_API_MINOR) && (patch2 >= OLD_API_PATCH))))
        {
            QString arch2 = QString();
            QString *arch2Ptr = &arch2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::archString,
                this, [arch2Ptr] (const QString &arch) {
                *arch2Ptr = arch;
            });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::archString,
                    &loop, &QEventLoop::quit);

            m_iodevice->getArchString();

            loop.exec();

            disconnect(conn);

            if(!arch2.isEmpty())
            {
                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(.+?)\\[(.+?):(.+?)\\]")).match(arch2);

                if(match.hasMatch())
                {
                    QSerialPortInfo raw_tempPort = QSerialPortInfo(selectedPort);
                    MyQSerialPortInfo tempPort(raw_tempPort);

                    QString board = match.captured(2);
                    QString id = match.captured(3);

                    m_fullBoardType = match.captured(1).trimmed();
                    m_boardType = board;
                    m_boardId = id;
                    m_boardVID = tempPort.vendorIdentifier();
                    m_boardPID = tempPort.productIdentifier();

                    boardTypeLabel = board;

                    QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();

                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        if ((value.toObject().value(QStringLiteral("boardArchString")).toString().toLower() == temp.toLower())
                        && matchVidPid(value.toObject(), QString(), tempPort))
                        {
                            boardTypeLabel = value.toObject().value(QStringLiteral("boardDisplayName")).toString();
                            m_boardTypeFolder = value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString();
                            m_boardResourceRoot = value.toObject().value(QStringLiteral("_resourceRoot")).toString();
                            m_boardVendor = value.toObject().value(QStringLiteral("_vendor")).toString();
                            m_boardFirmwareFolderAlias = value.toObject().value(QStringLiteral("boardFirmwareFolderAlias")).toString();
                            break;
                        }
                    }

                    QNetworkAccessManager *manager = new QNetworkAccessManager(this);

                    connect(manager, &QNetworkAccessManager::finished, this, [this, disableLicenseCheck, manager, board, id, vendor = m_boardVendor] (QNetworkReply *reply) {

                        QByteArray data = reply->readAll();

                        if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
                        {
                            if((!m_formKey.isEmpty())
                                // Third-party (vendor) boards always license-check,
                                // regardless of board type or the exemptions below.
                                || (!vendor.isEmpty())
                                // Skip OpenMV Cam M4's...
                                || ((!disableLicenseCheck) && (board != QStringLiteral("M4"))))
                            {
                                if(QString::fromUtf8(data).contains(QStringLiteral("<p>Yes</p>")))
                                {
                                    if (!m_formKey.isEmpty())
                                    {
                                        m_registerButton->setProperty("statusColor",
                                            Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ?
                                                                        QStringLiteral("lightgreen") :
                                                                        QStringLiteral("green"));
                                        m_registerButton->setText(Tr::tr("Registered"));
                                        m_registerButton->update();
                                        m_registerButton->setVisible(true);
                                        m_registerButtonSpacer->setVisible(true);
                                    }
                                    else
                                    {
                                        m_registerButton->setText(QString());
                                        m_registerButton->update();
                                        m_registerButton->setVisible(false);
                                        m_registerButtonSpacer->setVisible(false);
                                    }
                                }
                                else if(QString::fromUtf8(data).contains(QStringLiteral("<p>No</p>")))
                                {
                                    m_registerButton->setProperty("statusColor",
                                        Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ?
                                                                    QStringLiteral("lightcoral") :
                                                                    QStringLiteral("coral"));
                                    m_registerButton->setText(Tr::tr("Unregistered"));
                                    m_registerButton->update();
                                    m_registerButton->setVisible(true);
                                    m_registerButtonSpacer->setVisible(true);

                                    QTimer::singleShot(0, this, [this, board, id, vendor] { registerOpenMVCam(board, id, vendor); });
                                }
                                else if((!m_formKey.isEmpty()) && (!QString::fromUtf8(data).contains(QStringLiteral("<p>Yes</p>"))))
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        Tr::tr("Register OpenMV Cam"),
                                        Tr::tr("Database Error!"));

                                    CLOSE_CONNECT_END();
                                }
                            }
                        }
                        else if((!m_formKey.isEmpty()) && (reply->error() != QNetworkReply::NoError))
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Register OpenMV Cam"),
                                Tr::tr("Error: %L1!").arg(reply->error()));

                            CLOSE_CONNECT_END();
                        }
                        else if(!m_formKey.isEmpty())
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Register OpenMV Cam"),
                                Tr::tr("GET Network error!"));

                            CLOSE_CONNECT_END();
                        }

                        connect(reply, &QNetworkReply::destroyed, manager, &QNetworkAccessManager::deleteLater); reply->deleteLater();
                    });

                    QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("https://upload.openmv.io/check.php")));
                    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
                    request.setHeader(QNetworkRequest::UserAgentHeader, openmvServerUserAgent());
                    QByteArray postData = QStringLiteral("board=%1&id=%2").arg(board, id).toUtf8();
                    if(!m_boardVendor.isEmpty()) postData += QStringLiteral("&vendor=%1").arg(m_boardVendor).toUtf8();
                    QNetworkReply *reply = manager->post(request, postData);

                    if(reply)
                    {
                        connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
                    }
                    else if(!m_formKey.isEmpty())
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Register OpenMV Cam"),
                            Tr::tr("GET network error!"));

                        CLOSE_CONNECT_END();
                    }
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Timeout error while getting board architecture!"));

                CLOSE_CONNECT_END();
            }
        }

        if((major2 > LEARN_MTU_ADDED_MAJOR)
        || ((major2 == LEARN_MTU_ADDED_MAJOR) && (minor2 > LEARN_MTU_ADDED_MINOR))
        || ((major2 == LEARN_MTU_ADDED_MAJOR) && (minor2 == LEARN_MTU_ADDED_MINOR) && (patch2 >= LEARN_MTU_ADDED_PATCH)))
        {
            bool ok2 = bool();
            bool *ok2Ptr = &ok2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::learnedMTU,
                this, [ok2Ptr] (bool ok) {
                *ok2Ptr = ok;
            });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::learnedMTU,
                    &loop, &QEventLoop::quit);

            m_iodevice->learnMTU();

            loop.exec();

            disconnect(conn);

            if(!ok2)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Timeout error while learning MTU!"));

                CLOSE_CONNECT_END();
            }
        }

        // Stopping ///////////////////////////////////////////////////////////

        if(m_stopOnConnectDiconnectionAction->isChecked()) m_iodevice->scriptStop();

        if((!m_useGetState)
        || (major2 < OPENMV_ADD_GET_STATE_MAJOR)
        || ((major2 == OPENMV_ADD_GET_STATE_MAJOR) && (minor2 < OPENMV_ADD_GET_STATE_MINOR))
        || ((major2 == OPENMV_ADD_GET_STATE_MAJOR) && (minor2 == OPENMV_ADD_GET_STATE_MINOR) && (patch2 < OPENMV_ADD_GET_STATE_PATCH)))
        {
            // Drain text buffer
            {
                bool empty = bool();
                bool *emptyPtr = &empty;

                QEventLoop loop2;

                QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                    this, [emptyPtr] (bool ok) {
                    *emptyPtr = ok;
                });

                connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                        &loop2, &QEventLoop::quit);

                while(!empty)
                {
                    m_iodevice->getTxBuffer();
                    loop2.exec();
                }

                disconnect(conn2);
            }

            // Drain image buffer
            {
                bool empty = bool();
                bool *emptyPtr = &empty;

                QEventLoop loop2;

                QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                    this, [emptyPtr] (bool ok) {
                    *emptyPtr = ok;
                });

                connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                        &loop2, &QEventLoop::quit);

                while(!empty)
                {
                    m_iodevice->frameSizeDump();
                    loop2.exec();
                }

                disconnect(conn2);
            }
        } else {
            bool printEmpty = bool(), frameBufferEmpty = bool();
            bool *printEmptyPtr = &printEmpty, *frameBufferEmptyPtr = &frameBufferEmpty;

            QEventLoop loop2;

            QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                this, [printEmptyPtr] (bool ok) {
                *printEmptyPtr = ok;
            });

            QMetaObject::Connection conn3 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                this, [frameBufferEmptyPtr] (bool ok) {
                *frameBufferEmptyPtr = ok;
            });

            connect(m_iodevice, &OpenMVPluginIO::getStateDone,
                    &loop2, &QEventLoop::quit);

            while((!printEmpty) || (!frameBufferEmpty))
            {
                m_iodevice->getState();
                loop2.exec();
            }

            disconnect(conn2);
            disconnect(conn3);
        }

        m_jpgCompress->setVisible(m_iodevice->v2ProtocolEnabled());
        m_jpgCompressMode->setVisible(m_iodevice->v2ProtocolEnabled());

        if (!m_boardType.isEmpty() && m_iodevice->v2ProtocolEnabled())
        {
            bool jpegPreferred = bool();
            bool *jpegPreferredPtr = &jpegPreferred;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::jpegPreferred,
                                                   this, [jpegPreferredPtr] (bool ok) {
                                                       *jpegPreferredPtr = ok;
                                                   });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::jpegPreferred,
                    &loop, &QEventLoop::quit);

            m_iodevice->getJPEGPreferred();

            loop.exec();

            disconnect(conn);

            const Utils::Key jpgKey =
                Utils::keyFromString(QStringLiteral(SETTINGS_GROUP "/" JPG_COMPRESS_STATE "_") + m_boardType);
            bool jpgCompress = settings->value(jpgKey, jpegPreferred).toBool();
            // FORCE PREFFERED ON CONNECT
            jpgCompress = jpegPreferred;

            m_jpgCompress->setChecked(jpgCompress);
            m_iodevice->jpegEnable(jpgCompress);
        }
        else
        {
            m_iodevice->jpegEnable(m_jpgCompress->isChecked());
        }

        m_iodevice->fbEnable(!m_disableFrameBuffer->isChecked());

        Core::MessageManager::grayOutOldContent();

        ///////////////////////////////////////////////////////////////////////

        m_iodevice->getTimeout(); // clear

        m_frameSizeDumpTimer.restart();
        m_getScriptRunningTimer.restart();
        m_getTxBufferTimer.restart();

        m_timer.restart();
        m_queue.clear();
        m_cameraQueue.clear();
        m_connected = true;
        m_processEventsTimer->start(1);
        m_running = false;
        m_portName = selectedPort;
        m_portPath = QString();
        m_major = major2;
        m_minor = minor2;
        m_patch = patch2;

        // For a wifi link the "selected port" isn't a serial device, so we can't read the serial
        // off it. Use the serial the cam advertised over mDNS instead -- that's the same USB serial
        // the disk carries, so setPortPath() can still match the cam's USB drive (if attached).
        if(isNetworkPort(selectedPort))
        {
            m_portDriveSerialNumber = QString();
            for(const wifiPort_t &wifiPort : qAsConst(m_availableWifiPorts))
            {
                if(QStringLiteral("%1:%2").arg(wifiPort.name, wifiPort.addressAndPort) == selectedPort)
                {
                    m_portDriveSerialNumber = wifiPort.serialNumber;
                    break;
                }
            }
        }
        else
        {
            m_portDriveSerialNumber = serialPortDriveSerialNumber(selectedPort);
        }

        // A cam reporting a firmware version newer than the released firmware we ship
        // is running a development build (master's version macro is always bumped past
        // the last release tag). When it is, prefer the cached dev examples/docs.
        m_developmentCam = false;
        {
            QRegularExpressionMatch rel = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).
                match(m_firmwareSettings.object().value(QStringLiteral("firmware_version")).toString());

            if(rel.hasMatch())
            {
                int rMaj = rel.captured(1).toInt(), rMin = rel.captured(2).toInt(), rPat = rel.captured(3).toInt();
                m_developmentCam = (m_major > rMaj)
                    || ((m_major == rMaj) && (m_minor > rMin))
                    || ((m_major == rMaj) && (m_minor == rMin) && (m_patch > rPat));
            }
        }

        if(m_developmentCam && (!m_viewerMode))
        {
            // A dev cam is attached. Load the dev example filters from whatever is
            // already cached so the Examples menu reflects it immediately, then refresh
            // the dev examples/docs in the background -- that reloads the filters again
            // if a newer version lands. (The viewer hides those menus, so it never
            // caches them.)
            loadExampleFilters(devResourceFolder(QStringLiteral("examples")));
            backgroundSyncDevResources(DevExamples | DevDocs);
        }

        m_errorFilterString = QString();

        m_openDriveFolderAction->setEnabled(false);
        m_editWifiDebugAction->setEnabled(false);
        m_saveAction->setEnabled(false);
        m_resetAction->setEnabled(true);
        // Entering the bootloader / installing dev firmware needs USB/DFU. A wifi link is fine *if*
        // the cam is also plugged in over USB (matched by serial); on a purely-wireless cam there's
        // no USB to reach, so gray these out until it's plugged back in. (m_resetAction is a protocol
        // soft-reset, so it stays available. The "Load Custom Firmware" / "Erase" menu items are
        // never toggled -- they run their own bootloader scan and fail gracefully if no cam is found.)
        bool flashable = (!isNetworkPort(selectedPort)) || (!usbPortForSerial(m_portDriveSerialNumber).isNull());
        m_enterBootloaderAction->setEnabled(flashable);
        m_developmentReleaseAction->setEnabled(flashable);
        if(!m_autoReconnectAction->isChecked()) m_connectAction->setEnabled(false);
        m_connectAction->setVisible(false);
        if(!m_autoReconnectAction->isChecked()) m_disconnectAction->setEnabled(true);
        m_disconnectAction->setVisible(true);
        Core::IEditor *editor = Core::EditorManager::currentEditor();
        m_startAction->setEnabled(m_viewerMode || (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));
        m_startAction->setVisible(true);
        m_stopAction->setEnabled(false);
        m_stopAction->setVisible(false);

        m_boardLabel->setEnabled(true);
        m_boardLabel->setText(Tr::tr("Board: %L1").arg(boardTypeLabel));
        m_sensorLabel->setEnabled(true);
        m_sensorLabel->setText(m_sensorType.contains(QStringLiteral(","))
                               ? Tr::tr("Sensors: %L1").arg(m_sensorType)
                               : Tr::tr("Sensor: %L1").arg(m_sensorType));
        m_versionButton->setEnabled(true);
        m_versionButton->setText(Tr::tr("Firmware Version: %L1.%L2.%L3").arg(major2).arg(minor2).arg(patch2));
        m_portLabel->setEnabled(true);
        {
            // Show the camera's IDE-local name if it has one, else the real serial port name.
            const QString alias = cameraAlias(m_portDriveSerialNumber.isEmpty() ? m_portName : m_portDriveSerialNumber);
            m_portLabel->setText(Tr::tr("Serial Port: %L1").arg(alias.isEmpty() ? m_portName : alias));
        }
        m_pathButton->setEnabled(true);
        m_pathButton->setText(Tr::tr("Drive:"));
        m_fpsButton->setEnabled(true);
        m_fpsIde = 0.0;
        m_fpsCamera = 0.0;
        m_fpsCameraValid = false; // re-armed when the first v5.0.0 frame reports its FPS
        m_fpsButton->setMinimumWidth(m_fpsButton->fontMetrics().horizontalAdvance(QStringLiteral("FPS: 000.000")));
        refreshFpsButton();

        m_frameBuffer->enableSaveTemplate(false);
        m_frameBuffer->enableSaveDescriptor(false);

        if((major2 > OPENMV_ADD_TIME_INPUT_MAJOR)
        || ((major2 == OPENMV_ADD_TIME_INPUT_MAJOR) && (minor2 > OPENMV_ADD_TIME_INPUT_MINOR))
        || ((major2 == OPENMV_ADD_TIME_INPUT_MAJOR) && (minor2 == OPENMV_ADD_TIME_INPUT_MINOR) && (patch2 >= OPENMV_ADD_TIME_INPUT_PATCH)))
        {
            m_iodevice->timeInput();
        }

        // Fix Hello World ////////////////////////////////////////////////////

        TextEditor::BaseTextEditor *textEditor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::currentEditor());

        if(textEditor)
        {
            TextEditor::TextDocument *document = textEditor->textDocument();

            // Only auto-sync the default Hello World example for the connected
            // sensor while the user has NOT touched it. isModified() is
            // unreliable here (the async setFilePath()/openFinished() and
            // reload() reset it), so compare the live contents against the
            // pristine snapshot taken when the doc was opened. Any keystroke
            // makes them differ, which permanently protects the document.
            const QByteArray pristineHelloWorld =
                document ? document->property("OpenMVPristineHelloWorld").toByteArray() : QByteArray();

            if(document && document->displayName() == QStringLiteral("helloworld_1.py")
            && (!pristineHelloWorld.isEmpty())
            && (document->contents().simplified().trimmed()
                == pristineHelloWorld.simplified().trimmed()))
            {
                QString filePath = Core::ICore::allUsersResourcePath(QStringLiteral("examples/00-HelloWorld/helloworld.py")).toString();

                QFile file(filePath);

                if(file.open(QIODevice::ReadOnly))
                {
                    QByteArray data = file.readAll();

                    if((file.error() == QFile::NoError) && (!data.isEmpty()))
                    {
                        data = fixScriptForSensor(data);

                        if (document->contents().simplified().trimmed() != data.simplified().trimmed())
                        {
                            QFile reload(document->filePath().toString());

                            if(reload.open(QIODevice::WriteOnly))
                            {
                                bool ok = reload.write(data) == data.size();
                                reload.close();

                                if (ok)
                                {
                                    QString error;
                                    document->reload(&error);
                                    // Refresh the snapshot to the just-synced
                                    // contents so a later connect (e.g. a
                                    // different sensor) can re-sync, but only
                                    // while still untouched by the user.
                                    document->setProperty("OpenMVPristineHelloWorld", data);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Check Version //////////////////////////////////////////////////////

        QString latestFirmware = latestFirmwareForConnectedBoard();

        QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).match(latestFirmware);

        // Third-party boards get the normal out-of-date coloring and upgrade prompt
        // even in the viewer - vendor fleets receiving firmware updates through the
        // viewer are the primary third-party use case.
        if(m_viewerMode && m_boardResourceRoot.isEmpty())
        {
            // The viewer ships inside a customer's product to show off what they built on OpenMV.
            // Whatever firmware the product runs is theirs by design -- "out of date" doesn't apply,
            // and we must not push an upgrade that could overwrite their product firmware. Leave the
            // version text plain and don't pop the upgrade dialog.
            m_versionButton->setProperty("statusColor", QVariant());
            m_versionButton->update();
        }
        else if((major2 < match.captured(1).toInt())
        || ((major2 == match.captured(1).toInt()) && (minor2 < match.captured(2).toInt()))
        || ((major2 == match.captured(1).toInt()) && (minor2 == match.captured(2).toInt()) && (patch2 < match.captured(3).toInt())))
        {
            m_versionButton->setText(m_versionButton->text().append(Tr::tr(" - [ out of date - click here to updgrade ]")));

            // Recolor the out-of-date message red to make it obvious, reusing the same
            // theme-aware color the register button uses for an unregistered camera.
            m_versionButton->setProperty("statusColor",
                Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ?
                                            QStringLiteral("lightcoral") :
                                            QStringLiteral("coral"));
            m_versionButton->update();

            QTimer::singleShot(1, this, [this] {
                if ((!m_autoConnect) && Utils::CheckableMessageBox::question(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Your OpenMV Cam's firmware is out of date. Would you like to upgrade?"),
                        Utils::CheckableDecider(DONT_SHOW_UPGRADE_FW_AGAIN),
                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                        QMessageBox::Yes, QMessageBox::No, {}, {}, QMessageBox::Yes) == QMessageBox::Yes)
                {
                    OpenMVPlugin::updateCam(true);
                }
            });
        }
        else
        {
            m_versionButton->setText(m_versionButton->text().append(Tr::tr(" - [ latest ]")));

            // Firmware is current, so leave the text at its default color.
            m_versionButton->setProperty("statusColor", QVariant());
            m_versionButton->update();
        }

        if ((!m_autoConnect) && (m_boardType == QStringLiteral("IMXRT1060")) && isMacHidAccessDeniedForOpenMVIDE())
        {
            QMessageBox::warning(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Your OpenMV Cam RT1062 is connected, but %L1 does not have Input Monitoring "
                       "permission on this Mac. Without it, firmware updates for the RT1062 will fail with a "
                       "\"UsbHidPeripheral() cannot open USB HID device\" error, because macOS blocks the "
                       "NXP flashing tools (blhost, sdphost) that %L1 uses from opening the RT1062's USB HID "
                       "bootloader.\n\n"
                       "To grant access, open the Apple menu, go to System Settings, click Privacy & Security "
                       "in the sidebar, click Input Monitoring, and enable %L1 in the list. Then quit and "
                       "reopen %L1 so the new grant takes effect (macOS reads privacy grants at launch).\n\n"
                       "This warning will stop appearing once Input Monitoring is granted. You can keep using "
                       "your OpenMV Cam RT1062 for running scripts without this permission; it is only required "
                       "for firmware updates.").arg(QGuiApplication::applicationDisplayName()));
        }

        if ((!m_autoConnect) && isMacAccessorySecurityLikelyToInterfere())
        {
            Utils::CheckableMessageBox::information(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Your Apple Silicon Mac has an \"Allow accessories to connect\" security setting "
                       "that can interrupt connections to your OpenMV Cam. When your OpenMV Cam is in bootloader "
                       "(DFU) mode for a firmware update, macOS may show an \"Allow accessory to connect\" "
                       "popup that must be clicked before the connection works.\n\n"
                       "To prevent this, open the Apple menu, go to System Settings, click Privacy & Security "
                       "in the sidebar, scroll to the bottom to Accessories, and change \"Allow accessories to connect\" "
                       "to \"Automatically allow when unlocked\" (recommended) or \"Always allow\"."),
                Utils::CheckableDecider(DONT_SHOW_MAC_ACCESSORY_AGAIN),
                QMessageBox::Ok,
                QMessageBox::Ok);
        }

        ///////////////////////////////////////////////////////////////////////

        m_working = false;
        m_firmwareUpdateInProgress = false;

        OpenMVPlugin::setPortPath(true);

        if(m_autoRun)
        {
            startClicked();
        }
        else
        {
            QTimer::singleShot(0, this, &OpenMVPlugin::workingDone);
        }

        return;
    }
    else
    {
        deferHigh([this, forceBootloader,
                         forceFirmwarePath,
                         forceFlashFSErase,
                         justEraseFlashFs,
                         installTheLatestDevelopmentFirmware,
                         waitForCamera,
                         previousMapping,
                         romfsAccess,
                         forceBootloaderEntry,
                         customFirmwareBundleDir] {
            connectClicked(forceBootloader,
                           forceFirmwarePath,
                           forceFlashFSErase,
                           justEraseFlashFs,
                           installTheLatestDevelopmentFirmware,
                           waitForCamera,
                           previousMapping,
                           romfsAccess,
                           forceBootloaderEntry,
                           customFirmwareBundleDir);
        });
    }
}

void OpenMVPlugin::disconnectClicked(bool reset, bool enterBootloader)
{
    if(m_connected)
    {
        if(!m_working)
        {
            m_working = true;

            bool v2ProtocolEnabled = m_iodevice->v2ProtocolEnabled();

            // Stopping ///////////////////////////////////////////////////////
            {
                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                        &loop, &QEventLoop::quit);

                if(reset)
                {
                    flushPortPath();

                    m_iodevice->sysReset(enterBootloader);
                }
                else
                {
                    if(m_stopOnConnectDiconnectionAction->isChecked()) m_iodevice->scriptStop();

                    if((!m_useGetState)
                    || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
                    || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
                    || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
                    {
                        // Drain text buffer
                        {
                            bool empty = bool();
                            bool *emptyPtr = &empty;

                            QEventLoop loop2;

                            QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                                this, [emptyPtr] (bool ok) {
                                *emptyPtr = ok;
                            });

                            connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                                    &loop2, &QEventLoop::quit);

                            while(!empty)
                            {
                                m_iodevice->getTxBuffer();
                                loop2.exec();
                            }

                            disconnect(conn2);
                        }

                        // Drain image buffer
                        {
                            bool empty = bool();
                            bool *emptyPtr = &empty;

                            QEventLoop loop2;

                            QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                                this, [emptyPtr] (bool ok) {
                                *emptyPtr = ok;
                            });

                            connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                                    &loop2, &QEventLoop::quit);

                            while(!empty)
                            {
                                m_iodevice->frameSizeDump();
                                loop2.exec();
                            }

                            disconnect(conn2);
                        }
                    } else {
                        bool printEmpty = bool(), frameBufferEmpty = bool();
                        bool *printEmptyPtr = &printEmpty, *frameBufferEmptyPtr = &frameBufferEmpty;

                        QEventLoop loop2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                            this, [printEmptyPtr] (bool ok) {
                            *printEmptyPtr = ok;
                        });

                        QMetaObject::Connection conn3 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                            this, [frameBufferEmptyPtr] (bool ok) {
                            *frameBufferEmptyPtr = ok;
                        });

                        connect(m_iodevice, &OpenMVPluginIO::getStateDone,
                                &loop2, &QEventLoop::quit);

                        while((!printEmpty) || (!frameBufferEmpty))
                        {
                            m_iodevice->getState();
                            loop2.exec();
                        }

                        disconnect(conn2);
                        disconnect(conn3);
                    }
                }

                m_iodevice->close();

                loop.exec();
            }

            m_jpgCompress->setVisible(false);
            m_jpgCompressMode->setVisible(false);

            if (!m_boardType.isEmpty() && v2ProtocolEnabled)
            {
                Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
                const Utils::Key jpgKey =
                    Utils::keyFromString(QStringLiteral(SETTINGS_GROUP "/" JPG_COMPRESS_STATE "_") + m_boardType);

                settings->setValue(jpgKey, m_jpgCompress->isChecked());
            }

            ///////////////////////////////////////////////////////////////////

            m_iodevice->getTimeout(); // clear

            m_frameSizeDumpTimer.restart();
            m_getScriptRunningTimer.restart();
            m_getTxBufferTimer.restart();

            m_timer.restart();
            m_queue.clear();
            m_cameraQueue.clear();
            m_connected = false;
        m_processEventsTimer->stop();
            m_running = false;
            m_major = int();
            m_minor = int();
            m_patch = int();
            // No cam attached -- fall back to the released examples/docs.
            m_developmentCam = false;
            loadExampleFilters(devResourceFolder(QStringLiteral("examples")));
            // LEAVE CACHED m_boardTypeFolder = QString();
            // LEAVE CACHED m_fullBoardType = QString();
            // LEAVE CACHED m_boardType = QString();
            // LEAVE CACHED m_boardId = QString();
            // LEAVE CACHED m_boardVID = 0;
            // LEAVE CACHED m_boardPID = 0;
            m_sensorType = QString();
            m_portName = QString();
            m_portPath = QString();
            m_portDriveSerialNumber = QString();
            m_errorFilterString = QString();

            m_availableDrives.clear();

            m_openDriveFolderAction->setEnabled(false);
            m_editWifiDebugAction->setEnabled(false);
            m_saveAction->setEnabled(false);
            m_resetAction->setEnabled(false);
            m_enterBootloaderAction->setEnabled(false);
            m_developmentReleaseAction->setEnabled(false);
            m_connectAction->setVisible(true);
            if(!m_autoReconnectAction->isChecked()) m_connectAction->setEnabled(true);
            m_disconnectAction->setVisible(false);
            if(!m_autoReconnectAction->isChecked()) m_disconnectAction->setEnabled(false);
            m_startAction->setEnabled(false);
            m_startAction->setVisible(true);
            m_stopAction->setEnabled(false);
            m_stopAction->setVisible(false);

            m_registerButton->setText(QString());
            m_registerButton->setVisible(false);
            m_registerButtonSpacer->setVisible(false);
            m_boardLabel->setDisabled(true);
            m_boardLabel->setText(Tr::tr("Board:"));
            m_sensorLabel->setDisabled(true);
            m_sensorLabel->setText(Tr::tr("Sensor:"));
            m_versionButton->setDisabled(true);
            m_versionButton->setText(Tr::tr("Firmware Version:"));
            m_portLabel->setDisabled(true);
            m_portLabel->setText(Tr::tr("Serial Port:"));
            m_pathButton->setDisabled(true);
            m_pathButton->setText(Tr::tr("Drive:"));
            m_fpsButton->setDisabled(true);
            m_fpsIde = 0.0;
            m_fpsCamera = 0.0;
            m_fpsCameraValid = false;
            m_fpsButton->setMinimumWidth(m_fpsButton->fontMetrics().horizontalAdvance(QStringLiteral("FPS: 000.000")));
            m_fpsButton->setToolTip(Tr::tr("May be different from camera FPS"));
            m_fpsButton->setText(Tr::tr("FPS:"));

            m_frameBuffer->enableSaveTemplate(false);
            m_frameBuffer->enableSaveDescriptor(false);

            Python::Internal::PyLSClient::setPortPath(Utils::FilePath::fromUserInput(m_portPath));

            ///////////////////////////////////////////////////////////////////

            m_working = false;

            clearDeferred();
        }
        else
        {
            deferHigh([this, reset] { disconnectClicked(reset); });
        }
    }

    QTimer::singleShot(0, this, &OpenMVPlugin::disconnectDone);
}

void OpenMVPlugin::startClicked()
{
    if(!m_working)
    {
        m_working = true;

        // Stopping ///////////////////////////////////////////////////////////
        {
            bool running2 = bool();
            bool *running2Ptr = &running2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::scriptRunning,
                this, [running2Ptr] (bool running) {
                *running2Ptr = running;
            });

            if(!m_iodevice->queueisEmpty())
            {
                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::queueEmpty,
                        &loop, &QEventLoop::quit);

                loop.exec(); // drain previous commands
            }

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::scriptRunning,
                    &loop, &QEventLoop::quit);

            if((!m_useGetState)
            || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
            || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
            || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
            {
                m_iodevice->getScriptRunning();
            }
            else
            {
                m_iodevice->getState();
            }

            loop.exec(); // sync with camera

            disconnect(conn);

            // Don't start if already running.
            if(running2)
            {
                m_working = false;
                QTimer::singleShot(0, this, &OpenMVPlugin::workingDone);
                return;
            }

            if(0) // previous behavior...
            {
                m_iodevice->scriptStop();

                if((!m_useGetState)
                || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
                || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
                || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
                {
                    // Drain text buffer
                    {
                        bool empty = bool();
                        bool *emptyPtr = &empty;

                        QEventLoop loop2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                            this, [emptyPtr] (bool ok) {
                            *emptyPtr = ok;
                        });

                        connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                                &loop2, &QEventLoop::quit);

                        while(!empty)
                        {
                            m_iodevice->getTxBuffer();
                            loop2.exec();
                        }

                        disconnect(conn2);
                    }

                    // Drain image buffer
                    {
                        bool empty = bool();
                        bool *emptyPtr = &empty;

                        QEventLoop loop2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                            this, [emptyPtr] (bool ok) {
                            *emptyPtr = ok;
                        });

                        connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                                &loop2, &QEventLoop::quit);

                        while(!empty)
                        {
                            m_iodevice->frameSizeDump();
                            loop2.exec();
                        }

                        disconnect(conn2);
                    }
                } else {
                    bool printEmpty = bool(), frameBufferEmpty = bool();
                    bool *printEmptyPtr = &printEmpty, *frameBufferEmptyPtr = &frameBufferEmpty;

                    QEventLoop loop2;

                    QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                        this, [printEmptyPtr] (bool ok) {
                        *printEmptyPtr = ok;
                    });

                    QMetaObject::Connection conn3 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                        this, [frameBufferEmptyPtr] (bool ok) {
                        *frameBufferEmptyPtr = ok;
                    });

                    connect(m_iodevice, &OpenMVPluginIO::getStateDone,
                            &loop2, &QEventLoop::quit);

                    while((!printEmpty) || (!frameBufferEmpty))
                    {
                        m_iodevice->getState();
                        loop2.exec();
                    }

                    disconnect(conn2);
                    disconnect(conn3);
                }
            }
        }

        ///////////////////////////////////////////////////////////////////////

        QByteArray contents;

        if(m_viewerMode)
        {
            // In viewer mode the editor is hidden, but a document can still be open
            // (e.g. a script passed on the command line, which the IDE opens and
            // -auto_run then runs). Run that if present; otherwise pick one from disk.
            Core::IEditor *editor = Core::EditorManager::currentEditor();
            contents = (editor && editor->document()) ? editor->document()->contents() : QByteArray();

            if(contents.isEmpty())
            {
                Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
                QString path = QFileDialog::getOpenFileName(Core::ICore::dialogParent(), Tr::tr("Run Script"),
                    settings->value(SETTINGS_GROUP "/" LAST_VIEWER_RUN_SCRIPT_PATH, m_portPath.isEmpty() ? QDir::homePath() : m_portPath).toString(),
                    Tr::tr("Python Files (*.py);;Text Files (*.txt);;All Files (*)"));

                QFile file(path);

                if(path.isEmpty() || (!file.open(QIODevice::ReadOnly)))
                {
                    if(!path.isEmpty())
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(), Tr::tr("Run Script"),
                            Tr::tr("Error: Cannot open \"%L1\"!").arg(path));
                    }

                    m_working = false;
                    QTimer::singleShot(0, this, &OpenMVPlugin::workingDone);
                    return;
                }

                contents = file.readAll();
                file.close();
                settings->setValue(SETTINGS_GROUP "/" LAST_VIEWER_RUN_SCRIPT_PATH, path);
            }
        }
        else
        {
            contents = Core::EditorManager::currentEditor() ? Core::EditorManager::currentEditor()->document() ? Core::EditorManager::currentEditor()->document()->contents() : QByteArray() : QByteArray();
        }

        if(importHelper(contents))
        {
            m_iodevice->scriptExec(contents);
            m_iodevice->jpegEnable(m_jpgCompress->isChecked());
            m_iodevice->fbEnable(!m_disableFrameBuffer->isChecked());

            m_timer.restart();
            m_queue.clear();
            m_cameraQueue.clear();
        }
        else
        {
            m_fpsIde = 0.0;
            m_fpsCamera = 0.0;
            refreshFpsButton();
        }

        ///////////////////////////////////////////////////////////////////////

        QEventLoop loop;
        QTimer::singleShot(1000, &loop, &QEventLoop::quit); // allow some time for the script to start
        loop.exec();

        m_working = false;

        QTimer::singleShot(0, this, &OpenMVPlugin::workingDone);
    }
    else
    {
        deferHigh([this] { startClicked(); });
    }
}

void OpenMVPlugin::stopClicked()
{
    if(!m_working)
    {
        m_working = true;

        // Stopping ///////////////////////////////////////////////////////////
        {
            bool running2 = bool();
            bool *running2Ptr = &running2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::scriptRunning,
                this, [running2Ptr] (bool running) {
                *running2Ptr = running;
            });

            if(!m_iodevice->queueisEmpty())
            {
                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::queueEmpty,
                        &loop, &QEventLoop::quit);

                loop.exec(); // drain previous commands
            }

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::scriptRunning,
                    &loop, &QEventLoop::quit);

            if((!m_useGetState)
            || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
            || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
            || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
            {
                m_iodevice->getScriptRunning();
            }
            else
            {
                m_iodevice->getState();
            }

            loop.exec(); // sync with camera

            disconnect(conn);

            if(running2)
            {
                m_iodevice->scriptStop();

                if((!m_useGetState)
                || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
                || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
                || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
                {
                    // Drain text buffer
                    {
                        bool empty = bool();
                        bool *emptyPtr = &empty;

                        QEventLoop loop2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                            this, [emptyPtr] (bool ok) {
                            *emptyPtr = ok;
                        });

                        connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                                &loop2, &QEventLoop::quit);

                        while(!empty)
                        {
                            m_iodevice->getTxBuffer();
                            loop2.exec();
                        }

                        disconnect(conn2);
                    }

                    // Drain image buffer
                    {
                        bool empty = bool();
                        bool *emptyPtr = &empty;

                        QEventLoop loop2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                            this, [emptyPtr] (bool ok) {
                            *emptyPtr = ok;
                        });

                        connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                                &loop2, &QEventLoop::quit);

                        while(!empty)
                        {
                            m_iodevice->frameSizeDump();
                            loop2.exec();
                        }

                        disconnect(conn2);
                    }
                } else {
                    bool printEmpty = bool(), frameBufferEmpty = bool();
                    bool *printEmptyPtr = &printEmpty, *frameBufferEmptyPtr = &frameBufferEmpty;

                    QEventLoop loop2;

                    QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                        this, [printEmptyPtr] (bool ok) {
                        *printEmptyPtr = ok;
                    });

                    QMetaObject::Connection conn3 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                        this, [frameBufferEmptyPtr] (bool ok) {
                        *frameBufferEmptyPtr = ok;
                    });

                    connect(m_iodevice, &OpenMVPluginIO::getStateDone,
                            &loop2, &QEventLoop::quit);

                    while((!printEmpty) || (!frameBufferEmpty))
                    {
                        m_iodevice->getState();
                        loop2.exec();
                    }

                    disconnect(conn2);
                    disconnect(conn3);
                }
            }
        }

        ///////////////////////////////////////////////////////////////////////

        m_fpsIde = 0.0;
        m_fpsCamera = 0.0;
        refreshFpsButton();

        ///////////////////////////////////////////////////////////////////////

        m_working = false;

        ///////////////////////////////////////////////////////////////////////

        // The "more examples" nag is developer onboarding -- irrelevant to a viewer running a
        // customer's product.
        if((!m_viewerMode)
            && (Core::EditorManager::currentEditor()
            ? Core::EditorManager::currentEditor()->document()
                ? Core::EditorManager::currentEditor()->document()->displayName() == QStringLiteral("helloworld_1.py")
                : false
            : false))
        {
            QTimer::singleShot(2000, this, &OpenMVPlugin::showExamplesDialog);
        }

        ///////////////////////////////////////////////////////////////////////

        QTimer::singleShot(0, this, &OpenMVPlugin::workingDone);
    }
    else
    {
        deferHigh([this] { stopClicked(); });
    }
}

void OpenMVPlugin::showExamplesDialog()
{
    if (!QApplication::activeModalWidget()) {
        Utils::CheckableMessageBox::information(Core::ICore::dialogParent(),
                Tr::tr("More Examples"),
                Tr::tr("You can find more examples under the File -> Examples menu.\n\n"
                   "In particular, checkout the Image Processing -> Color-Tracking and Machine Learning -> TensorFlow examples."),
                Utils::CheckableDecider(DONT_SHOW_EXAMPLES_AGAIN),
                QMessageBox::Ok,
                QMessageBox::Ok);
    } else {
        QTimer::singleShot(2000, this, &OpenMVPlugin::showExamplesDialog); // try again
    }
}

// The newest firmware we ship is the global "firmware_version", but a board may
// pin its own last-shipped firmware via a per-board "firmware_version" (for boards
// we no longer build newer firmware for -- e.g. the Arduino Nano boards capped at
// 4.7.0). When the connected board pins one, treat that as "latest" for it so it
// isn't flagged out of date for firmware we never released. Third-party boards
// never fall back to the global version (it describes OpenMV firmware, not
// theirs): without a per-board version they are simply never out of date.
QString OpenMVPlugin::latestFirmwareForConnectedBoard() const
{
    QString latestFirmware = m_boardResourceRoot.isEmpty()
        ? m_firmwareSettings.object().value(QStringLiteral("firmware_version")).toString()
        : QString();

    if(!m_boardTypeFolder.isEmpty())
    {
        for(const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            const QJsonObject board = value.toObject();

            if((board.value(QStringLiteral("boardFirmwareFolder")).toString() == m_boardTypeFolder)
            && board.contains(QStringLiteral("firmware_version")))
            {
                latestFirmware = board.value(QStringLiteral("firmware_version")).toString();
                break;
            }
        }
    }

    return latestFirmware;
}

void OpenMVPlugin::updateCam(bool forceYes)
{
    if(!m_working)
    {
        QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).
            match(latestFirmwareForConnectedBoard());

        if((m_major < match.captured(1).toInt())
        || ((m_major == match.captured(1).toInt()) && (m_minor < match.captured(2).toInt()))
        || ((m_major == match.captured(1).toInt()) && (m_minor == match.captured(2).toInt()) && (m_patch < match.captured(3).toInt())))
        {
            Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

            QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            dialog->setWindowTitle(Tr::tr("Firmware Update"));
            QFormLayout *layout = new QFormLayout(dialog);

            layout->addWidget(new QLabel(forceYes ? Tr::tr("Upgrade options:") : Tr::tr("Update your OpenMV Cam's firmware to the latest version?")));

            QHBoxLayout *layout2 = new QHBoxLayout;
            layout2->setContentsMargins(6, 0, 0, 0);
            QWidget *widget = new QWidget;
            widget->setLayout(layout2);

            QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
            checkBox->setChecked(settings->value(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
            layout2->addWidget(checkBox);
            checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                        "This does not erase files on any removable SD card (if inserted)."));

            QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Reset ROMFS file system"));
            checkBox2->setChecked(settings->value(SETTINGS_GROUP "/" LAST_DFU_RESET_ROM_FS_STATE, false).toBool());
            layout2->addWidget(checkBox2);
            checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be reset back to default."));

            layout->addRow(widget);

            if((m_major < OPENMV_FORCE_ROMFS_UPGRADE_MAJOR)
            || ((m_major == OPENMV_FORCE_ROMFS_UPGRADE_MAJOR) && (m_minor < OPENMV_FORCE_ROMFS_UPGRADE_MINOR))
            || ((m_major == OPENMV_FORCE_ROMFS_UPGRADE_MAJOR) && (m_minor == OPENMV_FORCE_ROMFS_UPGRADE_MINOR) && (m_patch < OPENMV_FORCE_ROMFS_UPGRADE_PATCH)))
            {
                checkBox->setChecked(true);
                checkBox->setEnabled(false);
                checkBox2->setChecked(true);
                checkBox2->setEnabled(false);

                layout->addWidget(new QLabel(Tr::tr("Warning: Upgrading to the new firmware version requires the FAT file system to be erased.")));
            }

            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            layout2->addSpacing(80);
            layout2->addWidget(box);
            layout->addRow(widget);

            connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
            connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

            bool ok = dialog->exec() == QDialog::Accepted;

            if(ok)
            {
                if (checkBox->isEnabled())
                    settings->setValue(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, checkBox->isChecked());
                if (checkBox2->isEnabled())
                    settings->setValue(SETTINGS_GROUP "/" LAST_DFU_RESET_ROM_FS_STATE, checkBox2->isChecked());
            }


            if(ok)
            {
                disconnectClicked();

                if(ExtensionSystem::PluginManager::specForPlugin(this)->state() != ExtensionSystem::PluginSpec::Stopped)
                {
                    connectClicked(true, QString(), checkBox->isChecked(), false, false, false, QString(),
                                   checkBox2->isChecked() ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE);
                }
            }

            delete dialog;
        }
        else
        {
            QMessageBox::information(Core::ICore::dialogParent(),
                Tr::tr("Firmware Update"),
                Tr::tr("Your OpenMV Cam's firmware is up to date."));

            Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

            QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            dialog->setWindowTitle(Tr::tr("Firmware Update"));
            QFormLayout *layout = new QFormLayout(dialog);

            layout->addWidget(new QLabel(Tr::tr("Need to reset your OpenMV Cam's firmware to the release version?")));

            QHBoxLayout *layout2 = new QHBoxLayout;
            layout2->setContentsMargins(6, 0, 0, 0);
            QWidget *widget = new QWidget;
            widget->setLayout(layout2);

            QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
            checkBox->setChecked(settings->value(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
            layout2->addWidget(checkBox);
            checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                        "This does not erase files on any removable SD card (if inserted)."));

            QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Reset ROMFS file system"));
            checkBox2->setChecked(settings->value(SETTINGS_GROUP "/" LAST_DFU_RESET_ROM_FS_STATE, false).toBool());
            layout2->addWidget(checkBox2);
            checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be reset back to default."));

            layout->addRow(widget);

            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            layout2->addSpacing(80);
            layout2->addWidget(box);
            layout->addRow(widget);

            connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
            connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

            bool ok = dialog->exec() == QDialog::Accepted;

            if(ok)
            {
                settings->setValue(SETTINGS_GROUP "/" LAST_DFU_FLASH_FS_ERASE_STATE, checkBox->isChecked());
                settings->setValue(SETTINGS_GROUP "/" LAST_DFU_RESET_ROM_FS_STATE, checkBox2->isChecked());
            }


            if(ok)
            {
                disconnectClicked();

                if(ExtensionSystem::PluginManager::specForPlugin(this)->state() != ExtensionSystem::PluginSpec::Stopped)
                {
                    connectClicked(true, QString(), checkBox->isChecked(), false, false, false, QString(),
                                   checkBox2->isChecked() ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE);
                }
            }

            delete dialog;
        }
    }
    else
    {
        deferNormal([this, forceYes] { updateCam(forceYes); });
    }
}

QJsonObject OpenMVPlugin::getBoardSettings(const QString &title, Utils::QtcSettings *settings, bool autoConnectToBoard)
{
    if (m_nonDFUBoardPresent && (!m_working) && autoConnectToBoard)
    {
        QEventLoop loop;
        connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);
        QTimer::singleShot(0, this, [this] { connectClicked(); });
        loop.exec();
    }

    if (m_connected)
    {
        QSerialPortInfo raw_tempPort = QSerialPortInfo(m_portName);
        MyQSerialPortInfo tempPort(raw_tempPort);

        QString temp = QString(m_fullBoardType).simplified();

        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            // Don't ignore "hidden" here.
            if ((value.toObject().value(QStringLiteral("boardArchString")).toString().toLower() == temp.toLower())
            && matchVidPid(value.toObject(), QString(), tempPort))
            {
                return value.toObject();
            }
        }

        QMessageBox::critical(Core::ICore::dialogParent(),
            title,
            Tr::tr("No board settings for the connected board found!"));

        return QJsonObject();
    }
    else
    {
        QMap<QString, QJsonObject> mappingsHumanReadable;

        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            if (!value.toObject().value(QStringLiteral("hidden")).toBool())
            {
                mappingsHumanReadable.insert(value.toObject().value(QStringLiteral("boardDisplayName")).toString(), value.toObject());
            }
        }

        int index = mappingsHumanReadable.keys().indexOf(
            settings->value(SETTINGS_GROUP "/" LAST_BOARD_TYPE_STATE_GET).toString());

        bool ok = mappingsHumanReadable.size() == 1;
        QString temp = (mappingsHumanReadable.size() == 1) ? mappingsHumanReadable.keys().first() : QInputDialog::getItem(Core::ICore::dialogParent(),
            title, Tr::tr("Please select the board type"),
            mappingsHumanReadable.keys(), (index != -1) ? index : 0, false, &ok,
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

        if(ok)
        {
            settings->setValue(SETTINGS_GROUP "/" LAST_BOARD_TYPE_STATE_GET, temp);
            return mappingsHumanReadable.value(temp); // Get mappings key.
        }

        return QJsonObject();
    }
}

} // namespace Internal
} // namespace OpenMV
