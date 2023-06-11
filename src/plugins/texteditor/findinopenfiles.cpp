// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "findinopenfiles.h"

#include "textdocument.h"
#include "texteditortr.h"

#include <coreplugin/icore.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/documentmodel.h>

#include <utils/filesearch.h>

#include <QSettings>

// OPENMV-DIFF //
#include <QLabel>
// OPENMV-DIFF //

namespace TextEditor::Internal {

FindInOpenFiles::FindInOpenFiles()
{
    connect(Core::EditorManager::instance(), &Core::EditorManager::editorOpened,
            this, &FindInOpenFiles::updateEnabledState);
    connect(Core::EditorManager::instance(), &Core::EditorManager::editorsClosed,
            this, &FindInOpenFiles::updateEnabledState);
}

QString FindInOpenFiles::id() const
{
    return QLatin1String("Open Files");
}

QString FindInOpenFiles::displayName() const
{
    return Tr::tr("Open Documents");
}

Utils::FileIterator *FindInOpenFiles::files(const QStringList &nameFilters,
                                            const QStringList &exclusionFilters,
                                            const QVariant &additionalParameters) const
{
    Q_UNUSED(nameFilters)
    Q_UNUSED(exclusionFilters)
    Q_UNUSED(additionalParameters)
    QMap<Utils::FilePath, QTextCodec *> openEditorEncodings
        = TextDocument::openedTextDocumentEncodings();
    Utils::FilePaths fileNames;
    QList<QTextCodec *> codecs;
    const QList<Core::DocumentModel::Entry *> entries = Core::DocumentModel::entries();
    for (Core::DocumentModel::Entry *entry : entries) {
        const Utils::FilePath fileName = entry->filePath();
        if (!fileName.isEmpty()) {
            fileNames.append(fileName);
            QTextCodec *codec = openEditorEncodings.value(fileName);
            if (!codec)
                codec = Core::EditorManager::defaultTextCodec();
            codecs.append(codec);
        }
    }

    return new Utils::FileListIterator(fileNames, codecs);
}

QVariant FindInOpenFiles::additionalParameters() const
{
    return QVariant();
}

QString FindInOpenFiles::label() const
{
    return Tr::tr("Open documents:");
}

QString FindInOpenFiles::toolTip() const
{
    // %1 is filled by BaseFileFind::runNewSearch
    return Tr::tr("Open Documents\n%1");
}

// OPENMV-DIFF //
QWidget *FindInOpenFiles::createConfigWidget()
{
    if (!m_configWidget) {
        QLabel *label = new QLabel(Tr::tr("Please note that this only searches files that have been saved to disk."));
        label->setAlignment(Qt::AlignRight);
        m_configWidget = label;
    }
    return m_configWidget;
}
// OPENMV-DIFF //

bool FindInOpenFiles::isEnabled() const
{
    // OPENMV-DIFF //
    // return Core::DocumentModel::entryCount() > 0;
    // OPENMV-DIFF //
    const QList<Core::DocumentModel::Entry *> entries = Core::DocumentModel::entries();
    for (Core::DocumentModel::Entry *entry : entries) {
        const Utils::FilePath fileName = entry->filePath();
        if (!fileName.isEmpty()) {
            return true;
        }
    }
    return false;
    // OPENMV-DIFF //
}

void FindInOpenFiles::writeSettings(QSettings *settings)
{
    settings->beginGroup(QLatin1String("FindInOpenFiles"));
    writeCommonSettings(settings);
    settings->endGroup();
}

void FindInOpenFiles::readSettings(QSettings *settings)
{
    settings->beginGroup(QLatin1String("FindInOpenFiles"));
    readCommonSettings(settings, "*", "");
    settings->endGroup();
}

void FindInOpenFiles::updateEnabledState()
{
    emit enabledChanged(isEnabled());
}

} // TextEditor::Internal
