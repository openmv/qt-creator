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

#ifndef OPENMVTHIRDPARTY_H
#define OPENMVTHIRDPARTY_H

#include <QtCore>

#include <utils/filepath.h>

// Third Party Repositories.
//
// A third-party repository is a vendor-named folder that adds boards (and later
// examples/models) to the IDE without rebuilding it. It can exist in two places:
//
//   <install>/share/qtcreator/third-party/<vendor>/   read-only, dropped by a
//                                                     vendor installer
//   <all-users-resources>/third-party/<vendor>/       writable working copy;
//                                                     the ONLY copy the IDE reads
//
// Layout of a vendor folder:
//
//   <vendor>/
//     config.json           required; see below
//     firmware/
//       settings.json       same schema as the IDE's firmware/settings.json
//       <BOARD_FOLDER>/firmware.bin, romfs0.img, ...
//     firmware.version      sidecar version stamp of the installed firmware part
//     examples/             optional
//     examples.version
//
// config.json (the same file is hosted at "configUrl" for updates -- installing
// a repo from a URL literally downloads this file first):
//
//   {
//     "name": "acme",                      must match the folder name,
//                                          ^[a-z][a-z0-9_-]{1,31}$
//     "displayName": "Acme Robotics",
//     "homepage": "https://acme.example",
//     "configUrl": "https://acme.example/openmv/config.json",
//     "firmware": {
//       "release":     { "version": "1.2.0", "url": "https://...zip", "sha256": "..." },
//       "development": { "version": "dev-20260701", "url": "...", "sha256": "..." }
//     },
//     "examples": { "release": { "version": "1.1.0", "url": "...", "sha256": "..." } }
//   }
//
// At startup the install-dir repos are mirrored into the writable area (a repo
// part is only overwritten when the install dir ships a strictly newer
// "<part>.version"), then every writable repo's firmware/settings.json is merged
// into the IDE's parsed settings document. A third-party board whose masked app
// VID:PID overlaps an already-merged board REPLACES it (the shadowed entry is
// removed) and the override is recorded so the UI can always show what is being
// overridden. Merged boards are annotated with the internal keys "_resourceRoot"
// (absolute path of the vendor's writable firmware dir, used to resolve firmware
// binaries) and "_vendor". Keys starting with '_' are reserved and stripped from
// vendor-provided entries.

namespace OpenMV {
namespace Internal {

// Stored under the plugin's QSettings; the list of vendor ids that have already
// produced their one-time "this repo overrides/conflicts" warning box.
#define KNOWN_THIRD_PARTY_REPOS "OpenMV/KnownThirdPartyRepos"

// Stored under the plugin's QSettings; the last URL typed into the preferences
// page's "Install from URL" dialog.
#define LAST_THIRD_PARTY_INSTALL_URL "OpenMV/LastThirdPartyInstallUrl"

// Stored under the plugin's QSettings; the user-controlled vendor priority as a
// list of vendor ids, HIGHEST priority first. A repo higher in this list
// overrides those below it (and OpenMV, the implicit base) when they collide -
// a board with an overlapping USB id, or an example at the same relative path.
// Repos not in the list (freshly installed) sort after the listed ones,
// alphabetically. The preferences page's Up/Down buttons rewrite this.
#define THIRD_PARTY_REPO_ORDER "OpenMV/ThirdPartyRepoOrder"

class OpenMVThirdParty
{
public:
    struct Channel
    {
        QString version;
        QString url;
        QString sha256;
        bool isValid() const { return (!version.isEmpty()) && (!url.isEmpty()); }
    };

    struct Repo
    {
        QString id;
        QString displayName;
        QString homepage;
        QString configUrl;
        Channel firmwareRelease;
        Channel firmwareDev;
        Channel examplesRelease;
        Channel modelsRelease;
        Utils::FilePath writablePath;
        bool fromInstallDir = false;    // also present in the read-only install dir
        QString firmwareVersion;        // installed sidecar versions ("" if absent)
        QString examplesVersion;
        QString modelsVersion;
    };

    struct OverrideRecord
    {
        QString vendorId;
        QString vendorBoard;
        QString vendorVidPid;
        QString overriddenBoard;
        QString overriddenVidPid;
        // True when nothing was removed: the vendor app VID:PID merely overlaps a
        // built-in board's bootloader VID:PID (kept as a visible note only).
        bool bootloaderOnly = false;
    };

    static Utils::FilePath installRoot();   // resourcePath("third-party")
    static Utils::FilePath writableRoot();  // allUsersResourcePath("third-party")

    // Mirror install-dir repos into the writable area (see file header for the
    // per-part version rule). Never throws/exits; problems are appended to
    // warnings. Runs in the IDE and the viewer.
    static void mirrorInstallDirRepos(QStringList *warnings);

    // Enumerate the writable repos in user priority order (highest first; see
    // THIRD_PARTY_REPO_ORDER). Malformed vendor folders are skipped and
    // reported via warnings.
    static QList<Repo> scanRepos(QStringList *warnings = Q_NULLPTR);

    // The stored vendor priority order (highest first), and a setter the
    // preferences page's Up/Down buttons use.
    static QStringList repoOrder();
    static void setRepoOrder(const QStringList &order);

    // Merge every repo's firmware/settings.json into the built-in document.
    // Boards whose masked app VID:PID overlaps an already-merged board replace
    // it (recorded in overrides). Sensors are appended (id conflicts skipped).
    // "protocol" and top-level "firmware_version" sections are ignored.
    static QJsonDocument mergeFirmwareSettings(const QJsonDocument &builtIn,
                                               const QList<Repo> &repos,
                                               QList<OverrideRecord> *overrides,
                                               QStringList *warnings);

    // The folder that holds <boardFirmwareFolder>/ for this board: the vendor's
    // "_resourceRoot" for third-party boards, else the IDE's firmware folder.
    static Utils::FilePath firmwareRootForBoard(const QJsonObject &board);

    // The writable repo whose firmware dir equals firmwareRoot (a connected
    // third-party board's "_resourceRoot"), or a Repo with an empty id if none.
    static Repo repoForFirmwareRoot(const QString &firmwareRoot);

    // Sync repo's development firmware channel into <vendor>/firmware-dev (only
    // re-downloading when the dev version changed), with a modal progress
    // dialog. Returns the firmware-dev dir, or an empty path on failure / when
    // the repo declares no development channel (error is set).
    static Utils::FilePath syncDevChannelBlocking(const Repo &repo, QString *error, QWidget *parent);

    // The merged "boards" array, stably reordered so entries whose
    // "_resourceRoot" equals resourceRoot come first. First-match-wins loops
    // that key on a SHARED value (bootloaderVidPid) then prefer the board of
    // the operation in progress; with no third-party boards (or an empty
    // resourceRoot matching only built-ins, which stay in file order) the
    // iteration order - and therefore behavior - is unchanged.
    static QJsonArray boardsPreferringResourceRoot(const QJsonDocument &settings,
                                                   const QString &resourceRoot);

    // "Noisy once" policy helper: true if repos contains vendor ids never seen
    // before on this machine (all ids are then recorded as seen). The caller
    // shows the override/conflict warning box only when this returns true.
    static bool noteNewRepos(const QList<Repo> &repos);

    // One string per record, for the warning box, the log, and the UI panel.
    static QStringList overridesText(const QList<OverrideRecord> &overrides);

    // The records produced by this run's mergeFirmwareSettings() call -
    // regenerated every startup; backs the preferences page's dynamic panel.
    static QList<OverrideRecord> mergedOverrides();

    // --- Network updates -----------------------------------------------------
    //
    // A repo with a "configUrl" is updatable: the URL hosts the current
    // config.json, whose per-part release versions are compared (ordered)
    // against the local "<part>.version" sidecars. Payload zips are downloaded,
    // sha256-checked when a hash is published, and swapped into place. Repo
    // changes take effect on restart.

    enum Part
    {
        FirmwarePart = 1,
        ExamplesPart = 2,
        ModelsPart = 4,
    };

    // One updatable repo's check result: the freshly fetched config.json (raw
    // and parsed) and the bitmask of parts whose remote version is newer.
    struct UpdateCheck
    {
        Repo repo;                // as installed (writablePath/versions valid)
        Repo remote;              // parsed from remoteConfig
        QByteArray remoteConfig;  // fetched config.json, written back on install
        int parts = 0;
    };

    // Fetch every updatable repo's configUrl asynchronously (GUI thread, no
    // blocking) and invoke onDone with the repos that have updates. Fetch/parse
    // failures are logged and skipped. onDone is not called if context dies.
    static void launchUpdateCheck(const QList<Repo> &repos, int parts, QObject *context,
                                  std::function<void(const QList<UpdateCheck> &)> onDone);

    // Download + install the flagged parts of one repo with a modal progress
    // dialog, then update the sidecars and config.json. GUI thread.
    static bool installParts(const UpdateCheck &check, QString *error, QWidget *parent);

    // The full startup flow: scan -> launchUpdateCheck -> ONE aggregated user
    // prompt (deferred while any other modal dialog is open, so popups never
    // stack) -> sequential installParts -> restart offer. No-op without
    // updatable repos. When interactive, also reports "no updates found".
    static void checkAndPrompt(QObject *context, int parts, bool interactive);

    // Download config.json from url, validate it, create the writable vendor
    // folder, and install its release parts. Refuses an existing repo id
    // unless overwrite. Cleans up after itself on failure. GUI thread.
    static bool installFromUrl(const QUrl &url, bool overwrite, QString *repoId,
                               QString *error, QWidget *parent);

    // Delete a repo from the writable area (refused for install-dir repos) and
    // forget it in KNOWN_THIRD_PARTY_REPOS so a reinstall warns again.
    static bool removeRepo(const QString &id, QString *error);
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVTHIRDPARTY_H
