// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pipsupport.h"
// OPENMV-DIFF //
// #include "pythonbuildconfiguration.h"
// OPENMV-DIFF //
#include "pythonconstants.h"
#include "pythoneditor.h"
#include "pythonhighlighter.h"
#include "pythonkitaspect.h"
#include "pythonproject.h"
#include "pythonrunconfiguration.h"
#include "pythonsettings.h"
#include "pythontr.h"
#include "pythonwizardpage.h"
// OPENMV-DIFF //
#include "pythonlanguageclient.h"
// OPENMV-DIFF //

#ifdef WITH_TESTS
#include "tests/pyprojecttoml_test.h"
#endif // WITH_TESTS

#include <extensionsystem/iplugin.h>

#include <projectexplorer/buildtargetinfo.h>
#include <projectexplorer/jsonwizard/jsonwizardfactory.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/taskhub.h>

#include <texteditor/texteditorsettings.h>

#include <utils/fsengine/fileiconprovider.h>
#include <utils/theme/theme.h>

#include <QFutureWatcher>

using namespace ProjectExplorer;
using namespace Utils;

namespace Python::Internal {

static QFuture<QTextDocument *> highlightCode(const QString &code, const QString &mimeType)
{
    QTextDocument *document = new QTextDocument;
    document->setPlainText(code);

    std::shared_ptr<QPromise<QTextDocument *>> promise
        = std::make_shared<QPromise<QTextDocument *>>();

    promise->start();

    TextEditor::SyntaxHighlighter *highlighter = createPythonHighlighter();

    QObject::connect(
        highlighter, &TextEditor::SyntaxHighlighter::finished, document, [document, promise]() {
            promise->addResult(document);
            promise->finish();
        });

    QFutureWatcher<QTextDocument *> *watcher = new QFutureWatcher<QTextDocument *>(document);
    QObject::connect(watcher, &QFutureWatcher<QTextDocument *>::canceled, document, [document]() {
        document->deleteLater();
    });
    watcher->setFuture(promise->future());

    highlighter->setParent(document);
    highlighter->setFontSettings(TextEditor::TextEditorSettings::fontSettings());
    highlighter->setMimeType(mimeType);
    highlighter->setDocument(document);
    highlighter->rehighlight();

    return promise->future();
}

class PythonPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Python.json")

    void initialize() final
    {
#ifdef WITH_TESTS
        addTestCreator(createPyProjectTomlTest);
#endif
        Core::IOptionsPage::registerCategory(
            Constants::C_PYTHON_SETTINGS_CATEGORY,
            Tr::tr("Python"),
            ":/python/images/settingscategory_python.png");

        setupPythonEditorFactory(this);

        // OPENMV-DIFF //
        // setupPySideBuildStep();
        // setupPythonBuildConfiguration();
        // OPENMV-DIFF //

        // OPENMV-DIFF //
        // setupPythonRunConfiguration();
        // setupPythonRunWorker();
        // setupPythonDebugWorker();
        // setupPythonOutputParser();
        // OPENMV-DIFF //

        // OPENMV-DIFF //
        // setupPythonSettings() lost its argument in 20.0.2 (was setupPythonSettings(this)).
        // setupPipSupport(this) is obsolete in 20.0.2 (pip is driven by pipInstallerTask()), dropped.
        // OPENMV-DIFF //
        setupPythonSettings();

        // OPENMV-DIFF //
        // setupPythonWizard();
        // OPENMV-DIFF //

        // OPENMV-DIFF //
        // KitManager::setIrrelevantAspects(
        //     KitManager::irrelevantAspects() + QSet<Id>{PythonKitAspect::id()});
        // OPENMV-DIFF //

        // OPENMV-DIFF //
        // The issuesGenerator below replaces OpenMV's old PythonProject::projectIssues()
        // (same "No Python interpreter set" diagnostic), so it is kept.
        // OPENMV-DIFF //
        const auto issuesGenerator = [](const Kit *k) -> Tasks {
            if (!PythonKitAspect::python(k))
                return {BuildSystemTask(
                    Task::Error,
                    Tr::tr("No Python interpreter set for kit \"%1\".").arg(k->displayName()))};
            return {};
        };
        ProjectManager::registerProjectType<PythonProject>(
            Constants::C_PY_PROJECT_MIME_TYPE, issuesGenerator);
        ProjectManager::registerProjectType<PythonProject>(
            Constants::C_PY_PROJECT_MIME_TYPE_LEGACY, issuesGenerator);
        ProjectManager::registerProjectType<PythonProject>(
            Constants::C_PY_PROJECT_MIME_TYPE_TOML, issuesGenerator);

        // OPENMV-DIFF //
        // Python in chat code blocks is highlighted like the IDE's editors, which
        // use the generic (KSyntaxHighlighting) highlighter, not this plugin's.
        // auto oldHighlighter = Utils::Text::codeHighlighter();
        // Utils::Text::setCodeHighlighter(
        //     [oldHighlighter](const QString &code, const QString &mimeType)
        //         -> QFuture<QTextDocument *> {
        //         if (mimeType == "text/python" || mimeType == "text/x-python"
        //             || mimeType == "text/x-python3") {
        //             return highlightCode(code, mimeType);
        //         }
        //
        //         return oldHighlighter(code, mimeType);
        //     });
        Q_UNUSED(&highlightCode)
        // OPENMV-DIFF //
    }

    void extensionsInitialized() final
    {
        // Add MIME overlay icons (these icons displayed at Project dock panel)
        const QString imageFile = Utils::creatorTheme()->imageFile(Theme::IconOverlayPro,
                                                               ProjectExplorer::Constants::FILEOVERLAY_PY);
        FileIconProvider::registerIconOverlayForSuffix(imageFile, "py");

        TaskHub::addCategory({PythonErrorTaskCategory,
                              "Python",
                              Tr::tr("Issues parsed from Python runtime output."),
                              true});
    }

    // OPENMV-DIFF //
    ExtensionSystem::IPlugin::ShutdownFlag aboutToShutdown()
    {
        if (runningChecksDone())
        {
            return ExtensionSystem::IPlugin::SynchronousShutdown;
        }
        else
        {
            connect(pyLSConfigureAssistant(), &PyLSConfigureAssistant::runningChecksDone, this, &PythonPlugin::asynchronousShutdownFinished);
            return ExtensionSystem::IPlugin::AsynchronousShutdown;
        }
    }
    // OPENMV-DIFF //
};

} // namespace Python::Internal

#include "pythonplugin.moc"
