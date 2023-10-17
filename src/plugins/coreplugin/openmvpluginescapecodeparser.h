#ifndef OPENMVPLUGINESCAPECODEPARSER_H
#define OPENMVPLUGINESCAPECODEPARSER_H

#include "core_global.h"

#include <QtCore>
#include <QtWidgets>

/* Escape Code Protocol:
 *
 * You can trigger OpenMV IDE to do something by sending an escape code
 * starting with "\x1b[" followed by a sequence of positive integer numbers
 * seperated by ';' and terminated by 'O'. E.g. "\x1b[1O" or "\x1b[1;1O".
 * The above is the standard format for escape sequences so that they will
 * be handled gracefully (and discarded) by any other software than OpenMV IDE.
 *
 * Next, the numbers passed represent a function to be called (see enum below)
 * and any remaining numbers in the list are passed as arguments to said function.
 *
 * To send text to OpenMV IDE the START_TEXT escape code '1' is used to redirect all
 * text that is received after it to a buffer which will be processed when any other
 * escape code is received. E.g. "\x1b[1OHello World!\x1b[32O" redirects "Hello World!"
 * from being sent to the terminal to an internal buffer and the DIALOG_INFO escape code
 * '32' displays the contents of that buffer in a dialog box and turns off text redirection.
 *
 * Text is processed differently by each function. So, it's possible to send more complex
 * arguments (like json) via the text mechanism as the argument for more complex functions.
 *
 * Note, text arguments will appear in the standard terminal stream when parsed by other
 * software than OpenMV IDE. So, the text arguments should be kept readable.
 */

enum {
    ESCAPE_CODE_FUNCTION_RESET_SYSTEM = 0, // no args
    ESCAPE_CODE_FUNCTION_START_TEXT = 1, // no args

    ESCAPE_CODE_FUNCTION_DIALOG_INFO = 32, // no args
    ESCAPE_CODE_FUNCTION_DIALOG_WARNING = 33, // no args
    ESCAPE_CODE_FUNCTION_DIALOG_ERROR = 34, // no args
    ESCAPE_CODE_FUNCTION_DIALOG_QUESTION = 35, // no args

    ESCAPE_CODE_FUNCTION_DATASET_EDITOR_SAVE_IMAGE = 64, // no args
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
    QElapsedTimer m_timer;
};

} // namespace Core

#endif // OPENMVPLUGINESCAPECODEPARSER_H
