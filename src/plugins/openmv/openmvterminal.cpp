#include "openmvterminal.h"

#include "openmvpluginio.h"

#include <utils/theme/theme.h>

#define TERMINAL_SETTINGS_GROUP "OpenMVTerminal"
#define GEOMETRY "Geometry"
#define HSPLITTER_STATE "HSplitterState"
#define VSPLITTER_STATE "VSplitterState"
#define ZOOM_STATE "ZoomState"
#define FONT_ZOOM_STATE "FontZoomState"
#define LAST_SAVE_IMAGE_PATH "LastSaveImagePath"
#define HISTOGRAM_COLOR_SPACE_STATE "HistogramColorSpace"
#define LAST_SAVE_LOG_PATH "LastSaveLogPath"

MyPlainTextEdit::MyPlainTextEdit(qreal fontPointSizeF, QWidget *parent) : QPlainTextEdit(parent)
{
    m_tabWidth = TextEditor::TextEditorSettings::codeStyle()->tabSettings().m_serialTerminalTabSize;
    m_textCursor = QTextCursor(document());
    m_stateMachine = ASCII;
    m_shiftReg = QByteArray();
    m_frameBufferData = QByteArray();
    m_handler = Utils::AnsiEscapeCodeHandler();
    m_lastChar = QChar();

    connect(TextEditor::TextEditorSettings::codeStyle(), &TextEditor::ICodeStylePreferences::tabSettingsChanged, this, [this] (const TextEditor::TabSettings &settings) {
        m_tabWidth = settings.m_serialTerminalTabSize;
    });

    setReadOnly(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setFrameShape(QFrame::NoFrame);
    setUndoRedoEnabled(false);
    setMaximumBlockCount(100000);
    setWordWrapMode(QTextOption::NoWrap);

    QFont font = TextEditor::TextEditorSettings::fontSettings().defaultFixedFontFamily();
    font.setPointSize(fontPointSizeF);
    setFont(font);

    QPalette p = palette();
    p.setColor(QPalette::Highlight, p.color(QPalette::Active, QPalette::Highlight));
    p.setColor(QPalette::HighlightedText, p.color(QPalette::Active, QPalette::HighlightedText));
    p.setColor(QPalette::Base, Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal));
    p.setColor(QPalette::Text, Utils::creatorTheme()->color(Utils::Theme::TextColorNormal));
    setPalette(p);

    m_isCursorVisible = true;
    QTimer *timer = new QTimer(this);
    timer->setInterval(500);

    connect(timer, &QTimer::timeout, this, [this] {
        m_isCursorVisible = !m_isCursorVisible;
        viewport()->update();
    });

    timer->start();
}

void MyPlainTextEdit::readBytes(const QByteArray &data)
{
    QByteArray REPLString = "raw REPL; CTRL-B to exit\r\n>OK";

    bool atBottom = verticalScrollBar()->value() == verticalScrollBar()->maximum();

    QByteArray buffer;

    for(int i = 0, j = data.size(); i < j; i++)
    {
        if((m_stateMachine == UTF_8) && ((data.at(i) & 0xC0) != 0x80))
        {
            m_stateMachine = ASCII;
        }

        if((m_stateMachine == EXIT_0) && ((data.at(i) & 0xFF) != 0x00))
        {
            m_stateMachine = ASCII;
        }

        switch(m_stateMachine)
        {
            case ASCII:
            {
                if(((data.at(i) & 0xE0) == 0xC0)
                || ((data.at(i) & 0xF0) == 0xE0)
                || ((data.at(i) & 0xF8) == 0xF0)
                || ((data.at(i) & 0xFC) == 0xF8)
                || ((data.at(i) & 0xFE) == 0xFC)) // UTF_8
                {
                    m_shiftReg.clear();

                    m_stateMachine = UTF_8;
                }
                else if((data.at(i) & 0xFF) == 0xFF)
                {
                    m_stateMachine = EXIT_0;
                }
                else if((data.at(i) & 0xC0) == 0x80)
                {
                    m_frameBufferData.append(data.at(i));
                }
                else if((data.at(i) & 0xFF) == 0xFE)
                {
                    int size = m_frameBufferData.size();
                    QByteArray temp;

                    for(int k = 0, l = (size / 4) * 4; k < l; k += 4)
                    {
                        int x = 0;
                        x |= (m_frameBufferData.at(k + 0) & 0x3F) << 0;
                        x |= (m_frameBufferData.at(k + 1) & 0x3F) << 6;
                        x |= (m_frameBufferData.at(k + 2) & 0x3F) << 12;
                        x |= (m_frameBufferData.at(k + 3) & 0x3F) << 18;
                        temp.append((x >> 0) & 0xFF);
                        temp.append((x >> 8) & 0xFF);
                        temp.append((x >> 16) & 0xFF);
                    }

                    if((size % 4) == 3) // 2 bytes -> 16-bits -> 24-bits sent
                    {
                        int x = 0;
                        x |= (m_frameBufferData.at(size - 3) & 0x3F) << 0;
                        x |= (m_frameBufferData.at(size - 2) & 0x3F) << 6;
                        x |= (m_frameBufferData.at(size - 1) & 0x0F) << 12;
                        temp.append((x >> 0) & 0xFF);
                        temp.append((x >> 8) & 0xFF);
                    }

                    if((size % 4) == 2) // 1 byte -> 8-bits -> 16-bits sent
                    {
                        int x = 0;
                        x |= (m_frameBufferData.at(size - 2) & 0x3F) << 0;
                        x |= (m_frameBufferData.at(size - 1) & 0x03) << 6;
                        temp.append((x >> 0) & 0xFF);
                    }

                    QPixmap pixmap = QPixmap::fromImage(QImage::fromData(temp));

                    if(!pixmap.isNull())
                    {
                        emit frameBufferData(pixmap);
                    }

                    m_frameBufferData.clear();
                }
                else if((data.at(i) & 0x80) == 0x00) // ASCII
                {
                    buffer.append(data.at(i));
                }

                break;
            }

            case UTF_8:
            {
                if((((m_shiftReg.at(0) & 0xE0) == 0xC0) && (m_shiftReg.size() == 1))
                || (((m_shiftReg.at(0) & 0xF0) == 0xE0) && (m_shiftReg.size() == 2))
                || (((m_shiftReg.at(0) & 0xF8) == 0xF0) && (m_shiftReg.size() == 3))
                || (((m_shiftReg.at(0) & 0xFC) == 0xF8) && (m_shiftReg.size() == 4))
                || (((m_shiftReg.at(0) & 0xFE) == 0xFC) && (m_shiftReg.size() == 5)))
                {
                    buffer.append(m_shiftReg + data.at(i));

                    m_stateMachine = ASCII;
                }

                break;
            }

            case EXIT_0:
            {
                m_stateMachine = EXIT_1;

                break;
            }

            case EXIT_1:
            {
                m_stateMachine = ASCII;

                break;
            }
        }

        m_shiftReg = m_shiftReg.append(data.at(i)).right(qMax(5, REPLString.size()));

        if(m_shiftReg.endsWith(REPLString)) buffer.append('\n');
    }

    foreach(const Utils::FormattedText &text, m_handler.parseText(Utils::FormattedText(QString::fromUtf8(buffer))))
    {
        QString string;
        int column = m_textCursor.columnNumber();

        if(m_lastChar.unicode() == '\n')
        {
            string.append(QLatin1Char('\n'));
        }

        for(int i = 0, j = text.text.size(); i < j; i++)
        {
            switch(text.text.at(i).unicode())
            {
                case 15:
                case 17: // XON
                case 19: // XOFF
                case 23:
                case 24:
                case 25:
                case 26:
                case 27: // Escape - AnsiEscapeCodeHandler
                case 28:
                case 29:
                case 30:
                case 31:
                {
                    break;
                }

                case 0: // Null
                {
                    m_textCursor.insertText(string, text.format);
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 1: // Home Cursor
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::Start);
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 2: // Move Cursor Left
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::Left);
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 3: // Clear Screen
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.select(QTextCursor::Document);
                    m_textCursor.removeSelectedText();
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 4:
                case 127: // Delete
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.deleteChar();
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 5: // End Cursor
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::End);
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 6: // Move Cursor Right
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::Right);
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 7: // Beep Speaker
                {
                    m_textCursor.insertText(string, text.format);
                    QApplication::beep();
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 8: // Backspace
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.deletePreviousChar();
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 9: // Tab
                {
                    for(int k = m_tabWidth - (column % m_tabWidth); k > 0; k--)
                    {
                        string.append(QLatin1Char(' '));
                        column += 1;
                    }

                    break;
                }

                case 10: // Line Feed
                {
                    if(m_lastChar.unicode() != '\r')
                    {
                        string.append(QLatin1Char('\n'));
                        column = 0;
                    }

                    break;
                }

                case 11: // Clear to end of line.
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
                    m_textCursor.removeSelectedText();
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 12: // Clear lines below.
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
                    m_textCursor.removeSelectedText();
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 13: // Carriage Return
                {
                    string.append(QLatin1Char('\n'));
                    column = 0;
                    break;
                }

                case 14: // Move Cursor Down
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::Down);
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 16: // Move Cursor Up
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::Up);
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 18: // Move to start of line.
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::StartOfLine);
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 20: // Move to end of line.
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::EndOfLine);
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 21: // Clear to start of line.
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
                    m_textCursor.removeSelectedText();
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                case 22: // Clear lines above.
                {
                    m_textCursor.insertText(string, text.format);
                    m_textCursor.movePosition(QTextCursor::Start, QTextCursor::KeepAnchor);
                    m_textCursor.removeSelectedText();
                    string.clear();
                    column = m_textCursor.columnNumber();
                    break;
                }

                default:
                {
                    string.append(text.text.at(i));
                    column += 1;
                    break;
                }
            }

            m_lastChar = text.text.at(i);
        }

        if(string.endsWith(QLatin1Char('\n')))
        {
            string.chop(1);
        }

        if(m_textCursor.document()->isEmpty())
        {
            string.remove(QRegularExpression(QStringLiteral("^\\s+")));
        }

        m_textCursor.insertText(string, text.format);
    }

    if(atBottom)
    {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        // QPlainTextEdit destroys the first calls value in case of multiline
        // text, so make sure that the scroll bar actually gets the value set.
        // Is a noop if the first call succeeded.
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
}

void MyPlainTextEdit::clear()
{
    m_textCursor.select(QTextCursor::Document);
    m_textCursor.removeSelectedText();
    m_textCursor = QTextCursor(document());
    m_stateMachine = ASCII;
    m_shiftReg = QByteArray();
    m_frameBufferData = QByteArray();
    m_handler = Utils::AnsiEscapeCodeHandler();
    m_lastChar = QChar();
}

void MyPlainTextEdit::save()
{
    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(TERMINAL_SETTINGS_GROUP));

    QString path;

    forever
    {
        path =
        QFileDialog::getSaveFileName(Core::ICore::dialogParent(), tr("Save Log"),
            settings->value(QStringLiteral(LAST_SAVE_LOG_PATH), QDir::homePath()).toString(),
            tr("Text Files (*.txt);;All files (*)"));

        if((!path.isEmpty()) && QFileInfo(path).completeSuffix().isEmpty())
        {
            QMessageBox::warning(Core::ICore::dialogParent(),
                tr("Save Log"),
                QObject::tr("Please add a file extension!"));

            continue;
        }

        break;
    }

    if(!path.isEmpty())
    {
        Utils::FileSaver file(Utils::FilePath::fromString(path));

        if(!file.hasError())
        {
            if((!file.write(toPlainText().toUtf8())) || (!file.finalize()))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Save Log"),
                    tr("Error: %L1!").arg(file.errorString()));
            }
            else
            {
                settings->setValue(QStringLiteral(LAST_SAVE_LOG_PATH), path);
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                tr("Save Log"),
                tr("Error: %L1!").arg(file.errorString()));
        }
    }

    settings->endGroup();
}

void MyPlainTextEdit::execute(bool standAlone)
{
    // Write bytes slowly so as to not overload the MicroPython board.

    if(!standAlone)
    {
        QByteArray data = "\x03\r\n\x01" + QString::fromUtf8(Core::EditorManager::currentEditor()->document()->contents()).toUtf8() + "\x04\r\n\x02";

        for(int i = 0; i < data.size(); i += (TABOO_PACKET_SIZE - 1))
        {
            emit writeBytes(data.mid(i, TABOO_PACKET_SIZE - 1));
        }

        // emit writeBytes("\x03\r\n\x01" + QString::fromUtf8(Core::EditorManager::currentEditor()->document()->contents()).toUtf8() + "\x04\r\n\x02");
    }
    else
    {
        QByteArray data = "\x03\r\n\x01\r\nexecfile(\"/main.py\")\r\n\x04\r\n\x02";

        for(int i = 0; i < data.size(); i += (TABOO_PACKET_SIZE - 1))
        {
            emit writeBytes(data.mid(i, TABOO_PACKET_SIZE - 1));
        }

        // emit writeBytes("\x03\r\n\x01\r\nexecfile(\"/main.py\")\r\n\x04\r\n\x02");
    }
}

void MyPlainTextEdit::interrupt()
{
    emit writeBytes("\x03");
}

void MyPlainTextEdit::reload()
{
    emit writeBytes("\x04");
}

void MyPlainTextEdit::keyPressEvent(QKeyEvent *event)
{
    switch(event->key())
    {
        case Qt::Key_Delete:
        {
            emit writeBytes("\x04"); // CTRL+D (4)
            break;
        }
        case Qt::Key_Home:
        {
            if(!(event->modifiers() & Qt::ControlModifier))
            {
                emit writeBytes("\x01"); // CTRL+A (1)
            }

            break;
        }
        case Qt::Key_End:
        {
            if(!(event->modifiers() & Qt::ControlModifier))
            {
                emit writeBytes("\x05"); // CTRL+E (5)
            }

            break;
        }
        case Qt::Key_Left:
        {
            emit writeBytes("\x02"); // CTRL+B (2)
            break;
        }
        case Qt::Key_Up:
        {
            emit writeBytes("\x10"); // CTRL+P (16)
            break;
        }
        case Qt::Key_Right:
        {
            emit writeBytes("\x06"); // CTRL+F (6)
            break;
        }
        case Qt::Key_Down:
        {
            emit writeBytes("\x0E"); // CTRL+N (14)
            break;
        }
        case Qt::Key_PageUp:
        {
            QPlainTextEdit::keyPressEvent(event);
            break;
        }
        case Qt::Key_PageDown:
        {
            QPlainTextEdit::keyPressEvent(event);
            break;
        }
        case Qt::Key_Tab:
        case Qt::Key_Backspace:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Space:
        case Qt::Key_Exclam:
        case Qt::Key_QuoteDbl:
        case Qt::Key_NumberSign:
        case Qt::Key_Dollar:
        case Qt::Key_Percent:
        case Qt::Key_Ampersand:
        case Qt::Key_Apostrophe:
        case Qt::Key_ParenLeft:
        case Qt::Key_ParenRight:
        case Qt::Key_Asterisk:
        case Qt::Key_Plus:
        case Qt::Key_Comma:
        case Qt::Key_Minus:
        case Qt::Key_Period:
        case Qt::Key_Slash:
        case Qt::Key_0:
        case Qt::Key_1:
        case Qt::Key_2:
        case Qt::Key_3:
        case Qt::Key_4:
        case Qt::Key_5:
        case Qt::Key_6:
        case Qt::Key_7:
        case Qt::Key_8:
        case Qt::Key_9:
        case Qt::Key_Colon:
        case Qt::Key_Semicolon:
        case Qt::Key_Less:
        case Qt::Key_Equal:
        case Qt::Key_Greater:
        case Qt::Key_Question:
        case Qt::Key_At:
        case Qt::Key_A:
        case Qt::Key_B:
        case Qt::Key_C:
        case Qt::Key_D:
        case Qt::Key_E:
        case Qt::Key_F:
        case Qt::Key_G:
        case Qt::Key_H:
        case Qt::Key_I:
        case Qt::Key_J:
        case Qt::Key_K:
        case Qt::Key_L:
        case Qt::Key_M:
        case Qt::Key_N:
        case Qt::Key_O:
        case Qt::Key_P:
        case Qt::Key_Q:
        case Qt::Key_R:
        case Qt::Key_S:
        case Qt::Key_T:
        case Qt::Key_U:
        case Qt::Key_V:
        case Qt::Key_W:
        case Qt::Key_X:
        case Qt::Key_Y:
        case Qt::Key_Z:
        case Qt::Key_BracketLeft:
        case Qt::Key_Backslash:
        case Qt::Key_BracketRight:
        case Qt::Key_AsciiCircum:
        case Qt::Key_Underscore:
        case Qt::Key_QuoteLeft:
        case Qt::Key_BraceLeft:
        case Qt::Key_Bar:
        case Qt::Key_BraceRight:
        case Qt::Key_AsciiTilde:
        {
            QByteArray data = event->text().toUtf8();

            if((data == "\r") || (data == "\r\n") || (data == "\n"))
            {
                // emit writeBytes("\r\n");
                emit writeBytes("\r");
                emit writeBytes("\n");
            }
            else
            {
                emit writeBytes(data);
            }

            break;
        }
    }

    // Ensure we scroll also on Ctrl+Home or Ctrl+End

    if(event->matches(QKeySequence::MoveToStartOfDocument))
    {
        verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMinimum);
    }
    else if(event->matches(QKeySequence::MoveToEndOfDocument))
    {
        verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMaximum);
    }
}

void MyPlainTextEdit::wheelEvent(QWheelEvent *event)
{
    QPlainTextEdit::wheelEvent(event);

    if(event->modifiers() & Qt::ControlModifier) {
        Utils::FadingIndicator::showText(this, tr("Zoom: %1%").arg(int(100 * (font().pointSizeF() / TextEditor::TextEditorSettings::fontSettings().defaultFontSize()))), Utils::FadingIndicator::SmallText);
    }
}

void MyPlainTextEdit::resizeEvent(QResizeEvent *event)
{
    bool atBottom = verticalScrollBar()->value() == verticalScrollBar()->maximum();
    QPlainTextEdit::resizeEvent(event);

    if(atBottom)
    {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        // QPlainTextEdit destroys the first calls value in case of multiline
        // text, so make sure that the scroll bar actually gets the value set.
        // Is a noop if the first call succeeded.
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
}

void MyPlainTextEdit::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu;

    connect(menu.addAction(tr("Copy")), &QAction::triggered, this, [this] {
        copy();
    });

    connect(menu.addAction(tr("Paste")), &QAction::triggered, this, [this] {
        // Write bytes slowly so as to not overload the MicroPython board.

        QByteArray data = QApplication::clipboard()->text().toUtf8();

        for(int i = 0; i < data.size(); i += (TABOO_PACKET_SIZE - 1))
        {
            emit writeBytes(data.mid(i, TABOO_PACKET_SIZE - 1));
        }

        // emit writeBytes(QApplication::clipboard()->text().toUtf8());
    });

    menu.addSeparator();

    connect(menu.addAction(tr("Select All")), &QAction::triggered, this, [this] {
        selectAll();
    });

    menu.addSeparator();

    connect(menu.addAction(tr("Find")), &QAction::triggered, this, [this] {
        Core::ActionManager::command(Core::Constants::FIND_IN_DOCUMENT)->action()->trigger();
    });

    menu.exec(event->globalPos());
}

bool MyPlainTextEdit::focusNextPrevChild(bool next)
{
    Q_UNUSED(next)

    return false;
}

void MyPlainTextEdit::paintEvent(QPaintEvent *event)
{
    if(m_isCursorVisible)
    {
        QPainter painter(viewport());
        QRect r = cursorRect();
        r.setWidth(font().pointSize());
        painter.fillRect(r, Qt::white);
    }

    QPlainTextEdit::paintEvent(event);
}

OpenMVTerminal::OpenMVTerminal(const QString &displayName, QSettings *settings, const Core::Context &context, bool stand_alone, QWidget *parent) : QWidget(parent)
{
    setWindowTitle(displayName);
    setAttribute(Qt::WA_DeleteOnClose);

    m_settings = new QSettings(settings->fileName(), settings->format(), this);
    m_settings->beginGroup(QStringLiteral(TERMINAL_SETTINGS_GROUP));
    m_settings->beginGroup(displayName);

    ///////////////////////////////////////////////////////////////////////////

    Utils::StyledBar *styledBar0 = new Utils::StyledBar;
    QHBoxLayout *styledBar0Layout = new QHBoxLayout;
    styledBar0Layout->setContentsMargins(0, 0, 0, 0);
    styledBar0Layout->setSpacing(0);
    styledBar0Layout->addSpacing(4);
    styledBar0Layout->addWidget(new QLabel(tr("Frame Buffer")));
    styledBar0Layout->addSpacing(6);
    styledBar0->setLayout(styledBar0Layout);

    QToolButton *beginRecordingButton = new QToolButton;
    beginRecordingButton->setText(tr("Record"));
    beginRecordingButton->setToolTip(tr("Record the Frame Buffer"));
    beginRecordingButton->setEnabled(false);
    styledBar0Layout->addWidget(beginRecordingButton);

    QToolButton *endRecordingButton = new QToolButton;
    endRecordingButton->setText(tr("Stop"));
    endRecordingButton->setToolTip(tr("Stop recording"));
    endRecordingButton->setVisible(false);
    styledBar0Layout->addWidget(endRecordingButton);

    m_zoomButton = new QToolButton;
    m_zoomButton->setText(tr("Zoom"));
    m_zoomButton->setToolTip(tr("Zoom to fit"));
    m_zoomButton->setCheckable(true);
    styledBar0Layout->addWidget(m_zoomButton);

    Utils::ElidingLabel *recordingLabel = new Utils::ElidingLabel(tr("Elapsed: 0h:00m:00s:000ms - Size: 0 B - FPS: 0"));
    recordingLabel->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    recordingLabel->setStyleSheet(QStringLiteral("padding:4px;"));
    recordingLabel->setAlignment(Qt::AlignCenter);
    recordingLabel->setVisible(false);
    recordingLabel->setFont(TextEditor::TextEditorSettings::fontSettings().defaultFixedFontFamily());

    OpenMVPluginFB *frameBuffer = new OpenMVPluginFB;
    QWidget *tempWidget0 = new QWidget;
    QVBoxLayout *tempLayout0 = new QVBoxLayout;
    tempLayout0->setContentsMargins(0, 0, 0, 0);
    tempLayout0->setSpacing(0);
    tempLayout0->addWidget(styledBar0);
    tempLayout0->addWidget(frameBuffer);
    tempLayout0->addWidget(recordingLabel);
    tempWidget0->setLayout(tempLayout0);

    connect(m_zoomButton, &QToolButton::toggled, frameBuffer, &OpenMVPluginFB::enableFitInView);
    m_zoomButton->setChecked(m_settings->value(QStringLiteral(ZOOM_STATE), false).toBool());

    connect(frameBuffer, &OpenMVPluginFB::saveImage, this, [this] (const QPixmap &data) {
        QString path;

        forever
        {
            path =
            QFileDialog::getSaveFileName(this, tr("Save Image"),
                m_settings->value(QStringLiteral(LAST_SAVE_IMAGE_PATH), QDir::homePath()).toString(),
                tr("Image Files (*.bmp *.jpg *.jpeg *.png *.ppm)"));

            if((!path.isEmpty()) && QFileInfo(path).completeSuffix().isEmpty())
            {
                QMessageBox::warning(Core::ICore::dialogParent(),
                    tr("Save Image"),
                    QObject::tr("Please add a file extension!"));

                continue;
            }

            break;
        }

        if(!path.isEmpty())
        {
            if(data.save(path))
            {
                m_settings->setValue(QStringLiteral(LAST_SAVE_IMAGE_PATH), path);
            }
            else
            {
                QMessageBox::critical(this,
                    tr("Save Image"),
                    tr("Failed to save the image file for an unknown reason!"));
            }
        }
    });

    connect(frameBuffer, &OpenMVPluginFB::imageWriterTick, recordingLabel, &Utils::ElidingLabel::setText);

    connect(frameBuffer, &OpenMVPluginFB::pixmapUpdate, this, [this, beginRecordingButton] {
        beginRecordingButton->setEnabled(true);
    });

    connect(beginRecordingButton, &QToolButton::clicked, this, [this, beginRecordingButton, endRecordingButton, recordingLabel, frameBuffer] {
        if(frameBuffer->beginImageWriter())
        {
            beginRecordingButton->setVisible(false);
            endRecordingButton->setVisible(true);
            recordingLabel->setVisible(true);
        }
    });

    connect(endRecordingButton, &QToolButton::clicked, this, [this, beginRecordingButton, endRecordingButton, recordingLabel, frameBuffer] {
        frameBuffer->endImageWriter();
        beginRecordingButton->setVisible(true);
        endRecordingButton->setVisible(false);
        recordingLabel->setVisible(false);
    });

    connect(frameBuffer, &OpenMVPluginFB::imageWriterShutdown, this, [this, beginRecordingButton, endRecordingButton, recordingLabel] {
        beginRecordingButton->setVisible(true);
        endRecordingButton->setVisible(false);
        recordingLabel->setVisible(false);
    });

    Utils::StyledBar *styledBar1 = new Utils::StyledBar;
    QHBoxLayout *styledBar1Layout = new QHBoxLayout;
    styledBar1Layout->setContentsMargins(0, 0, 0, 0);
    styledBar1Layout->setSpacing(0);
    styledBar1Layout->addSpacing(4);
    styledBar1Layout->addWidget(new QLabel(tr("Histogram")));
    styledBar1Layout->addSpacing(6);
    styledBar1->setLayout(styledBar1Layout);

    m_colorSpace = new QComboBox;
    m_colorSpace->setProperty("hideborder", true);
    m_colorSpace->setProperty("drawleftborder", false);
    m_colorSpace->insertItem(RGB_COLOR_SPACE, tr("RGB Color Space"));
    m_colorSpace->insertItem(GRAYSCALE_COLOR_SPACE, tr("Grayscale Color Space"));
    m_colorSpace->insertItem(LAB_COLOR_SPACE, tr("LAB Color Space"));
    m_colorSpace->insertItem(YUV_COLOR_SPACE, tr("YUV Color Space"));
    m_colorSpace->setToolTip(tr("Use Grayscale/LAB for color tracking"));
    styledBar1Layout->addWidget(m_colorSpace);

    Utils::ElidingLabel *resLabel = new Utils::ElidingLabel(tr("Res - No Image"));
    resLabel->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    resLabel->setStyleSheet(QStringLiteral("padding:4px;"));
    resLabel->setAlignment(Qt::AlignCenter);

    OpenMVPluginHistogram *histogram = new OpenMVPluginHistogram;
    QWidget *tempWidget1 = new QWidget;
    QVBoxLayout *tempLayout1 = new QVBoxLayout;
    tempLayout1->setContentsMargins(0, 0, 0, 0);
    tempLayout1->setSpacing(0);
    tempLayout1->addWidget(styledBar1);
    tempLayout1->addWidget(resLabel);
    tempLayout1->addWidget(histogram);
    tempWidget1->setLayout(tempLayout1);

    connect(m_colorSpace, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), histogram, &OpenMVPluginHistogram::colorSpaceChanged);
    m_colorSpace->setCurrentIndex(m_settings->value(QStringLiteral(HISTOGRAM_COLOR_SPACE_STATE), RGB_COLOR_SPACE).toInt());

    connect(frameBuffer, &OpenMVPluginFB::pixmapUpdate, histogram, &OpenMVPluginHistogram::pixmapUpdate);

    connect(frameBuffer, &OpenMVPluginFB::resolutionAndROIUpdate, this, [this, resLabel] (const QSize &res, const QRect &roi) {
        if(res.isValid())
        {
            if(roi.isValid())
            {
                if((roi.width() > 1)
                || (roi.height() > 1))
                {
                    resLabel->setText(tr("Res (w:%1, h:%2) - ROI (x:%3, y:%4, w:%5, h:%6) - Pixels (%7)").arg(res.width()).arg(res.height()).arg(roi.x()).arg(roi.y()).arg(roi.width()).arg(roi.height()).arg(roi.width() * roi.height()));
                }
                else
                {
                    resLabel->setText(tr("Res (w:%1, h:%2) - Point (x:%3, y:%4)").arg(res.width()).arg(res.height()).arg(roi.x()).arg(roi.y()));
                }
            }
            else
            {
                resLabel->setText(tr("Res (w:%1, h:%2)").arg(res.width()).arg(res.height()));
            }
        }
        else
        {
            resLabel->setText(tr("Res - No Image"));
        }
    });

    ///////////////////////////////////////////////////////////////////////////

    Utils::StyledBar *styledBar2 = new Utils::StyledBar;
    QHBoxLayout *styledBar2Layout = new QHBoxLayout;
    styledBar2Layout->setContentsMargins(0, 0, 0, 0);
    styledBar2Layout->setSpacing(0);
    styledBar2Layout->addSpacing(5);
    styledBar2Layout->addWidget(new QLabel(tr("Serial Terminal")));
    styledBar2Layout->addSpacing(7);
    styledBar2Layout->addWidget(new Utils::StyledSeparator);
    styledBar2->setLayout(styledBar2Layout);

    QToolButton *clearButton = new QToolButton;
    clearButton->setIcon(Utils::Icons::CLEAN_TOOLBAR.icon());
    clearButton->setToolTip(tr("Clear"));
    styledBar2Layout->addWidget(clearButton);

    QToolButton *saveButton = new QToolButton;
    saveButton->setIcon(Utils::Icons::SAVEFILE_TOOLBAR.icon());
    saveButton->setToolTip(tr("Save"));
    styledBar2Layout->addWidget(saveButton);

    QToolButton *executeButton = new QToolButton;
    executeButton->setIcon(Utils::Icons::RUN_SMALL_TOOLBAR.icon());
    executeButton->setToolTip(stand_alone ? tr("Run \"/main.py\"") : tr("Run current script in editor window"));
    styledBar2Layout->addWidget(executeButton);
    if(!stand_alone) connect(Core::EditorManager::instance(), &Core::EditorManager::currentEditorChanged, executeButton, [executeButton] (Core::IEditor *editor) {
        executeButton->setEnabled(editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false);
    });

    QToolButton *interruptButton = new QToolButton;
    interruptButton->setIcon(Utils::Icons::STOP_SMALL_TOOLBAR.icon());
    interruptButton->setToolTip(tr("Stop running script"));
    styledBar2Layout->addWidget(interruptButton);

    QToolButton *reloadButton = new QToolButton;
    reloadButton->setIcon(Utils::Icons::RELOAD_TOOLBAR.icon());
    reloadButton->setToolTip(tr("Soft reset"));
    styledBar2Layout->addWidget(reloadButton);
    styledBar2Layout->addStretch(1);

    m_edit = new MyPlainTextEdit(m_settings->value(QStringLiteral(FONT_ZOOM_STATE), TextEditor::TextEditorSettings::fontSettings().defaultFontSize()).toReal());
    connect(this, &OpenMVTerminal::readBytes, m_edit, &MyPlainTextEdit::readBytes);
    connect(m_edit, &MyPlainTextEdit::writeBytes, this, &OpenMVTerminal::writeBytes);
    connect(m_edit, &MyPlainTextEdit::frameBufferData, frameBuffer, &OpenMVPluginFB::frameBufferData);
    connect(clearButton, &QToolButton::clicked, m_edit, &MyPlainTextEdit::clear);
    connect(saveButton, &QToolButton::clicked, m_edit, &MyPlainTextEdit::save);
    connect(executeButton, &QToolButton::clicked, this, [this, stand_alone] { m_edit->execute(stand_alone); });
    connect(interruptButton, &QToolButton::clicked, m_edit, &MyPlainTextEdit::interrupt);
    connect(reloadButton, &QToolButton::clicked, m_edit, &MyPlainTextEdit::reload);

    Aggregation::Aggregate *aggregate = new Aggregation::Aggregate;
    aggregate->add(m_edit);
    aggregate->add(new Core::BaseTextFind(m_edit));

    QWidget *tempWidget2 = new QWidget;
    QVBoxLayout *tempLayout2 = new QVBoxLayout;
    tempLayout2->setContentsMargins(0, 0, 0, 0);
    tempLayout2->setSpacing(0);
    tempLayout2->addWidget(styledBar2);
    tempLayout2->addWidget(m_edit);
    tempLayout2->addWidget(new Core::FindToolBarPlaceHolder(this));
    tempWidget2->setLayout(tempLayout2);

    QWidget *tempWidget3 = new QWidget;
    QVBoxLayout *tempLayout3 = new QVBoxLayout;
    tempLayout3->setContentsMargins(0, 0, 0, 0);
    tempLayout3->setSpacing(0);

    ///////////////////////////////////////////////////////////////////////////

    Utils::StyledBar *topBar = new Utils::StyledBar;
    topBar->setSingleRow(true);
    QHBoxLayout *topBarLayout = new QHBoxLayout;
    topBarLayout->setContentsMargins(0, 0, 0, 0);
    topBarLayout->setSpacing(0);
    m_topDrawer = new QToolButton;
    m_topDrawer->setArrowType(Qt::DownArrow);
    m_topDrawer->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    topBarLayout->addWidget(m_topDrawer);
    topBar->setLayout(topBarLayout);
    tempLayout3->addWidget(topBar);

    m_vsplitter = new Core::MiniSplitter(Qt::Vertical);
    m_vsplitter->insertWidget(0, tempWidget0);
    m_vsplitter->insertWidget(1, tempWidget1);
    m_vsplitter->setStretchFactor(0, 0);
    m_vsplitter->setStretchFactor(1, 1);
    m_vsplitter->setCollapsible(0, true);
    m_vsplitter->setCollapsible(1, true);
    tempLayout3->addWidget(m_vsplitter);

    Utils::StyledBar *bottomBar = new Utils::StyledBar;
    bottomBar->setSingleRow(true);
    QHBoxLayout *bottomBarLayout = new QHBoxLayout;
    bottomBarLayout->setContentsMargins(0, 0, 0, 0);
    bottomBarLayout->setSpacing(0);
    m_bottomDrawer = new QToolButton;
    m_bottomDrawer->setArrowType(Qt::UpArrow);
    m_bottomDrawer->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    bottomBarLayout->addWidget(m_bottomDrawer);
    bottomBar->setLayout(bottomBarLayout);
    tempLayout3->addWidget(bottomBar);

    tempWidget3->setLayout(tempLayout3);
    m_hsplitter = new Core::MiniSplitter(Qt::Horizontal);
    m_hsplitter->insertWidget(0, tempWidget2);
    m_hsplitter->insertWidget(1, tempWidget3);
    m_hsplitter->setStretchFactor(0, 1);
    m_hsplitter->setStretchFactor(1, 0);
    m_hsplitter->setCollapsible(0, true);
    m_hsplitter->setCollapsible(1, true);

    QHBoxLayout *layout = new QHBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);

    Utils::StyledBar *leftBar = new Utils::StyledBar;
    leftBar->setSingleRow(false);
    QVBoxLayout *leftBarLayout = new QVBoxLayout;
    leftBarLayout->setContentsMargins(0, 0, 0, 0);
    leftBarLayout->setSpacing(0);
    m_leftDrawer = new QToolButton;
    m_leftDrawer->setArrowType(Qt::RightArrow);
    m_leftDrawer->setMinimumHeight(160);
    leftBarLayout->addSpacing(22);
    leftBarLayout->addWidget(m_leftDrawer);
    leftBarLayout->addSpacing(160);
    leftBar->setLayout(leftBarLayout);
    layout->addWidget(leftBar);

    layout->addWidget(m_hsplitter);

    Utils::StyledBar *rightBar = new Utils::StyledBar;
    rightBar->setSingleRow(false);
    QVBoxLayout *rightBarLayout = new QVBoxLayout;
    rightBarLayout->setContentsMargins(0, 0, 0, 0);
    rightBarLayout->setSpacing(0);
    m_rightDrawer = new QToolButton;
    m_rightDrawer->setArrowType(Qt::LeftArrow);
    m_rightDrawer->setMinimumHeight(160);
    rightBarLayout->addSpacing(22);
    rightBarLayout->addWidget(m_rightDrawer);
    rightBarLayout->addSpacing(160);
    rightBar->setLayout(rightBarLayout);
    layout->addWidget(rightBar);

    setLayout(layout);

    connect(m_leftDrawer, &QToolButton::clicked, this, [this] {
        m_hsplitter->setSizes(QList<int>() << 1 << m_hsplitter->sizes().at(1));
        m_leftDrawer->parentWidget()->hide();
    });

    connect(m_hsplitter, &Core::MiniSplitter::splitterMoved, this, [this] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) m_leftDrawer->parentWidget()->setVisible(!m_hsplitter->sizes().at(0));
    });

    connect(m_rightDrawer, &QToolButton::clicked, this, [this] {
        m_hsplitter->setSizes(QList<int>() << m_hsplitter->sizes().at(0) << 1);
        m_rightDrawer->parentWidget()->hide();
    });

    connect(m_hsplitter, &Core::MiniSplitter::splitterMoved, this, [this] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) m_rightDrawer->parentWidget()->setVisible(!m_hsplitter->sizes().at(1));
    });

    connect(m_topDrawer, &QToolButton::clicked, this, [this] {
        m_vsplitter->setSizes(QList<int>() << 1 <<  m_vsplitter->sizes().at(1));
        m_topDrawer->parentWidget()->hide();
    });

    connect(m_vsplitter, &Core::MiniSplitter::splitterMoved, this, [this] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) m_topDrawer->parentWidget()->setVisible(!m_vsplitter->sizes().at(0));
    });

    connect(m_bottomDrawer, &QToolButton::clicked, this, [this] {
        m_vsplitter->setSizes(QList<int>() << m_vsplitter->sizes().at(0) << 1);
        m_bottomDrawer->parentWidget()->hide();
    });

    connect(m_vsplitter, &Core::MiniSplitter::splitterMoved, this, [this] (int pos, int index) {
        Q_UNUSED(pos) Q_UNUSED(index) m_bottomDrawer->parentWidget()->setVisible(!m_vsplitter->sizes().at(1));
    });

    ///////////////////////////////////////////////////////////////////////////

    m_styleSheet = QString(QStringLiteral("QAbstractScrollArea::corner{background-color:%2;}"
#ifndef Q_OS_MAC
    "QScrollBar:vertical{margin-top:17px;margin-right:0px;margin-bottom:17px;margin-left:0px;min-width:14px;background-color:%2;}"
    "QScrollBar::sub-line:vertical{subcontrol-origin:margin;subcontrol-position:top;height:17px;background-color:%2;border-top:1px solid %3;}"
    "QScrollBar::add-line:vertical{subcontrol-origin:margin;subcontrol-position:bottom;height:17px;background-color:%2;}"
    "QScrollBar::up-arrow:vertical{margin-left:1px;margin-right:1px;border-image:url(:/core/images/scroll-arrowup.png);}"
    "QScrollBar::down-arrow:vertical{margin-left:1px;margin-right:1px;border-image:url(:/core/images/scroll-arrowdown.png);}"
    "QScrollBar::sub-page:vertical{margin-left:1px;margin-right:1px;background-color:%2;}"
    "QScrollBar::add-page:vertical{margin-left:1px;margin-right:1px;background-color:%2;}"
    "QScrollBar::handle:vertical{margin-left:1px;margin-right:1px;min-height:20px;background-color:%3;}"
    "QScrollBar:horizontal{margin-top:0px;margin-right:17px;margin-bottom:0px;margin-left:17px;min-height:14px;background-color:%2;}"
    "QScrollBar::sub-line:horizontal{subcontrol-origin:margin;subcontrol-position:left;width:17px;background-color:%2;}"
    "QScrollBar::add-line:horizontal{subcontrol-origin:margin;subcontrol-position:right;width:17px;background-color:%2;}"
    "QScrollBar::left-arrow:horizontal{margin-top:1px;margin-bottom:1px;border-image:url(:/core/images/scroll-arrowleft.png);}"
    "QScrollBar::right-arrow:horizontal{margin-top:1px;margin-bottom:1px;border-image:url(:/core/images/scroll-arrowright.png);}"
    "QScrollBar::sub-page:horizontal{margin-top:1px;margin-bottom:1px;background-color:%2;}"
    "QScrollBar::add-page:horizontal{margin-top:1px;margin-bottom:1px;background-color:%2;}"
    "QScrollBar::handle:horizontal{margin-top:1px;margin-bottom:1px;min-width:20px;background-color:%3;}"
    "QScrollBar::up-arrow:hover,QScrollBar::right-arrow:hover,QScrollBar::down-arrow:hover,QScrollBar::left-arrow:hover,QScrollBar::handle:hover{background-color:%4;}"
    "QScrollBar::up-arrow:pressed,QScrollBar::right-arrow:pressed,QScrollBar::down-arrow:pressed,QScrollBar::left-arrow:pressed,QScrollBar::handle:pressed{background-color:%5;}"
#endif
    "QPlainTextEdit{background-color:%1;}")).
            arg(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal).name()).
            arg(Utils::creatorTheme()->color(Utils::Theme::DSscrollBarHandle).name()).
            arg(Utils::creatorTheme()->color(Utils::Theme::DSscrollBarTrack).name()).
            arg(Utils::creatorTheme()->color(Utils::Theme::DSsliderHandleHover).name()).
            arg(Utils::creatorTheme()->color(Utils::Theme::DSsliderHandleFocus).name());

    m_highDPIStyleSheet = QStringLiteral("QAbstractScrollArea::corner{background-color:%2;}"
#ifndef Q_OS_MAC
    "QScrollBar:vertical{margin-top:17px;margin-right:0px;margin-bottom:17px;margin-left:0px;min-width:14px;background-color:%2;}"
    "QScrollBar::sub-line:vertical{subcontrol-origin:margin;subcontrol-position:top;height:17px;background-color:%2;border-top:1px solid %3;}"
    "QScrollBar::add-line:vertical{subcontrol-origin:margin;subcontrol-position:bottom;height:17px;background-color:%2;}"
    "QScrollBar::up-arrow:vertical{margin-left:1px;margin-right:1px;border-image:url(:/core/images/scroll-arrowup_2x.png);}"
    "QScrollBar::down-arrow:vertical{margin-left:1px;margin-right:1px;border-image:url(:/core/images/scroll-arrowdown_2x.png);}"
    "QScrollBar::sub-page:vertical{margin-left:1px;margin-right:1px;background-color:%2;}"
    "QScrollBar::add-page:vertical{margin-left:1px;margin-right:1px;background-color:%2;}"
    "QScrollBar::handle:vertical{margin-left:1px;margin-right:1px;min-height:20px;background-color:%3;}"
    "QScrollBar:horizontal{margin-top:0px;margin-right:17px;margin-bottom:0px;margin-left:17px;min-height:14px;background-color:%2;}"
    "QScrollBar::sub-line:horizontal{subcontrol-origin:margin;subcontrol-position:left;width:17px;background-color:%2;}"
    "QScrollBar::add-line:horizontal{subcontrol-origin:margin;subcontrol-position:right;width:17px;background-color:%2;}"
    "QScrollBar::left-arrow:horizontal{margin-top:1px;margin-bottom:1px;border-image:url(:/core/images/scroll-arrowleft_2x.png);}"
    "QScrollBar::right-arrow:horizontal{margin-top:1px;margin-bottom:1px;border-image:url(:/core/images/scroll-arrowright_2x.png);}"
    "QScrollBar::sub-page:horizontal{margin-top:1px;margin-bottom:1px;background-color:%2;}"
    "QScrollBar::add-page:horizontal{margin-top:1px;margin-bottom:1px;background-color:%2;}"
    "QScrollBar::handle:horizontal{margin-top:1px;margin-bottom:1px;min-width:20px;background-color:%3;}"
    "QScrollBar::up-arrow:hover,QScrollBar::right-arrow:hover,QScrollBar::down-arrow:hover,QScrollBar::left-arrow:hover,QScrollBar::handle:hover{background-color:%4;}"
    "QScrollBar::up-arrow:pressed,QScrollBar::right-arrow:pressed,QScrollBar::down-arrow:pressed,QScrollBar::left-arrow:pressed,QScrollBar::handle:pressed{background-color:%5;}"
#endif
    "QPlainTextEdit{background-color:#%1;}").
            arg(Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal).name()).
            arg(Utils::creatorTheme()->color(Utils::Theme::DSscrollBarHandle).name()).
            arg(Utils::creatorTheme()->color(Utils::Theme::DSscrollBarTrack).name()).
            arg(Utils::creatorTheme()->color(Utils::Theme::DSsliderHandleHover).name()).
            arg(Utils::creatorTheme()->color(Utils::Theme::DSsliderHandleFocus).name());

    QPalette pal = palette();
    pal.setColor(QPalette::Base, Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal));
    setAutoFillBackground(true);
    setPalette(pal);

    m_devicePixelRatio = 0;

    m_context = new Core::IContext(this);
    m_context->setContext(context);
    m_context->setWidget(this);

    Core::ICore::addContextObject(m_context);

    Core::Command *overrideCtrlE = Core::ActionManager::registerAction(new QAction(QString(), Q_NULLPTR), Utils::Id("OpenMV.Terminal.Ctrl.E"), context);
    overrideCtrlE->setDefaultKeySequence(QStringLiteral("Ctrl+E"));

    Core::Command *overrideCtrlR = Core::ActionManager::registerAction(new QAction(QString(), Q_NULLPTR), Utils::Id("OpenMV.Terminal.Ctrl.R"), context);
    overrideCtrlR->setDefaultKeySequence(QStringLiteral("Ctrl+R"));
}

OpenMVTerminal::~OpenMVTerminal()
{
    Core::ICore::removeContextObject(m_context);
}

void OpenMVTerminal::showEvent(QShowEvent *event)
{
    if(m_settings->contains(QStringLiteral(GEOMETRY)))
    {
        restoreGeometry(m_settings->value(QStringLiteral(GEOMETRY)).toByteArray());
    }

    if(m_settings->contains(QStringLiteral(HSPLITTER_STATE)))
    {
        m_hsplitter->restoreState(m_settings->value(QStringLiteral(HSPLITTER_STATE)).toByteArray());
    }

    if(m_settings->contains(QStringLiteral(VSPLITTER_STATE)))
    {
        m_vsplitter->restoreState(m_settings->value(QStringLiteral(VSPLITTER_STATE)).toByteArray());
    }

    m_leftDrawer->parentWidget()->setVisible(m_settings->contains(QStringLiteral(HSPLITTER_STATE)) ? (!m_hsplitter->sizes().at(0)) : false);
    m_rightDrawer->parentWidget()->setVisible(m_settings->contains(QStringLiteral(HSPLITTER_STATE)) ? (!m_hsplitter->sizes().at(1)) : false);
    m_topDrawer->parentWidget()->setVisible(m_settings->contains(QStringLiteral(VSPLITTER_STATE)) ? (!m_vsplitter->sizes().at(0)) : false);
    m_bottomDrawer->parentWidget()->setVisible(m_settings->contains(QStringLiteral(VSPLITTER_STATE)) ? (!m_vsplitter->sizes().at(1)) : false);

    QWidget::showEvent(event);
}

void OpenMVTerminal::closeEvent(QCloseEvent *event)
{
    m_settings->setValue(QStringLiteral(GEOMETRY), saveGeometry());
    m_settings->setValue(QStringLiteral(HSPLITTER_STATE), m_hsplitter->saveState());
    m_settings->setValue(QStringLiteral(VSPLITTER_STATE), m_vsplitter->saveState());
    m_settings->setValue(QStringLiteral(ZOOM_STATE), m_zoomButton->isChecked());
    m_settings->setValue(QStringLiteral(FONT_ZOOM_STATE), m_edit->font().pointSizeF());
    m_settings->setValue(QStringLiteral(HISTOGRAM_COLOR_SPACE_STATE), m_colorSpace->currentIndex());

    QWidget::closeEvent(event);
}

void OpenMVTerminal::paintEvent(QPaintEvent *event)
{
    // We have to do this because Qt does not update the icons when switching between
    // a non-high dpi screen and a high-dpi screen.
    qreal ratio = devicePixelRatioF();
    if (!qFuzzyCompare(ratio, m_devicePixelRatio))
    {
        m_devicePixelRatio = ratio;
        setStyleSheet(qFuzzyCompare(1.0, ratio) ? m_styleSheet : m_highDPIStyleSheet); // reload icons
    }

    QWidget::paintEvent(event);
}

OpenMVTerminalSerialPort_private::OpenMVTerminalSerialPort_private(QObject *parent) : QObject(parent)
{
    m_port = Q_NULLPTR;
}

void OpenMVTerminalSerialPort_private::open(const QString &portName, int buadRate)
{
    if(m_port)
    {
        delete m_port;
    }

    m_port = new QSerialPort(portName, this);
    // QSerialPort is buggy unless this is set.
    m_port->setReadBufferSize(1000000);

    connect(m_port, &QSerialPort::readyRead, this, [this] {
        emit readBytes(m_port->readAll());
    });

    if((!m_port->setBaudRate(buadRate))
    || (!m_port->open(QIODevice::ReadWrite))
    || (!m_port->setDataTerminalReady(true)))
    {
        emit openResult(m_port->errorString());
        delete m_port;
        m_port = Q_NULLPTR;
    }
    else
    {
        emit openResult(QString());
    }
}

void OpenMVTerminalSerialPort_private::writeBytes(const QByteArray &data)
{
    if(m_port)
    {
        m_port->clearError();

        if((m_port->write(data) != data.size()) || (!m_port->flush()))
        {
            delete m_port;
            m_port = Q_NULLPTR;
        }
        else
        {
            QThread::msleep(1);
        }
    }
}

OpenMVTerminalSerialPort::OpenMVTerminalSerialPort(QObject *parent) : OpenMVTerminalPort(parent)
{
    QThread *thread = new QThread;
    OpenMVTerminalSerialPort_private* port = new OpenMVTerminalSerialPort_private;
    port->moveToThread(thread);

    connect(this, &OpenMVTerminalSerialPort::open,
            port, &OpenMVTerminalSerialPort_private::open);

    connect(port, &OpenMVTerminalSerialPort_private::openResult,
            this, &OpenMVTerminalSerialPort::openResult);

    connect(this, &OpenMVTerminalSerialPort::writeBytes,
            port, &OpenMVTerminalSerialPort_private::writeBytes);

    connect(port, &OpenMVTerminalSerialPort_private::readBytes,
            this, &OpenMVTerminalSerialPort::readBytes);

    connect(this, &OpenMVTerminalSerialPort::destroyed,
            port, &OpenMVTerminalSerialPort_private::deleteLater);

    connect(port, &OpenMVTerminalSerialPort_private::destroyed,
            thread, &QThread::quit);

    connect(thread, &QThread::finished,
            thread, &QThread::deleteLater);

    thread->start();
}

OpenMVTerminalUDPPort_private::OpenMVTerminalUDPPort_private(QObject *parent) : QObject(parent)
{
    m_port = Q_NULLPTR;
}

void OpenMVTerminalUDPPort_private::open(const QString &hostName, int port)
{
    if(m_port)
    {
        delete m_port;
    }

    m_port = new QUdpSocket(this);

    connect(m_port, &QSerialPort::readyRead, this, [this] {
        emit readBytes(m_port->readAll());
    });

    if(!hostName.isEmpty())
    {
        m_port->connectToHost(hostName, port);

        if(!m_port->waitForConnected())
        {
            emit openResult(m_port->errorString());
            delete m_port;
            m_port = Q_NULLPTR;
        }
        else
        {
            emit openResult(QString());
        }
    }
    else
    {
        if(!m_port->bind(port))
        {
            emit openResult(m_port->errorString());
            delete m_port;
            m_port = Q_NULLPTR;
        }
        else
        {
            if(m_port->localAddress() != QHostAddress::Any)
            {
                emit openResult(QString(QStringLiteral("OPENMV::%1:%2")).arg(m_port->localAddress().toString()).arg(m_port->localPort()));
            }
            else
            {
                QStringList addresses;

                foreach(const QNetworkInterface &interface, QNetworkInterface::allInterfaces())
                {
                    if(interface.flags().testFlag(QNetworkInterface::IsUp)
                    && interface.flags().testFlag(QNetworkInterface::IsRunning)
                    && interface.flags().testFlag(QNetworkInterface::CanBroadcast)
                    && (!interface.flags().testFlag(QNetworkInterface::IsLoopBack))
                    && (!interface.flags().testFlag(QNetworkInterface::IsPointToPoint))
                    && interface.flags().testFlag(QNetworkInterface::CanMulticast))
                    {
                        foreach(const QNetworkAddressEntry &entry, interface.addressEntries())
                        {
                            if((!entry.broadcast().isNull())
                            && (!entry.ip().isNull())
                            && (!entry.netmask().isNull())
                            && (!entry.ip().toString().endsWith(QStringLiteral(".1")))) // gateway
                            {
                                addresses.append(entry.ip().toString());
                            }
                        }
                    }
                }

                if(!addresses.isEmpty())
                {
                    emit openResult(QString(QStringLiteral("OPENMV::%1:%2")).arg(addresses.first()).arg(m_port->localPort()));
                }
                else
                {
                    emit openResult(QString(QStringLiteral("OPENMV::%1")).arg(m_port->localPort()));
                }
            }
        }
    }
}

void OpenMVTerminalUDPPort_private::writeBytes(const QByteArray &data)
{
    if(m_port)
    {
        if((m_port->write(data) != data.size()) || (!m_port->flush()))
        {
            delete m_port;
            m_port = Q_NULLPTR;
        }
        else
        {
            QThread::msleep(1);
        }
    }
}

OpenMVTerminalUDPPort::OpenMVTerminalUDPPort(QObject *parent) : OpenMVTerminalPort(parent)
{
    QThread *thread = new QThread;
    OpenMVTerminalUDPPort_private* port = new OpenMVTerminalUDPPort_private;
    port->moveToThread(thread);

    connect(this, &OpenMVTerminalUDPPort::open,
            port, &OpenMVTerminalUDPPort_private::open);

    connect(port, &OpenMVTerminalUDPPort_private::openResult,
            this, &OpenMVTerminalUDPPort::openResult);

    connect(this, &OpenMVTerminalUDPPort::writeBytes,
            port, &OpenMVTerminalUDPPort_private::writeBytes);

    connect(port, &OpenMVTerminalUDPPort_private::readBytes,
            this, &OpenMVTerminalUDPPort::readBytes);

    connect(this, &OpenMVTerminalUDPPort::destroyed,
            port, &OpenMVTerminalUDPPort_private::deleteLater);

    connect(port, &OpenMVTerminalUDPPort_private::destroyed,
            thread, &QThread::quit);

    connect(thread, &QThread::finished,
            thread, &QThread::deleteLater);

    thread->start();
}

OpenMVTerminalTCPPort_private::OpenMVTerminalTCPPort_private(QObject *parent) : QObject(parent)
{
    m_port = Q_NULLPTR;
}

void OpenMVTerminalTCPPort_private::open(const QString &hostName, int port)
{
    if(m_port)
    {
        delete m_port;
    }

    m_port = new QTcpSocket(this);

    connect(m_port, &QSerialPort::readyRead, this, [this] {
        emit readBytes(m_port->readAll());
    });

    if(!hostName.isEmpty())
    {
        m_port->connectToHost(hostName, port);

        if(!m_port->waitForConnected())
        {
            emit openResult(m_port->errorString());
            delete m_port;
            m_port = Q_NULLPTR;
        }
        else
        {
            emit openResult(QString());
        }
    }
    else
    {
        if(!m_port->bind(port))
        {
            emit openResult(m_port->errorString());
            delete m_port;
            m_port = Q_NULLPTR;
        }
        else
        {
            if(m_port->localAddress() != QHostAddress::Any)
            {
                emit openResult(QString(QStringLiteral("OPENMV::%1:%2")).arg(m_port->localAddress().toString()).arg(m_port->localPort()));
            }
            else
            {
                QStringList addresses;

                foreach(const QNetworkInterface &interface, QNetworkInterface::allInterfaces())
                {
                    if(interface.flags().testFlag(QNetworkInterface::IsUp)
                    && interface.flags().testFlag(QNetworkInterface::IsRunning)
                    && interface.flags().testFlag(QNetworkInterface::CanBroadcast)
                    && (!interface.flags().testFlag(QNetworkInterface::IsLoopBack))
                    && (!interface.flags().testFlag(QNetworkInterface::IsPointToPoint))
                    && interface.flags().testFlag(QNetworkInterface::CanMulticast))
                    {
                        foreach(const QNetworkAddressEntry &entry, interface.addressEntries())
                        {
                            if((!entry.broadcast().isNull())
                            && (!entry.ip().isNull())
                            && (!entry.netmask().isNull())
                            && (!entry.ip().toString().endsWith(QStringLiteral(".1")))) // gateway
                            {
                                addresses.append(entry.ip().toString());
                            }
                        }
                    }
                }

                if(!addresses.isEmpty())
                {
                    emit openResult(QString(QStringLiteral("OPENMV::%1:%2")).arg(addresses.first()).arg(m_port->localPort()));
                }
                else
                {
                    emit openResult(QString(QStringLiteral("OPENMV::%1")).arg(m_port->localPort()));
                }
            }
        }
    }
}

void OpenMVTerminalTCPPort_private::writeBytes(const QByteArray &data)
{
    if(m_port)
    {
        if((m_port->write(data) != data.size()) || (!m_port->flush()))
        {
            delete m_port;
            m_port = Q_NULLPTR;
        }
        else
        {
            QThread::msleep(1);
        }
    }
}

OpenMVTerminalTCPPort::OpenMVTerminalTCPPort(QObject *parent) : OpenMVTerminalPort(parent)
{
    QThread *thread = new QThread;
    OpenMVTerminalTCPPort_private* port = new OpenMVTerminalTCPPort_private;
    port->moveToThread(thread);

    connect(this, &OpenMVTerminalTCPPort::open,
            port, &OpenMVTerminalTCPPort_private::open);

    connect(port, &OpenMVTerminalTCPPort_private::openResult,
            this, &OpenMVTerminalTCPPort::openResult);

    connect(this, &OpenMVTerminalTCPPort::writeBytes,
            port, &OpenMVTerminalTCPPort_private::writeBytes);

    connect(port, &OpenMVTerminalTCPPort_private::readBytes,
            this, &OpenMVTerminalTCPPort::readBytes);

    connect(this, &OpenMVTerminalTCPPort::destroyed,
            port, &OpenMVTerminalTCPPort_private::deleteLater);

    connect(port, &OpenMVTerminalTCPPort_private::destroyed,
            thread, &QThread::quit);

    connect(thread, &QThread::finished,
            thread, &QThread::deleteLater);

    thread->start();
}
