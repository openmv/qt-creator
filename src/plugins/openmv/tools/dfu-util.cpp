#include "dfu-util.h"

QList<QString> getDevices()
{
    QString command;
    Utils::SynchronousProcess process;
    Utils::SynchronousProcessResponse response;
    process.setTimeoutS(10);
    process.setProcessChannelMode(QProcess::MergedChannels);
    response.result = Utils::SynchronousProcessResponse::FinishedError;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/windows/dfu-util.exe")));
        response = process.run(command, QStringList() << QStringLiteral("-l"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/osx/dfu-util")));
        response = process.run(command, QStringList() << QStringLiteral("-l"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/linux32/dfu-util")));
            response = process.run(command, QStringList() << QStringLiteral("-l"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/linux64/dfu-util")));
            response = process.run(command, QStringList() << QStringLiteral("-l"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            command = QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/arm/dfu-util")));
            response = process.run(command, QStringList() << QStringLiteral("-l"));
        }
    }

    if(response.result == Utils::SynchronousProcessResponse::Finished)
    {
        QStringList list, in = response.stdOut.split(QRegularExpression(QStringLiteral("\n|\r\n|\r")), QString::SkipEmptyParts);

        foreach(const QString &string, in)
        {
            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("Found DFU: \\[([A-Fa-f0-9:]+)\\].+?alt=0.+?serial=(\".+?\")")).match(string);

            if(match.hasMatch())
            {
                list.append(match.captured(1) + QLatin1Literal(",") + match.captured(2));
            }
        }

        return list;
    }
    else
    {
        QMessageBox box(QMessageBox::Warning, QObject::tr("Get Devices"), QObject::tr("Query failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
        box.setDetailedText(response.stdOut);
        box.setInformativeText(response.exitMessage(command, process.timeoutS()));
        box.setDefaultButton(QMessageBox::Ok);
        box.setEscapeButton(QMessageBox::Cancel);
        box.exec();

        return QList<QString>();
    }
}

void downloadFirmware(QString &command, Utils::SynchronousProcess &process, Utils::SynchronousProcessResponse &response, const QString &path, const QString &device, const QString &moreArgs)
{
    response.clear();

    if(Utils::HostOsInfo::isWindowsHost())
    {
        QFile file(QDir::tempPath() + QDir::separator() + QStringLiteral("openmvide-dfu-util.cmd"));

        if(file.open(QIODevice::WriteOnly))
        {
            command = QString(QStringLiteral("start /wait \"dfu-util.exe\" \"") +
                QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/windows/dfu-util.exe"))) + QString(QStringLiteral("\" -w -d ,%1 %2 -D \"")).arg(device).arg(moreArgs) +
                QDir::toNativeSeparators(QDir::cleanPath(path)) + QStringLiteral("\"\n"));
            QByteArray command2 = command.toUtf8();

            if(file.write(command2) == command2.size())
            {
                file.close();
                file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                response = process.run(QStringLiteral("cmd.exe"), QStringList()
                    << QStringLiteral("/c")
                    << QFileInfo(file).filePath());
            }
        }
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        QFile file(QDir::tempPath()  + QDir::separator() + QStringLiteral("openmvide-dfu-util.sh"));

        if(file.open(QIODevice::WriteOnly))
        {
            command = QString(QStringLiteral("#!/bin/sh\n\n\"") +
                QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/osx/dfu-util"))) + QString(QStringLiteral("\" -w -d ,%1 %2 -D \"")).arg(device).arg(moreArgs) +
                QDir::toNativeSeparators(QDir::cleanPath(path)) + QStringLiteral("\"")); // no extra new line
            QByteArray command2 = command.toUtf8();

            if(file.write(command2) == command2.size())
            {
                file.close();
                file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                response = process.run(QStringLiteral("open"), QStringList()
                    << QStringLiteral("-a")
                    << QStringLiteral("Terminal")
                    << QFileInfo(file).filePath());
            }
        }
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            QFile file(QDir::tempPath()  + QDir::separator() + QStringLiteral("openmvide-dfu-util.sh"));

            if(file.open(QIODevice::WriteOnly))
            {
                command = QString(QStringLiteral("#!/bin/sh\n\n\"") +
                    QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/linux32/dfu-util"))) + QString(QStringLiteral("\" -w d ,%1 %2 -D \"")).arg(device).arg(moreArgs) +
                    QDir::toNativeSeparators(QDir::cleanPath(path)) + QStringLiteral("\"\n"));
                QByteArray command2 = command.toUtf8();

                if(file.write(command2) == command2.size())
                {
                    file.close();
                    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                    response = process.run(QStringLiteral("x-terminal-emulator"), QStringList()
                        << QStringLiteral("-e")
                        << QFileInfo(file).filePath());
                }
            }
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            QFile file(QDir::tempPath()  + QDir::separator() + QStringLiteral("openmvide-dfu-util.sh"));

            if(file.open(QIODevice::WriteOnly))
            {
                command = QString(QStringLiteral("#!/bin/sh\n\n\"") +
                    QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/linux64/dfu-util"))) + QString(QStringLiteral("\" -w -d ,%1 %2 -D \"")).arg(device).arg(moreArgs) +
                    QDir::toNativeSeparators(QDir::cleanPath(path)) + QStringLiteral("\"\n"));
                QByteArray command2 = command.toUtf8();

                if(file.write(command2) == command2.size())
                {
                    file.close();
                    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                    response = process.run(QStringLiteral("x-terminal-emulator"), QStringList()
                        << QStringLiteral("-e")
                        << QFileInfo(file).filePath());
                }
            }
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            QFile file(QDir::tempPath()  + QDir::separator() + QStringLiteral("openmvide-dfu-util.sh"));

            if(file.open(QIODevice::WriteOnly))
            {
                command = QString(QStringLiteral("#!/bin/sh\n\n\"") +
                    QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/arm/dfu-util"))) + QString(QStringLiteral("\" -w -d ,%1 %2 -D \"")).arg(device).arg(moreArgs) +
                    QDir::toNativeSeparators(QDir::cleanPath(path)) + QStringLiteral("\"\n"));
                QByteArray command2 = command.toUtf8();

                if(file.write(command2) == command2.size())
                {
                    file.close();
                    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                    response = process.run(QStringLiteral("x-terminal-emulator"), QStringList()
                        << QStringLiteral("-e")
                        << QFileInfo(file).filePath());
                }
            }
        }
    }
}
