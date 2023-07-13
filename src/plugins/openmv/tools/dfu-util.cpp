#include <QtCore>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#include "loaderdialog.h"

#include "openmvtr.h"

#define DFU_UTIL_SETTINGS_GROUP "OpenMVDFUUtil"
#define LAST_DFU_UTIL_TERMINAL_WINDOW_GEOMETRY "LastDFUUtilTerminalWindowGeometry"
#define DFUSE_SETTINGS_GROUP "OpenMVDfuSe"
#define LAST_DFUSE_TERMINAL_WINDOW_GEOMETRY "LastDfuSeTerminalWindowGeometry"
#define PYDFU_SETTINGS_GROUP "OpenMVPyDfu"
#define LAST_PYDFU_TERMINAL_WINDOW_GEOMETRY "LastPyDfuTerminalWindowGeometry"

namespace OpenMV {
namespace Internal {

QList<QString> getDevices()
{
    Utils::FilePath command;
    Utils::QtcProcess process;
    process.setTimeoutS(10);
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setProcessChannelMode(QProcess::MergedChannels);

    if(Utils::HostOsInfo::isWindowsHost())
    {
        command = Core::ICore::resourcePath(QStringLiteral("dfu-util/windows/dfu-util.exe"));
        process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
        process.runBlocking(Utils::EventLoopMode::On);
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        command = Core::ICore::resourcePath(QStringLiteral("dfu-util/osx/dfu-util"));
        process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
        process.runBlocking(Utils::EventLoopMode::On);
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("dfu-util/linux32/dfu-util"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
            process.runBlocking(Utils::EventLoopMode::On);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("dfu-util/linux64/dfu-util"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
            process.runBlocking(Utils::EventLoopMode::On);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("dfu-util/arm/dfu-util"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
            process.runBlocking(Utils::EventLoopMode::On);
        }
    }

    if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
    {
        QStringList list, cannot, in = process.stdOut().split(QRegularExpression(QStringLiteral("\n|\r\n|\r")), Qt::SkipEmptyParts);

        foreach(const QString &string, in)
        {
            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("Found DFU: \\[([A-Fa-f0-9:]+)\\].+?alt=0.+?serial=\"(.+?)\"")).match(string);

            if(match.hasMatch())
            {
                list.append(match.captured(1) + QStringLiteral(",") + match.captured(2));
            }

            QRegularExpressionMatch match2 = QRegularExpression(QStringLiteral("Cannot open DFU device ([A-Fa-f0-9:]+)")).match(string);

            if(match2.hasMatch() && (match2.captured(1).toLower() == QStringLiteral("0483:df11")))
            {
                cannot.append(match2.captured(1));
            }
        }

        foreach(const QString &string, cannot)
        {
            list.append(string + QStringLiteral(",NULL"));
        }

        return list;
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

void downloadFirmware(const QString &details,
                      QString &command, Utils::QtcProcess &process,
                      const QString &path, const QString &device, const QString &moreArgs)
{
    QStringList list;

    foreach(const QString &d, getDevices())
    {
        QStringList vidpidserial = d.split(QStringLiteral(","));

        if((vidpidserial.at(0).toLower() == device.toLower()) && (vidpidserial.at(1) == QStringLiteral("NULL")))
        {
            list.append(d);
        }
    }

    if(!list.isEmpty())
    {
        // DfuSe/PyDfu do not offer a way to actually target the correct DFU device. So, if there's anything in the list
        // we are just going to run blindly and hope the user only hooked up one device.

        if(Utils::HostOsInfo::isWindowsHost())
        {
            QSettings *settings = ExtensionSystem::PluginManager::settings();
            settings->beginGroup(QStringLiteral(DFUSE_SETTINGS_GROUP));
            LoaderDialog *dialog = new LoaderDialog(Tr::tr("DfuSe"), details, process, settings, QStringLiteral(LAST_DFUSE_TERMINAL_WINDOW_GEOMETRY),
                                                    Core::ICore::dialogParent());

            QString stdOutBuffer = QString();
            QString *stdOutBufferPtr = &stdOutBuffer;
            bool stdOutFirstTime = true;
            bool *stdOutFirstTimePtr = &stdOutFirstTime;

            QObject::connect(&process, &Utils::QtcProcess::textOnStandardOutput, dialog, [dialog, stdOutBufferPtr, stdOutFirstTimePtr] (const QString &text) {
                stdOutBufferPtr->append(text);
                QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

                if(list.size())
                {
                    *stdOutBufferPtr = list.takeLast();
                }

                while(list.size())
                {
                    QString out = list.takeFirst();

                    if(out.startsWith(QStringLiteral("Target")))
                    {
                        QRegularExpressionMatch m = QRegularExpression(QStringLiteral("Target \\d+: Upgrading - (\\w+) Phase \\((\\d+)\\)")).match(out);

                        if(m.hasMatch())
                        {
                            dialog->setProgressBarLabel(m.captured(1) == QStringLiteral("Erase") ? Tr::tr("Erasing...") : Tr::tr("Downloading..."));
                            int p = m.captured(2).toInt();
                            dialog->setProgressBarRange(0, 100);
                            dialog->setProgressBarValue(p);
                        }

                        if(!*stdOutFirstTimePtr)
                        {
                            QTextCursor cursor = dialog->textCursor();
                            cursor.movePosition(QTextCursor::End);
                            cursor.select(QTextCursor::BlockUnderCursor);
                            cursor.removeSelectedText();
                            dialog->setTextCursor(cursor);
                        }

                        *stdOutFirstTimePtr = false;
                    }

                    dialog->appendPlainText(out);
                }
            });

            QString stdErrBuffer = QString();
            QString *stdErrBufferPtr = &stdErrBuffer;
            bool stdErrFirstTime = true;
            bool *stdErrFirstTimePtr = &stdErrFirstTime;

            QObject::connect(&process, &Utils::QtcProcess::textOnStandardError, dialog, [dialog, stdErrBufferPtr, stdErrFirstTimePtr] (const QString &text) {
                stdErrBufferPtr->append(text);
                QStringList list = stdErrBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

                if(list.size())
                {
                    *stdErrBufferPtr = list.takeLast();
                }

                while(list.size())
                {
                    QString out = list.takeFirst();

                    if(out.startsWith(QStringLiteral("Target")))
                    {
                        QRegularExpressionMatch m = QRegularExpression(QStringLiteral("Target \\d+: Upgrading - (\\w+) Phase \\((\\d+)\\)")).match(out);

                        if(m.hasMatch())
                        {
                            dialog->setProgressBarLabel(m.captured(1) == QStringLiteral("Erase") ? Tr::tr("Erasing...") : Tr::tr("Downloading..."));
                            int p = m.captured(2).toInt();
                            dialog->setProgressBarRange(0, 100);
                            dialog->setProgressBarValue(p);
                        }

                        if(!*stdErrFirstTimePtr)
                        {
                            QTextCursor cursor = dialog->textCursor();
                            cursor.movePosition(QTextCursor::End);
                            cursor.select(QTextCursor::BlockUnderCursor);
                            cursor.removeSelectedText();
                            dialog->setTextCursor(cursor);
                        }

                        *stdErrFirstTimePtr = false;
                    }

                    dialog->appendColoredText(out);
                }
            });

            Utils::FilePath binary = Core::ICore::resourcePath(QStringLiteral("dfuse/DfuSeCommand.exe"));
            QStringList args = QStringList() <<
                               QStringLiteral("-c") <<
                               QStringLiteral("-d") <<
                               QStringLiteral("--v") <<
                               QStringLiteral("--o") <<
                               QStringLiteral("--fn") <<
                               QDir::toNativeSeparators(QDir::cleanPath(path));

            command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            dialog->show();
            process.setTimeoutS(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

            delete dialog;
            settings->endGroup();
        }
        else
        {
            QSettings *settings = ExtensionSystem::PluginManager::settings();
            settings->beginGroup(QStringLiteral(PYDFU_SETTINGS_GROUP));
            LoaderDialog *dialog = new LoaderDialog(Tr::tr("PyDfu"), details, process, settings, QStringLiteral(LAST_PYDFU_TERMINAL_WINDOW_GEOMETRY),
                                                    Core::ICore::dialogParent());

            QString stdOutBuffer = QString();
            QString *stdOutBufferPtr = &stdOutBuffer;
            bool stdOutFirstTime = true;
            bool *stdOutFirstTimePtr = &stdOutFirstTime;

            QObject::connect(&process, &Utils::QtcProcess::textOnStandardOutput, dialog, [dialog, stdOutBufferPtr, stdOutFirstTimePtr] (const QString &text) {
                stdOutBufferPtr->append(text);
                QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

                if(list.size())
                {
                    *stdOutBufferPtr = list.takeLast();
                }

                while(list.size())
                {
                    QString out = list.takeFirst();

                    if(out.startsWith(QStringLiteral("0x")))
                    {
                        QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\d+)%")).match(out);

                        if(m.hasMatch())
                        {
                            dialog->setProgressBarLabel(Tr::tr("Downloading..."));
                            int p = m.captured(1).toInt();
                            dialog->setProgressBarRange(0, 100);
                            dialog->setProgressBarValue(p);
                        }

                        if(!*stdOutFirstTimePtr)
                        {
                            QTextCursor cursor = dialog->textCursor();
                            cursor.movePosition(QTextCursor::End);
                            cursor.select(QTextCursor::BlockUnderCursor);
                            cursor.removeSelectedText();
                            dialog->setTextCursor(cursor);
                        }

                        *stdOutFirstTimePtr = false;
                    }

                    dialog->appendPlainText(out);
                }
            });

            QString stdErrBuffer = QString();
            QString *stdErrBufferPtr = &stdErrBuffer;
            bool stdErrFirstTime = true;
            bool *stdErrFirstTimePtr = &stdErrFirstTime;

            QObject::connect(&process, &Utils::QtcProcess::textOnStandardError, dialog, [dialog, stdErrBufferPtr, stdErrFirstTimePtr] (const QString &text) {
                stdErrBufferPtr->append(text);
                QStringList list = stdErrBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

                if(list.size())
                {
                    *stdErrBufferPtr = list.takeLast();
                }

                while(list.size())
                {
                    QString out = list.takeFirst();

                    if(out.startsWith(QStringLiteral("0x")))
                    {
                        QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\d+)%")).match(out);

                        if(m.hasMatch())
                        {
                            dialog->setProgressBarLabel(Tr::tr("Downloading..."));
                            int p = m.captured(1).toInt();
                            dialog->setProgressBarRange(0, 100);
                            dialog->setProgressBarValue(p);
                        }

                        if(!*stdErrFirstTimePtr)
                        {
                            QTextCursor cursor = dialog->textCursor();
                            cursor.movePosition(QTextCursor::End);
                            cursor.select(QTextCursor::BlockUnderCursor);
                            cursor.removeSelectedText();
                            dialog->setTextCursor(cursor);
                        }

                        *stdErrFirstTimePtr = false;
                    }

                    dialog->appendColoredText(out);
                }
            });

            Utils::FilePath binary = Utils::FilePath::fromString(QStringLiteral("python3"));
            QStringList args = QStringList() <<
                               Core::ICore::resourcePath(QStringLiteral("pydfu/pydfu.py")).toString() <<
                               QStringLiteral("-u") <<
                               QDir::toNativeSeparators(QDir::cleanPath(path));

            command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            dialog->show();
            process.setTimeoutS(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

            delete dialog;
            settings->endGroup();
        }

        return;
    }

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(DFU_UTIL_SETTINGS_GROUP));
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("DFU Util"), details, process, settings, QStringLiteral(LAST_DFU_UTIL_TERMINAL_WINDOW_GEOMETRY),
                                            Core::ICore::dialogParent());

    QString stdOutBuffer = QString();
    QString *stdOutBufferPtr = &stdOutBuffer;
    bool stdOutFirstTime = true;
    bool *stdOutFirstTimePtr = &stdOutFirstTime;

    QObject::connect(&process, &Utils::QtcProcess::textOnStandardOutput, dialog, [dialog, stdOutBufferPtr, stdOutFirstTimePtr] (const QString &text) {
        stdOutBufferPtr->append(text);
        QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdOutBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.startsWith(QStringLiteral("Erase")) || out.startsWith(QStringLiteral("Download")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\w+)\\s+\\[=*\\s*\\]\\s+(\\d+)%\\s+(\\d+)\\s+bytes")).match(out);

                if(m.hasMatch())
                {
                    dialog->setProgressBarLabel(m.captured(1) == QStringLiteral("Erase") ? Tr::tr("Erasing...") : Tr::tr("Downloading..."));
                    int p = m.captured(2).toInt();
                    dialog->setProgressBarRange(0, 100);
                    dialog->setProgressBarValue(p);
                }

                if(!*stdOutFirstTimePtr)
                {
                    QTextCursor cursor = dialog->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    cursor.removeSelectedText();
                    dialog->setTextCursor(cursor);
                }

                *stdOutFirstTimePtr = false;
            }

            dialog->appendPlainText(out);
        }
    });

    QString stdErrBuffer = QString();
    QString *stdErrBufferPtr = &stdErrBuffer;
    bool stdErrFirstTime = true;
    bool *stdErrFirstTimePtr = &stdErrFirstTime;

    QObject::connect(&process, &Utils::QtcProcess::textOnStandardError, dialog, [dialog, stdErrBufferPtr, stdErrFirstTimePtr] (const QString &text) {
        stdErrBufferPtr->append(text);
        QStringList list = stdErrBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdErrBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.startsWith(QStringLiteral("Erase")) || out.startsWith(QStringLiteral("Download")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\w+)\\s+\\[=*\\s*\\]\\s+(\\d+)%\\s+(\\d+)\\s+bytes")).match(out);

                if(m.hasMatch())
                {
                    dialog->setProgressBarLabel(m.captured(1) == QStringLiteral("Erase") ? Tr::tr("Erasing...") : Tr::tr("Downloading..."));
                    int p = m.captured(2).toInt();
                    dialog->setProgressBarRange(0, 100);
                    dialog->setProgressBarValue(p);
                }

                if(!*stdErrFirstTimePtr)
                {
                    QTextCursor cursor = dialog->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    cursor.removeSelectedText();
                    dialog->setTextCursor(cursor);
                }

                *stdErrFirstTimePtr = false;
            }

            dialog->appendColoredText(out);
        }
    });

    Utils::FilePath binary;
    QStringList args = QStringList() <<
                       QStringLiteral("-w") <<
                       QStringLiteral("-d") <<
                       QString(QStringLiteral(",%1")).arg(device) <<
                       moreArgs.split(QLatin1Char(' ')) <<
                       QStringLiteral("-D") <<
                       QDir::toNativeSeparators(QDir::cleanPath(path));

    if(Utils::HostOsInfo::isWindowsHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/windows/dfu-util.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/osx/dfu-util"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/linux32/dfu-util"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/linux64/dfu-util"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/arm/dfu-util"));
        }
    }

    command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
    dialog->appendColoredText(command);

    dialog->show();
    process.setTimeoutS(300); // 5 minutes...
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setCommand(Utils::CommandLine(binary, args));
    process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

    delete dialog;
    settings->endGroup();
}

} // namespace Internal
} // namespace OpenMV
