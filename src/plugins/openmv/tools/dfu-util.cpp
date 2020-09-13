#include "dfu-util.h"

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
                QDir::toNativeSeparators(QDir::cleanPath(path)) + QStringLiteral("\"\n"));
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
