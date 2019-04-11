#include "openmvplugin.h"

#include "app/app_version.h"

namespace OpenMV {
namespace Internal {

OpenMVPlugin::OpenMVPlugin() : IPlugin()
{
    qRegisterMetaType<OpenMVPluginSerialPortCommand>("OpenMVPluginSerialPortCommand");
    qRegisterMetaType<OpenMVPluginSerialPortCommandResult>("OpenMVPluginSerialPortCommandResult");

    m_ioport = Q_NULLPTR;
    m_iodevice = Q_NULLPTR;

    m_frameSizeDumpTimer.start();
    m_getScriptRunningTimer.start();
    m_getTxBufferTimer.start();

    m_timer.start();
    m_queue = QQueue<qint64>();

    m_working = false;
    m_connected = false;
    m_running = false;
    m_major = int();
    m_minor = int();
    m_patch = int();
    m_reconnects = int();
    m_portName = QString();
    m_portPath = QString();

    m_errorFilterRegex = QRegularExpression(QStringLiteral(
        "  File \"(.+?)\", line (\\d+).*?\n"
        "(?!Exception: IDE interrupt)(.+?:.+?)\n"));
    m_errorFilterString = QString();

    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &OpenMVPlugin::processEvents);
    timer->start(1);
}

static void noShow()
{
    q_check_ptr(qobject_cast<Core::Internal::MainWindow *>(Core::ICore::mainWindow()))->disableShow(true);
}

static bool isNoShow()
{
    return q_check_ptr(qobject_cast<Core::Internal::MainWindow *>(Core::ICore::mainWindow()))->isShowDisabled();
}

static void displayError(const QString &string)
{
    if(Utils::HostOsInfo::isWindowsHost())
    {
        QMessageBox::critical(Q_NULLPTR, QString(), string);
    }
    else
    {
        qCritical("%s", qPrintable(string));
    }
}

bool OpenMVPlugin::initialize(const QStringList &arguments, QString *errorMessage)
{
    Q_UNUSED(errorMessage)

    if(arguments.contains(QStringLiteral("-open_serial_terminal"))
    || arguments.contains(QStringLiteral("-open_udp_client_terminal"))
    || arguments.contains(QStringLiteral("-open_udp_server_terminal"))
    || arguments.contains(QStringLiteral("-open_tcp_client_terminal"))
    || arguments.contains(QStringLiteral("-open_tcp_server_terminal")))
    {
        noShow();
    }

    ///////////////////////////////////////////////////////////////////////////

    int override_read_timeout = -1;
    int index_override_read_timeout = arguments.indexOf(QRegularExpression(QStringLiteral("-override_read_timeout")));

    if(index_override_read_timeout != -1)
    {
        if(arguments.size() > (index_override_read_timeout + 1))
        {
            bool ok;
            int tmp_override_read_timeout = arguments.at(index_override_read_timeout + 1).toInt(&ok);

            if(ok)
            {
                override_read_timeout = tmp_override_read_timeout;
            }
            else
            {
                displayError(tr("Invalid argument (%1) for -override_read_timeout").arg(arguments.at(index_override_read_timeout + 1)));
                exit(-1);
            }
        }
        else
        {
            displayError(tr("Missing argument for -override_read_timeout"));
            exit(-1);
        }
    }

    int override_read_stall_timeout = -1;
    int index_override_read_stall_timeout = arguments.indexOf(QRegularExpression(QStringLiteral("-override_read_stall_timeout")));

    if(index_override_read_stall_timeout != -1)
    {
        if(arguments.size() > (index_override_read_stall_timeout + 1))
        {
            bool ok;
            int tmp_override_read_stall_timeout = arguments.at(index_override_read_stall_timeout + 1).toInt(&ok);

            if(ok)
            {
                override_read_stall_timeout = tmp_override_read_stall_timeout;
            }
            else
            {
                displayError(tr("Invalid argument (%1) for -override_read_stall_timeout").arg(arguments.at(index_override_read_stall_timeout + 1)));
                exit(-1);
            }
        }
        else
        {
            displayError(tr("Missing argument for -override_read_stall_timeout"));
            exit(-1);
        }
    }

    m_ioport = new OpenMVPluginSerialPort(override_read_timeout, override_read_stall_timeout, this);
    m_iodevice = new OpenMVPluginIO(m_ioport, this);

    ///////////////////////////////////////////////////////////////////////////

    QSplashScreen *splashScreen = new QSplashScreen(QPixmap(QStringLiteral(SPLASH_PATH)));

    connect(Core::ICore::instance(), &Core::ICore::coreOpened,
            splashScreen, &QSplashScreen::deleteLater);

    if(!isNoShow()) splashScreen->show();

    ///////////////////////////////////////////////////////////////////////////

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    int major = settings->value(QStringLiteral(RESOURCES_MAJOR), 0).toInt();
    int minor = settings->value(QStringLiteral(RESOURCES_MINOR), 0).toInt();
    int patch = settings->value(QStringLiteral(RESOURCES_PATCH), 0).toInt();

    if((arguments.contains(QStringLiteral("-update_resources")))
    || (major < OMV_IDE_VERSION_MAJOR)
    || ((major == OMV_IDE_VERSION_MAJOR) && (minor < OMV_IDE_VERSION_MINOR))
    || ((major == OMV_IDE_VERSION_MAJOR) && (minor == OMV_IDE_VERSION_MINOR) && (patch < OMV_IDE_VERSION_RELEASE)))
    {
        settings->setValue(QStringLiteral(RESOURCES_MAJOR), 0);
        settings->setValue(QStringLiteral(RESOURCES_MINOR), 0);
        settings->setValue(QStringLiteral(RESOURCES_PATCH), 0);
        settings->sync();

        bool ok = true;

        QString error;

        if(!Utils::FileUtils::removeRecursively(Utils::FileName::fromString(Core::ICore::userResourcePath()), &error))
        {
            QMessageBox::critical(Q_NULLPTR, QString(), tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
            ok = false;
        }
        else
        {
            QStringList list = QStringList() << QStringLiteral("examples") << QStringLiteral("firmware") << QStringLiteral("html") << QStringLiteral("models");

            foreach(const QString &dir, list)
            {
                QString error;

                if(!Utils::FileUtils::copyRecursively(Utils::FileName::fromString(Core::ICore::resourcePath() + QLatin1Char('/') + dir),
                                                      Utils::FileName::fromString(Core::ICore::userResourcePath() + QLatin1Char('/') + dir),
                                                      &error))
                {
                    QMessageBox::critical(Q_NULLPTR, QString(), tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                    ok = false;
                    break;
                }
            }
        }

        if(ok)
        {
            settings->setValue(QStringLiteral(RESOURCES_MAJOR), OMV_IDE_VERSION_MAJOR);
            settings->setValue(QStringLiteral(RESOURCES_MINOR), OMV_IDE_VERSION_MINOR);
            settings->setValue(QStringLiteral(RESOURCES_PATCH), OMV_IDE_VERSION_RELEASE);
            settings->sync();
        }
        else
        {
            settings->endGroup();

            exit(-1);
        }
    }

    settings->endGroup();

    ///////////////////////////////////////////////////////////////////////////

    QStringList providerVariables;
    QStringList providerFunctions;
    QMap<QString, QStringList> providerFunctionArgs;

    QRegularExpression moduleRegEx(QStringLiteral("<div class=\"section\" id=\"module-(.+?)\">(.*?)<div class=\"section\""), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression spanRegEx(QStringLiteral("<span.*?>"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression linkRegEx(QStringLiteral("<a.*?>"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression classRegEx(QStringLiteral(" class=\".*?\""), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression cdfmRegEx(QStringLiteral("<dl class=\"(class|data|exception|function|method)\">\\s*<dt id=\"(.+?)\">(.*?)</dt>\\s*<dd>(.*?)</dd>\\s*</dl>"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression argumentRegEx(QStringLiteral("<span class=\"sig-paren\">\\(</span>(.*?)<span class=\"sig-paren\">\\)</span>"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression tupleRegEx(QStringLiteral("\\(.*?\\)"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression listRegEx(QStringLiteral("\\[.*?\\]"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression dictionaryRegEx(QStringLiteral("\\{.*?\\}"), QRegularExpression::DotMatchesEverythingOption);

    QDirIterator it(Core::ICore::userResourcePath() + QStringLiteral("/html/library"), QDir::Files);

    while(it.hasNext())
    {
        QFile file(it.next());

        if(file.open(QIODevice::ReadOnly))
        {
            QString data = QString::fromUtf8(file.readAll());

            if((file.error() == QFile::NoError) && (!data.isEmpty()))
            {
                file.close();

                QRegularExpressionMatch moduleMatch = moduleRegEx.match(data);

                if(moduleMatch.hasMatch())
                {
                    QString name = moduleMatch.captured(1);
                    QString text = moduleMatch.captured(2).
                                   remove(QStringLiteral("\u00B6")).
                                   remove(spanRegEx).
                                   remove(QStringLiteral("</span>")).
                                   remove(linkRegEx).
                                   remove(QStringLiteral("</a>")).
                                   remove(classRegEx).
                                   replace(QStringLiteral("<h1>"), QStringLiteral("<h3>")).
                                   replace(QStringLiteral("</h1>"), QStringLiteral("</h3>"));

                    documentation_t d;
                    d.moduleName = QString();
                    d.className = QString();
                    d.name = name;
                    d.text = text;
                    m_modules.append(d);

                    if(name.startsWith(QLatin1Char('u')))
                    {
                        d.name = name.mid(1);
                        m_modules.append(d);
                    }
                }

                QRegularExpressionMatchIterator matches = cdfmRegEx.globalMatch(data);

                while(matches.hasNext())
                {
                    QRegularExpressionMatch match = matches.next();
                    QString type = match.captured(1);
                    QString id = match.captured(2);
                    QString head = match.captured(3);
                    QString body = match.captured(4);
                    QStringList idList = id.split(QLatin1Char('.'), QString::SkipEmptyParts);

                    if((1 <= idList.size()) && (idList.size() <= 3))
                    {
                        documentation_t d;
                        d.moduleName = (idList.size() > 1) ? idList.at(0) : QString();
                        d.className = (idList.size() > 2) ? idList.at(1) : QString();
                        d.name = idList.last();
                        d.text = QString(QStringLiteral("<h3>%1</h3>%2")).arg(it.fileInfo().completeBaseName() + QStringLiteral(" - ") + head).arg(body).
                                 remove(QStringLiteral("\u00B6")).
                                 remove(spanRegEx).
                                 remove(QStringLiteral("</span>")).
                                 remove(linkRegEx).
                                 remove(QStringLiteral("</a>")).
                                 remove(classRegEx);

                        if(type == QStringLiteral("class"))
                        {
                            m_classes.append(d);
                            providerFunctions.append(d.name);
                        }
                        else if((type == QStringLiteral("data")) || (type == QStringLiteral("exception")))
                        {
                            m_datas.append(d);
                            providerVariables.append(d.name);
                        }
                        else if(type == QStringLiteral("function"))
                        {
                            m_functions.append(d);
                            providerFunctions.append(d.name);
                        }
                        else if(type == QStringLiteral("method"))
                        {
                            m_methods.append(d);
                            providerFunctions.append(d.name);
                        }

                        QRegularExpressionMatch args = argumentRegEx.match(head);

                        if(args.hasMatch())
                        {
                            QStringList list;

                            foreach(const QString &arg, args.captured(1).
                                                        remove(QLatin1String("<span class=\"optional\">[</span>")).
                                                        remove(QLatin1String("<span class=\"optional\">]</span>")).
                                                        remove(QLatin1String("<em>")).
                                                        remove(QLatin1String("</em>")).
                                                        remove(tupleRegEx).
                                                        remove(listRegEx).
                                                        remove(dictionaryRegEx).
                                                        remove(QLatin1Char(' ')).
                                                        split(QLatin1Char(','), QString::SkipEmptyParts))
                            {
                                int equals = arg.indexOf(QLatin1Char('='));
                                QString temp = (equals != -1) ? arg.left(equals) : arg;

                                m_arguments.insert(temp);
                                list.append(temp);
                            }

                            providerFunctionArgs.insert(d.name, list);
                        }
                    }
                }
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////

    connect(TextEditor::Internal::Manager::instance(), &TextEditor::Internal::Manager::highlightingFilesRegistered, this, [this] {
        QString id = TextEditor::Internal::Manager::instance()->definitionIdByName(QStringLiteral("Python"));

        if(!id.isEmpty())
        {
            QSharedPointer<TextEditor::Internal::HighlightDefinition> def = TextEditor::Internal::Manager::instance()->definition(id);

            if(def)
            {
                QSharedPointer<TextEditor::Internal::KeywordList> modulesList = def->keywordList(QStringLiteral("listOpenMVModules"));
                QSharedPointer<TextEditor::Internal::KeywordList> classesList = def->keywordList(QStringLiteral("listOpenMVClasses"));
                QSharedPointer<TextEditor::Internal::KeywordList> datasList = def->keywordList(QStringLiteral("listOpenMVDatas"));
                QSharedPointer<TextEditor::Internal::KeywordList> functionsList = def->keywordList(QStringLiteral("listOpenMVFunctions"));
                QSharedPointer<TextEditor::Internal::KeywordList> methodsList = def->keywordList(QStringLiteral("listOpenMVMethods"));
                QSharedPointer<TextEditor::Internal::KeywordList> argumentsList = def->keywordList(QStringLiteral("listOpenMVArguments"));

                if(modulesList)
                {
                    foreach(const documentation_t &d, m_modules)
                    {
                        modulesList->addKeyword(d.name);
                    }
                }

                if(classesList)
                {
                    foreach(const documentation_t &d, m_classes)
                    {
                        classesList->addKeyword(d.name);
                    }
                }

                if(datasList)
                {
                    foreach(const documentation_t &d, m_datas)
                    {
                        datasList->addKeyword(d.name);
                    }
                }

                if(functionsList)
                {
                    foreach(const documentation_t &d, m_functions)
                    {
                        functionsList->addKeyword(d.name);
                    }
                }

                if(methodsList)
                {
                    foreach(const documentation_t &d, m_methods)
                    {
                        methodsList->addKeyword(d.name);
                    }
                }

                if(argumentsList)
                {
                    foreach(const QString &d, m_arguments.values())
                    {
                        argumentsList->addKeyword(d);
                    }
                }
            }
        }
    });

    ///////////////////////////////////////////////////////////////////////////

    OpenMVPluginCompletionAssistProvider *provider = new OpenMVPluginCompletionAssistProvider(providerVariables, providerFunctions, providerFunctionArgs);
    provider->setParent(this);

    connect(Core::EditorManager::instance(), &Core::EditorManager::editorCreated, this, [this, provider] (Core::IEditor *editor, const QString &fileName) {
        TextEditor::BaseTextEditor *textEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor);

        if(textEditor && fileName.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
        {
            textEditor->textDocument()->setCompletionAssistProvider(provider);
            connect(textEditor->editorWidget(), &TextEditor::TextEditorWidget::tooltipOverrideRequested, this, [this] (TextEditor::TextEditorWidget *widget, const QPoint &globalPos, int position, bool *handled) {

                if(handled)
                {
                    *handled = true;
                }

                QTextCursor cursor(widget->textDocument()->document());
                cursor.setPosition(position);
                cursor.movePosition(QTextCursor::Start, QTextCursor::KeepAnchor);
                QString text = cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));

                if(!text.isEmpty())
                {
                    enum
                    {
                        IN_NONE,
                        IN_COMMENT,
                        IN_STRING_0,
                        IN_STRING_1
                    }
                    in_state = IN_NONE;

                    for(int i = 0; i < text.size(); i++)
                    {
                        switch(in_state)
                        {
                            case IN_NONE:
                            {
                                if((text.at(i) == QLatin1Char('#')) && ((!i) || (text.at(i-1) != QLatin1Char('\\')))) in_state = IN_COMMENT;
                                if((text.at(i) == QLatin1Char('\'')) && ((!i) || (text.at(i-1) != QLatin1Char('\\')))) in_state = IN_STRING_0;
                                if((text.at(i) == QLatin1Char('\"')) && ((!i) || (text.at(i-1) != QLatin1Char('\\')))) in_state = IN_STRING_1;
                                break;
                            }
                            case IN_COMMENT:
                            {
                                if((text.at(i) == QLatin1Char('\n')) && (text.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                                break;
                            }
                            case IN_STRING_0:
                            {
                                if((text.at(i) == QLatin1Char('\'')) && (text.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                                break;
                            }
                            case IN_STRING_1:
                            {
                                if((text.at(i) == QLatin1Char('\"')) && (text.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                                break;
                            }
                        }
                    }

                    if(in_state == IN_NONE)
                    {
                        cursor.setPosition(position);
                        cursor.select(QTextCursor::WordUnderCursor);
                        text = cursor.selectedText();

                        if(!text.isEmpty())
                        {
                            QStringList list;

                            foreach(const documentation_t &d, m_modules)
                            {
                                if(d.name == text)
                                {
                                    list.append(d.text);
                                }
                            }

                            if(qMin(cursor.position(), cursor.anchor()) && (widget->textDocument()->document()->characterAt(qMin(cursor.position(), cursor.anchor()) - 1) == QLatin1Char('.')))
                            {
                                foreach(const documentation_t &d, m_datas)
                                {
                                    if(d.name == text)
                                    {
                                        list.append(d.text);
                                    }
                                }
                            }

                            if(widget->textDocument()->document()->characterAt(qMax(cursor.position(), cursor.anchor())) == QLatin1Char('('))
                            {
                                foreach(const documentation_t &d, m_classes)
                                {
                                    if(d.name == text)
                                    {
                                        list.append(d.text);
                                    }
                                }

                                foreach(const documentation_t &d, m_functions)
                                {
                                    if(d.name == text)
                                    {
                                        list.append(d.text);
                                    }
                                }

                                foreach(const documentation_t &d, m_methods)
                                {
                                    if(d.name == text)
                                    {
                                        list.append(d.text);
                                    }
                                }
                            }

                            if(!list.isEmpty())
                            {
                                QString string;
                                int i = 0;

                                for(int j = 0, k = qCeil(qSqrt(list.size())); j < k; j++)
                                {
                                    string.append(QStringLiteral("<tr>"));

                                    for(int l = 0; l < k; l++)
                                    {
                                        string.append(QStringLiteral("<td style=\"padding:6px;\">") + list.at(i++) + QStringLiteral("</td>"));

                                        if(i >= list.size())
                                        {
                                            break;
                                        }
                                    }

                                    string.append(QStringLiteral("</tr>"));

                                    if(i >= list.size())
                                    {
                                        break;
                                    }
                                }

                                Utils::ToolTip::show(globalPos, QStringLiteral("<table>") + string + QStringLiteral("</table>"), widget);
                                return;
                            }
                        }
                    }
                }

                Utils::ToolTip::hide();
            });

            connect(textEditor->editorWidget(), &TextEditor::TextEditorWidget::contextMenuEventCB, this, [this, textEditor] (QMenu *menu, QString text) {

                QRegularExpressionMatch grayscaleMatch = QRegularExpression(QStringLiteral("^\\s*\\(\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*\\)\\s*$")).match(text);

                if(grayscaleMatch.hasMatch())
                {
                    menu->addSeparator();
                    QAction *action = new QAction(tr("Edit Grayscale threshold with Threshold Editor"), menu);
                    connect(action, &QAction::triggered, this, [this, textEditor, grayscaleMatch] {
                        QList<int> list = openThresholdEditor(QList<QVariant>()
                            << grayscaleMatch.captured(1).toInt()
                            << grayscaleMatch.captured(2).toInt()
                        );

                        if(!list.isEmpty())
                        {
                            textEditor->textCursor().removeSelectedText();
                            textEditor->textCursor().insertText(QString(QStringLiteral("(%1, %2)")).arg(list.at(0), 3) // can't use takeFirst() here
                                                                                                   .arg(list.at(1), 3)); // can't use takeFirst() here
                        }
                    });

                    menu->addAction(action);
                }

                QRegularExpressionMatch labMatch = QRegularExpression(QStringLiteral("^\\s*\\(\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*\\)\\s*$")).match(text);

                if(labMatch.hasMatch())
                {
                    menu->addSeparator();
                    QAction *action = new QAction(tr("Edit LAB threshold with Threshold Editor"), menu);
                    connect(action, &QAction::triggered, this, [this, textEditor, labMatch] {
                        QList<int> list = openThresholdEditor(QList<QVariant>()
                            << labMatch.captured(1).toInt()
                            << labMatch.captured(2).toInt()
                            << labMatch.captured(3).toInt()
                            << labMatch.captured(4).toInt()
                            << labMatch.captured(5).toInt()
                            << labMatch.captured(6).toInt()
                        );

                        if(!list.isEmpty())
                        {
                            textEditor->textCursor().removeSelectedText();
                            textEditor->textCursor().insertText(QString(QStringLiteral("(%1, %2, %3, %4, %5, %6)")).arg(list.at(2), 3) // can't use takeFirst() here
                                                                                                                   .arg(list.at(3), 3) // can't use takeFirst() here
                                                                                                                   .arg(list.at(4), 4) // can't use takeFirst() here
                                                                                                                   .arg(list.at(5), 4) // can't use takeFirst() here
                                                                                                                   .arg(list.at(6), 4) // can't use takeFirst() here
                                                                                                                   .arg(list.at(7), 4)); // can't use takeFirst() here
                        }
                    });

                    menu->addAction(action);
                }
            });
        }
    });

    ///////////////////////////////////////////////////////////////////////////

    qRegisterMetaType<importDataList_t>("importDataList_t");

    // Scan examples.
    {
        QThread *thread = new QThread;
        LoadFolderThread *loadFolderThread = new LoadFolderThread(Core::ICore::userResourcePath() + QStringLiteral("/examples"));
        loadFolderThread->moveToThread(thread);
        QTimer *timer = new QTimer(this);

        connect(timer, &QTimer::timeout,
                loadFolderThread, &LoadFolderThread::loadFolderSlot);

        connect(loadFolderThread, &LoadFolderThread::folderLoaded, this, [this] (const importDataList_t &output) {
            m_exampleModules = output;
        });

        connect(this, &OpenMVPluginSerialPort::destroyed,
                loadFolderThread, &OpenMVPluginSerialPort_private::deleteLater);

        connect(loadFolderThread, &OpenMVPluginSerialPort_private::destroyed,
                thread, &QThread::quit);

        connect(thread, &QThread::finished,
                thread, &QThread::deleteLater);

        thread->start();
        timer->start(FOLDER_SCAN_TIME);
        QTimer::singleShot(0, loadFolderThread, &LoadFolderThread::loadFolderSlot);
    }

    // Scan documents folder.
    {
        QThread *thread = new QThread;
        LoadFolderThread *loadFolderThread = new LoadFolderThread(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV"));
        loadFolderThread->moveToThread(thread);
        QTimer *timer = new QTimer(this);

        connect(timer, &QTimer::timeout,
                loadFolderThread, &LoadFolderThread::loadFolderSlot);

        connect(loadFolderThread, &LoadFolderThread::folderLoaded, this, [this] (const importDataList_t &output) {
            m_documentsModules = output;
        });

        connect(this, &OpenMVPluginSerialPort::destroyed,
                loadFolderThread, &OpenMVPluginSerialPort_private::deleteLater);

        connect(loadFolderThread, &OpenMVPluginSerialPort_private::destroyed,
                thread, &QThread::quit);

        connect(thread, &QThread::finished,
                thread, &QThread::deleteLater);

        thread->start();
        timer->start(FOLDER_SCAN_TIME);
        QTimer::singleShot(0, loadFolderThread, &LoadFolderThread::loadFolderSlot);
    }

    return true;
}

void OpenMVPlugin::extensionsInitialized()
{
    QApplication::setApplicationDisplayName(tr("OpenMV IDE"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(ICON_PATH)));

    ///////////////////////////////////////////////////////////////////////////

    connect(Core::ActionManager::command(Core::Constants::NEW)->action(), &QAction::triggered, this, [this] {
        Core::EditorManager::cutForwardNavigationHistory();
        Core::EditorManager::addCurrentPositionToNavigationHistory();
        QString titlePattern = tr("untitled_$.py");
        TextEditor::BaseTextEditor *editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::openEditorWithContents(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID, &titlePattern,
            QStringLiteral("# Untitled - By: %L1 - %L2\n"
                           "\n"
                           "import sensor, image, time\n"
                           "\n"
                           "sensor.reset()\n"
                           "sensor.set_pixformat(sensor.RGB565)\n"
                           "sensor.set_framesize(sensor.QVGA)\n"
                           "sensor.skip_frames(time = 2000)\n"
                           "\n"
                           "clock = time.clock()\n"
                           "\n"
                           "while(True):\n"
                           "    clock.tick()\n"
                           "    img = sensor.snapshot()\n"
                           "    print(clock.fps())\n").
            arg(Utils::Environment::systemEnvironment().userName()).arg(QDate::currentDate().toString()).toUtf8()));

        if(editor)
        {
            Core::EditorManager::addCurrentPositionToNavigationHistory();
            editor->editorWidget()->configureGenericHighlighter();
            Core::EditorManager::activateEditor(editor);
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                QObject::tr("New File"),
                QObject::tr("Can't open the new file!"));
        }
    });

    Core::ActionContainer *filesMenu = Core::ActionManager::actionContainer(Core::Constants::M_FILE);

    Core::ActionContainer *documentsFolder = Core::ActionManager::createMenu(Core::Id("OpenMV.DocumentsFolder"));
    filesMenu->addMenu(Core::ActionManager::actionContainer(Core::Constants::M_FILE_RECENTFILES), documentsFolder, Core::Constants::G_FILE_OPEN);
    documentsFolder->menu()->setTitle(tr("Documents Folder"));
    documentsFolder->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    connect(filesMenu->menu(), &QMenu::aboutToShow, this, [this, documentsFolder] {
        documentsFolder->menu()->clear();
        QMap<QString, QAction *> actions = aboutToShowExamplesRecursive(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV"), documentsFolder->menu(), true);

        if(actions.isEmpty())
        {
            QAction *action = new QAction(tr("Add some code to \"%L1\"").arg(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV")), documentsFolder->menu());
            action->setDisabled(true);
            documentsFolder->menu()->addAction(action);
        }
        else
        {
            documentsFolder->menu()->addActions(actions.values());
        }
    });

    Core::ActionContainer *examplesMenu = Core::ActionManager::createMenu(Core::Id("OpenMV.Examples"));
    filesMenu->addMenu(Core::ActionManager::actionContainer(Core::Constants::M_FILE_RECENTFILES), examplesMenu, Core::Constants::G_FILE_OPEN);
    examplesMenu->menu()->setTitle(tr("Examples"));
    examplesMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    connect(filesMenu->menu(), &QMenu::aboutToShow, this, [this, examplesMenu] {
        examplesMenu->menu()->clear();
        QMap<QString, QAction *> actions = aboutToShowExamplesRecursive(Core::ICore::userResourcePath() + QStringLiteral("/examples"), examplesMenu->menu());
        examplesMenu->menu()->addActions(actions.values());
        examplesMenu->menu()->setDisabled(actions.values().isEmpty());
    });

    Core::ActionContainer *toolsMenu = Core::ActionManager::actionContainer(Core::Constants::M_TOOLS);
    Core::ActionContainer *helpMenu = Core::ActionManager::actionContainer(Core::Constants::M_HELP);

    QAction *bootloaderCommand = new QAction(tr("Run Bootloader"), this);
    m_bootloaderCommand = Core::ActionManager::registerAction(bootloaderCommand, Core::Id("OpenMV.Bootloader"));
    toolsMenu->addAction(m_bootloaderCommand);
    connect(bootloaderCommand, &QAction::triggered, this, &OpenMVPlugin::bootloaderClicked);
    toolsMenu->addSeparator();

    QAction *configureSettingsCommand = new QAction(tr("Configure OpenMV Cam settings file"), this);
    m_configureSettingsCommand = Core::ActionManager::registerAction(configureSettingsCommand, Core::Id("OpenMV.Settings"));
    toolsMenu->addAction(m_configureSettingsCommand);
    configureSettingsCommand->setEnabled(false);
    connect(configureSettingsCommand, &QAction::triggered, this, &OpenMVPlugin::configureSettings);

    QAction *saveCommand = new QAction(tr("Save open script to OpenMV Cam"), this);
    m_saveCommand = Core::ActionManager::registerAction(saveCommand, Core::Id("OpenMV.Save"));
    toolsMenu->addAction(m_saveCommand);
    saveCommand->setEnabled(false);
    connect(saveCommand, &QAction::triggered, this, &OpenMVPlugin::saveScript);

    QAction *resetCommand = new QAction(tr("Reset OpenMV Cam"), this);
    m_resetCommand = Core::ActionManager::registerAction(resetCommand, Core::Id("OpenMV.Reset"));
    toolsMenu->addAction(m_resetCommand);
    resetCommand->setEnabled(false);
    connect(resetCommand, &QAction::triggered, this, [this] {disconnectClicked(true);});

    toolsMenu->addSeparator();
    m_openTerminalMenu = Core::ActionManager::createMenu(Core::Id("OpenMV.OpenTermnial"));
    m_openTerminalMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    m_openTerminalMenu->menu()->setTitle(tr("Open Terminal"));
    toolsMenu->addMenu(m_openTerminalMenu);
    connect(m_openTerminalMenu->menu(), &QMenu::aboutToShow, this, &OpenMVPlugin::openTerminalAboutToShow);

    Core::ActionContainer *machineVisionToolsMenu = Core::ActionManager::createMenu(Core::Id("OpenMV.MachineVision"));
    machineVisionToolsMenu->menu()->setTitle(tr("Machine Vision"));
    machineVisionToolsMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    toolsMenu->addMenu(machineVisionToolsMenu);

    QAction *thresholdEditorAction = new QAction(tr("Threshold Editor"), this);
    Core::Command *thresholdEditorCommand = Core::ActionManager::registerAction(thresholdEditorAction, Core::Id("OpenMV.ThresholdEditor"));
    machineVisionToolsMenu->addAction(thresholdEditorCommand);
    connect(thresholdEditorAction, &QAction::triggered, this, &OpenMVPlugin::openThresholdEditor);

    QAction *keypointsEditorAction = new QAction(tr("Keypoints Editor"), this);
    Core::Command *keypointsEditorCommand = Core::ActionManager::registerAction(keypointsEditorAction, Core::Id("OpenMV.KeypointsEditor"));
    machineVisionToolsMenu->addAction(keypointsEditorCommand);
    connect(keypointsEditorAction, &QAction::triggered, this, &OpenMVPlugin::openKeypointsEditor);

    machineVisionToolsMenu->addSeparator();
    Core::ActionContainer *aprilTagGeneratorSubmenu = Core::ActionManager::createMenu(Core::Id("OpenMV.AprilTagGenerator"));
    aprilTagGeneratorSubmenu->menu()->setTitle(tr("AprilTag Generator"));
    machineVisionToolsMenu->addMenu(aprilTagGeneratorSubmenu);

    QAction *tag16h5Action = new QAction(tr("TAG16H5 Family (30 Tags)"), this);
    Core::Command *tag16h5Command = Core::ActionManager::registerAction(tag16h5Action, Core::Id("OpenMV.TAG16H5"));
    aprilTagGeneratorSubmenu->addAction(tag16h5Command);
    connect(tag16h5Action, &QAction::triggered, this, [this] {openAprilTagGenerator(tag16h5_create());});

    QAction *tag25h7Action = new QAction(tr("TAG25H7 Family (242 Tags)"), this);
    Core::Command *tag25h7Command = Core::ActionManager::registerAction(tag25h7Action, Core::Id("OpenMV.TAG25H7"));
    aprilTagGeneratorSubmenu->addAction(tag25h7Command);
    connect(tag25h7Action, &QAction::triggered, this, [this] {openAprilTagGenerator(tag25h7_create());});

    QAction *tag25h9Action = new QAction(tr("TAG25H9 Family (35 Tags)"), this);
    Core::Command *tag25h9Command = Core::ActionManager::registerAction(tag25h9Action, Core::Id("OpenMV.TAG25H9"));
    aprilTagGeneratorSubmenu->addAction(tag25h9Command);
    connect(tag25h9Action, &QAction::triggered, this, [this] {openAprilTagGenerator(tag25h9_create());});

    QAction *tag36h10Action = new QAction(tr("TAG36H10 Family (2320 Tags)"), this);
    Core::Command *tag36h10Command = Core::ActionManager::registerAction(tag36h10Action, Core::Id("OpenMV.TAG36H10"));
    aprilTagGeneratorSubmenu->addAction(tag36h10Command);
    connect(tag36h10Action, &QAction::triggered, this, [this] {openAprilTagGenerator(tag36h10_create());});

    QAction *tag36h11Action = new QAction(tr("TAG36H11 Family (587 Tags - Recommended)"), this);
    Core::Command *tag36h11Command = Core::ActionManager::registerAction(tag36h11Action, Core::Id("OpenMV.TAG36H11"));
    aprilTagGeneratorSubmenu->addAction(tag36h11Command);
    connect(tag36h11Action, &QAction::triggered, this, [this] {openAprilTagGenerator(tag36h11_create());});

    QAction *tag36artoolkitAction = new QAction(tr("ARKTOOLKIT Family (512 Tags)"), this);
    Core::Command *tag36artoolkitCommand = Core::ActionManager::registerAction(tag36artoolkitAction, Core::Id("OpenMV.ARKTOOLKIT"));
    aprilTagGeneratorSubmenu->addAction(tag36artoolkitCommand);
    connect(tag36artoolkitAction, &QAction::triggered, this, [this] {openAprilTagGenerator(tag36artoolkit_create());});

    QAction *QRCodeGeneratorAction = new QAction(tr("QRCode Generator"), this);
    Core::Command *QRCodeGeneratorCommand = Core::ActionManager::registerAction(QRCodeGeneratorAction, Core::Id("OpenMV.QRCodeGenerator"));
    machineVisionToolsMenu->addAction(QRCodeGeneratorCommand);
    connect(QRCodeGeneratorAction, &QAction::triggered, this, [this] {
        QUrl url = QUrl(QStringLiteral("http://www.google.com/search?q=qr+code+generator"));

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    QAction *DatamatrixGeneratorAction = new QAction(tr("DataMatrix Generator"), this);
    Core::Command *DataMatrixGeneratorCommand = Core::ActionManager::registerAction(DatamatrixGeneratorAction, Core::Id("OpenMV.DataMatrixGenerator"));
    machineVisionToolsMenu->addAction(DataMatrixGeneratorCommand);
    connect(DatamatrixGeneratorAction, &QAction::triggered, this, [this] {
        QUrl url = QUrl(QStringLiteral("http://www.google.com/search?q=data+matrix+generator"));

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    QAction *BarcodeGeneratorAction = new QAction(tr("Barcode Generator"), this);
    Core::Command *BarcodeGeneratorCommand = Core::ActionManager::registerAction(BarcodeGeneratorAction, Core::Id("OpenMV.BarcodeGenerator"));
    machineVisionToolsMenu->addAction(BarcodeGeneratorCommand);
    connect(BarcodeGeneratorAction, &QAction::triggered, this, [this] {
        QUrl url = QUrl(QStringLiteral("http://www.google.com/search?q=barcode+generator"));

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    machineVisionToolsMenu->addSeparator();

    QAction *networkLibraryAction = new QAction(tr("CNN Network Library"), this);
    Core::Command *networkLibraryCommand = Core::ActionManager::registerAction(networkLibraryAction, Core::Id("OpenMV.NetworkLibrary"));
    machineVisionToolsMenu->addAction(networkLibraryCommand);
    connect(networkLibraryAction, &QAction::triggered, this, [this] {
        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QString src =
            QFileDialog::getOpenFileName(Core::ICore::dialogParent(), QObject::tr("Network to copy to OpenMV Cam"),
                Core::ICore::userResourcePath() + QStringLiteral("/models"),
                QObject::tr("Neural Network Model (*.network)"));

        if(!src.isEmpty())
        {
            QString dst =
                QFileDialog::getSaveFileName(Core::ICore::dialogParent(), QObject::tr("Where to save the network on the OpenMV Cam"),
                    m_portPath.isEmpty()
                    ? settings->value(QStringLiteral(LAST_MODEL_NO_CAM_PATH), QDir::homePath()).toString()
                    : settings->value(QStringLiteral(LAST_MODEL_WITH_CAM_PATH), QString(m_portPath + QFileInfo(src).fileName())).toString(),
                    QObject::tr("Neural Network Model (*.network)"));

            if(!dst.isEmpty())
            {
                if((!QFile(dst).exists()) || QFile::remove(dst))
                {
                    if(QFile::copy(src, dst))
                    {
                        settings->setValue(m_portPath.isEmpty() ? QStringLiteral(LAST_MODEL_NO_CAM_PATH) : QStringLiteral(LAST_MODEL_WITH_CAM_PATH), src);
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            QString(),
                            QObject::tr("Unable to overwrite output file!"));
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        QString(),
                        QObject::tr("Unable to overwrite output file!"));
                }
            }
        }

        settings->endGroup();
    });

    Core::ActionContainer *videoToolsMenu = Core::ActionManager::createMenu(Core::Id("OpenMV.VideoTools"));
    videoToolsMenu->menu()->setTitle(tr("Video Tools"));
    videoToolsMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    toolsMenu->addMenu(videoToolsMenu);

    QAction *convertVideoFile = new QAction(tr("Convert Video File"), this);
    Core::Command *convertVideoFileCommand = Core::ActionManager::registerAction(convertVideoFile, Core::Id("OpenMV.ConvertVideoFile"));
    videoToolsMenu->addAction(convertVideoFileCommand);
    connect(convertVideoFile, &QAction::triggered, this, [this] {convertVideoFileAction(m_portPath);});

    QAction *playVideoFile = new QAction(tr("Play Video File"), this);
    Core::Command *playVideoFileCommand = Core::ActionManager::registerAction(playVideoFile, Core::Id("OpenMV.PlayVideoFile"));
    if(!Utils::HostOsInfo::isLinuxHost()) videoToolsMenu->addAction(playVideoFileCommand);
    connect(playVideoFile, &QAction::triggered, this, [this] {playVideoFileAction(m_portPath);});

    QAction *docsAction = new QAction(tr("OpenMV Docs"), this);
    Core::Command *docsCommand = Core::ActionManager::registerAction(docsAction, Core::Id("OpenMV.Docs"));
    helpMenu->addAction(docsCommand, Core::Constants::G_HELP_SUPPORT);
    connect(docsAction, &QAction::triggered, this, [this] {
        QUrl url = QUrl::fromLocalFile(Core::ICore::userResourcePath() + QStringLiteral("/html/index.html"));

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    QAction *forumsAction = new QAction(tr("OpenMV Forums"), this);
    Core::Command *forumsCommand = Core::ActionManager::registerAction(forumsAction, Core::Id("OpenMV.Forums"));
    helpMenu->addAction(forumsCommand, Core::Constants::G_HELP_SUPPORT);
    connect(forumsAction, &QAction::triggered, this, [this] {
        QUrl url = QUrl(QStringLiteral("http://forums.openmv.io/"));

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    QAction *pinoutAction = new QAction(
         Utils::HostOsInfo::isMacHost() ? tr("About OpenMV Cam") : tr("About OpenMV Cam..."), this);
    pinoutAction->setMenuRole(QAction::ApplicationSpecificRole);
    Core::Command *pinoutCommand = Core::ActionManager::registerAction(pinoutAction, Core::Id("OpenMV.Pinout"));
    helpMenu->addAction(pinoutCommand, Core::Constants::G_HELP_ABOUT);
    connect(pinoutAction, &QAction::triggered, this, [this] {
        QUrl url = QUrl::fromLocalFile(Core::ICore::userResourcePath() + QStringLiteral("/html/_images/pinout.png"));

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    QAction *aboutAction = new QAction(QIcon::fromTheme(QStringLiteral("help-about")),
        Utils::HostOsInfo::isMacHost() ? tr("About OpenMV IDE") : tr("About OpenMV IDE..."), this);
    aboutAction->setMenuRole(QAction::AboutRole);
     Core::Command *aboutCommand = Core::ActionManager::registerAction(aboutAction, Core::Id("OpenMV.About"));
    helpMenu->addAction(aboutCommand, Core::Constants::G_HELP_ABOUT);
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(Core::ICore::dialogParent(), tr("About OpenMV IDE"), tr(
        "<p><b>About OpenMV IDE %L1</b></p>"
        "<p>By: Ibrahim Abdelkader & Kwabena W. Agyeman</p>"
        "<p><b>GNU GENERAL PUBLIC LICENSE</b></p>"
        "<p>Copyright (C) %L2 %L3</p>"
        "<p>This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the <a href=\"http://github.com/openmv/qt-creator/raw/master/LICENSE.GPL3-EXCEPT\">GNU General Public License</a> for more details.</p>"
        "<p><b>Questions or Comments?</b></p>"
        "<p>Contact us at <a href=\"mailto:openmv@openmv.io\">openmv@openmv.io</a>.</p>"
        ).arg(QLatin1String(Core::Constants::OMV_IDE_VERSION_LONG)).arg(QLatin1String(Core::Constants::OMV_IDE_YEAR)).arg(QLatin1String(Core::Constants::OMV_IDE_AUTHOR)) + tr(
        "<p><b>Credits</b></p>") + tr(
        "<p>OpenMV IDE English translation by Kwabena W. Agyeman.</p>"));
    });

    ///////////////////////////////////////////////////////////////////////////

    m_connectCommand =
        Core::ActionManager::registerAction(new QAction(QIcon(QStringLiteral(CONNECT_PATH)),
        tr("Connect"), this), Core::Id("OpenMV.Connect"));
    m_connectCommand->setDefaultKeySequence(QStringLiteral("Ctrl+E"));
    m_connectCommand->action()->setEnabled(true);
    m_connectCommand->action()->setVisible(true);
    connect(m_connectCommand->action(), &QAction::triggered, this, [this] {connectClicked();});

    m_disconnectCommand =
        Core::ActionManager::registerAction(new QAction(QIcon(QStringLiteral(DISCONNECT_PATH)),
        tr("Disconnect"), this), Core::Id("OpenMV.Disconnect"));
    m_disconnectCommand->setDefaultKeySequence(QStringLiteral("Ctrl+E"));
    m_disconnectCommand->action()->setEnabled(false);
    m_disconnectCommand->action()->setVisible(false);
    connect(m_disconnectCommand->action(), &QAction::triggered, this, [this] {disconnectClicked();});

    m_startCommand =
        Core::ActionManager::registerAction(new QAction(QIcon(QStringLiteral(START_PATH)),
        tr("Start (run script)"), this), Core::Id("OpenMV.Start"));
    m_startCommand->setDefaultKeySequence(QStringLiteral("Ctrl+R"));
    m_startCommand->action()->setEnabled(false);
    m_startCommand->action()->setVisible(true);
    connect(m_startCommand->action(), &QAction::triggered, this, &OpenMVPlugin::startClicked);
    connect(Core::EditorManager::instance(), &Core::EditorManager::currentEditorChanged, [this] (Core::IEditor *editor) {

        if(m_connected)
        {
            m_configureSettingsCommand->action()->setEnabled(!m_portPath.isEmpty());
            m_saveCommand->action()->setEnabled((!m_portPath.isEmpty()) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));
            m_startCommand->action()->setEnabled((!m_running) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));
            m_startCommand->action()->setVisible(!m_running);
            m_stopCommand->action()->setEnabled(m_running);
            m_stopCommand->action()->setVisible(m_running);
        }
    });

    m_stopCommand =
        Core::ActionManager::registerAction(new QAction(QIcon(QStringLiteral(STOP_PATH)),
        tr("Stop (halt script)"), this), Core::Id("OpenMV.Stop"));
    m_stopCommand->setDefaultKeySequence(QStringLiteral("Ctrl+R"));
    m_stopCommand->action()->setEnabled(false);
    m_stopCommand->action()->setVisible(false);
    connect(m_stopCommand->action(), &QAction::triggered, this, &OpenMVPlugin::stopClicked);
    connect(m_iodevice, &OpenMVPluginIO::scriptRunning, this, [this] (bool running) {

        if(m_connected)
        {
            Core::IEditor *editor = Core::EditorManager::currentEditor();
            m_configureSettingsCommand->action()->setEnabled(!m_portPath.isEmpty());
            m_saveCommand->action()->setEnabled((!m_portPath.isEmpty()) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));
            m_startCommand->action()->setEnabled((!running) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));
            m_startCommand->action()->setVisible(!running);
            m_stopCommand->action()->setEnabled(running);
            m_stopCommand->action()->setVisible(running);
            m_running = running;
        }
    });

    ///////////////////////////////////////////////////////////////////////////

    QMainWindow *mainWindow = q_check_ptr(qobject_cast<QMainWindow *>(Core::ICore::mainWindow()));
    Core::Internal::FancyTabWidget *widget = q_check_ptr(qobject_cast<Core::Internal::FancyTabWidget *>(mainWindow->centralWidget()));

    Core::Internal::FancyActionBar *actionBar0 = new Core::Internal::FancyActionBar(widget);
    widget->insertCornerWidget(0, actionBar0);

    actionBar0->insertAction(0, Core::ActionManager::command(Core::Constants::NEW)->action());
    actionBar0->insertAction(1, Core::ActionManager::command(Core::Constants::OPEN)->action());
    actionBar0->insertAction(2, Core::ActionManager::command(Core::Constants::SAVE)->action());

    actionBar0->setProperty("no_separator", true);
    actionBar0->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    Core::Internal::FancyActionBar *actionBar1 = new Core::Internal::FancyActionBar(widget);
    widget->insertCornerWidget(1, actionBar1);

    actionBar1->insertAction(0, Core::ActionManager::command(Core::Constants::UNDO)->action());
    actionBar1->insertAction(1, Core::ActionManager::command(Core::Constants::REDO)->action());
    actionBar1->insertAction(2, Core::ActionManager::command(Core::Constants::CUT)->action());
    actionBar1->insertAction(3, Core::ActionManager::command(Core::Constants::COPY)->action());
    actionBar1->insertAction(4, Core::ActionManager::command(Core::Constants::PASTE)->action());

    actionBar1->setProperty("no_separator", false);
    actionBar1->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    Core::Internal::FancyActionBar *actionBar2 = new Core::Internal::FancyActionBar(widget);
    widget->insertCornerWidget(2, actionBar2);

    actionBar2->insertAction(0, m_connectCommand->action());
    actionBar2->insertAction(1, m_disconnectCommand->action());
    actionBar2->insertAction(2, m_startCommand->action());
    actionBar2->insertAction(3, m_stopCommand->action());

    actionBar2->setProperty("no_separator", false);
    actionBar2->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    ///////////////////////////////////////////////////////////////////////////

    Utils::StyledBar *styledBar0 = new Utils::StyledBar;
    QHBoxLayout *styledBar0Layout = new QHBoxLayout;
    styledBar0Layout->setMargin(0);
    styledBar0Layout->setSpacing(0);
    styledBar0Layout->addSpacing(4);
    styledBar0Layout->addWidget(new QLabel(tr("Frame Buffer")));
    styledBar0Layout->addSpacing(6);
    styledBar0->setLayout(styledBar0Layout);

    QToolButton *beginRecordingButton = new QToolButton;
    beginRecordingButton->setText(tr("Record"));
    beginRecordingButton->setToolTip(tr("Record the Frame Buffer"));
    beginRecordingButton->setEnabled(false);
    styledBar0Layout->addWidget(beginRecordingButton);

    QToolButton *endRecordingButton = new QToolButton;
    endRecordingButton->setText(tr("Stop"));
    endRecordingButton->setToolTip(tr("Stop recording"));
    endRecordingButton->setVisible(false);
    styledBar0Layout->addWidget(endRecordingButton);

    QToolButton *zoomButton = new QToolButton;
    zoomButton->setText(tr("Zoom"));
    zoomButton->setToolTip(tr("Zoom to fit"));
    zoomButton->setCheckable(true);
    zoomButton->setChecked(false);
    styledBar0Layout->addWidget(zoomButton);

    m_jpgCompress = new QToolButton;
    m_jpgCompress->setText(tr("JPG"));
    m_jpgCompress->setToolTip(tr("JPEG compress the Frame Buffer for higher performance"));
    m_jpgCompress->setCheckable(true);
    m_jpgCompress->setChecked(true);
    ///// Disable JPEG Compress /////
    m_jpgCompress->setVisible(false);
    styledBar0Layout->addWidget(m_jpgCompress);
    connect(m_jpgCompress, &QToolButton::clicked, this, [this] {
        if(m_connected)
        {
            if(!m_working)
            {
                m_iodevice->jpegEnable(m_jpgCompress->isChecked());
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("JPG"),
                    tr("Busy... please wait..."));
            }
        }
    });

    m_disableFrameBuffer = new QToolButton;
    m_disableFrameBuffer->setText(tr("Disable"));
    m_disableFrameBuffer->setToolTip(tr("Disable the Frame Buffer for maximum performance"));
    m_disableFrameBuffer->setCheckable(true);
    m_disableFrameBuffer->setChecked(false);
    styledBar0Layout->addWidget(m_disableFrameBuffer);
    connect(m_disableFrameBuffer, &QToolButton::clicked, this, [this] {
        if(m_connected)
        {
            if(!m_working)
            {
                m_iodevice->fbEnable(!m_disableFrameBuffer->isChecked());
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Disable"),
                    tr("Busy... please wait..."));
            }
        }
    });

    Utils::ElidingLabel *disableLabel = new Utils::ElidingLabel(tr("Frame Buffer Disabled - click the disable button again to enable (top right)"));
    disableLabel->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    disableLabel->setStyleSheet(QStringLiteral("background-color:#1E1E27;color:#909090;padding:4px;"));
    disableLabel->setAlignment(Qt::AlignCenter);
    disableLabel->setVisible(m_disableFrameBuffer->isChecked());
    connect(m_disableFrameBuffer, &QToolButton::toggled, disableLabel, &QLabel::setVisible);

    Utils::ElidingLabel *recordingLabel = new Utils::ElidingLabel(tr("Elapsed: 0h:00m:00s:000ms - Size: 0 B - FPS: 0"));
    recordingLabel->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    recordingLabel->setStyleSheet(QStringLiteral("background-color:#1E1E27;color:#909090;padding:4px;"));
    recordingLabel->setAlignment(Qt::AlignCenter);
    recordingLabel->setVisible(false);
    recordingLabel->setFont(TextEditor::TextEditorSettings::fontSettings().defaultFixedFontFamily());

    m_frameBuffer = new OpenMVPluginFB;
    QWidget *tempWidget0 = new QWidget;
    QVBoxLayout *tempLayout0 = new QVBoxLayout;
    tempLayout0->setMargin(0);
    tempLayout0->setSpacing(0);
    tempLayout0->addWidget(styledBar0);
    tempLayout0->addWidget(disableLabel);
    tempLayout0->addWidget(m_frameBuffer);
    tempLayout0->addWidget(recordingLabel);
    tempWidget0->setLayout(tempLayout0);

    connect(zoomButton, &QToolButton::toggled, m_frameBuffer, &OpenMVPluginFB::enableFitInView);
    connect(m_iodevice, &OpenMVPluginIO::frameBufferData, this, [this] (const QPixmap &data) { if(!m_disableFrameBuffer->isChecked()) m_frameBuffer->frameBufferData(data); });
    connect(m_frameBuffer, &OpenMVPluginFB::saveImage, this, &OpenMVPlugin::saveImage);
    connect(m_frameBuffer, &OpenMVPluginFB::saveTemplate, this, &OpenMVPlugin::saveTemplate);
    connect(m_frameBuffer, &OpenMVPluginFB::saveDescriptor, this, &OpenMVPlugin::saveDescriptor);
    connect(m_frameBuffer, &OpenMVPluginFB::imageWriterTick, recordingLabel, &Utils::ElidingLabel::setText);

    connect(m_frameBuffer, &OpenMVPluginFB::pixmapUpdate, this, [this, beginRecordingButton] {
        beginRecordingButton->setEnabled(true);
    });

    connect(beginRecordingButton, &QToolButton::clicked, this, [this, beginRecordingButton, endRecordingButton, recordingLabel] {
        if(m_frameBuffer->beginImageWriter())
        {
            beginRecordingButton->setVisible(false);
            endRecordingButton->setVisible(true);
            recordingLabel->setVisible(true);
        }
    });

    connect(endRecordingButton, &QToolButton::clicked, this, [this, beginRecordingButton, endRecordingButton, recordingLabel] {
        m_frameBuffer->endImageWriter();
        beginRecordingButton->setVisible(true);
        endRecordingButton->setVisible(false);
        recordingLabel->setVisible(false);
    });

    connect(m_frameBuffer, &OpenMVPluginFB::imageWriterShutdown, this, [this, beginRecordingButton, endRecordingButton, recordingLabel] {
        beginRecordingButton->setVisible(true);
        endRecordingButton->setVisible(false);
        recordingLabel->setVisible(false);
    });

    Utils::StyledBar *styledBar1 = new Utils::StyledBar;
    QHBoxLayout *styledBar1Layout = new QHBoxLayout;
    styledBar1Layout->setMargin(0);
    styledBar1Layout->setSpacing(0);
    styledBar1Layout->addSpacing(4);
    styledBar1Layout->addWidget(new QLabel(tr("Histogram")));
    styledBar1Layout->addSpacing(6);
    styledBar1->setLayout(styledBar1Layout);

    QComboBox *colorSpace = new QComboBox;
    colorSpace->setProperty("hideborder", true);
    colorSpace->setProperty("drawleftborder", false);
    colorSpace->insertItem(RGB_COLOR_SPACE, tr("RGB Color Space"));
    colorSpace->insertItem(GRAYSCALE_COLOR_SPACE, tr("Grayscale Color Space"));
    colorSpace->insertItem(LAB_COLOR_SPACE, tr("LAB Color Space"));
    colorSpace->insertItem(YUV_COLOR_SPACE, tr("YUV Color Space"));
    colorSpace->setCurrentIndex(RGB_COLOR_SPACE);
    colorSpace->setToolTip(tr("Use Grayscale/LAB for color tracking"));
    styledBar1Layout->addWidget(colorSpace);

    Utils::ElidingLabel *resLabel = new Utils::ElidingLabel(tr("Res - No Image"));
    resLabel->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    resLabel->setStyleSheet(QStringLiteral("background-color:#1E1E27;color:#FFFFFF;padding:4px;"));
    resLabel->setAlignment(Qt::AlignCenter);

    m_histogram = new OpenMVPluginHistogram;
    QWidget *tempWidget1 = new QWidget;
    QVBoxLayout *tempLayout1 = new QVBoxLayout;
    tempLayout1->setMargin(0);
    tempLayout1->setSpacing(0);
    tempLayout1->addWidget(styledBar1);
    tempLayout1->addWidget(resLabel);
    tempLayout1->addWidget(m_histogram);
    tempWidget1->setLayout(tempLayout1);

    connect(colorSpace, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), m_histogram, &OpenMVPluginHistogram::colorSpaceChanged);
    connect(m_frameBuffer, &OpenMVPluginFB::pixmapUpdate, m_histogram, &OpenMVPluginHistogram::pixmapUpdate);

    connect(m_frameBuffer, &OpenMVPluginFB::resolutionAndROIUpdate, this, [this, resLabel] (const QSize &res, const QRect &roi) {
        if(res.isValid())
        {
            if(roi.isValid())
            {
                resLabel->setText(tr("Res (w:%1, h:%2) - ROI (x:%3, y:%4, w:%5, h:%6) - Pixels (%7)").arg(res.width()).arg(res.height()).arg(roi.x()).arg(roi.y()).arg(roi.width()).arg(roi.height()).arg(roi.width() * roi.height()));
            }
            else
            {
                resLabel->setText(tr("Res (w:%1, h:%2)").arg(res.width()).arg(res.height()));
            }
        }
        else
        {
            resLabel->setText(tr("Res - No Image"));
        }
    });

    Core::MiniSplitter *hsplitter = widget->m_hsplitter;
    Core::MiniSplitter *vsplitter = widget->m_vsplitter;
    vsplitter->insertWidget(0, tempWidget0);
    vsplitter->insertWidget(1, tempWidget1);
    vsplitter->setStretchFactor(0, 0);
    vsplitter->setStretchFactor(1, 1);
    vsplitter->setCollapsible(0, true);
    vsplitter->setCollapsible(1, true);

    connect(widget->m_leftDrawer, &QToolButton::clicked, this, [this, widget, hsplitter] {
        hsplitter->setSizes(QList<int>() << 1 << hsplitter->sizes().at(1));
        widget->m_leftDrawer->parentWidget()->hide();
    });

    connect(hsplitter, &Core::MiniSplitter::splitterMoved, this, [this, widget, hsplitter] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) widget->m_leftDrawer->parentWidget()->setVisible(!hsplitter->sizes().at(0));
    });

    connect(widget->m_rightDrawer, &QToolButton::clicked, this, [this, widget, hsplitter] {
        hsplitter->setSizes(QList<int>() << hsplitter->sizes().at(0) << 1);
        widget->m_rightDrawer->parentWidget()->hide();
    });

    connect(hsplitter, &Core::MiniSplitter::splitterMoved, this, [this, widget, hsplitter] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) widget->m_rightDrawer->parentWidget()->setVisible(!hsplitter->sizes().at(1));
    });

    connect(widget->m_topDrawer, &QToolButton::clicked, this, [this, widget, vsplitter] {
        vsplitter->setSizes(QList<int>() << 1 <<  vsplitter->sizes().at(1));
        widget->m_topDrawer->parentWidget()->hide();
    });

    connect(vsplitter, &Core::MiniSplitter::splitterMoved, this, [this, widget, vsplitter] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) widget->m_topDrawer->parentWidget()->setVisible(!vsplitter->sizes().at(0));
    });

    connect(widget->m_bottomDrawer, &QToolButton::clicked, this, [this, widget, vsplitter] {
        vsplitter->setSizes(QList<int>() << vsplitter->sizes().at(0) << 1);
        widget->m_bottomDrawer->parentWidget()->hide();
    });

    connect(vsplitter, &Core::MiniSplitter::splitterMoved, this, [this, widget, vsplitter] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) widget->m_bottomDrawer->parentWidget()->setVisible(!vsplitter->sizes().at(1));
    });

    connect(m_iodevice, &OpenMVPluginIO::printData, Core::MessageManager::instance(), &Core::MessageManager::printData);
    connect(m_iodevice, &OpenMVPluginIO::printData, this, &OpenMVPlugin::errorFilter);

    connect(m_iodevice, &OpenMVPluginIO::frameBufferData, this, [this] {
        m_queue.push_back(m_timer.restart());

        if(m_queue.size() > FPS_AVERAGE_BUFFER_DEPTH)
        {
            m_queue.pop_front();
        }

        qint64 average = 0;

        for(int i = 0; i < m_queue.size(); i++)
        {
            average += m_queue.at(i);
        }

        average /= m_queue.size();

        m_fpsLabel->setText(tr("FPS: %L1").arg(average ? (1000 / double(average)) : 0, 5, 'f', 1));
    });

    ///////////////////////////////////////////////////////////////////////////

    m_versionButton = new Utils::ElidingToolButton;
    m_versionButton->setText(tr("Firmware Version:"));
    m_versionButton->setToolTip(tr("Camera firmware version"));
    m_versionButton->setDisabled(true);
    Core::ICore::statusBar()->addPermanentWidget(m_versionButton);
    Core::ICore::statusBar()->addPermanentWidget(new QLabel());
    connect(m_versionButton, &QToolButton::clicked, this, &OpenMVPlugin::updateCam);

    m_portLabel = new Utils::ElidingLabel(tr("Serial Port:"));
    m_portLabel->setToolTip(tr("Camera serial port"));
    m_portLabel->setDisabled(true);
    Core::ICore::statusBar()->addPermanentWidget(m_portLabel);
    Core::ICore::statusBar()->addPermanentWidget(new QLabel());

    m_pathButton = new Utils::ElidingToolButton;
    m_pathButton->setText(tr("Drive:"));
    m_pathButton->setToolTip(tr("Drive associated with port"));
    m_pathButton->setDisabled(true);
    Core::ICore::statusBar()->addPermanentWidget(m_pathButton);
    Core::ICore::statusBar()->addPermanentWidget(new QLabel());
    connect(m_pathButton, &QToolButton::clicked, this, &OpenMVPlugin::setPortPath);

    m_fpsLabel = new Utils::ElidingLabel(tr("FPS:"));
    m_fpsLabel->setToolTip(tr("May be different from camera FPS"));
    m_fpsLabel->setDisabled(true);
    m_fpsLabel->setMinimumWidth(m_fpsLabel->fontMetrics().width(QStringLiteral("FPS: 000.000")));
    Core::ICore::statusBar()->addPermanentWidget(m_fpsLabel);

    ///////////////////////////////////////////////////////////////////////////

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));
    Core::EditorManager::restoreState(
        settings->value(QStringLiteral(EDITOR_MANAGER_STATE)).toByteArray());
    zoomButton->setChecked(
        settings->value(QStringLiteral(ZOOM_STATE), zoomButton->isChecked()).toBool());
    m_jpgCompress->setChecked(
        settings->value(QStringLiteral(JPG_COMPRESS_STATE), m_jpgCompress->isChecked()).toBool());
    m_disableFrameBuffer->setChecked(
        settings->value(QStringLiteral(DISABLE_FRAME_BUFFER_STATE), m_disableFrameBuffer->isChecked()).toBool());
    colorSpace->setCurrentIndex(
        settings->value(QStringLiteral(HISTOGRAM_COLOR_SPACE_STATE), colorSpace->currentIndex()).toInt());
    QFont font = TextEditor::TextEditorSettings::fontSettings().defaultFixedFontFamily();
    font.setPointSize(TextEditor::TextEditorSettings::fontSettings().defaultFontSize());
    Core::MessageManager::outputWindow()->setBaseFont(font);
    Core::MessageManager::outputWindow()->setWheelZoomEnabled(true);
    Core::MessageManager::outputWindow()->setFontZoom(
        settings->value(QStringLiteral(OUTPUT_WINDOW_FONT_ZOOM_STATE)).toFloat());
    settings->endGroup();

    connect(q_check_ptr(qobject_cast<Core::Internal::MainWindow *>(Core::ICore::mainWindow())), &Core::Internal::MainWindow::showEventSignal, this, [this, widget, settings, hsplitter, vsplitter] {
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));
        if(settings->contains(QStringLiteral(HSPLITTER_STATE))) hsplitter->restoreState(settings->value(QStringLiteral(HSPLITTER_STATE)).toByteArray());
        if(settings->contains(QStringLiteral(VSPLITTER_STATE))) vsplitter->restoreState(settings->value(QStringLiteral(VSPLITTER_STATE)).toByteArray());
        widget->m_leftDrawer->parentWidget()->setVisible(settings->contains(QStringLiteral(HSPLITTER_STATE)) ? (!hsplitter->sizes().at(0)) : false);
        widget->m_rightDrawer->parentWidget()->setVisible(settings->contains(QStringLiteral(HSPLITTER_STATE)) ? (!hsplitter->sizes().at(1)) : false);
        widget->m_topDrawer->parentWidget()->setVisible(settings->contains(QStringLiteral(VSPLITTER_STATE)) ? (!vsplitter->sizes().at(0)) : false);
        widget->m_bottomDrawer->parentWidget()->setVisible(settings->contains(QStringLiteral(VSPLITTER_STATE)) ? (!vsplitter->sizes().at(1)) : false);
        settings->endGroup();
    });

    m_openTerminalMenuData = QList<openTerminalMenuData_t>();

    for(int i = 0, j = settings->beginReadArray(QStringLiteral(OPEN_TERMINAL_SETTINGS_GROUP)); i < j; i++)
    {
        settings->setArrayIndex(i);
        openTerminalMenuData_t data;
        data.displayName = settings->value(QStringLiteral(OPEN_TERMINAL_DISPLAY_NAME)).toString();
        data.optionIndex = settings->value(QStringLiteral(OPEN_TERMINAL_OPTION_INDEX)).toInt();
        data.commandStr = settings->value(QStringLiteral(OPEN_TERMINAL_COMMAND_STR)).toString();
        data.commandVal = settings->value(QStringLiteral(OPEN_TERMINAL_COMMAND_VAL)).toInt();
        m_openTerminalMenuData.append(data);
    }

    settings->endArray();

    connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested, this, [this, zoomButton, colorSpace, hsplitter, vsplitter] {
        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));
        settings->setValue(QStringLiteral(EDITOR_MANAGER_STATE),
            Core::EditorManager::saveState());
        if(!isNoShow()) settings->setValue(QStringLiteral(HSPLITTER_STATE),
            hsplitter->saveState());
        if(!isNoShow()) settings->setValue(QStringLiteral(VSPLITTER_STATE),
            vsplitter->saveState());
        settings->setValue(QStringLiteral(ZOOM_STATE),
            zoomButton->isChecked());
        settings->setValue(QStringLiteral(JPG_COMPRESS_STATE),
            m_jpgCompress->isChecked());
        settings->setValue(QStringLiteral(DISABLE_FRAME_BUFFER_STATE),
            m_disableFrameBuffer->isChecked());
        settings->setValue(QStringLiteral(HISTOGRAM_COLOR_SPACE_STATE),
            colorSpace->currentIndex());
        settings->setValue(QStringLiteral(OUTPUT_WINDOW_FONT_ZOOM_STATE),
            Core::MessageManager::outputWindow()->fontZoom());
        settings->endGroup();

        settings->beginWriteArray(QStringLiteral(OPEN_TERMINAL_SETTINGS_GROUP));

        for(int i = 0, j = m_openTerminalMenuData.size(); i < j; i++)
        {
            settings->setArrayIndex(i);
            settings->setValue(QStringLiteral(OPEN_TERMINAL_DISPLAY_NAME), m_openTerminalMenuData.at(i).displayName);
            settings->setValue(QStringLiteral(OPEN_TERMINAL_OPTION_INDEX), m_openTerminalMenuData.at(i).optionIndex);
            settings->setValue(QStringLiteral(OPEN_TERMINAL_COMMAND_STR), m_openTerminalMenuData.at(i).commandStr);
            settings->setValue(QStringLiteral(OPEN_TERMINAL_COMMAND_VAL), m_openTerminalMenuData.at(i).commandVal);
        }

        settings->endArray();
    });

    ///////////////////////////////////////////////////////////////////////////

    Core::IEditor *editor = Core::EditorManager::currentEditor();

    if(editor ? (editor->document() ? editor->document()->contents().isEmpty() : true) : true)
    {
        QString filePath = Core::ICore::userResourcePath() + QStringLiteral("/examples/01-Basics/helloworld.py");

        QFile file(filePath);

        if(file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();

            if((file.error() == QFile::NoError) && (!data.isEmpty()))
            {
                Core::EditorManager::cutForwardNavigationHistory();
                Core::EditorManager::addCurrentPositionToNavigationHistory();

                QString titlePattern = QFileInfo(filePath).baseName().simplified() + QStringLiteral("_$.") + QFileInfo(filePath).completeSuffix();
                TextEditor::BaseTextEditor *editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::openEditorWithContents(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID, &titlePattern, data));

                if(editor)
                {
                    Core::EditorManager::addCurrentPositionToNavigationHistory();
                    editor->editorWidget()->configureGenericHighlighter();
                    Core::EditorManager::activateEditor(editor);
                }
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////

    QLoggingCategory::setFilterRules(QStringLiteral("qt.network.ssl.warning=false")); // http://stackoverflow.com/questions/26361145/qsslsocket-error-when-ssl-is-not-used

    if(!isNoShow()) connect(Core::ICore::instance(), &Core::ICore::coreOpened, this, [this] {

        QNetworkAccessManager *manager = new QNetworkAccessManager(this);

        connect(manager, &QNetworkAccessManager::finished, this, [this] (QNetworkReply *reply) {

            QByteArray data = reply->readAll();

            if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
            {
                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^(\\d+)\\.(\\d+)\\.(\\d+)$")).match(QString::fromUtf8(data).trimmed());

                int major = match.captured(1).toInt();
                int minor = match.captured(2).toInt();
                int patch = match.captured(3).toInt();

                if((OMV_IDE_VERSION_MAJOR < major)
                || ((OMV_IDE_VERSION_MAJOR == major) && (OMV_IDE_VERSION_MINOR < minor))
                || ((OMV_IDE_VERSION_MAJOR == major) && (OMV_IDE_VERSION_MINOR == minor) && (OMV_IDE_VERSION_RELEASE < patch)))
                {
                    QMessageBox box(QMessageBox::Information, tr("Update Available"), tr("A new version of OpenMV IDE (%L1.%L2.%L3) is available for download.").arg(major).arg(minor).arg(patch), QMessageBox::Cancel, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    QPushButton *button = box.addButton(tr("Download"), QMessageBox::AcceptRole);
                    box.setDefaultButton(button);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    if(box.clickedButton() == button)
                    {
                        QUrl url = QUrl(QStringLiteral("http://openmv.io/pages/download"));

                        if(!QDesktopServices::openUrl(url))
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                                  QString(),
                                                  tr("Failed to open: \"%L1\"").arg(url.toString()));
                        }
                    }
                    else
                    {
                        QTimer::singleShot(0, this, &OpenMVPlugin::packageUpdate);
                    }
                }
                else
                {
                    QTimer::singleShot(0, this, &OpenMVPlugin::packageUpdate);
                }
            }
            else
            {
                QTimer::singleShot(0, this, &OpenMVPlugin::packageUpdate);
            }

            reply->deleteLater();
        });

        QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("http://upload.openmv.io/openmv-ide-version.txt")));
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
        request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
        QNetworkReply *reply = manager->get(request);

        if(reply)
        {
            connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
        }
        else
        {
            QTimer::singleShot(0, this, &OpenMVPlugin::packageUpdate);
        }
    });
}

bool OpenMVPlugin::delayedInitialize()
{
    QUdpSocket *socket = new QUdpSocket(this);

    connect(socket, &QUdpSocket::readyRead, this, [this, socket] {
        while(socket->hasPendingDatagrams())
        {
            QByteArray datagram(socket->pendingDatagramSize(), 0);
            QHostAddress address;
            quint16 port;

            if((socket->readDatagram(datagram.data(), datagram.size(), &address, &port) == datagram.size()) && datagram.endsWith('\0') && (port == OPENMVCAM_BROADCAST_PORT))
            {
                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^(\\d+\\.\\d+\\.\\d+\\.\\d+):(\\d+):(.+)$")).match(QString::fromUtf8(datagram).trimmed());

                if(match.hasMatch())
                {
                    QHostAddress hostAddress = QHostAddress(match.captured(1));
                    bool hostPortOk;
                    quint16 hostPort = match.captured(2).toUInt(&hostPortOk);
                    QString hostName = match.captured(3).remove(QLatin1Char(':'));

                    if((address.toIPv4Address() == hostAddress.toIPv4Address()) && hostPortOk && (!hostName.isEmpty()))
                    {
                        wifiPort_t wifiPort;
                        wifiPort.addressAndPort = QString(QStringLiteral("%1:%2")).arg(hostAddress.toString()).arg(hostPort);
                        wifiPort.name = hostName;
                        wifiPort.time = QTime::currentTime();
                        if(!m_availableWifiPorts.contains(wifiPort)) m_availableWifiPorts.append(wifiPort);
                    }
                }
            }
        }
    });

    QTimer *timer = new QTimer(this);

    connect(timer, &QTimer::timeout, this, [this] {
        QTime currentTime = QTime::currentTime();
        QMutableListIterator<wifiPort_t> i(m_availableWifiPorts);

        while(i.hasNext())
        {
            if(qAbs(i.next().time.secsTo(currentTime)) >= WIFI_PORT_RETIRE)
            {
                i.remove();
            }
        }

        bool ok = false;

        foreach(QSerialPortInfo port, QSerialPortInfo::availablePorts())
        {
            if(port.hasVendorIdentifier() && (port.vendorIdentifier() == OPENMVCAM_VID)
            && port.hasProductIdentifier() && (port.productIdentifier() == OPENMVCAM_PID))
            {
                ok = true;
                break;
            }
        }

        if(!ok) {
            if(!m_availableWifiPorts.isEmpty()) {
                m_connectCommand->action()->setIcon(QIcon(QStringLiteral(CONNECT_WIFI_PATH)));
            } else {
                m_connectCommand->action()->setIcon(QIcon(QStringLiteral(CONNECT_PATH)));
            }
        } else {
            if(!m_availableWifiPorts.isEmpty()) {
                m_connectCommand->action()->setIcon(QIcon(QStringLiteral(CONNECT_USB_WIFI_PATH)));
            } else {
                m_connectCommand->action()->setIcon(QIcon(QStringLiteral(CONNECT_USB_PATH)));
            }
        }
    });

    if(socket->bind(OPENMVCAM_BROADCAST_PORT))
    {
        timer->start(1000);
    }
    else
    {
        delete socket;
        delete timer;

        if(!isNoShow()) QMessageBox::warning(Core::ICore::dialogParent(),
            tr("WiFi Programming Disabled!"),
            tr("Another application is using the OpenMV Cam broadcast discovery port. "
               "Please close that application and restart OpenMV IDE to enable WiFi programming."));
    }

    if(!QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV")))
    {
        QMessageBox::warning(Core::ICore::dialogParent(),
                    tr("Documents Folder Error"),
                    tr("Failed to create the documents folder!"));
    }

    return true;
}

ExtensionSystem::IPlugin::ShutdownFlag OpenMVPlugin::aboutToShutdown()
{
    if(!m_connected)
    {
        if(!m_working)
        {
            return ExtensionSystem::IPlugin::SynchronousShutdown;
        }
        else
        {
            connect(this, &OpenMVPlugin::workingDone, this, [this] { disconnectClicked(); });
            connect(this, &OpenMVPlugin::disconnectDone, this, &OpenMVPlugin::asynchronousShutdownFinished);
            return ExtensionSystem::IPlugin::AsynchronousShutdown;
        }
    }
    else
    {
        if(!m_working)
        {
            connect(this, &OpenMVPlugin::disconnectDone, this, &OpenMVPlugin::asynchronousShutdownFinished);
            QTimer::singleShot(0, this, [this] { disconnectClicked(); });
            return ExtensionSystem::IPlugin::AsynchronousShutdown;
        }
        else
        {
            connect(this, &OpenMVPlugin::workingDone, this, [this] { disconnectClicked(); });
            connect(this, &OpenMVPlugin::disconnectDone, this, &OpenMVPlugin::asynchronousShutdownFinished);
            return ExtensionSystem::IPlugin::AsynchronousShutdown;
        }
    }
}

QObject *OpenMVPlugin::remoteCommand(const QStringList &options, const QString &workingDirectory, const QStringList &arguments)
{
    Q_UNUSED(workingDirectory)
    Q_UNUSED(arguments)

    ///////////////////////////////////////////////////////////////////////////

    for(int i = 0; i < options.size(); i++)
    {
        if(options.at(i) == QStringLiteral("-open_serial_terminal"))
        {
            i += 1;

            if(options.size() > i)
            {
                QStringList list = options.at(i).split(QLatin1Char(':'));

                if(list.size() == 2)
                {
                    bool ok;
                    QString portNameValue = list.at(0);
                    int baudRateValue = list.at(1).toInt(&ok);

                    if(ok)
                    {
                        QString displayName = tr("Serial Port - %L1 - %L2 BPS").arg(portNameValue).arg(baudRateValue);
                        OpenMVTerminal *terminal = new OpenMVTerminal(displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(displayName)), true);
                        OpenMVTerminalSerialPort *terminalDevice = new OpenMVTerminalSerialPort(terminal);

                        connect(terminal, &OpenMVTerminal::writeBytes,
                                terminalDevice, &OpenMVTerminalPort::writeBytes);

                        connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                terminal, &OpenMVTerminal::readBytes);

                        QString errorMessage2 = QString();
                        QString *errorMessage2Ptr = &errorMessage2;

                        QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                            this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                            *errorMessage2Ptr = errorMessage;
                        });

                        // QProgressDialog scoping...
                        {
                            QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Q_NULLPTR,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                            dialog.setWindowModality(Qt::ApplicationModal);
                            dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                            dialog.setCancelButton(Q_NULLPTR);
                            QTimer::singleShot(1000, &dialog, &QWidget::show);

                            QEventLoop loop;

                            connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                    &loop, &QEventLoop::quit);

                            terminalDevice->open(portNameValue, baudRateValue);

                            loop.exec();
                            dialog.close();
                        }

                        disconnect(conn);

                        if(!errorMessage2.isEmpty())
                        {
                            QString errorMessage = tr("Error: %L1!").arg(errorMessage2);

                            if(Utils::HostOsInfo::isLinuxHost() && errorMessage2.contains(QStringLiteral("Permission Denied"), Qt::CaseInsensitive))
                            {
                                errorMessage += tr("\n\nTry doing:\n\nsudo adduser %L1 dialout\n\n...in a terminal and then restart your computer.").arg(Utils::Environment::systemEnvironment().userName());
                            }

                            delete terminalDevice;
                            delete terminal;

                            displayError(errorMessage);
                        }
                        else
                        {
                            terminal->show();
                        }
                    }
                    else
                    {
                        displayError(tr("Invalid baud rate argument (%1) for -open_serial_terminal").arg(list.at(1)));
                    }
                }
                else
                {
                    displayError(tr("-open_serial_terminal requires two arguments <port_name:baud_rate>"));
                }
            }
            else
            {
                displayError(tr("Missing arguments for -open_serial_terminal"));
            }
        }

        ///////////////////////////////////////////////////////////////////////

        if(options.at(i) == QStringLiteral("-open_udp_client_terminal"))
        {
            i += 1;

            if(options.size() > i)
            {
                QStringList list = options.at(i).split(QLatin1Char(':'));

                if(list.size() == 2)
                {
                    bool ok;
                    QString hostNameValue = list.at(0);
                    int portValue = list.at(1).toInt(&ok);

                    if(ok)
                    {
                        QString displayName = tr("UDP Client Connection - %1:%2").arg(hostNameValue).arg(portValue);
                        OpenMVTerminal *terminal = new OpenMVTerminal(displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(displayName)), true);
                        OpenMVTerminalUDPPort *terminalDevice = new OpenMVTerminalUDPPort(terminal);

                        connect(terminal, &OpenMVTerminal::writeBytes,
                                terminalDevice, &OpenMVTerminalPort::writeBytes);

                        connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                terminal, &OpenMVTerminal::readBytes);

                        QString errorMessage2 = QString();
                        QString *errorMessage2Ptr = &errorMessage2;

                        QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                            this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                            *errorMessage2Ptr = errorMessage;
                        });

                        // QProgressDialog scoping...
                        {
                            QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Q_NULLPTR,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                            dialog.setWindowModality(Qt::ApplicationModal);
                            dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                            dialog.setCancelButton(Q_NULLPTR);
                            QTimer::singleShot(1000, &dialog, &QWidget::show);

                            QEventLoop loop;

                            connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                    &loop, &QEventLoop::quit);

                            terminalDevice->open(hostNameValue, portValue);

                            loop.exec();
                            dialog.close();
                        }

                        disconnect(conn);

                        if(!errorMessage2.isEmpty())
                        {
                            QString errorMessage = tr("Error: %L1!").arg(errorMessage2);

                            delete terminalDevice;
                            delete terminal;

                            displayError(errorMessage);
                        }
                        else
                        {
                            terminal->show();
                        }
                    }
                    else
                    {
                        displayError(tr("Invalid port argument (%1) for -open_udp_client_terminal").arg(list.at(1)));
                    }
                }
                else
                {
                    displayError(tr("-open_udp_client_terminal requires two arguments <host_name:port>"));
                }
            }
            else
            {
                displayError(tr("Missing arguments for -open_udp_client_terminal"));
            }
        }

        ///////////////////////////////////////////////////////////////////////

        if(options.at(i) == QStringLiteral("-open_udp_server_terminal"))
        {
            i += 1;

            if(options.size() > i)
            {
                bool ok;
                int portValue = options.at(i).toInt(&ok);

                if(ok)
                {
                    QString displayName = tr("UDP Server Connection - %1").arg(portValue);
                    OpenMVTerminal *terminal = new OpenMVTerminal(displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(displayName)), true);
                    OpenMVTerminalUDPPort *terminalDevice = new OpenMVTerminalUDPPort(terminal);

                    connect(terminal, &OpenMVTerminal::writeBytes,
                            terminalDevice, &OpenMVTerminalPort::writeBytes);

                    connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                            terminal, &OpenMVTerminal::readBytes);

                    QString errorMessage2 = QString();
                    QString *errorMessage2Ptr = &errorMessage2;

                    QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                        this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                        *errorMessage2Ptr = errorMessage;
                    });

                    // QProgressDialog scoping...
                    {
                        QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Q_NULLPTR,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                        dialog.setWindowModality(Qt::ApplicationModal);
                        dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                        dialog.setCancelButton(Q_NULLPTR);
                        QTimer::singleShot(1000, &dialog, &QWidget::show);

                        QEventLoop loop;

                        connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                &loop, &QEventLoop::quit);

                        terminalDevice->open(QString(), portValue);

                        loop.exec();
                        dialog.close();
                    }

                    disconnect(conn);

                    if((!errorMessage2.isEmpty()) && (!errorMessage2.startsWith(QStringLiteral("OPENMV::"))))
                    {
                        QString errorMessage = tr("Error: %L1!").arg(errorMessage2);

                        delete terminalDevice;
                        delete terminal;

                        displayError(errorMessage);
                    }
                    else
                    {
                        if(!errorMessage2.isEmpty())
                        {
                            terminal->setWindowTitle(terminal->windowTitle().remove(QRegularExpression(QStringLiteral(" - \\d+"))) + QString(QStringLiteral(" - %1")).arg(errorMessage2.remove(0, 8)));
                        }

                        terminal->show();
                    }
                }
                else
                {
                    displayError(tr("Invalid port argument (%1) for -open_udp_server_terminal").arg(options.at(i)));
                }
            }
            else
            {
                displayError(tr("Missing arguments for -open_udp_server_terminal"));
            }
        }

        ///////////////////////////////////////////////////////////////////////

        if(options.at(i) == QStringLiteral("-open_tcp_client_terminal"))
        {
            i += 1;

            if(options.size() > i)
            {
                QStringList list = options.at(i).split(QLatin1Char(':'));

                if(list.size() == 2)
                {
                    bool ok;
                    QString hostNameValue = list.at(0);
                    int portValue = list.at(1).toInt(&ok);

                    if(ok)
                    {
                        QString displayName = tr("TCP Client Connection - %1:%2").arg(hostNameValue).arg(portValue);
                        OpenMVTerminal *terminal = new OpenMVTerminal(displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(displayName)), true);
                        OpenMVTerminalTCPPort *terminalDevice = new OpenMVTerminalTCPPort(terminal);

                        connect(terminal, &OpenMVTerminal::writeBytes,
                                terminalDevice, &OpenMVTerminalPort::writeBytes);

                        connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                terminal, &OpenMVTerminal::readBytes);

                        QString errorMessage2 = QString();
                        QString *errorMessage2Ptr = &errorMessage2;

                        QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                            this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                            *errorMessage2Ptr = errorMessage;
                        });

                        // QProgressDialog scoping...
                        {
                            QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Q_NULLPTR,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                            dialog.setWindowModality(Qt::ApplicationModal);
                            dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                            dialog.setCancelButton(Q_NULLPTR);
                            QTimer::singleShot(1000, &dialog, &QWidget::show);

                            QEventLoop loop;

                            connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                    &loop, &QEventLoop::quit);

                            terminalDevice->open(hostNameValue, portValue);

                            loop.exec();
                            dialog.close();
                        }

                        disconnect(conn);

                        if(!errorMessage2.isEmpty())
                        {
                            QString errorMessage = tr("Error: %L1!").arg(errorMessage2);

                            delete terminalDevice;
                            delete terminal;

                            displayError(errorMessage);
                        }
                        else
                        {
                            terminal->show();
                        }
                    }
                    else
                    {
                        displayError(tr("Invalid port argument (%1) for -open_tcp_client_terminal").arg(list.at(1)));
                    }
                }
                else
                {
                    displayError(tr("-open_tcp_client_terminal requires two arguments <host_name:port>"));
                }
            }
            else
            {
                displayError(tr("Missing arguments for -open_tcp_client_terminal"));
            }
        }

        ///////////////////////////////////////////////////////////////////////

        if(options.at(i) == QStringLiteral("-open_tcp_server_terminal"))
        {
            i += 1;

            if(options.size() > i)
            {
                bool ok;
                int portValue = options.at(i).toInt(&ok);

                if(ok)
                {
                    QString displayName = tr("TCP Server Connection - %1").arg(portValue);
                    OpenMVTerminal *terminal = new OpenMVTerminal(displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(displayName)), true);
                    OpenMVTerminalTCPPort *terminalDevice = new OpenMVTerminalTCPPort(terminal);

                    connect(terminal, &OpenMVTerminal::writeBytes,
                            terminalDevice, &OpenMVTerminalPort::writeBytes);

                    connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                            terminal, &OpenMVTerminal::readBytes);

                    QString errorMessage2 = QString();
                    QString *errorMessage2Ptr = &errorMessage2;

                    QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                        this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                        *errorMessage2Ptr = errorMessage;
                    });

                    // QProgressDialog scoping...
                    {
                        QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Q_NULLPTR,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                        dialog.setWindowModality(Qt::ApplicationModal);
                        dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                        dialog.setCancelButton(Q_NULLPTR);
                        QTimer::singleShot(1000, &dialog, &QWidget::show);

                        QEventLoop loop;

                        connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                &loop, &QEventLoop::quit);

                        terminalDevice->open(QString(), portValue);

                        loop.exec();
                        dialog.close();
                    }

                    disconnect(conn);

                    if((!errorMessage2.isEmpty()) && (!errorMessage2.startsWith(QStringLiteral("OPENMV::"))))
                    {
                        QString errorMessage = tr("Error: %L1!").arg(errorMessage2);

                        delete terminalDevice;
                        delete terminal;

                        displayError(errorMessage);
                    }
                    else
                    {
                        if(!errorMessage2.isEmpty())
                        {
                            terminal->setWindowTitle(terminal->windowTitle().remove(QRegularExpression(QStringLiteral(" - \\d+"))) + QString(QStringLiteral(" - %1")).arg(errorMessage2.remove(0, 8)));
                        }

                        terminal->show();
                    }
                }
                else
                {
                    displayError(tr("Invalid port argument (%1) for -open_tcp_server_terminal").arg(options.at(i)));
                }
            }
            else
            {
                displayError(tr("Missing arguments for -open_tcp_server_terminal"));
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////

    bool needToExit = true;

    foreach(QWindow *window, QApplication::allWindows())
    {
        if(window->isVisible())
        {
            needToExit = false;
        }
    }

    if(needToExit)
    {
        QTimer::singleShot(0, this, [this] { QApplication::exit(-1); });
    }

    return Q_NULLPTR;
}

void OpenMVPlugin::registerOpenMVCam(const QString board, const QString id)
{
    if(QMessageBox::warning(Core::ICore::dialogParent(),
        tr("Unregistered OpenMV Cam Detected"),
        tr("Your OpenMV Cam isn't registered. You need to register your OpenMV Cam with OpenMV for unlimited use with OpenMV IDE without any interruptions.\n\n"
           "Would you like to register your OpenMV Cam now?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
    == QMessageBox::Yes)
    {
        if(registerOpenMVCamDialog(board, id)) return;
    }

    if(QMessageBox::warning(Core::ICore::dialogParent(),
        tr("Unregistered OpenMV Cam Detected"),
        tr("Unregistered OpenMV Cams hurt the open-source OpenMV ecosystem by undercutting offical OpenMV Cam sales which help fund OpenMV Cam software development.\n\n"
           "Would you like to register your OpenMV Cam now?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
    == QMessageBox::Yes)
    {
        if(registerOpenMVCamDialog(board, id)) return;
    }

    if(QMessageBox::warning(Core::ICore::dialogParent(),
        tr("Unregistered OpenMV Cam Detected"),
        tr("OpenMV IDE will display these three messages boxes each time you connect until you register your OpenMV Cam...\n\n"
           "Would you like to register your OpenMV Cam now?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
    == QMessageBox::Yes)
    {
        if(registerOpenMVCamDialog(board, id)) return;
    }
}

bool OpenMVPlugin::registerOpenMVCamDialog(const QString board, const QString id)
{
    forever
    {
        QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
        dialog->setWindowTitle(tr("Register OpenMV Cam"));
        QVBoxLayout *layout = new QVBoxLayout(dialog);

        QLabel *label = new QLabel(tr("Please enter a board key to register your OpenMV Cam.<br/><br/>If you do not have a board key you may purchase one from OpenMV <a href=\"https://openmv.io/products/openmv-cam-board-key\">here</a>."));
        label->setOpenExternalLinks(true);
        layout->addWidget(label);

        QLineEdit *edit = new QLineEdit(QStringLiteral("#####-#####-#####-#####-#####"));
        layout->addWidget(edit);

        QLabel *info1 = new QLabel(QStringLiteral("Email <a href=\"mailto:openmv@openmv.io\">openmv@openmv.io</a> with your license key and the below info if you have trouble registering."));
        info1->setTextInteractionFlags(Qt::TextBrowserInteraction);
        layout->addWidget(info1);

        QLabel *info2 = new QLabel(QString(QStringLiteral("Board: %1 - ID: %2")).arg(board).arg(id));
        info2->setTextInteractionFlags(Qt::TextBrowserInteraction);
        layout->addWidget(info2);

        QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        layout->addWidget(box);

        bool boardKeyOk = dialog->exec() == QDialog::Accepted;
        QString boardKey = edit->text().replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(""));

        delete dialog;

        if(boardKeyOk)
        {
            QString chars, chars2;

            for(int i = 0; i < boardKey.size(); i++)
            {
                if(boardKey.at(i).isLetterOrNumber())
                {
                    if(chars2.size() && (!(chars2.size() % 5)))
                    {
                        chars.append(QLatin1Char('-'));
                    }

                    QChar chr = boardKey.at(i).toUpper();
                    chars.append(chr);
                    chars2.append(chr);
                }
            }

            if(QRegularExpression(QStringLiteral("^[0-9A-Z]{5}-[0-9A-Z]{5}-[0-9A-Z]{5}-[0-9A-Z]{5}-[0-9A-Z]{5}$")).match(chars).hasMatch())
            {
                QNetworkAccessManager manager(this);
                QProgressDialog dialog(tr("Registering OpenMV Cam..."), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));

                connect(&dialog, &QProgressDialog::canceled, &dialog, &QProgressDialog::reject);
                connect(&manager, &QNetworkAccessManager::finished, &dialog, &QProgressDialog::accept);

                QNetworkRequest request = QNetworkRequest(QUrl(QString(QStringLiteral("http://upload.openmv.io/openmv-swd-ids-register.php?board=%1&id=%2&id_key=%3")).arg(board).arg(id).arg(boardKey)));
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
                request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
                QNetworkReply *reply = manager.get(request);

                if(reply)
                {
                    connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));

                    bool wasCanceled = dialog.exec() != QDialog::Accepted;

                    QByteArray data = reply->readAll();

                    QTimer::singleShot(0, reply, &QNetworkReply::deleteLater);

                    if(!wasCanceled)
                    {
                        if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
                        {
                            if(QString::fromUtf8(data).contains(QStringLiteral("<p>Done</p>")))
                            {
                                QMessageBox::information(Core::ICore::dialogParent(),
                                    tr("Register OpenMV Cam"),
                                    tr("Thank you for registering your OpenMV Cam!"));

                                return true;
                            }
                            else if(QString::fromUtf8(data).contains(QStringLiteral("<p>Error: Invalid ID Key!</p>")))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Register OpenMV Cam"),
                                    tr("Invalid Board Key!"));
                            }
                            else if(QString::fromUtf8(data).contains(QStringLiteral("<p>Error: ID Key already used!</p>")))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Register OpenMV Cam"),
                                    tr("Board Key already used!"));
                            }
                            else if(QString::fromUtf8(data).contains(QStringLiteral("<p>Error: Board and ID already registered!</p>")))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Register OpenMV Cam"),
                                    tr("Board and ID already registered!"));
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Register OpenMV Cam"),
                                    tr("Database Error!"));
                            }
                        }
                        else if(reply->error() != QNetworkReply::NoError)
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Register OpenMV Cam"),
                                tr("Error: %L1!").arg(reply->error()));
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Register OpenMV Cam"),
                                tr("GET Network error!"));
                        }
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("Register OpenMV Cam"),
                        tr("GET network error!"));
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Register OpenMV Cam"),
                    tr("Invalidly formatted Board Key!"));
            }
        }
        else
        {
            return false;
        }
    }
}

static bool removeRecursively(const Utils::FileName &path, QString *error)
{
    return Utils::FileUtils::removeRecursively(path, error);
}

static bool removeRecursivelyWrapper(const Utils::FileName &path, QString *error)
{
    QEventLoop loop;
    QFutureWatcher<bool> watcher;
    QObject::connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(removeRecursively, path, error));
    loop.exec();
    return watcher.result();
}

static bool extractAll(QByteArray *data, const QString &path)
{
    QBuffer buffer(data);
    QZipReader reader(&buffer);
    return reader.extractAll(path);
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

void OpenMVPlugin::packageUpdate()
{
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);

    connect(manager, &QNetworkAccessManager::finished, this, [this] (QNetworkReply *reply) {

        QByteArray data = reply->readAll();

        if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
        {
            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^(\\d+)\\.(\\d+)\\.(\\d+)$")).match(QString::fromUtf8(data).trimmed());

            if(match.hasMatch())
            {
                int new_major = match.captured(1).toInt();
                int new_minor = match.captured(2).toInt();
                int new_patch = match.captured(3).toInt();

                QSettings *settings = ExtensionSystem::PluginManager::settings();
                settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

                int old_major = settings->value(QStringLiteral(RESOURCES_MAJOR)).toInt();
                int old_minor = settings->value(QStringLiteral(RESOURCES_MINOR)).toInt();
                int old_patch = settings->value(QStringLiteral(RESOURCES_PATCH)).toInt();

                settings->endGroup();

                if((old_major < new_major)
                || ((old_major == new_major) && (old_minor < new_minor))
                || ((old_major == new_major) && (old_minor == new_minor) && (old_patch < new_patch)))
                {
                    QMessageBox box(QMessageBox::Information, tr("Update Available"), tr("New OpenMV IDE reources are available (e.g. examples, firmware, documentation, etc.)."), QMessageBox::Cancel, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    QPushButton *button = box.addButton(tr("Install"), QMessageBox::AcceptRole);
                    box.setDefaultButton(button);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    if(box.clickedButton() == button)
                    {
                        QProgressDialog *dialog = new QProgressDialog(tr("Installing..."), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                        dialog->setWindowModality(Qt::ApplicationModal);
                        dialog->setAttribute(Qt::WA_ShowWithoutActivating);
                        dialog->setCancelButton(Q_NULLPTR);

                        QNetworkAccessManager *manager2 = new QNetworkAccessManager(this);

                        connect(manager2, &QNetworkAccessManager::finished, this, [this, new_major, new_minor, new_patch, dialog] (QNetworkReply *reply2) {

                            QByteArray data2 = reply2->readAll();

                            if((reply2->error() == QNetworkReply::NoError) && (!data2.isEmpty()))
                            {
                                QSettings *settings2 = ExtensionSystem::PluginManager::settings();
                                settings2->beginGroup(QStringLiteral(SETTINGS_GROUP));

                                settings2->setValue(QStringLiteral(RESOURCES_MAJOR), 0);
                                settings2->setValue(QStringLiteral(RESOURCES_MINOR), 0);
                                settings2->setValue(QStringLiteral(RESOURCES_PATCH), 0);
                                settings2->sync();

                                bool ok = true;

                                QString error;

                                if(!removeRecursivelyWrapper(Utils::FileName::fromString(Core::ICore::userResourcePath()), &error))
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        QString(),
                                        error + tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));

                                    QApplication::quit();
                                    ok = false;
                                }
                                else
                                {
                                    if(!extractAllWrapper(&data2, Core::ICore::userResourcePath()))
                                    {
                                        QMessageBox::critical(Core::ICore::dialogParent(),
                                            QString(),
                                            tr("Please close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));

                                        QApplication::quit();
                                        ok = false;
                                    }
                                }

                                if(ok)
                                {
                                    settings2->setValue(QStringLiteral(RESOURCES_MAJOR), new_major);
                                    settings2->setValue(QStringLiteral(RESOURCES_MINOR), new_minor);
                                    settings2->setValue(QStringLiteral(RESOURCES_PATCH), new_patch);
                                    settings2->sync();

                                    QMessageBox::information(Core::ICore::dialogParent(),
                                        QString(),
                                        tr("Installation Sucessful! Please restart OpenMV IDE."));

                                    QApplication::quit();
                                }

                                settings2->endGroup();
                            }
                            else if(reply2->error() != QNetworkReply::NoError)
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Package Update"),
                                    tr("Error: %L1!").arg(reply2->errorString()));
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Package Update"),
                                    tr("Cannot open the resources file \"%L1\"!").arg(reply2->request().url().toString()));
                            }

                            reply2->deleteLater();

                            delete dialog;
                        });

                        QNetworkRequest request2 = QNetworkRequest(QUrl(QStringLiteral("http://upload.openmv.io/openmv-ide-resources-%1.%2.%3/openmv-ide-resources-%1.%2.%3.zip").arg(new_major).arg(new_minor).arg(new_patch)));
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
                        request2.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
                        QNetworkReply *reply2 = manager2->get(request2);

                        if(reply2)
                        {
                            connect(reply2, &QNetworkReply::sslErrors, reply2, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));

                            dialog->show();
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Package Update"),
                                tr("Network request failed \"%L1\"!").arg(request2.url().toString()));
                        }
                    }
                }
            }
        }

        reply->deleteLater();
    });

    QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("http://upload.openmv.io/openmv-ide-resources-version.txt")));
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
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
    dialog->setWindowTitle(tr("Bootloader"));
    QFormLayout *layout = new QFormLayout(dialog);
    layout->setVerticalSpacing(0);

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    Utils::PathChooser *pathChooser = new Utils::PathChooser();
    pathChooser->setExpectedKind(Utils::PathChooser::File);
    pathChooser->setPromptDialogTitle(tr("Firmware Path"));
    pathChooser->setPromptDialogFilter(tr("Firmware Binary (*.bin *.dfu)"));
    pathChooser->setFileName(Utils::FileName::fromString(settings->value(QStringLiteral(LAST_FIRMWARE_PATH), QDir::homePath()).toString()));
    layout->addRow(tr("Firmware Path"), pathChooser);
    layout->addItem(new QSpacerItem(0, 6));

    QHBoxLayout *layout2 = new QHBoxLayout;
    layout2->setMargin(0);
    QWidget *widget = new QWidget;
    widget->setLayout(layout2);

    QCheckBox *checkBox = new QCheckBox(tr("Erase internal file system"));
    checkBox->setChecked(settings->value(QStringLiteral(LAST_FLASH_FS_ERASE_STATE), false).toBool());
    layout2->addWidget(checkBox);
    checkBox->setVisible(!pathChooser->path().endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive));
    QCheckBox *checkBox2 = new QCheckBox(tr("Erase internal file system"));
    checkBox2->setChecked(true);
    checkBox2->setEnabled(false);
    layout2->addWidget(checkBox2);
    checkBox2->setVisible(pathChooser->path().endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive));

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *run = new QPushButton(tr("Run"));
    run->setEnabled(pathChooser->isValid());
    box->addButton(run, QDialogButtonBox::AcceptRole);
    layout2->addSpacing(160);
    layout2->addWidget(box);
    layout->addRow(widget);

    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(pathChooser, &Utils::PathChooser::validChanged, run, &QPushButton::setEnabled);
    connect(pathChooser, &Utils::PathChooser::pathChanged, this, [this, dialog, checkBox, checkBox2] (const QString &path) {

        if(path.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
        {
            checkBox->setVisible(false);
            checkBox2->setVisible(true);
        }
        else
        {
            checkBox->setVisible(true);
            checkBox2->setVisible(false);
        }

        dialog->adjustSize();
    });

    if(dialog->exec() == QDialog::Accepted)
    {
        QString forceFirmwarePath = pathChooser->path();
        bool flashFSErase = checkBox->isChecked();

        if(QFileInfo(forceFirmwarePath).exists() && QFileInfo(forceFirmwarePath).isFile())
        {
            settings->setValue(QStringLiteral(LAST_FIRMWARE_PATH), forceFirmwarePath);
            settings->setValue(QStringLiteral(LAST_FLASH_FS_ERASE_STATE), flashFSErase);
            settings->endGroup();
            delete dialog;

            connectClicked(true, forceFirmwarePath, (flashFSErase || forceFirmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive)));
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                tr("Bootloader"),
                tr("\"%L1\" is not a file!").arg(forceFirmwarePath));

            settings->endGroup();
            delete dialog;
        }
    }
    else
    {
        settings->endGroup();
        delete dialog;
    }
}

#define CONNECT_END() \
do { \
    m_working = false; \
    QTimer::singleShot(0, this, &OpenMVPlugin::workingDone); \
    return; \
} while(0)

#define RECONNECT_END() \
do { \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked();}); \
    return; \
} while(0)

#define CLOSE_CONNECT_END() \
do { \
    QEventLoop m_loop; \
    connect(m_iodevice, &OpenMVPluginIO::closeResponse, &m_loop, &QEventLoop::quit); \
    m_iodevice->close(); \
    m_loop.exec(); \
    m_working = false; \
    QTimer::singleShot(0, this, &OpenMVPlugin::workingDone); \
    return; \
} while(0)

#define CLOSE_RECONNECT_END() \
do { \
    QEventLoop m_loop; \
    connect(m_iodevice, &OpenMVPluginIO::closeResponse, &m_loop, &QEventLoop::quit); \
    m_iodevice->close(); \
    m_loop.exec(); \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked();}); \
    return; \
} while(0)

void OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePath, bool forceFlashFSErase)
{
    if(!m_working)
    {
        m_working = true;

        QStringList stringList;

        foreach(QSerialPortInfo port, QSerialPortInfo::availablePorts())
        {
            if(port.hasVendorIdentifier() && (port.vendorIdentifier() == OPENMVCAM_VID)
            && port.hasProductIdentifier() && (port.productIdentifier() == OPENMVCAM_PID))
            {
                stringList.append(port.portName());
            }
        }

        if(Utils::HostOsInfo::isMacHost())
        {
            stringList = stringList.filter(QStringLiteral("cu"), Qt::CaseInsensitive);
        }

        if(!forceBootloader) foreach(wifiPort_t port, m_availableWifiPorts)
        {
            stringList.append(QString(QStringLiteral("%1:%2")).arg(port.name).arg(port.addressAndPort));
        }

        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QString selectedPort;
        bool forceBootloaderBricked = false;
        QString firmwarePath = forceFirmwarePath;
        int originalEraseFlashSectorStart = FLASH_SECTOR_START;
        int originalEraseFlashSectorEnd = FLASH_SECTOR_END;
        int originalEraseFlashSectorAllStart = FLASH_SECTOR_ALL_START;
        int originalEraseFlashSectorAllEnd = FLASH_SECTOR_ALL_END;

        if(stringList.isEmpty())
        {
            if(forceBootloader)
            {
                forceBootloaderBricked = true;
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Connect"),
                    tr("No OpenMV Cams found!"));

                QFile boards(Core::ICore::userResourcePath() + QStringLiteral("/firmware/boards.txt"));

                if(boards.open(QIODevice::ReadOnly))
                {
                    QMap<QString, QString> mappings;
                    QMap<QString, QPair<int, int> > eraseMappings;
                    QMap<QString, QPair<int, int> > eraseAllMappings;

                    forever
                    {
                        QByteArray data = boards.readLine();

                        if((boards.error() == QFile::NoError) && (!data.isEmpty()))
                        {
                            QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)")).match(QString::fromUtf8(data));
                            QString temp = mapping.captured(2).replace(QStringLiteral("_"), QStringLiteral(" "));
                            mappings.insert(temp, mapping.captured(3).replace(QStringLiteral("_"), QStringLiteral(" ")));
                            eraseMappings.insert(temp, QPair<int, int>(mapping.captured(5).toInt(), mapping.captured(6).toInt()));
                            eraseAllMappings.insert(temp, QPair<int, int>(mapping.captured(4).toInt(), mapping.captured(6).toInt()));
                        }
                        else
                        {
                            boards.close();
                            break;
                        }
                    }

                    if(!mappings.isEmpty())
                    {
                        if(QMessageBox::question(Core::ICore::dialogParent(),
                            tr("Connect"),
                            tr("Do you have an OpenMV Cam connected and is it bricked?"),
                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
                        == QMessageBox::Yes)
                        {
                            int index = mappings.keys().indexOf(settings->value(QStringLiteral(LAST_BOARD_TYPE_STATE)).toString());

                            bool ok;
                            QString temp = QInputDialog::getItem(Core::ICore::dialogParent(),
                                tr("Connect"), tr("Please select the board type"),
                                mappings.keys(), (index != -1) ? index : 0, false, &ok,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                            if(ok)
                            {
                                settings->setValue(QStringLiteral(LAST_BOARD_TYPE_STATE), temp);

                                int answer = QMessageBox::question(Core::ICore::dialogParent(),
                                    tr("Connect"),
                                    tr("Erase the internal file system?"),
                                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);

                                if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))
                                {
                                    forceBootloader = true;
                                    forceFlashFSErase = answer == QMessageBox::Yes;
                                    forceBootloaderBricked = true;
                                    firmwarePath = Core::ICore::userResourcePath() + QStringLiteral("/firmware/") + mappings.value(temp) + QStringLiteral("/firmware.bin");
                                    originalEraseFlashSectorStart = eraseMappings.value(temp).first;
                                    originalEraseFlashSectorEnd = eraseMappings.value(temp).second;
                                    originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;
                                    originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;
                                }
                            }
                        }
                    }
                }
            }
        }
        else if(stringList.size() == 1)
        {
            selectedPort = stringList.first();
            settings->setValue(QStringLiteral(LAST_SERIAL_PORT_STATE), selectedPort);
        }
        else
        {
            int index = stringList.indexOf(settings->value(QStringLiteral(LAST_SERIAL_PORT_STATE)).toString());

            bool ok;
            QString temp = QInputDialog::getItem(Core::ICore::dialogParent(),
                tr("Connect"), tr("Please select a serial port"),
                stringList, (index != -1) ? index : 0, false, &ok,
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

            if(ok)
            {
                selectedPort = temp;
                settings->setValue(QStringLiteral(LAST_SERIAL_PORT_STATE), selectedPort);
            }
        }

        settings->endGroup();

        if((!forceBootloaderBricked) && selectedPort.isEmpty())
        {
            CONNECT_END();
        }

        // Open Port //////////////////////////////////////////////////////////

        if(!forceBootloaderBricked)
        {
            QString errorMessage2 = QString();
            QString *errorMessage2Ptr = &errorMessage2;

            QMetaObject::Connection conn = connect(m_ioport, &OpenMVPluginSerialPort::openResult,
                this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                *errorMessage2Ptr = errorMessage;
            });

            QProgressDialog dialog(tr("Connecting... (Hit cancel if this takes more than 5 seconds)."), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
               Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
               (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
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
                    tr("Connect"),
                    tr("Error: %L1!").arg(errorMessage2));

                if(Utils::HostOsInfo::isLinuxHost() && errorMessage2.contains(QStringLiteral("Permission Denied"), Qt::CaseInsensitive))
                {
                    QMessageBox::information(Core::ICore::dialogParent(),
                        tr("Connect"),
                        tr("Try doing:\n\nsudo adduser %L1 dialout\n\n...in a terminal and then restart your computer.").arg(Utils::Environment::systemEnvironment().userName()));
                }

                CONNECT_END();
            }
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
                this, [this, major2Ptr, minor2Ptr, patch2Ptr] (int major, int minor, int patch) {
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
                if(m_reconnects < RECONNECTS_MAX)
                {
                    m_reconnects += 1;

                    QThread::msleep(10);
                    CLOSE_RECONNECT_END();
                }

                m_reconnects = 0;

                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Connect"),
                    tr("Timeout error while getting firmware version!"));

                QMessageBox::warning(Core::ICore::dialogParent(),
                    tr("Connect"),
                    tr("Do not try to connect while the green light on your OpenMV Cam is on!"));

                if(QMessageBox::question(Core::ICore::dialogParent(),
                    tr("Connect"),
                    tr("Try to connect again?"),
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
        }

        m_reconnects = 0;

        // Bootloader /////////////////////////////////////////////////////////

        if(forceBootloader)
        {
            if(!forceBootloaderBricked)
            {
                if(firmwarePath.isEmpty())
                {
                    if((major2 < OLD_API_MAJOR)
                    || ((major2 == OLD_API_MAJOR) && (minor2 < OLD_API_MINOR))
                    || ((major2 == OLD_API_MAJOR) && (minor2 == OLD_API_MINOR) && (patch2 < OLD_API_PATCH)))
                    {
                        firmwarePath = Core::ICore::userResourcePath() + QStringLiteral("/firmware/") + QStringLiteral(OLD_API_BOARD) + QStringLiteral("/firmware.bin");
                    }
                    else
                    {
                        QString arch2 = QString();
                        QString *arch2Ptr = &arch2;

                        QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::archString,
                            this, [this, arch2Ptr] (const QString &arch) {
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
                            QFile boards(Core::ICore::userResourcePath() + QStringLiteral("/firmware/boards.txt"));

                            if(boards.open(QIODevice::ReadOnly))
                            {
                                QMap<QString, QString> mappings;
                                QMap<QString, QPair<int, int> > eraseMappings;
                                QMap<QString, QPair<int, int> > eraseAllMappings;

                                forever
                                {
                                    QByteArray data = boards.readLine();

                                    if((boards.error() == QFile::NoError) && (!data.isEmpty()))
                                    {
                                        QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)")).match(QString::fromUtf8(data));
                                        QString temp = mapping.captured(1).replace(QStringLiteral("_"), QStringLiteral(" "));
                                        mappings.insert(temp, mapping.captured(3).replace(QStringLiteral("_"), QStringLiteral(" ")));
                                        eraseMappings.insert(temp, QPair<int, int>(mapping.captured(5).toInt(), mapping.captured(6).toInt()));
                                        eraseAllMappings.insert(temp, QPair<int, int>(mapping.captured(4).toInt(), mapping.captured(6).toInt()));
                                    }
                                    else
                                    {
                                        boards.close();
                                        break;
                                    }
                                }

                                QString temp = arch2.remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified().replace(QStringLiteral("_"), QStringLiteral(" "));

                                if(mappings.contains(temp))
                                {
                                    firmwarePath = Core::ICore::userResourcePath() + QStringLiteral("/firmware/") + mappings.value(temp) + QStringLiteral("/firmware.bin");
                                    originalEraseFlashSectorStart = eraseMappings.value(temp).first;
                                    originalEraseFlashSectorEnd = eraseMappings.value(temp).second;
                                    originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;
                                    originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;
                                }
                                else
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        tr("Connect"),
                                        tr("Unsupported board architecture!"));

                                    CLOSE_CONNECT_END();
                                }
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Connect"),
                                    tr("Error: %L1!").arg(boards.errorString()));

                                CLOSE_CONNECT_END();
                            }
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Connect"),
                                tr("Timeout error while getting board architecture!"));

                            CLOSE_CONNECT_END();
                        }
                    }
                }

                if(firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
                {
                    QEventLoop loop;

                    connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                            &loop, &QEventLoop::quit);

                    m_iodevice->close();

                    loop.exec();
                }
            }

            // BIN Bootloader /////////////////////////////////////////////////

            while(firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive))
            {
                QFile file(firmwarePath);

                if(file.open(QIODevice::ReadOnly))
                {
                    QByteArray data = file.readAll();

                    if((file.error() == QFile::NoError) && (!data.isEmpty()))
                    {
                        file.close();

                        QList<QByteArray> dataChunks;

                        for(int i = 0; i < data.size(); i += FLASH_WRITE_CHUNK_SIZE)
                        {
                            dataChunks.append(data.mid(i, qMin(FLASH_WRITE_CHUNK_SIZE, data.size() - i)));
                        }

                        if(dataChunks.last().size() % FLASH_WRITE_CHUNK_SIZE)
                        {
                            dataChunks.last().append(QByteArray(FLASH_WRITE_CHUNK_SIZE - dataChunks.last().size(), 255));
                        }

                        // Start Bootloader ///////////////////////////////////
                        {
                            bool done2 = bool(), loopExit = false, done22 = false;
                            bool *done2Ptr = &done2, *loopExitPtr = &loopExit, *done2Ptr2 = &done22;
                            int version2 = int(), *version2Ptr = &version2;

                            QMetaObject::Connection conn = connect(m_ioport, &OpenMVPluginSerialPort::bootloaderStartResponse,
                                this, [this, done2Ptr, loopExitPtr, version2Ptr] (bool done, int version) {
                                *done2Ptr = done;
                                *loopExitPtr = true;
                                *version2Ptr = version;
                            });

                            QMetaObject::Connection conn2 = connect(m_ioport, &OpenMVPluginSerialPort::bootloaderStopResponse,
                                this, [this, done2Ptr2] {
                                *done2Ptr2 = true;
                            });

                            QProgressDialog dialog(forceBootloaderBricked ? tr("Disconnect your OpenMV Cam and then reconnect it...\n\nHit cancel to skip to DFU reprogramming.") : tr("Connecting... (Hit cancel if this takes more than 5 seconds)."), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                            dialog.setWindowModality(Qt::ApplicationModal);
                            dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                            dialog.show();

                            connect(&dialog, &QProgressDialog::canceled,
                                    m_ioport, &OpenMVPluginSerialPort::bootloaderStop);

                            QEventLoop loop, loop0, loop1;

                            connect(m_ioport, &OpenMVPluginSerialPort::bootloaderStartResponse,
                                    &loop, &QEventLoop::quit);

                            connect(m_ioport, &OpenMVPluginSerialPort::bootloaderStopResponse,
                                    &loop0, &QEventLoop::quit);

                            connect(m_ioport, &OpenMVPluginSerialPort::bootloaderResetResponse,
                                    &loop1, &QEventLoop::quit);

                            m_ioport->bootloaderStart(selectedPort);

                            // NOT loop.exec();
                            while(!loopExit)
                            {
                                QSerialPortInfo::availablePorts();
                                QApplication::processEvents();
                                // Keep updating the list of available serial
                                // ports for the non-gui serial thread.
                            }

                            dialog.close();

                            if(!done22)
                            {
                                loop0.exec();
                            }

                            m_ioport->bootloaderReset();

                            loop1.exec();

                            disconnect(conn);

                            disconnect(conn2);

                            if(!done2)
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Connect"),
                                    tr("Unable to connect to your OpenMV Cam's normal bootloader!"));

                                if(forceFirmwarePath.isEmpty() && QMessageBox::question(Core::ICore::dialogParent(),
                                    tr("Connect"),
                                    tr("OpenMV IDE can still try to upgrade your OpenMV Cam using your OpenMV Cam's DFU Bootloader.\n\n"
                                       "Continue?"),
                                    QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                                == QMessageBox::Ok)
                                {
                                    firmwarePath = QFileInfo(firmwarePath).path() + QStringLiteral("/openmv.dfu");
                                    break;
                                }

                                CONNECT_END();
                            }

                            if(version2 == NEW_BOOTLDR)
                            {
                                int all_start2 = int(), *all_start2Ptr = &all_start2;
                                int start2 = int(), *start2Ptr = &start2;
                                int last2 = int(), *last2Ptr = &last2;

                                QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::bootloaderQueryDone,
                                    this, [this, all_start2Ptr, start2Ptr, last2Ptr] (int all_start, int start, int last) {
                                    *all_start2Ptr = all_start;
                                    *start2Ptr = start;
                                    *last2Ptr = last;
                                });

                                QEventLoop loop;

                                connect(m_iodevice, &OpenMVPluginIO::bootloaderQueryDone,
                                        &loop, &QEventLoop::quit);

                                m_iodevice->bootloaderQuery();

                                loop.exec();

                                disconnect(conn);

                                if((all_start2 || start2 || last2)
                                && ((0 <= all_start2) && (all_start2 <= 1023) && (0 <= start2) && (start2 <= 1023) && (0 <= last2) && (last2 <= 1023)))
                                {
                                    originalEraseFlashSectorStart = start2;
                                    originalEraseFlashSectorEnd = last2;
                                    originalEraseFlashSectorAllStart = all_start2;
                                    originalEraseFlashSectorAllEnd = last2;
                                }
                            }
                        }

                        // Erase Flash ////////////////////////////////////////
                        {
                            int flash_start = forceFlashFSErase ? originalEraseFlashSectorAllStart : originalEraseFlashSectorStart;
                            int flash_end = forceFlashFSErase ? originalEraseFlashSectorAllEnd : originalEraseFlashSectorEnd;

                            bool ok2 = bool();
                            bool *ok2Ptr = &ok2;

                            QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::flashEraseDone,
                                this, [this, ok2Ptr] (bool ok) {
                                *ok2Ptr = ok;
                            });

                            QProgressDialog dialog(tr("Erasing..."), tr("Cancel"), flash_start, flash_end, Core::ICore::dialogParent(),
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                            dialog.setWindowModality(Qt::ApplicationModal);
                            dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                            dialog.setCancelButton(Q_NULLPTR);
                            dialog.show();

                            for(int i = flash_start; i <= flash_end; i++)
                            {
                                QEventLoop loop0, loop1;

                                connect(m_iodevice, &OpenMVPluginIO::flashEraseDone,
                                        &loop0, &QEventLoop::quit);

                                m_iodevice->flashErase(i);

                                loop0.exec();

                                if(!ok2)
                                {
                                    break;
                                }

                                QTimer::singleShot(FLASH_ERASE_DELAY, &loop1, &QEventLoop::quit);

                                loop1.exec();

                                dialog.setValue(i);
                            }

                            dialog.close();

                            disconnect(conn2);

                            if(!ok2)
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Connect"),
                                    tr("Timeout Error!"));

                                CLOSE_CONNECT_END();
                            }
                        }

                        // Program Flash //////////////////////////////////////
                        {
                            bool ok2 = bool();
                            bool *ok2Ptr = &ok2;

                            QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::flashWriteDone,
                                this, [this, ok2Ptr] (bool ok) {
                                *ok2Ptr = ok;
                            });

                            QProgressDialog dialog(tr("Programming..."), tr("Cancel"), 0, dataChunks.size() - 1, Core::ICore::dialogParent(),
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                            dialog.setWindowModality(Qt::ApplicationModal);
                            dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                            dialog.setCancelButton(Q_NULLPTR);
                            dialog.show();

                            for(int i = 0; i < dataChunks.size(); i++)
                            {
                                QEventLoop loop0, loop1;

                                connect(m_iodevice, &OpenMVPluginIO::flashWriteDone,
                                        &loop0, &QEventLoop::quit);

                                m_iodevice->flashWrite(dataChunks.at(i));

                                loop0.exec();

                                if(!ok2)
                                {
                                    break;
                                }

                                QTimer::singleShot(FLASH_WRITE_DELAY, &loop1, &QEventLoop::quit);

                                loop1.exec();

                                dialog.setValue(i);
                            }

                            dialog.close();

                            disconnect(conn2);

                            if(!ok2)
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Connect"),
                                    tr("Timeout Error!"));

                                CLOSE_CONNECT_END();
                            }
                        }

                        // Reset Bootloader ///////////////////////////////////
                        {
                            QProgressDialog dialog(tr("Programming..."), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                            dialog.setWindowModality(Qt::ApplicationModal);
                            dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                            dialog.setCancelButton(Q_NULLPTR);
                            dialog.show();

                            QEventLoop loop;

                            connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                                    &loop, &QEventLoop::quit);

                            m_iodevice->bootloaderReset();
                            m_iodevice->close();

                            loop.exec();

                            dialog.close();

                            QMessageBox::information(Core::ICore::dialogParent(),
                                tr("Connect"),
                                tr("Firmware Upgrade complete!\n\n"
                                   "Your OpenMV Cam is running its built-in self-test... this may take a while.\n\n"
                                   "Click OK when your OpenMV Cam's RGB LED starts blinking blue - which indicates the self-test is complete."));

                            RECONNECT_END();
                        }
                    }
                    else if(file.error() != QFile::NoError)
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Connect"),
                            tr("Error: %L1!").arg(file.errorString()));

                        if(forceBootloaderBricked)
                        {
                            CONNECT_END();
                        }
                        else
                        {
                            CLOSE_CONNECT_END();
                        }
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Connect"),
                            tr("The firmware file is empty!"));

                        if(forceBootloaderBricked)
                        {
                            CONNECT_END();
                        }
                        else
                        {
                            CLOSE_CONNECT_END();
                        }
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("Connect"),
                        tr("Error: %L1!").arg(file.errorString()));

                    if(forceBootloaderBricked)
                    {
                        CONNECT_END();
                    }
                    else
                    {
                        CLOSE_CONNECT_END();
                    }
                }
            }

            // DFU Bootloader /////////////////////////////////////////////////

            if(firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
            {
                if(forceFlashFSErase || (QMessageBox::warning(Core::ICore::dialogParent(),
                    tr("Connect"),
                    tr("DFU update erases your OpenMV Cam's internal flash file system.\n\n"
                       "Backup your data before continuing!"),
                    QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                == QMessageBox::Ok))
                {
                    if(QMessageBox::information(Core::ICore::dialogParent(),
                        tr("Connect"),
                        tr("Disconnect your OpenMV Cam from your computer, add a jumper wire between the BOOT and RST pins, and then reconnect your OpenMV Cam to your computer.\n\n"
                           "Click the Ok button after your OpenMV Cam's DFU Bootloader has enumerated."),
                        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                    == QMessageBox::Ok)
                    {
                        QProgressDialog dialog(tr("Reprogramming...\n\n(may take up to 5 minutes)"), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                        dialog.setWindowModality(Qt::ApplicationModal);
                        dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                        dialog.setCancelButton(Q_NULLPTR);
                        dialog.show();

                        QString command;
                        Utils::SynchronousProcess process;
                        Utils::SynchronousProcessResponse response;
                        process.setTimeoutS(300); // 5 minutes...
                        process.setProcessChannelMode(QProcess::MergedChannels);

                        if(Utils::HostOsInfo::isWindowsHost())
                        {
                            for(int i = 0; i < 10; i++) // try multiple times...
                            {
                                command = QDir::cleanPath(QDir::toNativeSeparators(Core::ICore::resourcePath() + QStringLiteral("/dfuse/DfuSeCommand.exe")));
                                response = process.run(command, QStringList()
                                    << QStringLiteral("-c")
                                    << QStringLiteral("-d")
                                    << QStringLiteral("--v")
                                    << QStringLiteral("--o")
                                    << QStringLiteral("--fn")
                                    << QDir::cleanPath(QDir::toNativeSeparators(firmwarePath)));

                                if(response.result == Utils::SynchronousProcessResponse::Finished)
                                {
                                    break;
                                }
                                else
                                {
                                    QApplication::processEvents();
                                }
                            }
                        }
                        else
                        {
                            for(int i = 0; i < 10; i++) // try multiple times...
                            {
                                command = QDir::cleanPath(QDir::toNativeSeparators(Core::ICore::resourcePath() + QStringLiteral("/pydfu/pydfu.py")));
                                response = process.run(QStringLiteral("python"), QStringList()
                                    << command
                                    << QStringLiteral("-u")
                                    << QDir::cleanPath(QDir::toNativeSeparators(firmwarePath)));

                                if(response.result == Utils::SynchronousProcessResponse::Finished)
                                {
                                    break;
                                }
                                else
                                {
                                    QApplication::processEvents();
                                }
                            }
                        }

                        if(response.result == Utils::SynchronousProcessResponse::Finished)
                        {
                            QMessageBox::information(Core::ICore::dialogParent(),
                                tr("Connect"),
                                tr("DFU firmware update complete!\n\n") +
                                (Utils::HostOsInfo::isWindowsHost() ? tr("Disconnect your OpenMV Cam from your computer, remove the jumper wire between the BOOT and RST pins, and then reconnect your OpenMV Cam to your computer.\n\n") : QString()) +
                                tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while)."));

                            RECONNECT_END();
                        }
                        else
                        {
                            QMessageBox box(QMessageBox::Critical, tr("Connect"), tr("DFU firmware update failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                            box.setDetailedText(response.stdOut);
                            box.setInformativeText(response.exitMessage(command, process.timeoutS()));
                            box.setDefaultButton(QMessageBox::Ok);
                            box.setEscapeButton(QMessageBox::Cancel);
                            box.exec();

                            if(Utils::HostOsInfo::isMacHost())
                            {
                                QMessageBox::information(Core::ICore::dialogParent(),
                                    tr("Connect"),
                                    tr("PyDFU requires the following libraries to be installed:\n\n"
                                       "MacPorts:\n"
                                       "    sudo port install libusb py-pip\n"
                                       "    sudo pip install pyusb\n\n"
                                       "HomeBrew:\n"
                                       "    sudo brew install libusb python\n"
                                       "    sudo pip install pyusb"));
                            }

                            if(Utils::HostOsInfo::isLinuxHost())
                            {
                                QMessageBox::information(Core::ICore::dialogParent(),
                                    tr("Connect"),
                                    tr("PyDFU requires the following libraries to be installed:\n\n"
                                       "    sudo apt-get install libusb-1.0 python-pip\n"
                                       "    sudo pip install pyusb"));
                            }
                        }
                    }
                }

                CONNECT_END();
            }
        }

        // Check ID ///////////////////////////////////////////////////////////

        if((major2 > OLD_API_MAJOR)
        || ((major2 == OLD_API_MAJOR) && (minor2 > OLD_API_MINOR))
        || ((major2 == OLD_API_MAJOR) && (minor2 == OLD_API_MINOR) && (patch2 >= OLD_API_PATCH)))
        {
            QString arch2 = QString();
            QString *arch2Ptr = &arch2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::archString,
                this, [this, arch2Ptr] (const QString &arch) {
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
                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]")).match(arch2);

                if(match.hasMatch())
                {
                    QString board = match.captured(1);
                    QString id = match.captured(2);

                    // Skip OpenMV Cam M4's...
                    if(board != QStringLiteral("M4"))
                    {
                        QNetworkAccessManager *manager = new QNetworkAccessManager(this);

                        connect(manager, &QNetworkAccessManager::finished, this, [this, board, id] (QNetworkReply *reply) {

                            QByteArray data = reply->readAll();

                            if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
                            {
                                if(QString::fromUtf8(data).contains(QStringLiteral("<p>No</p>")))
                                {
                                    QTimer::singleShot(0, this, [this, board, id] { registerOpenMVCam(board, id); });
                                }
                            }

                            reply->deleteLater();
                        });

                        QNetworkRequest request = QNetworkRequest(QUrl(QString(QStringLiteral("http://upload.openmv.io/openmv-swd-ids-check.php?board=%L1&id=%L2")).arg(board).arg(id)));
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
                        request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
                        QNetworkReply *reply = manager->get(request);

                        if(reply)
                        {
                            connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
                        }
                    }
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Connect"),
                    tr("Timeout error while getting board architecture!"));

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
                this, [this, ok2Ptr] (bool ok) {
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
                    tr("Connect"),
                    tr("Timeout error while learning MTU!"));

                CLOSE_CONNECT_END();
            }
        }

        // Stopping ///////////////////////////////////////////////////////////

        m_iodevice->scriptStop();
        m_iodevice->jpegEnable(m_jpgCompress->isChecked());
        m_iodevice->fbEnable(!m_disableFrameBuffer->isChecked());

        Core::MessageManager::grayOutOldContent();

        ///////////////////////////////////////////////////////////////////////

        m_iodevice->getTimeout(); // clear

        m_frameSizeDumpTimer.restart();
        m_getScriptRunningTimer.restart();
        m_getTxBufferTimer.restart();

        m_timer.restart();
        m_queue.clear();
        m_connected = true;
        m_running = false;
        m_portName = selectedPort;
        m_portPath = QString();
        m_major = major2;
        m_minor = minor2;
        m_patch = patch2;
        m_errorFilterString = QString();

        m_bootloaderCommand->action()->setEnabled(false);
        m_configureSettingsCommand->action()->setEnabled(false);
        m_saveCommand->action()->setEnabled(false);
        m_resetCommand->action()->setEnabled(true);
        m_connectCommand->action()->setEnabled(false);
        m_connectCommand->action()->setVisible(false);
        m_disconnectCommand->action()->setEnabled(true);
        m_disconnectCommand->action()->setVisible(true);
        Core::IEditor *editor = Core::EditorManager::currentEditor();
        m_startCommand->action()->setEnabled(editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false);
        m_startCommand->action()->setVisible(true);
        m_stopCommand->action()->setEnabled(false);
        m_stopCommand->action()->setVisible(false);

        m_versionButton->setEnabled(true);
        m_versionButton->setText(tr("Firmware Version: %L1.%L2.%L3").arg(major2).arg(minor2).arg(patch2));
        m_portLabel->setEnabled(true);
        m_portLabel->setText(tr("Serial Port: %L1").arg(m_portName));
        m_pathButton->setEnabled(true);
        m_pathButton->setText(tr("Drive:"));
        m_fpsLabel->setEnabled(true);
        m_fpsLabel->setText(tr("FPS: 0"));

        m_frameBuffer->enableSaveTemplate(false);
        m_frameBuffer->enableSaveDescriptor(false);

        // Check Version //////////////////////////////////////////////////////

        QFile file(Core::ICore::userResourcePath() + QStringLiteral("/firmware/firmware.txt"));

        if(file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();

            if((file.error() == QFile::NoError) && (!data.isEmpty()))
            {
                file.close();

                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).match(QString::fromUtf8(data));

                if((major2 < match.captured(1).toInt())
                || ((major2 == match.captured(1).toInt()) && (minor2 < match.captured(2).toInt()))
                || ((major2 == match.captured(1).toInt()) && (minor2 == match.captured(2).toInt()) && (patch2 < match.captured(3).toInt())))
                {
                    m_versionButton->setText(m_versionButton->text().append(tr(" - [ out of date - click here to updgrade ]")));

                    QTimer::singleShot(1, this, [this] {
                        if(QMessageBox::warning(Core::ICore::dialogParent(),
                            tr("Connect"),
                            tr("Your OpenMV Cam's firmware is out of date. Would you like to upgrade?"),
                            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                        == QMessageBox::Ok)
                        {
                            OpenMVPlugin::updateCam(true);
                        }
                    });
                }
                else
                {
                    m_versionButton->setText(m_versionButton->text().append(tr(" - [ latest ]")));
                }
            }
        }

        ///////////////////////////////////////////////////////////////////////

        QTimer::singleShot(0, this, [this] { OpenMVPlugin::setPortPath(true); });

        CONNECT_END();
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Connect"),
            tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::disconnectClicked(bool reset)
{
    if(m_connected)
    {
        if(!m_working)
        {
            m_working = true;

            // Stopping ///////////////////////////////////////////////////////
            {
                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                        &loop, &QEventLoop::quit);

                if(reset)
                {
                    if(!m_portPath.isEmpty())
                    {
                        // DISALBED // Extra disk activity to flush changes...
                        // DISALBED QFile temp(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath)) + QStringLiteral("/openmv.null"));
                        // DISALBED if(temp.open(QIODevice::WriteOnly)) temp.write(QByteArray(FILE_FLUSH_BYTES, 0));
                        // DISALBED temp.remove();

#if defined(Q_OS_WIN)
                        wchar_t driveLetter[m_portPath.size()];
                        m_portPath.toWCharArray(driveLetter);

                        if(!ejectVolume(driveLetter[0]))
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Disconnect"),
                                tr("Failed to eject \"%L1\"!").arg(m_portPath));
                        }
#elif defined(Q_OS_LINUX)
                        bool ok = false;

                        DIR *dirp = opendir(m_portPath.toUtf8().constData());

                        if(dirp)
                        {
                            if(syncfs(dirfd(dirp)) >= 0)
                            {
                                ok = true;
                            }

                            if(closedir(dirp) < 0)
                            {
                                ok = false;
                            }
                        }

                        if(!ok)
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Disconnect"),
                                tr("Failed to eject \"%L1\"!").arg(m_portPath));
                        }
#elif defined(Q_OS_MAC)
                        if(sync_volume_np(m_portPath.toUtf8().constData(), SYNC_VOLUME_FULLSYNC | SYNC_VOLUME_WAIT) < 0)
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Disconnect"),
                                tr("Failed to eject \"%L1\"!").arg(m_portPath));
                        }
#endif
                    }

                    m_iodevice->sysReset();
                }
                else
                {
                    m_iodevice->scriptStop();
                }

                m_iodevice->close();

                loop.exec();
            }

            ///////////////////////////////////////////////////////////////////

            m_iodevice->getTimeout(); // clear

            m_frameSizeDumpTimer.restart();
            m_getScriptRunningTimer.restart();
            m_getTxBufferTimer.restart();

            m_timer.restart();
            m_queue.clear();
            m_connected = false;
            m_running = false;
            m_major = int();
            m_minor = int();
            m_patch = int();
            m_portName = QString();
            m_portPath = QString();
            m_errorFilterString = QString();

            m_bootloaderCommand->action()->setEnabled(true);
            m_configureSettingsCommand->action()->setEnabled(false);
            m_saveCommand->action()->setEnabled(false);
            m_resetCommand->action()->setEnabled(false);
            m_connectCommand->action()->setEnabled(true);
            m_connectCommand->action()->setVisible(true);
            m_disconnectCommand->action()->setVisible(false);
            m_disconnectCommand->action()->setEnabled(false);
            m_startCommand->action()->setEnabled(false);
            m_startCommand->action()->setVisible(true);
            m_stopCommand->action()->setEnabled(false);
            m_stopCommand->action()->setVisible(false);

            m_versionButton->setDisabled(true);
            m_versionButton->setText(tr("Firmware Version:"));
            m_portLabel->setDisabled(true);
            m_portLabel->setText(tr("Serial Port:"));
            m_pathButton->setDisabled(true);
            m_pathButton->setText(tr("Drive:"));
            m_fpsLabel->setDisabled(true);
            m_fpsLabel->setText(tr("FPS:"));

            m_frameBuffer->enableSaveTemplate(false);
            m_frameBuffer->enableSaveDescriptor(false);

            ///////////////////////////////////////////////////////////////////

            m_working = false;
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                reset ? tr("Reset") : tr("Disconnect"),
                tr("Busy... please wait..."));
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
                this, [this, running2Ptr] (bool running) {
                *running2Ptr = running;
            });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::scriptRunning,
                    &loop, &QEventLoop::quit);

            m_iodevice->getScriptRunning();

            loop.exec();

            disconnect(conn);

            if(running2)
            {
                m_iodevice->scriptStop();
            }
        }

        ///////////////////////////////////////////////////////////////////////

        QByteArray contents = Core::EditorManager::currentEditor()->document()->contents();

        if(importHelper(contents))
        {
            m_iodevice->scriptExec(contents);

            m_timer.restart();
            m_queue.clear();
        }
        else
        {
            m_fpsLabel->setText(tr("FPS: 0"));
        }

        ///////////////////////////////////////////////////////////////////////

        m_working = false;

        QTimer::singleShot(0, this, &OpenMVPlugin::workingDone);
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Start"),
            tr("Busy... please wait..."));
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
                this, [this, running2Ptr] (bool running) {
                *running2Ptr = running;
            });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::scriptRunning,
                    &loop, &QEventLoop::quit);

            m_iodevice->getScriptRunning();

            loop.exec();

            disconnect(conn);

            if(running2)
            {
                m_iodevice->scriptStop();
            }
        }

        ///////////////////////////////////////////////////////////////////////

        m_fpsLabel->setText(tr("FPS: 0"));

        ///////////////////////////////////////////////////////////////////////

        m_working = false;

        ///////////////////////////////////////////////////////////////////////

        if(Core::EditorManager::currentEditor()->document()->displayName() == QStringLiteral("helloworld_1.py"))
        {
            QTimer::singleShot(2000, this, [this] {
                QSettings *settings = ExtensionSystem::PluginManager::settings();
                settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

                if(!settings->value(QStringLiteral(DONT_SHOW_EXAMPLES_AGAIN), false).toBool())
                {
                    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    dialog->setWindowTitle(tr("More Examples"));
                    QVBoxLayout *v_layout = new QVBoxLayout(dialog);

                    QLabel *label = new QLabel(tr("New to OpenMV?\n\n"
                                                  "You can find more examples under the File -> Examples menu.\n\n"
                                                  "In particular, checkout the Color-Tracking examples."));
                    v_layout->addWidget(label);

                    QWidget *widget = new QWidget();
                    QHBoxLayout *h_layout = new QHBoxLayout(widget);
                    h_layout->setMargin(0);

                    QCheckBox *checkBox = new QCheckBox(tr("Don't show this message again."));
                    h_layout->addWidget(checkBox);

                    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok);
                    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
                    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
                    h_layout->addWidget(box);

                    v_layout->addSpacing(10);
                    v_layout->addWidget(widget);

                    dialog->exec();
                    settings->setValue(QStringLiteral(DONT_SHOW_EXAMPLES_AGAIN), checkBox->isChecked());

                    delete dialog;
                }

                settings->endGroup();
            });
        }

        ///////////////////////////////////////////////////////////////////////

        QTimer::singleShot(0, this, &OpenMVPlugin::workingDone);
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Stop"),
            tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::processEvents()
{
    if((!m_working) && m_connected)
    {
        if(m_iodevice->getTimeout())
        {
            disconnectClicked();
        }
        else
        {
            if((!m_disableFrameBuffer->isChecked()) && (!m_iodevice->frameSizeDumpQueued()) && m_frameSizeDumpTimer.hasExpired(FRAME_SIZE_DUMP_SPACING))
            {
                m_frameSizeDumpTimer.restart();
                m_iodevice->frameSizeDump();
            }

            if((!m_iodevice->getScriptRunningQueued()) && m_getScriptRunningTimer.hasExpired(GET_SCRIPT_RUNNING_SPACING))
            {
                m_getScriptRunningTimer.restart();
                m_iodevice->getScriptRunning();

                if(m_portPath.isEmpty())
                {
                    setPortPath(true);
                }
            }

            if((!m_iodevice->getTxBufferQueued()) && m_getTxBufferTimer.hasExpired(GET_TX_BUFFER_SPACING))
            {
                m_getTxBufferTimer.restart();
                m_iodevice->getTxBuffer();
            }

            if(m_timer.hasExpired(FPS_TIMER_EXPIRATION_TIME))
            {
                m_fpsLabel->setText(tr("FPS: 0"));
            }
        }
    }
}

void OpenMVPlugin::errorFilter(const QByteArray &data)
{
    m_errorFilterString.append(Utils::SynchronousProcess::normalizeNewlines(QString::fromUtf8(data)));

    QRegularExpressionMatch match;
    int index = m_errorFilterString.indexOf(m_errorFilterRegex, 0, &match);

    if(index != -1)
    {
        QString fileName = match.captured(1);
        int lineNumber = match.captured(2).toInt();
        QString errorMessage = tr(match.captured(3).toUtf8().data());

        Core::EditorManager::cutForwardNavigationHistory();
        Core::EditorManager::addCurrentPositionToNavigationHistory();

        TextEditor::BaseTextEditor *editor = Q_NULLPTR;

        if(fileName == QStringLiteral("<stdin>"))
        {
            editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::currentEditor());
        }
        else if(!m_portPath.isEmpty())
        {
            editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::openEditor(QDir::cleanPath(QDir::fromNativeSeparators(QString(QDir::separator() + fileName).prepend(m_portPath)))));
        }

        if(editor)
        {
            Core::EditorManager::addCurrentPositionToNavigationHistory();
            editor->gotoLine(lineNumber);

            QTextCursor cursor = editor->textCursor();

            if(cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor))
            {
                editor->editorWidget()->setBlockSelection(cursor);
            }

            Core::EditorManager::activateEditor(editor);
        }

        QMessageBox *box = new QMessageBox(QMessageBox::Critical, QString(), errorMessage, QMessageBox::Ok, Core::ICore::dialogParent());
        connect(box, &QMessageBox::finished, box, &QMessageBox::deleteLater);
        QTimer::singleShot(0, box, &QMessageBox::exec);

        m_errorFilterString = m_errorFilterString.mid(index + match.capturedLength(0));
    }

    m_errorFilterString = m_errorFilterString.right(ERROR_FILTER_MAX_SIZE);
}

void OpenMVPlugin::configureSettings()
{
    if(!m_working)
    {
        if(OpenMVCameraSettings(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath)) + QStringLiteral("/openmv.config")).exec() == QDialog::Accepted)
        {
            // Extra disk activity to flush changes...
            QFile temp(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath)) + QStringLiteral("/openmv.null"));
            if(temp.open(QIODevice::WriteOnly)) temp.write(QByteArray(FILE_FLUSH_BYTES, 0));
            temp.remove();
        }
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Configure Settings"),
            tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::saveScript()
{
    if(!m_working)
    {
        int answer = QMessageBox::question(Core::ICore::dialogParent(),
            tr("Save Script"),
            tr("Strip comments and convert spaces to tabs?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

        if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))
        {
            QByteArray contents = Core::EditorManager::currentEditor()->document()->contents();

            if(importHelper(contents))
            {
                Utils::FileSaver file(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath)) + QStringLiteral("/main.py"));

                if(!file.hasError())
                {
                    if(answer == QMessageBox::Yes)
                    {
                        contents = loadFilter(contents);
                    }

                    if((!file.write(contents)) || (!file.finalize()))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Save Script"),
                            tr("Error: %L1!").arg(file.errorString()));
                    }
                    else
                    {
                        // Extra disk activity to flush changes...
                        QFile temp(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath)) + QStringLiteral("/openmv.null"));
                        if(temp.open(QIODevice::WriteOnly)) temp.write(QByteArray(FILE_FLUSH_BYTES, 0));
                        temp.remove();
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("Save Script"),
                        tr("Error: %L1!").arg(file.errorString()));
                }
            }
        }
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Save Script"),
            tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::saveImage(const QPixmap &data)
{
    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    QString path =
        QFileDialog::getSaveFileName(Core::ICore::dialogParent(), tr("Save Image"),
            settings->value(QStringLiteral(LAST_SAVE_IMAGE_PATH), QDir::homePath()).toString(),
            tr("Image Files (*.bmp *.jpg *.jpeg *.png *.ppm)"));

    if(!path.isEmpty())
    {
        if(data.save(path))
        {
            settings->setValue(QStringLiteral(LAST_SAVE_IMAGE_PATH), path);
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                tr("Save Image"),
                tr("Failed to save the image file for an unknown reason!"));
        }
    }

    settings->endGroup();
}

void OpenMVPlugin::saveTemplate(const QRect &rect)
{
    if(!m_working)
    {
        QString drivePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath));

        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QString path =
            QFileDialog::getSaveFileName(Core::ICore::dialogParent(), tr("Save Template"),
                settings->value(QStringLiteral(LAST_SAVE_TEMPLATE_PATH), drivePath).toString(),
                tr("Image Files (*.bmp *.jpg *.jpeg *.pgm *.ppm)"));

        if(!path.isEmpty())
        {
            path = QDir::cleanPath(QDir::fromNativeSeparators(path));

            if((!path.startsWith(drivePath))
            || (!QDir(QFileInfo(path).path()).exists()))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Save Template"),
                    tr("Please select a valid path on the OpenMV Cam!"));
            }
            else
            {
                QByteArray sendPath = QString(path).remove(0, drivePath.size()).prepend(QLatin1Char('/')).toUtf8();

                if(sendPath.size() <= DESCRIPTOR_SAVE_PATH_MAX_LEN)
                {
                    m_iodevice->templateSave(rect.x(), rect.y(), rect.width(), rect.height(), sendPath);
                    settings->setValue(QStringLiteral(LAST_SAVE_TEMPLATE_PATH), path);
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("Save Template"),
                        tr("\"%L1\" is longer than a max length of %L2 characters!").arg(QString::fromUtf8(sendPath)).arg(DESCRIPTOR_SAVE_PATH_MAX_LEN));
                }
            }
        }

        settings->endGroup();
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Save Template"),
            tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::saveDescriptor(const QRect &rect)
{
    if(!m_working)
    {
        QString drivePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath));

        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QString path =
            QFileDialog::getSaveFileName(Core::ICore::dialogParent(), tr("Save Descriptor"),
                settings->value(QStringLiteral(LAST_SAVE_DESCRIPTOR_PATH), drivePath).toString(),
                tr("Keypoints Files (*.lbp *.orb)"));

        if(!path.isEmpty())
        {
            path = QDir::cleanPath(QDir::fromNativeSeparators(path));

            if((!path.startsWith(drivePath))
            || (!QDir(QFileInfo(path).path()).exists()))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Save Descriptor"),
                    tr("Please select a valid path on the OpenMV Cam!"));
            }
            else
            {
                QByteArray sendPath = QString(path).remove(0, drivePath.size()).prepend(QLatin1Char('/')).toUtf8();

                if(sendPath.size() <= DESCRIPTOR_SAVE_PATH_MAX_LEN)
                {
                    m_iodevice->descriptorSave(rect.x(), rect.y(), rect.width(), rect.height(), sendPath);
                    settings->setValue(QStringLiteral(LAST_SAVE_DESCRIPTOR_PATH), path);
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("Save Descriptor"),
                        tr("\"%L1\" is longer than a max length of %L2 characters!").arg(QString::fromUtf8(sendPath)).arg(DESCRIPTOR_SAVE_PATH_MAX_LEN));
                }
            }
        }

        settings->endGroup();
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Save Descriptor"),
            tr("Busy... please wait..."));
    }
}

QMap<QString, QAction *> OpenMVPlugin::aboutToShowExamplesRecursive(const QString &path, QMenu *parent, bool notExamples)
{
    QMap<QString, QAction *> actions;
    QDirIterator it(path, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    while(it.hasNext())
    {
        QString filePath = it.next();

        if(it.fileInfo().isDir())
        {
            QMenu *menu = new QMenu(it.fileName(), parent);
            QMap<QString, QAction *> menuActions = aboutToShowExamplesRecursive(filePath, menu, notExamples);
            menu->addActions(menuActions.values());
            menu->setDisabled(menuActions.values().isEmpty());
            actions.insertMulti(it.fileName(), menu->menuAction());
        }
        else
        {
            QAction *action = new QAction(it.fileName(), parent);
            connect(action, &QAction::triggered, this, [this, filePath, notExamples]
            {
                QFile file(filePath);

                if(file.open(QIODevice::ReadOnly))
                {
                    QByteArray data = file.readAll();

                    if((file.error() == QFile::NoError) && (!data.isEmpty()))
                    {
                        Core::EditorManager::cutForwardNavigationHistory();
                        Core::EditorManager::addCurrentPositionToNavigationHistory();

                        QString titlePattern = QFileInfo(filePath).baseName().simplified() + QStringLiteral("_$.") + QFileInfo(filePath).completeSuffix();
                        TextEditor::BaseTextEditor *editor = qobject_cast<TextEditor::BaseTextEditor *>(notExamples ? Core::EditorManager::openEditor(filePath) : Core::EditorManager::openEditorWithContents(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID, &titlePattern, data));

                        if(editor)
                        {
                            Core::EditorManager::addCurrentPositionToNavigationHistory();
                            if(!notExamples) editor->editorWidget()->configureGenericHighlighter();
                            Core::EditorManager::activateEditor(editor);
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                notExamples ? tr("Open File") : tr("Open Example"),
                                notExamples ? tr("Cannot open the file \"%L1\"!").arg(filePath) : tr("Cannot open the example file \"%L1\"!").arg(filePath));
                        }
                    }
                    else if(file.error() != QFile::NoError)
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            notExamples ? tr("Open File") : tr("Open Example"),
                            tr("Error: %L1!").arg(file.errorString()));
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            notExamples ? tr("Open File") : tr("Open Example"),
                            notExamples ? tr("Cannot open the file \"%L1\"!").arg(filePath) : tr("Cannot open the example file \"%L1\"!").arg(filePath));
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        notExamples ? tr("Open File") : tr("Open Example"),
                        tr("Error: %L1!").arg(file.errorString()));
                }
            });

            actions.insertMulti(it.fileName(), action);
        }
    }

    return actions;
}

void OpenMVPlugin::updateCam(bool forceYes)
{
    if(!m_working)
    {
        QFile file(Core::ICore::userResourcePath() + QStringLiteral("/firmware/firmware.txt"));

        if(file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();

            if((file.error() == QFile::NoError) && (!data.isEmpty()))
            {
                file.close();

                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).match(QString::fromUtf8(data));

                if((m_major < match.captured(1).toInt())
                || ((m_major == match.captured(1).toInt()) && (m_minor < match.captured(2).toInt()))
                || ((m_major == match.captured(1).toInt()) && (m_minor == match.captured(2).toInt()) && (m_patch < match.captured(3).toInt())))
                {
                    if(forceYes || (QMessageBox::warning(Core::ICore::dialogParent(),
                        tr("Firmware Update"),
                        tr("Update your OpenMV Cam's firmware to the latest version?"),
                        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                    == QMessageBox::Ok))
                    {
                        int answer = QMessageBox::question(Core::ICore::dialogParent(),
                            tr("Firmware Update"),
                            tr("Erase the internal file system?"),
                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);

                        if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))
                        {
                            disconnectClicked();

                            if(pluginSpec()->state() != ExtensionSystem::PluginSpec::Stopped)
                            {
                                connectClicked(true, QString(), answer == QMessageBox::Yes);
                            }
                        }
                    }
                }
                else
                {
                    QMessageBox::information(Core::ICore::dialogParent(),
                        tr("Firmware Update"),
                        tr("Your OpenMV Cam's firmware is up to date."));

                    if(QMessageBox::question(Core::ICore::dialogParent(),
                        tr("Firmware Update"),
                        tr("Need to reset your OpenMV Cam's firmware to the release version?"),
                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
                    == QMessageBox::Yes)
                    {
                        int answer = QMessageBox::question(Core::ICore::dialogParent(),
                            tr("Firmware Update"),
                            tr("Erase the internal file system?"),
                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);

                        if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))
                        {
                            disconnectClicked();

                            if(pluginSpec()->state() != ExtensionSystem::PluginSpec::Stopped)
                            {
                                connectClicked(true, QString(), answer == QMessageBox::Yes);
                            }
                        }
                    }
                }
            }
            else if(file.error() != QFile::NoError)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Firmware Update"),
                    tr("Error: %L1!").arg(file.errorString()));
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Firmware Update"),
                    tr("Cannot open firmware.txt!"));
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                tr("Firmware Update"),
                tr("Error: %L1!").arg(file.errorString()));
        }
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Firmware Update"),
            tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::setPortPath(bool silent)
{
    if(!m_working)
    {
        QStringList drives;

        foreach(const QStorageInfo &info, QStorageInfo::mountedVolumes())
        {
            if(info.isValid()
            && info.isReady()
            && (!info.isRoot())
            && (!info.isReadOnly())
            && (QString::fromUtf8(info.fileSystemType()).contains(QStringLiteral("fat"), Qt::CaseInsensitive) || QString::fromUtf8(info.fileSystemType()).contains(QStringLiteral("msdos"), Qt::CaseInsensitive))
            && ((!Utils::HostOsInfo::isMacHost()) || info.rootPath().startsWith(QStringLiteral("/volumes/"), Qt::CaseInsensitive))
            && ((!Utils::HostOsInfo::isLinuxHost()) || info.rootPath().startsWith(QStringLiteral("/media/"), Qt::CaseInsensitive) || info.rootPath().startsWith(QStringLiteral("/mnt/"), Qt::CaseInsensitive) || info.rootPath().startsWith(QStringLiteral("/run/"), Qt::CaseInsensitive)))
            {
                drives.append(info.rootPath());
            }
        }

        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SERIAL_PORT_SETTINGS_GROUP));

        if(drives.isEmpty())
        {
            if(!silent)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Select Drive"),
                    tr("No valid drives were found to associate with your OpenMV Cam!"));
            }

            m_portPath = QString();
        }
        else if(drives.size() == 1)
        {
            if(m_portPath == drives.first())
            {
                QMessageBox::information(Core::ICore::dialogParent(),
                    tr("Select Drive"),
                    tr("\"%L1\" is the only drive available so it must be your OpenMV Cam's drive.").arg(drives.first()));
            }
            else
            {
                m_portPath = drives.first();
                settings->setValue(m_portName, m_portPath);
            }
        }
        else
        {
            int index = drives.indexOf(settings->value(m_portName).toString());

            bool ok = silent;
            QString temp = silent ? drives.first() : QInputDialog::getItem(Core::ICore::dialogParent(),
                tr("Select Drive"), tr("Please associate a drive with your OpenMV Cam"),
                drives, (index != -1) ? index : 0, false, &ok,
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType() : Qt::WindowCloseButtonHint));

            if(ok)
            {
                m_portPath = temp;
                settings->setValue(m_portName, m_portPath);
            }
        }

        settings->endGroup();

        m_pathButton->setText((!m_portPath.isEmpty()) ? tr("Drive: %L1").arg(m_portPath) : tr("Drive:"));

        Core::IEditor *editor = Core::EditorManager::currentEditor();
        m_configureSettingsCommand->action()->setEnabled(!m_portPath.isEmpty());
        m_saveCommand->action()->setEnabled((!m_portPath.isEmpty()) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));

        m_frameBuffer->enableSaveTemplate(!m_portPath.isEmpty());
        m_frameBuffer->enableSaveDescriptor(!m_portPath.isEmpty());
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Select Drive"),
            tr("Busy... please wait..."));
    }
}

const int connectToSerialPortIndex = 0;
const int connectToUDPPortIndex = 1;
const int connectToTCPPortIndex = 2;

void OpenMVPlugin::openTerminalAboutToShow()
{
    m_openTerminalMenu->menu()->clear();
    connect(m_openTerminalMenu->menu()->addAction(tr("New Terminal")), &QAction::triggered, this, [this] {
        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QStringList optionList = QStringList()
            << tr("Connect to serial port")
            << tr("Connect to UDP port")
            << tr("Connect to TCP port");

        int optionListIndex = optionList.indexOf(settings->value(QStringLiteral(LAST_OPEN_TERMINAL_SELECT)).toString());

        bool optionNameOk;
        QString optionName = QInputDialog::getItem(Core::ICore::dialogParent(),
            tr("New Terminal"), tr("Please select an option"),
            optionList, (optionListIndex != -1) ? optionListIndex : 0, false, &optionNameOk,
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

        if(optionNameOk)
        {
            switch(optionList.indexOf(optionName))
            {
                case connectToSerialPortIndex:
                {
                    QStringList stringList;

                    foreach(QSerialPortInfo port, QSerialPortInfo::availablePorts())
                    {
                        stringList.append(port.portName());
                    }

                    if(Utils::HostOsInfo::isMacHost())
                    {
                        stringList = stringList.filter(QStringLiteral("cu"), Qt::CaseInsensitive);
                    }

                    int index = stringList.indexOf(settings->value(QStringLiteral(LAST_OPEN_TERMINAL_SERIAL_PORT)).toString());

                    bool portNameValueOk;
                    QString portNameValue = QInputDialog::getItem(Core::ICore::dialogParent(),
                        tr("New Terminal"), tr("Please select a serial port"),
                        stringList, (index != -1) ? index : 0, false, &portNameValueOk,
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                    if(portNameValueOk)
                    {
                        bool baudRateOk;
                        QString baudRate = QInputDialog::getText(Core::ICore::dialogParent(),
                            tr("New Terminal"), tr("Please enter a baud rate"),
                            QLineEdit::Normal, settings->value(QStringLiteral(LAST_OPEN_TERMINAL_SERIAL_PORT_BAUD_RATE), QStringLiteral("115200")).toString(), &baudRateOk,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(baudRateOk && (!baudRate.isEmpty()))
                        {
                            bool buadRateValueOk;
                            int baudRateValue = baudRate.toInt(&buadRateValueOk);

                            if(buadRateValueOk)
                            {
                                settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_SELECT), optionName);
                                settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_SERIAL_PORT), portNameValue);
                                settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_SERIAL_PORT_BAUD_RATE), baudRateValue);

                                openTerminalMenuData_t data;
                                data.displayName = tr("Serial Port - %L1 - %L2 BPS").arg(portNameValue).arg(baudRateValue);
                                data.optionIndex = connectToSerialPortIndex;
                                data.commandStr = portNameValue;
                                data.commandVal = baudRateValue;

                                if(!openTerminalMenuDataContains(data.displayName))
                                {
                                    m_openTerminalMenuData.append(data);

                                    if(m_openTerminalMenuData.size() > 10)
                                    {
                                        m_openTerminalMenuData.removeFirst();
                                    }
                                }

                                OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(data.displayName)));
                                OpenMVTerminalSerialPort *terminalDevice = new OpenMVTerminalSerialPort(terminal);

                                connect(terminal, &OpenMVTerminal::writeBytes,
                                        terminalDevice, &OpenMVTerminalPort::writeBytes);

                                connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                        terminal, &OpenMVTerminal::readBytes);

                                QString errorMessage2 = QString();
                                QString *errorMessage2Ptr = &errorMessage2;

                                QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                    this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                                    *errorMessage2Ptr = errorMessage;
                                });

                                // QProgressDialog scoping...
                                {
                                    QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                                    dialog.setWindowModality(Qt::ApplicationModal);
                                    dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                                    dialog.setCancelButton(Q_NULLPTR);
                                    QTimer::singleShot(1000, &dialog, &QWidget::show);

                                    QEventLoop loop;

                                    connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                            &loop, &QEventLoop::quit);

                                    terminalDevice->open(data.commandStr, data.commandVal);

                                    loop.exec();
                                    dialog.close();
                                }

                                disconnect(conn);

                                if(!errorMessage2.isEmpty())
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        tr("New Terminal"),
                                        tr("Error: %L1!").arg(errorMessage2));

                                    if(Utils::HostOsInfo::isLinuxHost() && errorMessage2.contains(QStringLiteral("Permission Denied"), Qt::CaseInsensitive))
                                    {
                                        QMessageBox::information(Core::ICore::dialogParent(),
                                            tr("New Terminal"),
                                            tr("Try doing:\n\nsudo adduser %L1 dialout\n\n...in a terminal and then restart your computer.").arg(Utils::Environment::systemEnvironment().userName()));
                                    }

                                    delete terminalDevice;
                                    delete terminal;
                                }
                                else
                                {
                                    terminal->show();
                                    connect(Core::ICore::instance(), &Core::ICore::coreAboutToClose,
                                            terminal, &OpenMVTerminal::close);
                                }
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("New Terminal"),
                                    tr("Invalid string: \"%L1\"!").arg(baudRate));
                            }
                        }
                    }

                    break;
                }
                case connectToUDPPortIndex:
                {
                    QMessageBox box(QMessageBox::Question, tr("New Terminal"), tr("Connect to a UDP server as a client or start a UDP Server?"), QMessageBox::Cancel, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    QPushButton *button0 = box.addButton(tr(" Connect to a Server "), QMessageBox::AcceptRole);
                    QPushButton *button1 = box.addButton(tr(" Start a Server "), QMessageBox::AcceptRole);
                    box.setDefaultButton(settings->value(QStringLiteral(LAST_OPEN_TERMINAL_UDP_TYPE_SELECT), 0).toInt() ? button1 : button0);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    if(box.clickedButton() == button0)
                    {
                        bool hostNameOk;
                        QString hostName = QInputDialog::getText(Core::ICore::dialogParent(),
                            tr("New Terminal"), tr("Please enter a IP address (or domain name) and port (e.g. xxx.xxx.xxx.xxx:xxxx)"),
                            QLineEdit::Normal, settings->value(QStringLiteral(LAST_OPEN_TERMINAL_UDP_PORT), QStringLiteral("xxx.xxx.xxx.xxx:xxxx")).toString(), &hostNameOk,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(hostNameOk && (!hostName.isEmpty()))
                        {
                            QStringList hostNameList = hostName.split(QLatin1Char(':'), QString::SkipEmptyParts);

                            if(hostNameList.size() == 2)
                            {
                                bool portValueOk;
                                QString hostNameValue = hostNameList.at(0);
                                int portValue = hostNameList.at(1).toInt(&portValueOk);

                                if(portValueOk)
                                {
                                    settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_SELECT), optionName);
                                    settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_UDP_TYPE_SELECT), 0);
                                    settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_UDP_PORT), hostName);

                                    openTerminalMenuData_t data;
                                    data.displayName = tr("UDP Client Connection - %1").arg(hostName);
                                    data.optionIndex = connectToUDPPortIndex;
                                    data.commandStr = hostNameValue;
                                    data.commandVal = portValue;

                                    if(!openTerminalMenuDataContains(data.displayName))
                                    {
                                        m_openTerminalMenuData.append(data);

                                        if(m_openTerminalMenuData.size() > 10)
                                        {
                                            m_openTerminalMenuData.removeFirst();
                                        }
                                    }

                                    OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(data.displayName)));
                                    OpenMVTerminalUDPPort *terminalDevice = new OpenMVTerminalUDPPort(terminal);

                                    connect(terminal, &OpenMVTerminal::writeBytes,
                                            terminalDevice, &OpenMVTerminalPort::writeBytes);

                                    connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                            terminal, &OpenMVTerminal::readBytes);

                                    QString errorMessage2 = QString();
                                    QString *errorMessage2Ptr = &errorMessage2;

                                    QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                        this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                                        *errorMessage2Ptr = errorMessage;
                                    });

                                    // QProgressDialog scoping...
                                    {
                                        QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                                        dialog.setWindowModality(Qt::ApplicationModal);
                                        dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                                        dialog.setCancelButton(Q_NULLPTR);
                                        QTimer::singleShot(1000, &dialog, &QWidget::show);

                                        QEventLoop loop;

                                        connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                                &loop, &QEventLoop::quit);

                                        terminalDevice->open(data.commandStr, data.commandVal);

                                        loop.exec();
                                        dialog.close();
                                    }

                                    disconnect(conn);

                                    if(!errorMessage2.isEmpty())
                                    {
                                        QMessageBox::critical(Core::ICore::dialogParent(),
                                            tr("New Terminal"),
                                            tr("Error: %L1!").arg(errorMessage2));

                                        delete terminalDevice;
                                        delete terminal;
                                    }
                                    else
                                    {
                                        terminal->show();
                                        connect(Core::ICore::instance(), &Core::ICore::coreAboutToClose,
                                                terminal, &OpenMVTerminal::close);
                                    }
                                }
                                else
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        tr("New Terminal"),
                                        tr("Invalid string: \"%L1\"!").arg(hostName));
                                }
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("New Terminal"),
                                    tr("Invalid string: \"%L1\"!").arg(hostName));
                            }
                        }
                    }
                    else if(box.clickedButton() == button1)
                    {
                        bool portValueOk;
                        int portValue = QInputDialog::getInt(Core::ICore::dialogParent(),
                            tr("New Terminal"), tr("Please enter a port number (enter 0 for any random free port)"),
                            settings->value(QStringLiteral(LAST_OPEN_TERMINAL_UDP_SERVER_PORT), 0).toInt(), 0, 65535, 1, &portValueOk,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(portValueOk)
                        {
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_SELECT), optionName);
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_UDP_TYPE_SELECT), 1);
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_UDP_SERVER_PORT), portValue);

                            openTerminalMenuData_t data;
                            data.displayName = tr("UDP Server Connection - %1").arg(portValue);
                            data.optionIndex = connectToUDPPortIndex;
                            data.commandStr = QString();
                            data.commandVal = portValue;

                            if(!openTerminalMenuDataContains(data.displayName))
                            {
                                m_openTerminalMenuData.append(data);

                                if(m_openTerminalMenuData.size() > 10)
                                {
                                    m_openTerminalMenuData.removeFirst();
                                }
                            }

                            OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(data.displayName)));
                            OpenMVTerminalUDPPort *terminalDevice = new OpenMVTerminalUDPPort(terminal);

                            connect(terminal, &OpenMVTerminal::writeBytes,
                                    terminalDevice, &OpenMVTerminalPort::writeBytes);

                            connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                    terminal, &OpenMVTerminal::readBytes);

                            QString errorMessage2 = QString();
                            QString *errorMessage2Ptr = &errorMessage2;

                            QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                                *errorMessage2Ptr = errorMessage;
                            });

                            // QProgressDialog scoping...
                            {
                                QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                                dialog.setWindowModality(Qt::ApplicationModal);
                                dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                                dialog.setCancelButton(Q_NULLPTR);
                                QTimer::singleShot(1000, &dialog, &QWidget::show);

                                QEventLoop loop;

                                connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                        &loop, &QEventLoop::quit);

                                terminalDevice->open(data.commandStr, data.commandVal);

                                loop.exec();
                                dialog.close();
                            }

                            disconnect(conn);

                            if((!errorMessage2.isEmpty()) && (!errorMessage2.startsWith(QStringLiteral("OPENMV::"))))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("New Terminal"),
                                    tr("Error: %L1!").arg(errorMessage2));

                                delete terminalDevice;
                                delete terminal;
                            }
                            else
                            {
                                if(!errorMessage2.isEmpty())
                                {
                                    terminal->setWindowTitle(terminal->windowTitle().remove(QRegularExpression(QStringLiteral(" - \\d+"))) + QString(QStringLiteral(" - %1")).arg(errorMessage2.remove(0, 8)));
                                }

                                terminal->show();
                                connect(Core::ICore::instance(), &Core::ICore::coreAboutToClose,
                                        terminal, &OpenMVTerminal::close);
                            }
                        }
                    }

                    break;
                }
                case connectToTCPPortIndex:
                {
                    QMessageBox box(QMessageBox::Question, tr("New Terminal"), tr("Connect to a TCP server as a client or start a TCP Server?"), QMessageBox::Cancel, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    QPushButton *button0 = box.addButton(tr(" Connect to a Server "), QMessageBox::AcceptRole);
                    QPushButton *button1 = box.addButton(tr(" Start a Server "), QMessageBox::AcceptRole);
                    box.setDefaultButton(settings->value(QStringLiteral(LAST_OPEN_TERMINAL_TCP_TYPE_SELECT), 0).toInt() ? button1 : button0);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    if(box.clickedButton() == button0)
                    {
                        bool hostNameOk;
                        QString hostName = QInputDialog::getText(Core::ICore::dialogParent(),
                            tr("New Terminal"), tr("Please enter a IP address (or domain name) and port (e.g. xxx.xxx.xxx.xxx:xxxx)"),
                            QLineEdit::Normal, settings->value(QStringLiteral(LAST_OPEN_TERMINAL_TCP_PORT), QStringLiteral("xxx.xxx.xxx.xxx:xxxx")).toString(), &hostNameOk,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(hostNameOk && (!hostName.isEmpty()))
                        {
                            QStringList hostNameList = hostName.split(QLatin1Char(':'), QString::SkipEmptyParts);

                            if(hostNameList.size() == 2)
                            {
                                bool portValueOk;
                                QString hostNameValue = hostNameList.at(0);
                                int portValue = hostNameList.at(1).toInt(&portValueOk);

                                if(portValueOk)
                                {
                                    settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_SELECT), optionName);
                                    settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_TCP_TYPE_SELECT), 0);
                                    settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_TCP_PORT), hostName);

                                    openTerminalMenuData_t data;
                                    data.displayName = tr("TCP Client Connection - %1").arg(hostName);
                                    data.optionIndex = connectToTCPPortIndex;
                                    data.commandStr = hostNameValue;
                                    data.commandVal = portValue;

                                    if(!openTerminalMenuDataContains(data.displayName))
                                    {
                                        m_openTerminalMenuData.append(data);

                                        if(m_openTerminalMenuData.size() > 10)
                                        {
                                            m_openTerminalMenuData.removeFirst();
                                        }
                                    }

                                    OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(data.displayName)));
                                    OpenMVTerminalTCPPort *terminalDevice = new OpenMVTerminalTCPPort(terminal);

                                    connect(terminal, &OpenMVTerminal::writeBytes,
                                            terminalDevice, &OpenMVTerminalPort::writeBytes);

                                    connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                            terminal, &OpenMVTerminal::readBytes);

                                    QString errorMessage2 = QString();
                                    QString *errorMessage2Ptr = &errorMessage2;

                                    QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                        this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                                        *errorMessage2Ptr = errorMessage;
                                    });

                                    // QProgressDialog scoping...
                                    {
                                        QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                                        dialog.setWindowModality(Qt::ApplicationModal);
                                        dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                                        dialog.setCancelButton(Q_NULLPTR);
                                        QTimer::singleShot(1000, &dialog, &QWidget::show);

                                        QEventLoop loop;

                                        connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                                &loop, &QEventLoop::quit);

                                        terminalDevice->open(data.commandStr, data.commandVal);

                                        loop.exec();
                                        dialog.close();
                                    }

                                    disconnect(conn);

                                    if(!errorMessage2.isEmpty())
                                    {
                                        QMessageBox::critical(Core::ICore::dialogParent(),
                                            tr("New Terminal"),
                                            tr("Error: %L1!").arg(errorMessage2));

                                        delete terminalDevice;
                                        delete terminal;
                                    }
                                    else
                                    {
                                        terminal->show();
                                        connect(Core::ICore::instance(), &Core::ICore::coreAboutToClose,
                                                terminal, &OpenMVTerminal::close);
                                    }
                                }
                                else
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        tr("New Terminal"),
                                        tr("Invalid string: \"%L1\"!").arg(hostName));
                                }
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("New Terminal"),
                                    tr("Invalid string: \"%L1\"!").arg(hostName));
                            }
                        }
                    }
                    else if(box.clickedButton() == button1)
                    {
                        bool portValueOk;
                        int portValue = QInputDialog::getInt(Core::ICore::dialogParent(),
                            tr("New Terminal"), tr("Please enter a port number (enter 0 for any random free port)"),
                            settings->value(QStringLiteral(LAST_OPEN_TERMINAL_TCP_SERVER_PORT), 0).toInt(), 0, 65535, 1, &portValueOk,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(portValueOk)
                        {
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_SELECT), optionName);
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_TCP_TYPE_SELECT), 1);
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_TCP_SERVER_PORT), portValue);

                            openTerminalMenuData_t data;
                            data.displayName = tr("TCP Server Connection - %1").arg(portValue);
                            data.optionIndex = connectToTCPPortIndex;
                            data.commandStr = QString();
                            data.commandVal = portValue;

                            if(!openTerminalMenuDataContains(data.displayName))
                            {
                                m_openTerminalMenuData.append(data);

                                if(m_openTerminalMenuData.size() > 10)
                                {
                                    m_openTerminalMenuData.removeFirst();
                                }
                            }

                            OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(data.displayName)));
                            OpenMVTerminalTCPPort *terminalDevice = new OpenMVTerminalTCPPort(terminal);

                            connect(terminal, &OpenMVTerminal::writeBytes,
                                    terminalDevice, &OpenMVTerminalPort::writeBytes);

                            connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                    terminal, &OpenMVTerminal::readBytes);

                            QString errorMessage2 = QString();
                            QString *errorMessage2Ptr = &errorMessage2;

                            QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                                *errorMessage2Ptr = errorMessage;
                            });

                            // QProgressDialog scoping...
                            {
                                QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                                dialog.setWindowModality(Qt::ApplicationModal);
                                dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                                dialog.setCancelButton(Q_NULLPTR);
                                QTimer::singleShot(1000, &dialog, &QWidget::show);

                                QEventLoop loop;

                                connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                        &loop, &QEventLoop::quit);

                                terminalDevice->open(data.commandStr, data.commandVal);

                                loop.exec();
                                dialog.close();
                            }

                            disconnect(conn);

                            if((!errorMessage2.isEmpty()) && (!errorMessage2.startsWith(QStringLiteral("OPENMV::"))))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("New Terminal"),
                                    tr("Error: %L1!").arg(errorMessage2));

                                delete terminalDevice;
                                delete terminal;
                            }
                            else
                            {
                                if(!errorMessage2.isEmpty())
                                {
                                    terminal->setWindowTitle(terminal->windowTitle().remove(QRegularExpression(QStringLiteral(" - \\d+"))) + QString(QStringLiteral(" - %1")).arg(errorMessage2.remove(0, 8)));
                                }

                                terminal->show();
                                connect(Core::ICore::instance(), &Core::ICore::coreAboutToClose,
                                        terminal, &OpenMVTerminal::close);
                            }
                        }
                    }

                    break;
                }
            }
        }

        settings->endGroup();
    });

    m_openTerminalMenu->menu()->addSeparator();

    for(int i = 0, j = m_openTerminalMenuData.size(); i < j; i++)
    {
        openTerminalMenuData_t data = m_openTerminalMenuData.at(i);
        connect(m_openTerminalMenu->menu()->addAction(data.displayName), &QAction::triggered, this, [this, data] {
            OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Core::Id::fromString(data.displayName)));
            OpenMVTerminalPort *terminalDevice;

            switch(data.optionIndex)
            {
                case connectToSerialPortIndex:
                {
                    terminalDevice = new OpenMVTerminalSerialPort(terminal);
                    break;
                }
                case connectToUDPPortIndex:
                {
                    terminalDevice = new OpenMVTerminalUDPPort(terminal);
                    break;
                }
                case connectToTCPPortIndex:
                {
                    terminalDevice = new OpenMVTerminalTCPPort(terminal);
                    break;
                }
                default:
                {
                    delete terminal;

                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("Open Terminal"),
                        tr("Error: Option Index!"));

                    return;
                }
            }

            connect(terminal, &OpenMVTerminal::writeBytes,
                    terminalDevice, &OpenMVTerminalPort::writeBytes);

            connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                    terminal, &OpenMVTerminal::readBytes);

            QString errorMessage2 = QString();
            QString *errorMessage2Ptr = &errorMessage2;

            QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                this, [this, errorMessage2Ptr] (const QString &errorMessage) {
                *errorMessage2Ptr = errorMessage;
            });

            // QProgressDialog scoping...
            {
                QProgressDialog dialog(tr("Connecting... (30 second timeout)"), tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                dialog.setWindowModality(Qt::ApplicationModal);
                dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                dialog.setCancelButton(Q_NULLPTR);
                QTimer::singleShot(1000, &dialog, &QWidget::show);

                QEventLoop loop;

                connect(terminalDevice, &OpenMVTerminalPort::openResult,
                        &loop, &QEventLoop::quit);

                terminalDevice->open(data.commandStr, data.commandVal);

                loop.exec();
                dialog.close();
            }

            disconnect(conn);

            if((!errorMessage2.isEmpty()) && (!errorMessage2.startsWith(QStringLiteral("OPENMV::"))))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Open Terminal"),
                    tr("Error: %L1!").arg(errorMessage2));

                delete terminalDevice;
                delete terminal;
            }
            else
            {
                if(!errorMessage2.isEmpty())
                {
                    terminal->setWindowTitle(terminal->windowTitle().remove(QRegularExpression(QStringLiteral(" - \\d+"))) + QString(QStringLiteral(" - %1")).arg(errorMessage2.remove(0, 8)));
                }

                terminal->show();
                connect(Core::ICore::instance(), &Core::ICore::coreAboutToClose,
                        terminal, &OpenMVTerminal::close);
            }
        });
    }

    if(m_openTerminalMenuData.size())
    {
        m_openTerminalMenu->menu()->addSeparator();
        connect(m_openTerminalMenu->menu()->addAction(tr("Clear Menu")), &QAction::triggered, this, [this] {
            m_openTerminalMenuData.clear();
        });
    }
}

QList<int> OpenMVPlugin::openThresholdEditor(const QVariant parameters)
{
    QMessageBox box(QMessageBox::Question, tr("Threshold Editor"), tr("Source image location?"), QMessageBox::Cancel, Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    QPushButton *button0 = box.addButton(tr(" Frame Buffer "), QMessageBox::AcceptRole);
    QPushButton *button1 = box.addButton(tr(" Image File "), QMessageBox::AcceptRole);
    box.setDefaultButton(button0);
    box.setEscapeButton(QMessageBox::Cancel);
    box.exec();

    QString drivePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath));

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    QList<int> result;

    if(box.clickedButton() == button0)
    {
        if(m_frameBuffer->pixmapValid())
        {
            ThresholdEditor editor(m_frameBuffer->pixmap(), settings->value(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE)).toByteArray(), Core::ICore::dialogParent(),
                Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint),
                ((!parameters.toList().isEmpty()) && ((parameters.toList().size() == 2) || (parameters.toList().size() == 6)))
                ? tr("The selected threshold tuple will be updated on close.")
                : QString());

            if(settings->contains(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE "_2")))
            {
                editor.setState(settings->value(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE "_2")).toList());
            }

            if(!parameters.toList().isEmpty())
            {
                QList<QVariant> list = parameters.toList();

                if(list.size() == 2) // Grayscale
                {
                    editor.setCombo(0);
                    editor.setInvert(false);
                    editor.setGMin(list.takeFirst().toInt());
                    editor.setGMax(list.takeFirst().toInt());
                }

                if(list.size() == 6) // LAB
                {
                    editor.setCombo(1);
                    editor.setInvert(false);
                    editor.setLMin(list.takeFirst().toInt());
                    editor.setLMax(list.takeFirst().toInt());
                    editor.setAMin(list.takeFirst().toInt());
                    editor.setAMax(list.takeFirst().toInt());
                    editor.setBMin(list.takeFirst().toInt());
                    editor.setBMax(list.takeFirst().toInt());
                }
            }

            // In normal mode exec always return rejected... the second statement below lets the if pass in this case.
            if((editor.exec() == QDialog::Accepted) || parameters.toList().isEmpty())
            {
                settings->setValue(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE), editor.saveGeometry());
                settings->setValue(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE "_2"), editor.getState());
                result = QList<int>()
                << editor.getGMin()
                << editor.getGMax()
                << editor.getLMin()
                << editor.getLMax()
                << editor.getAMin()
                << editor.getAMax()
                << editor.getBMin()
                << editor.getBMax();
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                tr("Threshold Editor"),
                tr("No image loaded!"));
        }
    }
    else if(box.clickedButton() == button1)
    {
        QString path =
            QFileDialog::getOpenFileName(Core::ICore::dialogParent(), tr("Image File"),
                settings->value(QStringLiteral(LAST_THRESHOLD_EDITOR_PATH), drivePath.isEmpty() ? QDir::homePath() : drivePath).toString(),
                tr("Image Files (*.bmp *.jpg *.jpeg *.png *.ppm)"));

        if(!path.isEmpty())
        {
            QPixmap pixmap = QPixmap(path);

            ThresholdEditor editor(pixmap, settings->value(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE)).toByteArray(), Core::ICore::dialogParent(),
                Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint),
                ((!parameters.toList().isEmpty()) && ((parameters.toList().size() == 2) || (parameters.toList().size() == 6)))
                ? tr("The selected threshold tuple will be updated on close.")
                : QString());

            if(settings->contains(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE "_2")))
            {
                editor.setState(settings->value(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE "_2")).toList());
            }

            if(!parameters.toList().isEmpty())
            {
                QList<QVariant> list = parameters.toList();

                if(list.size() == 2) // Grayscale
                {
                    editor.setCombo(0);
                    editor.setInvert(false);
                    editor.setGMin(list.takeFirst().toInt());
                    editor.setGMax(list.takeFirst().toInt());
                }

                if(list.size() == 6) // LAB
                {
                    editor.setCombo(1);
                    editor.setInvert(false);
                    editor.setLMin(list.takeFirst().toInt());
                    editor.setLMax(list.takeFirst().toInt());
                    editor.setAMin(list.takeFirst().toInt());
                    editor.setAMax(list.takeFirst().toInt());
                    editor.setBMin(list.takeFirst().toInt());
                    editor.setBMax(list.takeFirst().toInt());
                }
            }

            // In normal mode exec always return rejected... the second statement below lets the if pass in this case.
            if((editor.exec() == QDialog::Accepted) || parameters.toList().isEmpty())
            {
                settings->setValue(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE), editor.saveGeometry());
                settings->setValue(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE "_2"), editor.getState());
                settings->setValue(QStringLiteral(LAST_THRESHOLD_EDITOR_PATH), path);
                result = QList<int>()
                << editor.getGMin()
                << editor.getGMax()
                << editor.getLMin()
                << editor.getLMax()
                << editor.getAMin()
                << editor.getAMax()
                << editor.getBMin()
                << editor.getBMax();
            }
        }
    }

    settings->endGroup();

    return result;
}

void OpenMVPlugin::openKeypointsEditor()
{
    QMessageBox box(QMessageBox::Question, tr("Keypoints Editor"), tr("What would you like to do?"), QMessageBox::Cancel, Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    QPushButton *button0 = box.addButton(tr(" Edit File "), QMessageBox::AcceptRole);
    QPushButton *button1 = box.addButton(tr(" Merge Files "), QMessageBox::AcceptRole);
    box.setDefaultButton(button0);
    box.setEscapeButton(QMessageBox::Cancel);
    box.exec();

    QString drivePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath));

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    if(box.clickedButton() == button0)
    {
        QString path =
            QFileDialog::getOpenFileName(Core::ICore::dialogParent(), tr("Edit Keypoints"),
                settings->value(QStringLiteral(LAST_EDIT_KEYPOINTS_PATH), drivePath.isEmpty() ? QDir::homePath() : drivePath).toString(),
                tr("Keypoints Files (*.lbp *.orb)"));

        if(!path.isEmpty())
        {
            QScopedPointer<Keypoints> ks(Keypoints::newKeypoints(path));

            if(ks)
            {
                QString name = QFileInfo(path).completeBaseName();
                QStringList list = QDir(QFileInfo(path).path()).entryList(QStringList()
                    << (name + QStringLiteral(".bmp"))
                    << (name + QStringLiteral(".jpg"))
                    << (name + QStringLiteral(".jpeg"))
                    << (name + QStringLiteral(".ppm"))
                    << (name + QStringLiteral(".pgm"))
                    << (name + QStringLiteral(".pbm")),
                    QDir::Files,
                    QDir::Name);

                if(!list.isEmpty())
                {
                    QString pixmapPath = QFileInfo(path).path() + QDir::separator() + list.first();
                    QPixmap pixmap = QPixmap(pixmapPath);

                    KeypointsEditor editor(ks.data(), pixmap, settings->value(QStringLiteral(LAST_EDIT_KEYPOINTS_STATE)).toByteArray(), Core::ICore::dialogParent(),
                        Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                    if(editor.exec() == QDialog::Accepted)
                    {
                        if(QFile::exists(path + QStringLiteral(".bak")))
                        {
                            QFile::remove(path + QStringLiteral(".bak"));
                        }

                        if(QFile::exists(pixmapPath + QStringLiteral(".bak")))
                        {
                            QFile::remove(pixmapPath + QStringLiteral(".bak"));
                        }

                        if(QFile::copy(path, path + QStringLiteral(".bak"))
                        && QFile::copy(pixmapPath, pixmapPath + QStringLiteral(".bak"))
                        && ks->saveKeypoints(path))
                        {
                            settings->setValue(QStringLiteral(LAST_EDIT_KEYPOINTS_STATE), editor.saveGeometry());
                            settings->setValue(QStringLiteral(LAST_EDIT_KEYPOINTS_PATH), path);
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Save Edited Keypoints"),
                                tr("Failed to save the edited keypoints for an unknown reason!"));
                        }
                    }
                    else
                    {
                        settings->setValue(QStringLiteral(LAST_EDIT_KEYPOINTS_STATE), editor.saveGeometry());
                        settings->setValue(QStringLiteral(LAST_EDIT_KEYPOINTS_PATH), path);
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("Edit Keypoints"),
                        tr("Failed to find the keypoints image file!"));
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Edit Keypoints"),
                    tr("Failed to load the keypoints file for an unknown reason!"));
            }
        }
    }
    else if(box.clickedButton() == button1)
    {
        QStringList paths =
            QFileDialog::getOpenFileNames(Core::ICore::dialogParent(), tr("Merge Keypoints"),
                settings->value(QStringLiteral(LAST_MERGE_KEYPOINTS_OPEN_PATH), drivePath.isEmpty() ? QDir::homePath() : drivePath).toString(),
                tr("Keypoints Files (*.lbp *.orb)"));

        if(!paths.isEmpty())
        {
            QString first = paths.takeFirst();
            QScopedPointer<Keypoints> ks(Keypoints::newKeypoints(first));

            if(ks)
            {
                foreach(const QString &path, paths)
                {
                    ks->mergeKeypoints(path);
                }

                QString path =
                    QFileDialog::getSaveFileName(Core::ICore::dialogParent(), tr("Save Merged Keypoints"),
                        settings->value(QStringLiteral(LAST_MERGE_KEYPOINTS_SAVE_PATH), drivePath).toString(),
                        tr("Keypoints Files (*.lbp *.orb)"));

                if(!path.isEmpty())
                {
                    if(ks->saveKeypoints(path))
                    {
                        settings->setValue(QStringLiteral(LAST_MERGE_KEYPOINTS_OPEN_PATH), first);
                        settings->setValue(QStringLiteral(LAST_MERGE_KEYPOINTS_SAVE_PATH), path);
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Save Merged Keypoints"),
                            tr("Failed to save the merged keypoints for an unknown reason!"));
                    }
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Merge Keypoints"),
                    tr("Failed to load the first keypoints file for an unknown reason!"));
            }
        }
    }

    settings->endGroup();
}

void OpenMVPlugin::openAprilTagGenerator(apriltag_family_t *family)
{
    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setWindowTitle(tr("AprilTag Generator"));
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(tr("What tag images from the %L1 tag family do you want to generate?").arg(QString::fromUtf8(family->name).toUpper())));

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    QWidget *temp = new QWidget();
    QHBoxLayout *tempLayout = new QHBoxLayout(temp);
    tempLayout->setMargin(0);

    QWidget *minTemp = new QWidget();
    QFormLayout *minTempLayout = new QFormLayout(minTemp);
    minTempLayout->setMargin(0);
    QSpinBox *minRange = new QSpinBox();
    minRange->setMinimum(0);
    minRange->setMaximum(family->ncodes - 1);
    minRange->setValue(settings->value(QStringLiteral(LAST_APRILTAG_RANGE_MIN), 0).toInt());
    minRange->setAccelerated(true);
    minTempLayout->addRow(tr("Min (%1)").arg(0), minRange); // don't use %L1 here
    tempLayout->addWidget(minTemp);

    QWidget *maxTemp = new QWidget();
    QFormLayout *maxTempLayout = new QFormLayout(maxTemp);
    maxTempLayout->setMargin(0);
    QSpinBox *maxRange = new QSpinBox();
    maxRange->setMinimum(0);
    maxRange->setMaximum(family->ncodes - 1);
    maxRange->setValue(settings->value(QStringLiteral(LAST_APRILTAG_RANGE_MAX), family->ncodes - 1).toInt());
    maxRange->setAccelerated(true);
    maxTempLayout->addRow(tr("Max (%1)").arg(family->ncodes - 1), maxRange); // don't use %L1 here
    tempLayout->addWidget(maxTemp);

    layout->addWidget(temp);

    QCheckBox *checkBox = new QCheckBox(tr("Inlcude tag family and ID number in the image"));
    checkBox->setCheckable(true);
    checkBox->setChecked(settings->value(QStringLiteral(LAST_APRILTAG_INCLUDE), true).toBool());
    layout->addWidget(checkBox);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(box);

    if(dialog->exec() == QDialog::Accepted)
    {
        int min = qMin(minRange->value(), maxRange->value());
        int max = qMax(minRange->value(), maxRange->value());
        int number = max - min + 1;
        bool include = checkBox->isChecked();

        QString path =
            QFileDialog::getExistingDirectory(Core::ICore::dialogParent(), tr("AprilTag Generator - Where do you want to save %n tag image(s) to?", "", number),
                settings->value(QStringLiteral(LAST_APRILTAG_PATH), QDir::homePath()).toString());

        if(!path.isEmpty())
        {
            QProgressDialog progress(tr("Generating images..."), tr("Cancel"), 0, number - 1, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
            progress.setWindowModality(Qt::ApplicationModal);

            for(int i = 0; i < number; i++)
            {
                progress.setValue(i);

                QImage image(family->d + 4, family->d + 4, QImage::Format_Grayscale8);

                for(uint32_t y = 0; y < (family->d + 4); y++)
                {
                    for(uint32_t x = 0; x < (family->d + 4); x++)
                    {
                        if((x == 0) || (x == (family->d + 3)) || (y == 0) || (y == (family->d + 3)))
                        {
                            image.setPixel(x, y, -1);
                        }
                        else if((x == 1) || (x == (family->d + 2)) || (y == 1) || (y == (family->d + 2)))
                        {
                            image.setPixel(x, y, family->black_border ? 0 : -1);
                        }
                        else
                        {
                            image.setPixel(x, y, ((family->codes[min + i] >> (((family->d + 1 - y) * family->d) + (family->d + 1 - x))) & 1) ? -1 : 0);
                        }
                    }
                }

                QPixmap pixmap(816, include ? 1056 : 816); // 8" x 11" (96 DPI)
                pixmap.fill();

                QPainter painter;

                if(!painter.begin(&pixmap))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("AprilTag Generator"),
                        tr("Painting - begin failed!"));

                    progress.cancel();
                    break;
                }

                QFont font = painter.font();
                font.setPointSize(40);
                painter.setFont(font);

                painter.setBrush(QBrush(Qt::black, Qt::SolidPattern));
                painter.drawRect(0, 0, 816, 816);
                painter.drawImage(68, 68, image.scaled(680, 680, Qt::KeepAspectRatio, Qt::FastTransformation));

                if(include)
                {
                    painter.drawText(0 + 8, 8 + 800 + 8 + 80, 800, 80, Qt::AlignHCenter | Qt::AlignVCenter, QString::fromUtf8(family->name).toUpper() + QString(QStringLiteral(" - %1")).arg(min + i));
                }

                if(!painter.end())
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("AprilTag Generator"),
                        tr("Painting - end failed!"));

                    progress.cancel();
                    break;
                }

                if(!pixmap.save(path + QDir::separator() + QString::fromUtf8(family->name).toLower() + QString(QStringLiteral("_%1.png")).arg(min + i)))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("AprilTag Generator"),
                        tr("Failed to save the image file for an unknown reason!"));

                    progress.cancel();
                }

                if(progress.wasCanceled())
                {
                    break;
                }
            }

            if(!progress.wasCanceled())
            {
                settings->setValue(QStringLiteral(LAST_APRILTAG_RANGE_MIN), min);
                settings->setValue(QStringLiteral(LAST_APRILTAG_RANGE_MAX), max);
                settings->setValue(QStringLiteral(LAST_APRILTAG_INCLUDE), include);
                settings->setValue(QStringLiteral(LAST_APRILTAG_PATH), path);

                QMessageBox::information(Core::ICore::dialogParent(),
                    tr("AprilTag Generator"),
                    tr("Generation complete!"));
            }
        }
    }

    settings->endGroup();
    delete dialog;
    free(family->name);
    free(family->codes);
    free(family);
}

QByteArray loadFilter(const QByteArray &data)
{
    QString data2 = QString::fromUtf8(data);
    data2.remove(QRegularExpression(QStringLiteral("^\\s*?\n"), QRegularExpression::MultilineOption));
    data2.remove(QRegularExpression(QStringLiteral("^\\s*#.*?\n"), QRegularExpression::MultilineOption));
    data2.remove(QRegularExpression(QStringLiteral("\\s*#.*?$"), QRegularExpression::MultilineOption));
    return data2.replace(QStringLiteral("    "), QStringLiteral("\t")).toUtf8();
}

importDataList_t loadFolder(const QString &path)
{
    importDataList_t list;

    QDirIterator it(path, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    while(it.hasNext())
    {
        QString pathName = it.next();

        if(QFileInfo(pathName).isDir())
        {
            QString initName = pathName + QStringLiteral("/__init__.py");

            if(QFileInfo(initName).exists() && QFileInfo(initName).isFile())
            {
                importData_t data;
                data.moduleName = QDir(pathName).dirName();
                data.modulePath = pathName;
                data.moduleHash = QByteArray();

                QDirIterator it2(pathName, QDir::Files, QDirIterator::Subdirectories);

                while(it2.hasNext())
                {
                    QFile file(it2.next());

                    if(file.open(QIODevice::ReadOnly))
                    {
                        data.moduleHash.append(loadFilter(file.readAll()));
                    }
                }

                data.moduleHash = QCryptographicHash::hash(data.moduleHash, QCryptographicHash::Sha1);
                list.append(data);
            }
            else
            {
                list.append(loadFolder(pathName));
            }
        }
        else if(QFileInfo(pathName).isFile() && pathName.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
        {
            QFile file(pathName);

            if(file.open(QIODevice::ReadOnly))
            {
                importData_t data;
                data.moduleName = QFileInfo(pathName).baseName();
                data.modulePath = pathName;
                data.moduleHash = QCryptographicHash::hash(loadFilter(file.readAll()), QCryptographicHash::Sha1);
                list.append(data);
            }
        }
    }

    return list;
}

void OpenMVPlugin::parseImports(const QString &fileText, const QString &moduleFolder, const QStringList &builtInModules, importDataList_t &targetModules, QStringList &errorModules)
{
    QRegularExpression importFromRegex(QStringLiteral("(import|from)\\s+(.*)"));
    QRegularExpression importAsRegex(QStringLiteral("\\s+as\\s+.*"));
    QRegularExpression fromImportRegex(QStringLiteral("\\s+import\\s+.*"));

    QStringList fileTextList = QStringList() << fileText;
    QStringList fileTextPathList = QStringList() << moduleFolder;

    while(fileTextList.size())
    {
        QStringList lineList = fileTextList.takeFirst().replace(QRegularExpression(QStringLiteral("\\s*[\\\\\\s]+[\r\n]+\\s*")), QStringLiteral(" ")).split(QRegularExpression(QStringLiteral("[\r\n;]")), QString::SkipEmptyParts);
        QString lineListPath = fileTextPathList.takeFirst();

        foreach(const QString &line, lineList)
        {
            QRegularExpressionMatch importFromRegexMatch = importFromRegex.match(line);

            if(importFromRegexMatch.hasMatch())
            {
                QStringList importLineList = importFromRegexMatch.captured(2).remove(importAsRegex).remove(fromImportRegex).split(QLatin1Char(','), QString::SkipEmptyParts);

                foreach(const QString &importLine, importLineList)
                {
                    QString importLinePath = importLine.simplified().split(QLatin1Char('.'), QString::SkipEmptyParts).takeFirst();

                    if(!builtInModules.contains(importLinePath))
                    {
                        bool contains = false;

                        foreach(const importData_t &data, targetModules)
                        {
                            if(data.moduleName == importLinePath) 
                            {
                                contains = true;
                                break;
                            }
                        }

                        if((!contains) && (!errorModules.contains(importLinePath)))
                        {
                            if(!m_portPath.isEmpty())
                            {
                                QFileInfo infoF(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + lineListPath + QDir::separator() + importLinePath + QStringLiteral(".py"))));

                                if((!infoF.exists()) || (!infoF.isFile()))
                                {
                                    QFileInfo infoD(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + importLinePath + QStringLiteral("/__init__.py"))));

                                    if(infoD.exists() && infoD.isFile())
                                    {
                                        importData_t data;
                                        data.moduleName = importLinePath;
                                        data.modulePath = infoD.path();
                                        data.moduleHash = QByteArray();

                                        QDirIterator it2(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + importLinePath)), QDir::Files, QDirIterator::Subdirectories);

                                        while(it2.hasNext())
                                        {
                                            QString filePath = it2.next();
                                            QFile file(filePath);

                                            if(file.open(QIODevice::ReadOnly))
                                            {
                                                QByteArray bytes = loadFilter(file.readAll());

                                                if(filePath.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
                                                {
                                                    fileTextList.append(QString::fromUtf8(bytes));
                                                    fileTextPathList.append(QDir(m_portPath).relativeFilePath(filePath));
                                                }

                                                data.moduleHash.append(bytes);
                                            }
                                        }

                                        data.moduleHash = QCryptographicHash::hash(data.moduleHash, QCryptographicHash::Sha1);
                                        targetModules.append(data);
                                    }
                                    else
                                    {
                                        errorModules.append(importLinePath);
                                    }
                                }
                                else
                                {
                                    QFile file(infoF.filePath());

                                    if(file.open(QIODevice::ReadOnly))
                                    {
                                        QByteArray bytes = loadFilter(file.readAll());
                                        fileTextList.append(QString::fromUtf8(bytes));
                                        fileTextPathList.append(QDir(m_portPath).relativeFilePath(infoF.path()));

                                        importData_t data;
                                        data.moduleName = importLinePath;
                                        data.modulePath = infoF.filePath();
                                        data.moduleHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
                                        targetModules.append(data);
                                    }
                                }
                            }
                            else
                            {
                                // Can't really do anything...
                            }
                        }
                    }
                }
            }
        }
    }
}

static bool myCopyHelper(const QFileInfo &src, const QFileInfo &dst, QString *error)
{
    QFile file(src.filePath());

    if(file.open(QIODevice::ReadOnly))
    {
        QTemporaryFile temp;

        if(temp.open())
        {
            QByteArray data = loadFilter(file.readAll());

            if(temp.write(data) == data.size())
            {
                temp.close();

                if(QFile::copy(QFileInfo(temp).filePath(), dst.filePath()))
                {
                    return true;
                }
                else
                {
                    if(error) *error = QObject::tr("Copy Failed!");
                }
            }
            else
            {
                if(error) *error = temp.errorString();
            }
        }
        else
        {
            if(error) *error = temp.errorString();
        }
    }
    else
    {
        if(error) *error = file.errorString();
    }

    return false;
}

static bool myCopy(const QString &src, const QString &dst)
{
    QFile file(src);

    if(file.open(QIODevice::ReadOnly))
    {
        QTemporaryFile temp;

        if(temp.open())
        {
            QByteArray data = loadFilter(file.readAll());

            if(temp.write(data) == data.size())
            {
                temp.close();

                if(QFile::copy(QFileInfo(temp).filePath(), dst))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool OpenMVPlugin::importHelper(const QByteArray &text)
{
    QMap<QString, importData_t> map;

    foreach(const importData_t &data, m_exampleModules)
    {
        map.insert(data.moduleName, data);
    }

    foreach(const importData_t &data, m_documentsModules)
    {
        map.insert(data.moduleName, data); // Documents Folder Modules override Examples Modules
    }

    QStringList builtInModules;

    foreach(const documentation_t module, m_modules)
    {
        builtInModules.append(module.name);
    }

    importDataList_t targetModules;
    QStringList errorModules;
    parseImports(QString::fromUtf8(loadFilter(text)), QDir::separator(), builtInModules, targetModules, errorModules);

    while(!targetModules.isEmpty())
    {
        importData_t targetModule = targetModules.takeFirst();

        if(map.contains(targetModule.moduleName) && (targetModule.moduleHash != map.value(targetModule.moduleName).moduleHash))
        {
            int answer = QMessageBox::question(Core::ICore::dialogParent(),
                tr("Import Helper"),
                tr("Module \"%L1\" on your OpenMV Cam is different than the copy on your computer.\n\nWould you like OpenMV IDE to update the module on your OpenMV Cam?").arg(targetModule.moduleName),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

            if(answer == QMessageBox::Yes)
            {
                QString sourcePath = map.value(targetModule.moduleName).modulePath;

                if(QFileInfo(sourcePath).isDir())
                {
                    QString targetPath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + targetModule.moduleName));

                    QString error;

                    if(!Utils::FileUtils::removeRecursively(Utils::FileName::fromString(targetPath), &error))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to remove \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    if(!Utils::FileUtils::copyRecursively(Utils::FileName::fromString(sourcePath), Utils::FileName::fromString(targetPath), &error, myCopyHelper))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    QDirIterator it(sourcePath, QDir::Files, QDirIterator::Subdirectories);

                    while(it.hasNext())
                    {
                        QFile file(it.next());

                        if(file.open(QIODevice::ReadOnly))
                        {
                            parseImports(QString::fromUtf8(loadFilter(file.readAll())), QDir::separator(), builtInModules, targetModules, errorModules);
                        }
                    }
                }
                else
                {
                    QString targetPath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + targetModule.moduleName + QStringLiteral(".py")));

                    if(!QFile::remove(targetPath))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to remove \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    if(!myCopy(sourcePath, targetPath))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    QFile file(sourcePath);

                    if(file.open(QIODevice::ReadOnly))
                    {
                        parseImports(QString::fromUtf8(loadFilter(file.readAll())), QDir::separator(), builtInModules, targetModules, errorModules);
                    }
                }
            }
            else if(answer == QMessageBox::No)
            {
                int answer = QMessageBox::question(Core::ICore::dialogParent(),
                    tr("Import Helper"),
                    tr("Would you like OpenMV IDE to update the module on your computer?"),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

                if(answer == QMessageBox::Yes)
                {
                    QString targetPath = map.value(targetModule.moduleName).modulePath;

                    if(Utils::FileName::fromString(targetPath).isChildOf(QDir(Core::ICore::userResourcePath() + QStringLiteral("/examples"))))
                    {
                        targetPath = QDir::cleanPath(QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV/") + (QFileInfo(targetPath).isDir() ? QDir(targetPath).dirName() : QFileInfo(targetPath).fileName())));
                    }

                    if(QFileInfo(targetPath).isDir())
                    {
                        QString sourcePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + targetModule.moduleName));

                        QString error;

                        if(QDir(targetPath).exists()) // May not exist if copying from examples to documents folder.
                        {
                            if(!Utils::FileUtils::removeRecursively(Utils::FileName::fromString(targetPath), &error))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Import Helper"),
                                    tr("Failed to remove \"%L1\"!").arg(targetPath));

                                continue;
                            }
                        }

                        if(!Utils::FileUtils::copyRecursively(Utils::FileName::fromString(sourcePath), Utils::FileName::fromString(targetPath), &error)) // Don't use myCopyHelper() here...
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Import Helper"),
                                tr("Failed to create \"%L1\"!").arg(targetPath));

                            continue;
                        }
                    }
                    else
                    {
                        QString sourcePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + targetModule.moduleName + QStringLiteral(".py")));

                        if(QFileInfo(targetPath).exists()) // May not exist if copying from examples to documents folder.
                        {
                            if(!QFile::remove(targetPath))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Import Helper"),
                                    tr("Failed to remove \"%L1\"!").arg(targetPath));

                                continue;
                            }
                        }

                        if(!QFile::copy(sourcePath, targetPath)) // Don't use myCopy() here...
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Import Helper"),
                                tr("Failed to create \"%L1\"!").arg(targetPath));

                            continue;
                        }
                    }
                }
                else if(answer == QMessageBox::Cancel)
                {
                    return false;
                }
            }
            else if(answer == QMessageBox::Cancel)
            {
                return false;
            }
        }
    }

    while(!errorModules.isEmpty())
    {
        QString errorModule = errorModules.takeFirst();

        if(map.contains(errorModule))
        {
            int answer = QMessageBox::question(Core::ICore::dialogParent(),
                tr("Import Helper"),
                tr("Module \"%L1\" may be required to run your script.\n\nWould you like OpenMV IDE to copy it to your OpenMV Cam?").arg(errorModule),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

            if(answer == QMessageBox::Yes)
            {
                QString sourcePath = map.value(errorModule).modulePath;

                if(QFileInfo(sourcePath).isDir())
                {
                    QString targetPath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + errorModule));

                    QString error;

                    if(!Utils::FileUtils::copyRecursively(Utils::FileName::fromString(sourcePath), Utils::FileName::fromString(targetPath), &error, myCopyHelper))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    QDirIterator it(sourcePath, QDir::Files, QDirIterator::Subdirectories);

                    while(it.hasNext())
                    {
                        QFile file(it.next());

                        if(file.open(QIODevice::ReadOnly))
                        {
                            parseImports(QString::fromUtf8(loadFilter(file.readAll())), QDir::separator(), builtInModules, targetModules, errorModules);
                        }
                    }
                }
                else
                {
                    QString targetPath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + errorModule + QStringLiteral(".py")));

                    if(!QDir().mkpath(QFileInfo(targetPath).path()))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(QFileInfo(targetPath).path()));

                        continue;
                    }

                    if(!myCopy(sourcePath, targetPath))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    QFile file(sourcePath);

                    if(file.open(QIODevice::ReadOnly))
                    {
                        parseImports(QString::fromUtf8(loadFilter(file.readAll())), QDir::separator(), builtInModules, targetModules, errorModules);
                    }
                }
            }
            else if(answer == QMessageBox::Cancel)
            {
                return false;
            }
        }
    }

    return true;
}

} // namespace Internal
} // namespace OpenMV
