#include "openmvplugin.h"

#include "app/app_version.h"

#include "openmvtr.h"

namespace OpenMV {
namespace Internal {

OpenMVPlugin::OpenMVPlugin() : IPlugin()
{
    qRegisterMetaType<OpenMVPluginSerialPortCommand>("OpenMVPluginSerialPortCommand");
    qRegisterMetaType<OpenMVPluginSerialPortCommandResult>("OpenMVPluginSerialPortCommandResult");

    m_autoConnect = false;
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
    m_boardType = QString();
    m_boardId = QString();
    m_boardVID = 0;
    m_boardPID = 0;
    m_sensorType = QString();
    m_reconnects = int();
    m_portName = QString();
    m_portPath = QString();
    m_formKey = QString();

    m_serialNumberFilter = QString();
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

static bool copyOperator(const Utils::FilePath &src, const Utils::FilePath &dest, QString *error)
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

void OpenMVPlugin::processDocumentationMatch(const QRegularExpressionMatch &match,
                                             QStringList &providerVariables,
                                             QStringList &providerClasses, QMap<QString, QStringList> &providerClassArgs,
                                             QStringList &providerFunctions, QMap<QString, QStringList> &providerFunctionArgs,
                                             QStringList &providerMethods, QMap<QString, QStringList> &providerMethodArgs)
{
    QString type = match.captured(1);
    QString id = match.captured(2);
    QString head = match.captured(3);
    QString body = match.captured(4);
    QStringList idList = id.split(QLatin1Char('.'), Qt::SkipEmptyParts);

    if((1 <= idList.size()) && (idList.size() <= 5))
    {
        QRegularExpressionMatch args = m_argumentRegEx.match(head);
        QString argumentString;

        if(args.hasMatch())
        {
            argumentString = QLatin1Char('(') + QString(args.captured(1)).
            remove(m_emRegEx).
            remove(m_spanRegEx).
            remove(QLatin1String("</em>")).
            remove(QLatin1String("</span>")).
            replace(QStringLiteral("[,"), QStringLiteral(" [ ,")) + QLatin1Char(')');
        }

        if(idList.size() == 1)
        {
            idList.prepend(QStringLiteral("builtin"));
        }

        if(idList.size() == 2 && (type == QStringLiteral("method")))
        {
            idList.prepend(QStringLiteral("builtin"));
        }

        QRegularExpressionMatch cdfmRegExInsideMatch = m_cdfmRegExInside.match(body);

        if(cdfmRegExInsideMatch.hasMatch())
        {
            processDocumentationMatch(cdfmRegExInsideMatch,
                                      providerVariables,
                                      providerClasses, providerClassArgs,
                                      providerFunctions, providerFunctionArgs,
                                      providerMethods, providerMethodArgs);
            body.remove(m_cdfmRegExInside);
        }

        documentation_t d;
        d.moduleName = (idList.size() > 1) ? idList.at(0) : QString();
        if(idList.size() > 1) idList.removeAll(d.moduleName);
        d.className = (idList.size() > 1) ? idList.at(0) : QString();
        d.name = (idList.size() > 0) ? idList.last() : d.moduleName;
        d.text = QString(QStringLiteral("<h3>%1%2</h3>%3")).arg(d.moduleName.isEmpty() ? d.name : (d.moduleName + QStringLiteral(" - ") + (d.className.isEmpty() ? d.name : (d.className + QLatin1Char('.') + d.name)))).arg(argumentString).arg(body).
                 remove(QStringLiteral("\u00B6")).
                 remove(m_spanRegEx).
                 remove(QStringLiteral("</span>")).
                 remove(m_linkRegEx).
                 remove(QStringLiteral("</a>")).
                 remove(m_classRegEx).
                 replace(QStringLiteral("<li><p>"), QStringLiteral("<li>")).
                 replace(QStringLiteral("</p></li>"), QStringLiteral("</li>")).
                 remove(QStringLiteral("<blockquote>")).
                 remove(QStringLiteral("</blockquote>"));

        if(QString(d.text).remove(QRegularExpression(QStringLiteral("<h3>.+?</h3>"))).isEmpty())
        {
            return;
        }

        if((type == QStringLiteral("class")) || (type == QStringLiteral("exception")))
        {
            m_classes.append(d);
            providerClasses.append(d.name);
        }
        else if(type == QStringLiteral("data"))
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
            providerMethods.append(d.name);
        }

        if(args.hasMatch())
        {
            QStringList list;

            foreach(const QString &arg, QString(args.captured(1)).
                                        remove(QLatin1String("<span class=\"optional\">[</span>")).
                                        remove(QLatin1String("<span class=\"optional\">]</span>")).
                                        remove(m_emRegEx).
                                        remove(m_spanRegEx).
                                        remove(QLatin1String("</em>")).
                                        remove(QLatin1String("</span>")).
                                        remove(QLatin1Char('[')).
                                        remove(QLatin1Char(']')).
                                        remove(m_tupleRegEx).
                                        remove(m_listRegEx).
                                        remove(m_dictionaryRegEx).
                                        remove(QLatin1Char(' ')).
                                        split(QLatin1Char(','), Qt::SkipEmptyParts))
            {
                int equals = arg.indexOf(QLatin1Char('='));
                QString temp = (equals != -1) ? arg.left(equals) : arg;

                m_arguments.insert(temp);
                list.append(temp);
            }

            if(type == QStringLiteral("class"))
            {
                providerClassArgs.insert(d.name, list);
            }
            else if(type == QStringLiteral("function"))
            {
                providerFunctionArgs.insert(d.name, list);
            }
            else if(type == QStringLiteral("method"))
            {
                providerMethodArgs.insert(d.name, list);
            }
        }
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
                displayError(Tr::tr("Invalid argument (%1) for -override_read_timeout").arg(arguments.at(index_override_read_timeout + 1)));
                exit(-1);
            }
        }
        else
        {
            displayError(Tr::tr("Missing argument for -override_read_timeout"));
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
                displayError(Tr::tr("Invalid argument (%1) for -override_read_stall_timeout").arg(arguments.at(index_override_read_stall_timeout + 1)));
                exit(-1);
            }
        }
        else
        {
            displayError(Tr::tr("Missing argument for -override_read_stall_timeout"));
            exit(-1);
        }
    }

    if(arguments.contains(QStringLiteral("-list_ports")))
    {
        QStringList stringList;

        foreach(QSerialPortInfo raw_port, QSerialPortInfo::availablePorts())
        {
            MyQSerialPortInfo port(raw_port);

            if(port.hasVendorIdentifier() && port.hasProductIdentifier() && (((port.vendorIdentifier() == OPENMVCAM_VID) && (port.productIdentifier() == OPENMVCAM_PID) && (port.serialNumber() != QStringLiteral("000000000010")) && (port.serialNumber() != QStringLiteral("000000000011")))
            || ((port.vendorIdentifier() == ARDUINOCAM_VID) && (((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||
                                                                ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||
                                                                ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||
                                                                ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID)))
            || ((port.vendorIdentifier() == RPI2040_VID) && (port.productIdentifier() == RPI2040_PID))))
            {
                stringList.append(port.portName());
            }
        }

        if(Utils::HostOsInfo::isMacHost())
        {
            stringList = stringList.filter(QStringLiteral("cu"), Qt::CaseInsensitive);
        }

        QTextStream out(stdout);

        foreach(const QString &port, stringList)
        {
            QSerialPortInfo raw_info = QSerialPortInfo(port);
            MyQSerialPortInfo info(raw_info);

            out << QString(QStringLiteral("\"name\":\"%1\", \"description\":\"%2\", \"manufacturer\":\"%3\", \"vid\":0x%4, \"pid\":0x%5, \"serial\":\"%6\", \"location\":\"%7\""))
                   .arg(info.portName())
                   .arg(info.description())
                   .arg(info.manufacturer())
                   .arg(QString(QStringLiteral("%1").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0'))).toUpper())
                   .arg(QString(QStringLiteral("%1").arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))).toUpper())
                   .arg(info.serialNumber().toUpper())
                   .arg(info.systemLocation()) << endl;
        }

        exit(0);
    }

    m_serialNumberFilter = QString();
    int index_serial_number_filter = arguments.indexOf(QRegularExpression(QStringLiteral("-serial_number_filter")));

    if(index_serial_number_filter != -1)
    {
        if(arguments.size() > (index_serial_number_filter + 1))
        {
            m_serialNumberFilter = arguments.at(index_serial_number_filter + 1);
        }
        else
        {
            displayError(Tr::tr("Missing argument for -serial_number_filter"));
            exit(-1);
        }
    }

    bool autoRun = arguments.contains(QStringLiteral("-auto_run"));
    m_autoConnect = autoRun || arguments.contains(QStringLiteral("-auto_connect"));

    if(m_autoConnect)
    {
        connect(ExtensionSystem::PluginManager::instance(), &ExtensionSystem::PluginManager::initializationDone, this, [this, autoRun] {
            QTimer::singleShot(0, this, [this, autoRun] {
                connectClicked();

                if(autoRun)
                {
                    QTimer::singleShot(0, this, [this] {
                        startClicked();
                    });
                }
            });
        });
    }

    if(arguments.contains(QStringLiteral("-full_screen")))
    {
        connect(ExtensionSystem::PluginManager::instance(), &ExtensionSystem::PluginManager::initializationDone, this, [] {
            QAction *action = Core::ActionManager::command(Core::Constants::TOGGLE_FULLSCREEN)->action();

            if(!Core::ICore::mainWindow()->isFullScreen())
            {
                QTimer::singleShot(0, action, &QAction::trigger);
            }
        });
    }

    m_ioport = new OpenMVPluginSerialPort(override_read_timeout, override_read_stall_timeout, this);
    m_iodevice = new OpenMVPluginIO(m_ioport, this);

    ///////////////////////////////////////////////////////////////////////////

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    QSplashScreen *splashScreen = new QSplashScreen(QPixmap(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral(DARK_SPLASH_PATH) : QStringLiteral(LIGHT_SPLASH_PATH)));
    Core::ICore::mainWindow()->restoreGeometry(settings->value(QStringLiteral("MainWindow/WindowGeometry")).toByteArray()); // Move to the correct screen for moving splash...
    splashScreen->move(Core::ICore::mainWindow()->screen()->availableGeometry().center() - splashScreen->rect().center());

    if(!qFuzzyCompare(splashScreen->screen()->devicePixelRatio(), 1.0))
    {
        QPixmap hdpi = QPixmap(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral(DARK_SPLASH_HIDPI_PATH) : QStringLiteral(LIGHT_SPLASH_HIDPI_PATH));
        hdpi.setDevicePixelRatio(2.0);
        splashScreen->setPixmap(hdpi);
    }

    connect(Core::ICore::instance(), &Core::ICore::coreOpened,
            splashScreen, &QSplashScreen::deleteLater);

    if(!isNoShow()) splashScreen->show();

    ///////////////////////////////////////////////////////////////////////////

    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    int major = settings->value(QStringLiteral(RESOURCES_MAJOR), 0).toInt();
    int minor = settings->value(QStringLiteral(RESOURCES_MINOR), 0).toInt();
    int patch = settings->value(QStringLiteral(RESOURCES_PATCH), 0).toInt();

    if((arguments.contains(QStringLiteral("-update_resources")))
    || (major < IDE_VERSION_MAJOR)
    || ((major == IDE_VERSION_MAJOR) && (minor < IDE_VERSION_MINOR))
    || ((major == IDE_VERSION_MAJOR) && (minor == IDE_VERSION_MINOR) && (patch < IDE_VERSION_RELEASE)))
    {
        settings->setValue(QStringLiteral(RESOURCES_MAJOR), 0);
        settings->setValue(QStringLiteral(RESOURCES_MINOR), 0);
        settings->setValue(QStringLiteral(RESOURCES_PATCH), 0);
        settings->sync();

        bool ok = true;

        QString error;

        if(!Core::ICore::userResourcePath().removeRecursively(&error))
        {
            QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
            ok = false;
        }
        else
        {
            Utils::FilePath oldUserResourcesPath = Core::ICore::userResourcePath().parentDir().pathAppended(QStringLiteral("qtcreator"));

            if(oldUserResourcesPath.exists())
            {
                if(!oldUserResourcesPath.removeRecursively(&error))
                {
                    QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                    ok = false;
                }
            }

            if(ok)
            {
                QStringList list = QStringList() << QStringLiteral("examples") << QStringLiteral("firmware") << QStringLiteral("html") << QStringLiteral("models");

                foreach(const QString &dir, list)
                {
                    QString error;

                    if(!Utils::FileUtils::copyRecursively(Core::ICore::resourcePath(dir),
                                                          Core::ICore::userResourcePath(dir),
                                                          &error,
                                                          copyOperator))

                    {
                        QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                        ok = false;
                        break;
                    }
                }
            }
        }

        if(ok)
        {
            settings->setValue(QStringLiteral(RESOURCES_MAJOR), IDE_VERSION_MAJOR);
            settings->setValue(QStringLiteral(RESOURCES_MINOR), IDE_VERSION_MINOR);
            settings->setValue(QStringLiteral(RESOURCES_PATCH), IDE_VERSION_RELEASE);
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
    QStringList providerClasses;
    QMap<QString, QStringList> providerClassArgs;
    QStringList providerFunctions;
    QMap<QString, QStringList> providerFunctionArgs;
    QStringList providerMethods;
    QMap<QString, QStringList> providerMethodArgs;

    QRegularExpression moduleRegEx(QStringLiteral("<section id=\"module-(.+?)\">(.*?)<section"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression moduleRegEx2(QStringLiteral("<section id=\"module-(.+?)\">(.*?)</section>"), QRegularExpression::DotMatchesEverythingOption);
    m_emRegEx = QRegularExpression(QLatin1String("<em.*?>"), QRegularExpression::DotMatchesEverythingOption);
    m_spanRegEx = QRegularExpression(QStringLiteral("<span.*?>"), QRegularExpression::DotMatchesEverythingOption);
    m_linkRegEx = QRegularExpression(QStringLiteral("<a.*?>"), QRegularExpression::DotMatchesEverythingOption);
    m_classRegEx = QRegularExpression(QStringLiteral(" class=\".*?\""), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression cdfmRegEx(QStringLiteral("<dl class=\"py (class|data|exception|function|method)\">\\s*<dt class=\".+?\" id=\"(.+?)\">(.*?)</dt>\\s*<dd>(.*?)(?:<section|</dd>\\s*</dl>)"), QRegularExpression::DotMatchesEverythingOption);
    m_cdfmRegExInside = QRegularExpression(QStringLiteral("<dl class=\"py (class|data|exception|function|method)\">\\s*<dt class=\".+?\" id=\"(.+?)\">(.*?)</dt>\\s*<dd>(.*)"), QRegularExpression::DotMatchesEverythingOption);
    m_argumentRegEx = QRegularExpression(QStringLiteral("<span class=\"sig-paren\">\\(</span>(.*?)<span class=\"sig-paren\">\\)</span>"), QRegularExpression::DotMatchesEverythingOption);
    m_tupleRegEx = QRegularExpression(QStringLiteral("\\(.*?\\)"), QRegularExpression::DotMatchesEverythingOption);
    m_listRegEx = QRegularExpression(QStringLiteral("\\[.*?\\]"), QRegularExpression::DotMatchesEverythingOption);
    m_dictionaryRegEx = QRegularExpression(QStringLiteral("\\{.*?\\}"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression sectionRegEx(QStringLiteral("<section.*"), QRegularExpression::DotMatchesEverythingOption);

    QDirIterator it(Core::ICore::userResourcePath(QStringLiteral("html/library")).toString(), QDir::Files);

    while(it.hasNext())
    {
        QFile file(it.next());

        if(file.open(QIODevice::ReadOnly))
        {
            QString data = QString::fromUtf8(file.readAll());

            if((file.error() == QFile::NoError) && (!data.isEmpty()))
            {
                file.close();

                QRegularExpressionMatchIterator moduleMatch = moduleRegEx2.globalMatch(data);
                if(!moduleMatch.hasNext()) moduleMatch = moduleRegEx.globalMatch(data);

                while(moduleMatch.hasNext())
                {
                    QRegularExpressionMatch match = moduleMatch.next();
                    QString name = match.captured(1);
                    QString text = match.captured(2).
                                   remove(QStringLiteral("\u00B6")).
                                   remove(m_spanRegEx).
                                   remove(QStringLiteral("</span>")).
                                   remove(m_linkRegEx).
                                   remove(QStringLiteral("</a>")).
                                   remove(m_classRegEx).
                                   replace(QStringLiteral("<h1>"), QStringLiteral("<h3>")).
                                   replace(QStringLiteral("</h1>"), QStringLiteral("</h3>")).
                                   remove(sectionRegEx);

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
                    processDocumentationMatch(matches.next(),
                                              providerVariables,
                                              providerClasses, providerClassArgs,
                                              providerFunctions, providerFunctionArgs,
                                              providerMethods, providerMethodArgs);
                }
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////

    KSyntaxHighlighting::Definition id = TextEditor::Highlighter::definitionForName(QStringLiteral("Python"));

    if(id.isValid())
    {
        if(id.d && id.d->load())
        {
            KSyntaxHighlighting::KeywordList *modulesList = id.d->keywordList(QStringLiteral("listOpenMVModules"));
            KSyntaxHighlighting::KeywordList *classesList = id.d->keywordList(QStringLiteral("listOpenMVClasses"));
            KSyntaxHighlighting::KeywordList *datasList = id.d->keywordList(QStringLiteral("listOpenMVDatas"));
            KSyntaxHighlighting::KeywordList *functionsList = id.d->keywordList(QStringLiteral("listOpenMVFunctions"));
            KSyntaxHighlighting::KeywordList *methodsList = id.d->keywordList(QStringLiteral("listOpenMVMethods"));
            KSyntaxHighlighting::KeywordList *argumentsList = id.d->keywordList(QStringLiteral("listOpenMVArguments"));

            if(modulesList)
            {
                QStringList list = modulesList->keywords();
                list.removeAll(QStringLiteral("OpenMVVModulesPlaceHolderKeyword"));

                foreach(const documentation_t &d, m_modules)
                {
                    list.append(d.name);
                }

                modulesList->setKeywordList(list);
            }

            if(classesList)
            {
                QStringList list = classesList->keywords();
                list.removeAll(QStringLiteral("OpenMVClassesPlaceHolderKeyword"));

                foreach(const documentation_t &d, m_classes)
                {
                    list.append(d.name);
                }

                classesList->setKeywordList(list);
            }

            if(datasList)
            {
                QStringList list = datasList->keywords();
                list.removeAll(QStringLiteral("OpenMVDatasPlaceHolderKeyword"));

                foreach(const documentation_t &d, m_datas)
                {
                    list.append(d.name);
                }

                datasList->setKeywordList(list);
            }

            if(functionsList)
            {
                QStringList list = functionsList->keywords();
                list.removeAll(QStringLiteral("OpenMVFunctionsPlaceHolderKeyword"));

                foreach(const documentation_t &d, m_functions)
                {
                    list.append(d.name);
                }

                functionsList->setKeywordList(list);
            }

            if(methodsList)
            {
                QStringList list = methodsList->keywords();
                list.removeAll(QStringLiteral("OpenMVMethodsPlaceHolderKeyword"));

                foreach(const documentation_t &d, m_methods)
                {
                    list.append(d.name);
                }

                methodsList->setKeywordList(list);
            }

            if(argumentsList)
            {
                QStringList list = argumentsList->keywords();
                list.removeAll(QStringLiteral("OpenMVArgumentsPlaceHolderKeyword"));

                foreach(const QString &d, m_arguments.values())
                {
                    list.append(d);
                }

                argumentsList->setKeywordList(list);
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////

    OpenMVPluginCompletionAssistProvider *provider = new OpenMVPluginCompletionAssistProvider(providerVariables,
                                                                                              providerClasses, providerClassArgs,
                                                                                              providerFunctions, providerFunctionArgs,
                                                                                              providerMethods, providerMethodArgs,
                                                                                              this);

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

                        QTextCursor newCursor(cursor);
                        QString maybeModuleName;
                        bool moduleFilter = false;

                        // 1. Move the cursor to break selection, 2. Move the cursor to '.', 3. Move the cursor onto the word behind '.'.
                        if(newCursor.movePosition(QTextCursor::PreviousWord, QTextCursor::MoveAnchor, 3))
                        {
                            newCursor.select(QTextCursor::WordUnderCursor);
                            maybeModuleName = newCursor.selectedText();

                            if(!maybeModuleName.isEmpty())
                            {
                                foreach(const documentation_t &d, m_modules)
                                {
                                    if(d.name == maybeModuleName)
                                    {
                                        moduleFilter = true;
                                        break;
                                    }
                                }
                            }
                        }

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

                            foreach(const documentation_t &d, m_datas)
                            {
                                if((d.name == text) && ((!moduleFilter) || (d.moduleName == maybeModuleName)))
                                {
                                    list.append(d.text);
                                }
                            }

                            if(widget->textDocument()->document()->characterAt(qMax(cursor.position(), cursor.anchor())) == QLatin1Char('('))
                            {
                                foreach(const documentation_t &d, m_classes)
                                {
                                    if((d.name == text) && ((!moduleFilter) || (d.moduleName == maybeModuleName)))
                                    {
                                        list.append(d.text);
                                    }
                                }

                                foreach(const documentation_t &d, m_functions)
                                {
                                    if((d.name == text) && ((!moduleFilter) || (d.moduleName == maybeModuleName)))
                                    {
                                        list.append(d.text);
                                    }
                                }

                                if(qMin(cursor.position(), cursor.anchor()) && (widget->textDocument()->document()->characterAt(qMin(cursor.position(), cursor.anchor()) - 1) == QLatin1Char('.')))
                                {
                                    foreach(const documentation_t &d, m_methods)
                                    {
                                        if((d.name == text) && ((!moduleFilter) || (d.moduleName == maybeModuleName)))
                                        {
                                            list.append(d.text);
                                        }
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
                    QAction *action = new QAction(Tr::tr("Edit Grayscale threshold with Threshold Editor"), menu);
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
                    QAction *action = new QAction(Tr::tr("Edit LAB threshold with Threshold Editor"), menu);
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
        LoadFolderThread *loadFolderThread = new LoadFolderThread(Core::ICore::userResourcePath(QStringLiteral("examples")).toString(), true);
        loadFolderThread->moveToThread(thread);
        QTimer *timer = new QTimer(this);

        connect(timer, &QTimer::timeout,
                loadFolderThread, &LoadFolderThread::loadFolderSlot);

        connect(loadFolderThread, &LoadFolderThread::folderLoaded, this, [this] (const importDataList_t &output) {
            m_exampleModules = output;
        });

        connect(this, &OpenMVPlugin::destroyed,
                loadFolderThread, &LoadFolderThread::deleteLater);

        connect(loadFolderThread, &LoadFolderThread::destroyed,
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
        LoadFolderThread *loadFolderThread = new LoadFolderThread(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV"), false);
        loadFolderThread->moveToThread(thread);
        QTimer *timer = new QTimer(this);

        connect(timer, &QTimer::timeout,
                loadFolderThread, &LoadFolderThread::loadFolderSlot);

        connect(loadFolderThread, &LoadFolderThread::folderLoaded, this, [this] (const importDataList_t &output) {
            m_documentsModules = output;
        });

        connect(this, &OpenMVPlugin::destroyed,
                loadFolderThread, &LoadFolderThread::deleteLater);

        connect(loadFolderThread, &LoadFolderThread::destroyed,
                thread, &QThread::quit);

        connect(thread, &QThread::finished,
                thread, &QThread::deleteLater);

        thread->start();
        timer->start(FOLDER_SCAN_TIME);
        QTimer::singleShot(0, loadFolderThread, &LoadFolderThread::loadFolderSlot);
    }

    ///////////////////////////////////////////////////////////////////////////

    int index_form_key = arguments.indexOf(QRegularExpression(QStringLiteral("-form_key")));

    if(index_form_key != -1)
    {
        if(arguments.size() > (index_form_key + 1))
        {
            m_formKey = arguments.at(index_form_key + 1);
        }
        else
        {
            displayError(Tr::tr("Missing argument for -form_key"));
            exit(-1);
        }
    }

    return true;
}

void OpenMVPlugin::extensionsInitialized()
{
    connect(Core::ActionManager::command(Core::Constants::NEW_FILE)->action(), &QAction::triggered, this, [this] {
        Core::EditorManager::cutForwardNavigationHistory();
        Core::EditorManager::addCurrentPositionToNavigationHistory();
        QString titlePattern = Tr::tr("untitled_$.py");

        QByteArray data =
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
                       "    print(clock.fps())\n").arg(Utils::Environment::systemEnvironment().toDictionary().userName()).arg(QDate::currentDate().toString()).toUtf8();

        if((m_sensorType == QStringLiteral("HM01B0")) ||
           (m_sensorType == QStringLiteral("HM0360")) ||
           (m_sensorType == QStringLiteral("MT9V0X2")) ||
           (m_sensorType == QStringLiteral("MT9V0X4")))
        {
            data = data.replace(QByteArrayLiteral("sensor.set_pixformat(sensor.RGB565)"), QByteArrayLiteral("sensor.set_pixformat(sensor.GRAYSCALE)"));
            if(m_sensorType == QStringLiteral("HM01B0")) data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"), QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
        }

        TextEditor::BaseTextEditor *editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::openEditorWithContents(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID, &titlePattern, data));

        if(editor)
        {
            QTemporaryFile file(QDir::tempPath() + QDir::separator() + QString(editor->document()->displayName()).replace(QStringLiteral(".py"), QStringLiteral("_XXXXXX.py")));

            if(file.open())
            {
                if(file.write(data) == data.size())
                {
                    file.setAutoRemove(false);
                    file.close();

                    editor->document()->setProperty("diffFilePath", QFileInfo(file).canonicalFilePath());
                    Core::EditorManager::addCurrentPositionToNavigationHistory();
                    editor->editorWidget()->configureGenericHighlighter();
                    Core::EditorManager::activateEditor(editor);
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("New File"),
                        Tr::tr("Can't open the new file!"));
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("New File"),
                    Tr::tr("Can't open the new file!"));
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("New File"),
                Tr::tr("Can't open the new file!"));
        }
    });

    Core::ActionContainer *filesMenu = Core::ActionManager::actionContainer(Core::Constants::M_FILE);

    Core::ActionContainer *documentsFolder = Core::ActionManager::createMenu(Utils::Id("OpenMV.DocumentsFolder"));
    filesMenu->addMenu(Core::ActionManager::actionContainer(Core::Constants::M_FILE_RECENTFILES), documentsFolder);
    documentsFolder->menu()->setTitle(Tr::tr("Documents Folder"));
    documentsFolder->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    connect(filesMenu->menu(), &QMenu::aboutToShow, this, [this, documentsFolder] {
        documentsFolder->menu()->clear();
        QMultiMap<QString, QAction *> actions = aboutToShowExamplesRecursive(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV"), documentsFolder->menu(), true);

        if(actions.isEmpty())
        {
            QAction *action = new QAction(Tr::tr("Add some code to \"%L1\"").arg(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV")), documentsFolder->menu());
            action->setDisabled(true);
            documentsFolder->menu()->addAction(action);
        }
        else
        {
            documentsFolder->menu()->addActions(actions.values());
        }
    });

    Core::ActionContainer *examplesMenu = Core::ActionManager::createMenu(Utils::Id("OpenMV.Examples"));
    filesMenu->addMenu(Core::ActionManager::actionContainer(Core::Constants::M_FILE_RECENTFILES), examplesMenu);
    examplesMenu->menu()->setTitle(Tr::tr("Examples"));
    examplesMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    connect(filesMenu->menu(), &QMenu::aboutToShow, this, [this, examplesMenu] {
        examplesMenu->menu()->clear();
        QMultiMap<QString, QAction *> actions = aboutToShowExamplesRecursive(Core::ICore::userResourcePath(QStringLiteral("examples")).toString(), examplesMenu->menu());
        examplesMenu->menu()->addActions(actions.values());
        examplesMenu->menu()->setDisabled(actions.values().isEmpty());
    });

    Core::ActionContainer *toolsMenu = Core::ActionManager::actionContainer(Core::Constants::M_TOOLS);
    Core::ActionContainer *helpMenu = Core::ActionManager::actionContainer(Core::Constants::M_HELP);

    m_bootloaderAction = new QAction(Tr::tr("Run Bootloader (Load Firmware)"), this);
    Core::Command *bootloaderCommand = Core::ActionManager::registerAction(m_bootloaderAction, Utils::Id("OpenMV.Bootloader"));
    bootloaderCommand->setDefaultKeySequence(QKeySequence(Tr::tr("Ctrl+Shift+L")));
    toolsMenu->addAction(bootloaderCommand);
    connect(m_bootloaderAction, &QAction::triggered, this, &OpenMVPlugin::bootloaderClicked);

    m_eraseAction = new QAction(Tr::tr("Erase Onboard Data Flash"), this);
    Core::Command *eraseCommand = Core::ActionManager::registerAction(m_eraseAction, Utils::Id("OpenMV.Erase"));
    eraseCommand->setDefaultKeySequence(QKeySequence(Tr::tr("Ctrl+Shift+E")));
    toolsMenu->addAction(eraseCommand);
    connect(m_eraseAction, &QAction::triggered, this, [this] {
        if(QMessageBox::warning(Core::ICore::dialogParent(),
            Tr::tr("Erase Onboard Data Flash"),
            Tr::tr("Are you sure you want to erase your OpenMV Cam's onboard flash drive?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
        == QMessageBox::Yes)connectClicked(true, QString(), true, true);
    });
    toolsMenu->addSeparator();

    m_autoReconnectAction = new QAction(Tr::tr("Auto Reconnect to OpenMV Cam"), this);
    m_autoReconnectAction->setToolTip(Tr::tr("When Auto Reconnect is enabled OpenMV IDE will automatically reconnect to your OpenMV if detected."));
    Core::Command *autoReconnectCommand = Core::ActionManager::registerAction(m_autoReconnectAction, Utils::Id("OpenMV.AutoReconnect"));
    toolsMenu->addAction(autoReconnectCommand);
    m_autoReconnectAction->setCheckable(true);
    m_autoReconnectAction->setChecked(false);

    m_stopOnConnectDiconnectionAction = new QAction(Tr::tr("Stop Script on Connect/Disconnect"), this);
    m_stopOnConnectDiconnectionAction->setToolTip(Tr::tr("Stop the script on Connect or Disconnect (note that the IDE disconnects on close if connected)."));
    Core::Command *stopOnConnectDiconnectionCommand = Core::ActionManager::registerAction(m_stopOnConnectDiconnectionAction, Utils::Id("OpenMV.StopOnConnectDisconnect"));
    toolsMenu->addAction(stopOnConnectDiconnectionCommand);
    m_stopOnConnectDiconnectionAction->setCheckable(true);
    m_stopOnConnectDiconnectionAction->setChecked(true);

    toolsMenu->addSeparator();

    m_openDriveFolderAction = new QAction(Tr::tr("Open OpenMV Cam Drive folder"), this);
    m_openDriveFolderCommand = Core::ActionManager::registerAction(m_openDriveFolderAction, Utils::Id("OpenMV.OpenDriveFolder"));
    toolsMenu->addAction(m_openDriveFolderCommand);
    m_openDriveFolderAction->setEnabled(false);
    connect(m_openDriveFolderAction, &QAction::triggered, this, [this] {Core::FileUtils::showInGraphicalShell(Core::ICore::mainWindow(), Utils::FilePath::fromString(m_portPath).pathAppended(Utils::HostOsInfo::isWindowsHost() ? QStringLiteral("") : QStringLiteral(".openmv_disk"))); });

    m_configureSettingsAction = new QAction(Tr::tr("Configure OpenMV Cam settings file"), this);
    m_configureSettingsCommand = Core::ActionManager::registerAction(m_configureSettingsAction, Utils::Id("OpenMV.Settings"));
    toolsMenu->addAction(m_configureSettingsCommand);
    m_configureSettingsAction->setEnabled(false);
    connect(m_configureSettingsAction, &QAction::triggered, this, &OpenMVPlugin::configureSettings);

    m_saveAction = new QAction(Tr::tr("Save open script to OpenMV Cam (as main.py)"), this);
    m_saveCommand = Core::ActionManager::registerAction(m_saveAction, Utils::Id("OpenMV.Save"));
    toolsMenu->addAction(m_saveCommand);
    m_saveAction->setEnabled(false);
    connect(m_saveAction, &QAction::triggered, this, &OpenMVPlugin::saveScript);

    m_resetAction = new QAction(Tr::tr("Reset OpenMV Cam"), this);
    m_resetCommand = Core::ActionManager::registerAction(m_resetAction, Utils::Id("OpenMV.Reset"));
    toolsMenu->addAction(m_resetCommand);
    m_resetAction->setEnabled(false);
    connect(m_resetAction, &QAction::triggered, this, [this] {disconnectClicked(true);});

    m_developmentReleaseAction = new QAction(Tr::tr("Install the Latest Development Release"), this);
    m_developmentReleaseCommand = Core::ActionManager::registerAction(m_developmentReleaseAction, Utils::Id("OpenMV.InstallTheLatestDevelopmentRelease"));
    toolsMenu->addAction(m_developmentReleaseCommand);
    m_developmentReleaseAction->setEnabled(false);
    connect(m_developmentReleaseAction, &QAction::triggered, this, &OpenMVPlugin::installTheLatestDevelopmentRelease);

    toolsMenu->addSeparator();
    m_openTerminalMenu = Core::ActionManager::createMenu(Utils::Id("OpenMV.OpenTermnial"));
    m_openTerminalMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    m_openTerminalMenu->menu()->setTitle(Tr::tr("Open Terminal"));
    toolsMenu->addMenu(m_openTerminalMenu);
    connect(m_openTerminalMenu->menu(), &QMenu::aboutToShow, this, &OpenMVPlugin::openTerminalAboutToShow);

    Core::ActionContainer *machineVisionToolsMenu = Core::ActionManager::createMenu(Utils::Id("OpenMV.MachineVision"));
    machineVisionToolsMenu->menu()->setTitle(Tr::tr("Machine Vision"));
    machineVisionToolsMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    toolsMenu->addMenu(machineVisionToolsMenu);

    QAction *thresholdEditorAction = new QAction(Tr::tr("Threshold Editor"), this);
    Core::Command *thresholdEditorCommand = Core::ActionManager::registerAction(thresholdEditorAction, Utils::Id("OpenMV.ThresholdEditor"));
    machineVisionToolsMenu->addAction(thresholdEditorCommand);
    connect(thresholdEditorAction, &QAction::triggered, this, &OpenMVPlugin::openThresholdEditor);

    QAction *keypointsEditorAction = new QAction(Tr::tr("Keypoints Editor"), this);
    Core::Command *keypointsEditorCommand = Core::ActionManager::registerAction(keypointsEditorAction, Utils::Id("OpenMV.KeypointsEditor"));
    machineVisionToolsMenu->addAction(keypointsEditorCommand);
    connect(keypointsEditorAction, &QAction::triggered, this, &OpenMVPlugin::openKeypointsEditor);

    machineVisionToolsMenu->addSeparator();
    Core::ActionContainer *aprilTagGeneratorSubmenu = Core::ActionManager::createMenu(Utils::Id("OpenMV.AprilTagGenerator"));
    aprilTagGeneratorSubmenu->menu()->setTitle(Tr::tr("AprilTag Generator"));
    machineVisionToolsMenu->addMenu(aprilTagGeneratorSubmenu);

    QAction *tag16h5Action = new QAction(Tr::tr("TAG16H5 Family (30 Tags)"), this);
    Core::Command *tag16h5Command = Core::ActionManager::registerAction(tag16h5Action, Utils::Id("OpenMV.TAG16H5"));
    aprilTagGeneratorSubmenu->addAction(tag16h5Command);
    connect(tag16h5Action, &QAction::triggered, this, [this] {openAprilTagGenerator(tag16h5_create());});

    QAction *tag25h7Action = new QAction(Tr::tr("TAG25H7 Family (242 Tags)"), this);
    Core::Command *tag25h7Command = Core::ActionManager::registerAction(tag25h7Action, Utils::Id("OpenMV.TAG25H7"));
    aprilTagGeneratorSubmenu->addAction(tag25h7Command);
    connect(tag25h7Action, &QAction::triggered, this, [this] {openAprilTagGenerator(tag25h7_create());});

    QAction *tag25h9Action = new QAction(Tr::tr("TAG25H9 Family (35 Tags)"), this);
    Core::Command *tag25h9Command = Core::ActionManager::registerAction(tag25h9Action, Utils::Id("OpenMV.TAG25H9"));
    aprilTagGeneratorSubmenu->addAction(tag25h9Command);
    connect(tag25h9Action, &QAction::triggered, this, [this] {openAprilTagGenerator(tag25h9_create());});

    QAction *tag36h10Action = new QAction(Tr::tr("TAG36H10 Family (2320 Tags)"), this);
    Core::Command *tag36h10Command = Core::ActionManager::registerAction(tag36h10Action, Utils::Id("OpenMV.TAG36H10"));
    aprilTagGeneratorSubmenu->addAction(tag36h10Command);
    connect(tag36h10Action, &QAction::triggered, this, [this] {openAprilTagGenerator(tag36h10_create());});

    QAction *tag36h11Action = new QAction(Tr::tr("TAG36H11 Family (587 Tags - Recommended)"), this);
    Core::Command *tag36h11Command = Core::ActionManager::registerAction(tag36h11Action, Utils::Id("OpenMV.TAG36H11"));
    aprilTagGeneratorSubmenu->addAction(tag36h11Command);
    connect(tag36h11Action, &QAction::triggered, this, [this] {openAprilTagGenerator(tag36h11_create());});

    QAction *tag36artoolkitAction = new QAction(Tr::tr("ARKTOOLKIT Family (512 Tags)"), this);
    Core::Command *tag36artoolkitCommand = Core::ActionManager::registerAction(tag36artoolkitAction, Utils::Id("OpenMV.ARKTOOLKIT"));
    aprilTagGeneratorSubmenu->addAction(tag36artoolkitCommand);
    connect(tag36artoolkitAction, &QAction::triggered, this, [this] {openAprilTagGenerator(tag36artoolkit_create());});

    QAction *QRCodeGeneratorAction = new QAction(Tr::tr("QRCode Generator"), this);
    Core::Command *QRCodeGeneratorCommand = Core::ActionManager::registerAction(QRCodeGeneratorAction, Utils::Id("OpenMV.QRCodeGenerator"));
    machineVisionToolsMenu->addAction(QRCodeGeneratorCommand);
    connect(QRCodeGeneratorAction, &QAction::triggered, this, [] {
        QUrl url = QUrl(QStringLiteral("https://www.google.com/search?q=qr+code+generator"));

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  Tr::tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    QAction *DatamatrixGeneratorAction = new QAction(Tr::tr("DataMatrix Generator"), this);
    Core::Command *DataMatrixGeneratorCommand = Core::ActionManager::registerAction(DatamatrixGeneratorAction, Utils::Id("OpenMV.DataMatrixGenerator"));
    machineVisionToolsMenu->addAction(DataMatrixGeneratorCommand);
    connect(DatamatrixGeneratorAction, &QAction::triggered, this, [] {
        QUrl url = QUrl(QStringLiteral("https://www.google.com/search?q=data+matrix+generator"));

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  Tr::tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    QAction *BarcodeGeneratorAction = new QAction(Tr::tr("Barcode Generator"), this);
    Core::Command *BarcodeGeneratorCommand = Core::ActionManager::registerAction(BarcodeGeneratorAction, Utils::Id("OpenMV.BarcodeGenerator"));
    machineVisionToolsMenu->addAction(BarcodeGeneratorCommand);
    connect(BarcodeGeneratorAction, &QAction::triggered, this, [] {
        QUrl url = QUrl(QStringLiteral("https://www.google.com/search?q=barcode+generator"));

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  Tr::tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    machineVisionToolsMenu->addSeparator();

    QAction *networkLibraryAction = new QAction(Tr::tr("CNN Network Library"), this);
    Core::Command *networkLibraryCommand = Core::ActionManager::registerAction(networkLibraryAction, Utils::Id("OpenMV.NetworkLibrary"));
    machineVisionToolsMenu->addAction(networkLibraryCommand);
    connect(networkLibraryAction, &QAction::triggered, this, [this] {
        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QString src =
            QFileDialog::getOpenFileName(Core::ICore::dialogParent(), Tr::tr("Network to copy to OpenMV Cam"),
                Core::ICore::userResourcePath(QStringLiteral("models")).toString(),
                Tr::tr("TensorFlow Model (*.tflite);;Neural Network Model (*.network);;Label Files (*.txt);;All Files (*.*)"));

        if(!src.isEmpty())
        {
            QString dst;

            forever
            {
                dst =
                QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Where to save the network on the OpenMV Cam"),
                    m_portPath.isEmpty()
                    ? Utils::FilePath::fromVariant(settings->value(QStringLiteral(LAST_MODEL_NO_CAM_PATH), QDir::homePath())).pathAppended(QFileInfo(src).fileName()).toString()
                    : Utils::FilePath::fromVariant(settings->value(QStringLiteral(LAST_MODEL_WITH_CAM_PATH), m_portPath)).pathAppended(QFileInfo(src).fileName()).toString(),
                    Tr::tr("TensorFlow Model (*.tflite);;Neural Network Model (*.network);;Label Files (*.txt);;All Files (*.*)"));

                if((!dst.isEmpty()) && QFileInfo(dst).completeSuffix().isEmpty())
                {
                    QMessageBox::warning(Core::ICore::dialogParent(),
                        Tr::tr("Where to save the network on the OpenMV Cam"),
                        Tr::tr("Please add a file extension!"));

                    continue;
                }

                break;
            }

            if(!dst.isEmpty())
            {
                if((!QFile(dst).exists()) || QFile::remove(dst))
                {
                    if(QFile::copy(src, dst))
                    {
                        settings->setValue(m_portPath.isEmpty() ? QStringLiteral(LAST_MODEL_NO_CAM_PATH) : QStringLiteral(LAST_MODEL_WITH_CAM_PATH), QFileInfo(dst).path());
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            QString(),
                            Tr::tr("Unable to overwrite output file!"));
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        QString(),
                        Tr::tr("Unable to overwrite output file!"));
                }
            }
        }

        settings->endGroup();
    });

    Core::ActionContainer *videoToolsMenu = Core::ActionManager::createMenu(Utils::Id("OpenMV.VideoTools"));
    videoToolsMenu->menu()->setTitle(Tr::tr("Video Tools"));
    videoToolsMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    toolsMenu->addMenu(videoToolsMenu);

    QAction *convertVideoFile = new QAction(Tr::tr("Convert Video File"), this);
    Core::Command *convertVideoFileCommand = Core::ActionManager::registerAction(convertVideoFile, Utils::Id("OpenMV.ConvertVideoFile"));
    videoToolsMenu->addAction(convertVideoFileCommand);
    connect(convertVideoFile, &QAction::triggered, this, [this] {convertVideoFileAction(m_portPath);});

    QAction *playVideoFile = new QAction(Tr::tr("Play Video File"), this);
    Core::Command *playVideoFileCommand = Core::ActionManager::registerAction(playVideoFile, Utils::Id("OpenMV.PlayVideoFile"));
    if(!(Utils::HostOsInfo::isLinuxHost()
    && ((QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
    || (QSysInfo::buildCpuArchitecture() == QStringLiteral("arm")))))
    {
        videoToolsMenu->addAction(playVideoFileCommand);
    }
    connect(playVideoFile, &QAction::triggered, this, [this] {playVideoFileAction(m_portPath);});

    QAction *playRTSPStream = new QAction(Tr::tr("Play RTSP Stream"), this);
    Core::Command *playRTSPStreamCommand = Core::ActionManager::registerAction(playRTSPStream, Utils::Id("OpenMV.PlayRTSPStream"));
    if(!(Utils::HostOsInfo::isLinuxHost()
    && ((QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
    || (QSysInfo::buildCpuArchitecture() == QStringLiteral("arm")))))
    {
        videoToolsMenu->addSeparator();
        videoToolsMenu->addAction(playRTSPStreamCommand);
    }
    connect(playRTSPStream, &QAction::triggered, this, [] {playRTSPStreamAction();});

    Core::ActionContainer *datasetEditorMenu = Core::ActionManager::createMenu(Utils::Id("OpenMV.DatasetEditor"));
    datasetEditorMenu->menu()->setTitle(Tr::tr("Dataset Editor"));
    datasetEditorMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    toolsMenu->addMenu(datasetEditorMenu);

    QAction *newDatasetAction = new QAction(Tr::tr("New Dataset"), this);
    Core::Command *newDatasetCommand = Core::ActionManager::registerAction(newDatasetAction, Utils::Id("OpenMV.NewDataset"));
    datasetEditorMenu->addAction(newDatasetCommand);
    connect(newDatasetAction, &QAction::triggered, this, [this] {
        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QString path =
            QFileDialog::getExistingDirectory(Core::ICore::dialogParent(), Tr::tr("Dataset Editor - Choose a folder to build the dataset in"),
                settings->value(QStringLiteral(LAST_DATASET_EDITOR_PATH), QDir::homePath()).toString());

        if(!path.isEmpty())
        {
            bool ok = !QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).count();

            if(!ok)
            {
                if(QMessageBox::warning(Core::ICore::dialogParent(),
                    Tr::tr("New Dataset"),
                    Tr::tr("The selected folder is not empty and the contents will be deleted. Continue?"),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)
                == QMessageBox::Yes)
                {
                    if(QDir::cleanPath(QDir::fromNativeSeparators(m_datasetEditor->rootPath())) == QDir::cleanPath(QDir::fromNativeSeparators(path)))
                    {
                        m_datasetEditor->setRootPath(QString());
                    }

                    QString error;

                    if(!Utils::FilePath::fromString(path).removeRecursively(&error))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("New Dataset"),
                            Tr::tr("Failed to remove \"%L1\"!").arg(path));
                    }
                    else if(!QDir().mkdir(path))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("New Dataset"),
                            Tr::tr("Failed to create \"%L1\"!").arg(path));
                    }
                    else
                    {
                        ok = true;
                    }
                }
            }

            if(ok)
            {
                QByteArray contents = QStringLiteral("# Dataset Capture Script - By: %L1 - %L2\n"
                                                     "\n"
                                                     "# Use this script to control how your OpenMV Cam captures images for your dataset.\n"
                                                     "# You should apply the same image pre-processing steps you expect to run on images\n"
                                                     "# that you will feed to your model during run-time.\n"
                                                     "\n"
                                                     "import sensor, image, time\n"
                                                     "\n"
                                                     "sensor.reset()\n"
                                                     "sensor.set_pixformat(sensor.RGB565) # Modify as you like.\n"
                                                     "sensor.set_framesize(sensor.QVGA) # Modify as you like.\n"
                                                     "sensor.skip_frames(time = 2000)\n"
                                                     "\n"
                                                     "clock = time.clock()\n"
                                                     "\n"
                                                     "while(True):\n"
                                                     "    clock.tick()\n"
                                                     "    img = sensor.snapshot()\n"
                                                     "    # Apply lens correction if you need it.\n"
                                                     "    # img.lens_corr()\n"
                                                     "    # Apply rotation correction if you need it.\n"
                                                     "    # img.rotation_corr()\n"
                                                     "    # Apply other filters...\n"
                                                     "    # E.g. mean/median/mode/midpoint/etc.\n"
                                                     "    print(clock.fps())\n").
                                      arg(Utils::Environment::systemEnvironment().toDictionary().userName()).arg(QDate::currentDate().toString()).toUtf8();

                if((m_sensorType == QStringLiteral("HM01B0")) || (m_sensorType == QStringLiteral("MT9V034")))
                {
                    contents = contents.replace(QByteArrayLiteral("sensor.set_pixformat(sensor.RGB565)"), QByteArrayLiteral("sensor.set_pixformat(sensor.GRAYSCALE)"));
                    if(m_sensorType == QStringLiteral("HM01B0")) contents = contents.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"), QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
                }

                Utils::FileSaver file(Utils::FilePath::fromString(path).pathAppended(QStringLiteral("dataset_capture_script.py")));

                if(!file.hasError())
                {
                    if((!file.write(contents)) || (!file.finalize()))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("New Dataset"),
                            Tr::tr("Error: %L1!").arg(file.errorString()));
                    }
                    else
                    {
                        TextEditor::BaseTextEditor *editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::openEditor(file.filePath()));

                        if(editor)
                        {
                            m_datasetEditor->setRootPath(path);
                            Core::EditorManager::addCurrentPositionToNavigationHistory();
                            Core::EditorManager::activateEditor(editor);
                            settings->setValue(QStringLiteral(LAST_DATASET_EDITOR_PATH), path);
                        }
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("New Dataset"),
                        Tr::tr("Error: %L1!").arg(file.errorString()));
                }
            }
        }

        settings->endGroup();
    });

    QAction *openDatasetAction = new QAction(Tr::tr("Open Dataset"), this);
    Core::Command *openDatasetCommand = Core::ActionManager::registerAction(openDatasetAction, Utils::Id("OpenMV.OpenDataset"));
    datasetEditorMenu->addAction(openDatasetCommand);
    connect(openDatasetAction, &QAction::triggered, this, [this] {
        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QString path =
            QFileDialog::getExistingDirectory(Core::ICore::dialogParent(), Tr::tr("Dataset Editor - Choose a dataset folder to open"),
                settings->value(QStringLiteral(LAST_DATASET_EDITOR_PATH), QDir::homePath()).toString());

        if(!path.isEmpty())
        {
            QString name = path + QStringLiteral("/dataset_capture_script.py");

            if(QFile(name).exists())
            {
                TextEditor::BaseTextEditor *editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::openEditor(Utils::FilePath::fromString(name)));

                if(editor)
                {
                    m_datasetEditor->setRootPath(path);
                    Core::EditorManager::addCurrentPositionToNavigationHistory();
                    Core::EditorManager::activateEditor(editor);
                    settings->setValue(QStringLiteral(LAST_DATASET_EDITOR_PATH), path);
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Open Dataset"),
                    Tr::tr("The selected folder does not appear to be a valid OpenMV Cam Image Dataset!"));
            }
        }

        settings->endGroup();
    });

    datasetEditorMenu->addSeparator();

    Core::ActionContainer *datasetEditorExportMenu = Core::ActionManager::createMenu(Utils::Id("OpenMV.DatasetEditorExport"));
    datasetEditorExportMenu->menu()->setTitle(Tr::tr("Export"));
    datasetEditorExportMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    datasetEditorMenu->addMenu(datasetEditorExportMenu);

    QAction *exportDataseFlatAction = new QAction(Tr::tr("Export Dataset to Zip File"), this);
    Core::Command *exportDatasetFlatCommand = Core::ActionManager::registerAction(exportDataseFlatAction, Utils::Id("OpenMV.ExportDataset"));
    datasetEditorExportMenu->addAction(exportDatasetFlatCommand);
    connect(exportDataseFlatAction, &QAction::triggered, this, [this] {
        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QString path;

        forever
        {
            path =
            QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Export Dataset"),
                settings->value(QStringLiteral(LAST_DATASET_EDITOR_EXPORT_PATH), QDir::homePath()).toString(),
                Tr::tr("Zip Files (*.zip)"));

            if((!path.isEmpty()) && QFileInfo(path).completeSuffix().isEmpty())
            {
                QMessageBox::warning(Core::ICore::dialogParent(),
                    Tr::tr("Export Dataset"),
                    Tr::tr("Please add a file extension!"));

                continue;
            }

            break;
        }

        if(!path.isEmpty())
        {
            QList< QPair<QString, QString> > list;

            foreach(const QString &className, m_datasetEditor->classFolderList())
            {
                foreach(const QString &snapshotName, m_datasetEditor->snapshotList(className))
                {
                    list.append(QPair<QString, QString>(m_datasetEditor->rootPath() + QDir::separator() + className + QDir::separator() + snapshotName, QString(className).remove(QStringLiteral(".class")) + QLatin1Char('.') + snapshotName));
                }
            }

            QProgressDialog progress(Tr::tr("Exporting..."), Tr::tr("Cancel"), 0, list.size(), Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));

            progress.setWindowModality(Qt::ApplicationModal);

            QZipWriter writer(path);

            QPair<QString, QString> pair; foreach(pair, list)
            {
                QFile file(pair.first);

                if(file.open(QIODevice::ReadOnly))
                {
                    writer.addFile(pair.second, file.readAll());
                }
                else
                {
                    progress.cancel();

                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Export Dataset"),
                        Tr::tr("Error: %L1!").arg(file.errorString()));
                }

                if(progress.wasCanceled())
                {
                    break;
                }

                progress.setValue(progress.value() + 1);
            }

            writer.close();

            if(!progress.wasCanceled())
            {
                settings->setValue(QStringLiteral(LAST_DATASET_EDITOR_EXPORT_PATH), path);
            }
            else if(!QFile::remove(path))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Export Dataset"),
                    Tr::tr("Failed to remove \"%L1\"!").arg(path));
            }
        }

        settings->endGroup();
    });

    datasetEditorExportMenu->addSeparator();

    QAction *uploadToEdgeImpulseProjectAction = new QAction(Tr::tr("Upload to Edge Impulse Project"), this);
    Core::Command *uploadToEdgeImpulseProjectCommand = Core::ActionManager::registerAction(uploadToEdgeImpulseProjectAction, Utils::Id("OpenMV.UploadToEdgeImpulseProjectAction"));
    datasetEditorExportMenu->addAction(uploadToEdgeImpulseProjectCommand);
    connect(uploadToEdgeImpulseProjectAction, &QAction::triggered, this, [this] { uploadToSelectedProject(m_datasetEditor); });

    QAction *logInToEdgeImpulseAccountAction = new QAction(Tr::tr("Login to Edge Impulse Account"), this);
    Core::Command *loginToEdgeImpulseAccountCommand = Core::ActionManager::registerAction(logInToEdgeImpulseAccountAction, Utils::Id("OpenMV.LogInToEdgeImpulseAccount"));
    datasetEditorExportMenu->addAction(loginToEdgeImpulseAccountCommand);
    connect(logInToEdgeImpulseAccountAction, &QAction::triggered, this, [this] { loginToEdgeImpulse(m_datasetEditor); });

    QAction *logOutFromEdgeImpulseAccountAction = new QAction(Tr::tr("Logout from Account: %L1").arg(loggedIntoEdgeImpulse()), this);
    Core::Command *logOutFromEdgeImpulseAccountCommand = Core::ActionManager::registerAction(logOutFromEdgeImpulseAccountAction, Utils::Id("OpenMV.LogOutFromEdgeImpulseAccount"));
    datasetEditorExportMenu->addAction(logOutFromEdgeImpulseAccountCommand);
    connect(logOutFromEdgeImpulseAccountAction, &QAction::triggered, this, [] { logoutFromEdgeImpulse(); });

    connect(datasetEditorMenu->menu(), &QMenu::aboutToShow, this,
        [this, uploadToEdgeImpulseProjectAction, logInToEdgeImpulseAccountAction, logOutFromEdgeImpulseAccountAction, loginToEdgeImpulseAccountCommand, logOutFromEdgeImpulseAccountCommand] {
        QString accountName = loggedIntoEdgeImpulse();
        uploadToEdgeImpulseProjectAction->setVisible(!accountName.isEmpty());
        uploadToEdgeImpulseProjectAction->setEnabled(m_datasetEditor->isVisible() && (!accountName.isEmpty()));
        logInToEdgeImpulseAccountAction->setVisible(accountName.isEmpty());
        // Text/Image has to be set through the proxy action - enabled/visible through regular action.
        loginToEdgeImpulseAccountCommand->action()->setText(m_datasetEditor->isVisible() ? Tr::tr("Login to Edge Impulse Account and Upload to Project") : Tr::tr("Login to Edge Impulse Account"));
        logOutFromEdgeImpulseAccountAction->setVisible(!accountName.isEmpty());
        // Text/Image has to be set through the proxy action - enabled/visible through regular action.
        logOutFromEdgeImpulseAccountCommand->action()->setText(Tr::tr("Logout from Account: %L1").arg(accountName));
    });

    datasetEditorExportMenu->addSeparator();

    QAction *uploadToEdgeImpulseByAPIKeyAction = new QAction(Tr::tr("Upload to Edge Impulse by API Key"), this);
    Core::Command *uploadToEdgeImpulseByAPIKeyCommand = Core::ActionManager::registerAction(uploadToEdgeImpulseByAPIKeyAction, Utils::Id("OpenMV.UploadEdgeImpulseAPIKey"));
    datasetEditorExportMenu->addAction(uploadToEdgeImpulseByAPIKeyCommand);
    connect(uploadToEdgeImpulseByAPIKeyAction, &QAction::triggered, this, [this] { uploadProjectByAPIKey(m_datasetEditor); });

    datasetEditorMenu->addSeparator();

    QAction *closeDatasetAction = new QAction(Tr::tr("Close Dataset"), this);
    Core::Command *closeDatasetCommand = Core::ActionManager::registerAction(closeDatasetAction, Utils::Id("OpenMV.CloseDataset"));
    datasetEditorMenu->addAction(closeDatasetCommand);

    QAction *docsAction = new QAction(Tr::tr("OpenMV Docs"), this);
    Core::Command *docsCommand = Core::ActionManager::registerAction(docsAction, Utils::Id("OpenMV.Docs"));
    helpMenu->addAction(docsCommand, Core::Constants::G_HELP_SUPPORT);
    connect(docsAction, &QAction::triggered, this, [] {
        QUrl url = QUrl::fromLocalFile(Core::ICore::userResourcePath(QStringLiteral("html/index.html")).toString());

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  Tr::tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    QAction *forumsAction = new QAction(Tr::tr("OpenMV Forums"), this);
    Core::Command *forumsCommand = Core::ActionManager::registerAction(forumsAction, Utils::Id("OpenMV.Forums"));
    helpMenu->addAction(forumsCommand, Core::Constants::G_HELP_SUPPORT);
    connect(forumsAction, &QAction::triggered, this, [] {
        QUrl url = QUrl(QStringLiteral("https://forums.openmv.io/"));

        if(!QDesktopServices::openUrl(url))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  QString(),
                                  Tr::tr("Failed to open: \"%L1\"").arg(url.toString()));
        }
    });

    Core::ActionContainer *pinoutMenu = Core::ActionManager::createMenu(Utils::Id("OpenMV.PinoutMenu"));
    pinoutMenu->menu()->setTitle(Utils::HostOsInfo::isMacHost() ? Tr::tr("About OpenMV Cam") : Tr::tr("About OpenMV Cam..."));
    pinoutMenu->setOnAllDisabledBehavior(Core::ActionContainer::Show);
    helpMenu->addMenu(pinoutMenu);

    typedef QPair<QString, QString> QStringPair;
    QList<QStringPair> cameras;

    cameras.append(QStringPair(QStringLiteral("H7 Plus"), QStringLiteral("cam-h7-plus-ov5640")));
    cameras.append(QStringPair(QStringLiteral("H7"), QStringLiteral("cam-h7-ov7725")));
    cameras.append(QStringPair(QStringLiteral("M7"), QStringLiteral("cam-m7-ov7725")));
    cameras.append(QStringPair(QStringLiteral("M4"), QStringLiteral("cam-m4-ov7725")));
    cameras.append(QStringPair(QStringLiteral("M4 Original"), QStringLiteral("cam-m4-ov2640")));

    foreach(const QStringPair &cam, cameras)
    {
        QAction *pinout = new QAction(
             Utils::HostOsInfo::isMacHost() ? Tr::tr("About OpenMV Cam %1").arg(cam.first) : Tr::tr("About OpenMV Cam %1...").arg(cam.first), this);
        Core::Command *pinoutCommand = Core::ActionManager::registerAction(pinout, Utils::Id(QString(QStringLiteral("OpenMV.Pinout.%1")).arg(cam.second).toUtf8().constData()));
        pinoutMenu->addAction(pinoutCommand);
        connect(pinout, &QAction::triggered, this, [cam] {
            QUrl url = QUrl::fromLocalFile(Core::ICore::userResourcePath(QString(QStringLiteral("/html/_images/pinout-openmv-%1.png")).arg(cam.second)).toString());

            if(!QDesktopServices::openUrl(url))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                                      QString(),
                                      Tr::tr("Failed to open: \"%L1\"").arg(url.toString()));
            }
        });
    }

    QAction *aboutAction = new QAction(QIcon::fromTheme(QStringLiteral("help-about")),
        Utils::HostOsInfo::isMacHost() ? Tr::tr("About OpenMV IDE") : Tr::tr("About OpenMV IDE..."), this);
    aboutAction->setMenuRole(QAction::AboutRole);
     Core::Command *aboutCommand = Core::ActionManager::registerAction(aboutAction, Utils::Id("OpenMV.About"));
    helpMenu->addAction(aboutCommand, Core::Constants::G_HELP_ABOUT);
    connect(aboutAction, &QAction::triggered, this, [] {
        QMessageBox::about(Core::ICore::dialogParent(), Tr::tr("About OpenMV IDE"), Tr::tr(
        "<p><b>About OpenMV IDE %L1</b></p>"
        "<p>By: Ibrahim Abdelkader & Kwabena W. Agyeman</p>"
        "<p><b>GNU GENERAL PUBLIC LICENSE</b></p>"
        "<p>Copyright (C) %L2 %L3</p>"
        "<p>This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the <a href=\"https://github.com/openmv/qt-creator/raw/master/LICENSE.GPL3-EXCEPT\">GNU General Public License</a> for more details.</p>"
        "<p><b>Questions or Comments?</b></p>"
        "<p>Contact us at <a href=\"mailto:openmv@openmv.io\">openmv@openmv.io</a>.</p>"
        ).arg(QLatin1String(Core::Constants::IDE_VERSION_LONG)).arg(QLatin1String(Core::Constants::IDE_YEAR)).arg(QLatin1String(Core::Constants::IDE_AUTHOR)) + Tr::tr(
        "<p><b>Credits</b></p>") + Tr::tr(
        "<p>OpenMV IDE English translation by Kwabena W. Agyeman.</p>") + Tr::tr(
        "<p><b>Partners</b></p>") +
        QStringLiteral("<p><a href=\"https://www.arduino.cc/\"><img source=\":/openmv/images/arduino-partnership.png\"></a></p>") +
        QString(QStringLiteral("<p><a href=\"https://edgeimpulse.com/\"><img source=\":/openmv/images/edge-impulse-partnership-%1.png\"></a></p>")).arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("dark") : QStringLiteral("light")) +
        QString(QStringLiteral("<p><a href=\"https://www.nxp.com/\"><img source=\":/openmv/images/nxp-logo-%1.png\"></a></p>")).arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("dark") : QStringLiteral("light")) +
        QString(QStringLiteral("<p><a href=\"https://www.st.com/\"><img source=\":/openmv/images/st-logo-%1.png\"></a></p>")).arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("dark") : QStringLiteral("light"))
        );
    });

    ///////////////////////////////////////////////////////////////////////////

    m_connectCommand =
        Core::ActionManager::registerAction(m_connectAction = new QAction(QIcon(QStringLiteral(CONNECT_PATH)),
        Tr::tr("Connect"), this), Utils::Id("OpenMV.Connect"));
    m_connectCommand->setDefaultKeySequence(QStringLiteral("Ctrl+E"));
    m_connectAction->setEnabled(true);
    m_connectAction->setVisible(true);
    connect(m_connectAction, &QAction::triggered, this, [this] {connectClicked();});

    m_disconnectCommand =
        Core::ActionManager::registerAction(m_disconnectAction = new QAction(QIcon(QStringLiteral(DISCONNECT_PATH)),
        Tr::tr("Disconnect"), this), Utils::Id("OpenMV.Disconnect"));
    m_disconnectCommand->setDefaultKeySequence(QStringLiteral("Ctrl+E"));
    m_disconnectAction->setEnabled(false);
    m_disconnectAction->setVisible(false);
    connect(m_disconnectAction, &QAction::triggered, this, [this] {disconnectClicked();});
    connect(m_autoReconnectAction, &QAction::toggled, this, [this] (bool state) {
        m_connectAction->setEnabled(!state);
        m_disconnectAction->setEnabled(!state);
        if(state) {
            static_cast<Utils::ProxyAction *>(m_connectCommand->action())->setOverrideToolTip(m_autoReconnectAction->toolTip());
            static_cast<Utils::ProxyAction *>(m_disconnectCommand->action())->setOverrideToolTip(m_autoReconnectAction->toolTip());
        } else {
            static_cast<Utils::ProxyAction *>(m_connectCommand->action())->setOverrideToolTip(QString());
            static_cast<Utils::ProxyAction *>(m_disconnectCommand->action())->setOverrideToolTip(QString());
        }
    });

    m_startCommand =
        Core::ActionManager::registerAction(m_startAction = new QAction(QIcon(QStringLiteral(START_PATH)),
        Tr::tr("Start (run script)"), this), Utils::Id("OpenMV.Start"));
    m_startCommand->setDefaultKeySequence(QStringLiteral("Ctrl+R"));
    m_startAction->setEnabled(false);
    m_startAction->setVisible(true);
    connect(m_startAction, &QAction::triggered, this, &OpenMVPlugin::startClicked);
    connect(Core::EditorManager::instance(), &Core::EditorManager::currentEditorChanged, [this] (Core::IEditor *editor) {

        if(m_connected)
        {
            m_openDriveFolderAction->setEnabled(!m_portPath.isEmpty());
            m_configureSettingsAction->setEnabled(!m_portPath.isEmpty());
            m_saveAction->setEnabled((!m_portPath.isEmpty()) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));
            m_startAction->setEnabled((!m_running) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));
            m_startAction->setVisible(!m_running);
            m_stopAction->setEnabled(m_running);
            m_stopAction->setVisible(m_running);
        }
    });

    m_stopCommand =
        Core::ActionManager::registerAction(m_stopAction = new QAction(QIcon(QStringLiteral(STOP_PATH)),
        Tr::tr("Stop (halt script)"), this), Utils::Id("OpenMV.Stop"));
    m_stopCommand->setDefaultKeySequence(QStringLiteral("Ctrl+R"));
    m_stopAction->setEnabled(false);
    m_stopAction->setVisible(false);
    connect(m_stopAction, &QAction::triggered, this, &OpenMVPlugin::stopClicked);
    connect(m_iodevice, &OpenMVPluginIO::scriptRunning, this, [this] (bool running) {

        if(m_connected)
        {
            Core::IEditor *editor = Core::EditorManager::currentEditor();
            m_openDriveFolderAction->setEnabled(!m_portPath.isEmpty());
            m_configureSettingsAction->setEnabled(!m_portPath.isEmpty());
            m_saveAction->setEnabled((!m_portPath.isEmpty()) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));
            m_startAction->setEnabled((!running) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));
            m_startAction->setVisible(!running);
            m_stopAction->setEnabled(running);
            m_stopAction->setVisible(running);
            m_running = running;
        }
    });

    ///////////////////////////////////////////////////////////////////////////

    QMainWindow *mainWindow = q_check_ptr(qobject_cast<QMainWindow *>(Core::ICore::mainWindow()));
    Core::Internal::FancyTabWidget *widget = qobject_cast<Core::Internal::FancyTabWidget *>(mainWindow->centralWidget());
    if(!widget) widget = qobject_cast<Core::Internal::FancyTabWidget *>(mainWindow->centralWidget()->layout()->itemAt(1)->widget()); // for tabbededitor
    widget = q_check_ptr(widget);

    Core::Internal::FancyActionBar *actionBar0 = new Core::Internal::FancyActionBar(widget);
    widget->insertCornerWidget(0, actionBar0);

    actionBar0->insertAction(0, Core::ActionManager::command(Core::Constants::NEW_FILE)->action(), QIcon(QStringLiteral(":/openmv/images/filenew.png")));
    actionBar0->insertAction(1, Core::ActionManager::command(Core::Constants::OPEN)->action(), QIcon(QStringLiteral(":/openmv/images/fileopen.png")));
    actionBar0->insertAction(2, Core::ActionManager::command(Core::Constants::SAVE)->action(), QIcon(QStringLiteral(":/openmv/images/filesave.png")));

    actionBar0->setProperty("no_separator", true);
    actionBar0->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    Core::Internal::FancyActionBar *actionBar1 = new Core::Internal::FancyActionBar(widget);
    widget->insertCornerWidget(1, actionBar1);

    actionBar1->insertAction(0, Core::ActionManager::command(Core::Constants::UNDO)->action(), QIcon(QStringLiteral(":/openmv/images/undo.png")));
    actionBar1->insertAction(1, Core::ActionManager::command(Core::Constants::REDO)->action(), QIcon(QStringLiteral(":/openmv/images/redo.png")));
    actionBar1->insertAction(2, Core::ActionManager::command(Core::Constants::CUT)->action(), QIcon(QStringLiteral(":/openmv/images/editcut.png")));
    actionBar1->insertAction(3, Core::ActionManager::command(Core::Constants::COPY)->action(), QIcon(QStringLiteral(":/openmv/images/editcopy.png")));
    actionBar1->insertAction(4, Core::ActionManager::command(Core::Constants::PASTE)->action(), QIcon(QStringLiteral(":/openmv/images/editpaste.png")));

    actionBar1->setProperty("no_separator", true);
    actionBar1->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    Core::Internal::FancyActionBar *actionBar2 = new Core::Internal::FancyActionBar(widget);
    widget->insertCornerWidget(2, actionBar2);

    actionBar2->insertAction(0, m_connectCommand->action());
    actionBar2->insertAction(1, m_disconnectCommand->action());
    actionBar2->insertAction(2, m_startCommand->action());
    actionBar2->insertAction(3, m_stopCommand->action());

    actionBar2->setProperty("no_separator", true);
    actionBar2->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    ///////////////////////////////////////////////////////////////////////////

    Utils::StyledBar *styledBar0 = new Utils::StyledBar;
    QHBoxLayout *styledBar0Layout = new QHBoxLayout;
    styledBar0Layout->setContentsMargins(0, 0, 0, 0);
    styledBar0Layout->setSpacing(0);
    styledBar0Layout->addSpacing(4);
    styledBar0Layout->addWidget(new QLabel(Tr::tr("Frame Buffer")));
    styledBar0Layout->addSpacing(6);
    styledBar0->setLayout(styledBar0Layout);

    QToolButton *beginRecordingButton = new QToolButton;
    beginRecordingButton->setText(Tr::tr("Record"));
    beginRecordingButton->setToolTip(Tr::tr("Record the Frame Buffer"));
    beginRecordingButton->setEnabled(false);
    styledBar0Layout->addWidget(beginRecordingButton);

    QToolButton *endRecordingButton = new QToolButton;
    endRecordingButton->setText(Tr::tr("Stop"));
    endRecordingButton->setToolTip(Tr::tr("Stop recording"));
    endRecordingButton->setVisible(false);
    styledBar0Layout->addWidget(endRecordingButton);

    QToolButton *zoomButton = new QToolButton;
    zoomButton->setText(Tr::tr("Zoom"));
    zoomButton->setToolTip(Tr::tr("Zoom to fit"));
    zoomButton->setCheckable(true);
    zoomButton->setChecked(false);
    styledBar0Layout->addWidget(zoomButton);

    m_jpgCompress = new QToolButton;
    m_jpgCompress->setText(Tr::tr("JPG"));
    m_jpgCompress->setToolTip(Tr::tr("JPEG compress the Frame Buffer for higher performance"));
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
                    Tr::tr("JPG"),
                    Tr::tr("Busy... please wait..."));
            }
        }
    });

    m_disableFrameBuffer = new QToolButton;
    m_disableFrameBuffer->setText(Tr::tr("Disable"));
    m_disableFrameBuffer->setToolTip(Tr::tr("Disable the Frame Buffer for maximum performance"));
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
                    Tr::tr("Disable"),
                    Tr::tr("Busy... please wait..."));
            }
        }
    });

    Utils::ElidingLabel *disableLabel = new Utils::ElidingLabel(Tr::tr("Frame Buffer Disabled - click the disable button again to enable (top right)"));
    disableLabel->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    disableLabel->setStyleSheet(QString(QStringLiteral("background-color:%1;color:%2;padding:4px;")).
                                arg(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal).name()).
                                arg(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal).name()));
    disableLabel->setAlignment(Qt::AlignCenter);
    disableLabel->setVisible(m_disableFrameBuffer->isChecked());
    connect(m_disableFrameBuffer, &QToolButton::toggled, disableLabel, &QLabel::setVisible);

    Utils::ElidingLabel *recordingLabel = new Utils::ElidingLabel(Tr::tr("Elapsed: 0h:00m:00s:000ms - Size: 0 B - FPS: 0"));
    recordingLabel->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    recordingLabel->setStyleSheet(QString(QStringLiteral("background-color:%1;color:%2;padding:4px;")).
                                  arg(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal).name()).
                                  arg(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal).name()));
    recordingLabel->setAlignment(Qt::AlignCenter);
    recordingLabel->setVisible(false);
    recordingLabel->setFont(TextEditor::TextEditorSettings::fontSettings().defaultFixedFontFamily());

    m_frameBuffer = new OpenMVPluginFB;
    QWidget *tempWidget0 = new QWidget;
    QVBoxLayout *tempLayout0 = new QVBoxLayout;
    tempLayout0->setContentsMargins(0, 0, 0, 0);
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

    connect(m_frameBuffer, &OpenMVPluginFB::pixmapUpdate, this, [beginRecordingButton] (const QPixmap &pixmap) {
        beginRecordingButton->setEnabled(!pixmap.isNull());
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

    connect(m_frameBuffer, &OpenMVPluginFB::imageWriterShutdown, this, [ beginRecordingButton, endRecordingButton, recordingLabel] {
        beginRecordingButton->setVisible(true);
        endRecordingButton->setVisible(false);
        recordingLabel->setVisible(false);
    });

    Utils::StyledBar *styledBar1 = new Utils::StyledBar;
    QHBoxLayout *styledBar1Layout = new QHBoxLayout;
    styledBar1Layout->setContentsMargins(0, 0, 0, 0);
    styledBar1Layout->setSpacing(0);
    styledBar1Layout->addSpacing(4);
    styledBar1Layout->addWidget(new QLabel(Tr::tr("Histogram")));
    styledBar1Layout->addSpacing(6);
    styledBar1->setLayout(styledBar1Layout);

    QComboBox *colorSpace = new QComboBox;
    colorSpace->setProperty("hideborder", true);
    colorSpace->setProperty("drawleftborder", false);
    colorSpace->insertItem(RGB_COLOR_SPACE, Tr::tr("RGB Color Space"));
    colorSpace->insertItem(GRAYSCALE_COLOR_SPACE, Tr::tr("Grayscale Color Space"));
    colorSpace->insertItem(LAB_COLOR_SPACE, Tr::tr("LAB Color Space"));
    colorSpace->insertItem(YUV_COLOR_SPACE, Tr::tr("YUV Color Space"));
    colorSpace->setCurrentIndex(RGB_COLOR_SPACE);
    colorSpace->setToolTip(Tr::tr("Use Grayscale/LAB for color tracking"));
    styledBar1Layout->addWidget(colorSpace);

    Utils::ElidingLabel *resLabel = new Utils::ElidingLabel(Tr::tr("Res - No Image"));
    resLabel->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    resLabel->setStyleSheet(QString(QStringLiteral("background-color:%1;color:%2;padding:4px;")).
                            arg(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal).name()).
                            arg(Utils::creatorTheme()->color(Utils::Theme::TextColorNormal).name()));
    resLabel->setAlignment(Qt::AlignCenter);

    m_histogram = new OpenMVPluginHistogram;
    QWidget *tempWidget1 = new QWidget;
    QVBoxLayout *tempLayout1 = new QVBoxLayout;
    tempLayout1->setContentsMargins(0, 0, 0, 0);
    tempLayout1->setSpacing(0);
    tempLayout1->addWidget(styledBar1);
    tempLayout1->addWidget(resLabel);
    tempLayout1->addWidget(m_histogram);
    tempWidget1->setLayout(tempLayout1);

    connect(colorSpace, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), m_histogram, &OpenMVPluginHistogram::colorSpaceChanged);
    connect(m_frameBuffer, &OpenMVPluginFB::pixmapUpdate, m_histogram, &OpenMVPluginHistogram::pixmapUpdate);

    connect(m_frameBuffer, &OpenMVPluginFB::resolutionAndROIUpdate, this, [resLabel] (const QSize &res, const QRect &roi) {
        if(res.isValid())
        {
            if(roi.isValid())
            {
                if((roi.width() > 1)
                || (roi.height() > 1))
                {
                    resLabel->setText(Tr::tr("Res (w:%1, h:%2) - ROI (x:%3, y:%4, w:%5, h:%6) - Pixels (%7)").arg(res.width()).arg(res.height()).arg(roi.x()).arg(roi.y()).arg(roi.width()).arg(roi.height()).arg(roi.width() * roi.height()));
                }
                else
                {
                    resLabel->setText(Tr::tr("Res (w:%1, h:%2) - Point (x:%3, y:%4)").arg(res.width()).arg(res.height()).arg(roi.x()).arg(roi.y()));
                }
            }
            else
            {
                resLabel->setText(Tr::tr("Res (w:%1, h:%2)").arg(res.width()).arg(res.height()));
            }
        }
        else
        {
            resLabel->setText(Tr::tr("Res - No Image"));
        }
    });

    Core::MiniSplitter *msplitter = widget->m_msplitter;
    Core::MiniSplitter *hsplitter = widget->m_hsplitter;
    Core::MiniSplitter *vsplitter = widget->m_vsplitter;
    vsplitter->insertWidget(0, tempWidget0);
    vsplitter->insertWidget(1, tempWidget1);
    vsplitter->setStretchFactor(0, 0);
    vsplitter->setStretchFactor(1, 1);
    vsplitter->setCollapsible(0, true);
    vsplitter->setCollapsible(1, true);

    connect(widget->m_leftDrawer, &QToolButton::clicked, this, [widget, hsplitter] {
        hsplitter->setSizes(QList<int>() << 1 << hsplitter->sizes().at(1));
        widget->m_leftDrawer->parentWidget()->hide();
    });

    connect(hsplitter, &Core::MiniSplitter::splitterMoved, this, [widget, hsplitter] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) widget->m_leftDrawer->parentWidget()->setVisible(!hsplitter->sizes().at(0));
    });

    connect(widget->m_rightDrawer, &QToolButton::clicked, this, [widget, hsplitter] {
        hsplitter->setSizes(QList<int>() << hsplitter->sizes().at(0) << 1);
        widget->m_rightDrawer->parentWidget()->hide();
    });

    connect(hsplitter, &Core::MiniSplitter::splitterMoved, this, [widget, hsplitter] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) widget->m_rightDrawer->parentWidget()->setVisible(!hsplitter->sizes().at(1));
    });

    connect(widget->m_topDrawer, &QToolButton::clicked, this, [widget, vsplitter] {
        vsplitter->setSizes(QList<int>() << 1 <<  vsplitter->sizes().at(1));
        widget->m_topDrawer->parentWidget()->hide();
        // Handle Special Case to fix 1px Graphical issue.
        vsplitter->setProperty("NoDrawToolBarBorders", false);
    });

    connect(vsplitter, &Core::MiniSplitter::splitterMoved, this, [widget, vsplitter] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) widget->m_topDrawer->parentWidget()->setVisible(!vsplitter->sizes().at(0));
        // Handle Special Case to fix 1px Graphical issue.
        vsplitter->setProperty("NoDrawToolBarBorders", widget->m_topDrawer->parentWidget()->isVisible());
    });

    connect(widget->m_bottomDrawer, &QToolButton::clicked, this, [widget, vsplitter] {
        vsplitter->setSizes(QList<int>() << vsplitter->sizes().at(0) << 1);
        widget->m_bottomDrawer->parentWidget()->hide();
    });

    connect(vsplitter, &Core::MiniSplitter::splitterMoved, this, [widget, vsplitter] (int pos, int index) {
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

        m_fpsLabel->setText(Tr::tr("FPS: %L1").arg(average ? (1000 / double(average)) : 0, 5, 'f', 1));
    });

    ///////////////////////////////////////////////////////////////////////////

    m_datasetEditor = new OpenMVDatasetEditor;
    connect(m_frameBuffer, &OpenMVPluginFB::pixmapUpdate, m_datasetEditor, &OpenMVDatasetEditor::frameBufferData);

    QLabel *dataSetEditorLabel = new QLabel(Tr::tr("Dataset Editor"));
    connect(m_datasetEditor, &OpenMVDatasetEditor::rootPathSet, this, [dataSetEditorLabel] (const QString &path) { dataSetEditorLabel->setToolTip(path); });

    Utils::StyledBar *datasetEditorStyledBar0 = new Utils::StyledBar;
    QHBoxLayout *datasetEditorStyledBarLayout0 = new QHBoxLayout;
    datasetEditorStyledBarLayout0->setContentsMargins(0, 0, 0, 0);
    datasetEditorStyledBarLayout0->setSpacing(0);
    datasetEditorStyledBarLayout0->addSpacing(4);
    datasetEditorStyledBarLayout0->addWidget(dataSetEditorLabel);
    datasetEditorStyledBarLayout0->addSpacing(6);
    datasetEditorStyledBar0->setLayout(datasetEditorStyledBarLayout0);

    QToolButton *datasetEditorCloseButton = new QToolButton;
    datasetEditorCloseButton->setIcon(Utils::Icons::CLOSE_TOOLBAR.icon());
    datasetEditorCloseButton->setToolTip(Tr::tr("Close"));
    datasetEditorStyledBarLayout0->addWidget(datasetEditorCloseButton);

    Utils::StyledBar *datasetEditorStyledBar1 = new Utils::StyledBar;
    QHBoxLayout *datasetEditorStyledBarLayout1 = new QHBoxLayout;
    datasetEditorStyledBarLayout1->setContentsMargins(0, 0, 0, 0);
    datasetEditorStyledBarLayout1->setSpacing(0);
    datasetEditorStyledBarLayout1->addSpacing(4);
    datasetEditorStyledBarLayout1->addWidget(new QLabel(Tr::tr("Image Preview")));
    datasetEditorStyledBarLayout1->addSpacing(6);
    datasetEditorStyledBar1->setLayout(datasetEditorStyledBarLayout1);

    OpenMVPluginFB *datasetEditorFB = new OpenMVPluginFB;
    datasetEditorFB->enableInteraction(false);
    datasetEditorFB->enableFitInView(true);
    connect(m_datasetEditor, &OpenMVDatasetEditor::pixmapUpdate, datasetEditorFB, &OpenMVPluginFB::frameBufferData);

    QAction *datasetEditorNewFolderAction = new QAction(QIcon(QStringLiteral(NEW_FOLDER_PATH)), Tr::tr("New Class Folder"), this);
    Core::Command *datasetEditorNewFolder = Core::ActionManager::registerAction(datasetEditorNewFolderAction, Utils::Id("OpenMV.NewClassFolder"));
    datasetEditorNewFolder->setDefaultKeySequence(QStringLiteral("Ctrl+Shift+N"));
    datasetEditorNewFolderAction->setEnabled(false);
    datasetEditorNewFolderAction->setVisible(false);
    connect(m_datasetEditor, &OpenMVDatasetEditor::visibilityChanged, datasetEditorNewFolderAction, &QAction::setEnabled);
    connect(datasetEditorNewFolderAction, &QAction::triggered, m_datasetEditor, &OpenMVDatasetEditor::newClassFolder);

    QAction *datasetEditorSnapshotAction = new QAction(QIcon(QStringLiteral(SNAPSHOT_PATH)), Tr::tr("Capture Data"), this);
    Core::Command *datasetEditorSnapshot = Core::ActionManager::registerAction(datasetEditorSnapshotAction, Utils::Id("OpenMV.CaptureData"));
    datasetEditorSnapshot->setDefaultKeySequence(QStringLiteral("Ctrl+Shift+S"));
    datasetEditorSnapshotAction->setEnabled(false);
    datasetEditorSnapshotAction->setVisible(false);
    connect(m_datasetEditor, &OpenMVDatasetEditor::snapshotEnable, datasetEditorSnapshotAction, &QAction::setEnabled);
    connect(datasetEditorSnapshotAction, &QAction::triggered, m_datasetEditor, &OpenMVDatasetEditor::snapshot);

    Core::Internal::FancyActionBar *datasetEditorActionBar = new Core::Internal::FancyActionBar(widget);
    widget->insertCornerWidget(2, datasetEditorActionBar);

    datasetEditorActionBar->insertAction(0, datasetEditorNewFolder->action());
    datasetEditorActionBar->insertAction(1, datasetEditorSnapshot->action());

    datasetEditorActionBar->setProperty("no_separator", true);
    datasetEditorActionBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    Core::MiniSplitter *datasetEditorWidget = new Core::MiniSplitter(Qt::Vertical);

    QWidget *datasetEditorWidgetTop = new QWidget;
    QVBoxLayout *datasetEditorLayoutTop = new QVBoxLayout;
    datasetEditorLayoutTop->setContentsMargins(0, 0, 0, 0);
    datasetEditorLayoutTop->setSpacing(0);
    datasetEditorLayoutTop->addWidget(datasetEditorStyledBar0);
    datasetEditorLayoutTop->addWidget(m_datasetEditor);
    datasetEditorWidgetTop->setLayout(datasetEditorLayoutTop);
    datasetEditorWidget->insertWidget(0, datasetEditorWidgetTop);

    QWidget *datasetEditorWidgetBottom = new QWidget;
    QVBoxLayout *datasetEditorLayoutBottom = new QVBoxLayout;
    datasetEditorLayoutBottom->setContentsMargins(0, 0, 0, 0);
    datasetEditorLayoutBottom->setSpacing(0);
    datasetEditorLayoutBottom->addWidget(datasetEditorStyledBar1);
    datasetEditorLayoutBottom->addWidget(datasetEditorFB);
    datasetEditorWidgetBottom->setLayout(datasetEditorLayoutBottom);
    datasetEditorWidget->insertWidget(1, datasetEditorWidgetBottom);

    connect(closeDatasetAction, &QAction::triggered, this, [this, datasetEditorWidget] { m_datasetEditor->setRootPath(QString()); datasetEditorWidget->hide(); });
    connect(datasetEditorCloseButton, &QToolButton::clicked, this, [this, datasetEditorWidget] { m_datasetEditor->setRootPath(QString()); datasetEditorWidget->hide(); });
    connect(m_datasetEditor, &OpenMVDatasetEditor::rootPathClosed, this, [] (const QString &path) { Core::EditorManager::closeEditors(Core::DocumentModel::editorsForFilePath(Utils::FilePath::fromString(path).pathAppended(QStringLiteral("/dataset_capture_script.py")))); });
    connect(m_datasetEditor, &OpenMVDatasetEditor::rootPathSet, datasetEditorWidget, &QWidget::show);
    connect(m_datasetEditor, &OpenMVDatasetEditor::visibilityChanged, this, [actionBar1, exportDataseFlatAction, uploadToEdgeImpulseProjectAction, uploadToEdgeImpulseByAPIKeyAction, closeDatasetAction, datasetEditorNewFolderAction, datasetEditorSnapshotAction, datasetEditorActionBar] (bool visible) {
        actionBar1->setSizePolicy(QSizePolicy::Preferred, visible ? QSizePolicy::Maximum : QSizePolicy::Minimum);
        exportDataseFlatAction->setEnabled(visible);
        uploadToEdgeImpulseProjectAction->setEnabled(visible && (!loggedIntoEdgeImpulse().isEmpty()));
        uploadToEdgeImpulseByAPIKeyAction->setEnabled(visible);
        closeDatasetAction->setEnabled(visible);
        datasetEditorNewFolderAction->setVisible(visible);
        datasetEditorSnapshotAction->setVisible(visible);
        datasetEditorActionBar->setVisible(visible);
    });

    exportDataseFlatAction->setDisabled(true);
    uploadToEdgeImpulseProjectAction->setDisabled(true);
    uploadToEdgeImpulseByAPIKeyAction->setDisabled(true);
    closeDatasetAction->setDisabled(true);
    datasetEditorActionBar->hide();
    datasetEditorWidget->hide();

    msplitter->insertWidget(0, datasetEditorWidget);
    msplitter->setStretchFactor(0, 0);
    msplitter->setCollapsible(0, false);

    ///////////////////////////////////////////////////////////////////////////

    m_boardLabel = new Utils::ElidingLabel(Tr::tr("Board:"));
    m_boardLabel->setToolTip(Tr::tr("Camera board type"));
    m_boardLabel->setDisabled(true);
    Core::ICore::statusBar()->addPermanentWidget(m_boardLabel);
    Core::ICore::statusBar()->addPermanentWidget(new QLabel());

    m_sensorLabel = new Utils::ElidingLabel(Tr::tr("Sensor:"));
    m_sensorLabel->setToolTip(Tr::tr("Camera sensor module"));
    m_sensorLabel->setDisabled(true);
    Core::ICore::statusBar()->addPermanentWidget(m_sensorLabel);
    Core::ICore::statusBar()->addPermanentWidget(new QLabel());

    m_versionButton = new Utils::ElidingToolButton;
    m_versionButton->setText(Tr::tr("Firmware Version:"));
    m_versionButton->setToolTip(Tr::tr("Camera firmware version"));
    m_versionButton->setDisabled(true);
    Core::ICore::statusBar()->addPermanentWidget(m_versionButton);
    Core::ICore::statusBar()->addPermanentWidget(new QLabel());
    connect(m_versionButton, &QToolButton::clicked, this, &OpenMVPlugin::updateCam);


    m_portLabel = new Utils::ElidingLabel(Tr::tr("Serial Port:"));
    m_portLabel->setToolTip(Tr::tr("Camera serial port"));
    m_portLabel->setDisabled(true);
    Core::ICore::statusBar()->addPermanentWidget(m_portLabel);
    Core::ICore::statusBar()->addPermanentWidget(new QLabel());

    m_pathButton = new Utils::ElidingToolButton;
    m_pathButton->setText(Tr::tr("Drive:"));
    m_pathButton->setToolTip(Tr::tr("Drive associated with port"));
    m_pathButton->setDisabled(true);
    Core::ICore::statusBar()->addPermanentWidget(m_pathButton);
    Core::ICore::statusBar()->addPermanentWidget(new QLabel());
    connect(m_pathButton, &QToolButton::clicked, this, &OpenMVPlugin::setPortPath);

    m_fpsLabel = new Utils::ElidingLabel(Tr::tr("FPS:"));
    m_fpsLabel->setToolTip(Tr::tr("May be different from camera FPS"));
    m_fpsLabel->setDisabled(true);
    m_fpsLabel->setMinimumWidth(m_fpsLabel->fontMetrics().horizontalAdvance(QStringLiteral("FPS: 000.000")));
    Core::ICore::statusBar()->addPermanentWidget(m_fpsLabel);

#ifdef Q_OS_MAC
    QApplication::setFont(m_boardLabel->font(), "QToolButton");
#endif

    ///////////////////////////////////////////////////////////////////////////

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));
    Core::EditorManager::restoreState(
        settings->value(QStringLiteral(EDITOR_MANAGER_STATE)).toByteArray());
    m_autoReconnectAction->setChecked(
        settings->value(QStringLiteral(AUTO_RECONNECT_STATE), m_autoReconnectAction->isChecked()).toBool());
    m_stopOnConnectDiconnectionAction->setChecked(
        settings->value(QStringLiteral(STOP_SCRIPT_CONNECT_DISCONNECT_STATE), m_stopOnConnectDiconnectionAction->isChecked()).toBool());
    m_connectAction->setEnabled(!m_autoReconnectAction->isChecked());
    m_disconnectAction->setEnabled(!m_autoReconnectAction->isChecked());
    if(m_autoReconnectAction->isChecked()) {
        static_cast<Utils::ProxyAction *>(m_connectCommand->action())->setOverrideToolTip(m_autoReconnectAction->toolTip());
        static_cast<Utils::ProxyAction *>(m_disconnectCommand->action())->setOverrideToolTip(m_autoReconnectAction->toolTip());
    } else {
        static_cast<Utils::ProxyAction *>(m_connectCommand->action())->setOverrideToolTip(QString());
        static_cast<Utils::ProxyAction *>(m_disconnectCommand->action())->setOverrideToolTip(QString());
    }
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
    Core::MessageManager::outputWindow()->setTabSettings(TextEditor::TextEditorSettings::codeStyle()->tabSettings().m_serialTerminalTabSize);
    connect(TextEditor::TextEditorSettings::codeStyle(), &TextEditor::ICodeStylePreferences::tabSettingsChanged, this, [] (const TextEditor::TabSettings &settings) {
        Core::MessageManager::outputWindow()->setTabSettings(settings.m_serialTerminalTabSize);
    });
    settings->endGroup();

    connect(Core::MessageManager::outputWindow(), &Core::OutputWindow::writeBytes, m_iodevice, &OpenMVPluginIO::mainTerminalInput);

    connect(q_check_ptr(qobject_cast<Core::Internal::MainWindow *>(Core::ICore::mainWindow())), &Core::Internal::MainWindow::showEventSignal, this, [this, widget, settings, msplitter, hsplitter, vsplitter] {
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));
        if(settings->contains(QStringLiteral(LAST_DATASET_EDITOR_PATH)) && settings->value(QStringLiteral(LAST_DATASET_EDITOR_LOADED)).toBool()) m_datasetEditor->setRootPath(settings->value(QStringLiteral(LAST_DATASET_EDITOR_PATH)).toString());
        if(settings->contains(QStringLiteral(MSPLITTER_STATE))) msplitter->restoreState(settings->value(QStringLiteral(MSPLITTER_STATE)).toByteArray());
        if(settings->contains(QStringLiteral(HSPLITTER_STATE))) hsplitter->restoreState(settings->value(QStringLiteral(HSPLITTER_STATE)).toByteArray());
        if(settings->contains(QStringLiteral(VSPLITTER_STATE))) vsplitter->restoreState(settings->value(QStringLiteral(VSPLITTER_STATE)).toByteArray());
        widget->m_leftDrawer->parentWidget()->setVisible(settings->contains(QStringLiteral(HSPLITTER_STATE)) ? (!hsplitter->sizes().at(0)) : false);
        widget->m_rightDrawer->parentWidget()->setVisible(settings->contains(QStringLiteral(HSPLITTER_STATE)) ? (!hsplitter->sizes().at(1)) : false);
        widget->m_topDrawer->parentWidget()->setVisible(settings->contains(QStringLiteral(VSPLITTER_STATE)) ? (!vsplitter->sizes().at(0)) : false);
        widget->m_bottomDrawer->parentWidget()->setVisible(settings->contains(QStringLiteral(VSPLITTER_STATE)) ? (!vsplitter->sizes().at(1)) : false);
        settings->endGroup();
        // Handle Special Case to fix 1px Graphical issue.
        vsplitter->setProperty("NoDrawToolBarBorders", widget->m_topDrawer->parentWidget()->isVisible());
    });

    connect(q_check_ptr(qobject_cast<Core::Internal::MainWindow *>(Core::ICore::mainWindow())), &Core::Internal::MainWindow::hideEventSignal, this, [this, settings, msplitter, hsplitter, vsplitter] {
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));
        if(!isNoShow()) settings->setValue(QStringLiteral(LAST_DATASET_EDITOR_LOADED),
            !m_datasetEditor->rootPath().isEmpty());
        if(!isNoShow()) settings->setValue(QStringLiteral(MSPLITTER_STATE),
            msplitter->saveState());
        if(!isNoShow()) settings->setValue(QStringLiteral(HSPLITTER_STATE),
            hsplitter->saveState());
        if(!isNoShow()) settings->setValue(QStringLiteral(VSPLITTER_STATE),
            vsplitter->saveState());
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

    connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested, this, [this, zoomButton, colorSpace, msplitter, hsplitter, vsplitter] {
        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));
        settings->setValue(QStringLiteral(EDITOR_MANAGER_STATE),
            Core::EditorManager::saveState());
        if(!isNoShow()) settings->setValue(QStringLiteral(LAST_DATASET_EDITOR_LOADED),
            !m_datasetEditor->rootPath().isEmpty());
        if(!isNoShow()) settings->setValue(QStringLiteral(MSPLITTER_STATE),
            msplitter->saveState());
        if(!isNoShow()) settings->setValue(QStringLiteral(HSPLITTER_STATE),
            hsplitter->saveState());
        if(!isNoShow()) settings->setValue(QStringLiteral(VSPLITTER_STATE),
            vsplitter->saveState());
        settings->setValue(QStringLiteral(AUTO_RECONNECT_STATE),
            m_autoReconnectAction->isChecked());
        settings->setValue(QStringLiteral(STOP_SCRIPT_CONNECT_DISCONNECT_STATE),
            m_stopOnConnectDiconnectionAction->isChecked());
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

    if(!editor)
    {
        QList<Core::IEditor *> editors = Core::EditorManager::visibleEditors();

        if(!editors.isEmpty())
        {
            editor = editors.first();
        }
    }

    if(editor ? (editor->document() ? editor->document()->contents().isEmpty() : true) : true)
    {
        QString filePath = Core::ICore::userResourcePath(QStringLiteral("examples/00-HelloWorld/helloworld.py")).toString();

        QFile file(filePath);

        if(file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();

            if((file.error() == QFile::NoError) && (!data.isEmpty()))
            {
                if((m_sensorType == QStringLiteral("HM01B0")) ||
                   (m_sensorType == QStringLiteral("HM0360")) ||
                   (m_sensorType == QStringLiteral("MT9V0X2")) ||
                   (m_sensorType == QStringLiteral("MT9V0X4")))
                {
                    data = data.replace(QByteArrayLiteral("sensor.set_pixformat(sensor.RGB565)"), QByteArrayLiteral("sensor.set_pixformat(sensor.GRAYSCALE)"));
                    if(m_sensorType == QStringLiteral("HM01B0")) data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"), QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
                }

                Core::EditorManager::cutForwardNavigationHistory();
                Core::EditorManager::addCurrentPositionToNavigationHistory();

                QString titlePattern = QFileInfo(filePath).baseName().simplified() + QStringLiteral("_$.") + QFileInfo(filePath).completeSuffix();
                TextEditor::BaseTextEditor *editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::openEditorWithContents(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID, &titlePattern, data));

                if(editor)
                {
                    editor->document()->setProperty("diffFilePath", filePath);
                    Core::EditorManager::addCurrentPositionToNavigationHistory();
                    editor->editorWidget()->configureGenericHighlighter();
                    Core::EditorManager::activateEditor(editor);
                }
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////

    QLoggingCategory::setFilterRules(QStringLiteral("qt.network.ssl.warning=false")); // https://stackoverflow.com/questions/26361145/qsslsocket-error-when-ssl-is-not-used

    if(!isNoShow()) connect(Core::ICore::instance(), &Core::ICore::coreOpened, this, [this] {

        QNetworkAccessManager *manager = new QNetworkAccessManager(this);

        connect(manager, &QNetworkAccessManager::finished, this, [this, manager] (QNetworkReply *reply) {

            QByteArray data = reply->readAll();

            if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
            {
                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^(\\d+)\\.(\\d+)\\.(\\d+)$")).match(QString::fromUtf8(data).trimmed());

                int major = match.captured(1).toInt();
                int minor = match.captured(2).toInt();
                int patch = match.captured(3).toInt();

                if((IDE_VERSION_MAJOR < major)
                || ((IDE_VERSION_MAJOR == major) && (IDE_VERSION_MINOR < minor))
                || ((IDE_VERSION_MAJOR == major) && (IDE_VERSION_MINOR == minor) && (IDE_VERSION_RELEASE < patch)))
                {
                    QMessageBox box(QMessageBox::Information, Tr::tr("Update Available"), Tr::tr("A new version of OpenMV IDE (%L1.%L2.%L3) is available for download.").arg(major).arg(minor).arg(patch), QMessageBox::Cancel, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    QPushButton *button = box.addButton(Tr::tr("Download"), QMessageBox::AcceptRole);
                    box.setDefaultButton(button);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    if(box.clickedButton() == button)
                    {
                        QUrl url = QUrl(QStringLiteral("https://openmv.io/pages/download"));

                        if(!QDesktopServices::openUrl(url))
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                                  QString(),
                                                  Tr::tr("Failed to open: \"%L1\"").arg(url.toString()));
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

            connect(reply, &QNetworkReply::destroyed, manager, &QNetworkAccessManager::deleteLater); reply->deleteLater();
        });

        QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("https://raw.githubusercontent.com/openmv/openmv-ide-version/main/openmv-ide-version.txt")));
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

                        if(!m_availableWifiPorts.contains(wifiPort))
                        {
                            m_availableWifiPorts.append(wifiPort);
                        }
                        else
                        {
                            m_availableWifiPorts[m_availableWifiPorts.indexOf(wifiPort)].time = QTime::currentTime();
                        }
                    }
                }
            }
        }
    });

    // Scan Serial Ports
    {
        QThread *thread = new QThread;
        ScanSerialPortsThread *scanSerialPortsThread = new ScanSerialPortsThread();
        scanSerialPortsThread->moveToThread(thread);
        QTimer *timer = new QTimer(this);

        connect(timer, &QTimer::timeout,
                scanSerialPortsThread, &ScanSerialPortsThread::scanSerialPortsSlot);

        connect(scanSerialPortsThread, &ScanSerialPortsThread::serialPorts, this, [this] (const QList<MyQSerialPortInfo> &output) {
            QTime currentTime = QTime::currentTime();

            for(QList<wifiPort_t>::iterator it = m_availableWifiPorts.begin(); it != m_availableWifiPorts.end(); )
            {
                if(qAbs(it->time.secsTo(currentTime)) >= WIFI_PORT_RETIRE)
                {
                    it = m_availableWifiPorts.erase(it);
                }
                else
                {
                    it++;
                }
            }

            bool ok = false;

            foreach(const MyQSerialPortInfo &port, output)
            {
                if(port.hasVendorIdentifier() && port.hasProductIdentifier()
                && (m_serialNumberFilter.isEmpty() || (m_serialNumberFilter == port.serialNumber().toUpper()))
                && (((port.vendorIdentifier() == OPENMVCAM_VID) && (port.productIdentifier() == OPENMVCAM_PID) && (port.serialNumber() != QStringLiteral("000000000010")) && (port.serialNumber() != QStringLiteral("000000000011")))
                || ((port.vendorIdentifier() == ARDUINOCAM_VID) && (((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||
                                                                    ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||
                                                                    ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||
                                                                    ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID)))
                || ((port.vendorIdentifier() == RPI2040_VID) && (port.productIdentifier() == RPI2040_PID))))
                {
                    ok = true;
                    break;
                }
            }

            bool dark = Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface);

            if(!ok) {
                if(!m_availableWifiPorts.isEmpty()) {
                    m_connectCommand->action()->setIcon(QIcon(dark ? QStringLiteral(CONNECT_WIFI_DARK_PATH) : QStringLiteral(CONNECT_WIFI_LIGHT_PATH)));
                } else {
                    m_connectCommand->action()->setIcon(QIcon(QStringLiteral(CONNECT_PATH)));
                }
            } else {
                if(!m_availableWifiPorts.isEmpty()) {
                    m_connectCommand->action()->setIcon(QIcon(dark ? QStringLiteral(CONNECT_USB_WIFI_DARK_PATH) : QStringLiteral(CONNECT_USB_WIFI_LIGHT_PATH)));
                } else {
                    m_connectCommand->action()->setIcon(QIcon(dark ? QStringLiteral(CONNECT_USB_DARK_PATH) : QStringLiteral(CONNECT_USB_LIGHT_PATH)));
                }
            }

            if(ok && m_autoReconnectAction->isChecked() && (!m_working) && (!m_connected))
            {
                QTimer::singleShot(1000, this, [this] { if(m_autoReconnectAction->isChecked() && (!m_working) && (!m_connected)) connectClicked(); });
            }
        });

        connect(this, &OpenMVPlugin::destroyed,
                scanSerialPortsThread, &ScanSerialPortsThread::deleteLater);

        connect(scanSerialPortsThread, &ScanSerialPortsThread::destroyed,
                thread, &QThread::quit);

        connect(thread, &QThread::finished,
                thread, &QThread::deleteLater);

        thread->start();
        timer->start(1000);
        QTimer::singleShot(0, scanSerialPortsThread, &ScanSerialPortsThread::scanSerialPortsSlot);
    }

    if(!socket->bind(OPENMVCAM_BROADCAST_PORT))
    {
        delete socket;

        if(!isNoShow()) QMessageBox::warning(Core::ICore::dialogParent(),
            Tr::tr("WiFi Programming Disabled!"),
            Tr::tr("Another application is using the OpenMV Cam broadcast discovery port. "
               "Please close that application and restart OpenMV IDE to enable WiFi programming."));
    }

    if(!QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV")))
    {
        QMessageBox::warning(Core::ICore::dialogParent(),
                    Tr::tr("Documents Folder Error"),
                    Tr::tr("Failed to create the documents folder!"));
    }

    if(Core::EditorManager::currentEditor()
        ? Core::EditorManager::currentEditor()->document()
            ? Core::EditorManager::currentEditor()->document()->displayName() == QStringLiteral("helloworld_1.py")
            : false
        : false)
    {
        QTimer::singleShot(2000, this, [] {
            Utils::CheckableMessageBox::doNotShowAgainInformation(Core::ICore::dialogParent(),
                    Tr::tr("OpenMV Cam LED Colors"),
                    Tr::tr("Thanks for using the OpenMV Cam and OpenMV IDE!\n\n"
                       "Your OpenMV Cam's onboard LED blinks with diffent colors to indicate its state:\n\n"
                       "Blinking Green:\n\nYour OpenMV Cam's onboard bootloader is running. "
                       "The onboard bootloader runs for a few seconds when your OpenMV Cam is powered via USB to allow OpenMV IDE to reprogram your OpenMV Cam.\n\n"
                       "Blinking Blue:\n\nYour OpenMV Cam is running the default main.py script onboard.\n\n"
                       "If you have an SD card installed or overwrote the main.py script on your OpenMV Cam then it will run whatever code you loaded on it instead.\n\n"
                       "If the LED is blinking blue but OpenMV IDE can't connect to your OpenMV Cam "
                       "please make sure you are connecting your OpenMV Cam to your PC with a USB cable that supplies both data and power.\n\n"
                       "Blinking White:\n\nYour OpenMV Cam's firmware is panicking because of a hardware failure. "
                       "Please check that your OpenMV Cam's camera module is installed securely.\n\n"),
                    ExtensionSystem::PluginManager::settings(),
                    QStringLiteral(DONT_SHOW_LED_STATES_AGAIN),
                    QDialogButtonBox::Ok,
                    QDialogButtonBox::Ok);
        });
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
                        QString displayName = Tr::tr("Serial Port - %L1 - %L2 BPS").arg(portNameValue).arg(baudRateValue);
                        OpenMVTerminal *terminal = new OpenMVTerminal(displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(displayName)), true);
                        OpenMVTerminalSerialPort *terminalDevice = new OpenMVTerminalSerialPort(terminal);

                        connect(terminal, &OpenMVTerminal::writeBytes,
                                terminalDevice, &OpenMVTerminalPort::writeBytes);

                        connect(terminal, &OpenMVTerminal::execScript,
                                terminalDevice, &OpenMVTerminalPort::execScript);

                        connect(terminal, &OpenMVTerminal::interruptScript,
                                terminalDevice, &OpenMVTerminalPort::interruptScript);

                        connect(terminal, &OpenMVTerminal::reloadScript,
                                terminalDevice, &OpenMVTerminalPort::reloadScript);

                        connect(terminal, &OpenMVTerminal::paste,
                                terminalDevice, &OpenMVTerminalPort::paste);

                        connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                terminal, &OpenMVTerminal::readBytes);

                        QString errorMessage2 = QString();
                        QString *errorMessage2Ptr = &errorMessage2;

                        QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                            this, [errorMessage2Ptr] (const QString &errorMessage) {
                            *errorMessage2Ptr = errorMessage;
                        });

                        // QProgressDialog scoping...
                        {
                            QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Q_NULLPTR,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                            QString errorMessage = Tr::tr("Error: %L1!").arg(errorMessage2);

                            if(Utils::HostOsInfo::isLinuxHost() && errorMessage2.contains(QStringLiteral("Permission Denied"), Qt::CaseInsensitive))
                            {
                                errorMessage += Tr::tr("\n\nTry doing:\n\nsudo adduser %L1 dialout\n\n...in a terminal and then restart your computer.").arg(Utils::Environment::systemEnvironment().toDictionary().userName());
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
                        displayError(Tr::tr("Invalid baud rate argument (%1) for -open_serial_terminal").arg(list.at(1)));
                    }
                }
                else
                {
                    displayError(Tr::tr("-open_serial_terminal requires two arguments <port_name:baud_rate>"));
                }
            }
            else
            {
                displayError(Tr::tr("Missing arguments for -open_serial_terminal"));
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
                        QString displayName = Tr::tr("UDP Client Connection - %1:%2").arg(hostNameValue).arg(portValue);
                        OpenMVTerminal *terminal = new OpenMVTerminal(displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(displayName)), true);
                        OpenMVTerminalUDPPort *terminalDevice = new OpenMVTerminalUDPPort(terminal);

                        connect(terminal, &OpenMVTerminal::writeBytes,
                                terminalDevice, &OpenMVTerminalPort::writeBytes);

                        connect(terminal, &OpenMVTerminal::execScript,
                                terminalDevice, &OpenMVTerminalPort::execScript);

                        connect(terminal, &OpenMVTerminal::interruptScript,
                                terminalDevice, &OpenMVTerminalPort::interruptScript);

                        connect(terminal, &OpenMVTerminal::reloadScript,
                                terminalDevice, &OpenMVTerminalPort::reloadScript);

                        connect(terminal, &OpenMVTerminal::paste,
                                terminalDevice, &OpenMVTerminalPort::paste);

                        connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                terminal, &OpenMVTerminal::readBytes);

                        QString errorMessage2 = QString();
                        QString *errorMessage2Ptr = &errorMessage2;

                        QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                            this, [errorMessage2Ptr] (const QString &errorMessage) {
                            *errorMessage2Ptr = errorMessage;
                        });

                        // QProgressDialog scoping...
                        {
                            QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Q_NULLPTR,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                            QString errorMessage = Tr::tr("Error: %L1!").arg(errorMessage2);

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
                        displayError(Tr::tr("Invalid port argument (%1) for -open_udp_client_terminal").arg(list.at(1)));
                    }
                }
                else
                {
                    displayError(Tr::tr("-open_udp_client_terminal requires two arguments <host_name:port>"));
                }
            }
            else
            {
                displayError(Tr::tr("Missing arguments for -open_udp_client_terminal"));
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
                    QString displayName = Tr::tr("UDP Server Connection - %1").arg(portValue);
                    OpenMVTerminal *terminal = new OpenMVTerminal(displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(displayName)), true);
                    OpenMVTerminalUDPPort *terminalDevice = new OpenMVTerminalUDPPort(terminal);

                    connect(terminal, &OpenMVTerminal::writeBytes,
                            terminalDevice, &OpenMVTerminalPort::writeBytes);

                    connect(terminal, &OpenMVTerminal::execScript,
                            terminalDevice, &OpenMVTerminalPort::execScript);

                    connect(terminal, &OpenMVTerminal::interruptScript,
                            terminalDevice, &OpenMVTerminalPort::interruptScript);

                    connect(terminal, &OpenMVTerminal::reloadScript,
                            terminalDevice, &OpenMVTerminalPort::reloadScript);

                    connect(terminal, &OpenMVTerminal::paste,
                            terminalDevice, &OpenMVTerminalPort::paste);

                    connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                            terminal, &OpenMVTerminal::readBytes);

                    QString errorMessage2 = QString();
                    QString *errorMessage2Ptr = &errorMessage2;

                    QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                        this, [errorMessage2Ptr] (const QString &errorMessage) {
                        *errorMessage2Ptr = errorMessage;
                    });

                    // QProgressDialog scoping...
                    {
                        QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Q_NULLPTR,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                            (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                        QString errorMessage = Tr::tr("Error: %L1!").arg(errorMessage2);

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
                    displayError(Tr::tr("Invalid port argument (%1) for -open_udp_server_terminal").arg(options.at(i)));
                }
            }
            else
            {
                displayError(Tr::tr("Missing arguments for -open_udp_server_terminal"));
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
                        QString displayName = Tr::tr("TCP Client Connection - %1:%2").arg(hostNameValue).arg(portValue);
                        OpenMVTerminal *terminal = new OpenMVTerminal(displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(displayName)), true);
                        OpenMVTerminalTCPPort *terminalDevice = new OpenMVTerminalTCPPort(terminal);

                        connect(terminal, &OpenMVTerminal::writeBytes,
                                terminalDevice, &OpenMVTerminalPort::writeBytes);

                        connect(terminal, &OpenMVTerminal::execScript,
                                terminalDevice, &OpenMVTerminalPort::execScript);

                        connect(terminal, &OpenMVTerminal::interruptScript,
                                terminalDevice, &OpenMVTerminalPort::interruptScript);

                        connect(terminal, &OpenMVTerminal::reloadScript,
                                terminalDevice, &OpenMVTerminalPort::reloadScript);

                        connect(terminal, &OpenMVTerminal::paste,
                                terminalDevice, &OpenMVTerminalPort::paste);

                        connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                terminal, &OpenMVTerminal::readBytes);

                        QString errorMessage2 = QString();
                        QString *errorMessage2Ptr = &errorMessage2;

                        QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                            this, [errorMessage2Ptr] (const QString &errorMessage) {
                            *errorMessage2Ptr = errorMessage;
                        });

                        // QProgressDialog scoping...
                        {
                            QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Q_NULLPTR,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                            QString errorMessage = Tr::tr("Error: %L1!").arg(errorMessage2);

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
                        displayError(Tr::tr("Invalid port argument (%1) for -open_tcp_client_terminal").arg(list.at(1)));
                    }
                }
                else
                {
                    displayError(Tr::tr("-open_tcp_client_terminal requires two arguments <host_name:port>"));
                }
            }
            else
            {
                displayError(Tr::tr("Missing arguments for -open_tcp_client_terminal"));
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
                    QString displayName = Tr::tr("TCP Server Connection - %1").arg(portValue);
                    OpenMVTerminal *terminal = new OpenMVTerminal(displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(displayName)), true);
                    OpenMVTerminalTCPPort *terminalDevice = new OpenMVTerminalTCPPort(terminal);

                    connect(terminal, &OpenMVTerminal::writeBytes,
                            terminalDevice, &OpenMVTerminalPort::writeBytes);

                    connect(terminal, &OpenMVTerminal::execScript,
                            terminalDevice, &OpenMVTerminalPort::execScript);

                    connect(terminal, &OpenMVTerminal::interruptScript,
                            terminalDevice, &OpenMVTerminalPort::interruptScript);

                    connect(terminal, &OpenMVTerminal::reloadScript,
                            terminalDevice, &OpenMVTerminalPort::reloadScript);

                    connect(terminal, &OpenMVTerminal::paste,
                            terminalDevice, &OpenMVTerminalPort::paste);

                    connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                            terminal, &OpenMVTerminal::readBytes);

                    QString errorMessage2 = QString();
                    QString *errorMessage2Ptr = &errorMessage2;

                    QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                        this, [errorMessage2Ptr] (const QString &errorMessage) {
                        *errorMessage2Ptr = errorMessage;
                    });

                    // QProgressDialog scoping...
                    {
                        QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Q_NULLPTR,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                            (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                        QString errorMessage = Tr::tr("Error: %L1!").arg(errorMessage2);

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
                    displayError(Tr::tr("Invalid port argument (%1) for -open_tcp_server_terminal").arg(options.at(i)));
                }
            }
            else
            {
                displayError(Tr::tr("Missing arguments for -open_tcp_server_terminal"));
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
        QTimer::singleShot(0, this, [] { QApplication::exit(-1); });
    }

    return Q_NULLPTR;
}

void OpenMVPlugin::registerOpenMVCam(const QString board, const QString id)
{
    if(!m_formKey.isEmpty())
    {
        QNetworkAccessManager manager(this);
        QEventLoop loop;

        connect(&manager, &QNetworkAccessManager::finished, &loop, &QEventLoop::quit);

        QNetworkRequest request = QNetworkRequest(QUrl(QString(QStringLiteral("https://upload.openmv.io/openmv-swd-ids-insert.php?board=%L1&id=%L2&form_key=%L3")).arg(board).arg(id).arg(m_formKey)));
        QNetworkReply *reply = manager.get(request);

        if(reply)
        {
            connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));

            loop.exec();

            QByteArray data = reply->readAll();

            QTimer::singleShot(0, reply, &QNetworkReply::deleteLater);

            if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
            {
                QString text = QString::fromUtf8(data);
                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("Remaining\\s+(\\d+)")).match(text);

                if(match.hasMatch())
                {
                    QMessageBox::information(Core::ICore::dialogParent(),
                        Tr::tr("Register OpenMV Cam"),
                        Tr::tr("OpenMV Cam automatically registered!\n\nBoard: %1\nID: %2\n\n%3 Board Keys remaining for registering board type: %1\n\n"
                               "Please run Examples->HelloWorld->helloworld.py to test the vision quality and focus the camera (if applicable).").arg(board).arg(id).arg(match.captured(1)));

                    return;
                }
                else if(text.contains(QStringLiteral("Done")))
                {
                    QMessageBox::information(Core::ICore::dialogParent(),
                        Tr::tr("Register OpenMV Cam"),
                        Tr::tr("OpenMV Cam automatically registered!\n\nBoard: %1\nID: %2\n\nPlease run Examples->HelloWorld->helloworld.py to test the vision quality and focus the camera (if applicable).").arg(board).arg(id));

                    return;
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Register OpenMV Cam"),
                        Tr::tr("Database Error!"));

                    return;
                }
            }
            else if(reply->error() != QNetworkReply::NoError)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Register OpenMV Cam"),
                    Tr::tr("Error: %L1!").arg(reply->error()));

                return;
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Register OpenMV Cam"),
                    Tr::tr("GET Network error!"));

                return;
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Register OpenMV Cam"),
                Tr::tr("GET network error!"));

            return;
        }
    }

    if(QMessageBox::warning(Core::ICore::dialogParent(),
        Tr::tr("Unregistered OpenMV Cam Detected"),
        Tr::tr("Your OpenMV Cam isn't registered. You need to register your OpenMV Cam with OpenMV for unlimited use with OpenMV IDE without any interruptions.\n\n"
           "Would you like to register your OpenMV Cam now?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
    == QMessageBox::Yes)
    {
        if(registerOpenMVCamDialog(board, id)) return;
    }

    if(QMessageBox::warning(Core::ICore::dialogParent(),
        Tr::tr("Unregistered OpenMV Cam Detected"),
        Tr::tr("Unregistered OpenMV Cams hurt the open-source OpenMV ecosystem by undercutting offical OpenMV Cam sales which help fund OpenMV Cam software development.\n\n"
           "Would you like to register your OpenMV Cam now?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
    == QMessageBox::Yes)
    {
        if(registerOpenMVCamDialog(board, id)) return;
    }

    if(QMessageBox::warning(Core::ICore::dialogParent(),
        Tr::tr("Unregistered OpenMV Cam Detected"),
        Tr::tr("OpenMV IDE will display these three messages boxes each time you connect until you register your OpenMV Cam...\n\n"
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
        dialog->setWindowTitle(Tr::tr("Register OpenMV Cam"));
        QVBoxLayout *layout = new QVBoxLayout(dialog);

        QLabel *label = new QLabel(Tr::tr("Please enter a board key to register your OpenMV Cam.<br/><br/>If you do not have a board key you may purchase one from OpenMV <a href=\"https://openmv.io/products/openmv-cam-board-key\">here</a>."));
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
                QProgressDialog dialog(Tr::tr("Registering OpenMV Cam..."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));

                connect(&dialog, &QProgressDialog::canceled, &dialog, &QProgressDialog::reject);
                connect(&manager, &QNetworkAccessManager::finished, &dialog, &QProgressDialog::accept);

                QNetworkRequest request = QNetworkRequest(QUrl(QString(QStringLiteral("https://upload.openmv.io/openmv-swd-ids-register.php?board=%1&id=%2&id_key=%3")).arg(board).arg(id).arg(boardKey)));
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
                            QString text = QString::fromUtf8(data);

                            if(text.contains(QStringLiteral("<p>Done</p>")))
                            {
                                QMessageBox::information(Core::ICore::dialogParent(),
                                    Tr::tr("Register OpenMV Cam"),
                                    Tr::tr("Thank you for registering your OpenMV Cam!"));

                                return true;
                            }
                            else if(text.contains(QStringLiteral("<p>Error: Invalid ID Key for board type!</p>")))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Register OpenMV Cam"),
                                    Tr::tr("Invalid Board Key for Board Type!"));
                            }
                            else if(text.contains(QStringLiteral("<p>Error: Invalid ID Key!</p>")))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Register OpenMV Cam"),
                                    Tr::tr("Invalid Board Key!"));
                            }
                            else if(text.contains(QStringLiteral("<p>Error: ID Key already used!</p>")))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Register OpenMV Cam"),
                                    Tr::tr("Board Key already used!"));
                            }
                            else if(text.contains(QStringLiteral("<p>Error: Board and ID already registered!</p>")))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Register OpenMV Cam"),
                                    Tr::tr("Board and ID already registered!"));
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Register OpenMV Cam"),
                                    Tr::tr("Database Error!"));
                            }
                        }
                        else if(reply->error() != QNetworkReply::NoError)
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Register OpenMV Cam"),
                                Tr::tr("Error: %L1!").arg(reply->error()));
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Register OpenMV Cam"),
                                Tr::tr("GET Network error!"));
                        }
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Register OpenMV Cam"),
                        Tr::tr("GET network error!"));
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Register OpenMV Cam"),
                    Tr::tr("Invalidly formatted Board Key!"));
            }
        }
        else
        {
            return false;
        }
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
                m_fpsLabel->setText(Tr::tr("FPS: 0"));
            }
        }
    }
}

void OpenMVPlugin::errorFilter(const QByteArray &data)
{
    m_errorFilterString.append(QString::fromUtf8(data).replace(QStringLiteral("\r\n"), QStringLiteral("\n")));

    QRegularExpressionMatch match;
    int index = m_errorFilterString.indexOf(m_errorFilterRegex, 0, &match);

    if(index != -1)
    {
        QString fileName = match.captured(1);
        int lineNumber = match.captured(2).toInt();
        QString errorMessage = Tr::tr(match.captured(3).toUtf8().data());

        bool builtinModule = false;
        QString builtinModuleName = QFileInfo(fileName).baseName();

        foreach(const documentation_t &d, m_modules)
        {
            if(d.name == builtinModuleName)
            {
                builtinModule = true;
                break;
            }
        }

        TextEditor::BaseTextEditor *editor = Q_NULLPTR;

        if(!builtinModule)
        {
            Core::EditorManager::cutForwardNavigationHistory();
            Core::EditorManager::addCurrentPositionToNavigationHistory();

            if(fileName == QStringLiteral("<stdin>"))
            {
                editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::currentEditor());
            }
            else if(!m_portPath.isEmpty())
            {
                editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::openEditor(Utils::FilePath::fromString(m_portPath).pathAppended(fileName)));
            }
        }

        if(editor)
        {
            Core::EditorManager::addCurrentPositionToNavigationHistory();
            editor->gotoLine(lineNumber);
            editor->editorWidget()->gotoLineEndWithSelection();
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
            Tr::tr("Configure Settings"),
            Tr::tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::saveScript()
{
    if(!m_working)
    {
        int answer = QMessageBox::question(Core::ICore::dialogParent(),
            Tr::tr("Save Script"),
            Tr::tr("Strip comments and convert spaces to tabs?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

        if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))
        {
            QByteArray contents = Core::EditorManager::currentEditor() ? Core::EditorManager::currentEditor()->document() ? Core::EditorManager::currentEditor()->document()->contents() : QByteArray() : QByteArray();

            if(importHelper(contents))
            {
                Utils::FileSaver file(Utils::FilePath::fromString(m_portPath).pathAppended(QStringLiteral("main.py")));

                if(!file.hasError())
                {
                    if(answer == QMessageBox::Yes)
                    {
                        contents = loadFilter(contents);
                    }

                    if((!file.write(contents)) || (!file.finalize()))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Save Script"),
                            Tr::tr("Error: %L1!").arg(file.errorString()));
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
                        Tr::tr("Save Script"),
                        Tr::tr("Error: %L1!").arg(file.errorString()));
                }
            }
        }
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Save Script"),
            Tr::tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::saveImage(const QPixmap &data)
{
    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    QString path;

    forever
    {
        path =
        QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Save Image"),
            settings->value(QStringLiteral(LAST_SAVE_IMAGE_PATH), QDir::homePath()).toString(),
            Tr::tr("Image Files (*.bmp *.jpg *.jpeg *.png *.ppm)"));

        if((!path.isEmpty()) && QFileInfo(path).completeSuffix().isEmpty())
        {
            QMessageBox::warning(Core::ICore::dialogParent(),
                Tr::tr("Save Image"),
                Tr::tr("Please add a file extension!"));

            continue;
        }

        break;
    }

    if(!path.isEmpty())
    {
        if(data.save(path))
        {
            settings->setValue(QStringLiteral(LAST_SAVE_IMAGE_PATH), path);
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Save Image"),
                Tr::tr("Failed to save the image file for an unknown reason!"));
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

        QString path;

        forever
        {
            path =
            QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Save Template"),
                settings->value(QStringLiteral(LAST_SAVE_TEMPLATE_PATH), drivePath).toString(),
                Tr::tr("Image Files (*.bmp *.jpg *.jpeg *.pgm *.ppm)"));

            if((!path.isEmpty()) && QFileInfo(path).completeSuffix().isEmpty())
            {
                QMessageBox::warning(Core::ICore::dialogParent(),
                    Tr::tr("Save Template"),
                    Tr::tr("Please add a file extension!"));

                continue;
            }

            break;
        }

        if(!path.isEmpty())
        {
            path = QDir::cleanPath(QDir::fromNativeSeparators(path));

            if((!path.startsWith(drivePath))
            || (!QDir(QFileInfo(path).path()).exists()))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Save Template"),
                    Tr::tr("Please select a valid path on the OpenMV Cam!"));
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
                        Tr::tr("Save Template"),
                        Tr::tr("\"%L1\" is longer than a max length of %L2 characters!").arg(QString::fromUtf8(sendPath)).arg(DESCRIPTOR_SAVE_PATH_MAX_LEN));
                }
            }
        }

        settings->endGroup();
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Save Template"),
            Tr::tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::saveDescriptor(const QRect &rect)
{
    if(!m_working)
    {
        QString drivePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath));

        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QString path;

        forever
        {
            path =
            QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Save Descriptor"),
                settings->value(QStringLiteral(LAST_SAVE_DESCRIPTOR_PATH), drivePath).toString(),
                Tr::tr("Keypoints Files (*.lbp *.orb)"));

            if((!path.isEmpty()) && QFileInfo(path).completeSuffix().isEmpty())
            {
                QMessageBox::warning(Core::ICore::dialogParent(),
                    Tr::tr("Save Descriptor"),
                    Tr::tr("Please add a file extension!"));

                continue;
            }

            break;
        }

        if(!path.isEmpty())
        {
            path = QDir::cleanPath(QDir::fromNativeSeparators(path));

            if((!path.startsWith(drivePath))
            || (!QDir(QFileInfo(path).path()).exists()))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Save Descriptor"),
                    Tr::tr("Please select a valid path on the OpenMV Cam!"));
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
                        Tr::tr("Save Descriptor"),
                        Tr::tr("\"%L1\" is longer than a max length of %L2 characters!").arg(QString::fromUtf8(sendPath)).arg(DESCRIPTOR_SAVE_PATH_MAX_LEN));
                }
            }
        }

        settings->endGroup();
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Save Descriptor"),
            Tr::tr("Busy... please wait..."));
    }
}

QMultiMap<QString, QAction *> OpenMVPlugin::aboutToShowExamplesRecursive(const QString &path, QMenu *parent, bool notExamples)
{
    QMultiMap<QString, QAction *> actions;
    QDirIterator it(path, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    while(it.hasNext())
    {
        QString filePath = it.next();

        if(it.fileInfo().isDir())
        {
            QMenu *menu = new QMenu(notExamples ? it.fileName() : it.fileName().remove(QRegularExpression(QStringLiteral("^\\d+-"))).replace(QLatin1Char('-'), QLatin1Char(' ')), parent);
            QMultiMap<QString, QAction *> menuActions = aboutToShowExamplesRecursive(filePath, menu, notExamples);
            menu->addActions(menuActions.values());
            menu->setDisabled(menuActions.values().isEmpty());
            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^\\d+-")).match(it.fileName());
            actions.insert(QString(QStringLiteral("%1-")).arg(match.hasMatch() ? match.captured(1).toInt() : INT_MAX, 10) + it.fileName(), menu->menuAction());
        }
        else
        {
            QAction *action = new QAction(notExamples ? it.fileName() : it.fileName().remove(QRegularExpression(QStringLiteral("^\\d+-"))), parent);
            connect(action, &QAction::triggered, this, [this, filePath, notExamples]
            {
                QFile file(filePath);

                if(file.open(QIODevice::ReadOnly))
                {
                    QByteArray data = file.readAll();

                    if((file.error() == QFile::NoError) && (!data.isEmpty()))
                    {
                        if((m_sensorType == QStringLiteral("HM01B0")) ||
                           (m_sensorType == QStringLiteral("HM0360")) ||
                           (m_sensorType == QStringLiteral("MT9V0X2")) ||
                           (m_sensorType == QStringLiteral("MT9V0X4")))
                        {
                            data = data.replace(QByteArrayLiteral("sensor.set_pixformat(sensor.RGB565)"), QByteArrayLiteral("sensor.set_pixformat(sensor.GRAYSCALE)"));
                            if(m_sensorType == QStringLiteral("HM01B0")) data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"), QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
                        }

                        Core::EditorManager::cutForwardNavigationHistory();
                        Core::EditorManager::addCurrentPositionToNavigationHistory();

                        QString titlePattern = QFileInfo(filePath).baseName().simplified() + QStringLiteral("_$.") + QFileInfo(filePath).completeSuffix();
                        TextEditor::BaseTextEditor *editor = qobject_cast<TextEditor::BaseTextEditor *>(notExamples ? Core::EditorManager::openEditor(Utils::FilePath::fromString(filePath)) : Core::EditorManager::openEditorWithContents(Core::Constants::K_DEFAULT_TEXT_EDITOR_ID, &titlePattern, data));

                        if(editor)
                        {
                            editor->document()->setProperty("diffFilePath", filePath);
                            Core::EditorManager::addCurrentPositionToNavigationHistory();
                            if(!notExamples) editor->editorWidget()->configureGenericHighlighter();
                            Core::EditorManager::activateEditor(editor);
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                notExamples ? Tr::tr("Open File") : Tr::tr("Open Example"),
                                notExamples ? Tr::tr("Cannot open the file \"%L1\"!").arg(filePath) : Tr::tr("Cannot open the example file \"%L1\"!").arg(filePath));
                        }
                    }
                    else if(file.error() != QFile::NoError)
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            notExamples ? Tr::tr("Open File") : Tr::tr("Open Example"),
                            Tr::tr("Error: %L1!").arg(file.errorString()));
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            notExamples ? Tr::tr("Open File") : Tr::tr("Open Example"),
                            notExamples ? Tr::tr("Cannot open the file \"%L1\"!").arg(filePath) : Tr::tr("Cannot open the example file \"%L1\"!").arg(filePath));
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        notExamples ? Tr::tr("Open File") : Tr::tr("Open Example"),
                        Tr::tr("Error: %L1!").arg(file.errorString()));
                }
            });

            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^\\d+-")).match(it.fileName());
            actions.insert(QString(QStringLiteral("%1-")).arg(match.hasMatch() ? match.captured(1).toInt() : INT_MAX, 10) + it.fileName(), action);
        }
    }

    return actions;
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
            && (QString::fromUtf8(info.fileSystemType()).contains(QStringLiteral("fat"), Qt::CaseInsensitive) || QString::fromUtf8(info.fileSystemType()).contains(QStringLiteral("msdos"), Qt::CaseInsensitive) || QString::fromUtf8(info.fileSystemType()).contains(QStringLiteral("fuseblk"), Qt::CaseInsensitive))
            && ((!Utils::HostOsInfo::isMacHost()) || info.rootPath().startsWith(QStringLiteral("/volumes/"), Qt::CaseInsensitive))
            && ((!Utils::HostOsInfo::isLinuxHost()) || info.rootPath().startsWith(QStringLiteral("/media/"), Qt::CaseInsensitive) || info.rootPath().startsWith(QStringLiteral("/mnt/"), Qt::CaseInsensitive) || info.rootPath().startsWith(QStringLiteral("/run/"), Qt::CaseInsensitive)))
            {
                if(((m_major < OPENMV_DISK_ADDED_MAJOR)
                || ((m_major == OPENMV_DISK_ADDED_MAJOR) && (m_minor < OPENMV_DISK_ADDED_MINOR))
                || ((m_major == OPENMV_DISK_ADDED_MAJOR) && (m_minor == OPENMV_DISK_ADDED_MINOR) && (m_patch < OPENMV_DISK_ADDED_PATCH)))
                || QFile::exists(info.rootPath() + QStringLiteral(OPENMV_DISK_ADDED_NAME)))
                {
                    drives.append(info.rootPath());
                }
            }
        }

        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SERIAL_PORT_SETTINGS_GROUP));

        if(drives.isEmpty())
        {
            if(!silent)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Select Drive"),
                    Tr::tr("No valid drives were found to associate with your OpenMV Cam!"));
            }

            m_portPath = QString();
        }
        else if(drives.size() == 1)
        {
            if(m_portPath == drives.first())
            {
                QMessageBox::information(Core::ICore::dialogParent(),
                    Tr::tr("Select Drive"),
                    Tr::tr("\"%L1\" is the only drive available so it must be your OpenMV Cam's drive.").arg(drives.first()));
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
                Tr::tr("Select Drive"), Tr::tr("Please associate a drive with your OpenMV Cam"),
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

        m_pathButton->setText((!m_portPath.isEmpty()) ? Tr::tr("Drive: %L1").arg(m_portPath) : Tr::tr("Drive:"));

        Core::IEditor *editor = Core::EditorManager::currentEditor();
        m_openDriveFolderAction->setEnabled(!m_portPath.isEmpty());
        m_configureSettingsAction->setEnabled(!m_portPath.isEmpty());
        m_saveAction->setEnabled((!m_portPath.isEmpty()) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));

        m_frameBuffer->enableSaveTemplate(!m_portPath.isEmpty());
        m_frameBuffer->enableSaveDescriptor(!m_portPath.isEmpty());
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Select Drive"),
            Tr::tr("Busy... please wait..."));
    }
}

const int connectToSerialPortIndex = 0;
const int connectToUDPPortIndex = 1;
const int connectToTCPPortIndex = 2;

void OpenMVPlugin::openTerminalAboutToShow()
{
    m_openTerminalMenu->menu()->clear();
    connect(m_openTerminalMenu->menu()->addAction(Tr::tr("New Terminal")), &QAction::triggered, this, [this] {
        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QStringList optionList = QStringList()
            << Tr::tr("Connect to serial port")
            << Tr::tr("Connect to UDP port")
            << Tr::tr("Connect to TCP port");

        int optionListIndex = optionList.indexOf(settings->value(QStringLiteral(LAST_OPEN_TERMINAL_SELECT)).toString());

        bool optionNameOk;
        QString optionName = QInputDialog::getItem(Core::ICore::dialogParent(),
            Tr::tr("New Terminal"), Tr::tr("Please select an option"),
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

                    foreach(QSerialPortInfo raw_port, QSerialPortInfo::availablePorts())
                    {
                        MyQSerialPortInfo port(raw_port);

                        stringList.append(port.portName());
                    }

                    if(Utils::HostOsInfo::isMacHost())
                    {
                        stringList = stringList.filter(QStringLiteral("cu"), Qt::CaseInsensitive);
                    }

                    if(!stringList.isEmpty())
                    {
                        int index = stringList.indexOf(settings->value(QStringLiteral(LAST_OPEN_TERMINAL_SERIAL_PORT)).toString());

                        bool portNameValueOk;
                        QString portNameValue = QInputDialog::getItem(Core::ICore::dialogParent(),
                            Tr::tr("New Terminal"), Tr::tr("Please select a serial port"),
                            stringList, (index != -1) ? index : 0, false, &portNameValueOk,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(portNameValueOk)
                        {
                            bool baudRateOk;
                            QString baudRate = QInputDialog::getText(Core::ICore::dialogParent(),
                                Tr::tr("New Terminal"), Tr::tr("Please enter a baud rate"),
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
                                    data.displayName = Tr::tr("Serial Port - %L1 - %L2 BPS").arg(portNameValue).arg(baudRateValue);
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

                                    OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(data.displayName)));
                                    OpenMVTerminalSerialPort *terminalDevice = new OpenMVTerminalSerialPort(terminal);

                                    connect(terminal, &OpenMVTerminal::writeBytes,
                                            terminalDevice, &OpenMVTerminalPort::writeBytes);

                                    connect(terminal, &OpenMVTerminal::execScript,
                                            terminalDevice, &OpenMVTerminalPort::execScript);

                                    connect(terminal, &OpenMVTerminal::interruptScript,
                                            terminalDevice, &OpenMVTerminalPort::interruptScript);

                                    connect(terminal, &OpenMVTerminal::reloadScript,
                                            terminalDevice, &OpenMVTerminalPort::reloadScript);

                                    connect(terminal, &OpenMVTerminal::paste,
                                            terminalDevice, &OpenMVTerminalPort::paste);

                                    connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                            terminal, &OpenMVTerminal::readBytes);

                                    QString errorMessage2 = QString();
                                    QString *errorMessage2Ptr = &errorMessage2;

                                    QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                        this, [errorMessage2Ptr] (const QString &errorMessage) {
                                        *errorMessage2Ptr = errorMessage;
                                    });

                                    // QProgressDialog scoping...
                                    {
                                        QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                            (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                                            Tr::tr("New Terminal"),
                                            Tr::tr("Error: %L1!").arg(errorMessage2));

                                        if(Utils::HostOsInfo::isLinuxHost() && errorMessage2.contains(QStringLiteral("Permission Denied"), Qt::CaseInsensitive))
                                        {
                                            QMessageBox::information(Core::ICore::dialogParent(),
                                                Tr::tr("New Terminal"),
                                                Tr::tr("Try doing:\n\n") + QStringLiteral("sudo adduser %L1 dialout\n\n").arg(Utils::Environment::systemEnvironment().toDictionary().userName()) + Tr::tr("...in a terminal and then restart your computer."));
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
                                        Tr::tr("New Terminal"),
                                        Tr::tr("Invalid string: \"%L1\"!").arg(baudRate));
                                }
                            }
                        }
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("New Terminal"),
                            Tr::tr("No serial ports found!"));
                    }

                    break;
                }
                case connectToUDPPortIndex:
                {
                    QMessageBox box(QMessageBox::Question, Tr::tr("New Terminal"), Tr::tr("Connect to a UDP server as a client or start a UDP Server?"), QMessageBox::Cancel, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    QPushButton *button0 = box.addButton(Tr::tr(" Connect to a Server "), QMessageBox::AcceptRole);
                    QPushButton *button1 = box.addButton(Tr::tr(" Start a Server "), QMessageBox::AcceptRole);
                    box.setDefaultButton(settings->value(QStringLiteral(LAST_OPEN_TERMINAL_UDP_TYPE_SELECT), 0).toInt() ? button1 : button0);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    if(box.clickedButton() == button0)
                    {
                        bool hostNameOk;
                        QString hostName = QInputDialog::getText(Core::ICore::dialogParent(),
                            Tr::tr("New Terminal"), Tr::tr("Please enter a IP address (or domain name) and port (e.g. xxx.xxx.xxx.xxx:xxxx)"),
                            QLineEdit::Normal, settings->value(QStringLiteral(LAST_OPEN_TERMINAL_UDP_PORT), QStringLiteral("xxx.xxx.xxx.xxx:xxxx")).toString(), &hostNameOk,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(hostNameOk && (!hostName.isEmpty()))
                        {
                            QStringList hostNameList = hostName.split(QLatin1Char(':'), Qt::SkipEmptyParts);

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
                                    data.displayName = Tr::tr("UDP Client Connection - %1").arg(hostName);
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

                                    OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(data.displayName)));
                                    OpenMVTerminalUDPPort *terminalDevice = new OpenMVTerminalUDPPort(terminal);

                                    connect(terminal, &OpenMVTerminal::writeBytes,
                                            terminalDevice, &OpenMVTerminalPort::writeBytes);

                                    connect(terminal, &OpenMVTerminal::execScript,
                                            terminalDevice, &OpenMVTerminalPort::execScript);

                                    connect(terminal, &OpenMVTerminal::interruptScript,
                                            terminalDevice, &OpenMVTerminalPort::interruptScript);

                                    connect(terminal, &OpenMVTerminal::reloadScript,
                                            terminalDevice, &OpenMVTerminalPort::reloadScript);

                                    connect(terminal, &OpenMVTerminal::paste,
                                            terminalDevice, &OpenMVTerminalPort::paste);

                                    connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                            terminal, &OpenMVTerminal::readBytes);

                                    QString errorMessage2 = QString();
                                    QString *errorMessage2Ptr = &errorMessage2;

                                    QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                        this, [errorMessage2Ptr] (const QString &errorMessage) {
                                        *errorMessage2Ptr = errorMessage;
                                    });

                                    // QProgressDialog scoping...
                                    {
                                        QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                            (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                                            Tr::tr("New Terminal"),
                                            Tr::tr("Error: %L1!").arg(errorMessage2));

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
                                        Tr::tr("New Terminal"),
                                        Tr::tr("Invalid string: \"%L1\"!").arg(hostName));
                                }
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("New Terminal"),
                                    Tr::tr("Invalid string: \"%L1\"!").arg(hostName));
                            }
                        }
                    }
                    else if(box.clickedButton() == button1)
                    {
                        bool portValueOk;
                        int portValue = QInputDialog::getInt(Core::ICore::dialogParent(),
                            Tr::tr("New Terminal"), Tr::tr("Please enter a port number (enter 0 for any random free port)"),
                            settings->value(QStringLiteral(LAST_OPEN_TERMINAL_UDP_SERVER_PORT), 0).toInt(), 0, 65535, 1, &portValueOk,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(portValueOk)
                        {
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_SELECT), optionName);
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_UDP_TYPE_SELECT), 1);
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_UDP_SERVER_PORT), portValue);

                            openTerminalMenuData_t data;
                            data.displayName = Tr::tr("UDP Server Connection - %1").arg(portValue);
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

                            OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(data.displayName)));
                            OpenMVTerminalUDPPort *terminalDevice = new OpenMVTerminalUDPPort(terminal);

                            connect(terminal, &OpenMVTerminal::writeBytes,
                                    terminalDevice, &OpenMVTerminalPort::writeBytes);

                            connect(terminal, &OpenMVTerminal::execScript,
                                    terminalDevice, &OpenMVTerminalPort::execScript);

                            connect(terminal, &OpenMVTerminal::interruptScript,
                                    terminalDevice, &OpenMVTerminalPort::interruptScript);

                            connect(terminal, &OpenMVTerminal::reloadScript,
                                    terminalDevice, &OpenMVTerminalPort::reloadScript);

                            connect(terminal, &OpenMVTerminal::paste,
                                    terminalDevice, &OpenMVTerminalPort::paste);

                            connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                    terminal, &OpenMVTerminal::readBytes);

                            QString errorMessage2 = QString();
                            QString *errorMessage2Ptr = &errorMessage2;

                            QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                this, [errorMessage2Ptr] (const QString &errorMessage) {
                                *errorMessage2Ptr = errorMessage;
                            });

                            // QProgressDialog scoping...
                            {
                                QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                    (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                                    Tr::tr("New Terminal"),
                                    Tr::tr("Error: %L1!").arg(errorMessage2));

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
                    QMessageBox box(QMessageBox::Question, Tr::tr("New Terminal"), Tr::tr("Connect to a TCP server as a client or start a TCP Server?"), QMessageBox::Cancel, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    QPushButton *button0 = box.addButton(Tr::tr(" Connect to a Server "), QMessageBox::AcceptRole);
                    QPushButton *button1 = box.addButton(Tr::tr(" Start a Server "), QMessageBox::AcceptRole);
                    box.setDefaultButton(settings->value(QStringLiteral(LAST_OPEN_TERMINAL_TCP_TYPE_SELECT), 0).toInt() ? button1 : button0);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    if(box.clickedButton() == button0)
                    {
                        bool hostNameOk;
                        QString hostName = QInputDialog::getText(Core::ICore::dialogParent(),
                            Tr::tr("New Terminal"), Tr::tr("Please enter a IP address (or domain name) and port (e.g. xxx.xxx.xxx.xxx:xxxx)"),
                            QLineEdit::Normal, settings->value(QStringLiteral(LAST_OPEN_TERMINAL_TCP_PORT), QStringLiteral("xxx.xxx.xxx.xxx:xxxx")).toString(), &hostNameOk,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(hostNameOk && (!hostName.isEmpty()))
                        {
                            QStringList hostNameList = hostName.split(QLatin1Char(':'), Qt::SkipEmptyParts);

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
                                    data.displayName = Tr::tr("TCP Client Connection - %1").arg(hostName);
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

                                    OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(data.displayName)));
                                    OpenMVTerminalTCPPort *terminalDevice = new OpenMVTerminalTCPPort(terminal);

                                    connect(terminal, &OpenMVTerminal::writeBytes,
                                            terminalDevice, &OpenMVTerminalPort::writeBytes);

                                    connect(terminal, &OpenMVTerminal::execScript,
                                            terminalDevice, &OpenMVTerminalPort::execScript);

                                    connect(terminal, &OpenMVTerminal::interruptScript,
                                            terminalDevice, &OpenMVTerminalPort::interruptScript);

                                    connect(terminal, &OpenMVTerminal::reloadScript,
                                            terminalDevice, &OpenMVTerminalPort::reloadScript);

                                    connect(terminal, &OpenMVTerminal::paste,
                                            terminalDevice, &OpenMVTerminalPort::paste);

                                    connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                            terminal, &OpenMVTerminal::readBytes);

                                    QString errorMessage2 = QString();
                                    QString *errorMessage2Ptr = &errorMessage2;

                                    QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                        this, [errorMessage2Ptr] (const QString &errorMessage) {
                                        *errorMessage2Ptr = errorMessage;
                                    });

                                    // QProgressDialog scoping...
                                    {
                                        QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                            (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                                            Tr::tr("New Terminal"),
                                            Tr::tr("Error: %L1!").arg(errorMessage2));

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
                                        Tr::tr("New Terminal"),
                                        Tr::tr("Invalid string: \"%L1\"!").arg(hostName));
                                }
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("New Terminal"),
                                    Tr::tr("Invalid string: \"%L1\"!").arg(hostName));
                            }
                        }
                    }
                    else if(box.clickedButton() == button1)
                    {
                        bool portValueOk;
                        int portValue = QInputDialog::getInt(Core::ICore::dialogParent(),
                            Tr::tr("New Terminal"), Tr::tr("Please enter a port number (enter 0 for any random free port)"),
                            settings->value(QStringLiteral(LAST_OPEN_TERMINAL_TCP_SERVER_PORT), 0).toInt(), 0, 65535, 1, &portValueOk,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(portValueOk)
                        {
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_SELECT), optionName);
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_TCP_TYPE_SELECT), 1);
                            settings->setValue(QStringLiteral(LAST_OPEN_TERMINAL_TCP_SERVER_PORT), portValue);

                            openTerminalMenuData_t data;
                            data.displayName = Tr::tr("TCP Server Connection - %1").arg(portValue);
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

                            OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(data.displayName)));
                            OpenMVTerminalTCPPort *terminalDevice = new OpenMVTerminalTCPPort(terminal);

                            connect(terminal, &OpenMVTerminal::writeBytes,
                                    terminalDevice, &OpenMVTerminalPort::writeBytes);

                            connect(terminal, &OpenMVTerminal::execScript,
                                    terminalDevice, &OpenMVTerminalPort::execScript);

                            connect(terminal, &OpenMVTerminal::interruptScript,
                                    terminalDevice, &OpenMVTerminalPort::interruptScript);

                            connect(terminal, &OpenMVTerminal::reloadScript,
                                    terminalDevice, &OpenMVTerminalPort::reloadScript);

                            connect(terminal, &OpenMVTerminal::paste,
                                    terminalDevice, &OpenMVTerminalPort::paste);

                            connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                                    terminal, &OpenMVTerminal::readBytes);

                            QString errorMessage2 = QString();
                            QString *errorMessage2Ptr = &errorMessage2;

                            QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                                this, [errorMessage2Ptr] (const QString &errorMessage) {
                                *errorMessage2Ptr = errorMessage;
                            });

                            // QProgressDialog scoping...
                            {
                                QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                                    (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                                    Tr::tr("New Terminal"),
                                    Tr::tr("Error: %L1!").arg(errorMessage2));

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
            OpenMVTerminal *terminal = new OpenMVTerminal(data.displayName, ExtensionSystem::PluginManager::settings(), Core::Context(Utils::Id::fromString(data.displayName)));
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
                        Tr::tr("Open Terminal"),
                        Tr::tr("Error: Option Index!"));

                    return;
                }
            }

            connect(terminal, &OpenMVTerminal::writeBytes,
                    terminalDevice, &OpenMVTerminalPort::writeBytes);

            connect(terminal, &OpenMVTerminal::execScript,
                    terminalDevice, &OpenMVTerminalPort::execScript);

            connect(terminal, &OpenMVTerminal::interruptScript,
                    terminalDevice, &OpenMVTerminalPort::interruptScript);

            connect(terminal, &OpenMVTerminal::reloadScript,
                    terminalDevice, &OpenMVTerminalPort::reloadScript);

            connect(terminal, &OpenMVTerminal::paste,
                    terminalDevice, &OpenMVTerminalPort::paste);

            connect(terminalDevice, &OpenMVTerminalPort::readBytes,
                    terminal, &OpenMVTerminal::readBytes);

            QString errorMessage2 = QString();
            QString *errorMessage2Ptr = &errorMessage2;

            QMetaObject::Connection conn = connect(terminalDevice, &OpenMVTerminalPort::openResult,
                this, [errorMessage2Ptr] (const QString &errorMessage) {
                *errorMessage2Ptr = errorMessage;
            });

            // QProgressDialog scoping...
            {
                QProgressDialog dialog(Tr::tr("Connecting... (30 second timeout)"), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                    (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
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
                    Tr::tr("Open Terminal"),
                    Tr::tr("Error: %L1!").arg(errorMessage2));

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
        connect(m_openTerminalMenu->menu()->addAction(Tr::tr("Clear Menu")), &QAction::triggered, this, [this] {
            m_openTerminalMenuData.clear();
        });
    }
}

QList<int> OpenMVPlugin::openThresholdEditor(const QVariant parameters)
{
    QMessageBox box(QMessageBox::Question, Tr::tr("Threshold Editor"), Tr::tr("Source image location?"), QMessageBox::Cancel, Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    QPushButton *button0 = box.addButton(Tr::tr(" Frame Buffer "), QMessageBox::AcceptRole);
    QPushButton *button1 = box.addButton(Tr::tr(" Image File "), QMessageBox::AcceptRole);
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
                ? Tr::tr("The selected threshold tuple will be updated on close.")
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
                Tr::tr("Threshold Editor"),
                Tr::tr("No image loaded!"));
        }
    }
    else if(box.clickedButton() == button1)
    {
        QString path =
            QFileDialog::getOpenFileName(Core::ICore::dialogParent(), Tr::tr("Image File"),
                settings->value(QStringLiteral(LAST_THRESHOLD_EDITOR_PATH), drivePath.isEmpty() ? QDir::homePath() : drivePath).toString(),
                Tr::tr("Image Files (*.bmp *.jpg *.jpeg *.png *.ppm)"));

        if(!path.isEmpty())
        {
            QPixmap pixmap = QPixmap(path);

            ThresholdEditor editor(pixmap, settings->value(QStringLiteral(LAST_THRESHOLD_EDITOR_STATE)).toByteArray(), Core::ICore::dialogParent(),
                Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint),
                ((!parameters.toList().isEmpty()) && ((parameters.toList().size() == 2) || (parameters.toList().size() == 6)))
                ? Tr::tr("The selected threshold tuple will be updated on close.")
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
    QMessageBox box(QMessageBox::Question, Tr::tr("Keypoints Editor"), Tr::tr("What would you like to do?"), QMessageBox::Cancel, Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    QPushButton *button0 = box.addButton(Tr::tr(" Edit File "), QMessageBox::AcceptRole);
    QPushButton *button1 = box.addButton(Tr::tr(" Merge Files "), QMessageBox::AcceptRole);
    box.setDefaultButton(button0);
    box.setEscapeButton(QMessageBox::Cancel);
    box.exec();

    QString drivePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath));

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    if(box.clickedButton() == button0)
    {
        QString path =
            QFileDialog::getOpenFileName(Core::ICore::dialogParent(), Tr::tr("Edit Keypoints"),
                settings->value(QStringLiteral(LAST_EDIT_KEYPOINTS_PATH), drivePath.isEmpty() ? QDir::homePath() : drivePath).toString(),
                Tr::tr("Keypoints Files (*.lbp *.orb)"));

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
                                Tr::tr("Save Edited Keypoints"),
                                Tr::tr("Failed to save the edited keypoints for an unknown reason!"));
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
                        Tr::tr("Edit Keypoints"),
                        Tr::tr("Failed to find the keypoints image file!"));
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Edit Keypoints"),
                    Tr::tr("Failed to load the keypoints file for an unknown reason!"));
            }
        }
    }
    else if(box.clickedButton() == button1)
    {
        QStringList paths =
            QFileDialog::getOpenFileNames(Core::ICore::dialogParent(), Tr::tr("Merge Keypoints"),
                settings->value(QStringLiteral(LAST_MERGE_KEYPOINTS_OPEN_PATH), drivePath.isEmpty() ? QDir::homePath() : drivePath).toString(),
                Tr::tr("Keypoints Files (*.lbp *.orb)"));

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

                QString path;

                forever
                {
                    path =
                    QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Save Merged Keypoints"),
                        settings->value(QStringLiteral(LAST_MERGE_KEYPOINTS_SAVE_PATH), drivePath).toString(),
                        Tr::tr("Keypoints Files (*.lbp *.orb)"));

                    if((!path.isEmpty()) && QFileInfo(path).completeSuffix().isEmpty())
                    {
                        QMessageBox::warning(Core::ICore::dialogParent(),
                            Tr::tr("Save Merged Keypoints"),
                            Tr::tr("Please add a file extension!"));

                        continue;
                    }

                    break;
                }

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
                            Tr::tr("Save Merged Keypoints"),
                            Tr::tr("Failed to save the merged keypoints for an unknown reason!"));
                    }
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Merge Keypoints"),
                    Tr::tr("Failed to load the first keypoints file for an unknown reason!"));
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
    dialog->setWindowTitle(Tr::tr("AprilTag Generator"));
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(Tr::tr("What tag images from the %L1 tag family do you want to generate?").arg(QString::fromUtf8(family->name).toUpper())));

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    QWidget *temp = new QWidget();
    QHBoxLayout *tempLayout = new QHBoxLayout(temp);
    tempLayout->setContentsMargins(0, 0, 0, 0);

    QWidget *minTemp = new QWidget();
    QFormLayout *minTempLayout = new QFormLayout(minTemp);
    minTempLayout->setContentsMargins(0, 0, 0, 0);
    QSpinBox *minRange = new QSpinBox();
    minRange->setMinimum(0);
    minRange->setMaximum(family->ncodes - 1);
    minRange->setValue(settings->value(QStringLiteral(LAST_APRILTAG_RANGE_MIN), 0).toInt());
    minRange->setAccelerated(true);
    minTempLayout->addRow(Tr::tr("Min (%1)").arg(0), minRange); // don't use %L1 here
    tempLayout->addWidget(minTemp);

    QWidget *maxTemp = new QWidget();
    QFormLayout *maxTempLayout = new QFormLayout(maxTemp);
    maxTempLayout->setContentsMargins(0, 0, 0, 0);
    QSpinBox *maxRange = new QSpinBox();
    maxRange->setMinimum(0);
    maxRange->setMaximum(family->ncodes - 1);
    maxRange->setValue(settings->value(QStringLiteral(LAST_APRILTAG_RANGE_MAX), family->ncodes - 1).toInt());
    maxRange->setAccelerated(true);
    maxTempLayout->addRow(Tr::tr("Max (%1)").arg(family->ncodes - 1), maxRange); // don't use %L1 here
    tempLayout->addWidget(maxTemp);

    layout->addWidget(temp);

    QCheckBox *checkBox = new QCheckBox(Tr::tr("Inlcude tag family and ID number in the image"));
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
            QFileDialog::getExistingDirectory(Core::ICore::dialogParent(), Tr::tr("AprilTag Generator - Where do you want to save %n tag image(s) to?", "", number),
                settings->value(QStringLiteral(LAST_APRILTAG_PATH), QDir::homePath()).toString());

        if(!path.isEmpty())
        {
            QProgressDialog progress(Tr::tr("Generating images..."), Tr::tr("Cancel"), 0, number - 1, Core::ICore::dialogParent(),
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
                    progress.cancel();

                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("AprilTag Generator"),
                        Tr::tr("Painting - begin failed!"));

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
                    progress.cancel();

                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("AprilTag Generator"),
                        Tr::tr("Painting - end failed!"));

                    break;
                }

                if(!pixmap.save(path + QDir::separator() + QString::fromUtf8(family->name).toLower() + QString(QStringLiteral("_%1.png")).arg(min + i)))
                {
                    progress.cancel();

                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("AprilTag Generator"),
                        Tr::tr("Failed to save the image file for an unknown reason!"));
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
                    Tr::tr("AprilTag Generator"),
                    Tr::tr("Generation complete!"));
            }
        }
    }

    settings->endGroup();
    delete dialog;
    free(family->name);
    free(family->codes);
    free(family);
}

} // namespace Internal
} // namespace OpenMV
