/* Copyright (C) 2023-2024 OpenMV, LLC.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Any redistribution, use, or modification in source or binary form
 *    is done solely for personal benefit and not for any commercial
 *    purpose or for monetary gain. For commercial licensing options,
 *    please contact openmv@openmv.io
 *
 * THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT
 * OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
            d.className = QString();
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

            for(QString arg : processArgumentSplitting(QString(args.captured(1)).
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

                if (temp == QStringLiteral("..."))
                {
                    continue;
                }

                if (temp == QStringLiteral("'param'"))
                {
                    temp = QStringLiteral("param");
                    arg.replace(QStringLiteral("'param'"), QStringLiteral("param"));
                }

                // TODO: Remove me after documentation is cleaned up..
                // This is a fix for a spelling error in the docs.
                if (arg.endsWith(QStringLiteral("Nonee")))
                {
                    arg.chop(1);
                }

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

// Builds the same documentation structures as the HTML parsing path below, but
// from the .pyi stubs that newer documentation packages ship in html/stubs.
// The stubs are machine-generated by openmv-doc's tools/genpyi.py with a fixed
// layout: module-level "NAME: type" constants, "def name(args) -> ret:"
// functions (with "@overload" stacks), and "class Name:" blocks containing
// "__init__", attributes, and methods - each optionally followed by a
// docstring whose lines are whitespace-stripped and whose closing quotes sit
// alone on the final line.
void OpenMVPlugin::loadStubs(const Utils::FilePath &stubsPath,
                             QStringList &providerVariables,
                             QStringList &providerClasses, QMap<QString, QStringList> &providerClassArgs,
                             QStringList &providerFunctions, QMap<QString, QStringList> &providerFunctionArgs,
                             QStringList &providerMethods, QMap<QString, QStringList> &providerMethodArgs)
{
    // Render the docstring layout literally: every newline is a line break
    // (so blank lines separate paragraphs exactly as written) and runs of
    // spaces survive html whitespace collapsing (indented example code).
    auto docToHtml = [] (const QString &title, const QString &doc) {
        QString body = doc.toHtmlEscaped().trimmed();

        if(!body.isEmpty())
        {
            body.replace(QStringLiteral("  "), QStringLiteral("&nbsp;&nbsp;"));
            body.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
            body = QStringLiteral("<p>") + body + QStringLiteral("</p>");
        }

        return QStringLiteral("<h3>%1</h3>%2").arg(title.toHtmlEscaped()).arg(body);
    };

    auto parseDef = [] (const QString &line, QString &name, QString &args, QString &returnType) {
        int open = line.indexOf(QLatin1Char('('));

        if(open == -1)
        {
            return false;
        }

        name = line.mid(4, open - 4).trimmed();

        int depth = 0, close = -1;

        for(int i = open; i < line.size(); i++)
        {
            if(line.at(i) == QLatin1Char('(')) depth += 1;
            else if(line.at(i) == QLatin1Char(')')) { depth -= 1; if(!depth) { close = i; break; } }
        }

        if(close == -1)
        {
            return false;
        }

        args = line.mid(open + 1, close - open - 1);

        returnType = QString();
        int arrow = line.indexOf(QStringLiteral("->"), close);

        if(arrow != -1)
        {
            returnType = line.mid(arrow + 2).trimmed();
            if(returnType.endsWith(QLatin1Char(':'))) returnType.chop(1);
            returnType = returnType.trimmed();
        }

        return true;
    };

    auto argumentNames = [this] (const QString &argsString, bool isMethod) {
        QStringList names;
        QStringList split = processArgumentSplitting(argsString);

        for(int i = 0; i < split.size(); i++)
        {
            QString arg = split.at(i).trimmed();

            if(isMethod && (i == 0) && (arg == QStringLiteral("self")))
            {
                continue;
            }

            int colon = arg.indexOf(QLatin1Char(':'));
            int equals = arg.indexOf(QLatin1Char('='));
            int cut = ((colon != -1) && ((equals == -1) || (colon < equals))) ? colon : equals;
            QString name = ((cut != -1) ? arg.left(cut) : arg).trimmed();

            while(name.startsWith(QLatin1Char('*')))
            {
                name.remove(0, 1);
            }

            if(name.isEmpty() || (name == QStringLiteral("/")) || (name == QStringLiteral("...")))
            {
                continue;
            }

            m_arguments.insert(name);
            names.append(name);
        }

        return names;
    };

    QDirIterator it(stubsPath.toString(), QStringList() << QStringLiteral("*.pyi"), QDir::Files, QDirIterator::Subdirectories);

    while(it.hasNext())
    {
        QFile file(it.next());

        if(!file.open(QIODevice::ReadOnly))
        {
            continue;
        }

        QString data = QString::fromUtf8(file.readAll());
        file.close();

        if((file.error() != QFile::NoError) || data.isEmpty())
        {
            continue;
        }

        QString relativePath = QDir(stubsPath.toString()).relativeFilePath(QFileInfo(file).absoluteFilePath());
        relativePath.chop(4); // ".pyi"
        QStringList moduleParts = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);

        if((!moduleParts.isEmpty()) && (moduleParts.last() == QStringLiteral("__init__")))
        {
            moduleParts.removeLast();
        }

        if(moduleParts.isEmpty())
        {
            continue;
        }

        QString moduleName = moduleParts.join(QLatin1Char('.'));

        QStringList lines = data.split(QLatin1Char('\n'));

        auto readDocstring = [&lines] (int &i) {
            QString result;

            if(i >= lines.size())
            {
                return result;
            }

            QString trimmed = lines.at(i).trimmed();
            QString opened;

            if(trimmed.startsWith(QStringLiteral("\"\"\""))) opened = trimmed.mid(3);
            else if(trimmed.startsWith(QStringLiteral("r\"\"\""))) opened = trimmed.mid(4);
            else return result;

            i += 1;

            if((!opened.isEmpty()) && opened.endsWith(QStringLiteral("\"\"\"")))
            {
                opened.chop(3);
                return opened;
            }

            QStringList docLines;

            if(!opened.isEmpty())
            {
                docLines.append(opened);
            }

            while(i < lines.size())
            {
                QString docLine = lines.at(i).trimmed();
                i += 1;

                if(docLine == QStringLiteral("\"\"\""))
                {
                    break;
                }

                docLines.append(docLine);
            }

            return docLines.join(QLatin1Char('\n'));
        };

        // Module docstring: the first statement after the header comment.
        int firstLine = 0;

        while((firstLine < lines.size())
        && (lines.at(firstLine).trimmed().isEmpty() || lines.at(firstLine).trimmed().startsWith(QLatin1Char('#'))))
        {
            firstLine += 1;
        }

        QString moduleDoc = readDocstring(firstLine);

        documentation_t moduleEntry;
        moduleEntry.moduleName = QString();
        moduleEntry.className = QString();
        moduleEntry.name = moduleName;
        moduleEntry.text = docToHtml(moduleName, moduleDoc);
        m_modules.append(moduleEntry);

        QString currentClass;

        for(int i = firstLine; i < lines.size(); )
        {
            const QString &raw = lines.at(i);
            QString trimmed = raw.trimmed();

            int indent = 0;
            while((indent < raw.size()) && (raw.at(indent) == QLatin1Char(' '))) indent += 1;

            i += 1;

            if(trimmed.isEmpty()
            || trimmed.startsWith(QLatin1Char('#'))
            || trimmed.startsWith(QStringLiteral("from "))
            || trimmed.startsWith(QStringLiteral("import "))
            || trimmed.startsWith(QStringLiteral("@overload"))
            || (trimmed == QStringLiteral("...")))
            {
                continue;
            }

            if(trimmed.startsWith(QStringLiteral("class ")) && (indent == 0))
            {
                int cut = trimmed.indexOf(QLatin1Char('('));
                if(cut == -1) cut = trimmed.indexOf(QLatin1Char(':'));
                if(cut == -1) continue;

                currentClass = trimmed.mid(6, cut - 6).trimmed();

                documentation_t d;
                d.moduleName = moduleName;
                d.className = QString();
                d.name = currentClass;
                d.text = docToHtml(moduleName + QStringLiteral(" - ") + currentClass, readDocstring(i));
                m_classes.append(d);
                providerClasses.append(currentClass);
            }
            else if(trimmed.startsWith(QStringLiteral("def ")))
            {
                QString name, args, returnType;

                if(!parseDef(trimmed, name, args, returnType))
                {
                    continue;
                }

                QString doc = readDocstring(i);

                if(indent == 0)
                {
                    currentClass = QString();

                    QStringList names = argumentNames(args, false);

                    if(providerFunctionArgs.value(name).size() <= names.size())
                    {
                        providerFunctionArgs.insert(name, names);
                    }

                    if(providerFunctions.isEmpty() || (providerFunctions.last() != name))
                    {
                        providerFunctions.append(name);
                    }

                    if(!doc.isEmpty())
                    {
                        documentation_t d;
                        d.moduleName = moduleName;
                        d.className = QString();
                        d.name = name;
                        d.text = docToHtml(moduleName + QStringLiteral(" - ") + name + QLatin1Char('(') + args + QLatin1Char(')'), doc);
                        m_functions.append(d);
                    }
                }
                else if(!currentClass.isEmpty())
                {
                    QStringList names = argumentNames(args, true);

                    if(name == QStringLiteral("__init__"))
                    {
                        if(providerClassArgs.value(currentClass).size() <= names.size())
                        {
                            providerClassArgs.insert(currentClass, names);
                        }

                        // Lift the constructor signature into the class tooltip.
                        if((!m_classes.isEmpty()) && (m_classes.last().name == currentClass))
                        {
                            QString ctorArgs = QString(args).remove(QRegularExpression(QStringLiteral("^self(,\\s*)?")));
                            m_classes.last().text.replace(QRegularExpression(QStringLiteral("^<h3>.*?</h3>")),
                                                          QStringLiteral("<h3>%1</h3>").arg(QString(moduleName + QStringLiteral(" - ") + currentClass
                                                                                            + QLatin1Char('(') + ctorArgs + QLatin1Char(')')).toHtmlEscaped()));
                        }
                    }
                    else
                    {
                        if(providerMethodArgs.value(name).size() <= names.size())
                        {
                            providerMethodArgs.insert(name, names);
                        }

                        if(providerMethods.isEmpty() || (providerMethods.last() != name))
                        {
                            providerMethods.append(name);
                        }

                        if(!doc.isEmpty())
                        {
                            documentation_t d;
                            d.moduleName = moduleName;
                            d.className = currentClass;
                            d.name = name;
                            d.text = docToHtml(moduleName + QStringLiteral(" - ") + currentClass + QLatin1Char('.') + name + QLatin1Char('(') + args + QLatin1Char(')'), doc);
                            m_methods.append(d);
                        }
                    }
                }
            }
            else if(trimmed.contains(QLatin1Char(':')))
            {
                QString name = trimmed.left(trimmed.indexOf(QLatin1Char(':'))).trimmed();

                if(name.isEmpty() || (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(name).hasMatch()))
                {
                    continue;
                }

                if(indent == 0)
                {
                    currentClass = QString();
                }

                QString doc = readDocstring(i);

                providerVariables.append(name);

                if(!doc.isEmpty())
                {
                    documentation_t d;
                    d.moduleName = moduleName;
                    d.className = (indent == 0) ? QString() : currentClass;
                    d.name = name;
                    d.text = docToHtml(moduleName + QStringLiteral(" - ") + (d.className.isEmpty() ? name : (d.className + QLatin1Char('.') + name)), doc);
                    m_datas.append(d);
                }
            }
        }
    }
}

bool OpenMVPlugin::loadDocs(bool update_resoruces, bool update_editors)
{
    m_modules = QList<documentation_t>();
    m_classes = QList<documentation_t>();
    m_datas = QList<documentation_t>();
    m_functions = QList<documentation_t>();
    m_methods = QList<documentation_t>();
    m_arguments = QSet<QString>();
    m_argumentsByHierarchy = QMap<QStringList, QStringList>();
    m_returnTypesByHierarchy = QMap<QStringList, QString>();

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

    // Newer documentation packages ship .pyi stubs (generated by openmv-doc's
    // tools/genpyi.py) alongside the html. When they are present, load
    // everything from the stubs and skip the html scraping and the
    // micropython-headers generation below - the stubs are both the parsing
    // source and the language server workspace. Older documentation packages
    // without stubs fall back to the original html parsing path unchanged.
    const Utils::FilePath stubsPath = Core::ICore::allUsersResourcePath(QStringLiteral("html/stubs"));
    const bool stubsAvailable = QDirIterator(stubsPath.toString(), QStringList() << QStringLiteral("*.pyi"), QDir::Files, QDirIterator::Subdirectories).hasNext();

    if(stubsAvailable)
    {
        loadStubs(stubsPath,
                  providerVariables,
                  providerClasses, providerClassArgs,
                  providerFunctions, providerFunctionArgs,
                  providerMethods, providerMethodArgs);
    }
    else
    {
        QDirIterator it(Core::ICore::allUsersResourcePath(QStringLiteral("html/library")).toString(), QDir::Files);

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
    }

    ///////////////////////////////////////////////////////////////////////////

    if (update_editors)
    {
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
    }

    ///////////////////////////////////////////////////////////////////////////

    if (update_editors)
    {
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
                                bool moduleNameMatch = false;
                                const QChar nextChar = widget->textDocument()->document()->characterAt(qMax(cursor.position(), cursor.anchor()));

                                for(const documentation_t &d : m_modules)
                                {
                                    if(d.name == text)
                                    {
                                        list.append(d.text);
                                        moduleNameMatch = true;
                                    }
                                }

                                // A known module name followed by '.' is that module being
                                // dereferenced (e.g. time.clock()) - same-named data and
                                // attribute entries from other modules do not apply.
                                if(!(moduleNameMatch && (nextChar == QLatin1Char('.'))))
                                {
                                    for(const documentation_t &d : m_datas)
                                    {
                                        if((d.name == text) && ((!moduleFilter) || (d.moduleName == maybeModuleName)))
                                        {
                                            list.append(d.text);
                                        }
                                    }
                                }

                                if(nextChar == QLatin1Char('('))
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

                                auto showOriginalToolTip = [globalPos, widget] (const QString &originalToolTip) {
                                    // The hover text is markdown-ish: ```python ...``` code fences
                                    // around signatures with plain-text documentation between them.
                                    // Render the fences as <pre> blocks and keep the documentation's
                                    // line structure (blank lines separate paragraphs).
                                    QString source = QString(originalToolTip).remove(QStringLiteral("\\")).trimmed();
                                    QString html;

                                    auto appendText = [&html] (const QString &chunk) {
                                        QString text = chunk.toHtmlEscaped().trimmed();

                                        if(!text.isEmpty())
                                        {
                                            text.replace(QStringLiteral("  "), QStringLiteral("&nbsp;&nbsp;"));
                                            text.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
                                            html.append(QStringLiteral("<p>") + text + QStringLiteral("</p>"));
                                        }
                                    };

                                    QRegularExpression fence(QStringLiteral("```(?:python)?\\s*(.+?)\\s*```"), QRegularExpression::DotMatchesEverythingOption);
                                    QRegularExpressionMatchIterator fences = fence.globalMatch(source);
                                    int pos = 0;

                                    while(fences.hasNext())
                                    {
                                        QRegularExpressionMatch match = fences.next();
                                        appendText(source.mid(pos, match.capturedStart() - pos));
                                        html.append(QStringLiteral("<pre>") + match.captured(1).toHtmlEscaped() + QStringLiteral("</pre>"));
                                        pos = match.capturedEnd();
                                    }

                                    appendText(source.mid(pos));
                                    Utils::ToolTip::show(globalPos, QStringLiteral("<table><tr><td style=\"padding:6px;\">") + html + QStringLiteral("</td></tr></table>"), widget);
                                };

                                if(!list.isEmpty())
                                {
                                    int index = originalToolTip.indexOf(QStringLiteral("<h3>"));
                                    QString cleanedToolTip = originalToolTip;

                                    if (index != -1)
                                    {
                                        cleanedToolTip = originalToolTip.mid(index).remove(QStringLiteral("\\"));
                                        list = QStringList() << cleanedToolTip;
                                    }
                                    else if (!originalToolTip.isEmpty() && (!((moduleFilter || moduleNameMatch) && (list.size() == 1))))
                                    {
                                        // The language server resolved the actual symbol under the
                                        // cursor, so its hover text is the right documentation.
                                        // The name-matched list below cannot tell same-named
                                        // symbols from different modules and classes apart, so
                                        // only fall back to it (as a grid of candidates) when no
                                        // hover text is available.
                                        //
                                        // Exceptions where our single matched entry is already
                                        // resolved and better than the server's:
                                        // - a module-qualified name (e.g. csi.RGB565): the server
                                        //   reports the docstring of the constant's type (int)
                                        //   rather than the constant's own documentation.
                                        // - a module name itself (e.g. time): for modules that
                                        //   share a name with the CPython standard library the
                                        //   server reports CPython's module docstring.
                                        showOriginalToolTip(originalToolTip);
                                        return;
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
                                else if(!originalToolTip.isEmpty())
                                {
                                    showOriginalToolTip(originalToolTip);
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
    }

    ///////////////////////////////////////////////////////////////////////////

    // With stubs available the language server points at html/stubs directly
    // (see Client::initialize()), so the micropython-headers generation is not
    // needed; the resource update has already deleted the stale folder.
    if (update_resoruces && (!stubsAvailable))
    {
        const Utils::FilePath &headers = Core::ICore::allUsersResourcePath(QStringLiteral("micropython-headers"));

        if(headers.exists())
        {
            QString error;

            if(!headers.removeRecursively(&error))
            {
                QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                return false;
            }
        }

        QDir().mkdir(headers.toString());

        for (const documentation_t &modules : m_modules)
        {
            if (modules.name == QStringLiteral("collections"))
            {
                continue;
            }

            Utils::FilePath path = headers;
            path = path.pathAppended(modules.name + QStringLiteral(".py"));
            QFile file(path.toString());

            if(file.open(QIODevice::WriteOnly))
            {
                QTextStream stream(&file);

                stream << "from __future__ import annotations\n";
                stream << "from typing import List, Tuple, Union, Any, Optional\n";

                if (modules.name != QStringLiteral("image"))
                {
                    stream << "import image\n";
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
                    }
                }

                file.close();
            }
        }
    }

    return true;
}

} // namespace Internal
} // namespace OpenMV
