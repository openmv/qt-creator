// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pythonsettings.h"

#include "pythonconstants.h"
#include "pythonkitaspect.h"
#include "pythontr.h"
#include "pythonutils.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/progressmanager/processprogress.h>

// OPENMV-DIFF //
// Debugger plugin is disabled in OpenMV; kits carry no debugger aspect.
// #include <debugger/debuggerkitaspect.h>
// OPENMV-DIFF //

#include <projectexplorer/kitaspect.h>
#include <projectexplorer/environmentkitaspect.h>
#include <projectexplorer/kitmanager.h>

#include <extensionsystem/pluginmanager.h>

#include <languageclient/languageclient_global.h>
#include <languageclient/languageclientsettings.h>
#include <languageclient/languageclientmanager.h>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/detailswidget.h>
#include <utils/environment.h>
#include <utils/layoutbuilder.h>
#include <utils/listmodel.h>
#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>
#include <utils/qtcassert.h>
#include <utils/treemodel.h>
#include <utils/utilsicons.h>
#include <utils/shutdownguard.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace Python::Internal {

// This is defacto the unapplied state of PythonSettings::defaultInterpreter().id()
static QString s_defaultId;

InterpreterModel::InterpreterModel(const std::function<bool(QString)> &isDefaultId)
{
    setDataAccessor([isDefaultId](const Interpreter &interpreter, int column, int role) -> QVariant {
        if (interpreter.id == "none") {
            if (role == Qt::DisplayRole)
                return Tr::tr("None", "No Python interpreter");
            if (role == KitAspect::IsNoneRole)
                return true;
            return {};
        }
        switch (role) {
        case Qt::DisplayRole:
            return interpreter.name;
        case Qt::FontRole: {
            QFont f;
            f.setBold(isDefaultId && isDefaultId(interpreter.id));
            return f;
        }
        case Qt::ToolTipRole:
            if (!interpreter.command.isLocal())
                break;
            if (interpreter.command.isEmpty())
                return Tr::tr("Executable is empty.");
            if (!interpreter.command.exists())
                return Tr::tr("\"%1\" does not exist.").arg(interpreter.command.toUserOutput());
            if (!interpreter.command.isExecutableFile())
                return Tr::tr("\"%1\" is not an executable file.")
                    .arg(interpreter.command.toUserOutput());
            break;
        case Qt::DecorationRole:
            if (column == 0 && !PythonSettings::interpreterIsValid(interpreter))
                return Utils::Icons::CRITICAL.icon();
            break;
        case KitAspect::IdRole:
            return interpreter.id;
        case KitAspect::QualityRole:
            return int(PythonSettings::interpreterIsValid(interpreter));
        default:
            break;
        }
        return {};
    });
    setAllData(PythonSettings::interpreters());
}

// This is defacto the unapplied state of PythonSettings::interpreters()
InterpreterModel &interpreterModel()
{
    static InterpreterModel theInterpreterModel([](const QString &id) { return id == s_defaultId; });
    return theInterpreterModel;
}

Interpreter PythonSettings::createInterpreter(
    const FilePath &python,
    const QString &defaultName,
    const QString &suffix,
    const DetectionSource &detectionSource)
{
    Interpreter result;
    result.id = QUuid::createUuid().toString();
    result.command = python;
    result.detectionSource = detectionSource;

    result.name = pythonVersion(python);
    if (result.name.isEmpty())
        result.name = defaultName;
    QDir pythonDir(python.parentDir().toUrlishString());
    if (pythonDir.exists() && pythonDir.exists("activate") && pythonDir.cdUp())
        result.name += QString(" (%1)").arg(pythonDir.dirName());
    if (!suffix.isEmpty())
        result.name += ' ' + suffix;

    return result;
}

class InterpreterDetailsWidget : public QWidget
{
    Q_OBJECT
public:
    InterpreterDetailsWidget()
    {
        m_executable.setExpectedKind(PathChooser::ExistingCommand);
        m_executable.setAllowPathFromDevice(true);

        connect(&m_name, &QLineEdit::textChanged, this, &InterpreterDetailsWidget::changed);
        connect(&m_executable, &PathChooser::textChanged, this, &InterpreterDetailsWidget::changed);

        using namespace Layouting;

        Form {
            Tr::tr("Name:"), &m_name, br,
            Tr::tr("Executable:"), &m_executable,
            noMargin
        }.attachTo(this);
    }

    void updateInterpreter(const Interpreter &interpreter)
    {
        QSignalBlocker blocker(this); // do not emit changed when we change the controls here
        m_currentInterpreter = interpreter;
        m_name.setText(interpreter.name);
        m_executable.setFilePath(interpreter.command);
    }

    Interpreter toInterpreter()
    {
        m_currentInterpreter.command = m_executable.filePath();
        m_currentInterpreter.name = m_name.text();
        return m_currentInterpreter;
    }
    QLineEdit m_name;
    PathChooser m_executable;
    Interpreter m_currentInterpreter;

signals:
    void changed();
};


class InterpreterOptionsWidget : public Core::IOptionsPageWidget
{
public:
    InterpreterOptionsWidget();

    void apply() override
    {
        PythonSettings::setInterpreter(interpreterModel().interpreters(), s_defaultId);
    }

    void cancel() override
    {
        interpreterModel().setInterpreters(PythonSettings::interpreters());
        s_defaultId = PythonSettings::defaultInterpreterId();
    }

private:
    void currentChanged(const QModelIndex &index, const QModelIndex &previous);
    void detailsChanged();
    void updateCleanButton();
    void updateGenerateKitButton(const Interpreter &interpreter);
    void addItem();
    void deleteItem();
    void makeDefault();
    void generateKit();
    void cleanUp();

    QTreeView m_view;
    InterpreterDetailsWidget m_detailsWidget;
    QPushButton m_addButton;
    QPushButton m_deleteButton;
    QPushButton m_makeDefaultButton;
    QPushButton m_generateKitButton;
    QPushButton m_cleanButton;
};

InterpreterOptionsWidget::InterpreterOptionsWidget()
{
    m_addButton.setText(Tr::tr("&Add"));

    m_deleteButton.setText(Tr::tr("&Delete"));
    m_deleteButton.setEnabled(false);
    m_makeDefaultButton.setText(Tr::tr("&Make Default"));
    m_makeDefaultButton.setEnabled(false);
    m_generateKitButton.setText(Tr::tr("&Generate Kit"));
    m_generateKitButton.setEnabled(false);

    m_cleanButton.setText(Tr::tr("&Clean Up"));
    m_cleanButton.setToolTip(Tr::tr("Remove all Python interpreters without a valid executable."));

    using namespace Layouting;
    Row {
        Column {
            m_view,
            m_detailsWidget
        },
        Column {
            m_addButton,
            m_deleteButton,
            m_makeDefaultButton,
            m_generateKitButton,
            m_cleanButton,
            st
        }
    }.attachTo(this);

    updateCleanButton();

    m_detailsWidget.hide();

    m_view.setModel(&interpreterModel());
    m_view.setHeaderHidden(true);
    m_view.setSelectionMode(QAbstractItemView::SingleSelection);
    m_view.setSelectionBehavior(QAbstractItemView::SelectItems);

    connect(&m_addButton, &QPushButton::pressed, this, &InterpreterOptionsWidget::addItem);
    connect(&m_deleteButton, &QPushButton::pressed, this, &InterpreterOptionsWidget::deleteItem);
    connect(&m_makeDefaultButton, &QPushButton::pressed, this, &InterpreterOptionsWidget::makeDefault);
    connect(&m_generateKitButton, &QPushButton::pressed, this, &InterpreterOptionsWidget::generateKit);
    connect(&m_cleanButton, &QPushButton::pressed, this, &InterpreterOptionsWidget::cleanUp);

    connect(&m_detailsWidget, &InterpreterDetailsWidget::changed,
            this, &InterpreterOptionsWidget::detailsChanged);
    connect(m_view.selectionModel(), &QItemSelectionModel::currentChanged,
            this, &InterpreterOptionsWidget::currentChanged);

    installMarkSettingsDirtyTriggerRecursively(this);
}

void InterpreterModel::addInterpreter(const Interpreter &interpreter)
{
    appendItem(interpreter);
}

void InterpreterModel::removeInterpreterFrom(const QString &detectionSource)
{
    destroyItems([&detectionSource](const Interpreter &interpreter) {
        return interpreter.detectionSource.id == detectionSource;
    });
}

void InterpreterModel::setInterpreters(const QList<Interpreter> &interpreters)
{
    clear();
    for (const Interpreter &interpreter : interpreters)
        appendItem(interpreter);
}

QList<Interpreter> InterpreterModel::interpreters() const
{
    QList<Interpreter> interpreters;
    for (const TreeItem *treeItem : *this)
        interpreters << static_cast<const ListItem<Interpreter> *>(treeItem)->itemData;
    return interpreters;
}

QList<Interpreter> InterpreterModel::interpreterFrom(const QString &detectionSource) const
{
    return allData([&detectionSource](const Interpreter &interpreter) {
        return interpreter.detectionSource.id == detectionSource;
    });
}

void InterpreterOptionsWidget::currentChanged(const QModelIndex &index, const QModelIndex &previous)
{
    if (previous.isValid()) {
        interpreterModel().itemAt(previous.row())->itemData = m_detailsWidget.toInterpreter();
        emit interpreterModel().dataChanged(previous, previous);
    }
    if (index.isValid()) {
        const Interpreter interpreter = interpreterModel().itemAt(index.row())->itemData;
        m_detailsWidget.updateInterpreter(interpreter);
        m_detailsWidget.show();
        updateGenerateKitButton(interpreter);
    } else {
        m_detailsWidget.hide();
        m_generateKitButton.setEnabled(false);
    }
    m_deleteButton.setEnabled(index.isValid());
    m_makeDefaultButton.setEnabled(index.isValid());
}

void InterpreterOptionsWidget::detailsChanged()
{
    const QModelIndex &index = m_view.currentIndex();
    if (index.isValid()) {
        const Interpreter interpreter = m_detailsWidget.toInterpreter();
        interpreterModel().itemAt(index.row())->itemData = interpreter;
        emit interpreterModel().dataChanged(index, index);
        updateGenerateKitButton(interpreter);
    }
    updateCleanButton();
}

void InterpreterOptionsWidget::updateCleanButton()
{
    m_cleanButton.setEnabled(Utils::anyOf(interpreterModel().allData(), [](const Interpreter &interpreter) {
        return !interpreter.command.isExecutableFile();
    }));
}

void InterpreterOptionsWidget::updateGenerateKitButton(const Interpreter &interpreter)
{
    bool enabled = !KitManager::kit(Id::fromString(interpreter.id))
            && (!interpreter.command.isLocal() || interpreter.command.isExecutableFile());
    m_generateKitButton.setEnabled(enabled);
}

void InterpreterOptionsWidget::addItem()
{
    const QModelIndex &index = interpreterModel().indexForItem(interpreterModel().appendItem(
        {QUuid::createUuid().toString(), QString("Python"), FilePath(), DetectionSource::Manual}));
    QTC_ASSERT(index.isValid(), return);
    m_view.setCurrentIndex(index);
    updateCleanButton();
    markSettingsDirty();
}

void InterpreterOptionsWidget::deleteItem()
{
    const QModelIndex &index = m_view.currentIndex();
    if (index.isValid()) {
        interpreterModel().destroyItem(interpreterModel().itemAt(index.row()));
        markSettingsDirty();
    }
    updateCleanButton();
}

static const QStringList &plugins()
{
    static const QStringList plugins{"flake8",
                                     "jedi_completion",
                                     "jedi_definition",
                                     "jedi_hover",
                                     "jedi_references",
                                     "jedi_signature_help",
                                     "jedi_symbols",
                                     "mccabe",
                                     "pycodestyle",
                                     "pydocstyle",
                                     "pyflakes",
                                     "pylint",
                                     "yapf"};
    return plugins;
}

class PyLSConfigureWidget : public Core::IOptionsPageWidget
{
public:
    PyLSConfigureWidget()
        : m_editor(LanguageClient::createJsonEditor(this))
        , m_advancedLabel(new QLabel)
        , m_pluginsGroup(new QGroupBox(Tr::tr("Plugins:")))
        , m_mainGroup(new QGroupBox(Tr::tr("Use Python Language Server")))
    {
        m_mainGroup->setCheckable(true);

        auto mainGroupLayout = new QVBoxLayout;

        auto pluginsLayout = new QVBoxLayout;
        m_pluginsGroup->setLayout(pluginsLayout);
        m_pluginsGroup->setFlat(true);
        for (const QString &plugin : plugins()) {
            auto checkBox = new QCheckBox(plugin, this);
            connect(checkBox, &QCheckBox::clicked, this, [this, plugin, checkBox]() {
                updatePluginEnabled(checkBox->checkState(), plugin);
            });
            m_checkBoxes[plugin] = checkBox;
            pluginsLayout->addWidget(checkBox);
        }

        mainGroupLayout->addWidget(m_pluginsGroup);

        const QString labelText = Tr::tr("For a complete list of available options, consult the "
                                         "[Python LSP Server configuration documentation](%1).")
                                      .arg("https://github.com/python-lsp/python-lsp-server/blob/"
                                           "develop/CONFIGURATION.md");
        m_advancedLabel->setTextFormat(Qt::MarkdownText);
        m_advancedLabel->setText(labelText);
        m_advancedLabel->setOpenExternalLinks(true);
        mainGroupLayout->addWidget(m_advancedLabel);
        mainGroupLayout->addWidget(m_editor->editorWidget(), 1);

        mainGroupLayout->addStretch();

        auto advanced = new QCheckBox(Tr::tr("Advanced"));
        setIgnoreForDirtyHook(advanced);
        advanced->setChecked(false);
        mainGroupLayout->addWidget(advanced);

        // OPENMV-DIFF //
        // m_mainGroup->setLayout(mainGroupLayout);
        // OPENMV-DIFF //
        m_mainGroup->hide();
        // OPENMV-DIFF //

        // OPENMV-DIFF //
        // QVBoxLayout *mainLayout = new QVBoxLayout;
        // mainLayout->addWidget(m_mainGroup);
        // setLayout(mainLayout);
        // OPENMV-DIFF //
        setLayout(mainGroupLayout);
        // OPENMV-DIFF //

        m_editor->textDocument()->setPlainText(PythonSettings::pylsConfiguration());
        m_mainGroup->setChecked(PythonSettings::pylsEnabled());
        updateCheckboxes();

        setAdvanced(false);

        connect(advanced,
                &QCheckBox::toggled,
                this,
                &PyLSConfigureWidget::setAdvanced);

        installMarkSettingsDirtyTriggerRecursively(this);
        connect(m_editor->textDocument(), &TextEditor::TextDocument::contentsChangedWithPosition,
                this, markSettingsDirty);
    }

    void apply() override
    {
        PythonSettings::setPylsEnabled(m_mainGroup->isChecked());
        PythonSettings::setPyLSConfiguration(m_editor->textDocument()->plainText());
    }
private:
    void setAdvanced(bool advanced)
    {
        m_editor->editorWidget()->setVisible(advanced);
        m_advancedLabel->setVisible(advanced);
        m_pluginsGroup->setVisible(!advanced);
        updateCheckboxes();
    }

    void updateCheckboxes()
    {
        const QJsonDocument document = QJsonDocument::fromJson(
            m_editor->textDocument()->plainText().toUtf8());
        if (document.isObject()) {
            const QJsonObject pluginsObject
                = document.object()["pylsp"].toObject()["plugins"].toObject();
            for (const QString &plugin : plugins()) {
                auto checkBox = m_checkBoxes[plugin];
                if (!checkBox)
                    continue;
                const QJsonValue enabled = pluginsObject[plugin].toObject()["enabled"];
                if (!enabled.isBool())
                    checkBox->setCheckState(Qt::PartiallyChecked);
                else
                    checkBox->setCheckState(enabled.toBool(false) ? Qt::Checked : Qt::Unchecked);
            }
        }
    }

    void updatePluginEnabled(Qt::CheckState check, const QString &plugin)
    {
        if (check == Qt::PartiallyChecked)
            return;
        QJsonDocument document = QJsonDocument::fromJson(
            m_editor->textDocument()->plainText().toUtf8());
        QJsonObject config;
        if (!document.isNull())
            config = document.object();
        QJsonObject pylsp = config["pylsp"].toObject();
        QJsonObject plugins = pylsp["plugins"].toObject();
        QJsonObject pluginValue = plugins[plugin].toObject();
        pluginValue.insert("enabled", check == Qt::Checked);
        plugins.insert(plugin, pluginValue);
        pylsp.insert("plugins", plugins);
        config.insert("pylsp", pylsp);
        document.setObject(config);
        m_editor->textDocument()->setPlainText(QString::fromUtf8(document.toJson()));
    }

    QMap<QString, QCheckBox *> m_checkBoxes;
    TextEditor::BaseTextEditor *m_editor = nullptr;
    QLabel *m_advancedLabel = nullptr;
    QGroupBox *m_pluginsGroup = nullptr;
    QGroupBox *m_mainGroup = nullptr;
};


class PyLSOptionsPage : public Core::IOptionsPage
{
public:
    PyLSOptionsPage()
    {
        setId(Constants::C_PYLSCONFIGURATION_PAGE_ID);
        setDisplayName(Tr::tr("Language Server Configuration"));
        setCategory(Constants::C_PYTHON_SETTINGS_CATEGORY);
        setWidgetCreator([] {return new PyLSConfigureWidget();});
        // OPENMV-DIFF //
        IOptionsPage::registerCategory(
            Constants::C_PYTHON_SETTINGS_CATEGORY,
            Tr::tr("Python"),
            ":/python/images/settingscategory_python.png");
        // OPENMV-DIFF //
    }
};

static PyLSOptionsPage &pylspOptionsPage()
{
    static PyLSOptionsPage page;
    return page;
}

void InterpreterOptionsWidget::makeDefault()
{
    const QModelIndex &index = m_view.currentIndex();
    if (index.isValid()) {
        QModelIndex defaultIndex = interpreterModel().findIndex([](const Interpreter &interpreter) {
            return interpreter.id == s_defaultId;
        });
        s_defaultId = interpreterModel().itemAt(index.row())->itemData.id;
        emit interpreterModel().dataChanged(index, index, {Qt::FontRole});
        if (defaultIndex.isValid())
            emit interpreterModel().dataChanged(defaultIndex, defaultIndex, {Qt::FontRole});
        if (!defaultIndex.isValid() || defaultIndex != index)
            markSettingsDirty();
    }
}

void InterpreterOptionsWidget::generateKit()
{
    const QModelIndex &index = m_view.currentIndex();
    if (index.isValid())
        PythonSettings::addKitsForInterpreter(interpreterModel().itemAt(index.row())->itemData, true);
    m_generateKitButton.setEnabled(false);
}

void InterpreterOptionsWidget::cleanUp()
{
    interpreterModel().destroyItems(
        [](const Interpreter &interpreter) { return !interpreter.command.isExecutableFile(); });
    updateCleanButton();
    markSettingsDirty();
}

constexpr char settingsGroupKey[] = "Python";
constexpr char interpreterKey[] = "Interpeter";
constexpr char defaultKey[] = "DefaultInterpeter";
constexpr char kitsGeneratedKey[] = "KitsGenerated";
constexpr char pylsEnabledKey[] = "PylsEnabled";
constexpr char pylsConfigurationKey[] = "PylsConfiguration";

static QString defaultPylsConfiguration()
{
    static QJsonObject configuration;
    if (configuration.isEmpty()) {
        QJsonObject enabled;
        enabled.insert("enabled", true);
        QJsonObject disabled;
        disabled.insert("enabled", false);
        // OPENMV-DIFF //
        // flake8 off and pycodestyle on (pylsp's own defaults, spelled out), both
        // with 120-column lines. One object per plugin: a second insert() under the
        // same key replaces the first, which used to drop the "enabled" flags.
        QJsonObject flake8;
        flake8.insert("enabled", false);
        flake8.insert("maxLineLength", 120);
        QJsonObject pycodestyle;
        pycodestyle.insert("enabled", true);
        pycodestyle.insert("maxLineLength", 120);
        // OPENMV-DIFF //
        QJsonObject plugins;
        // OPENMV-DIFF //
        // plugins.insert("flake8", disabled);
        // OPENMV-DIFF //
        plugins.insert("flake8", flake8);
        // OPENMV-DIFF //
        plugins.insert("jedi_completion", enabled);
        plugins.insert("jedi_definition", enabled);
        plugins.insert("jedi_hover", enabled);
        plugins.insert("jedi_references", enabled);
        plugins.insert("jedi_signature_help", enabled);
        plugins.insert("jedi_symbols", enabled);
        plugins.insert("mccabe", disabled);
        // OPENMV-DIFF //
        // plugins.insert("pycodestyle", disabled);
        // OPENMV-DIFF //
        plugins.insert("pycodestyle", pycodestyle);
        // OPENMV-DIFF //
        plugins.insert("pydocstyle", disabled);
        plugins.insert("pyflakes", enabled);
        plugins.insert("pylint", disabled);
        plugins.insert("yapf", enabled);
        QJsonObject pylsp;
        pylsp.insert("plugins", plugins);
        configuration.insert("pylsp", pylsp);
    }
    return QString::fromUtf8(QJsonDocument(configuration).toJson());
}

void PythonSettings::disableOutdatedPylsNow()
{
    using namespace LanguageClient;
    const QList<BaseSettings *>
            settings = LanguageClientSettings::pageSettings();
    for (const BaseSettings *setting : settings) {
        if (setting->settingsTypeId() != LanguageClient::Constants::LANGUAGECLIENT_STDIO_SETTINGS_ID)
            continue;
        auto stdioSetting = static_cast<const StdIOSettings *>(setting);
        if (stdioSetting->arguments().startsWith("-m pyls")
                && stdioSetting->languageFilter().isSupported("foo.py", Constants::C_PY_MIMETYPE)) {
            LanguageClientManager::enableClientSettings(stdioSetting->id(), false);
        }
    }
}

void PythonSettings::disableOutdatedPyls()
{
    using namespace ExtensionSystem;
    if (PluginManager::isInitializationDone()) {
        disableOutdatedPylsNow();
    } else {
        QObject::connect(PluginManager::instance(), &PluginManager::initializationDone,
                         this, &PythonSettings::disableOutdatedPylsNow);
    }
}

static void pythonsFromRegistry(QPromise<QList<Interpreter>> &promise)
{
    QList<Interpreter> pythons;
    QSettings pythonRegistry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore",
                             QSettings::NativeFormat);
    for (const QString &versionGroup : pythonRegistry.childGroups()) {
        if (promise.isCanceled())
            return;

        pythonRegistry.beginGroup(versionGroup);
        QString name = pythonRegistry.value("DisplayName").toString();
        QVariant regVal = pythonRegistry.value("InstallPath/ExecutablePath");
        if (regVal.isValid()) {
            const FilePath &executable = FilePath::fromUserInput(regVal.toString());
            if (executable.exists()) {
                pythons << Interpreter{QUuid::createUuid().toString(),
                                       name,
                                       FilePath::fromUserInput(regVal.toString())};
            }
        }
        regVal = pythonRegistry.value("InstallPath/.");
        if (regVal.isValid()) {
            const FilePath &path = FilePath::fromUserInput(regVal.toString());
            const FilePath python = path.pathAppended("python").withExecutableSuffix();
            if (python.exists())
                pythons << PythonSettings::createInterpreter(python, "Python " + versionGroup);
        }
        pythonRegistry.endGroup();
    }
    promise.addResult(pythons);
}

static void pythonsFromPath(QPromise<QList<Interpreter>> &promise)
{
    QList<Interpreter> pythons;
    if (HostOsInfo::isWindowsHost()) {
        for (const FilePath &executable : FilePath("python").searchAllInPath()) {
            if (promise.isCanceled())
                return;

            // Windows creates empty redirector files that may interfere
            if (executable.toFileInfo().size() == 0)
                continue;
            if (executable.exists())
                pythons << PythonSettings::createInterpreter(executable, "Python from Path");
        }
    } else {
        const QStringList filters = {"python",
                                     "python[1-9].[0-9]",
                                     "python[1-9].[1-9][0-9]",
                                     "python[1-9]"};
        const FilePaths dirs = Environment::systemEnvironment().path();
        QSet<FilePath> used;
        for (const FilePath &path : dirs) {
            const QDir dir(path.toUrlishString());
            for (const QFileInfo &fi : dir.entryInfoList(filters)) {
                if (promise.isCanceled())
                    return;

                const FilePath executable = FilePath::fromUserInput(fi.canonicalFilePath());
                if (!used.contains(executable) && executable.exists()) {
                    used.insert(executable);
                    // OPENMV-DIFF //
                    // pythons << PythonSettings::createInterpreter(executable, "Python from Path");
                    // OPENMV-DIFF //
                    Interpreter i = PythonSettings::createInterpreter(executable, "Python from Path");
                    if (i.name.contains(QRegularExpression("Python 3"))) {
                        if (pythons.isEmpty()) pythons << i;
                        else {
                            // Insertion sort.
                            bool inserted = false;
                            for (int j = 0; j < pythons.size(); ++j) {
                                if (i.name > pythons[j].name) {
                                    pythons.insert(j, i);
                                    inserted = true;
                                    break;
                                }
                            }
                            if (!inserted) {
                                pythons.append(i);
                            }
                        }
                    }
                    // OPENMV-DIFF //
                }
            }
        }
    }
    // OPENMV-DIFF //
    if (pythons.size() > 1)
        pythons = QList<Interpreter>() << pythons.takeFirst();
    // OPENMV-DIFF //
    promise.addResult(pythons);
}

static QString idForPythonFromPath(const QList<Interpreter> &pythons)
{
    FilePath pythonFromPath = FilePath("python3").searchInPath();
    if (pythonFromPath.isEmpty())
        pythonFromPath = FilePath("python").searchInPath();
    if (pythonFromPath.isEmpty())
        return {};
    const Interpreter &defaultInterpreter
        = findOrDefault(pythons, [pythonFromPath](const Interpreter &interpreter) {
              return interpreter.command == pythonFromPath;
          });
    return defaultInterpreter.id;
}

static PythonSettings *settingsInstance = nullptr;

static bool alreadyRegistered(const Interpreter &candidate)
{
    return Utils::anyOf(settingsInstance->interpreters(),
                        [candidate = candidate.command](const Interpreter &interpreter) {
                            return interpreter.command.isSameDevice(candidate)
                                   && interpreter.command.resolveSymlinks()
                                          == candidate.resolveSymlinks();
                        });
}

PythonSettings::PythonSettings()
{
    QTC_ASSERT(!settingsInstance, return);
    settingsInstance = this;

    setObjectName("PythonSettings");
    ExtensionSystem::PluginManager::addObject(this);

    initFromSettings(Core::ICore::settings());

    const auto onRegistrySetup = [](Async<QList<Interpreter>> &task) {
        task.setConcurrentCallData(pythonsFromRegistry);
    };
    const auto onPathSetup = [](Async<QList<Interpreter>> &task) {
        task.setConcurrentCallData(pythonsFromPath);
    };
    const auto onTaskDone = [](const Async<QList<Interpreter>> &task) {
        if (!task.isResultAvailable())
            return;

        const auto interpreters = task.result();
        for (const Interpreter &interpreter : interpreters) {
            if (!alreadyRegistered(interpreter))
                // OPENMV-DIFF //
                // settingsInstance->addInterpreter(interpreter);
                // OPENMV-DIFF //
                settingsInstance->addInterpreter(interpreter, true);
                // OPENMV-DIFF //
        }
    };

    const Group recipe {
        finishAllAndSuccess,
        Utils::HostOsInfo::isWindowsHost()
            ? AsyncTask<QList<Interpreter>>(onRegistrySetup, onTaskDone) : nullItem,
        AsyncTask<QList<Interpreter>>(onPathSetup, onTaskDone)
    };
    // OPENMV-DIFF //
    // m_taskTreeRunner.start(recipe);
    // OPENMV-DIFF //
    // Register the IDE's bundled interpreter at <resources>/<relPath> and return
    // the id of the resulting list entry. createInterpreter() mints a fresh uuid
    // on every launch, so if this path was already persisted (from a prior run)
    // its stored id differs; we must resolve the registered entry's id by command
    // path, otherwise forcing the default to the fresh id leaves it dangling and
    // detectPython() silently falls back to some other (e.g. stale) interpreter.
    const auto registerBundled = [](const QString &relPath) -> QString {
        const Interpreter interpreter = createInterpreter(Core::ICore::resourcePath(relPath), {});
        if (!alreadyRegistered(interpreter))
            settingsInstance->addInterpreter(interpreter, true);
        return Utils::findOrDefault(settingsInstance->interpreters(),
                                    [&interpreter](const Interpreter &i) {
                                        return i.command.resolveSymlinks()
                                               == interpreter.command.resolveSymlinks();
                                    })
            .id;
    };

    QString bundledInterpreterId;
    if (Utils::HostOsInfo::isWindowsHost())
    {
        bundledInterpreterId = registerBundled(QStringLiteral("python/win/python.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        // Apple Silicon (and an x86_64 binary under Rosetta) ships the native
        // arm64 interpreter under python/mac-aarch64; Intel ships python/mac.
        // The old unconditional "python/mac/bin/python" path does not exist on
        // arm64 builds, so the forced default below pointed at a missing
        // interpreter and silently disabled the Python language server.
        bundledInterpreterId = registerBundled(
            ((Utils::HostOsInfo::hostArchitecture() == Utils::OsArchArm64)
             || Utils::HostOsInfo::isRunningUnderRosetta())
                ? QStringLiteral("python/mac-aarch64/bin/python3.13")
                : QStringLiteral("python/mac/bin/python"));
    }
    else if (Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
            bundledInterpreterId = registerBundled(QStringLiteral("python/linux-x86/bin/python"));
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
            bundledInterpreterId = registerBundled(QStringLiteral("python/linux-x86_64/bin/python"));
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
            bundledInterpreterId = registerBundled(QStringLiteral("python/linux-arm/bin/python"));
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
            bundledInterpreterId = registerBundled(QStringLiteral("python/linux-arm64/bin/python"));
    }
    else
    {
        m_taskTreeRunner.start(recipe);
    }
    // Force the IDE's bundled interpreter as the default on every launch. A
    // stale persisted default (e.g. one pinned to a previous build tree, or a
    // system python removed by an OS/Xcode update) otherwise silently disables
    // the Python language server while in-process Kate highlighting still works.
    if (!bundledInterpreterId.isEmpty())
        m_defaultInterpreterId = bundledInterpreterId;
    // OPENMV-DIFF //

    if (m_defaultInterpreterId.isEmpty())
        m_defaultInterpreterId = idForPythonFromPath(m_interpreters);

    s_defaultId = m_defaultInterpreterId;

    writeToSettings(Core::ICore::settings());

    pylspOptionsPage();
}

PythonSettings::~PythonSettings()
{
    ExtensionSystem::PluginManager::removeObject(this);
    settingsInstance = nullptr;
}

static void setRelevantAspectsToKit(Kit *k)
{
    QTC_ASSERT(k, return);
    QSet<Utils::Id> relevantAspects = k->relevantAspects();
    // OPENMV-DIFF //
    // Dropped Debugger::DebuggerKitAspect::id() (Debugger plugin disabled).
    relevantAspects.unite(
        {PythonKitAspect::id(), EnvironmentKitAspect::id()});
    // OPENMV-DIFF //
    k->setRelevantAspects(relevantAspects);
}

void PythonSettings::addKitsForInterpreter(const Interpreter &interpreter, bool force)
{
    if (!KitManager::isLoaded()) {
        connect(KitManager::instance(),
                &KitManager::kitsLoaded,
                settingsInstance,
                [interpreter, force]() { addKitsForInterpreter(interpreter, force); });
        return;
    }

    const Id kitId = Id::fromString(interpreter.id);
    if (Kit *k = KitManager::kit(kitId)) {
        setRelevantAspectsToKit(k);
    } else if (force || !isVenvPython(interpreter.command)) {
        KitManager::registerKit(
            [interpreter](Kit *k) {
                if (interpreter.detectionSource.id.isEmpty())
                    k->setDetectionSource({DetectionSource::FromSystem, "Python"});
                else
                    k->setDetectionSource(interpreter.detectionSource);

                k->setUnexpandedDisplayName("%{Python:Name}");
                setRelevantAspectsToKit(k);
                PythonKitAspect::setPython(k, interpreter.id);
                k->setSticky(PythonKitAspect::id(), true);
            },
            kitId);
    }
}

void PythonSettings::removeKitsForInterpreter(const Interpreter &interpreter)
{
    if (!KitManager::isLoaded()) {
        connect(KitManager::instance(), &KitManager::kitsLoaded, settingsInstance, [interpreter]() {
            removeKitsForInterpreter(interpreter);
        });
        return;
    }

    if (Kit *k = KitManager::kit(Id::fromString(interpreter.id)))
        KitManager::deregisterKit(k);
}

bool PythonSettings::interpreterIsValid(const Interpreter &interpreter)
{
    return !interpreter.command.isLocal() || interpreter.command.isExecutableFile();
}

void PythonSettings::setInterpreter(const QList<Interpreter> &interpreters, const QString &defaultId)
{
    if (defaultId == settingsInstance->m_defaultInterpreterId
        && interpreters == settingsInstance->m_interpreters) {
        return;
    }
    QList<Interpreter> toRemove = settingsInstance->m_interpreters;
    for (const Interpreter &interpreter : interpreters) {
        if (!Utils::eraseOne(toRemove, Utils::equal(&Interpreter::id, interpreter.id)))
            addKitsForInterpreter(interpreter, false);
    }
    for (const Interpreter &interpreter : std::as_const(toRemove))
        removeKitsForInterpreter(interpreter);
    settingsInstance->m_interpreters = interpreters;
    settingsInstance->m_defaultInterpreterId = defaultId;
    saveSettings();
}

void PythonSettings::setPyLSConfiguration(const QString &configuration)
{
    if (configuration == settingsInstance->m_pylsConfiguration)
        return;
    settingsInstance->m_pylsConfiguration = configuration;
    saveSettings();
    emit instance()->pylsConfigurationChanged(configuration);
}

void PythonSettings::setPylsEnabled(const bool &enabled)
{
    if (enabled == settingsInstance->m_pylsEnabled)
        return;
    settingsInstance->m_pylsEnabled = enabled;
    saveSettings();
    emit instance()->pylsEnabledChanged(enabled);
}

bool PythonSettings::pylsEnabled()
{
    return settingsInstance->m_pylsEnabled;
}

QString PythonSettings::pylsConfiguration()
{
    return settingsInstance->m_pylsConfiguration;
}

void PythonSettings::addInterpreter(const Interpreter &interpreter, bool isDefault)
{
    if (Utils::anyOf(settingsInstance->m_interpreters, Utils::equal(&Interpreter::id, interpreter.id)))
        return;
    settingsInstance->m_interpreters.append(interpreter);
    if (isDefault)
        settingsInstance->m_defaultInterpreterId = interpreter.id;
    saveSettings();
    addKitsForInterpreter(interpreter, false);
}

Interpreter PythonSettings::addInterpreter(const FilePath &interpreterPath,
                                           bool isDefault,
                                           const QString &nameSuffix)
{
    const Interpreter interpreter = createInterpreter(interpreterPath, {}, nameSuffix);
    addInterpreter(interpreter, isDefault);
    markSettingsDirty();
    return interpreter;
}

PythonSettings *PythonSettings::instance()
{
    QTC_CHECK(settingsInstance);
    return settingsInstance;
}

void PythonSettings::createVirtualEnvironmentInteractive(
    const FilePath &startDirectory,
    const Interpreter &defaultInterpreter,
    const std::function<void(const FilePath &)> &callback)
{
    QDialog dialog;
    dialog.setModal(true);
    auto layout = new QFormLayout(&dialog);
    auto interpreters = new QComboBox;
    const QString preselectedId = defaultInterpreter.id.isEmpty()
                                      ? PythonSettings::defaultInterpreter().id
                                      : defaultInterpreter.id;
    for (const Interpreter &interpreter : PythonSettings::interpreters()) {
        interpreters->addItem(interpreter.name, interpreter.id);
        if (!preselectedId.isEmpty() && interpreter.id == preselectedId)
            interpreters->setCurrentIndex(interpreters->count() - 1);
    }
    layout->addRow(Tr::tr("Python interpreter:"), interpreters);
    auto pathChooser = new PathChooser();
    pathChooser->setInitialBrowsePathBackup(startDirectory);
    pathChooser->setExpectedKind(PathChooser::Directory);
    pathChooser->setPromptDialogTitle(Tr::tr("New Python Virtual Environment Directory"));
    layout->addRow(Tr::tr("Virtual environment directory:"), pathChooser);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto createButton = buttons->addButton(Tr::tr("Create"), QDialogButtonBox::AcceptRole);
    createButton->setEnabled(false);
    connect(pathChooser,
            &PathChooser::validChanged,
            createButton,
            [createButton](bool valid) { createButton->setEnabled(valid); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    dialog.setLayout(layout);
    if (dialog.exec() == QDialog::Rejected) {
        callback({});
        return;
    }

    const Interpreter interpreter = PythonSettings::interpreter(
        interpreters->currentData().toString());

    auto venvDir = pathChooser->filePath();
    createVirtualEnvironment(interpreter.command, venvDir, callback);
}

static QSet<FilePath> &venvsCurrentlySettingUp()
{
    static QSet<FilePath> venvs;
    return venvs;
}

bool PythonSettings::isRunningVirtualEnvironmentSetup(const FilePath &python)
{
    return Utils::anyOf(venvsCurrentlySettingUp(), [python](const FilePath &venv) {
        return python.isChildOf(venv);
    });
}

void PythonSettings::createVirtualEnvironment(
    const FilePath &python,
    const FilePath &directory,
    const std::function<void(const FilePath &)> &callback)
{
    QTC_ASSERT(python.isExecutableFile(), return);
    QTC_ASSERT(!directory.exists() || directory.isDir(), return);

    const CommandLine command(python, QStringList{"-m", "venv", directory.toUserOutput()});
    venvsCurrentlySettingUp().insert(directory);

    auto process = new Process;
    auto progress = new Core::ProcessProgress(process);
    progress->setDisplayName(Tr::tr("Create Python venv"));
    QObject::connect(process, &Process::done, [directory, process, callback](){
        venvsCurrentlySettingUp().remove(directory);
        switch (process->result()) {
        case ProcessResult::FinishedWithSuccess: {
            FilePath venvPython = directory.osType() == Utils::OsTypeWindows ? directory / "Scripts"
                                                                             : directory / "bin";
            venvPython = venvPython.pathAppended("python").withExecutableSuffix();
            if (venvPython.exists()) {
                if (callback)
                    callback(venvPython);
                emit instance()->virtualEnvironmentCreated(venvPython);
            }
            break;
        }
        case ProcessResult::FinishedWithError:
            Core::MessageManager::writeFlashing(
                Tr::tr("Venv creation failed:\n%1").arg(process->allOutput()));
            break;
        case ProcessResult::TerminatedAbnormally:
            Core::MessageManager::writeFlashing(
                Tr::tr("Venv creation terminated abnormally:\n%1").arg(process->errorString()));
            break;
        case ProcessResult::StartFailed:
            Core::MessageManager::writeFlashing(
                Tr::tr("Venv creation could not be started:\n%1").arg(process->allOutput()));
            break;
        case ProcessResult::Canceled:
            Core::MessageManager::writeFlashing(Tr::tr("Venv creation canceled."));
            break;
        }

        process->deleteLater();
    });
    process->setCommand(command);
    process->start();
}

QList<Interpreter> PythonSettings::detectPythonVenvs(const FilePath &path)
{
    QList<Interpreter> result;
    QDir dir = path.toFileInfo().isDir() ? QDir(path.toUrlishString()) : path.toFileInfo().dir();
    if (dir.exists()) {
        const QString venvPython = HostOsInfo::withExecutableSuffix("python");
        const QString activatePath = HostOsInfo::isWindowsHost() ? QString{"Scripts"}
                                                                 : QString{"bin"};
        do {
            for (const QString &directory : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (dir.cd(directory)) {
                    if (dir.cd(activatePath)) {
                        if (dir.exists("activate") && dir.exists(venvPython)) {
                            FilePath python = FilePath::fromString(dir.absoluteFilePath(venvPython));
                            dir.cdUp();
                            const QString defaultName = QString("Python (%1 Virtual Environment)")
                                                            .arg(dir.dirName());
                            Interpreter interpreter
                                = Utils::findOrDefault(PythonSettings::interpreters(),
                                                       Utils::equal(&Interpreter::command, python));
                            if (interpreter.command.isEmpty()) {
                                interpreter = createInterpreter(python, defaultName);
                                PythonSettings::addInterpreter(interpreter);
                            }
                            result << interpreter;
                        } else {
                            dir.cdUp();
                        }
                    }
                    dir.cdUp();
                }
            }
        } while (dir.cdUp() && !(dir.isRoot() && Utils::HostOsInfo::isAnyUnixHost()));
    }
    return result;
}

void PythonSettings::initFromSettings(QtcSettings *settings)
{
    settings->beginGroup(settingsGroupKey);
    const QVariantList interpreterList = settings->value(interpreterKey).toList();
    for (const QVariant &interpreterVariant : interpreterList) {
        if (isStore(interpreterVariant)) {
            Interpreter interpreter;
            interpreter.fromMap(Utils::storeFromVariant(interpreterVariant));
            Utils::erase(m_interpreters, Utils::equal(&Interpreter::id, interpreter.id));
            m_interpreters << interpreter;
            continue;
        }

        auto interpreterMembers = interpreterVariant.toList();
        if (interpreterMembers.size() <= 3)
            continue; // old settings, skip

        const auto id = interpreterMembers.value(0).toString();
        if (Utils::contains(m_interpreters, Utils::equal(&Interpreter::id, id)))
            continue; // already exists

        const bool isAutoDetected = interpreterMembers.value(3, true).toBool();
        const QString detectionSourceId = interpreterMembers.value(4, QString()).toString();
        DetectionSource detectionSource{
            isAutoDetected ? DetectionSource::FromSystem : DetectionSource::Manual,
            detectionSourceId};

        m_interpreters << Interpreter{
            id,
            interpreterMembers.value(1).toString(),
            FilePath::fromSettings(interpreterMembers.value(2)),
            detectionSource};
    }

    const auto keepInterpreter = [](const Interpreter &interpreter) {
        return !interpreter.detectionSource.isAutoDetected() // always keep user added interpreters
               || !interpreter.command.isLocal() // remote devices might not be reachable at startup
               || interpreter.command.isExecutableFile();
    };

    const auto [valid, outdatedInterpreters] = Utils::partition(m_interpreters, keepInterpreter);
    m_interpreters = valid;

    const bool kitsGenerated = settings->value(kitsGeneratedKey, false).toBool();
    if (kitsGenerated)
        fixupPythonKits();
    for (const Interpreter &interpreter : std::as_const(m_interpreters)) {
        if (!kitsGenerated) {
            if (interpreter.detectionSource.isAutoDetected()) {
                const FilePath &cmd = interpreter.command;
                if (!cmd.isLocal() || cmd.parentDir().pathAppended("activate").exists())
                    continue;
            }
            addKitsForInterpreter(interpreter, false);
        }
    }

    for (const Interpreter &outdated : outdatedInterpreters)
        removeKitsForInterpreter(outdated);

    m_defaultInterpreterId = settings->value(defaultKey).toString();

    // OPENMV-DIFF //
    // QVariant pylsEnabled = settings->value(pylsEnabledKey);
    // OPENMV-DIFF //
    QVariant pylsEnabled = true;
    // OPENMV-DIFF //
    if (pylsEnabled.isNull())
        disableOutdatedPyls();
    else
        m_pylsEnabled = pylsEnabled.toBool();
    const QVariant pylsConfiguration = settings->value(pylsConfigurationKey);
    if (!pylsConfiguration.isNull())
        m_pylsConfiguration = pylsConfiguration.toString();
    else
        m_pylsConfiguration = defaultPylsConfiguration();
    settings->endGroup();
}

void PythonSettings::writeToSettings(QtcSettings *settings)
{
    settings->beginGroup(settingsGroupKey);
    QVariantList interpretersList;
    for (const Interpreter &interpreter : std::as_const(m_interpreters)) {
        if (interpreter.detectionSource.isTemporary())
            continue;

        QVariantList members{
            interpreter.id,
            interpreter.name,
            interpreter.command.toSettings(),
            interpreter.detectionSource.isAutoDetected()};
        // We need to cast to QVariant() here, otherwise interpretersList will simply append each
        // member as a separate item.
        interpretersList.append(QVariant(members)); // old settings
        Store newSettings;
        interpreter.toMap(newSettings);
        interpretersList.append(variantFromStore(newSettings)); // new settings
    }
    settings->setValue(interpreterKey, interpretersList);
    settings->setValue(defaultKey, m_defaultInterpreterId);

    settings->setValueWithDefault(pylsConfigurationKey,
                                  m_pylsConfiguration,
                                  defaultPylsConfiguration());

    settings->setValue(pylsEnabledKey, m_pylsEnabled);
    settings->setValue(kitsGeneratedKey, true);
    settings->endGroup();
}

QString PythonSettings::defaultInterpreterId()
{
    return settingsInstance->m_defaultInterpreterId;
}

std::optional<ExecutableItem> PythonSettings::autoDetect(
    Kit *kit,
    const Utils::FilePaths &searchPaths,
    const DetectionSource &detectionSource,
    const LogCallback &logCallback)
{
    Q_UNUSED(kit);

    const auto setupSearch = [searchPaths, detectionSource](Async<Interpreter> &task) {
        const QList<Interpreter> alreadyConfigured = PythonSettings::interpreters();

        task.setConcurrentCallData(
            [](QPromise<Interpreter> &promise,
               const FilePaths &searchPaths,
               const QList<Interpreter> &alreadyConfigured,
               const DetectionSource &detectionSource) {
                for (const FilePath &path : searchPaths) {
                    const FilePath python = path.pathAppended("python3").withExecutableSuffix();
                    if (!python.isExecutableFile())
                        continue;
                    if (Utils::contains(
                            alreadyConfigured, Utils::equal(&Interpreter::command, python)))
                        continue;

                    Interpreter interpreter = PythonSettings::createInterpreter(
                        python, {}, "(" + python.toUserOutput() + ")", detectionSource);

                    promise.addResult(interpreter);
                }
            },
            searchPaths,
            alreadyConfigured,
            detectionSource);
    };

    const auto searchDone = [detectionSource, logCallback](const Async<Interpreter> &task) {
        for (const auto &interpreter : task.results()) {
            interpreterModel().addInterpreter(interpreter);
            logCallback(
                Tr::tr("Found \"%1\" (%2).")
                    .arg(interpreter.name, interpreter.command.toUserOutput()));
        }
    };

    return AsyncTask<Interpreter>(setupSearch, searchDone);
}

void PythonSettings::removeDetectedPython(
    const QString &detectionSource, const LogCallback &logCallback)
{
    for (Interpreter &interpreter : interpreterModel().interpreterFrom(detectionSource))
        logCallback(Tr::tr("Removing Python: %1.").arg(interpreter.name));

    interpreterModel().removeInterpreterFrom(detectionSource);
}

void PythonSettings::listDetectedPython(
    const QString &detectionSource, const LogCallback &logCallback)
{
    for (Interpreter &interpreter: interpreterModel().interpreterFrom(detectionSource))
        logCallback(Tr::tr("Python: %1.").arg(interpreter.name));
}

void PythonSettings::fixupPythonKits()
{
    if (!KitManager::isLoaded()) {
        connect(KitManager::instance(),
                &KitManager::kitsLoaded,
                settingsInstance,
                &PythonSettings::fixupPythonKits,
                Qt::UniqueConnection);
        return;
    }
    for (const Interpreter &interpreter : std::as_const(m_interpreters)) {
        if (auto k = KitManager::kit(Id::fromString(interpreter.id)))
            setRelevantAspectsToKit(k);
    }
}

void PythonSettings::saveSettings()
{
    QTC_ASSERT(settingsInstance, return);
    settingsInstance->writeToSettings(Core::ICore::settings());
    emit settingsInstance->interpretersChanged(settingsInstance->m_interpreters,
                                               settingsInstance->m_defaultInterpreterId);
}

QList<Interpreter> PythonSettings::interpreters()
{
    return settingsInstance->m_interpreters;
}

Interpreter PythonSettings::defaultInterpreter()
{
    return interpreter(settingsInstance->m_defaultInterpreterId);
}

Interpreter PythonSettings::interpreter(const QString &interpreterId)
{
    return Utils::findOrDefault(settingsInstance->m_interpreters,
                                Utils::equal(&Interpreter::id, interpreterId));
}

void setupPythonSettings()
{
    static GuardedObject thePythonSettings{new PythonSettings};
}

// InterpreterOptionsPage

class InterpreterOptionsPage : public Core::IOptionsPage
{
public:
    InterpreterOptionsPage()
    // OPENMV-DIFF //
    // OpenMV hides the Interpreters page (bundled interpreter is forced), so the page
    // is constructed with IOptionsPage(false). Upstream moved this class to the bottom
    // of the file and switched keywords() -> setFixedKeywords(); both are kept.
    : Core::IOptionsPage(false)
    // OPENMV-DIFF //
    {
        setId(Constants::C_PYTHONOPTIONS_PAGE_ID);
        setDisplayName(Tr::tr("Interpreters"));
        setCategory(Constants::C_PYTHON_SETTINGS_CATEGORY);
        setWidgetCreator([] { return new InterpreterOptionsWidget; });
        setFixedKeywords({
            Tr::tr("Name:"),
            Tr::tr("Executable:"),
            Tr::tr("&Add"),
            Tr::tr("&Delete"),
            Tr::tr("&Clean Up"),
            Tr::tr("&Make Default")
        });
    }
};

static InterpreterOptionsPage page;

} // Python::Internal

#include "pythonsettings.moc"
