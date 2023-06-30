// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "keywordscompletionassist.h"

#include <coreplugin/coreconstants.h>
#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/functionhintproposal.h>
#include <texteditor/codeassist/genericproposalmodel.h>
#include <texteditor/completionsettings.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/texteditorconstants.h>
#include <texteditor/texteditor.h>

#include <QDir>
#include <QFileInfo>

#include <utils/algorithm.h>
#include <utils/utilsicons.h>

// OPENMV-DIFF //
#include <QRegularExpression>
#include <QStack>
// OPENMV-DIFF //

namespace TextEditor {

// --------------------------
// Keywords
// --------------------------
// Note: variables and functions must be sorted
// OPENMV-DIFF //
// Keywords::Keywords(const QStringList &variables, const QStringList &functions, const QMap<QString, QStringList> &functionArgs)
// OPENMV-DIFF //
Keywords::Keywords(const QStringList &variables,
                   const QStringList &classes, const QMap<QString, QStringList> &classArgs,
                   const QStringList &functions, const QMap<QString, QStringList> &functionArgs,
                   const QStringList &methods, const QMap<QString, QStringList> &methodArgs)
// OPENMV-DIFF //
    : m_variables(Utils::sorted(variables)),
      // OPENMV-DIFF //
      m_classes(Utils::sorted(classes)),
      m_classArgs(classArgs),
      m_methods(Utils::sorted(methods)),
      m_methodArgs(methodArgs),
      // OPENMV-DIFF //
      m_functions(Utils::sorted(functions)),
      m_functionArgs(functionArgs)
{
    // OPENMV-DIFF //
    publicRegex = QRegularExpression("^[^_].+$");
    protectedRegex = QRegularExpression("^_[^_].+$");
    privateRegex = QRegularExpression("^__[^_].+$");
    // OPENMV-DIFF //
}

bool Keywords::isVariable(const QString &word) const
{
    return std::binary_search(m_variables.constBegin(), m_variables.constEnd(), word);
}

bool Keywords::isFunction(const QString &word) const
{
    return std::binary_search(m_functions.constBegin(), m_functions.constEnd(), word);
}
// OPENMV-DIFF //
// QStringList Keywords::variables() const
// OPENMV-DIFF //
QStringList Keywords::variables(KeywordsScope_t scope) const
// OPENMV-DIFF //
{
    // OPENMV-DIFF //
    if (scope == KEYWORDS_SCOPE_PUBLIC) return m_variables.filter(publicRegex);
    if (scope == KEYWORDS_SCOPE_PROTECTED) return m_variables.filter(protectedRegex);
    if (scope == KEYWORDS_SCOPE_PRIVATE) return m_variables.filter(privateRegex);
    // OPENMV-DIFF //
    return m_variables;
}

// OPENMV-DIFF //
// QStringList Keywords::functions() const
// OPENMV-DIFF //
QStringList Keywords::functions(KeywordsScope_t scope) const
// OPENMV-DIFF //
{
    // OPENMV-DIFF //
    if (scope == KEYWORDS_SCOPE_PUBLIC) return m_functions.filter(publicRegex);
    if (scope == KEYWORDS_SCOPE_PROTECTED) return m_functions.filter(protectedRegex);
    if (scope == KEYWORDS_SCOPE_PRIVATE) return m_functions.filter(privateRegex);
    // OPENMV-DIFF //
    return m_functions;
}

QStringList Keywords::argsForFunction(const QString &function) const
{
    return m_functionArgs.value(function);
}

// OPENMV-DIFF //
bool Keywords::isClass(const QString &word) const
{
    return std::binary_search(m_classes.constBegin(), m_classes.constEnd(), word);
}

bool Keywords::isMethod(const QString &word) const
{
    return std::binary_search(m_methods.constBegin(), m_methods.constEnd(), word);
}

QStringList Keywords::classes(KeywordsScope_t scope) const
{
    if (scope == KEYWORDS_SCOPE_PUBLIC) return m_classes.filter(publicRegex);
    if (scope == KEYWORDS_SCOPE_PROTECTED) return m_classes.filter(protectedRegex);
    if (scope == KEYWORDS_SCOPE_PRIVATE) return m_classes.filter(privateRegex);
    return m_classes;
}

QStringList Keywords::argsForClass(const QString &className) const
{
    return m_classArgs.value(className);
}

QStringList Keywords::methods(KeywordsScope_t scope) const
{
    if (scope == KEYWORDS_SCOPE_PUBLIC) return m_methods.filter(publicRegex);
    if (scope == KEYWORDS_SCOPE_PROTECTED) return m_methods.filter(protectedRegex);
    if (scope == KEYWORDS_SCOPE_PRIVATE) return m_methods.filter(privateRegex);
    return m_methods;
}

QStringList Keywords::argsForMethod(const QString &methodName) const
{
    return m_methodArgs.value(methodName);
}
// OPENMV-DIFF //

// --------------------------
// KeywordsAssistProposalItem
// --------------------------
KeywordsAssistProposalItem::KeywordsAssistProposalItem(bool isFunction)
    : m_isFunction(isFunction)
{
}

bool KeywordsAssistProposalItem::prematurelyApplies(const QChar &c) const
{
    // only '(' in case of a function
    return c == QLatin1Char('(') && m_isFunction;
}

void KeywordsAssistProposalItem::applyContextualContent(TextDocumentManipulatorInterface &manipulator,
                                                        int basePosition) const
{
    const CompletionSettings &settings = TextEditorSettings::completionSettings();

    int replaceLength = manipulator.currentPosition() - basePosition;
    QString toInsert = text();
    int cursorOffset = 0;
    const QChar characterAtCurrentPosition = manipulator.characterAt(manipulator.currentPosition());
    bool setAutoCompleteSkipPosition = false;

    if (m_isFunction && settings.m_autoInsertBrackets) {
        if (settings.m_spaceAfterFunctionName) {
            if (manipulator.textAt(manipulator.currentPosition(), 2) == QLatin1String(" (")) {
                cursorOffset = 2;
            } else if ( characterAtCurrentPosition == QLatin1Char('(')
                       || characterAtCurrentPosition == QLatin1Char(' ')) {
                replaceLength += 1;
                toInsert += QLatin1String(" (");
            } else {
                toInsert += QLatin1String(" ()");
                cursorOffset = -1;
                setAutoCompleteSkipPosition = true;
            }
        } else {
            if (characterAtCurrentPosition == QLatin1Char('(')) {
                cursorOffset = 1;
            } else {
                toInsert += QLatin1String("()");
                cursorOffset = -1;
                setAutoCompleteSkipPosition = true;
            }
        }
    }

    manipulator.replace(basePosition, replaceLength, toInsert);
    if (cursorOffset)
        manipulator.setCursorPosition(manipulator.currentPosition() + cursorOffset);
    if (setAutoCompleteSkipPosition)
        manipulator.setAutoCompleteSkipPosition(manipulator.currentPosition());
}

// -------------------------
// KeywordsFunctionHintModel
// -------------------------
KeywordsFunctionHintModel::KeywordsFunctionHintModel(const QStringList &functionSymbols)
        : m_functionSymbols(functionSymbols)
{}

void KeywordsFunctionHintModel::reset()
{}

int KeywordsFunctionHintModel::size() const
{
    return m_functionSymbols.size();
}

QString KeywordsFunctionHintModel::text(int index) const
{
    return m_functionSymbols.at(index);
}

int KeywordsFunctionHintModel::activeArgument(const QString &prefix) const
{
    // OPENMV-DIFF //
    {
        QString prefix2 = QString(prefix).replace(QChar::ParagraphSeparator, QLatin1Char('\n'));

        if(!prefix2.isEmpty())
        {
            enum
            {
                IN_PUSH_0, // (
                IN_PUSH_1, // [
                IN_PUSH_2, // {
                IN_COMMA
            };

            QStack<int> in_stack;

            enum
            {
                IN_NONE,
                IN_COMMENT,
                IN_STRING_0,
                IN_STRING_1
            }
            in_state = IN_NONE;

            for(int i = 0; i < prefix2.size(); i++)
            {
                switch(in_state)
                {
                    case IN_NONE:
                    {
                        if((prefix2.at(i) == QLatin1Char('#')) && ((!i) || (prefix2.at(i-1) != QLatin1Char('\\')))) in_state = IN_COMMENT;
                        if((prefix2.at(i) == QLatin1Char('\'')) && ((!i) || (prefix2.at(i-1) != QLatin1Char('\\')))) in_state = IN_STRING_0;
                        if((prefix2.at(i) == QLatin1Char('\"')) && ((!i) || (prefix2.at(i-1) != QLatin1Char('\\')))) in_state = IN_STRING_1;
                        if(prefix2.at(i) == QLatin1Char('(')) in_stack.push(IN_PUSH_0);
                        if(prefix2.at(i) == QLatin1Char('[')) in_stack.push(IN_PUSH_1);
                        if(prefix2.at(i) == QLatin1Char('{')) in_stack.push(IN_PUSH_2);
                        if(prefix2.at(i) == QLatin1Char(')')) while(in_stack.size() && (in_stack.pop() != IN_PUSH_0));
                        if(prefix2.at(i) == QLatin1Char(']')) while(in_stack.size() && (in_stack.pop() != IN_PUSH_1));
                        if(prefix2.at(i) == QLatin1Char('}')) while(in_stack.size() && (in_stack.pop() != IN_PUSH_2));
                        if(prefix2.at(i) == QLatin1Char(',')) in_stack.push(IN_COMMA);
                        break;
                    }
                    case IN_COMMENT:
                    {
                        if((prefix2.at(i) == QLatin1Char('\n')) && (prefix2.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                        break;
                    }
                    case IN_STRING_0:
                    {
                        if((prefix2.at(i) == QLatin1Char('\'')) && (prefix2.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                        break;
                    }
                    case IN_STRING_1:
                    {
                        if((prefix2.at(i) == QLatin1Char('\"')) && (prefix2.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                        break;
                    }
                }
            }

            if(in_state != IN_NONE) return -1;

            int commaCount = 0;

            while(in_stack.size() && (in_stack.top() == IN_COMMA))
            {
                commaCount += 1;
                in_stack.pop();
            }

            return ((in_stack.size() == 1) && (in_stack.top() == IN_PUSH_0) && (commaCount < m_functionSymbols.size())) ? commaCount : -1;
        }
    }
    // OPENMV-DIFF //
    Q_UNUSED(prefix)
    return 1;
}

// ---------------------------------
// KeywordsCompletionAssistProcessor
// ---------------------------------
KeywordsCompletionAssistProcessor::KeywordsCompletionAssistProcessor(const Keywords &keywords)
    : m_snippetCollector(QString(), QIcon(":/texteditor/images/snippet.png"))
    // OPENMV-DIFF //
    // , m_variableIcon(QLatin1String(":/codemodel/images/keyword.png"))
    // , m_functionIcon(QLatin1String(":/codemodel/images/member.png"))
    // OPENMV-DIFF //
    , m_variableIcon(Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::VarPublic))
    , m_functionIcon(Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::FuncPublicStatic))
    , m_variableProtectedIcon(Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::VarProtected))
    , m_variablePrivateIcon(Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::VarPrivate))
    , m_functionProtectedIcon(Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::FuncProtectedStatic))
    , m_functionPrivateIcon(Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::FuncPrivateStatic))
    , m_classIcon(Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::Class))
    , m_methodPublicIcon(Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::FuncPublic))
    , m_methodProtectedIcon(Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::FuncProtected))
    , m_methodPrivateIcon(Utils::CodeModelIcon::iconForType(Utils::CodeModelIcon::FuncPrivate))
    // OPENMV-DIFF //
    , m_keywords(keywords)
{}

IAssistProposal *KeywordsCompletionAssistProcessor::performAsync()
{
    // OPENMV-DIFF //
    {
        QTextCursor cursor(interface()->textDocument());
        cursor.setPosition(interface()->position());
        cursor.movePosition(QTextCursor::Start, QTextCursor::KeepAnchor);
        QString text = cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));

        if(!text.isEmpty())
        {
            enum
            {
                IN_PUSH_0, // (
                IN_PUSH_1, // [
                IN_PUSH_2, // {
                IN_COMMA
            };

            QStack< QPair<int, int> > in_stack;

            enum
            {
                IN_NONE,
                IN_COMMENT,
                IN_STRING_0,
                IN_STRING_1
            }
            in_state = IN_NONE;

            for(int i = 0; i < text.size(); i++)
            {
                switch(in_state)
                {
                    case IN_NONE:
                    {
                        if((text.at(i) == QLatin1Char('#')) && ((!i) || (text.at(i-1) != QLatin1Char('\\')))) in_state = IN_COMMENT;
                        if((text.at(i) == QLatin1Char('\'')) && ((!i) || (text.at(i-1) != QLatin1Char('\\')))) in_state = IN_STRING_0;
                        if((text.at(i) == QLatin1Char('\"')) && ((!i) || (text.at(i-1) != QLatin1Char('\\')))) in_state = IN_STRING_1;
                        if(text.at(i) == QLatin1Char('(')) in_stack.push(QPair<int, int>(IN_PUSH_0, i));
                        if(text.at(i) == QLatin1Char('[')) in_stack.push(QPair<int, int>(IN_PUSH_1, i));
                        if(text.at(i) == QLatin1Char('{')) in_stack.push(QPair<int, int>(IN_PUSH_2, i));
                        if(text.at(i) == QLatin1Char(')')) while(in_stack.size() && (in_stack.pop().first != IN_PUSH_0));
                        if(text.at(i) == QLatin1Char(']')) while(in_stack.size() && (in_stack.pop().first != IN_PUSH_1));
                        if(text.at(i) == QLatin1Char('}')) while(in_stack.size() && (in_stack.pop().first != IN_PUSH_2));
                        if(text.at(i) == QLatin1Char(',')) in_stack.push(QPair<int, int>(IN_COMMA, i));
                        break;
                    }
                    case IN_COMMENT:
                    {
                        if((text.at(i) == QLatin1Char('\n')) && (text.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                        break;
                    }
                    case IN_STRING_0:
                    {
                        if((text.at(i) == QLatin1Char('\'')) && (text.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                        break;
                    }
                    case IN_STRING_1:
                    {
                        if((text.at(i) == QLatin1Char('\"')) && (text.at(i-1) != QLatin1Char('\\'))) in_state = IN_NONE;
                        break;
                    }
                }
            }

            if(in_state != IN_NONE) return 0;

            while(in_stack.size() && (in_stack.top().first == IN_COMMA))
            {
                in_stack.pop();
            }

            if(!interface()->position()) return 0;
            QChar chr = interface()->characterAt(interface()->position() - 1);

            if(chr == QLatin1Char('.'))
            {
                if(!(interface()->position() - 1)) return 0;
                QChar chr2 = interface()->characterAt(interface()->position() - 2);
                bool chr2IsBracket = (chr2 == QLatin1Char(')')) || (chr2 == QLatin1Char(']')) || (chr2 == QLatin1Char('}'));
                cursor.setPosition(interface()->position() - 2);
                cursor.select(QTextCursor::WordUnderCursor);
                if((!chr2IsBracket) && (cursor.selectedText().isEmpty())) return 0;
                if((!chr2IsBracket) && (!cursor.selectedText().contains(QRegularExpression(QStringLiteral("[a-zA-Z_][a-zA-Z_0-9]*"))))) return 0;
                int startPosition = interface()->position();

                QList<AssistProposalItemInterface *> items;
                items.append(generateProposalList(m_keywords.classes(), m_classIcon));
                items.append(generateProposalList(m_keywords.functions(KEYWORDS_SCOPE_PUBLIC), m_functionIcon));
                items.append(generateProposalList(m_keywords.functions(KEYWORDS_SCOPE_PROTECTED), m_functionProtectedIcon));
                items.append(generateProposalList(m_keywords.functions(KEYWORDS_SCOPE_PRIVATE), m_functionPrivateIcon));
                items.append(generateProposalList(m_keywords.methods(KEYWORDS_SCOPE_PUBLIC), m_methodPublicIcon));
                items.append(generateProposalList(m_keywords.methods(KEYWORDS_SCOPE_PROTECTED), m_methodProtectedIcon));
                items.append(generateProposalList(m_keywords.methods(KEYWORDS_SCOPE_PRIVATE), m_methodPrivateIcon));
                items.append(generateProposalList(m_keywords.variables(KEYWORDS_SCOPE_PUBLIC), m_variableIcon));
                items.append(generateProposalList(m_keywords.variables(KEYWORDS_SCOPE_PROTECTED), m_variableProtectedIcon));
                items.append(generateProposalList(m_keywords.variables(KEYWORDS_SCOPE_PRIVATE), m_variablePrivateIcon));
                return new GenericProposal(startPosition, items);
            }
            else if(chr == QLatin1Char('('))
            {
                if(!(interface()->position() - 1)) return 0;
                cursor.setPosition(interface()->position() - 2);
                cursor.select(QTextCursor::WordUnderCursor);
                if((!m_keywords.isClass(cursor.selectedText()))
                && (!m_keywords.isFunction(cursor.selectedText()))
                && (!m_keywords.isMethod(cursor.selectedText()))) return 0;
                int startPosition = interface()->position() - cursor.selectedText().size() - 1;

                QString word = cursor.selectedText();
                QStringList keywords;
                if (m_keywords.isClass(word)) keywords = m_keywords.argsForClass(word);
                else if (m_keywords.isFunction(word)) keywords = m_keywords.argsForFunction(word);
                else if (m_keywords.isMethod(word)) keywords = m_keywords.argsForMethod(word);
                if(keywords.isEmpty()) return 0;
                return new FunctionHintProposal(startPosition, FunctionHintProposalModelPtr(new KeywordsFunctionHintModel(keywords)));
            }
            else if((chr == QLatin1Char(',')) && (in_stack.size() >= 1) && (in_stack.top().first == IN_PUSH_0))
            {
                if(!in_stack.top().second) return 0;
                cursor.setPosition(in_stack.top().second - 1);
                cursor.select(QTextCursor::WordUnderCursor);
                if((!m_keywords.isClass(cursor.selectedText()))
                && (!m_keywords.isFunction(cursor.selectedText()))
                && (!m_keywords.isMethod(cursor.selectedText()))) return 0;
                int startPosition = in_stack.top().second - cursor.selectedText().size();

                QString word = cursor.selectedText();
                QStringList keywords;
                if (m_keywords.isClass(word)) keywords = m_keywords.argsForClass(word);
                else if (m_keywords.isFunction(word)) keywords = m_keywords.argsForFunction(word);
                else if (m_keywords.isMethod(word)) keywords = m_keywords.argsForMethod(word);
                if(keywords.isEmpty()) return 0;
                return new FunctionHintProposal(startPosition, FunctionHintProposalModelPtr(new KeywordsFunctionHintModel(keywords)));
            }
            else if(chr.isLetterOrNumber() || (chr == QLatin1Char('_')))
            {
                cursor.setPosition(interface()->position() - 1);
                cursor.select(QTextCursor::WordUnderCursor);
                if(cursor.selectedText().isEmpty()) return 0;
                if(!cursor.selectedText().contains(QRegularExpression(QStringLiteral("[a-zA-Z_][a-zA-Z_0-9]*")))) return 0;
                int startPosition = interface()->position() - cursor.selectedText().size();

                QList<AssistProposalItemInterface *> items;
                items.append(generateProposalList(m_keywords.classes(), m_classIcon));
                items.append(generateProposalList(m_keywords.functions(KEYWORDS_SCOPE_PUBLIC), m_functionIcon));
                items.append(generateProposalList(m_keywords.functions(KEYWORDS_SCOPE_PROTECTED), m_functionProtectedIcon));
                items.append(generateProposalList(m_keywords.functions(KEYWORDS_SCOPE_PRIVATE), m_functionPrivateIcon));
                items.append(generateProposalList(m_keywords.methods(KEYWORDS_SCOPE_PUBLIC), m_methodPublicIcon));
                items.append(generateProposalList(m_keywords.methods(KEYWORDS_SCOPE_PROTECTED), m_methodProtectedIcon));
                items.append(generateProposalList(m_keywords.methods(KEYWORDS_SCOPE_PRIVATE), m_methodPrivateIcon));
                items.append(generateProposalList(m_keywords.variables(KEYWORDS_SCOPE_PUBLIC), m_variableIcon));
                items.append(generateProposalList(m_keywords.variables(KEYWORDS_SCOPE_PROTECTED), m_variableProtectedIcon));
                items.append(generateProposalList(m_keywords.variables(KEYWORDS_SCOPE_PRIVATE), m_variablePrivateIcon));
                return new GenericProposal(startPosition, items);
            }
        }

        return 0;
    }
    // OPENMV-DIFF //

    if (isInComment(interface()))
        return nullptr;

    int pos = interface()->position();

    // Find start position
    QChar chr = interface()->characterAt(pos - 1);
    if (chr == '(')
        --pos;
    // Skip to the start of a name
    do {
        chr = interface()->characterAt(--pos);
    } while (chr.isLetterOrNumber() || chr == '_');

    ++pos;

    int startPosition = pos;

    if (interface()->reason() == IdleEditor) {
        QChar characterUnderCursor = interface()->characterAt(interface()->position());
        if (characterUnderCursor.isLetterOrNumber() || interface()->position() - startPosition
                < TextEditorSettings::completionSettings().m_characterThreshold) {
            QList<AssistProposalItemInterface *> items;
            if (m_dynamicCompletionFunction)
                m_dynamicCompletionFunction(interface(), &items, startPosition);
            if (items.isEmpty())
                return nullptr;
            return new GenericProposal(startPosition, items);
        }
    }

    // extract word
    QString word;
    do {
        word += interface()->characterAt(pos);
        chr = interface()->characterAt(++pos);
    } while ((chr.isLetterOrNumber() || chr == '_') && chr != '(');

    if (m_keywords.isFunction(word) && interface()->characterAt(pos) == '(') {
        QStringList functionSymbols = m_keywords.argsForFunction(word);
        if (functionSymbols.size() == 0)
            return nullptr;
        FunctionHintProposalModelPtr model(new KeywordsFunctionHintModel(functionSymbols));
        return new FunctionHintProposal(startPosition, model);
    } else {
        const int originalStartPos = startPosition;
        QList<AssistProposalItemInterface *> items;
        if (m_dynamicCompletionFunction)
            m_dynamicCompletionFunction(interface(), &items, startPosition);
        if (startPosition == originalStartPos) {
            items.append(m_snippetCollector.collect());
            items.append(generateProposalList(m_keywords.variables(), m_variableIcon));
            items.append(generateProposalList(m_keywords.functions(), m_functionIcon));
        }
        return new GenericProposal(startPosition, items);
    }
}

void KeywordsCompletionAssistProcessor::setSnippetGroup(const QString &id)
{
    m_snippetCollector.setGroupId(id);
}

void KeywordsCompletionAssistProcessor::setKeywords(const Keywords &keywords)
{
    m_keywords = keywords;
}

void KeywordsCompletionAssistProcessor::setDynamicCompletionFunction(DynamicCompletionFunction func)
{
    m_dynamicCompletionFunction = func;
}

bool KeywordsCompletionAssistProcessor::isInComment(const AssistInterface *interface) const
{
    QTextCursor tc(interface->textDocument());
    tc.setPosition(interface->position());
    tc.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
    return tc.selectedText().contains('#');
}

QList<AssistProposalItemInterface *>
KeywordsCompletionAssistProcessor::generateProposalList(const QStringList &words, const QIcon &icon)
{
    return Utils::transform(words, [this, &icon](const QString &word) -> AssistProposalItemInterface * {
        // OPENMV-DIFF //
        // AssistProposalItem *item = new KeywordsAssistProposalItem(m_keywords.isFunction(word));
        // OPENMV-DIFF //
        AssistProposalItem *item = new KeywordsAssistProposalItem(m_keywords.isClass(word) || m_keywords.isFunction(word) || m_keywords.isMethod(word));
        // OPENMV-DIFF //
        item->setText(word);
        item->setIcon(icon);
        return item;
    });
}

KeywordsCompletionAssistProvider::KeywordsCompletionAssistProvider(const Keywords &keyWords,
                                                                   const QString &snippetGroup)
    : m_keyWords(keyWords)
    , m_snippetGroup(snippetGroup)
{ }

void KeywordsCompletionAssistProvider::setDynamicCompletionFunction(
        const DynamicCompletionFunction &func)
{
    m_completionFunc = func;
}

IAssistProcessor *KeywordsCompletionAssistProvider::createProcessor(const AssistInterface *) const
{
    auto processor = new KeywordsCompletionAssistProcessor(m_keyWords);
    processor->setSnippetGroup(m_snippetGroup);
    processor->setDynamicCompletionFunction(m_completionFunc);
    return processor;
}

void pathComplete(const AssistInterface *interface, QList<AssistProposalItemInterface *> *items,
                  int &startPosition)
{
    if (!items)
        return;

    if (interface->filePath().isEmpty())
        return;

    // For pragmatic reasons, we don't support spaces in file names here.
    static const auto canOccurInFilePath = [](const QChar &c) {
        return c.isLetterOrNumber() || c == '.' || c == '/' || c == '_' || c == '-';
    };

    int pos = interface->position();
    QChar chr;
    // Skip to the start of a name
    do {
        chr = interface->characterAt(--pos);
    } while (canOccurInFilePath(chr));

    const int startPos= ++pos;

    if (interface->reason() == IdleEditor && interface->position() - startPos < 3)
        return;

    const QString word = interface->textAt(startPos, interface->position() - startPos);
    QDir baseDir = interface->filePath().toFileInfo().absoluteDir();
    const int lastSlashPos = word.lastIndexOf(QLatin1Char('/'));

    QString prefix = word;
    if (lastSlashPos != -1) {
        prefix = word.mid(lastSlashPos +1);
        if (!baseDir.cd(word.left(lastSlashPos)))
            return;
    }

    const QFileInfoList entryInfoList
            = baseDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entryInfoList) {
        const QString &fileName = entry.fileName();
        if (fileName.startsWith(prefix)) {
            AssistProposalItem *item = new AssistProposalItem;
            if (entry.isDir()) {
                item->setText(fileName + QLatin1String("/"));
                item->setIcon(Utils::Icons::DIR.icon());
            } else {
                item->setText(fileName);
                item->setIcon(Utils::Icons::UNKNOWN_FILE.icon());
            }
            *items << item;
        }
    }
    if (!items->empty())
        startPosition = startPos;
}


} // namespace TextEditor
