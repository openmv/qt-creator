#include <QtCore>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#include "openmvtr.h"

#define PICOTOOL_SETTINGS_GROUP "OpenMVPICOTOOL"
#define LAST_PICOTOOL_TERMINAL_WINDOW_GEOMETRY "LastPICOTOOLTerminalWindowGeometry"

namespace OpenMV {
namespace Internal {

QList<QString> picotoolGetDevices()
{
    Utils::FilePath command;
    Utils::QtcProcess process;
    process.setTimeoutS(10);
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setProcessChannelMode(QProcess::MergedChannels);

    if(Utils::HostOsInfo::isWindowsHost())
    {
        command = Core::ICore::resourcePath(QStringLiteral("picotool/windows/picotool.exe"));
        process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("info")));
        process.runBlocking(Utils::EventLoopMode::On);
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        command = Core::ICore::resourcePath(QStringLiteral("picotool/osx/picotool"));
        process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("info")));
        process.runBlocking(Utils::EventLoopMode::On);
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("picotool/linux32/picotool"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("info")));
            process.runBlocking(Utils::EventLoopMode::On);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("picotool/linux64/picotool"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("info")));
            process.runBlocking(Utils::EventLoopMode::On);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("picotool/arm/picotool"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("info")));
            process.runBlocking(Utils::EventLoopMode::On);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("aarch64"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("picotool/aarch64/picotool"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("info")));
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
        QMessageBox box(QMessageBox::Warning, Tr::tr("Get Devices"), Tr::tr("Query failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
        box.setDetailedText(command.toString() + QStringLiteral("\n\n") + process.stdOut());
        box.setDefaultButton(QMessageBox::Ok);
        box.setEscapeButton(QMessageBox::Cancel);
        box.exec();

        return QList<QString>();
    }
}

void picotoolReset(QString &command, Utils::QtcProcess &process)
{
    Utils::FilePath binary;
    QStringList args = QStringList() <<
                       QString(QStringLiteral("reboot"));

    if(Utils::HostOsInfo::isWindowsHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("picotool/windows/picotool.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("picotool/osx/picotool"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("picotool/linux32/picotool"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("picotool/linux64/picotool"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("picotool/arm/picotool"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("aarch64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("picotool/aarch64/picotool"));
        }
    }

    command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));

    process.setTimeoutS(300); // 5 minutes...
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setCommand(Utils::CommandLine(binary, args));
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
    dialog->setWindowTitle(Tr::tr("PicoTool"));
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

            plainTextEdit->appendHtml(QStringLiteral("<p style=\"color:%1\">%2</p>").
                                      arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightcoral") : QStringLiteral("coral")).
                                      arg(out));
        }
    });

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(box);

    QObject::connect(dialog, &QDialog::rejected, [&process] { process.terminate(); });

    Utils::FilePath binary;
    QStringList args = QStringList() <<
                       QStringLiteral("load") <<
                       moreArgs.split(QLatin1Char(' ')) <<
                       QDir::toNativeSeparators(QDir::cleanPath(path));

    if(Utils::HostOsInfo::isWindowsHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("picotool/windows/picotool.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("picotool/osx/picotool"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("picotool/linux32/picotool"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("picotool/linux64/picotool"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("picotool/arm/picotool"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("aarch64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("picotool/aarch64/picotool"));
        }
    }

    command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
    plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                              arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                              arg(command));

    dialog->show();
    process.setTimeoutS(300); // 5 minutes...
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setCommand(Utils::CommandLine(binary, args));
    process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

    settings->setValue(QStringLiteral(LAST_PICOTOOL_TERMINAL_WINDOW_GEOMETRY), dialog->saveGeometry());
    settings->endGroup();
    delete dialog;
}

} // namespace Internal
} // namespace OpenMV
