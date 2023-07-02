#include "tabbar.h"

#include "constants.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <utils/fsengine/fileiconprovider.h>
#include <coreplugin/idocument.h>
// OPENMV-DIFF //
// #include <projectexplorer/session.h>
// OPENMV-DIFF //
#include <coreplugin/icore.h>
// OPENMV-DIFF //

#include <QMenu>
#include <QMouseEvent>
#include <QShortcut>
#include <QTabBar>

// OPENMV-DIFF //
#include "tabbededitortr.h"
// OPENMV-DIFF //

using namespace Core::Internal;

using namespace TabbedEditor::Internal;

/// TODO: Use Core::DocumentModel for everything

TabBar::TabBar(QWidget *parent) :
    QTabBar(parent)
{
    setExpanding(false);
    setMovable(true);
    setTabsClosable(true);
    setUsesScrollButtons(true);
    // OPENMV-DIFF //
    setDrawBase(false);
    // OPENMV-DIFF //

    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Fixed);
    sp.setHorizontalStretch(1);
    sp.setVerticalStretch(0);
    sp.setHeightForWidth(sizePolicy().hasHeightForWidth());
    setSizePolicy(sp);

    connect(this, &QTabBar::tabMoved, [this](int from, int to) {
        m_editors.move(from, to);
    });

    Core::EditorManager *em = Core::EditorManager::instance();

    connect(em, &Core::EditorManager::editorOpened, this, &TabBar::addEditorTab);
    connect(em, &Core::EditorManager::editorsClosed, this, &TabBar::removeEditorTabs);
    connect(em, SIGNAL(currentEditorChanged(Core::IEditor*)), SLOT(selectEditorTab(Core::IEditor*)));

    connect(this, &QTabBar::currentChanged, this, &TabBar::activateEditor);
    connect(this, &QTabBar::tabCloseRequested, this, &TabBar::closeTab);

    // OPENMV-DIFF //
    // ProjectExplorer::SessionManager *sm = ProjectExplorer::SessionManager::instance();
    // connect(sm, &ProjectExplorer::SessionManager::sessionLoaded, [this, em]() {
    //     foreach (Core::DocumentModel::Entry *entry, Core::DocumentModel::entries())
    //         em->activateEditorForEntry(entry, Core::EditorManager::DoNotChangeCurrentEditor);
    // });
    // OPENMV-DIFF //

    // OPENMV-DIFF //
    // const QString shortCutSequence = QStringLiteral("Ctrl+Alt+%1");
    // for (int i = 1; i <= 10; ++i) {
    //     QShortcut *shortcut = new QShortcut(shortCutSequence.arg(i % 10), this);
    //     connect(shortcut, &QShortcut::activated, [this, shortcut]() {
    //         setCurrentIndex(m_shortcuts.indexOf(shortcut));
    //     });
    //     m_shortcuts.append(shortcut);
    // }
    // OPENMV-DIFF //

    QAction *prevTabAction = new QAction(QStringLiteral("Switch to previous tab"), this);
    Core::Command *prevTabCommand
            = Core::ActionManager::registerAction(prevTabAction,
                                                  TabbedEditor::Constants::PREV_TAB_ID,
                                                  Core::Context(Core::Constants::C_GLOBAL));
    // OPENMV-DIFF //
    // prevTabCommand->setDefaultKeySequence(QKeySequence(QStringLiteral("Ctrl+Shift+J")));
    // OPENMV-DIFF //
    prevTabCommand->setDefaultKeySequence(QKeySequence(QStringLiteral("Ctrl+Page Up")));
    // OPENMV-DIFF //

    connect(prevTabAction, SIGNAL(triggered()), this, SLOT(prevTabAction()));

    // OPENMV-DIFF //
    QAction *moveTabLeftAction = new QAction(Tr::tr("Move tab left"), this);
    Core::Command *moveTabLeftCommand
            = Core::ActionManager::registerAction(moveTabLeftAction,
                                                  TabbedEditor::Constants::MOVE_TAB_LEFT_ID,
                                                  Core::Context(Core::Constants::C_GLOBAL));
    moveTabLeftCommand->setDefaultKeySequence(QKeySequence(QStringLiteral("Ctrl+Shift+Page Up")));
    connect(moveTabLeftAction, SIGNAL(triggered()), this, SLOT(moveTabLeftAction()));
    // OPENMV-DIFF //

    QAction *nextTabAction = new QAction(Tr::tr("Switch to next tab"), this);
    Core::Command *nextTabCommand
            = Core::ActionManager::registerAction(nextTabAction,
                                                  TabbedEditor::Constants::NEXT_TAB_ID,
                                                  Core::Context(Core::Constants::C_GLOBAL));
    // OPENMV-DIFF //
    // nextTabCommand->setDefaultKeySequence(QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    // OPENMV-DIFF //
    nextTabCommand->setDefaultKeySequence(QKeySequence(QStringLiteral("Ctrl+Page Down")));
    // OPENMV-DIFF //

    connect(nextTabAction, SIGNAL(triggered()), this, SLOT(nextTabAction()));

    // OPENMV-DIFF //
    QAction *moveTabRightAction = new QAction(Tr::tr("Move tab right"), this);
    Core::Command *moveTabRightCommand
            = Core::ActionManager::registerAction(moveTabRightAction,
                                                  TabbedEditor::Constants::MOVE_TAB_RIGHT_ID,
                                                  Core::Context(Core::Constants::C_GLOBAL));
    moveTabRightCommand->setDefaultKeySequence(QKeySequence(QStringLiteral("Ctrl+Shift+Page Down")));
    connect(moveTabRightAction, SIGNAL(triggered()), this, SLOT(moveTabRightAction()));
    // OPENMV-DIFF //
}

void TabBar::activateEditor(int index)
{
    if (index < 0 || index >= m_editors.size())
        return;

    Core::EditorManager::instance()->activateEditor(m_editors[index]);
}

void TabBar::addEditorTab(Core::IEditor *editor)
{
    Core::IDocument *document = editor->document();

    const int index = addTab(document->displayName());
    // OPENMV-DIFF //
    // setTabIcon(index, Core::FileIconProvider::icon(document->filePath().toFileInfo()));
    // setTabToolTip(index, document->filePath().toString());
    // OPENMV-DIFF //
    QString path = document->filePath().toString();

    if(path.isEmpty())
    {
        setTabIcon(index, Utils::FileIconProvider::icon(Core::ICore::userResourcePath().pathAppended(QStringLiteral("/examples/OpenMV/01-Basics/helloworld.py"))));
        setTabToolTip(index, document->displayName());
    }
    else
    {
        setTabIcon(index, Utils::FileIconProvider::icon(document->filePath()));
        setTabToolTip(index, document->filePath().toString());
    }
    // OPENMV-DIFF //

    m_editors.append(editor);

    connect(document, &Core::IDocument::changed, [this, editor, document]() {
        const int index = m_editors.indexOf(editor);
        if (index == -1)
            return;
        QString tabText = document->displayName();
        if (document->isModified())
            tabText += QLatin1Char('*');
        setTabText(index, tabText);
    });
}

void TabBar::removeEditorTabs(QList<Core::IEditor *> editors)
{
    blockSignals(true); // Avoid calling activateEditor()
    foreach (Core::IEditor *editor, editors) {
        const int index = m_editors.indexOf(editor);
        if (index == -1)
            continue;
        m_editors.removeAt(index);
        removeTab(index);
    }
    blockSignals(false);
}

void TabBar::selectEditorTab(Core::IEditor *editor)
{
    const int index = m_editors.indexOf(editor);
    if (index == -1)
        return;
    setCurrentIndex(index);
}

void TabBar::closeTab(int index)
{
    if (index < 0 || index >= m_editors.size())
        return;
    Core::EditorManager::instance()->closeEditors(QList<Core::IEditor *>() << m_editors.at(index));
}

void TabBar::prevTabAction()
{
    int index = currentIndex();
    if (index >= 1)
        setCurrentIndex(index - 1);
    else
        setCurrentIndex(count() - 1);
}

// OPENMV-DIFF //
void TabBar::moveTabLeftAction()
{
    int index = currentIndex();
    if (index >= 1)
        moveTab(index, index - 1);
    else
        moveTab(index, count() - 1);
}
// OPENMV-DIFF //

void TabBar::nextTabAction()
{
    int index = currentIndex();
    if (index < count() - 1)
        setCurrentIndex(index + 1);
    else
        setCurrentIndex(0);
}

// OPENMV-DIFF //
void TabBar::moveTabRightAction()
{
    int index = currentIndex();
    if (index < count() - 1)
        moveTab(index, index + 1);
    else
        moveTab(index, 0);
}
// OPENMV-DIFF //

void TabBar::contextMenuEvent(QContextMenuEvent *event)
{
    const int index = tabAt(event->pos());
    if (index == -1)
        return;

    QScopedPointer<QMenu> menu(new QMenu());

    Core::IEditor *editor = m_editors[index];
    Core::DocumentModel::Entry *entry = Core::DocumentModel::entryForDocument(editor->document());
    Core::EditorManager::addSaveAndCloseEditorActions(menu.data(), entry, editor);
    menu->addSeparator();
    Core::EditorManager::addNativeDirAndOpenWithActions(menu.data(), entry);

    menu->exec(mapToGlobal(event->pos()));
}

void TabBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton)
        closeTab(tabAt(event->pos()));
    QTabBar::mouseReleaseEvent(event);
}
