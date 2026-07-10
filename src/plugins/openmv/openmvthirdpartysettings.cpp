/* Copyright (C) 2026 OpenMV, LLC.
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

#include <QtCore>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>

#include "openmvthirdparty.h"
#include "openmvthirdpartysettings.h"
#include "openmvtr.h"

namespace OpenMV {
namespace Internal {

static bool viewerMode()
{
    return QCoreApplication::arguments().contains(QStringLiteral("-viewer_mode"));
}

static int updateParts()
{
    return OpenMVThirdParty::FirmwarePart | (viewerMode() ? 0 : OpenMVThirdParty::ExamplesPart);
}

static void offerRestart(QWidget *parent)
{
    if (QMessageBox::question(parent,
        Tr::tr("Third Party Repositories"),
        Tr::tr("Changes take effect after restarting.\n\nRestart %L1 now?").arg(QGuiApplication::applicationDisplayName()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
    {
        Core::ICore::restart();
    }
}

class OpenMVThirdPartySettingsWidget : public Core::IOptionsPageWidget
{
public:
    OpenMVThirdPartySettingsWidget()
    {
        QVBoxLayout *layout = new QVBoxLayout(this);

        QLabel *header = new QLabel(Tr::tr("Third party repositories add support for boards (and their firmware, "
                                           "examples, etc.) made by other companies to %L1.")
                                    .arg(QGuiApplication::applicationDisplayName()));
        header->setWordWrap(true);
        layout->addWidget(header);

        m_tree = new QTreeWidget;
        m_tree->setColumnCount(6);
        m_tree->setHeaderLabels(QStringList()
            << Tr::tr("Name") << Tr::tr("Id") << Tr::tr("Source")
            << Tr::tr("Firmware") << Tr::tr("Examples") << Tr::tr("Update URL"));
        m_tree->setRootIsDecorated(false);
        m_tree->setAllColumnsShowFocus(true);
        m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
        m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(m_tree);

        QHBoxLayout *buttons = new QHBoxLayout;
        m_installButton = new QPushButton(Tr::tr("Install from URL..."));
        m_removeButton = new QPushButton(Tr::tr("Remove"));
        m_updateButton = new QPushButton(Tr::tr("Check for Updates"));
        buttons->addWidget(m_installButton);
        buttons->addWidget(m_removeButton);
        buttons->addWidget(m_updateButton);
        buttons->addStretch();
        layout->addLayout(buttons);

        m_overridesBox = new QGroupBox(Tr::tr("Override warnings"));
        QVBoxLayout *overridesLayout = new QVBoxLayout(m_overridesBox);
        m_overridesList = new QPlainTextEdit;
        m_overridesList->setReadOnly(true);
        overridesLayout->addWidget(m_overridesList);
        layout->addWidget(m_overridesBox);

        connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] { updateButtons(); });
        connect(m_installButton, &QPushButton::clicked, this, [this] { installClicked(); });
        connect(m_removeButton, &QPushButton::clicked, this, [this] { removeClicked(); });
        connect(m_updateButton, &QPushButton::clicked, this, [this] {
            OpenMVThirdParty::checkAndPrompt(this, updateParts(), true);
        });

        refresh();
    }

    // Everything on this page is immediate and file-driven.
    void apply() final { }

private:
    void refresh()
    {
        m_tree->clear();
        m_repos = OpenMVThirdParty::scanRepos();

        for (const OpenMVThirdParty::Repo &repo : std::as_const(m_repos))
        {
            QTreeWidgetItem *item = new QTreeWidgetItem(m_tree, QStringList()
                << repo.displayName
                << repo.id
                << (repo.fromInstallDir ? Tr::tr("Built-in") : Tr::tr("User"))
                << (repo.firmwareVersion.isEmpty() ? QStringLiteral("-") : repo.firmwareVersion)
                << (repo.examplesVersion.isEmpty() ? QStringLiteral("-") : repo.examplesVersion)
                << (repo.configUrl.isEmpty()
                    ? (repo.fromInstallDir ? Tr::tr("<Built-In>") : Tr::tr("<None>"))
                    : repo.configUrl));
            item->setData(0, Qt::UserRole, repo.id);
        }

        for (int i = 0; i < m_tree->columnCount(); i++)
        {
            m_tree->resizeColumnToContents(i);
            m_tree->setColumnWidth(i, m_tree->columnWidth(i) + 30);
        }

        QStringList lines = OpenMVThirdParty::overridesText(OpenMVThirdParty::mergedOverrides());
        m_overridesBox->setVisible(!lines.isEmpty());
        m_overridesList->setPlainText(lines.join(QStringLiteral("\n")));

        updateButtons();
    }

    const OpenMVThirdParty::Repo *selectedRepo() const
    {
        QTreeWidgetItem *item = m_tree->currentItem();

        if (item)
        {
            QString id = item->data(0, Qt::UserRole).toString();

            for (const OpenMVThirdParty::Repo &repo : m_repos)
            {
                if (repo.id == id)
                {
                    return &repo;
                }
            }
        }

        return Q_NULLPTR;
    }

    void updateButtons()
    {
        const OpenMVThirdParty::Repo *repo = selectedRepo();
        m_removeButton->setEnabled(repo && (!repo->fromInstallDir));

        bool anyUpdatable = false;

        for (const OpenMVThirdParty::Repo &r : m_repos)
        {
            anyUpdatable = anyUpdatable || (!r.configUrl.isEmpty());
        }

        m_updateButton->setEnabled(anyUpdatable);
    }

    void installClicked()
    {
        Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

        bool ok;
        QString text = QInputDialog::getText(this,
            Tr::tr("Install from URL"),
            Tr::tr("URL of the repository's config.json file:"),
            QLineEdit::Normal,
            settings->value(LAST_THIRD_PARTY_INSTALL_URL).toString(),
            &ok,
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint).trimmed();

        if ((!ok) || text.isEmpty())
        {
            return;
        }

        settings->setValue(LAST_THIRD_PARTY_INSTALL_URL, text);
        settings->sync();

        QString repoId, error;

        if (!OpenMVThirdParty::installFromUrl(QUrl(text), false, &repoId, &error, this))
        {
            // The id is known once the config downloaded - offer to reinstall
            // an existing user repo (never one shipped in the install dir).
            if ((!repoId.isEmpty())
            && OpenMVThirdParty::writableRoot().pathAppended(repoId).exists()
            && (!OpenMVThirdParty::installRoot().pathAppended(repoId).exists()))
            {
                if (QMessageBox::question(this,
                    Tr::tr("Install from URL"),
                    Tr::tr("A repository named \"%L1\" is already installed. Reinstall it?").arg(repoId),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
                {
                    return;
                }

                error = QString();

                if (!OpenMVThirdParty::installFromUrl(QUrl(text), true, &repoId, &error, this))
                {
                    QMessageBox::critical(this, Tr::tr("Install from URL"), error);
                    refresh();
                    return;
                }
            }
            else
            {
                QMessageBox::critical(this, Tr::tr("Install from URL"), error);
                refresh();
                return;
            }
        }

        refresh();
        offerRestart(this);
    }

    void removeClicked()
    {
        const OpenMVThirdParty::Repo *repo = selectedRepo();

        if (!repo)
        {
            return;
        }

        if (QMessageBox::question(this,
            Tr::tr("Remove Repository"),
            Tr::tr("Remove \"%L1\"?\n\nThis deletes its boards, firmware, and examples.").arg(repo->displayName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        {
            return;
        }

        QString error;

        if (!OpenMVThirdParty::removeRepo(repo->id, &error))
        {
            QMessageBox::critical(this, Tr::tr("Remove Repository"), error);
            refresh();
            return;
        }

        refresh();
        offerRestart(this);
    }

    QTreeWidget *m_tree;
    QPushButton *m_installButton;
    QPushButton *m_removeButton;
    QPushButton *m_updateButton;
    QGroupBox *m_overridesBox;
    QPlainTextEdit *m_overridesList;
    QList<OpenMVThirdParty::Repo> m_repos;
};

OpenMVThirdPartySettingsPage::OpenMVThirdPartySettingsPage()
{
    setId("OpenMV.ThirdPartyRepositories");
    setDisplayName(Tr::tr("Third Party Repositories"));
    setCategory("ZZ.OpenMV");
    setDisplayCategory(Tr::tr("OpenMV"));
    setCategoryIconPath(Utils::FilePath::fromString(QStringLiteral(":/openmv/images/settingscategory_openmv.png")));
    setWidgetCreator([] { return new OpenMVThirdPartySettingsWidget; });
}

// Self-registers the page with the options dialog (Qt Creator pattern).
static const OpenMVThirdPartySettingsPage thirdPartySettingsPage;

} // namespace Internal
} // namespace OpenMV
