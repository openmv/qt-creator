// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pythonlanguageclient.h"

#include "pipsupport.h"
// OPENMV-DIFF //
// OpenMV drops the Python build-configuration + PySide extra-compiler sources, so their
// headers are no longer available/needed here.
// #include "pythonbuildconfiguration.h"
// #include "pysideuicextracompiler.h"
// OPENMV-DIFF //
#include "pythonconstants.h"
#include "pythonproject.h"
#include "pythonsettings.h"
#include "pythontr.h"
#include "pythonutils.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <languageclient/languageclientinterface.h>
#include <languageclient/languageclientmanager.h>
#include <languageserverprotocol/textsynchronization.h>
#include <languageserverprotocol/workspace.h>

#include <projectexplorer/buildconfiguration.h>
// OPENMV-DIFF //
// buildsteplist.h / extracompiler.h were only used by the removed PySide extra-compiler logic.
// #include <projectexplorer/buildsteplist.h>
// #include <projectexplorer/extracompiler.h>
// OPENMV-DIFF //
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>

#include <QtTaskTree/QMappedTaskTreeRunner>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/async.h>
#include <utils/infobar.h>
#include <utils/qtcprocess.h>

#include <QJsonDocument>

using namespace LanguageClient;
using namespace LanguageServerProtocol;
using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace TextEditor;
using namespace Utils;

namespace Python::Internal {

static constexpr char installPylsInfoBarId[] = "Python::InstallPyls";
static constexpr char updatePylsInfoBarId[] = "Python::updatePyls";
static constexpr char alwaysUpdateKey[] = "Python/AlwaysUpdatePyls";

class PythonLanguageServerState
{
public:
    enum {
        NotInstallable,
        Installable,
        Updatable,
        Installed
    } state;
    FilePath pylsModulePath;
};

static QHash<FilePath, PyLSClient*> &pythonClients()
{
    static QHash<FilePath, PyLSClient*> clients;
    return clients;
}

static FilePath pyLspPath(const FilePath &python)
{
    const QString version = pythonVersion(python);
    if (python.isLocal())
        return Core::ICore::userResourcePath() / "pylsp" / version;
    if (const Result<FilePath> tmpDir = python.tmpDir())
        return *tmpDir / "qc-pylsp" / version;
    return {};
}

static PythonLanguageServerState checkPythonLanguageServer(const FilePath &python, bool pipUsable)
{
    static QHash<FilePath, PythonLanguageServerState> m_stateCache;
    using namespace LanguageClient;
    using namespace std::chrono;

    // OPENMV-DIFF //
    // Prefer the pylsp that OpenMV bundles under <resources>/pylsp/<arch>. If present,
    // report it as Installed immediately so we never pip-install or update a user copy.
    FilePath path;

    if (Utils::HostOsInfo::isWindowsHost())
    {
        path = Core::ICore::resourcePath() / "pylsp" / "windows";
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        if ((Utils::HostOsInfo::hostArchitecture() == Utils::OsArchArm64)
            || Utils::HostOsInfo::isRunningUnderRosetta())
        {
            path = Core::ICore::resourcePath() / "pylsp" / "mac-aarch64";
        }
        else
        {
            path = Core::ICore::resourcePath() / "pylsp" / "mac";
        }
    }
    else if (Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            path = Core::ICore::resourcePath() / "pylsp" / "linux-x86";
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            path = Core::ICore::resourcePath() / "pylsp" / "linux-x86_64";
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            path = Core::ICore::resourcePath() / "pylsp" / "linux-arm";
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            path = Core::ICore::resourcePath() / "pylsp" / "linux-arm64";
        }
    }

    if (path.pathAppended("bin").pathAppended("pylsp").withExecutableSuffix().exists())
    {
        return {PythonLanguageServerState::Installed, path};
    }
    // OPENMV-DIFF //

    if (auto it = m_stateCache.find(python); it != m_stateCache.end())
        return it.value();

    const FilePath lspPath = pyLspPath(python);
    if (!lspPath.isEmpty()) {
        const FilePath pylsp = (lspPath / "bin" / "pylsp").withExecutableSuffix();
        if (pylsp.exists()) {
            if (pipUsable) {
                Process pythonProcess;
                Environment env = pylsp.deviceEnvironment();
                env.appendOrSet("PYTHONPATH", lspPath.toUserOutput());
                pythonProcess.setEnvironment(env);
                pythonProcess.setCommand(
                    {python, {"-m", "pip", "list", "--outdated", "--format=json"}});
                pythonProcess.runBlocking(20s);
                QString output = pythonProcess.allOutput();

                // Only the first line contains the json data. Following lines might contain warnings.
                if (int index = output.indexOf('\n'); index >= 0)
                    output.truncate(index);

                const QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8());
                for (const QJsonValue &value : doc.array()) {
                    if (value.toObject().value("name") == "python-lsp-server") {
                        Process pylsProcess;
                        Environment env = pylsp.deviceEnvironment();
                        env.appendOrSet("PYTHONPATH", lspPath.toUserOutput());
                        pylsProcess.setEnvironment(env);
                        pylsProcess.setCommand({pylsp, {"--version"}});
                        pylsProcess.runBlocking(20s);
                        output = pylsProcess.allOutput();
                        if (!output.contains(value.toObject().value("latest_version").toString()))
                            return {PythonLanguageServerState::Updatable, lspPath};
                        break;
                    }
                }
            }
            return m_stateCache.insert(python, {PythonLanguageServerState::Installed, lspPath})
                .value();
        }

        if (pipUsable)
            return {PythonLanguageServerState::Installable, lspPath};
    }
    return m_stateCache.insert(python, {PythonLanguageServerState::NotInstallable, FilePath()})
        .value();
}


class PyLSInterface : public StdIOClientInterface
{
public:
    PyLSInterface()
        : m_extraPythonPath("QtCreator-pyls-XXXXXX")
    { }

    TemporaryDirectory m_extraPythonPath;
protected:
    void startImpl() override
    {
        const FilePath python = m_cmd.executable();
        Environment env = python.deviceEnvironment();
        // OPENMV-DIFF //
        FilePath path;

        if (Utils::HostOsInfo::isWindowsHost())
        {
            path = Core::ICore::resourcePath() / "pylsp" / "windows";
        }
        else if(Utils::HostOsInfo::isMacHost())
        {
            if ((Utils::HostOsInfo::hostArchitecture() == Utils::OsArchArm64)
                || Utils::HostOsInfo::isRunningUnderRosetta())
            {
                path = Core::ICore::resourcePath() / "pylsp" / "mac-aarch64";
            }
            else
            {
                path = Core::ICore::resourcePath() / "pylsp" / "mac";
            }
        }
        else if (Utils::HostOsInfo::isLinuxHost())
        {
            if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
            {
                path = Core::ICore::resourcePath() / "pylsp" / "linux-x86";
            }
            else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
            {
                path = Core::ICore::resourcePath() / "pylsp" / "linux-x86_64";
            }
            else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
            {
                path = Core::ICore::resourcePath() / "pylsp" / "linux-arm";
            }
            else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
            {
                path = Core::ICore::resourcePath() / "pylsp" / "linux-arm64";
            }
        }

        if (path.pathAppended("bin").pathAppended("pylsp").withExecutableSuffix().exists())
        {
            env.prependOrSet("PYTHONPYCACHEPREFIX", Core::ICore::allUsersResourcePath(QStringLiteral("pycache")).toFSPathString());
            env.prependOrSet("PYTHONPATH", path.path());
        }
        // OPENMV-DIFF //
        const FilePath lspPath = pyLspPath(python);
        if (!lspPath.isEmpty() && lspPath.exists() && QTC_GUARD(lspPath.isSameDevice(python))) {
            env.appendOrSet("PYTHONPATH", lspPath.path());
        }
        if (python.isLocal()) {
            // todo check where to put this tempdir in remote setups
            env.appendOrSet("PYTHONPATH", m_extraPythonPath.path().toUrlishString());
        }
        if (env.hasChanges())
            setEnvironment(env);
        StdIOClientInterface::startImpl();
    }
};

PyLSClient *clientForPython(const FilePath &python)
{
    if (auto client = pythonClients()[python])
        return client;
    auto interface = new PyLSInterface;
    interface->setCommandLine({python, {"-m", "pylsp"}});
    auto client = new PyLSClient(interface);
    client->setName(Tr::tr("Python Language Server (%1)").arg(python.toUserOutput()));
    client->updateConfiguration();
    LanguageFilter filter;
    filter.mimeTypes = QStringList() << Constants::C_PY_MIMETYPE << Constants::C_PY3_MIMETYPE;
    client->setSupportedLanguage(filter);
    client->start();
    pythonClients()[python] = client;
    // OPENMV-DIFF //
    // Upstream primes PySide .ui extra-compilers for every project whose active
    // PythonBuildConfiguration targets this interpreter. OpenMV has no build
    // configuration / PySide support, so there is nothing to prime.
    // for (Project *project : ProjectManager::projects()) {
    //     if (auto *bc = qobject_cast<PythonBuildConfiguration *>(project->activeBuildConfiguration())) {
    //         if (bc->python() == python)
    //             client->updateExtraCompilers(project);
    //     }
    // }
    // OPENMV-DIFF //
    return client;
}

PyLSClient::PyLSClient(PyLSInterface *interface)
    : Client(interface)
    // OPENMV-DIFF //
    // , m_extraCompilerOutputDir(interface->m_extraPythonPath.path())
    // OPENMV-DIFF //
{
    connect(this, &Client::initialized, this, &PyLSClient::updateConfiguration);
    connect(PythonSettings::instance(), &PythonSettings::pylsConfigurationChanged,
            this, &PyLSClient::updateConfiguration);
    connect(PythonSettings::instance(), &PythonSettings::pylsEnabledChanged,
            this, [this](const bool enabled){
                if (!enabled)
                    LanguageClientManager::shutdownClient(this);
            });

}

PyLSClient::~PyLSClient()
{
    pythonClients().remove(pythonClients().key(this));
    // OPENMV-DIFF //
    // No PySide extra-compilers to tear down in OpenMV's minimal plugin.
    // for (const QPointer<PySideUicExtraCompiler> &compiler : std::as_const(m_extraCompilers))
    //     deleteExtraCompiler(compiler);
    // OPENMV-DIFF //
}

// OPENMV-DIFF //
static QStringList m_extraStubPaths;
void PyLSClient::setExtraStubPaths(const QStringList &paths)
{
    m_extraStubPaths = paths;
}
// OPENMV-DIFF //

void PyLSClient::updateConfiguration()
{
    const auto doc = QJsonDocument::fromJson(PythonSettings::pylsConfiguration().toUtf8());
    // OPENMV-DIFF //
    // Inject the third-party repo stub directories as jedi extra_paths so their
    // .pyi stubs provide completion alongside OpenMV's (which are the workspace
    // root). Highest priority first, so an earlier stub wins a name collision.
    if (doc.isObject() && (!m_extraStubPaths.isEmpty())) {
        QJsonObject config = doc.object();
        QJsonObject pylsp = config.value("pylsp").toObject();
        QJsonObject plugins = pylsp.value("plugins").toObject();
        QJsonObject jedi = plugins.value("jedi").toObject();
        jedi.insert("extra_paths", QJsonArray::fromStringList(m_extraStubPaths));
        plugins.insert("jedi", jedi);
        pylsp.insert("plugins", plugins);
        config.insert("pylsp", pylsp);
        Client::updateConfiguration(config);
        return;
    }
    // OPENMV-DIFF //
    if (doc.isArray())
        Client::updateConfiguration(doc.array());
    else if (doc.isObject())
        Client::updateConfiguration(doc.object());
}

// OPENMV-DIFF //
static Utils::FilePath m_portPath;
void PyLSClient::setPortPath(const FilePath &portPath) {
    QHash<FilePath, PyLSClient *> &clients = pythonClients();

    for (auto it = clients.begin(); it != clients.end(); ++it) {
        PyLSClient *client = it.value();

        if (client)
        {
            if (m_portPath.isEmpty())
            {
                if (!portPath.isEmpty())
                {
    // BREAKS AUTO COMPLETION WHEN ENABLED
    //                WorkspaceFoldersChangeEvent event;
    //                event.setAdded({WorkSpaceFolder(client->hostPathToServerUri(portPath),
    //                                                portPath.fileName())});
    //                DidChangeWorkspaceFoldersParams params;
    //                params.setEvent(event);
    //                DidChangeWorkspaceFoldersNotification change(params);
    //                client->sendMessage(change);
                    client->m_extraWorkspaceDirs.append(portPath);
                }
            }
            else if (m_portPath != portPath)
            {
                if (!portPath.isEmpty())
                {
    // BREAKS AUTO COMPLETION WHEN ENABLED
    //                WorkspaceFoldersChangeEvent event;
    //                event.setAdded({WorkSpaceFolder(client->hostPathToServerUri(portPath),
    //                                                 portPath.fileName())});
    //                event.setRemoved({WorkSpaceFolder(client->hostPathToServerUri(m_portPath),
    //                                                  m_portPath.fileName())});
    //                DidChangeWorkspaceFoldersParams params;
    //                params.setEvent(event);
    //                DidChangeWorkspaceFoldersNotification change(params);
    //                client->sendMessage(change);
                    client->m_extraWorkspaceDirs.append(portPath);
                    client->m_extraWorkspaceDirs.removeOne(m_portPath);
                }
                else
                {
    // BREAKS AUTO COMPLETION WHEN ENABLED
    //                WorkspaceFoldersChangeEvent event;
    //                event.setRemoved({WorkSpaceFolder(client->hostPathToServerUri(m_portPath),
    //                                                  m_portPath.fileName())});
    //                DidChangeWorkspaceFoldersParams params;
    //                params.setEvent(event);
    //                DidChangeWorkspaceFoldersNotification change(params);
    //                client->sendMessage(change);
                    client->m_extraWorkspaceDirs.removeOne(m_portPath);
                }
            }
        }
    }

    m_portPath = portPath;
}
// OPENMV-DIFF //

void PyLSClient::openDocument(TextDocument *document)
{
    using namespace LanguageServerProtocol;
    // OPENMV-DIFF //
    // if (reachable()) {
    // OPENMV-DIFF //
    if (0) {
    // OPENMV-DIFF //
        const FilePath documentPath = document->filePath();
        if (isSupportedDocument(document) && !pythonProjectForFile(documentPath)) {
            const FilePath workspacePath = documentPath.parentDir();
            if (!m_extraWorkspaceDirs.contains(workspacePath)) {
                WorkspaceFoldersChangeEvent event;
                event.setAdded({WorkSpaceFolder(hostPathToServerUri(workspacePath),
                                                workspacePath.fileName())});
                DidChangeWorkspaceFoldersParams params;
                params.setEvent(event);
                DidChangeWorkspaceFoldersNotification change(params);
                sendMessage(change);
                m_extraWorkspaceDirs.append(workspacePath);
            }
        }
    }
    Client::openDocument(document);
}

// OPENMV-DIFF //
// The following methods implemented PySide .ui -> ui_*.py extra-compilation driven by
// the Python build configuration / PySideBuildStep. OpenMV ships neither, so the whole
// block (buildConfigurationClosed override, updateExtraCompilers, updateExtraCompilerContents,
// deleteExtraCompiler) is dropped. The declarations are removed from pythonlanguageclient.h too.
//
// void PyLSClient::buildConfigurationClosed(BuildConfiguration *bc)
// {
//     if (auto pythonBuildConfig = qobject_cast<PythonBuildConfiguration *>(bc)) {
//         const FilePath python = pythonBuildConfig->python();
//         if (!python.isEmpty() && clientForPython(python) == this) {
//             const QList<QPointer<PySideUicExtraCompiler>> compilers = Utils::takeAll(
//                 m_extraCompilers,
//                 [project = bc->project()](const QPointer<PySideUicExtraCompiler> &compiler) {
//                     return compiler && compiler->project() == project;
//                 });
//             for (PySideUicExtraCompiler *compiler : compilers)
//                 deleteExtraCompiler(compiler);
//         }
//     }
//     Client::buildConfigurationClosed(bc);
// }
//
// void PyLSClient::updateExtraCompilers(const Project *project) { ... }
// void PyLSClient::updateExtraCompilerContents(ExtraCompiler *compiler, const FilePath &file) { ... }
// void PyLSClient::deleteExtraCompiler(ExtraCompiler *compiler) { ... }
// OPENMV-DIFF //

PyLSClient *PyLSClient::clientForPython(const FilePath &python)
{
    return pythonClients()[python];
}

// OPENMV-DIFF //
// Declared in pythonlanguageclient.h instead (so the plugin can connect to the
// runningChecksDone() signal). The upstream 20.0.2 declaration is kept here only as a
// reference; the header copy is the one that is compiled.
// class PyLSConfigureAssistant : public QObject
// {
// public:
//     PyLSConfigureAssistant();
//
//     void handlePyLSState(const FilePath &python,
//                          const PythonLanguageServerState &state,
//                          TextDocument *document);
//     void resetEditorInfoBar(TextDocument *document);
//     void installPythonLanguageServer(const FilePath &python,
//                                      QPointer<TextDocument> document,
//                                      const FilePath &pylsPath, bool silent, bool upgrade);
//     void startCheckPythonLanguageServer(
//         const FilePath &python, QPointer<TextDocument> document, bool pipUsable);
//     void openDocument(const FilePath &python, TextDocument *document);
//
//     QHash<FilePath, QList<TextDocument *>> m_infoBarEntries;
//     QMappedTaskTreeRunner<TextDocument *> m_documentRunner;
//     QSingleTaskTreeRunner m_pipInstallerRunner;
// };
// OPENMV-DIFF //

void PyLSConfigureAssistant::installPythonLanguageServer(const FilePath &python,
                                                         QPointer<TextDocument> document,
                                                         const FilePath &pylsPath,
                                                         bool silent,
                                                         bool upgrade)
{
    document->infoBar()->removeInfo(installPylsInfoBarId);

    // Hide all install info bar entries for this python, but keep them in the list
    // so the language server will be setup properly after the installation is done.
    for (TextDocument *additionalDocument : m_infoBarEntries[python])
        additionalDocument->infoBar()->removeInfo(installPylsInfoBarId);

    PipInstallerData data;
    data.python = python;
    data.targetPath = pylsPath;
    data.packages = {PipPackage{"python-lsp-server[all]", "Python Language Server"}};
    data.upgrade = upgrade;
    // OPENMV-DIFF //
    // Always install/update the bundled language server silently (no prompts/output).
    silent = true;
    // OPENMV-DIFF //
    data.silent = silent;

    const auto onDone = [this, python, document](DoneWith result) {
        const QList<TextDocument *> additionalDocuments = m_infoBarEntries.take(python);
        if (result == DoneWith::Success) {
            if (PyLSClient *client = clientForPython(python)) {
                if (document)
                    LanguageClientManager::openDocumentWithClient(document, client);
                for (TextDocument *additionalDocument : additionalDocuments)
                    LanguageClientManager::openDocumentWithClient(additionalDocument, client);
            }
        }
    };

    m_pipInstallerRunner.start(pipInstallerTask(data), {}, onDone);
}

void PyLSConfigureAssistant::startCheckPythonLanguageServer(
    const FilePath &python, QPointer<TextDocument> document, bool pipUsable)
{
    if (!document)
        return;
    const auto onSetup = [python, pipUsable](Async<PythonLanguageServerState> &task) {
        task.setConcurrentCallData(&checkPythonLanguageServer, python, pipUsable);
    };
    const auto onDone =
        [this, python, document](const Async<PythonLanguageServerState> &task) {
            if (document && task.isResultAvailable())
                handlePyLSState(python, task.result(), document);
            // OPENMV-DIFF //
            // Emit runningChecksDone() once no document checks remain running, so the
            // plugin's asynchronous aboutToShutdown() can complete. Deferred via a queued
            // call so the just-finished task is already removed from m_documentRunner.
            QMetaObject::invokeMethod(this, [this] {
                if (!m_documentRunner.isRunning())
                    emit runningChecksDone();
            }, Qt::QueuedConnection);
            // OPENMV-DIFF //
        };

    using namespace std::literals::chrono_literals;
    const Group recipe{AsyncTask<PythonLanguageServerState>(onSetup, onDone).withTimeout(10s)};
    m_documentRunner.start(document, recipe);
}

void PyLSConfigureAssistant::openDocument(const FilePath &python, TextDocument *document)
{
    resetEditorInfoBar(document);
    // OPENMV-DIFF //
    // OpenMV's editor documents are frequently temporary/unsaved (welcome script,
    // camera scripts); they must still get the language server. Upstream 20.0.2
    // added `|| document->isTemporary()` here to skip them; drop it. pylsp
    // analyses the in-memory contents, so a real file path is not required.
    // if (!PythonSettings::pylsEnabled() || !python.exists() || document->isTemporary())
    if (!PythonSettings::pylsEnabled() || !python.exists())
        return;
    // OPENMV-DIFF //

    if (auto client = pythonClients().value(python)) {
        LanguageClientManager::openDocumentWithClient(document, client);
        return;
    }

    // OPENMV-DIFF //
    // Upstream 20.0.2 replaced OpenMV's QFutureWatcher + m_runningChecks map with
    // pipIsUsableAsync() -> startCheckPythonLanguageServer(), which drives the check
    // through m_documentRunner. OpenMV's runningChecksDone() emission now happens in
    // startCheckPythonLanguageServer()'s onDone (keyed off m_documentRunner.isRunning()),
    // so the old watcher plumbing here is obsolete and dropped.
    // OPENMV-DIFF //
    pipIsUsableAsync(
        python,
        [self = QPointer(this), python, document = QPointer<TextDocument>(document)](
            const bool pipUsable) {
            if (!self)
                return;
            self->startCheckPythonLanguageServer(python, document, pipUsable);
        });
}

void PyLSConfigureAssistant::handlePyLSState(const FilePath &python,
                                             const PythonLanguageServerState &state,
                                             TextDocument *document)
{
    if (state.state == PythonLanguageServerState::NotInstallable)
        return;

    InfoBar *infoBar = document->infoBar();
    if (state.state == PythonLanguageServerState::Installable
        && infoBar->canInfoBeAdded(installPylsInfoBarId)) {
        // OPENMV-DIFF //
        installPythonLanguageServer(python, document, state.pylsModulePath, false, false);
        return;
        // OPENMV-DIFF //
        auto message = Tr::tr("Install Python language server (PyLS) for %1 (%2). "
                              "The language server provides Python specific completion and annotation.")
                           .arg(pythonVersion(python), python.toUserOutput());
        InfoBarEntry info(installPylsInfoBarId, message, InfoBarEntry::GlobalSuppression::Enabled);
        info.addCustomButton(Tr::tr("Install"), [this, python, document, state] {
            installPythonLanguageServer(python, document, state.pylsModulePath, false, false);
        });
        infoBar->addInfo(info);
        m_infoBarEntries[python] << document;
    } else if (state.state == PythonLanguageServerState::Updatable) {
        if (infoBar->canInfoBeAdded(updatePylsInfoBarId)) {
            // OPENMV-DIFF //
            installPythonLanguageServer(python, document, state.pylsModulePath, false, true);
            return;
            // OPENMV-DIFF //
            auto message = Tr::tr("Update Python language server (PyLS) for %1 (%2).")
                               .arg(pythonVersion(python), python.toUserOutput());
            InfoBarEntry info(updatePylsInfoBarId, message);
            info.addCustomButton(
                Tr::tr("Always Update"),
                [this, python, document, state] {
                    Core::ICore::settings()->setValue(alwaysUpdateKey, true);
                    installPythonLanguageServer(python, document, state.pylsModulePath, false, true);
                },
                {},
                InfoBarEntry::ButtonAction::SuppressPersistently);
            info.addCustomButton(
                Tr::tr("Update"),
                [this, python, document, state] {
                    installPythonLanguageServer(python, document, state.pylsModulePath, false, true);
                },
                {},
                InfoBarEntry::ButtonAction::Hide);
            info.addCustomButton(
                Tr::tr("Never"),
                [document, python] {
                    if (auto client = clientForPython(python))
                        LanguageClientManager::openDocumentWithClient(document, client);
                },
                {},
                InfoBarEntry::ButtonAction::SuppressPersistently);
            info.setCancelButtonInfo([python, document]{
                if (auto client = clientForPython(python))
                    LanguageClientManager::openDocumentWithClient(document, client);
            });
            infoBar->addInfo(info);

            m_infoBarEntries[python] << document;
        } else if (Core::ICore::settings()->value(alwaysUpdateKey, false).toBool()) {
            installPythonLanguageServer(python, document, state.pylsModulePath, true, true);
        } else if (auto client = clientForPython(python)) {
            LanguageClientManager::openDocumentWithClient(document, client);
        }
    } else if (state.state == PythonLanguageServerState::Installed) {
        if (auto client = clientForPython(python))
            LanguageClientManager::openDocumentWithClient(document, client);
    }
}

void PyLSConfigureAssistant::resetEditorInfoBar(TextDocument *document)
{
    for (QList<TextDocument *> &documents : m_infoBarEntries)
        documents.removeAll(document);
    document->infoBar()->removeInfo(installPylsInfoBarId);
    m_documentRunner.resetKey(document);
}

PyLSConfigureAssistant::PyLSConfigureAssistant()
{
    Core::EditorManager::instance();

    connect(Core::EditorManager::instance(),
            &Core::EditorManager::documentClosed,
            this,
            [this](Core::IDocument *document) {
                if (auto textDocument = qobject_cast<TextDocument *>(document))
                    resetEditorInfoBar(textDocument);
            });
}

// OPENMV-DIFF //
// static PyLSConfigureAssistant *pyLSConfigureAssistant()
// OPENMV-DIFF //
PyLSConfigureAssistant *pyLSConfigureAssistant()
// OPENMV-DIFF //
{
    static PyLSConfigureAssistant thePyLSConfigureAssistant;
    return &thePyLSConfigureAssistant;
}

void openDocumentWithPython(const FilePath &python, TextDocument *document)
{
    // OPENMV-DIFF //
    // pyLSConfigureAssistant().openDocument(python, document);
    // OPENMV-DIFF //
    pyLSConfigureAssistant()->openDocument(python, document);
    // OPENMV-DIFF //
}

// OPENMV-DIFF //
bool runningChecksDone() {
    // m_runningChecks (QFutureWatcher map) was replaced by m_documentRunner in 20.0.2.
    return !pyLSConfigureAssistant()->m_documentRunner.isRunning();
}
// OPENMV-DIFF //

} // Python::Internal
