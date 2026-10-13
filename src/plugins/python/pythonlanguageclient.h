// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>
#include <utils/temporarydirectory.h>

#include <languageclient/client.h>
#include <languageclient/languageclientsettings.h>

#include <QtTaskTree/QMappedTaskTreeRunner>

// OPENMV-DIFF //
#if defined(PYTHON_LIBRARY)
#  define PYTHON_EXPORT Q_DECL_EXPORT
#elif defined(PYTHON_STATIC_LIBRARY)
#  define PYTHON_EXPORT
#else
#  define PYTHON_EXPORT Q_DECL_IMPORT
#endif
// OPENMV-DIFF //

// OPENMV-DIFF //
// namespace ProjectExplorer { class ExtraCompiler; }
// OPENMV-DIFF //
namespace TextEditor { class TextDocument; }

namespace Python::Internal {

class PyLSInterface;
// OPENMV-DIFF //
// class PySideUicExtraCompiler;
// OPENMV-DIFF //
class PythonLanguageServerState;

// OPENMV-DIFF //
// OpenMV exposes PyLSConfigureAssistant in the header (upstream keeps it private in the
// .cpp) so the plugin can connect to the runningChecksDone() signal for async shutdown.
// The body is kept in sync with upstream 20.0.2's TaskTree-based rewrite: the old
// QFutureWatcher<PythonLanguageServerState> m_runningChecks map and the openDocument()
// watcher logic were replaced by startCheckPythonLanguageServer() + m_documentRunner /
// m_pipInstallerRunner. Only OpenMV's Q_OBJECT + runningChecksDone() signal are additive.
class PyLSConfigureAssistant : public QObject
{
    Q_OBJECT
public:
    PyLSConfigureAssistant();

    void handlePyLSState(const Utils::FilePath &python,
                         const PythonLanguageServerState &state,
                         TextEditor::TextDocument *document);
    void resetEditorInfoBar(TextEditor::TextDocument *document);
    void installPythonLanguageServer(const Utils::FilePath &python,
                                     QPointer<TextEditor::TextDocument> document,
                                     const Utils::FilePath &pylsPath, bool silent, bool upgrade);
    void startCheckPythonLanguageServer(
        const Utils::FilePath &python, QPointer<TextEditor::TextDocument> document, bool pipUsable);
    void openDocument(const Utils::FilePath &python, TextEditor::TextDocument *document);

    QHash<Utils::FilePath, QList<TextEditor::TextDocument *>> m_infoBarEntries;
    QtTaskTree::QMappedTaskTreeRunner<TextEditor::TextDocument *> m_documentRunner;
    QtTaskTree::QSingleTaskTreeRunner m_pipInstallerRunner;
signals:
    void runningChecksDone();
};
// OPENMV-DIFF //

// OPENMV-DIFF //
// class PyLSClient : public LanguageClient::Client
// OPENMV-DIFF //
class PYTHON_EXPORT PyLSClient : public LanguageClient::Client
// OPENMV-DIFF //
{
    Q_OBJECT
public:
    explicit PyLSClient(PyLSInterface *interface);
    ~PyLSClient();

    void openDocument(TextEditor::TextDocument *document) override;
    // OPENMV-DIFF //
    // PySide extra-compiler plumbing dropped (no Python build configuration in OpenMV).
    // void buildConfigurationClosed(ProjectExplorer::BuildConfiguration *bc) override;
    // void updateExtraCompilers(const ProjectExplorer::Project *project);
    // OPENMV-DIFF //

    static PyLSClient *clientForPython(const Utils::FilePath &python);
    void updateConfiguration();

    // OPENMV-DIFF //
    static void setPortPath(const Utils::FilePath &portPath);
    // Extra directories (third-party repo stubs/ folders) added to jedi's
    // extra_paths so vendor .pyi stubs give completion for custom firmware APIs.
    // Set by the OpenMV plugin at startup; applied on the next updateConfiguration().
    static void setExtraStubPaths(const QStringList &paths);
    // OPENMV-DIFF //

private:
    // OPENMV-DIFF //
    // PySide extra-compiler helpers + their state removed.
    // void updateExtraCompilerContents(ProjectExplorer::ExtraCompiler *compiler,
    //                                  const Utils::FilePath &file);
    // void closeExtraDoc(const Utils::FilePath &file);
    // void deleteExtraCompiler(ProjectExplorer::ExtraCompiler *compiler);
    // OPENMV-DIFF //

    Utils::FilePaths m_extraWorkspaceDirs;
    // OPENMV-DIFF //
    // Utils::FilePath m_extraCompilerOutputDir;
    // QList<QPointer<PySideUicExtraCompiler>> m_extraCompilers;
    // OPENMV-DIFF //
};

void openDocumentWithPython(const Utils::FilePath &python, TextEditor::TextDocument *document);

// OPENMV-DIFF //
PyLSConfigureAssistant *pyLSConfigureAssistant();
bool runningChecksDone();
// OPENMV-DIFF //

} // Python::Internal
