// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textindenter.h"

#include <QTextDocument>
#include <QTextCursor>

// OPENMV-DIFF //
#include <QStack>
// OPENMV-DIFF //

using namespace TextEditor;

TextIndenter::TextIndenter(QTextDocument *doc)
    : Indenter(doc)
{}

TextIndenter::~TextIndenter() = default;

// Indent a text block based on previous line.
// Simple text paragraph layout:
// aaaa aaaa
//
//   bbb bb
//   bbb bb
//
//  - list
//    list line2
//
//  - listn
//
// ccc
//
// @todo{Add formatting to wrap paragraphs. This requires some
// hoops as the current indentation routines are not prepared
// for additional block being inserted. It might be possible
// to do in 2 steps (indenting/wrapping)}
int TextIndenter::indentFor(const QTextBlock &block,
                            const TabSettings &tabSettings,
                            int cursorPositionInEditor)
{
    Q_UNUSED(tabSettings)
    Q_UNUSED(cursorPositionInEditor)

    QTextBlock previous = block.previous();
    if (!previous.isValid())
        return 0;

    const QString previousText = previous.text();
    // Empty line indicates a start of a new paragraph. Leave as is.
    // OPENMV-DIFF //
    // if (previousText.isEmpty() || previousText.trimmed().isEmpty())
    //     return 0;
    // OPENMV-DIFF //
    if(!previousText.isEmpty())
    {
        enum
        {
            IN_PUSH_0, // (
            IN_PUSH_1, // [
            IN_PUSH_2, // {
            IN_COLON
        };

        QStack<QPair<int, int> > in_stack;

        enum
        {
            IN_NONE,
            IN_COMMENT,
            IN_STRING_0,
            IN_STRING_1
        }
        in_state = IN_NONE;

        for(int i = 0; i < previousText.size(); i++)
        {
            switch(in_state)
            {
                case IN_NONE:
                {
                    if((previousText.at(i) == QLatin1Char('#')) && ((!i) || (previousText.at(i-1) != QLatin1Char('\\')))) in_state = IN_COMMENT;
                    if((previousText.at(i) == QLatin1Char('\'')) && ((!i) || (previousText.at(i-1) != QLatin1Char('\\')))) in_state = IN_STRING_0;
                    if((previousText.at(i) == QLatin1Char('\"')) && ((!i) || (previousText.at(i-1) != QLatin1Char('\\')))) in_state = IN_STRING_1;
                    if(previousText.at(i) == QLatin1Char('(')) in_stack.push(QPair<int, int>(IN_PUSH_0, i));
                    if(previousText.at(i) == QLatin1Char('[')) in_stack.push(QPair<int, int>(IN_PUSH_1, i));
                    if(previousText.at(i) == QLatin1Char('{')) in_stack.push(QPair<int, int>(IN_PUSH_2, i));
                    if(previousText.at(i) == QLatin1Char(')')) while(in_stack.size() && (in_stack.pop().first != IN_PUSH_0));
                    if(previousText.at(i) == QLatin1Char(']')) while(in_stack.size() && (in_stack.pop().first != IN_PUSH_1));
                    if(previousText.at(i) == QLatin1Char('}')) while(in_stack.size() && (in_stack.pop().first != IN_PUSH_2));
                    if(previousText.at(i) == QLatin1Char(':')) in_stack.push(QPair<int, int>(IN_COLON, i));
                    break;
                }
                case IN_COMMENT:
                {
                    if((previousText.at(i) == QLatin1Char('\n')) && (previousText.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                    break;
                }
                case IN_STRING_0:
                {
                    if((previousText.at(i) == QLatin1Char('\'')) && (previousText.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                    break;
                }
                case IN_STRING_1:
                {
                    if((previousText.at(i) == QLatin1Char('\"')) && (previousText.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                    break;
                }
            }
        }

        if((in_state != IN_NONE) && (in_state != IN_COMMENT)) return tabSettings.indentationColumn(previousText);

        if(in_stack.size() == 1)
        {
            if(in_stack.top().first == IN_COLON) return tabSettings.indentationColumn(previousText) + tabSettings.m_tabSize;

            if((in_stack.top().first == IN_PUSH_0)
            || (in_stack.top().first == IN_PUSH_1)
            || (in_stack.top().first == IN_PUSH_2)) return tabSettings.columnAt(previousText, in_stack.top().second + 1);
        }
    }
    // OPENMV-DIFF //

    return tabSettings.indentationColumn(previousText);
}

IndentationForBlock TextIndenter::indentationForBlocks(const QVector<QTextBlock> &blocks,
                                                       const TabSettings &tabSettings,
                                                       int /*cursorPositionInEditor*/)
{
    IndentationForBlock ret;
    for (const QTextBlock &block : blocks)
        ret.insert(block.blockNumber(), indentFor(block, tabSettings));
    return ret;
}

void TextIndenter::indentBlock(const QTextBlock &block,
                               const QChar &typedChar,
                               const TabSettings &tabSettings,
                               int /*cursorPositionInEditor*/)
{
    Q_UNUSED(typedChar)
    const int indent = indentFor(block, tabSettings);
    if (indent < 0)
        return;
    tabSettings.indentLine(block, indent);
}

void TextIndenter::indent(const QTextCursor &cursor,
                          const QChar &typedChar,
                          const TabSettings &tabSettings,
                          int /*cursorPositionInEditor*/)
{
    if (cursor.hasSelection()) {
        QTextBlock block = m_doc->findBlock(cursor.selectionStart());
        const QTextBlock end = m_doc->findBlock(cursor.selectionEnd()).next();
        do {
            indentBlock(block, typedChar, tabSettings);
            block = block.next();
        } while (block.isValid() && block != end);
    } else {
        indentBlock(cursor.block(), typedChar, tabSettings);
    }
}

void TextIndenter::reindent(const QTextCursor &cursor,
                            const TabSettings &tabSettings,
                            int /*cursorPositionInEditor*/)
{
    if (cursor.hasSelection()) {
        QTextBlock block = m_doc->findBlock(cursor.selectionStart());
        const QTextBlock end = m_doc->findBlock(cursor.selectionEnd()).next();

        // skip empty blocks
        while (block.isValid() && block != end) {
            QString bt = block.text();
            if (TabSettings::firstNonSpace(bt) < bt.size())
                break;
            indentBlock(block, QChar::Null, tabSettings);
            block = block.next();
        }

        int previousIndentation = tabSettings.indentationColumn(block.text());
        indentBlock(block, QChar::Null, tabSettings);
        int currentIndentation = tabSettings.indentationColumn(block.text());
        int delta = currentIndentation - previousIndentation;

        block = block.next();
        while (block.isValid() && block != end) {
            tabSettings.reindentLine(block, delta);
            block = block.next();
        }
    } else {
        indentBlock(cursor.block(), QChar::Null, tabSettings);
    }
}

std::optional<TabSettings> TextIndenter::tabSettings() const
{
    return std::optional<TabSettings>();
}
