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

void bossacDownloadFirmware(QString &command, Utils::QtcProcess &process, const QString &path, const QString &device, const QString &moreArgs)
{
    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(BOSSAC_SETTINGS_GROUP));

    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
        Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)) |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setAttribute(Qt::WA_ShowWithoutActivating);
    dialog->setWindowTitle(Tr::tr("BOSSAC"));
    dialog->setSizeGripEnabled(true);

    if(settings->contains(QStringLiteral(LAST_BOSSAC_TERMINAL_WINDOW_GEOMETRY)))
    {
        dialog->restoreGeometry(settings->value(QStringLiteral(LAST_BOSSAC_TERMINAL_WINDOW_GEOMETRY)).toByteArray());
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
            QString out = list.takeFirst().remove(QStringLiteral("write(addr=0x34,size=0x1000)"));

            if(out.isEmpty() || out.startsWith(QStringLiteral("writeBuffer")))
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("[")))
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
            QString out = list.takeFirst().remove(QStringLiteral("write(addr=0x34,size=0x1000)"));

            if(out.isEmpty() || out.startsWith(QStringLiteral("writeBuffer")))
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("[")))
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
    plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                              arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                              arg(command));

    dialog->show();
    process.setTimeoutS(300); // 5 minutes...
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setCommand(Utils::CommandLine(binary, args));
    process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

    settings->setValue(QStringLiteral(LAST_BOSSAC_TERMINAL_WINDOW_GEOMETRY), dialog->saveGeometry());
    settings->endGroup();
    delete dialog;
}

} // namespace Internal
} // namespace OpenMV
