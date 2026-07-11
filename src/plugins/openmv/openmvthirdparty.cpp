/* Copyright (C) 2026 OpenMV, LLC.
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

#include <QtCore>
#include <QtNetwork>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>

#include "openmvthirdparty.h"
#include "openmvtr.h"
#include "qzip/qzipreader.h"

namespace OpenMV {
namespace Internal {

static QList<OpenMVThirdParty::OverrideRecord> s_mergedOverrides;

static const QRegularExpression &vendorIdRegex()
{
    static const QRegularExpression re(QStringLiteral("^[a-z][a-z0-9_-]{1,31}$"));
    return re;
}

static bool copyFileOperator(const Utils::FilePath &src, const Utils::FilePath &dest, QString *error)
{
    dest.parentDir().ensureWritableDir();

    if (!src.copyFile(dest))
    {
        if (error)
        {
            *error = Tr::tr("Could not copy file \"%1\" to \"%2\".").arg(src.toUserOutput(), dest.toUserOutput());
        }

        return false;
    }

    return true;
}

// The sidecar "<part>.version" files hold a single version string ("1.2.0").
// Missing/unreadable => empty string, which orders below every real version.
static QString readVersionFile(const Utils::FilePath &path)
{
    QFile file(path.toString());

    if (file.open(QIODevice::ReadOnly))
    {
        return QString::fromUtf8(file.readAll()).trimmed();
    }

    return QString();
}

static bool versionGreater(const QString &a, const QString &b)
{
    return QVersionNumber::compare(QVersionNumber::fromString(a),
                                   QVersionNumber::fromString(b)) > 0;
}

static OpenMVThirdParty::Channel parseChannel(const QJsonObject &part, const QString &channel)
{
    QJsonObject obj = part.value(channel).toObject();

    OpenMVThirdParty::Channel result;
    result.version = obj.value(QStringLiteral("version")).toString();
    result.url = obj.value(QStringLiteral("url")).toString();
    result.sha256 = obj.value(QStringLiteral("sha256")).toString();
    return result;
}

// Parse config.json contents. context names the source in errors (folder name
// or URL); expectedName, when set, must match the "name" field (folder repos).
static bool parseConfigData(const QByteArray &data, const QString &context, const QString &expectedName,
                            OpenMVThirdParty::Repo *repo, QString *error)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        *error = Tr::tr("\"%L1\" config.json - %L2").arg(context).arg(parseError.errorString());
        return false;
    }

    QJsonObject obj = doc.object();
    QString name = obj.value(QStringLiteral("name")).toString();

    if (!vendorIdRegex().match(name).hasMatch())
    {
        *error = Tr::tr("\"%L1\" config.json - invalid repository name \"%L2\"").arg(context).arg(name);
        return false;
    }

    if ((!expectedName.isEmpty()) && (name != expectedName))
    {
        *error = Tr::tr("\"%L1\" config.json - name \"%L2\" does not match the folder name").arg(context).arg(name);
        return false;
    }

    repo->id = name;
    repo->displayName = obj.value(QStringLiteral("displayName")).toString();

    if (repo->displayName.isEmpty())
    {
        repo->displayName = name;
    }

    repo->homepage = obj.value(QStringLiteral("homepage")).toString();
    repo->configUrl = obj.value(QStringLiteral("configUrl")).toString();

    QJsonObject firmware = obj.value(QStringLiteral("firmware")).toObject();
    repo->firmwareRelease = parseChannel(firmware, QStringLiteral("release"));
    repo->firmwareDev = parseChannel(firmware, QStringLiteral("development"));

    QJsonObject examples = obj.value(QStringLiteral("examples")).toObject();
    repo->examplesRelease = parseChannel(examples, QStringLiteral("release"));

    QJsonObject models = obj.value(QStringLiteral("models")).toObject();
    repo->modelsRelease = parseChannel(models, QStringLiteral("release"));

    return true;
}

static bool parseConfig(const Utils::FilePath &vendorDir, OpenMVThirdParty::Repo *repo, QString *error)
{
    QFile file(vendorDir.pathAppended(QStringLiteral("config.json")).toString());

    if (!file.open(QIODevice::ReadOnly))
    {
        *error = Tr::tr("\"%L1\" does not have a readable config.json").arg(vendorDir.fileName());
        return false;
    }

    return parseConfigData(file.readAll(), vendorDir.fileName(), vendorDir.fileName(), repo, error);
}

static QStringList vendorFolderNames(const Utils::FilePath &root)
{
    return QDir(root.toString()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

// VID:PID parsing that mirrors matchVidPid()/isBootloaderType() in
// openmvpluginconnect.cpp: "VVVV:PPPP" hex strings, a hex boardPidMask, and
// matching defined as (devicePid & mask) == pid.
static bool parseVidPid(const QString &vidPid, const QString &pidMask, int *vid, int *pid, int *mask)
{
    QStringList list = vidPid.split(QStringLiteral(":"));

    if (list.size() != 2)
    {
        return false;
    }

    bool vidOk, pidOk;
    *vid = list.at(0).toInt(&vidOk, 16);
    *pid = list.at(1).toInt(&pidOk, 16);

    bool maskOk;
    *mask = pidMask.toInt(&maskOk, 16);

    if (!maskOk)
    {
        *mask = 0xFFFF;
    }

    return vidOk && pidOk;
}

static bool boardVidPid(const QJsonObject &board, int *vid, int *pid, int *mask)
{
    return parseVidPid(board.value(QStringLiteral("boardVidPid")).toString(),
                       board.value(QStringLiteral("boardPidMask")).toString(),
                       vid, pid, mask);
}

// True when some device PID can satisfy both masked matches - i.e. the two
// entries agree on every bit where both masks care.
static bool vidPidOverlap(int vidA, int pidA, int maskA, int vidB, int pidB, int maskB)
{
    return (vidA == vidB) && ((pidA & maskA & maskB) == (pidB & maskA & maskB));
}

Utils::FilePath OpenMVThirdParty::installRoot()
{
    return Core::ICore::resourcePath(QStringLiteral("third-party"));
}

Utils::FilePath OpenMVThirdParty::writableRoot()
{
    return Core::ICore::allUsersResourcePath(QStringLiteral("third-party"));
}

void OpenMVThirdParty::mirrorInstallDirRepos(QStringList *warnings)
{
    Utils::FilePath in = installRoot();

    if (!in.exists())
    {
        return;
    }

    for (const QString &vendor : vendorFolderNames(in))
    {
        Utils::FilePath vendorIn = in.pathAppended(vendor);

        Repo repo;
        QString error;

        if (!parseConfig(vendorIn, &repo, &error))
        {
            warnings->append(error);
            continue;
        }

        Utils::FilePath vendorOut = writableRoot().pathAppended(vendor);

        if (!vendorOut.exists())
        {
            if (!Utils::FileUtils::copyRecursively(vendorIn, vendorOut, &error, copyFileOperator))
            {
                warnings->append(Tr::tr("\"%L1\" - failed to install: %L2").arg(vendor).arg(error));
                vendorOut.removeRecursively();
            }

            continue;
        }

        // The vendor already exists in the writable area (from a previous run or
        // a network update). A part is only refreshed when the install dir ships
        // a strictly newer sidecar version, so newer network-updated content is
        // never clobbered by an older bundled copy, while a newer installer run
        // still pushes its content through.
        bool updated = false;

        for (const QString &part : {QStringLiteral("firmware"), QStringLiteral("examples"), QStringLiteral("models")})
        {
            QString sidecar = part + QStringLiteral(".version");

            if (!vendorIn.pathAppended(part).exists())
            {
                continue;
            }

            if (versionGreater(readVersionFile(vendorIn.pathAppended(sidecar)),
                               readVersionFile(vendorOut.pathAppended(sidecar))))
            {
                Utils::FilePath outSidecar = vendorOut.pathAppended(sidecar);

                if (outSidecar.exists())
                {
                    outSidecar.removeFile();
                }

                QString error2;

                if ((!vendorOut.pathAppended(part).removeRecursively(&error2))
                || (!Utils::FileUtils::copyRecursively(vendorIn.pathAppended(part), vendorOut.pathAppended(part), &error2, copyFileOperator))
                || (!vendorIn.pathAppended(sidecar).copyFile(outSidecar)))
                {
                    warnings->append(Tr::tr("\"%L1\" - failed to update \"%L2\": %L3").arg(vendor).arg(part).arg(error2));
                    continue;
                }

                updated = true;
            }
        }

        if (updated)
        {
            Utils::FilePath outConfig = vendorOut.pathAppended(QStringLiteral("config.json"));

            if (outConfig.exists())
            {
                outConfig.removeFile();
            }

            vendorIn.pathAppended(QStringLiteral("config.json")).copyFile(outConfig);
        }
    }
}

QList<OpenMVThirdParty::Repo> OpenMVThirdParty::scanRepos(QStringList *warnings)
{
    QList<Repo> repos;

    Utils::FilePath root = writableRoot();

    if (!root.exists())
    {
        return repos;
    }

    for (const QString &vendor : vendorFolderNames(root))
    {
        Utils::FilePath vendorDir = root.pathAppended(vendor);

        Repo repo;
        QString error;

        if (!parseConfig(vendorDir, &repo, &error))
        {
            if (warnings)
            {
                warnings->append(error);
            }

            continue;
        }

        repo.writablePath = vendorDir;
        repo.fromInstallDir = installRoot().pathAppended(vendor).exists();
        repo.firmwareVersion = readVersionFile(vendorDir.pathAppended(QStringLiteral("firmware.version")));
        repo.examplesVersion = readVersionFile(vendorDir.pathAppended(QStringLiteral("examples.version")));
        repo.modelsVersion = readVersionFile(vendorDir.pathAppended(QStringLiteral("models.version")));

        repos.append(repo);
    }

    // Apply the user priority order (highest first). Listed repos come first in
    // that order; the rest (freshly installed, not yet ordered) keep their
    // alphabetical order after them.
    const QStringList order = repoOrder();

    std::stable_sort(repos.begin(), repos.end(), [&order] (const Repo &a, const Repo &b) {
        int ia = order.indexOf(a.id);
        int ib = order.indexOf(b.id);
        if (ia < 0) ia = order.size();
        if (ib < 0) ib = order.size();
        return ia < ib;
    });

    return repos;
}

QStringList OpenMVThirdParty::repoOrder()
{
    return ExtensionSystem::PluginManager::settings()->value(THIRD_PARTY_REPO_ORDER).toStringList();
}

void OpenMVThirdParty::setRepoOrder(const QStringList &order)
{
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->setValue(THIRD_PARTY_REPO_ORDER, order);
    settings->sync();
}

QJsonDocument OpenMVThirdParty::mergeFirmwareSettings(const QJsonDocument &builtIn,
                                                      const QList<Repo> &repos,
                                                      QList<OverrideRecord> *overrides,
                                                      QStringList *warnings)
{
    QJsonObject root = builtIn.object();
    QJsonArray boards = root.value(QStringLiteral("boards")).toArray();
    QJsonArray sensors = root.value(QStringLiteral("sensors")).toArray();

    for (const Repo &repo : repos)
    {
        QFile file(repo.writablePath.pathAppended(QStringLiteral("firmware/settings.json")).toString());

        if (!file.open(QIODevice::ReadOnly))
        {
            warnings->append(Tr::tr("\"%L1\" - missing firmware/settings.json").arg(repo.id));
            continue;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);

        if (parseError.error != QJsonParseError::NoError)
        {
            warnings->append(Tr::tr("\"%L1\" firmware/settings.json - %L2").arg(repo.id).arg(parseError.errorString()));
            continue;
        }

        QJsonObject vendorRoot = doc.object();

        // Protocol timing tables are IDE-core behavior and the global
        // firmware_version drives updates for OpenMV boards; third-party boards
        // must carry a per-board "firmware_version" instead.
        if (vendorRoot.contains(QStringLiteral("protocol")))
        {
            warnings->append(Tr::tr("\"%L1\" firmware/settings.json - \"protocol\" is not allowed (ignored)").arg(repo.id));
        }

        if (vendorRoot.contains(QStringLiteral("firmware_version")))
        {
            warnings->append(Tr::tr("\"%L1\" firmware/settings.json - top-level \"firmware_version\" is ignored (use a per-board \"firmware_version\")").arg(repo.id));
        }

        QString resourceRoot = repo.writablePath.pathAppended(QStringLiteral("firmware")).toString();

        for (const QJsonValue &value : vendorRoot.value(QStringLiteral("boards")).toArray())
        {
            QJsonObject board = value.toObject();

            // Leading-underscore keys are reserved for the IDE's annotations.
            for (const QString &key : board.keys())
            {
                if (key.startsWith(QLatin1Char('_')))
                {
                    board.remove(key);
                }
            }

            QString displayName = board.value(QStringLiteral("boardDisplayName")).toString();
            QString folder = board.value(QStringLiteral("boardFirmwareFolder")).toString();

            if (displayName.isEmpty())
            {
                displayName = folder;
            }

            int vid, pid, mask;

            if (!boardVidPid(board, &vid, &pid, &mask))
            {
                warnings->append(Tr::tr("\"%L1\" board \"%L2\" - missing or invalid \"boardVidPid\" (skipped)").arg(repo.id).arg(displayName));
                continue;
            }

            if (folder.isEmpty())
            {
                warnings->append(Tr::tr("\"%L1\" board \"%L2\" - missing \"boardFirmwareFolder\" (skipped)").arg(repo.id).arg(displayName));
                continue;
            }

            if (!board.contains(QStringLiteral("firmware_version")))
            {
                warnings->append(Tr::tr("\"%L1\" board \"%L2\" - no per-board \"firmware_version\" (the board will never be offered firmware updates)").arg(repo.id).arg(displayName));
            }

            // A vendor may not reuse another vendor's (or the IDE's) firmware
            // folder name - the folder name is a key in version lookups and in
            // bootloaderSettings command paths. Entries from the SAME vendor may
            // share a folder (built-in boards do this for legacy VID:PID rows).
            bool folderConflict = false;

            for (const QJsonValue &existingValue : boards)
            {
                QJsonObject existing = existingValue.toObject();

                if ((existing.value(QStringLiteral("boardFirmwareFolder")).toString() == folder)
                && (existing.value(QStringLiteral("_vendor")).toString() != repo.id))
                {
                    warnings->append(Tr::tr("\"%L1\" board \"%L2\" - \"boardFirmwareFolder\" \"%L3\" is already used by \"%L4\" (skipped)")
                                     .arg(repo.id).arg(displayName).arg(folder)
                                     .arg(existing.value(QStringLiteral("_vendor")).toString().isEmpty()
                                          ? QGuiApplication::applicationDisplayName()
                                          : existing.value(QStringLiteral("_vendor")).toString()));
                    folderConflict = true;
                    break;
                }
            }

            if (folderConflict)
            {
                continue;
            }

            // Masked app VID:PID overlap. Repos are processed highest priority
            // first, so an already-merged board that overlaps is either an
            // OpenMV base board (this vendor overrides it - the base entry is
            // removed and the override recorded for the UI) or a board from a
            // HIGHER-priority vendor (which wins - this board is rejected). The
            // user reorders priority in the preferences page.
            QJsonArray keptBoards;
            QString shadowedBy;

            for (const QJsonValue &existingValue : boards)
            {
                QJsonObject existing = existingValue.toObject();

                int existingVid, existingPid, existingMask;

                if (boardVidPid(existing, &existingVid, &existingPid, &existingMask)
                && vidPidOverlap(vid, pid, mask, existingVid, existingPid, existingMask))
                {
                    QString existingVendor = existing.value(QStringLiteral("_vendor")).toString();

                    if (existingVendor.isEmpty())
                    {
                        // OpenMV base board -> this vendor overrides it (drop it).
                        OverrideRecord record;
                        record.vendorId = repo.id;
                        record.vendorBoard = displayName;
                        record.vendorVidPid = board.value(QStringLiteral("boardVidPid")).toString();
                        record.overriddenBoard = existing.value(QStringLiteral("boardDisplayName")).toString();
                        record.overriddenVidPid = existing.value(QStringLiteral("boardVidPid")).toString();
                        overrides->append(record);
                        continue;
                    }

                    // A higher-priority vendor already claimed this id -> keep it.
                    shadowedBy = existingVendor;
                }

                keptBoards.append(existingValue);
            }

            boards = keptBoards;

            if (!shadowedBy.isEmpty())
            {
                warnings->append(Tr::tr("\"%L1\" board \"%L2\" (%L3) - USB id already provided by the higher-priority repository \"%L4\" (skipped)")
                                 .arg(repo.id).arg(displayName).arg(board.value(QStringLiteral("boardVidPid")).toString()).arg(shadowedBy));
                continue;
            }

            // The vendor app VID:PID overlapping a remaining board's bootloader
            // VID:PID is kept as a visible note only - bootloader IDs are shared
            // infrastructure (signed drivers) and the DFU-detected dialog already
            // disambiguates by asking the user.
            for (const QJsonValue &existingValue : boards)
            {
                QJsonObject existing = existingValue.toObject();

                int blVid, blPid, blMask;

                if (parseVidPid(existing.value(QStringLiteral("bootloaderVidPid")).toString(), QString(), &blVid, &blPid, &blMask)
                && vidPidOverlap(vid, pid, mask, blVid, blPid, blMask))
                {
                    OverrideRecord record;
                    record.vendorId = repo.id;
                    record.vendorBoard = displayName;
                    record.vendorVidPid = board.value(QStringLiteral("boardVidPid")).toString();
                    record.overriddenBoard = existing.value(QStringLiteral("boardDisplayName")).toString();
                    record.overriddenVidPid = existing.value(QStringLiteral("bootloaderVidPid")).toString();
                    record.bootloaderOnly = true;
                    overrides->append(record);
                }
            }

            board[QStringLiteral("_resourceRoot")] = resourceRoot;
            board[QStringLiteral("_vendor")] = repo.id;
            boards.append(board);
        }

        for (const QJsonValue &value : vendorRoot.value(QStringLiteral("sensors")).toArray())
        {
            QJsonObject sensor = value.toObject();

            for (const QString &key : sensor.keys())
            {
                if (key.startsWith(QLatin1Char('_')))
                {
                    sensor.remove(key);
                }
            }

            bool sensorConflict = false;

            for (const QJsonValue &existingValue : sensors)
            {
                if (existingValue.toObject().value(QStringLiteral("id")) == sensor.value(QStringLiteral("id")))
                {
                    warnings->append(Tr::tr("\"%L1\" sensor \"%L2\" - id conflicts with an existing sensor (skipped)")
                                     .arg(repo.id).arg(sensor.value(QStringLiteral("name")).toString()));
                    sensorConflict = true;
                    break;
                }
            }

            if (sensorConflict)
            {
                continue;
            }

            sensor[QStringLiteral("_vendor")] = repo.id;
            sensors.append(sensor);
        }
    }

    root[QStringLiteral("boards")] = boards;
    root[QStringLiteral("sensors")] = sensors;

    s_mergedOverrides = *overrides;

    return QJsonDocument(root);
}

QList<OpenMVThirdParty::OverrideRecord> OpenMVThirdParty::mergedOverrides()
{
    return s_mergedOverrides;
}

QJsonArray OpenMVThirdParty::boardsPreferringResourceRoot(const QJsonDocument &settings,
                                                          const QString &resourceRoot)
{
    QJsonArray preferred, others;

    for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())
    {
        if (value.toObject().value(QStringLiteral("_resourceRoot")).toString() == resourceRoot)
        {
            preferred.append(value);
        }
        else
        {
            others.append(value);
        }
    }

    for (const QJsonValue &value : others)
    {
        preferred.append(value);
    }

    return preferred;
}

Utils::FilePath OpenMVThirdParty::firmwareRootForBoard(const QJsonObject &board)
{
    QString resourceRoot = board.value(QStringLiteral("_resourceRoot")).toString();

    if (!resourceRoot.isEmpty())
    {
        return Utils::FilePath::fromString(resourceRoot);
    }

    return Core::ICore::allUsersResourcePath(QStringLiteral("firmware"));
}

bool OpenMVThirdParty::noteNewRepos(const QList<Repo> &repos)
{
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

    QStringList known = settings->value(KNOWN_THIRD_PARTY_REPOS).toStringList();
    QStringList updated = known;
    bool anyNew = false;

    for (const Repo &repo : repos)
    {
        if (!known.contains(repo.id))
        {
            updated.append(repo.id);
            anyNew = true;
        }
    }

    if (anyNew)
    {
        settings->setValue(KNOWN_THIRD_PARTY_REPOS, updated);
        settings->sync();
    }

    return anyNew;
}

// Blocking GET on the calling thread via a local event loop (the pattern the
// dev-resource sync uses). SSL errors are ignored to match that behavior. An
// optional progress dialog shows download progress and can abort the request.
static QByteArray httpGetBlocking(const QUrl &url, QString *error, QProgressDialog *progress)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply] (const QList<QSslError> &) {
        reply->ignoreSslErrors();
    });

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    if (progress)
    {
        QObject::connect(reply, &QNetworkReply::downloadProgress, progress, [progress] (qint64 received, qint64 total) {
            if (total > 0)
            {
                progress->setRange(0, 1000);
                progress->setValue(int((received * 1000) / total));
            }
        });

        QObject::connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    }

    loop.exec();

    QByteArray data = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);

    if ((!ok) && error)
    {
        *error = reply->errorString();
    }

    reply->deleteLater();
    return ok ? data : QByteArray();
}

// Extract a payload zip (one top-level directory whose contents are the part's
// payload) and swap it in as <vendorDir>/<part>.
static bool installArchiveToPart(const QByteArray &data, const Utils::FilePath &vendorDir,
                                 const QString &part, QString *error)
{
    Utils::FilePath staging = vendorDir.pathAppended(QStringLiteral(".staging"));
    staging.removeRecursively();

    QByteArray copy = data;
    QBuffer buffer(&copy);
    buffer.open(QIODevice::ReadOnly);
    QZipReader reader(&buffer);

    if (!reader.extractAll(staging.toString()))
    {
        *error = Tr::tr("failed to extract the downloaded archive");
        staging.removeRecursively();
        return false;
    }

    QStringList entries = QDir(staging.toString()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    if (entries.size() != 1)
    {
        *error = Tr::tr("the downloaded archive must contain exactly one top-level folder");
        staging.removeRecursively();
        return false;
    }

    Utils::FilePath target = vendorDir.pathAppended(part);
    QString removeError;

    if (!target.removeRecursively(&removeError))
    {
        *error = removeError;
        staging.removeRecursively();
        return false;
    }

    if (!QDir().rename(staging.pathAppended(entries.first()).toString(), target.toString()))
    {
        *error = Tr::tr("failed to move the extracted folder into place");
        staging.removeRecursively();
        return false;
    }

    staging.removeRecursively();
    return true;
}

static bool writeVendorFile(const Utils::FilePath &path, const QByteArray &data, QString *error)
{
    QFile file(path.toString());

    if ((!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) || (file.write(data) != data.size()))
    {
        *error = Tr::tr("failed to write \"%L1\"").arg(path.toUserOutput());
        return false;
    }

    return true;
}

// Run fn once no modal dialog is open, polling so an update prompt can never
// stack on top of another popup (the resources update, a connect dialog, ...).
static void whenNoModal(QObject *context, std::function<void()> fn)
{
    if (!QApplication::activeModalWidget())
    {
        fn();
        return;
    }

    QTimer::singleShot(1000, context, [context, fn] { whenNoModal(context, fn); });
}

static void offerRestart(QWidget *parent)
{
    if (QMessageBox::question(parent,
        Tr::tr("Third Party Repositories"),
        Tr::tr("Changes take effect after restarting.\n\nRestart %L1 now?").arg(QGuiApplication::applicationDisplayName()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
    {
        Core::ICore::restart();
    }
}

void OpenMVThirdParty::launchUpdateCheck(const QList<Repo> &repos, int parts, QObject *context,
                                         std::function<void(const QList<UpdateCheck> &)> onDone)
{
    QList<Repo> updatable;

    for (const Repo &repo : repos)
    {
        if (!repo.configUrl.isEmpty())
        {
            updatable.append(repo);
        }
    }

    if (updatable.isEmpty())
    {
        onDone(QList<UpdateCheck>());
        return;
    }

    QNetworkAccessManager *manager = new QNetworkAccessManager(context);
    auto results = std::make_shared<QList<UpdateCheck> >();
    auto pending = std::make_shared<int>(updatable.size());

    for (const Repo &repo : updatable)
    {
        QNetworkRequest request(QUrl(repo.configUrl));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply *reply = manager->get(request);
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply] (const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });

        QObject::connect(reply, &QNetworkReply::finished, manager,
                         [manager, results, pending, repo, parts, reply, onDone] {
            reply->deleteLater();

            if (reply->error() == QNetworkReply::NoError)
            {
                UpdateCheck check;
                check.repo = repo;
                check.remoteConfig = reply->readAll();

                QString error;

                if (parseConfigData(check.remoteConfig, repo.configUrl, repo.id, &check.remote, &error))
                {
                    if ((parts & FirmwarePart) && check.remote.firmwareRelease.isValid()
                    && versionGreater(check.remote.firmwareRelease.version, repo.firmwareVersion))
                    {
                        check.parts |= FirmwarePart;
                    }

                    if ((parts & ExamplesPart) && check.remote.examplesRelease.isValid()
                    && versionGreater(check.remote.examplesRelease.version, repo.examplesVersion))
                    {
                        check.parts |= ExamplesPart;
                    }

                    if ((parts & ModelsPart) && check.remote.modelsRelease.isValid()
                    && versionGreater(check.remote.modelsRelease.version, repo.modelsVersion))
                    {
                        check.parts |= ModelsPart;
                    }

                    if (check.parts)
                    {
                        results->append(check);
                    }
                }
                else
                {
                    qWarning("[Third Party Repositories] update check: %s", qPrintable(error));
                }
            }
            else
            {
                qWarning("[Third Party Repositories] update check \"%s\": %s",
                         qPrintable(repo.id), qPrintable(reply->errorString()));
            }

            if (--(*pending) == 0)
            {
                manager->deleteLater();
                onDone(*results);
            }
        });
    }
}

bool OpenMVThirdParty::installParts(const UpdateCheck &check, QString *error, QWidget *parent)
{
    struct PartInfo { int bit; QString name; Channel channel; };

    const QList<PartInfo> partList = {
        { FirmwarePart, QStringLiteral("firmware"), check.remote.firmwareRelease },
        { ExamplesPart, QStringLiteral("examples"), check.remote.examplesRelease },
        { ModelsPart, QStringLiteral("models"), check.remote.modelsRelease },
    };

    for (const PartInfo &part : partList)
    {
        if (!(check.parts & part.bit))
        {
            continue;
        }

        QProgressDialog progress(Tr::tr("Downloading \"%L1\" %L2...").arg(check.remote.displayName).arg(part.name),
                                 Tr::tr("Cancel"), 0, 0, parent,
                                 Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                 (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
        progress.setWindowTitle(Tr::tr("Third Party Repositories"));
        progress.setWindowModality(Qt::ApplicationModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);

        QByteArray data = httpGetBlocking(QUrl(part.channel.url), error, &progress);

        progress.close();

        if (data.isEmpty())
        {
            if (error && error->isEmpty())
            {
                *error = Tr::tr("empty download");
            }

            return false;
        }

        if ((!part.channel.sha256.isEmpty())
        && (QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()).toLower()
            != part.channel.sha256.toLower()))
        {
            *error = Tr::tr("the downloaded \"%L1\" archive failed its sha256 check").arg(part.name);
            return false;
        }

        if (!installArchiveToPart(data, check.repo.writablePath, part.name, error))
        {
            return false;
        }

        if (!writeVendorFile(check.repo.writablePath.pathAppended(part.name + QStringLiteral(".version")),
                             part.channel.version.toUtf8() + QByteArrayLiteral("\n"), error))
        {
            return false;
        }
    }

    return writeVendorFile(check.repo.writablePath.pathAppended(QStringLiteral("config.json")),
                           check.remoteConfig, error);
}

void OpenMVThirdParty::checkAndPrompt(QObject *context, int parts, bool interactive)
{
    launchUpdateCheck(scanRepos(), parts, context, [context, interactive] (const QList<UpdateCheck> &updates) {
        if (updates.isEmpty())
        {
            if (interactive)
            {
                QMessageBox::information(Core::ICore::dialogParent(),
                    Tr::tr("Third Party Repositories"),
                    Tr::tr("All third party repositories are up to date."));
            }

            return;
        }

        auto prompt = [updates] {
            QStringList lines;

            for (const UpdateCheck &check : updates)
            {
                QStringList what;

                if (check.parts & FirmwarePart)
                {
                    what.append(Tr::tr("firmware %L1 -> %L2").arg(check.repo.firmwareVersion.isEmpty()
                        ? Tr::tr("none") : check.repo.firmwareVersion).arg(check.remote.firmwareRelease.version));
                }

                if (check.parts & ExamplesPart)
                {
                    what.append(Tr::tr("examples %L1 -> %L2").arg(check.repo.examplesVersion.isEmpty()
                        ? Tr::tr("none") : check.repo.examplesVersion).arg(check.remote.examplesRelease.version));
                }

                if (check.parts & ModelsPart)
                {
                    what.append(Tr::tr("models %L1 -> %L2").arg(check.repo.modelsVersion.isEmpty()
                        ? Tr::tr("none") : check.repo.modelsVersion).arg(check.remote.modelsRelease.version));
                }

                lines.append(QStringLiteral("%1 - %2").arg(check.remote.displayName).arg(what.join(QStringLiteral(", "))));
            }

            if (QMessageBox::question(Core::ICore::dialogParent(),
                Tr::tr("Third Party Repositories"),
                Tr::tr("Updates are available:\n\n%L1\n\nInstall now?").arg(lines.join(QStringLiteral("\n"))),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
            {
                return;
            }

            QStringList errors;
            bool anyInstalled = false;

            for (const UpdateCheck &check : updates)
            {
                QString error;

                if (installParts(check, &error, Core::ICore::dialogParent()))
                {
                    anyInstalled = true;
                }
                else
                {
                    errors.append(QStringLiteral("%1 - %2").arg(check.repo.id).arg(error));
                }
            }

            if (!errors.isEmpty())
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Third Party Repositories"),
                    Tr::tr("Some updates failed:\n\n%L1").arg(errors.join(QStringLiteral("\n"))));
            }

            if (anyInstalled)
            {
                offerRestart(Core::ICore::dialogParent());
            }
        };

        // At startup the check runs in the background and must never pop a
        // dialog on top of another one; from the preferences page the user
        // just clicked a button, so answer immediately.
        if (interactive)
        {
            prompt();
        }
        else
        {
            whenNoModal(context, prompt);
        }
    });
}

bool OpenMVThirdParty::installFromUrl(const QUrl &url, bool overwrite, QString *repoId,
                                      QString *error, QWidget *parent)
{
    QByteArray data = httpGetBlocking(url, error, Q_NULLPTR);

    if (data.isEmpty())
    {
        if (error && error->isEmpty())
        {
            *error = Tr::tr("empty download");
        }

        return false;
    }

    Repo remote;

    if (!parseConfigData(data, url.toString(), QString(), &remote, error))
    {
        return false;
    }

    if (!remote.firmwareRelease.isValid())
    {
        *error = Tr::tr("config.json does not define a firmware release channel");
        return false;
    }

    if (repoId)
    {
        *repoId = remote.id;
    }

    // The hosted config.json may omit its own URL - inject the URL the user
    // installed from so the repo receives updates.
    if (remote.configUrl.isEmpty())
    {
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        obj[QStringLiteral("configUrl")] = url.toString();
        data = QJsonDocument(obj).toJson();
        remote.configUrl = url.toString();
    }

    Utils::FilePath vendorDir = writableRoot().pathAppended(remote.id);

    if (vendorDir.exists())
    {
        if (!overwrite)
        {
            *error = Tr::tr("a repository named \"%L1\" is already installed").arg(remote.id);
            return false;
        }

        if (!vendorDir.removeRecursively(error))
        {
            return false;
        }
    }

    vendorDir.ensureWritableDir();

    if (!writeVendorFile(vendorDir.pathAppended(QStringLiteral("config.json")), data, error))
    {
        vendorDir.removeRecursively();
        return false;
    }

    UpdateCheck check;
    check.repo.id = remote.id;
    check.repo.writablePath = vendorDir;
    check.remote = remote;
    check.remoteConfig = data;
    check.parts = FirmwarePart
        | (remote.examplesRelease.isValid() ? ExamplesPart : 0)
        | (remote.modelsRelease.isValid() ? ModelsPart : 0);

    if (!installParts(check, error, parent))
    {
        vendorDir.removeRecursively();
        return false;
    }

    return true;
}

OpenMVThirdParty::Repo OpenMVThirdParty::repoForFirmwareRoot(const QString &firmwareRoot)
{
    for (const Repo &repo : scanRepos())
    {
        if (repo.writablePath.pathAppended(QStringLiteral("firmware")).toString() == firmwareRoot)
        {
            return repo;
        }
    }

    return Repo();
}

Utils::FilePath OpenMVThirdParty::syncDevChannelBlocking(const Repo &repo, QString *error, QWidget *parent)
{
    if (!repo.firmwareDev.isValid())
    {
        *error = Tr::tr("\"%L1\" does not provide a development firmware channel").arg(repo.displayName);
        return Utils::FilePath();
    }

    Utils::FilePath devDir = repo.writablePath.pathAppended(QStringLiteral("firmware-dev"));
    Utils::FilePath sidecar = repo.writablePath.pathAppended(QStringLiteral("firmware-dev.version"));

    // The dev version is compared for inequality (dev builds are not ordered);
    // a matching sidecar means the cache is already the requested build.
    if (devDir.exists() && (readVersionFile(sidecar) == repo.firmwareDev.version))
    {
        return devDir;
    }

    QProgressDialog progress(Tr::tr("Downloading \"%L1\" development firmware...").arg(repo.displayName),
                             Tr::tr("Cancel"), 0, 0, parent,
                             Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                             (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    progress.setWindowTitle(Tr::tr("Third Party Repositories"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QByteArray data = httpGetBlocking(QUrl(repo.firmwareDev.url), error, &progress);

    progress.close();

    if (data.isEmpty())
    {
        if (error && error->isEmpty())
        {
            *error = Tr::tr("empty download");
        }

        return Utils::FilePath();
    }

    if ((!repo.firmwareDev.sha256.isEmpty())
    && (QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()).toLower()
        != repo.firmwareDev.sha256.toLower()))
    {
        *error = Tr::tr("the downloaded development firmware failed its sha256 check");
        return Utils::FilePath();
    }

    if (!installArchiveToPart(data, repo.writablePath, QStringLiteral("firmware-dev"), error))
    {
        return Utils::FilePath();
    }

    if (!writeVendorFile(sidecar, repo.firmwareDev.version.toUtf8() + QByteArrayLiteral("\n"), error))
    {
        return Utils::FilePath();
    }

    return devDir;
}

bool OpenMVThirdParty::removeRepo(const QString &id, QString *error)
{
    if (installRoot().pathAppended(id).exists())
    {
        *error = Tr::tr("\"%L1\" was installed into the application directory by an installer - remove it there").arg(id);
        return false;
    }

    Utils::FilePath vendorDir = writableRoot().pathAppended(id);

    if (!vendorDir.exists())
    {
        *error = Tr::tr("\"%L1\" is not installed").arg(id);
        return false;
    }

    if (!vendorDir.removeRecursively(error))
    {
        return false;
    }

    // Forget the repo so a reinstall gets the first-seen warning box again, and
    // drop it from the priority order.
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    QStringList known = settings->value(KNOWN_THIRD_PARTY_REPOS).toStringList();
    known.removeAll(id);
    settings->setValue(KNOWN_THIRD_PARTY_REPOS, known);

    QStringList order = settings->value(THIRD_PARTY_REPO_ORDER).toStringList();
    order.removeAll(id);
    settings->setValue(THIRD_PARTY_REPO_ORDER, order);

    settings->sync();

    return true;
}

QStringList OpenMVThirdParty::overridesText(const QList<OverrideRecord> &overrides)
{
    QStringList list;

    for (const OverrideRecord &record : overrides)
    {
        if (record.bootloaderOnly)
        {
            list.append(Tr::tr("\"%L1\" board \"%L2\" (%L3) overlaps the bootloader id of \"%L4\" (%L5)")
                        .arg(record.vendorId).arg(record.vendorBoard).arg(record.vendorVidPid)
                        .arg(record.overriddenBoard).arg(record.overriddenVidPid));
        }
        else
        {
            list.append(Tr::tr("\"%L1\" board \"%L2\" (%L3) overrides \"%L4\" (%L5)")
                        .arg(record.vendorId).arg(record.vendorBoard).arg(record.vendorVidPid)
                        .arg(record.overriddenBoard).arg(record.overriddenVidPid));
        }
    }

    return list;
}

} // namespace Internal
} // namespace OpenMV
