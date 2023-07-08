#include <QtCore>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "openmvtr.h"

#define IMX_SETTINGS_GROUP "OpenMVIMX"
#define LAST_IMX_TERMINAL_WINDOW_GEOMETRY "LastIMXTerminalWindowGeometry"

namespace OpenMV {
namespace Internal {

QList<QPair<int, int> > imxVidPidList(bool spd_host, bool bl_host)
{
    QList<QPair<int, int> > pidvidlist;

    QFile imxSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/imx.txt")).toString());

    if(imxSettings.open(QIODevice::ReadOnly))
    {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(imxSettings.readAll(), &error);

        if(error.error == QJsonParseError::NoError)
        {
            foreach(const QJsonValue &val, doc.array())
            {
                QJsonObject obj = val.toObject();

                if(spd_host)
                {
                    QStringList pidvid = obj.value(QStringLiteral("sdphost_pidvid")).toString().split(QChar(','));

                    if(pidvid.size() == 2)
                    {
                        bool okay_pid; int pid = pidvid.at(0).toInt(&okay_pid, 16);
                        bool okay_vid; int vid = pidvid.at(1).toInt(&okay_vid, 16);

                        if(okay_pid && okay_vid)
                        {
                            QPair<int, int> entry(pid, vid);

                            if(!pidvidlist.contains(entry))
                            {
                                pidvidlist.append(entry);
                            }
                        }
                    }
                }

                if(bl_host)
                {
                    QStringList pidvid = obj.value(QStringLiteral("blhost_pidvid")).toString().split(QChar(','));

                    if(pidvid.size() == 2)
                    {
                        bool okay_pid; int pid = pidvid.at(0).toInt(&okay_pid, 16);
                        bool okay_vid; int vid = pidvid.at(1).toInt(&okay_vid, 16);

                        if(okay_pid && okay_vid)
                        {
                            QPair<int, int> entry(pid, vid);

                            if(!pidvidlist.contains(entry))
                            {
                                pidvidlist.append(entry);
                            }
                        }
                    }
                }
            }
        }
    }

    return pidvidlist;
}

QStringList imxGetAllDevices(bool spd_host, bool bl_host)
{
    QList<QPair<int, int> > pidvidlist = imxVidPidList(spd_host, bl_host);

    QStringList devices;

    if(!pidvidlist.isEmpty())
    {
#ifdef Q_OS_WIN
        UINT nDevices = 0;

        if((GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)) == 0) && (nDevices != 0))
        {
            PRAWINPUTDEVICELIST pRawInputDeviceList = new RAWINPUTDEVICELIST[sizeof(RAWINPUTDEVICELIST) * nDevices];

            if(pRawInputDeviceList && (GetRawInputDeviceList(pRawInputDeviceList, &nDevices, sizeof(RAWINPUTDEVICELIST)) == nDevices))
            {
                for(UINT i = 0; i < nDevices; i++)
                {
                    RID_DEVICE_INFO rdiDeviceInfo;
                    UINT nBufferSize = sizeof(RID_DEVICE_INFO);

                    if(GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice, RIDI_DEVICEINFO, &rdiDeviceInfo, &nBufferSize) == sizeof(RID_DEVICE_INFO))
                    {
                        if(rdiDeviceInfo.dwType == RIM_TYPEHID)
                        {
                            QPair<int, int> entry(rdiDeviceInfo.hid.dwVendorId, rdiDeviceInfo.hid.dwProductId);

                            if(pidvidlist.contains(entry))
                            {
                                devices.append(QString(QStringLiteral("%1:%2,NULL")).
                                               arg(rdiDeviceInfo.hid.dwVendorId, 4, 16, QChar('0')).
                                               arg(rdiDeviceInfo.hid.dwProductId, 4, 16, QChar('0')));
                            }
                        }
                    }
                }

                delete[] pRawInputDeviceList;
            }
        }
#else
        Utils::QtcProcess process;
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(QStringLiteral("lsusb")), QStringList()));
        process.runBlocking(Utils::EventLoopMode::On);

        if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
        {
            QRegularExpression regex(QStringLiteral("ID ([0-9a-zA-Z]+):([0-9a-zA-Z]+)"));

            foreach(const QString s, process.stdOut().split(QRegularExpression(QStringLiteral("\n|\r\n|\r")), Qt::SkipEmptyParts))
            {
                QRegularExpressionMatch match = regex.match(s);

                if(match.hasMatch())
                {
                    bool vidOk, pidOk;
                    int vid = match.captured(1).toInt(&vidOk, 16);
                    int pid = match.captured(2).toInt(&pidOk, 16);

                    if(vidOk && pidOk)
                    {
                        QPair<int, int> entry(vid, pid);

                        if(pidvidlist.contains(entry))
                        {
                            devices.append(QString(QStringLiteral("%1:%2,NULL")).
                                           arg(vid, 4, 16, QChar('0')).
                                           arg(pid, 4, 16, QChar('0')));
                        }
                    }
                }
            }
        }
#endif
    }

    return devices;
}

bool imxGetDevice(QJsonObject &obj)
{
    Utils::QtcProcess process;
    process.setTimeoutS(10);
    process.setProcessChannelMode(QProcess::MergedChannels);

    Utils::FilePath blhost_binary;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        blhost_binary = Core::ICore::resourcePath(QStringLiteral("blhost/win/blhost.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        blhost_binary = Core::ICore::resourcePath(QStringLiteral("blhost/mac/blhost"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            blhost_binary = Core::ICore::resourcePath(QStringLiteral("blhost/linux/amd64/blhost"));
        }
    }

    if(blhost_binary.isEmpty())
    {
        return false;
    }

    QStringList args = QStringList() <<
                       QStringLiteral("-u") <<
                       obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                       QStringLiteral("--") <<
                       QStringLiteral("get-property") <<
                       QStringLiteral("1");

    process.setCommand(Utils::CommandLine(blhost_binary, args));
    process.runBlocking(Utils::EventLoopMode::On);

    if((process.result() == Utils::ProcessResult::FinishedWithSuccess) || (process.result() == Utils::ProcessResult::FinishedWithError))
    {
        QStringList in = process.stdOut().split(QRegularExpression(QStringLiteral("\n|\r\n|\r")), Qt::SkipEmptyParts);

        if((in.size() == 1) && (in.at(0).contains(QStringLiteral("cannot open USB HID device"))))
        {
            return false;
        }
        else
        {
            return true;
        }
    }
    else
    {
        return false;
    }
}

bool imxDownloadBootloaderAndFirmware(QJsonObject &obj, bool forceFlashFSErase, bool justEraseFlashFs)
{
    bool result = true;
    Utils::QtcProcess process;

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(IMX_SETTINGS_GROUP));

    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
        Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)) |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setAttribute(Qt::WA_ShowWithoutActivating);
    dialog->setWindowTitle(Tr::tr("NXP IMX"));
    dialog->setSizeGripEnabled(true);

    if(settings->contains(QStringLiteral(LAST_IMX_TERMINAL_WINDOW_GEOMETRY)))
    {
        dialog->restoreGeometry(settings->value(QStringLiteral(LAST_IMX_TERMINAL_WINDOW_GEOMETRY)).toByteArray());
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

    int ok = true;
    int *okPtr = &ok;

    QEventLoop loop;

    QMetaObject::Connection conn = QObject::connect(dialog, &QDialog::finished,
        &loop, [okPtr] () {
        *okPtr = false;
    });

    QObject::connect(dialog, &QDialog::finished, &loop, &QEventLoop::quit);

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

            if(out.startsWith(QStringLiteral("(1/1)")))
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

            if(out.startsWith(QStringLiteral("(1/1)")))
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

    Utils::FilePath sdphost_binary, blhost_binary;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        sdphost_binary = Core::ICore::resourcePath(QStringLiteral("sdphost/win/sdphost.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        sdphost_binary = Core::ICore::resourcePath(QStringLiteral("sdphost/mac/sdphost"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            sdphost_binary = Core::ICore::resourcePath(QStringLiteral("sdphost/linux/i386/sdphost"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            sdphost_binary = Core::ICore::resourcePath(QStringLiteral("sdphost/linux/amd64/sdphost"));
        }
    }

    if(sdphost_binary.isEmpty())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("NXP IMX"),
            Tr::tr("This feature is not supported on this machine!"));

        result = false;
        goto cleanup;
    }

    if(Utils::HostOsInfo::isWindowsHost())
    {
        blhost_binary = Core::ICore::resourcePath(QStringLiteral("blhost/win/blhost.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        blhost_binary = Core::ICore::resourcePath(QStringLiteral("blhost/mac/blhost"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            blhost_binary = Core::ICore::resourcePath(QStringLiteral("blhost/linux/amd64/blhost"));
        }
    }

    if(blhost_binary.isEmpty())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("NXP IMX"),
            Tr::tr("This feature is not supported on this machine!"));

        result = false;
        goto cleanup;
    }

    dialog->show();

    // Load Flash Loader
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("sdphost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("write-file") <<
                           obj.value(QStringLiteral("sdphost_flash_loader_address")).toString() <<
                           QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("sdphost_flash_loader_path")).toString()));

        QString command = QString(QStringLiteral("%1 %2")).arg(sdphost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(sdphost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Start Flash Loader
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("sdphost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("jump-address") <<
                           obj.value(QStringLiteral("sdphost_flash_loader_address")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(sdphost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(sdphost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(conn);

    if(!ok)
    {
        result = false;
        goto cleanup;
    }

    // Wait for Flash Loader
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("get-property") <<
                           QStringLiteral("1");

        QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(blhost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Write Memory Configuration
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("fill-memory") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_address")).toString() <<
                           QStringLiteral("4") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_spi")).toString() <<
                           QStringLiteral("word");

        QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(blhost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Load Memory Configuration
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("configure-memory") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_type")).toString() <<
                           obj.value(QStringLiteral("blhost_memory_configuration_address")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(blhost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Erase Memory
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("-t") <<
                           QStringLiteral("100000") <<
                           QStringLiteral("--") <<
                           QStringLiteral("flash-erase-region") <<
                           obj.value(QStringLiteral("blhost_secure_bootloader_erase_address")).toString() <<
                           obj.value(QStringLiteral("blhost_secure_bootloader_length")).toString() <<
                           obj.value(QStringLiteral("blhost_memory_configuration_type")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(blhost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Write FCB Configuration
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("fill-memory") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_address")).toString() <<
                           QStringLiteral("4") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_fcb")).toString() <<
                           QStringLiteral("word");

        QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(blhost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Load Memory Configuration Again
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("configure-memory") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_type")).toString() <<
                           obj.value(QStringLiteral("blhost_memory_configuration_address")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(blhost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Write Image
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("write-memory") <<
                           obj.value(QStringLiteral("blhost_secure_bootloader_write_address")).toString() <<
                           QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("blhost_secure_bootloader_path")).toString()));

        QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(blhost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    if(forceFlashFSErase
    && (obj.value(QStringLiteral("blhost_disk_address")).toString().toInt(nullptr, 16) != 0)
    && (obj.value(QStringLiteral("blhost_disk_size")).toString().toInt(nullptr, 16) > 0))
    {
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightseagreen") : QStringLiteral("seagreen")).
                                  arg(Tr::tr("This command takes a while to execute. Please be patient.")));

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("60000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_disk_address")).toString() <<
                               obj.value(QStringLiteral("blhost_disk_size")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
            plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                      arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                      arg(command));

            process.setTimeoutS(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(blhost_binary, args));
            process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if(!justEraseFlashFs)
    {
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightseagreen") : QStringLiteral("seagreen")).
                                  arg(Tr::tr("This command takes a while to execute. Please be patient.")));

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("60000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_firmware_address")).toString() <<
                               obj.value(QStringLiteral("blhost_firmware_length")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
            plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                      arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                      arg(command));

            process.setTimeoutS(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(blhost_binary, args));
            process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }

        // Write Image
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("--") <<
                               QStringLiteral("write-memory") <<
                               obj.value(QStringLiteral("blhost_firmware_address")).toString() <<
                               QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("blhost_firmware_path")).toString()));

            QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
            plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                      arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                      arg(command));

            process.setTimeoutS(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(blhost_binary, args));
            process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    // Burn E-Fuse
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("efuse-program-once") <<
                           obj.value(QStringLiteral("blhost_efuse_burn_address")).toString() <<
                           obj.value(QStringLiteral("blhost_efuse_burn_data")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(blhost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Reset
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("reset");

        QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(blhost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

cleanup:

    settings->setValue(QStringLiteral(LAST_IMX_TERMINAL_WINDOW_GEOMETRY), dialog->saveGeometry());
    settings->endGroup();
    delete dialog;

    return result;
}

bool imxDownloadFirmware(QJsonObject &obj, bool forceFlashFSErase, bool justEraseFlashFs)
{
    bool result = true;
    Utils::QtcProcess process;

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(IMX_SETTINGS_GROUP));

    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
        Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)) |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setAttribute(Qt::WA_ShowWithoutActivating);
    dialog->setWindowTitle(Tr::tr("NXP IMX"));
    dialog->setSizeGripEnabled(true);

    if(settings->contains(QStringLiteral(LAST_IMX_TERMINAL_WINDOW_GEOMETRY)))
    {
        dialog->restoreGeometry(settings->value(QStringLiteral(LAST_IMX_TERMINAL_WINDOW_GEOMETRY)).toByteArray());
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

            if(out.startsWith(QStringLiteral("(1/1)")))
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

            if(out.startsWith(QStringLiteral("(1/1)")))
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

    Utils::FilePath blhost_binary;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        blhost_binary = Core::ICore::resourcePath(QStringLiteral("blhost/win/blhost.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        blhost_binary = Core::ICore::resourcePath(QStringLiteral("blhost/mac/blhost"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            blhost_binary = Core::ICore::resourcePath(QStringLiteral("blhost/linux/amd64/blhost"));
        }
    }

    if(blhost_binary.isEmpty())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("NXP IMX"),
            Tr::tr("This feature is not supported on this machine!"));

        result = false;
        goto cleanup;
    }

    dialog->show();

    if(forceFlashFSErase
    && (obj.value(QStringLiteral("blhost_disk_address")).toString().toInt(nullptr, 16) != 0)
    && (obj.value(QStringLiteral("blhost_disk_size")).toString().toInt(nullptr, 16) > 0))
    {
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightseagreen") : QStringLiteral("seagreen")).
                                  arg(Tr::tr("This command takes a while to execute. Please be patient.")));

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("60000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_disk_address")).toString() <<
                               obj.value(QStringLiteral("blhost_disk_size")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
            plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                      arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                      arg(command));

            process.setTimeoutS(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(blhost_binary, args));
            process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if(!justEraseFlashFs)
    {
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightseagreen") : QStringLiteral("seagreen")).
                                  arg(Tr::tr("This command takes a while to execute. Please be patient.")));

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("60000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_firmware_address")).toString() <<
                               obj.value(QStringLiteral("blhost_firmware_length")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
            plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                      arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                      arg(command));

            process.setTimeoutS(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(blhost_binary, args));
            process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }

        // Write Image
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("--") <<
                               QStringLiteral("write-memory") <<
                               obj.value(QStringLiteral("blhost_firmware_address")).toString() <<
                               QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("blhost_firmware_path")).toString()));

            QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
            plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                      arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                      arg(command));

            process.setTimeoutS(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(blhost_binary, args));
            process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    // Reset
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("reset");

        QString command = QString(QStringLiteral("%1 %2")).arg(blhost_binary.toString()).arg(args.join(QLatin1Char(' ')));
        plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                  arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("lightblue") : QStringLiteral("blue")).
                                  arg(command));

        process.setTimeoutS(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(blhost_binary, args));
        process.runBlocking(Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

cleanup:

    settings->setValue(QStringLiteral(LAST_IMX_TERMINAL_WINDOW_GEOMETRY), dialog->saveGeometry());
    settings->endGroup();
    delete dialog;

    return result;
}

} // namespace Internal
} // namespace OpenMV
