#include "openmvplugin.h"

#include "openmvtr.h"

namespace OpenMV {
namespace Internal {

QStringList OpenMVPlugin::processArgumentSplitting(const QString &args)
{
    enum
    {
        IN_PUSH_0, // (
        IN_PUSH_1, // [
        IN_PUSH_2 // {
    };

    QStack<int> in_stack;

    QList<int> splits;
    splits << 0;

    for(int i = 0; i < args.size(); i++)
    {
        if(args.at(i) == QLatin1Char('(')) in_stack.push(IN_PUSH_0);
        if(args.at(i) == QLatin1Char('[')) in_stack.push(IN_PUSH_1);
        if(args.at(i) == QLatin1Char('{')) in_stack.push(IN_PUSH_2);
        if(args.at(i) == QLatin1Char(')')) while(in_stack.size() && (in_stack.pop() != IN_PUSH_0));
        if(args.at(i) == QLatin1Char(']')) while(in_stack.size() && (in_stack.pop() != IN_PUSH_1));
        if(args.at(i) == QLatin1Char('}')) while(in_stack.size() && (in_stack.pop() != IN_PUSH_2));
        if(args.at(i) == QLatin1Char(',') && in_stack.isEmpty()) {
            splits.append(i);
        }
    }

    splits.append(args.size());

    QStringList list;

    for(int i = 1; i < splits.size(); i++)
    {
        QString s = args.mid(splits[i-1], splits[i] - splits[i-1]);
        if(s.startsWith(',')) s.removeAt(0);
        if(!s.isEmpty()) list.append(s);
    }

    return list;
}

void OpenMVPlugin::processDocumentationMatch(const QRegularExpressionMatch &match,
                                             QStringList &providerVariables,
                                             QStringList &providerClasses, QMap<QString, QStringList> &providerClassArgs,
                                             QStringList &providerFunctions, QMap<QString, QStringList> &providerFunctionArgs,
                                             QStringList &providerMethods, QMap<QString, QStringList> &providerMethodArgs)
{
    QString type = match.captured(1);
    QString id = match.captured(2);
    QString head = match.captured(3);
    QString body = match.captured(4);
    QStringList idList = id.split(QLatin1Char('.'), Qt::SkipEmptyParts);

    if((1 <= idList.size()) && (idList.size() <= 5))
    {
        QRegularExpressionMatch args = m_argumentRegEx.match(head);
        QString argumentString;

        if(args.hasMatch())
        {
            argumentString = QLatin1Char('(') + QString(args.captured(1)).
            remove(m_emRegEx).
            remove(m_spanRegEx).
            remove(QLatin1String("</em>")).
            remove(QLatin1String("</span>")).
            replace(QStringLiteral("[,"), QStringLiteral(" [ ,")) + QLatin1Char(')');
        }

        if(idList.size() == 1)
        {
            idList.prepend(QStringLiteral("builtin"));
        }

        if(idList.size() == 2 && (type == QStringLiteral("method")))
        {
            idList.prepend(QStringLiteral("builtin"));
        }

        QRegularExpressionMatch cdfmRegExInsideMatch = m_cdfmRegExInside.match(body);

        if(cdfmRegExInsideMatch.hasMatch())
        {
            processDocumentationMatch(cdfmRegExInsideMatch,
                                      providerVariables,
                                      providerClasses, providerClassArgs,
                                      providerFunctions, providerFunctionArgs,
                                      providerMethods, providerMethodArgs);
            body.remove(m_cdfmRegExInside);
        }

        QRegularExpressionMatchIterator cdfmRegExSharedMatch = m_cdfmRegExShared.globalMatch(QString(head) + QStringLiteral("</dt>"));

        while(cdfmRegExSharedMatch.hasNext())
        {
            QRegularExpressionMatch match2 = cdfmRegExSharedMatch.next();
            QString temp = QStringLiteral("<dl class=\"py %1\"><dt class=\"sig sig-object py\" id=\"%2\">%3</dt><dd>%4").arg(type).arg(match2.captured(1)).arg(match2.captured(2)).arg(body);
            QRegularExpressionMatch cdfmRegExInsideMatch = m_cdfmRegExInside.match(temp);

            if(cdfmRegExInsideMatch.hasMatch())
            {
                processDocumentationMatch(cdfmRegExInsideMatch,
                                          providerVariables,
                                          providerClasses, providerClassArgs,
                                          providerFunctions, providerFunctionArgs,
                                          providerMethods, providerMethodArgs);
            }
        }

        documentation_t d;

        d.name = (idList.size() > 0) ? idList.at(idList.size() - 1) : QString();

        if ((type == QStringLiteral("class")) || (type == QStringLiteral("exception")))
        {
            d.className = d.name;
            d.moduleName = (idList.size() > 1) ? idList.mid(0, idList.size() - 1).join('.') : QString();

            // Remove duplicate module names in path (bug fix for documentation issues)...
            QStringList test = d.moduleName.split('.');
            if ((test.size() >= 2) && (test.at(test.size() - 1) == test.at(test.size() - 2))) {
                test.removeLast();
                d.moduleName = test.join('.');
            }
        }
        else if ((type == QStringLiteral("method")) || (type == QStringLiteral("attribute")))
        {
            d.className = (idList.size() > 1) ? idList.at(idList.size() - 2) : QString();
            d.moduleName = (idList.size() > 2) ? idList.mid(0, idList.size() - 2).join('.') : QString();

            // Remove duplicate module names in path (bug fix for documentation issues)...
            QStringList test = d.moduleName.split('.');
            if ((test.size() >= 2) && (test.at(test.size() - 1) == test.at(test.size() - 2))) {
                test.removeLast();
                d.moduleName = test.join('.');
            }
        }
        else
        {
            d.moduleName = (idList.size() > 1) ? idList.mid(0, idList.size() - 1).join('.') : QString();
        }

        d.text = QString(QStringLiteral("<h3>%1%2</h3>%3")).arg(d.moduleName.isEmpty() ? d.name : (d.moduleName + QStringLiteral(" - ") + (d.className.isEmpty() ? d.name : (d.className + QLatin1Char('.') + d.name)))).arg(argumentString).arg(body).
                 remove(QStringLiteral("\u00B6")).
                 remove(m_spanRegEx).
                 remove(QStringLiteral("</span>")).
                 remove(m_anchorRegEx).
                 remove(QStringLiteral("</a>")).
                 remove(m_classRegEx).
                 replace(QStringLiteral("<li><p>"), QStringLiteral("<li>")).
                 replace(QStringLiteral("</p></li>"), QStringLiteral("</li>")).
                 remove(QStringLiteral("<blockquote>")).
                 remove(QStringLiteral("</blockquote>"));

        if(QString(d.text).remove(QRegularExpression(QStringLiteral("<h3>.+?</h3>"))).isEmpty())
        {
            return;
        }

        if((type == QStringLiteral("class")) || (type == QStringLiteral("exception")))
        {
            // Get rid of backup class objects created to handle lack of class defination...
            if (m_classes.size() && (m_classes.last().name == d.name))
            {
                m_classes.removeLast();
            }

            m_classes.append(d);
            providerClasses.append(d.name);
        }
        else if((type == QStringLiteral("data")) || (type == QStringLiteral("attribute")))
        {
            m_datas.append(d);
            providerVariables.append(d.name);
        }
        else if(type == QStringLiteral("function"))
        {
            m_functions.append(d);
            providerFunctions.append(d.name);
        }
        else if(type == QStringLiteral("method"))
        {
            bool missing = true;

            for(const documentation_t &m : m_classes)
            {
                if(m.name == d.className)
                {
                    missing = false;
                    break;
                }
            }

            if(missing)
            {
                documentation_t d2;
                d2.moduleName = d.moduleName;
                d2.className = d.className;
                d2.name = d.className;
                d2.text = QStringLiteral("<h3>%1</h3>").arg(d2.moduleName.isEmpty() ? d2.name : (d2.moduleName + QStringLiteral(" - ") + (d2.className.isEmpty() ? d2.name : (d2.className + QLatin1Char('.') + d2.name))));
                m_classes.append(d2);
            }

            m_methods.append(d);
            providerMethods.append(d.name);
        }

        QRegularExpressionMatch returnMatch = m_returnTypeRegEx.match(head);

        if(returnMatch.hasMatch())
        {
            QString returnString = returnMatch.captured(1).remove(QStringLiteral("<span class=\"w\"> </span>")).
                    remove(m_spanRegEx).
                    remove(QStringLiteral("</span>")).
                    remove(m_anchorRegEx).
                    remove(QStringLiteral("</a>")).
                    remove(m_preRexEx).
                    remove(QStringLiteral("</pre>")).
                    remove(m_emRegEx).
                    remove(QLatin1String("</em>"));

            if(type == QStringLiteral("class"))
            {
                m_returnTypesByHierarchy.insert(QStringList() << d.moduleName << d.name, returnString);
            }
            else if(type == QStringLiteral("function"))
            {
                m_returnTypesByHierarchy.insert(QStringList() << d.moduleName << d.name, returnString);
            }
            else if(type == QStringLiteral("method"))
            {
                m_returnTypesByHierarchy.insert(QStringList() << d.moduleName << d.className << d.name, returnString);
            }
        }

        QRegularExpressionMatch returnMatch2 = m_dataReturnTypeRexEx.match(head);

        if(returnMatch2.hasMatch())
        {
            QString returnString = returnMatch2.captured(1).remove(QStringLiteral("<span class=\"w\"> </span>")).
                    remove(m_spanRegEx).
                    remove(QStringLiteral("</span>")).
                    remove(m_anchorRegEx).
                    remove(QStringLiteral("</a>")).
                    remove(m_preRexEx).
                    remove(QStringLiteral("</pre>")).
                    remove(m_emRegEx).
                    remove(QLatin1String("</em>"));

            if(type == QStringLiteral("data"))
            {
                m_returnTypesByHierarchy.insert(QStringList() << d.moduleName << d.name, returnString);
            }

            if(type == QStringLiteral("attribute"))
            {
                m_returnTypesByHierarchy.insert(QStringList() << d.moduleName << d.className << d.name, returnString);
            }
        }

        if(args.hasMatch())
        {
            QStringList list, listWithTypesAndDefaults;

            for(const QString &arg : processArgumentSplitting(QString(args.captured(1)).
                                        remove(QLatin1String("<span class=\"optional\">[</span>")).
                                        remove(QLatin1String("<span class=\"optional\">]</span>")).
                                        remove(m_emRegEx).
                                        remove(m_spanRegEx).
                                        remove(m_anchorRegEx).
                                        remove(QLatin1String("</em>")).
                                        remove(QLatin1String("</span>")).
                                        remove(QLatin1String("</a>")).
                                        remove(QLatin1Char(' '))))
            {
                int equals = arg.indexOf(QLatin1Char('='));
                QString temp = (equals != -1) ? arg.left(equals) : arg;
                temp = QString(temp).remove(m_typeHintRegEx);

                m_arguments.insert(temp);
                list.append(temp);
                listWithTypesAndDefaults.append(arg);
            }

            if(type == QStringLiteral("class"))
            {
                m_argumentsByHierarchy.insert(QStringList() << d.moduleName << d.name, listWithTypesAndDefaults);
                providerClassArgs.insert(d.name, list);
            }
            else if(type == QStringLiteral("function"))
            {
                m_argumentsByHierarchy.insert(QStringList() << d.moduleName << d.name, listWithTypesAndDefaults);
                providerFunctionArgs.insert(d.name, list);
            }
            else if(type == QStringLiteral("method"))
            {
                m_argumentsByHierarchy.insert(QStringList() << d.moduleName << d.className << d.name, listWithTypesAndDefaults);
                providerMethodArgs.insert(d.name, list);
            }
        }
    }
}

void OpenMVPlugin::loadDocs()
{
    QStringList providerVariables;
    QStringList providerClasses;
    QMap<QString, QStringList> providerClassArgs;
    QStringList providerFunctions;
    QMap<QString, QStringList> providerFunctionArgs;
    QStringList providerMethods;
    QMap<QString, QStringList> providerMethodArgs;

    QRegularExpression moduleRegEx(QStringLiteral("<section id=\"module-(.+?)\">(.*?)<section"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression moduleRegEx2(QStringLiteral("<section id=\"module-(.+?)\">(.*?)</section>"), QRegularExpression::DotMatchesEverythingOption);
    m_emRegEx = QRegularExpression(QLatin1String("<em.*?>"), QRegularExpression::DotMatchesEverythingOption);
    m_spanRegEx = QRegularExpression(QStringLiteral("<span.*?>"), QRegularExpression::DotMatchesEverythingOption);
    m_anchorRegEx = QRegularExpression(QStringLiteral("<a.*?>"), QRegularExpression::DotMatchesEverythingOption);
    m_preRexEx = QRegularExpression(QStringLiteral("<pre.*?>"), QRegularExpression::DotMatchesEverythingOption);
    m_classRegEx = QRegularExpression(QStringLiteral(" class=\".*?\""), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression cdfmRegEx(QStringLiteral("<dl class=\"py (class|data|exception|function|method|attribute)\">\\s*<dt class=\".+?\" id=\"(.+?)\">(.*?)</dt>\\s*<dd>(.*?)(?:<section|</dd>\\s*</dl>)"), QRegularExpression::DotMatchesEverythingOption);
    m_cdfmRegExInside = QRegularExpression(QStringLiteral("<dl class=\"py (class|data|exception|function|method|attribute)\">\\s*<dt class=\".+?\" id=\"(.+?)\">(.*?)</dt>\\s*<dd>(.*)"), QRegularExpression::DotMatchesEverythingOption);
    m_cdfmRegExShared = QRegularExpression(QStringLiteral("<dt class=\".+?\" id=\"(.+?)\">(.*?)</dt>"), QRegularExpression::DotMatchesEverythingOption);
    m_argumentRegEx = QRegularExpression(QStringLiteral("<span class=\"sig-paren\">\\(</span>(.*?)<span class=\"sig-paren\">\\)</span>"), QRegularExpression::DotMatchesEverythingOption);
    m_returnTypeRegEx = QRegularExpression(QStringLiteral("<span class=\"sig-return-typehint\">(.+?)<a class=\"headerlink\""), QRegularExpression::DotMatchesEverythingOption);
    m_dataReturnTypeRexEx = QRegularExpression(QStringLiteral("<span class=\"pre\">:(.+?)<a class=\"headerlink\""), QRegularExpression::DotMatchesEverythingOption);
    m_tupleRegEx = QRegularExpression(QStringLiteral("\\(.*?\\)"), QRegularExpression::DotMatchesEverythingOption);
    m_listRegEx = QRegularExpression(QStringLiteral("\\[.*?\\]"), QRegularExpression::DotMatchesEverythingOption);
    m_dictionaryRegEx = QRegularExpression(QStringLiteral("\\{.*?\\}"), QRegularExpression::DotMatchesEverythingOption);
    m_typeHintRegEx = QRegularExpression(QStringLiteral("(:\\s*[^:]+)"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression sectionRegEx(QStringLiteral("<section.*"), QRegularExpression::DotMatchesEverythingOption);

    QDirIterator it(Core::ICore::userResourcePath(QStringLiteral("html/library")).toString(), QDir::Files);

    while(it.hasNext())
    {
        QFile file(it.next());

        if(file.open(QIODevice::ReadOnly))
        {
            QString data = QString::fromUtf8(file.readAll());

            if((file.error() == QFile::NoError) && (!data.isEmpty()))
            {
                file.close();

                QRegularExpressionMatchIterator moduleMatch = moduleRegEx2.globalMatch(data);
                if(!moduleMatch.hasNext()) moduleMatch = moduleRegEx.globalMatch(data);

                while(moduleMatch.hasNext())
                {
                    QRegularExpressionMatch match = moduleMatch.next();
                    QString name = match.captured(1);
                    QString text = match.captured(2).
                                   remove(QStringLiteral("\u00B6")).
                                   remove(m_spanRegEx).
                                   remove(QStringLiteral("</span>")).
                                   remove(m_anchorRegEx).
                                   remove(QStringLiteral("</a>")).
                                   remove(m_classRegEx).
                                   replace(QStringLiteral("<h1>"), QStringLiteral("<h3>")).
                                   replace(QStringLiteral("</h1>"), QStringLiteral("</h3>")).
                                   remove(sectionRegEx);

                    documentation_t d;
                    d.moduleName = QString();
                    d.className = QString();
                    d.name = name;
                    d.text = text;
                    m_modules.append(d);

                    if(name.startsWith(QLatin1Char('u')))
                    {
                        d.name = name.mid(1);
                        m_modules.append(d);
                    }
                }

                QRegularExpressionMatchIterator matches = cdfmRegEx.globalMatch(data);

                while(matches.hasNext())
                {
                    processDocumentationMatch(matches.next(),
                                              providerVariables,
                                              providerClasses, providerClassArgs,
                                              providerFunctions, providerFunctionArgs,
                                              providerMethods, providerMethodArgs);
                }
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////

    KSyntaxHighlighting::Definition id = TextEditor::HighlighterHelper::definitionForName(QStringLiteral("Python"));

    if(id.isValid())
    {
        if(id.d && id.d->load())
        {
            KSyntaxHighlighting::KeywordList *modulesList = id.d->keywordList(QStringLiteral("listOpenMVModules"));
            KSyntaxHighlighting::KeywordList *classesList = id.d->keywordList(QStringLiteral("listOpenMVClasses"));
            KSyntaxHighlighting::KeywordList *datasList = id.d->keywordList(QStringLiteral("listOpenMVDatas"));
            KSyntaxHighlighting::KeywordList *functionsList = id.d->keywordList(QStringLiteral("listOpenMVFunctions"));
            KSyntaxHighlighting::KeywordList *methodsList = id.d->keywordList(QStringLiteral("listOpenMVMethods"));
            KSyntaxHighlighting::KeywordList *argumentsList = id.d->keywordList(QStringLiteral("listOpenMVArguments"));

            if(modulesList)
            {
                QStringList list = modulesList->keywords();
                list.removeAll(QStringLiteral("OpenMVVModulesPlaceHolderKeyword"));

                for(const documentation_t &d : m_modules)
                {
                    list.append(d.name);
                }

                modulesList->setKeywordList(list);
            }

            if(classesList)
            {
                QStringList list = classesList->keywords();
                list.removeAll(QStringLiteral("OpenMVClassesPlaceHolderKeyword"));

                for(const documentation_t &d : m_classes)
                {
                    list.append(d.name);
                }

                classesList->setKeywordList(list);
            }

            if(datasList)
            {
                QStringList list = datasList->keywords();
                list.removeAll(QStringLiteral("OpenMVDatasPlaceHolderKeyword"));

                for(const documentation_t &d : m_datas)
                {
                    list.append(d.name);
                }

                datasList->setKeywordList(list);
            }

            if(functionsList)
            {
                QStringList list = functionsList->keywords();
                list.removeAll(QStringLiteral("OpenMVFunctionsPlaceHolderKeyword"));

                for(const documentation_t &d : m_functions)
                {
                    list.append(d.name);
                }

                functionsList->setKeywordList(list);
            }

            if(methodsList)
            {
                QStringList list = methodsList->keywords();
                list.removeAll(QStringLiteral("OpenMVMethodsPlaceHolderKeyword"));

                for(const documentation_t &d : m_methods)
                {
                    list.append(d.name);
                }

                methodsList->setKeywordList(list);
            }

            if(argumentsList)
            {
                QStringList list = argumentsList->keywords();
                list.removeAll(QStringLiteral("OpenMVArgumentsPlaceHolderKeyword"));

                for(const QString &d : m_arguments.values())
                {
                    list.append(d);
                }

                argumentsList->setKeywordList(list);
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////

    OpenMVPluginCompletionAssistProvider *provider = new OpenMVPluginCompletionAssistProvider(providerVariables,
                                                                                              providerClasses, providerClassArgs,
                                                                                              providerFunctions, providerFunctionArgs,
                                                                                              providerMethods, providerMethodArgs,
                                                                                              this);

    connect(Core::EditorManager::instance(), &Core::EditorManager::editorCreated, this, [this, provider] (Core::IEditor *editor, const Utils::FilePath &filePath) {
        TextEditor::BaseTextEditor *textEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor);

        if(textEditor && filePath.toString().endsWith(QStringLiteral(".py"), Qt::CaseInsensitive))
        {
            textEditor->textDocument()->setCompletionAssistProvider(provider);
            connect(textEditor->editorWidget(), &TextEditor::TextEditorWidget::lateTooltipOverrideRequested, this,
                [this] (TextEditor::TextEditorWidget *widget, const QPoint &globalPos, int position, bool *handled, const QString &originalToolTip) {

                if(handled)
                {
                    *handled = true;
                }

                QTextCursor cursor(widget->textDocument()->document());
                cursor.setPosition(position);
                cursor.movePosition(QTextCursor::Start, QTextCursor::KeepAnchor);
                QString text = cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));

                if(!text.isEmpty())
                {
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

                    if(in_state == IN_NONE)
                    {
                        cursor.setPosition(position);
                        cursor.select(QTextCursor::WordUnderCursor);
                        text = cursor.selectedText();

                        QTextCursor newCursor(cursor);
                        QString maybeModuleName;
                        bool moduleFilter = false;

                        // 1. Move the cursor to break selection, 2. Move the cursor to '.', 3. Move the cursor onto the word behind '.'.
                        if(newCursor.movePosition(QTextCursor::PreviousWord, QTextCursor::MoveAnchor, 3))
                        {
                            newCursor.select(QTextCursor::WordUnderCursor);
                            maybeModuleName = newCursor.selectedText();

                            if(!maybeModuleName.isEmpty())
                            {
                                for(const documentation_t &d : m_modules)
                                {
                                    if(d.name == maybeModuleName)
                                    {
                                        moduleFilter = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if(!text.isEmpty())
                        {
                            QStringList list;

                            for(const documentation_t &d : m_modules)
                            {
                                if(d.name == text)
                                {
                                    list.append(d.text);
                                }
                            }

                            for(const documentation_t &d : m_datas)
                            {
                                if((d.name == text) && ((!moduleFilter) || (d.moduleName == maybeModuleName)))
                                {
                                    list.append(d.text);
                                }
                            }

                            if(widget->textDocument()->document()->characterAt(qMax(cursor.position(), cursor.anchor())) == QLatin1Char('('))
                            {
                                for(const documentation_t &d : m_classes)
                                {
                                    if((d.name == text) && ((!moduleFilter) || (d.moduleName == maybeModuleName)))
                                    {
                                        list.append(d.text);
                                    }
                                }

                                for(const documentation_t &d : m_functions)
                                {
                                    if((d.name == text) && ((!moduleFilter) || (d.moduleName == maybeModuleName)))
                                    {
                                        list.append(d.text);
                                    }
                                }

                                if(qMin(cursor.position(), cursor.anchor()) && (widget->textDocument()->document()->characterAt(qMin(cursor.position(), cursor.anchor()) - 1) == QLatin1Char('.')))
                                {
                                    for(const documentation_t &d : m_methods)
                                    {
                                        if((d.name == text) && ((!moduleFilter) || (d.moduleName == maybeModuleName)))
                                        {
                                            list.append(d.text);
                                        }
                                    }
                                }
                            }

                            if(!list.isEmpty())
                            {
                                int index = originalToolTip.indexOf(QStringLiteral("<h3>"));
                                QString cleanedToolTip = originalToolTip;

                                if (index != -1)
                                {
                                    cleanedToolTip = originalToolTip.mid(index).remove(QStringLiteral("\\"));
                                    list = QStringList() << cleanedToolTip;
                                }

                                QString string;
                                int i = 0;

                                for(int j = 0, k = qCeil(qSqrt(list.size())); j < k; j++)
                                {
                                    string.append(QStringLiteral("<tr>"));

                                    for(int l = 0; l < k; l++)
                                    {
                                        string.append(QStringLiteral("<td style=\"padding:6px;\">") + list.at(i++) + QStringLiteral("</td>"));

                                        if(i >= list.size())
                                        {
                                            break;
                                        }
                                    }

                                    string.append(QStringLiteral("</tr>"));

                                    if(i >= list.size())
                                    {
                                        break;
                                    }
                                }

                                Utils::ToolTip::show(globalPos, QStringLiteral("<table>") + string + QStringLiteral("</table>"), widget);
                                return;
                            }
                        }
                    }
                }

                Utils::ToolTip::hide();
            });

            connect(textEditor->editorWidget(), &TextEditor::TextEditorWidget::contextMenuEventCB, this, [this, textEditor] (QMenu *menu, QString text) {

                QRegularExpressionMatch grayscaleMatch = QRegularExpression(QStringLiteral("^\\s*\\(\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*\\)\\s*$")).match(text);

                if(grayscaleMatch.hasMatch())
                {
                    menu->addSeparator();
                    QAction *action = new QAction(Tr::tr("Edit Grayscale threshold with Threshold Editor"), menu);
                    connect(action, &QAction::triggered, this, [this, textEditor, grayscaleMatch] {
                        QList<int> list = openThresholdEditor(QList<QVariant>()
                            << grayscaleMatch.captured(1).toInt()
                            << grayscaleMatch.captured(2).toInt()
                        );

                        if(!list.isEmpty())
                        {
                            textEditor->textCursor().removeSelectedText();
                            textEditor->textCursor().insertText(QString(QStringLiteral("(%1, %2)")).arg(list.at(0), 3) // can't use takeFirst() here
                                                                                                   .arg(list.at(1), 3)); // can't use takeFirst() here
                        }
                    });

                    menu->addAction(action);
                }

                QRegularExpressionMatch labMatch = QRegularExpression(QStringLiteral("^\\s*\\(\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*,\\s*([+-]?\\d+)\\s*\\)\\s*$")).match(text);

                if(labMatch.hasMatch())
                {
                    menu->addSeparator();
                    QAction *action = new QAction(Tr::tr("Edit LAB threshold with Threshold Editor"), menu);
                    connect(action, &QAction::triggered, this, [this, textEditor, labMatch] {
                        QList<int> list = openThresholdEditor(QList<QVariant>()
                            << labMatch.captured(1).toInt()
                            << labMatch.captured(2).toInt()
                            << labMatch.captured(3).toInt()
                            << labMatch.captured(4).toInt()
                            << labMatch.captured(5).toInt()
                            << labMatch.captured(6).toInt()
                        );

                        if(!list.isEmpty())
                        {
                            textEditor->textCursor().removeSelectedText();
                            textEditor->textCursor().insertText(QString(QStringLiteral("(%1, %2, %3, %4, %5, %6)")).arg(list.at(2), 3) // can't use takeFirst() here
                                                                                                                   .arg(list.at(3), 3) // can't use takeFirst() here
                                                                                                                   .arg(list.at(4), 4) // can't use takeFirst() here
                                                                                                                   .arg(list.at(5), 4) // can't use takeFirst() here
                                                                                                                   .arg(list.at(6), 4) // can't use takeFirst() here
                                                                                                                   .arg(list.at(7), 4)); // can't use takeFirst() here
                        }
                    });

                    menu->addAction(action);
                }
            });
        }
    });

    ///////////////////////////////////////////////////////////////////////////

    const Utils::FilePath &headers = Core::ICore::userResourcePath(QStringLiteral("micropython-headers"));

    if (!QDir(headers.toString()).exists())
    {
        QDir().mkdir(headers.toString());
    }

    for (const documentation_t &modules : m_modules)
    {
        Utils::FilePath path = headers;
        path = path.pathAppended(modules.name + QStringLiteral(".py"));
        QFile file(path.toString());

        if(file.open(QIODevice::WriteOnly))
        {
            QTextStream stream(&file);

            stream << "from typing import List, Tuple, Union, Any\n";

            if (modules.name != QStringLiteral("image"))
            {
                stream << "import image\n";
            }

            for (const documentation_t &classes : m_classes)
            {
                if (classes.moduleName == modules.name)
                {
                    QStringList hierarchy = QStringList() << classes.moduleName << classes.name;
                    stream << "class " << classes.name << ":\n";
                    stream << "\tdef __init__(self";

                    if (m_argumentsByHierarchy.contains(hierarchy))
                    {
                        stream << ", " << m_argumentsByHierarchy.value(hierarchy).join(", ");
                        if (m_returnTypesByHierarchy.contains(hierarchy)) stream << ") -> " << m_returnTypesByHierarchy.value(hierarchy) << ":\n";
                        else stream << "):\n";
                    }
                    else
                    {
                        stream << "):\n";
                    }

                    stream << "\t\t\"\"\"\n";
                    stream << "\t\t" << classes.text.simplified().trimmed().replace(QStringLiteral("> <"), QStringLiteral("><")) << "\n";
                    stream << "\t\t\"\"\"\n";
                    stream << "\t\tpass\n";

                    for (const documentation_t &methods : m_methods)
                    {
                        if (methods.moduleName == modules.name && methods.className == classes.name)
                        {
                            QStringList hierarchy = QStringList() << methods.moduleName << methods.className << methods.name;
                            stream << "\tdef " << methods.name << "(";
                            stream << (QStringList() << "self" << m_argumentsByHierarchy.value(hierarchy)).join(", ");
                            if (m_returnTypesByHierarchy.contains(hierarchy)) stream << ") -> " << m_returnTypesByHierarchy.value(hierarchy) << ":\n";
                            else stream << "):\n";
                            stream << "\t\t\"\"\"\n";
                            stream << "\t\t" << methods.text.simplified().trimmed().replace(QStringLiteral("> <"), QStringLiteral("><")) << "\n";
                            stream << "\t\t\"\"\"\n";
                            stream << "\t\tpass\n";
                        }
                    }

                    for (const documentation_t &datas : m_datas)
                    {
                        if (datas.moduleName == modules.name && datas.className == classes.name)
                        {
                            QStringList hierarchy = QStringList() << datas.moduleName << datas.className << datas.name;
                            stream << "\t" << datas.name;
                            if (m_returnTypesByHierarchy.contains(hierarchy)) stream << ": " << m_returnTypesByHierarchy.value(hierarchy);
                            stream << " = None\n";
                            stream << "\t\"\"\"\n";
                            stream << "\t" << datas.text.simplified().trimmed().replace(QStringLiteral("> <"), QStringLiteral("><")) << "\n";
                            stream << "\t\"\"\"\n";
                        }
                    }
                }
            }

            for (const documentation_t &function : m_functions)
            {
                if (function.moduleName == modules.name)
                {
                    QStringList hierarchy = QStringList() << function.moduleName << function.name;
                    stream << "def " << function.name << "(";
                    stream << m_argumentsByHierarchy.value(hierarchy).join(", ");
                    if (m_returnTypesByHierarchy.contains(hierarchy)) stream << ") -> " << m_returnTypesByHierarchy.value(hierarchy) << ":\n";
                    else stream << "):\n";
                    stream << "\t\"\"\"\n";
                    stream << "\t" << function.text.simplified().trimmed().replace(QStringLiteral("> <"), QStringLiteral("><")) << "\n";
                    stream << "\t\"\"\"\n";
                    stream << "\tpass\n";
                }
            }

            for (const documentation_t &datas : m_datas)
            {
                if ((datas.moduleName == modules.name) && datas.className.isEmpty())
                {
                    QStringList hierarchy = QStringList() << datas.moduleName << datas.name;
                    stream << "\"\"\"\n";
                    stream << "" << datas.text.simplified().trimmed().replace(QStringLiteral("> <"), QStringLiteral("><")) << "\n";
                    stream << "\"\"\"\n";
                    stream << datas.name;
                    if (m_returnTypesByHierarchy.contains(hierarchy)) stream << ": " << m_returnTypesByHierarchy.value(hierarchy);
                    stream << " = None\n";
                }
            }

            file.close();
        }
    }
}

} // namespace Internal
} // namespace OpenMV
