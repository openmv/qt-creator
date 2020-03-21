#include "dfu-util.h"

bool downloadFirmware(const QString &path)
{
    bool result = false;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        QFile file(QDir::tempPath() + QDir::separator() + QStringLiteral("openmvide-dfu-util.cmd"));

        if(file.open(QIODevice::WriteOnly))
        {
            QByteArray command = QString(QStringLiteral("\"") +
                QDir::cleanPath(QDir::toNativeSeparators(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/windows/dfu-util.exe"))) + QStringLiteral("\" -d ,0483:df11 -a 0 -R -D \"") +
                QDir::cleanPath(QDir::toNativeSeparators(path)) + QStringLiteral("\"\n")).toUtf8();

            if(file.write(command) == command.size())
            {
                file.close();
                file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                result = QProcess::startDetached(QStringLiteral("cmd.exe"), QStringList()
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
            QByteArray command = QString(QStringLiteral("#!/bin/sh\n\n\"") +
                QDir::cleanPath(QDir::toNativeSeparators(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/osx/dfu-util"))) + QStringLiteral("\" -d ,0483:df11 -a 0 -R -D \"") +
                QDir::cleanPath(QDir::toNativeSeparators(path)) + QStringLiteral("\"\n")).toUtf8();

            if(file.write(command) == command.size())
            {
                file.close();
                file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                result = QProcess::startDetached(QStringLiteral("open"), QStringList()
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
                QByteArray command = QString(QStringLiteral("#!/bin/sh\n\n\"") +
                    QDir::cleanPath(QDir::toNativeSeparators(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/linux32/dfu-util"))) + QStringLiteral("\" -d ,0483:df11 -a 0 -R -D \"") +
                    QDir::cleanPath(QDir::toNativeSeparators(path)) + QStringLiteral("\"\n")).toUtf8();

                if(file.write(command) == command.size())
                {
                    file.close();
                    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                    result = QProcess::startDetached(QStringLiteral("x-terminal-emulator"), QStringList()
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
                QByteArray command = QString(QStringLiteral("#!/bin/sh\n\n\"") +
                    QDir::cleanPath(QDir::toNativeSeparators(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/linux64/dfu-util"))) + QStringLiteral("\" -d ,0483:df11 -a 0 -R -D \"") +
                    QDir::cleanPath(QDir::toNativeSeparators(path)) + QStringLiteral("\"\n")).toUtf8();

                if(file.write(command) == command.size())
                {
                    file.close();
                    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                    result = QProcess::startDetached(QStringLiteral("x-terminal-emulator"), QStringList()
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
                QByteArray command = QString(QStringLiteral("#!/bin/sh\n\n\"") +
                    QDir::cleanPath(QDir::toNativeSeparators(Core::ICore::resourcePath() + QStringLiteral("/dfu-util/arm/dfu-util"))) + QStringLiteral("\" -d ,0483:df11 -a 0 -R -D \"") +
                    QDir::cleanPath(QDir::toNativeSeparators(path)) + QStringLiteral("\"\n")).toUtf8();

                if(file.write(command) == command.size())
                {
                    file.close();
                    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                    result = QProcess::startDetached(QStringLiteral("x-terminal-emulator"), QStringList()
                        << QStringLiteral("-e")
                        << QFileInfo(file).filePath());
                }
            }
        }
    }

    if(!result)
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            QObject::tr("Download Firmware"),
            QObject::tr("Failed to launch dfu-util!"));
    }

    return result;
}
