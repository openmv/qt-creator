#include "picotool.h"

QList<QString> picotoolGetDevices()
{
    QString command;
    Utils::QtcProcess process;
    process.setTimeoutS(10);
    process.setProcessChannelMode(QProcess::MergedChannels);

    if(Utils::HostOsInfo::isWindowsHost())
    {
        command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/windows/picotool.exe")));
        process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(command), QStringList() << QStringLiteral("info")));
        process.runBlocking(Utils::EventLoopMode::On);
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/osx/picotool")));
        process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(command), QStringList() << QStringLiteral("info")));
        process.runBlocking(Utils::EventLoopMode::On);
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/linux32/picotool")));
            process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(command), QStringList() << QStringLiteral("info")));
            process.runBlocking(Utils::EventLoopMode::On);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/linux64/picotool")));
            process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(command), QStringList() << QStringLiteral("info")));
            process.runBlocking(Utils::EventLoopMode::On);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/arm/picotool")));
            process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(command), QStringList() << QStringLiteral("info")));
            process.runBlocking(Utils::EventLoopMode::On);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("aarch64"))
        {
            command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/aarch64/picotool")));
            process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(command), QStringList() << QStringLiteral("info")));
            process.runBlocking(Utils::EventLoopMode::On);
        }
    }

    if((process.result() == Utils::ProcessResult::FinishedWithSuccess) || (process.result() == Utils::ProcessResult::FinishedWithError))
    {
        QStringList in = process.stdOut().split(QRegularExpression(QStringLiteral("\n|\r\n|\r")), Qt::SkipEmptyParts);

        if ((in.size() == 1) && (in.first() == QStringLiteral("No accessible RP2040 devices in BOOTSEL mode were found.")))
        {
            return QList<QString>();
        }
        else
        {
            return QList<QString>() << QStringLiteral("28ea:0003");
        }
    }
    else
    {
        QMessageBox box(QMessageBox::Warning, QObject::tr("Get Devices"), QObject::tr("Query failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
        box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut());
        box.setDefaultButton(QMessageBox::Ok);
        box.setEscapeButton(QMessageBox::Cancel);
        box.exec();

        return QList<QString>();
    }
}

void picotoolReset(QString &command, Utils::QtcProcess &process)
{
    QString binary;
    QStringList args = QStringList() <<
                       QString(QStringLiteral("reboot"));

    if(Utils::HostOsInfo::isWindowsHost())
    {
        binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/windows/picotool.exe")));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/osx/picotool")));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/linux32/picotool")));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/linux64/picotool")));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/arm/picotool")));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("aarch64"))
        {
            binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/aarch64/picotool")));
        }
    }

    command = QString(QStringLiteral("%1 %2")).arg(binary).arg(args.join(QLatin1Char(' ')));

    process.setTimeoutS(300); // 5 minutes...
    process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(binary), args));
    process.runBlocking(Utils::EventLoopMode::On);
}

void picotoolDownloadFirmware(QString &command, Utils::QtcProcess &process, const QString &path, const QString &moreArgs)
{
    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(PICOTOOL_SETTINGS_GROUP));

    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
        Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)) |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setAttribute(Qt::WA_ShowWithoutActivating);
    dialog->setWindowTitle(QObject::tr("PicoTool"));
    dialog->setSizeGripEnabled(true);

    if(settings->contains(QStringLiteral(LAST_PICOTOOL_TERMINAL_WINDOW_GEOMETRY)))
    {
        dialog->restoreGeometry(settings->value(QStringLiteral(LAST_PICOTOOL_TERMINAL_WINDOW_GEOMETRY)).toByteArray());
    }
    else
    {
        dialog->resize(640, 480);
    }

    QVBoxLayout *layout = new QVBoxLayout(dialog);

    QPlainTextEdit *plainTextEdit = new QPlainTextEdit();
    plainTextEdit->setReadOnly(true);
    QFont font = TextEditor::TextEditorSettings::fontSettings().defaultFixedFontFamily();
    plainTextEdit->setFont(font);

    layout->addWidget(plainTextEdit);

    QString stdOutBuffer = QString();
    QString *stdOutBufferPtr = &stdOutBuffer;
    bool stdOutFirstTime = true;
    bool *stdOutFirstTimePtr = &stdOutFirstTime;

    QObject::connect(&process, &Utils::QtcProcess::textOnStandardOutput, plainTextEdit, [plainTextEdit, stdOutBufferPtr, stdOutFirstTimePtr] (const QString &text) {
        stdOutBufferPtr->append(text);
        QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdOutBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.isEmpty())
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("Loading into Flash:")))
            {
                if(!*stdOutFirstTimePtr)
                {
                    QTextCursor cursor = plainTextEdit->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    cursor.removeSelectedText();
                    plainTextEdit->setTextCursor(cursor);
                }

                *stdOutFirstTimePtr = false;
            }

            plainTextEdit->appendPlainText(out);
        }
    });

    QString stdErrBuffer = QString();
    QString *stdErrBufferPtr = &stdErrBuffer;
    bool stdErrFirstTime = true;
    bool *stdErrFirstTimePtr = &stdErrFirstTime;

    QObject::connect(&process, &Utils::QtcProcess::textOnStandardError, plainTextEdit, [plainTextEdit, stdErrBufferPtr, stdErrFirstTimePtr] (const QString &text) {
        stdErrBufferPtr->append(text);
        QStringList list = stdErrBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdErrBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.isEmpty())
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("Loading into Flash:")))
            {
                if(!*stdErrFirstTimePtr)
                {
                    QTextCursor cursor = plainTextEdit->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    cursor.removeSelectedText();
                    plainTextEdit->setTextCursor(cursor);
                }

                *stdErrFirstTimePtr = false;
            }

            plainTextEdit->appendHtml(QStringLiteral("<p style=\"color:red\">%1</p>").arg(out));
        }
    });

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(box);

    QObject::connect(dialog, &QDialog::rejected, [&process] { process.terminate(); });

    QString binary;
    QStringList args = QStringList() <<
                       QStringLiteral("load") <<
                       moreArgs.split(QLatin1Char(' ')) <<
                       QDir::toNativeSeparators(QDir::cleanPath(path));

    if(Utils::HostOsInfo::isWindowsHost())
    {
        binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/windows/picotool.exe")));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/osx/picotool")));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/linux32/picotool")));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/linux64/picotool")));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/arm/picotool")));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("aarch64"))
        {
            binary = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath().toString() + QStringLiteral("/picotool/aarch64/picotool")));
        }
    }

    command = QString(QStringLiteral("%1 %2")).arg(binary).arg(args.join(QLatin1Char(' ')));
    plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:blue\">%1</p><br/><br/>")).arg(command));

    dialog->show();
    process.setTimeoutS(300); // 5 minutes...
    process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(binary), args));
    process.runBlocking(Utils::EventLoopMode::On);

    settings->setValue(QStringLiteral(LAST_PICOTOOL_TERMINAL_WINDOW_GEOMETRY), dialog->saveGeometry());
    settings->endGroup();
    delete dialog;
}
