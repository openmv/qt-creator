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
#include <QGuiApplication>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/fileutils.h>

#include "openmvthirdparty.h"
#include "openmvtr.h"

namespace OpenMV {
namespace Internal {

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

static bool parseConfig(const Utils::FilePath &vendorDir, OpenMVThirdParty::Repo *repo, QString *error)
{
    QFile file(vendorDir.pathAppended(QStringLiteral("config.json")).toString());

    if (!file.open(QIODevice::ReadOnly))
    {
        *error = Tr::tr("\"%L1\" does not have a readable config.json").arg(vendorDir.fileName());
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        *error = Tr::tr("\"%L1\" config.json - %L2").arg(vendorDir.fileName()).arg(parseError.errorString());
        return false;
    }

    QJsonObject obj = doc.object();
    QString name = obj.value(QStringLiteral("name")).toString();

    if (!vendorIdRegex().match(name).hasMatch())
    {
        *error = Tr::tr("\"%L1\" config.json - invalid repository name \"%L2\"").arg(vendorDir.fileName()).arg(name);
        return false;
    }

    if (name != vendorDir.fileName())
    {
        *error = Tr::tr("\"%L1\" config.json - name \"%L2\" does not match the folder name").arg(vendorDir.fileName()).arg(name);
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

    return true;
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

        for (const QString &part : {QStringLiteral("firmware"), QStringLiteral("examples")})
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

        repos.append(repo);
    }

    return repos;
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

            // Masked app VID:PID overlap with an already-merged board shadows
            // it: the existing entry is removed so every consumer of the merged
            // document (matching, VID:PID lists, mapping tables) sees only the
            // vendor's board. Recorded so the UI can always show the override.
            QJsonArray keptBoards;

            for (const QJsonValue &existingValue : boards)
            {
                QJsonObject existing = existingValue.toObject();

                int existingVid, existingPid, existingMask;

                if (boardVidPid(existing, &existingVid, &existingPid, &existingMask)
                && vidPidOverlap(vid, pid, mask, existingVid, existingPid, existingMask))
                {
                    OverrideRecord record;
                    record.vendorId = repo.id;
                    record.vendorBoard = displayName;
                    record.vendorVidPid = board.value(QStringLiteral("boardVidPid")).toString();
                    record.overriddenBoard = existing.value(QStringLiteral("boardDisplayName")).toString();
                    record.overriddenVidPid = existing.value(QStringLiteral("boardVidPid")).toString();
                    overrides->append(record);
                    continue;
                }

                keptBoards.append(existingValue);
            }

            boards = keptBoards;

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

    return QJsonDocument(root);
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
