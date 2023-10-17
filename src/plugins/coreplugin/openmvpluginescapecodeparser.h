#ifndef OPENMVPLUGINESCAPECODEPARSER_H
#define OPENMVPLUGINESCAPECODEPARSER_H

#include "core_global.h"

#include <QtCore>
#include <QtWidgets>

enum {
    ESCAPE_CODE_FUNCTION_RESET_SYSTEM = 0,
    ESCAPE_CODE_FUNCTION_START_TEXT = 1,

    ESCAPE_CODE_FUNCTION_DIALOG_INFO = 32,
    ESCAPE_CODE_FUNCTION_DIALOG_WARNING = 33,
    ESCAPE_CODE_FUNCTION_DIALOG_ERROR = 34,
    ESCAPE_CODE_FUNCTION_DIALOG_QUESTION = 35,

    ESCAPE_CODE_FUNCTION_DATASET_EDITOR_SAVE_IMAGE = 64,
};

namespace Core {

class CORE_EXPORT OpenMVPluginEscapeCodeParser : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVPluginEscapeCodeParser(QObject *parent = Q_NULLPTR);
    void resetParser();
    void parseEscapeCodes(const QStringList &escapeCodes);
    QString parseText(const QString &text);

signals:
    void dataSetEditorSaveImage();

private:
    void createMessageBox(QMessageBox::Icon icon);

    bool m_buffering;
    QString m_buffer;
    QPointer<QMessageBox> m_box;
};

} // namespace Core

#endif // OPENMVPLUGINESCAPECODEPARSER_H
