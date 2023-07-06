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

void downloadFirmware(QString &command, Utils::QtcProcess &process, const QString &path, const QString &device, const QString &moreArgs)
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

            QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)) |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            dialog->setAttribute(Qt::WA_ShowWithoutActivating);
            dialog->setWindowTitle(Tr::tr("DfuSe"));
            dialog->setSizeGripEnabled(true);

            if(settings->contains(QStringLiteral(LAST_DFUSE_TERMINAL_WINDOW_GEOMETRY)))
            {
                dialog->restoreGeometry(settings->value(QStringLiteral(LAST_DFUSE_TERMINAL_WINDOW_GEOMETRY)).toByteArray());
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

                    if(out.startsWith(QStringLiteral("Target")))
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

                    if(out.startsWith(QStringLiteral("Target")))
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

            Utils::FilePath binary = Core::ICore::resourcePath(QStringLiteral("dfuse/DfuSeCommand.exe"));
            QStringList args = QStringList() <<
                               QStringLiteral("-c") <<
                               QStringLiteral("-d") <<
                               QStringLiteral("--v") <<
                               QStringLiteral("--o") <<
                               QStringLiteral("--fn") <<
                               QDir::toNativeSeparators(QDir::cleanPath(path));

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

            settings->setValue(QStringLiteral(LAST_DFUSE_TERMINAL_WINDOW_GEOMETRY), dialog->saveGeometry());
            settings->endGroup();
            delete dialog;
        }
        else
        {
            QSettings *settings = ExtensionSystem::PluginManager::settings();
            settings->beginGroup(QStringLiteral(PYDFU_SETTINGS_GROUP));

            QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)) |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            dialog->setAttribute(Qt::WA_ShowWithoutActivating);
            dialog->setWindowTitle(Tr::tr("PyDfu"));
            dialog->setSizeGripEnabled(true);

            if(settings->contains(QStringLiteral(LAST_PYDFU_TERMINAL_WINDOW_GEOMETRY)))
            {
                dialog->restoreGeometry(settings->value(QStringLiteral(LAST_PYDFU_TERMINAL_WINDOW_GEOMETRY)).toByteArray());
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
                    plainTextEdit->appendPlainText(list.takeFirst());
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
                    plainTextEdit->appendHtml(QStringLiteral("<p style=\"color:%1\">%2</p>").
                                              arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightcoral") : QStringLiteral("coral")).
                                              arg(list.takeFirst()));
                }
            });

            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
            QObject::connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
            QObject::connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
            layout->addWidget(box);

            QObject::connect(dialog, &QDialog::rejected, [&process] { process.terminate(); });

            Utils::FilePath binary = Utils::FilePath::fromString(QStringLiteral("python3"));
            QStringList args = QStringList() <<
                               Core::ICore::resourcePath(QStringLiteral("pydfu/pydfu.py")).toString() <<
                               QStringLiteral("-u") <<
                               QDir::toNativeSeparators(QDir::cleanPath(path));

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

            settings->setValue(QStringLiteral(LAST_PYDFU_TERMINAL_WINDOW_GEOMETRY), dialog->saveGeometry());
            settings->endGroup();
            delete dialog;
        }

        return;
    }

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(DFU_UTIL_SETTINGS_GROUP));

    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
        Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)) |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setAttribute(Qt::WA_ShowWithoutActivating);
    dialog->setWindowTitle(Tr::tr("DFU Util"));
    dialog->setSizeGripEnabled(true);

    if(settings->contains(QStringLiteral(LAST_DFU_UTIL_TERMINAL_WINDOW_GEOMETRY)))
    {
        dialog->restoreGeometry(settings->value(QStringLiteral(LAST_DFU_UTIL_TERMINAL_WINDOW_GEOMETRY)).toByteArray());
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

            if(out.startsWith(QStringLiteral("Erase")) || out.startsWith(QStringLiteral("Download")))
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

            if(out.startsWith(QStringLiteral("Erase")) || out.startsWith(QStringLiteral("Download")))
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
    plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                              arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                              arg(command));

    dialog->show();
    process.setTimeoutS(300); // 5 minutes...
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setCommand(Utils::CommandLine(binary, args));
    process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

    settings->setValue(QStringLiteral(LAST_DFU_UTIL_TERMINAL_WINDOW_GEOMETRY), dialog->saveGeometry());
    settings->endGroup();
    delete dialog;
}

} // namespace Internal
} // namespace OpenMV

