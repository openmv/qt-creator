// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "findinopenfiles.h"

#include "basefilefind.h"
#include "textdocument.h"
#include "texteditortr.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/documentmodel.h>

#include <utils/qtcsettings.h>

using namespace Utils;

// OPENMV-DIFF //
#include <QLabel>
// OPENMV-DIFF //

namespace TextEditor::Internal {

class FindInOpenFiles : public BaseFileFind
{
public:
    FindInOpenFiles();
    // OPENMV-DIFF //
    QWidget *createConfigWidget() override;
    // OPENMV-DIFF //

private:
    QString id() const final;
    QString displayName() const final;
    bool isEnabled() const final;
    Utils::Store save() const final;
    void restore(const Utils::Store &s) final;

    QString label() const final;
    QString toolTip() const final;

    FileContainerProvider fileContainerProvider() const final;
    void updateEnabledState() { emit enabledChanged(isEnabled()); }

    // deprecated
    QByteArray settingsKey() const final;

    // OPENMV-DIFF //
    QPointer<QWidget> m_configWidget = nullptr;
    // OPENMV-DIFF //
};

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

FileContainerProvider FindInOpenFiles::fileContainerProvider() const
{
    return [] {
        const QMap<FilePath, QTextCodec *> encodings = TextDocument::openedTextDocumentEncodings();
        FilePaths fileNames;
        QList<QTextCodec *> codecs;
        const QList<Core::DocumentModel::Entry *> entries = Core::DocumentModel::entries();
        for (Core::DocumentModel::Entry *entry : entries) {
            const FilePath fileName = entry->filePath();
            if (!fileName.isEmpty()) {
                fileNames.append(fileName);
                QTextCodec *codec = encodings.value(fileName);
                if (!codec)
                    codec = Core::EditorManager::defaultTextCodec();
                codecs.append(codec);
            }
        }
        return FileListContainer(fileNames, codecs);
    };
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

const char kDefaultInclusion[] = "*";
const char kDefaultExclusion[] = "";

Store FindInOpenFiles::save() const
{
    Store s;
    writeCommonSettings(s, kDefaultInclusion, kDefaultExclusion);
    return s;
}

void FindInOpenFiles::restore(const Store &s)
{
    readCommonSettings(s, kDefaultInclusion, kDefaultExclusion);
}

QByteArray FindInOpenFiles::settingsKey() const
{
    return "FindInOpenFiles";
}

void setupFindInOpenFiles()
{
    static FindInOpenFiles theFindInOpenFiles;
}

} // TextEditor::Internal
