#include "openmvpluginescapecodeparser.h"

#include <coreplugin/icore.h>
#include <utils/hostosinfo.h>

namespace Core {

OpenMVPluginEscapeCodeParser::OpenMVPluginEscapeCodeParser(QObject *parent) : QObject(parent)
{
    resetParser();
    m_box = nullptr;
    m_timer = QElapsedTimer();
}

void OpenMVPluginEscapeCodeParser::resetParser()
{
    m_buffering = false;
    m_buffer = QString();
}

void OpenMVPluginEscapeCodeParser::createMessageBox(QMessageBox::Icon icon)
{
    if(m_timer.isValid() && (!m_timer.hasExpired(1000))) return;
    if(m_box) delete m_box;
    m_box = new QMessageBox(icon, QString(), m_buffer, QMessageBox::Ok, Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    m_box->setAttribute(Qt::WA_DeleteOnClose, true);
    m_box->show();
    m_timer.start();
}

void OpenMVPluginEscapeCodeParser::parseEscapeCodes(const QStringList &escapeCodes)
{
    if(escapeCodes.isEmpty()) return;
    QString function = escapeCodes.at(0);

    bool ok = false;
    int fnumber = function.toInt(&ok);

    if(ok)
    {
        switch(fnumber)
        {
            case ESCAPE_CODE_FUNCTION_RESET_SYSTEM:
            {
                resetParser();
                break;
            }
            case ESCAPE_CODE_FUNCTION_START_TEXT:
            {
                m_buffering = true;
                m_buffer = QString();
                break;
            }
            case ESCAPE_CODE_FUNCTION_DIALOG_INFO:
            {
                createMessageBox(QMessageBox::Information);
                resetParser();
                break;
            }
            case ESCAPE_CODE_FUNCTION_DIALOG_WARNING:
            {
                createMessageBox(QMessageBox::Warning);
                resetParser();
                break;
            }
            case ESCAPE_CODE_FUNCTION_DIALOG_ERROR:
            {
                createMessageBox(QMessageBox::Critical);
                resetParser();
                break;
            }
            case ESCAPE_CODE_FUNCTION_DIALOG_QUESTION:
            {
                createMessageBox(QMessageBox::Question);
                resetParser();
                break;
            }
            case ESCAPE_CODE_FUNCTION_DATASET_EDITOR_SAVE_IMAGE:
            {
                emit dataSetEditorSaveImage();
                break;
            }
        }
    }
}

QString OpenMVPluginEscapeCodeParser::parseText(const QString &text)
{
    if(!m_buffering) return text;
    m_buffer.append(text);
    return QString();
}

} // namespace Core
