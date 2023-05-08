#include "openmvplugin.h"

namespace OpenMV {
namespace Internal {

QByteArray loadFilter(const QByteArray &data)
{
    QString data2 = QString::fromUtf8(data);

    enum
    {
        IN_NONE,
        IN_COMMENT,
        IN_STRING_0,
        IN_STRING_1
    }
    in_state = IN_NONE;

    for(int i = 0, j = data2.size(), k = 0; i < j; i++)
    {
        switch(in_state)
        {
            case IN_NONE:
            {
                if((data2.at(i) == QLatin1Char('#')) && ((!i) || (data2.at(i-1) != QLatin1Char('\\'))))
                {
                    in_state = IN_COMMENT;
                    k = i;
                }
                if((data2.at(i) == QLatin1Char('\'')) && ((!i) || (data2.at(i-1) != QLatin1Char('\\')))) in_state = IN_STRING_0;
                if((data2.at(i) == QLatin1Char('\"')) && ((!i) || (data2.at(i-1) != QLatin1Char('\\')))) in_state = IN_STRING_1;
                break;
            }
            case IN_COMMENT:
            {
                if((data2.at(i) == QLatin1Char('\n')) && (data2.at(i-1) != QLatin1Char('\\')))
                {
                    in_state = IN_NONE;
                    int size = i - k; // excluding the newline character
                    data2.remove(k, size);
                    // reset backwards
                    i = k - 1;
                    j -= size;
                }
                break;
            }
            case IN_STRING_0:
            {
                if((data2.at(i) == QLatin1Char('\'')) && (data2.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                break;
            }
            case IN_STRING_1:
            {
                if((data2.at(i) == QLatin1Char('\"')) && (data2.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                break;
            }
        }
    }

    data2.remove(QRegularExpression(QStringLiteral("\\s*$"), QRegularExpression::MultilineOption));
    data2.remove(QRegularExpression(QStringLiteral("^\\s*?\n"), QRegularExpression::MultilineOption));
    data2.remove(QRegularExpression(QStringLiteral("^\\s*#.*?\n"), QRegularExpression::MultilineOption));
    data2.remove(QRegularExpression(QStringLiteral("^\\s*['\"]['\"]['\"].*?['\"]['\"]['\"]\\s*?\n"), QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption));
    return data2.replace(QStringLiteral("    "), QStringLiteral("\t")).toUtf8();
}

importDataList_t loadFolder(const QString &rootPath, const QString &path, bool flat)
{
    importDataList_t list;

    QDirIterator it(path, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    while(it.hasNext())
    {
        QString pathName = it.next();

        if(QFileInfo(pathName).isDir())
        {
            QStringList pathNameList = QStringList() << pathName;

            while(!pathNameList.isEmpty())
            {
                QString workingPathName = pathNameList.takeFirst();

                QString initName = workingPathName + QStringLiteral("/__init__.py");

                if(QFileInfo(initName).exists() && QFileInfo(initName).isFile())
                {
                    QFileInfo info(workingPathName);
                    QString relativePath = QDir::cleanPath(QDir::fromNativeSeparators(QDir(rootPath).relativeFilePath(info.path()))).replace(QLatin1Char('/'), QLatin1Char('.'));
                    if (relativePath == QStringLiteral(".")) relativePath = QString();
                    else relativePath += QLatin1Char('.');
                    importData_t data;
                    data.moduleName = (flat ? QString() : relativePath) + QDir(workingPathName).dirName();
                    data.modulePath = workingPathName;
                    data.moduleHash = QByteArray();

                    QDirIterator it2(workingPathName, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
                    QByteArrayList hashList;

                    while(it2.hasNext())
                    {
                        QString subPathName = it2.next();

                        if(QFileInfo(subPathName).isDir())
                        {
                            pathNameList.append(subPathName);
                        }
                        else if(QFileInfo(subPathName).isFile() && subPathName.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
                        {
                            QFile file(subPathName);

                            if(file.open(QIODevice::ReadOnly))
                            {
                                hashList.append(loadFilter(file.readAll()));
                            }
                        }
                    }

                    qSort(hashList);
                    foreach(const QByteArray &b, hashList) data.moduleHash.append(b);
                    data.moduleHash = QCryptographicHash::hash(data.moduleHash, QCryptographicHash::Sha1);
                    list.append(data);
                }
                else
                {
                    list.append(loadFolder(rootPath, workingPathName, flat));
                }
            }
        }
        else if(QFileInfo(pathName).isFile() && pathName.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
        {
            QFile file(pathName);

            if(file.open(QIODevice::ReadOnly))
            {
                QFileInfo info(pathName);
                QString relativePath = QDir::cleanPath(QDir::fromNativeSeparators(QDir(rootPath).relativeFilePath(info.path()))).replace(QLatin1Char('/'), QLatin1Char('.'));
                if (relativePath == QStringLiteral(".")) relativePath = QString();
                else relativePath += QLatin1Char('.');
                importData_t data;
                data.moduleName = (flat ? QString() : relativePath) + QFileInfo(pathName).baseName();
                data.modulePath = pathName;
                data.moduleHash = QCryptographicHash::hash(loadFilter(file.readAll()), QCryptographicHash::Sha1);
                list.append(data);
            }
        }
    }

    return list;
}

void OpenMVPlugin::parseImports(const QString &fileText, const QString &moduleFolder, const QStringList &builtInModules, importDataList_t &targetModules, QStringList &errorModules)
{
    QRegularExpression importFromRegex(QStringLiteral("(import|from)\\s+(.*)"));
    QRegularExpression importAsRegex(QStringLiteral("\\s+as\\s+.*"));
    QRegularExpression fromImportRegex(QStringLiteral("\\s+import\\s+.*"));

    QStringList fileTextList = QStringList() << fileText;
    QStringList fileTextPathList = QStringList() << moduleFolder;

    while(fileTextList.size())
    {
        QStringList lineList = fileTextList.takeFirst().replace(QRegularExpression(QStringLiteral("\\s*[\\\\\\s]+[\r\n]+\\s*")), QStringLiteral(" ")).split(QRegularExpression(QStringLiteral("[\r\n;]")), QString::SkipEmptyParts);
        QString lineListPath = fileTextPathList.takeFirst();

        foreach(const QString &line, lineList)
        {
            QRegularExpressionMatch importFromRegexMatch = importFromRegex.match(line);

            if(importFromRegexMatch.hasMatch())
            {
                QStringList importLineList = importFromRegexMatch.captured(2).remove(importAsRegex).remove(fromImportRegex).split(QLatin1Char(','), QString::SkipEmptyParts);

                foreach(const QString &importLine, importLineList)
                {
                    QString importLinePath = importLine.simplified();

                    if(!builtInModules.contains(importLinePath))
                    {
                        bool contains = false;

                        if(importLinePath.startsWith(QLatin1Char('.')))
                        {
                            contains = true;
                            break;
                        }
                        else
                        {
                            foreach(const importData_t &data, targetModules)
                            {
                                if(data.moduleName == importLinePath)
                                {
                                    contains = true;
                                    break;
                                }
                            }
                        }

                        if((!contains) && (!errorModules.contains(importLinePath)))
                        {
                            if(!m_portPath.isEmpty())
                            {
                                QString importLinePathReal = QString(importLinePath).replace(QLatin1Char('.'), QLatin1Char('/'));
                                QFileInfo infoF(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + lineListPath + QDir::separator() + importLinePathReal + QStringLiteral(".py"))));

                                if((!infoF.exists()) || (!infoF.isFile()))
                                {
                                    QFileInfo infoD(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + lineListPath + QDir::separator() + importLinePathReal + QStringLiteral("/__init__.py"))));

                                    if(infoD.exists() && infoD.isFile())
                                    {
                                        importData_t data;
                                        data.moduleName = importLinePath;
                                        data.modulePath = infoD.path();
                                        data.moduleHash = QByteArray();

                                        QDirIterator it2(QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + lineListPath + QDir::separator() + importLinePathReal)), QDir::Files | QDir::NoDotAndDotDot);
                                        QByteArrayList hashList;

                                        while(it2.hasNext())
                                        {
                                            QString filePath = it2.next();

                                            if(filePath.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
                                            {
                                                QFile file(filePath);

                                                if(file.open(QIODevice::ReadOnly))
                                                {
                                                    QByteArray bytes = loadFilter(file.readAll());
                                                    fileTextList.append(QString::fromUtf8(bytes));
                                                    fileTextPathList.append(QDir(m_portPath).relativeFilePath(filePath));
                                                    hashList.append(bytes);
                                                }
                                            }
                                        }

                                        qSort(hashList);
                                        foreach(const QByteArray &b, hashList) data.moduleHash.append(b);
                                        data.moduleHash = QCryptographicHash::hash(data.moduleHash, QCryptographicHash::Sha1);
                                        targetModules.append(data);
                                    }
                                    else
                                    {
                                        errorModules.append(importLinePath);
                                    }
                                }
                                else
                                {
                                    QFile file(infoF.filePath());

                                    if(file.open(QIODevice::ReadOnly))
                                    {
                                        QByteArray bytes = loadFilter(file.readAll());
                                        fileTextList.append(QString::fromUtf8(bytes));
                                        fileTextPathList.append(QDir(m_portPath).relativeFilePath(infoF.path()));

                                        importData_t data;
                                        data.moduleName = importLinePath;
                                        data.modulePath = infoF.filePath();
                                        data.moduleHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
                                        targetModules.append(data);
                                    }
                                }
                            }
                            else
                            {
                                // Can't really do anything...
                            }
                        }
                    }
                }
            }
        }
    }
}

static bool myCopy(const QString &src, const QString &dst)
{
    QFile file(src);

    if(file.open(QIODevice::ReadOnly))
    {
        QTemporaryFile temp;

        if(temp.open())
        {
            QByteArray data = loadFilter(file.readAll());

            if(temp.write(data) == data.size())
            {
                temp.close();

                if(QFile::copy(QFileInfo(temp).filePath(), dst))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

static bool myCopyFolder(const QString &src, const QString &dst)
{
    QDirIterator it(src, QDir::Files | QDir::NoDotAndDotDot);

    while(it.hasNext())
    {
        QString path = it.next();

        if(path.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
        {
            if(!myCopy(path, dst + QDir::separator() + QFileInfo(path).fileName()))
            {
                return false;
            }
        }
    }

    return true;
}

static bool myCopyFolderBasic(const QString &src, const QString &dst)
{
    QDirIterator it(src, QDir::Files | QDir::NoDotAndDotDot);

    while(it.hasNext())
    {
        QString path = it.next();

        if(path.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
        {
            if(!QFile::copy(path, dst + QDir::separator() + QFileInfo(path).fileName()))
            {
                return false;
            }
        }
    }

    return true;
}

static bool myClearFolder(const QString &folder)
{
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot);

    while(it.hasNext())
    {
        QString path = it.next();

        if(path.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
        {
            if(!QFile::remove(path))
            {
                return false;
            }
        }
    }

    return true;
}

bool OpenMVPlugin::importHelper(const QByteArray &text)
{
    QMap<QString, QPair<importData_t, QString> > map;

    foreach(const importData_t &data, m_exampleModules)
    {
        map.insert(data.moduleName, QPair<importData_t, QString>(data, QFileInfo(data.modulePath).fileName())); // flat
    }

    QDir documentsDir = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV"));

    foreach(const importData_t &data, m_documentsModules)
    {
        map.insert(data.moduleName, QPair<importData_t, QString>(data, documentsDir.relativeFilePath(data.modulePath))); // Documents Folder Modules override Examples Modules
    }

    QStringList builtInModules;

    foreach(const documentation_t module, m_modules)
    {
        builtInModules.append(module.name);
    }

    importDataList_t targetModules;
    QStringList errorModules;
    parseImports(QString::fromUtf8(loadFilter(text)), QDir::separator(), builtInModules, targetModules, errorModules);

    while(!targetModules.isEmpty())
    {
        importData_t targetModule = targetModules.takeFirst();

        if(map.contains(targetModule.moduleName) && (targetModule.moduleHash != map.value(targetModule.moduleName).first.moduleHash))
        {
            int answer = QMessageBox::question(Core::ICore::dialogParent(),
                tr("Import Helper"),
                tr("Module \"%L1\" on your OpenMV Cam is different than the copy on your computer.\n\nWould you like OpenMV IDE to update the module on your OpenMV Cam?").arg(targetModule.moduleName),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

            if(answer == QMessageBox::Yes)
            {
                QString sourcePath = map.value(targetModule.moduleName).first.modulePath;

                if(QFileInfo(sourcePath).isDir())
                {
                    QString targetPath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + map.value(targetModule.moduleName).second));

                    if(!myClearFolder(targetPath))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to remove \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    if(!myCopyFolder(sourcePath, targetPath))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    QDirIterator it(sourcePath, QDir::Files | QDir::NoDotAndDotDot);

                    while(it.hasNext())
                    {
                        QFile file(it.next());

                        if(file.open(QIODevice::ReadOnly))
                        {
                            parseImports(QString::fromUtf8(loadFilter(file.readAll())), QDir::separator(), builtInModules, targetModules, errorModules);
                        }
                    }
                }
                else
                {
                    QString targetPath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + map.value(targetModule.moduleName).second));

                    if(!QFile::remove(targetPath))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to remove \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    if(!myCopy(sourcePath, targetPath))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    QFile file(sourcePath);

                    if(file.open(QIODevice::ReadOnly))
                    {
                        parseImports(QString::fromUtf8(loadFilter(file.readAll())), QDir::separator(), builtInModules, targetModules, errorModules);
                    }
                }
            }
            else if(answer == QMessageBox::No)
            {
                int answer = QMessageBox::question(Core::ICore::dialogParent(),
                    tr("Import Helper"),
                    tr("Would you like OpenMV IDE to update the module on your computer?"),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

                if(answer == QMessageBox::Yes)
                {
                    QString targetPath = map.value(targetModule.moduleName).first.modulePath;

                    if(Utils::FileName::fromString(targetPath).isChildOf(QDir(Core::ICore::userResourcePath() + QStringLiteral("/examples"))))
                    {
                        targetPath = QDir::cleanPath(QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV/") + (QFileInfo(targetPath).isDir() ? QDir(targetPath).dirName() : QFileInfo(targetPath).fileName())));
                    }

                    if(QFileInfo(targetPath).isDir())
                    {
                        QString sourcePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + map.value(targetModule.moduleName).second));

                        if(QDir(targetPath).exists()) // May not exist if copying from examples to documents folder.
                        {
                            if(!myClearFolder(targetPath))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Import Helper"),
                                    tr("Failed to remove \"%L1\"!").arg(targetPath));

                                continue;
                            }
                        }
                        else if(!QDir().mkpath(QFileInfo(targetPath).path()))
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Import Helper"),
                                tr("Failed to create \"%L1\"!").arg(QFileInfo(targetPath).path()));

                            continue;
                        }

                        if(!myCopyFolderBasic(sourcePath, targetPath)) // Don't use myCopyFolder() here...
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Import Helper"),
                                tr("Failed to create \"%L1\"!").arg(targetPath));

                            continue;
                        }
                    }
                    else
                    {
                        QString sourcePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + map.value(targetModule.moduleName).second));

                        if(QFileInfo(targetPath).exists()) // May not exist if copying from examples to documents folder.
                        {
                            if(!QFile::remove(targetPath))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Import Helper"),
                                    tr("Failed to remove \"%L1\"!").arg(targetPath));

                                continue;
                            }
                        }

                        if(!QFile::copy(sourcePath, targetPath)) // Don't use myCopy() here...
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Import Helper"),
                                tr("Failed to create \"%L1\"!").arg(targetPath));

                            continue;
                        }
                    }
                }
                else if(answer == QMessageBox::Cancel)
                {
                    return false;
                }
            }
            else if(answer == QMessageBox::Cancel)
            {
                return false;
            }
        }
    }

    while(!errorModules.isEmpty())
    {
        QString errorModule = errorModules.takeFirst();

        if(map.contains(errorModule))
        {
            int answer = QMessageBox::question(Core::ICore::dialogParent(),
                tr("Import Helper"),
                tr("Module \"%L1\" may be required to run your script.\n\nWould you like OpenMV IDE to copy it to your OpenMV Cam?").arg(errorModule),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

            if(answer == QMessageBox::Yes)
            {
                QString sourcePath = map.value(errorModule).first.modulePath;

                if(QFileInfo(sourcePath).isDir())
                {
                    QString targetPath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + map.value(errorModule).second));

                    if(!QDir().mkpath(QDir::toNativeSeparators(QDir::cleanPath(targetPath))))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    if(!myCopyFolder(sourcePath, targetPath))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    QDirIterator it(sourcePath, QDir::Files | QDir::NoDotAndDotDot);

                    while(it.hasNext())
                    {
                        QFile file(it.next());

                        if(file.open(QIODevice::ReadOnly))
                        {
                            parseImports(QString::fromUtf8(loadFilter(file.readAll())), QDir::separator(), builtInModules, targetModules, errorModules);
                        }
                    }
                }
                else
                {
                    QString targetPath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath + QDir::separator() + map.value(errorModule).second));

                    if(!QDir().mkpath(QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(targetPath).path()))))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(QFileInfo(targetPath).path()));

                        continue;
                    }

                    if(!myCopy(sourcePath, targetPath))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Import Helper"),
                            tr("Failed to create \"%L1\"!").arg(targetPath));

                        continue;
                    }

                    QFile file(sourcePath);

                    if(file.open(QIODevice::ReadOnly))
                    {
                        parseImports(QString::fromUtf8(loadFilter(file.readAll())), QDir::separator(), builtInModules, targetModules, errorModules);
                    }
                }
            }
            else if(answer == QMessageBox::Cancel)
            {
                return false;
            }
        }
    }

    return true;
}

} // namespace Internal
} // namespace OpenMV
