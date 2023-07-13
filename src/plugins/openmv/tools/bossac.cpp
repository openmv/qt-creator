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

#define BOSSAC_SETTINGS_GROUP "OpenMVBOSSAC"
#define LAST_BOSSAC_TERMINAL_WINDOW_GEOMETRY "LastBOSSACTerminalWindowGeometry"

namespace OpenMV {
namespace Internal {

void bossacRunBootloader(Utils::QtcProcess &process, const QString &device)
{
    Utils::FilePath binary;
    QStringList args = QStringList() <<
                       QString(QStringLiteral("--port=%1")).arg(device) <<
                       QStringLiteral("-a");

    if(Utils::HostOsInfo::isWindowsHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("bossac/windows/bossac.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("bossac/osx/bossac"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("bossac/linux32/bossac"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("bossac/linux64/bossac"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("bossac/arm/bossac"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("aarch64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("bossac/aarch64/bossac"));
        }
    }

    process.setTimeoutS(300); // 5 minutes...
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setCommand(Utils::CommandLine(binary, args));
    process.runBlocking(Utils::EventLoopMode::On);
}

void bossacDownloadFirmware(const QString &details, QString &command, Utils::QtcProcess &process, const QString &path, const QString &device, const QString &moreArgs)
{
    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(BOSSAC_SETTINGS_GROUP));
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("BOSSAC"), details, process, settings, QStringLiteral(LAST_BOSSAC_TERMINAL_WINDOW_GEOMETRY),
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
            QString out = list.takeFirst().remove(QStringLiteral("write(addr=0x34,size=0x1000)"));

            if(out.isEmpty() || out.startsWith(QStringLiteral("writeBuffer")))
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("[")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\[=*\\s*\\]\\s+(\\d+)%")).match(out);

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
            QString out = list.takeFirst().remove(QStringLiteral("write(addr=0x34,size=0x1000)"));

            if(out.isEmpty() || out.startsWith(QStringLiteral("writeBuffer")))
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("[")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\[=*\\s*\\]\\s+(\\d+)%")).match(out);

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

    Utils::FilePath binary;
    QStringList args = QStringList() <<
                       QStringLiteral("-e") <<
                       QStringLiteral("-w") <<
                       moreArgs.split(QLatin1Char(' ')) <<
                       QString(QStringLiteral("--port=%1")).arg(device) <<
                       QStringLiteral("-i") <<
                       QStringLiteral("-d") <<
                       QStringLiteral("-U") <<
                       QStringLiteral("-R") <<
                       QDir::toNativeSeparators(QDir::cleanPath(path));

    if(Utils::HostOsInfo::isWindowsHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("bossac/windows/bossac.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("bossac/osx/bossac"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("bossac/linux32/bossac"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("bossac/linux64/bossac"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("bossac/arm/bossac"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("aarch64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("bossac/aarch64/bossac"));
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
