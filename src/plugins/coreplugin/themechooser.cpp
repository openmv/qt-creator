// Copyright (C) 2016 Thorben Kroeger <thorbenkroeger@gmail.com>.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "themechooser.h"

#include "coreconstants.h"
#include "coreplugintr.h"
#include "icore.h"

#include <utils/algorithm.h>
#include <utils/theme/theme.h>
#include <utils/theme/theme_p.h>

#include <QAbstractListModel>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSpacerItem>

using namespace Utils;

namespace Core::Internal {

const char themeNameKey[] = "ThemeName";
// OPENMV-DIFF //
// Synthetic theme id that follows the host's system color scheme at startup,
// resolving to flat-light or flat-dark.
const char themeAutoId[] = "auto";
// OPENMV-DIFF //

ThemeEntry::ThemeEntry(Id id, const QString &filePath)
    : m_id(id)
    , m_filePath(filePath)
{}

Id ThemeEntry::id() const
{
    return m_id;
}

QString ThemeEntry::displayName() const
{
    // OPENMV-DIFF //
    if (m_id == Id(themeAutoId))
        return Tr::tr("Auto");
    // OPENMV-DIFF //
    if (m_displayName.isEmpty() && !m_filePath.isEmpty()) {
        QSettings settings(m_filePath, QSettings::IniFormat);
        m_displayName = settings.value(QLatin1String(themeNameKey),
                                       Tr::tr("unnamed")).toString();
    }
    return m_displayName;
}

QString ThemeEntry::filePath() const
{
    return m_filePath;
}

class ThemeListModel final : public QAbstractListModel
{
public:
    ThemeListModel() = default;

    int rowCount(const QModelIndex &parent) const final
    {
        return parent.isValid() ? 0 : m_themes.size();
    }

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (role == Qt::DisplayRole)
            return m_themes.at(index.row()).displayName();
        return QVariant();
    }

    void removeTheme(int index)
    {
        beginRemoveRows(QModelIndex(), index, index);
        m_themes.removeAt(index);
        endRemoveRows();
    }

    void setThemes(const QList<ThemeEntry> &themes)
    {
        beginResetModel();
        m_themes = themes;
        endResetModel();
    }

    const ThemeEntry &themeAt(int index) const
    {
        return m_themes.at(index);
    }

private:
    QList<ThemeEntry> m_themes;
};

class ThemeChooserPrivate
{
public:
    ThemeListModel m_themeListModel;
    QComboBox *m_themeComboBox;
};

ThemeChooser::ThemeChooser()
   : d(new ThemeChooserPrivate)
{
    d->m_themeComboBox = new QComboBox;

    const QList<ThemeEntry> themes = ThemeEntry::availableThemes();
    d->m_themeListModel.setThemes(themes);

    d->m_themeComboBox->setModel(&d->m_themeListModel);
    const int selected =
            Utils::indexOf(themes, Utils::equal(&ThemeEntry::id, ThemeEntry::themeSetting()));
    if (selected >= 0)
        d->m_themeComboBox->setCurrentIndex(selected);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(d->m_themeComboBox);
    layout->addWidget(new QLabel(Tr::tr("Current theme: %1").arg(creatorTheme()->displayName())));
    layout->addSpacerItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));
}

ThemeChooser::~ThemeChooser()
{
    delete d;
}

QComboBox *ThemeChooser::themeComboBox() const
{
    return d->m_themeComboBox;
}

Id ThemeEntry::defaultThemeId()
{
    // OPENMV-DIFF //
    // OpenMV ships only flat-light / flat-dark, so the upstream
    // light-2024 / dark-2024 / flat defaults do not exist here. Default to
    // flat-dark; "Auto" (follow the system color scheme) stays selectable.
    // return Theme::systemColorScheme() == Qt::ColorScheme::Light ? "light-2024"
    //      : Theme::systemColorScheme() == Qt::ColorScheme::Dark  ? "dark-2024"
    //                                                             : "flat";
    return "flat-dark";
    // OPENMV-DIFF //
}

void ThemeChooser::apply()
{
    const int index = d->m_themeComboBox->currentIndex();
    if (index == -1)
        return;
    const QString themeId = d->m_themeListModel.themeAt(index).id().toString();
    QtcSettings *settings = ICore::settings();
    const QString currentThemeId = ThemeEntry::themeSetting().toString();
    if (currentThemeId != themeId) {
        // save filename of selected theme in global config
        settings->setValueWithDefault(
            Constants::SETTINGS_THEME, themeId, ThemeEntry::defaultThemeId().toString());
        ICore::askForRestart(Tr::tr("The theme change will take effect after restart."));
    }
}

bool ThemeChooser::isDirty() const
{
    const int index = d->m_themeComboBox->currentIndex();
    if (index == -1)
        return false;
    const QString themeId = d->m_themeListModel.themeAt(index).id().toString();
    const QString currentThemeId = ThemeEntry::themeSetting().toString();
    return currentThemeId != themeId;
}

static void addThemesFromPath(const QString &path, QList<ThemeEntry> *themes)
{
    static const QLatin1String extension("*.creatortheme");
    QDir themeDir(path);
    themeDir.setNameFilters({extension});
    themeDir.setFilter(QDir::Files);
    const QStringList themeList = themeDir.entryList();
    for (const QString &fileName : std::as_const(themeList)) {
        QString id = QFileInfo(fileName).completeBaseName();
        // OPENMV-DIFF //
        // OpenMV ships only the flat-light / flat-dark themes; skip any others.
        // (The isQtDesignStudio() design-theme branch from the 14.0.2 base is
        // obsolete -- upstream 20.0.2 removed it.)
        if ((id != QStringLiteral("flat-light")) && (id != QStringLiteral("flat-dark"))) continue;
        // OPENMV-DIFF //
        themes->append(ThemeEntry(Id::fromString(id), themeDir.absoluteFilePath(fileName)));
    }
}

QList<ThemeEntry> ThemeEntry::availableThemes()
{
    QList<ThemeEntry> themes;

    static const FilePath installThemeDir = ICore::resourcePath("themes");
    // OPENMV-DIFF //
    // OpenMV loads only install themes, never user themes.
    // static const FilePath userThemeDir = ICore::userResourcePath("themes");
    // OPENMV-DIFF //
    addThemesFromPath(installThemeDir.toUrlishString(), &themes);
    if (themes.isEmpty())
        qWarning() << "Warning: No themes found in installation: "
                   << installThemeDir.toUserOutput();
    // OPENMV-DIFF //
    // Prepend the synthetic "Auto" entry so it leads the selector;
    // it follows the system color scheme instead of a fixed theme. This replaces
    // the upstream "move default theme to front" reorder below.
    // const int defaultIndex = Utils::indexOf(themes, Utils::equal(&ThemeEntry::id, defaultThemeId()));
    // if (defaultIndex > 0) { // == exists and not at front
    //     ThemeEntry defaultEntry = themes.takeAt(defaultIndex);
    //     themes.prepend(defaultEntry);
    // }
    if (!themes.isEmpty())
        themes.prepend(ThemeEntry(Id(themeAutoId), QString()));
    // addThemesFromPath(userThemeDir.toUrlishString(), &themes);
    // OPENMV-DIFF //
    return themes;
}

Id ThemeEntry::themeSetting()
{
    const Id setting = Id::fromSetting(
        ICore::settings()->value(Constants::SETTINGS_THEME, defaultThemeId().toString()));

    const QList<ThemeEntry> themes = availableThemes();
    if (themes.empty())
        return Id();
    const bool settingValid = Utils::contains(themes, Utils::equal(&ThemeEntry::id, setting));

    // OPENMV-DIFF //
    // return settingValid ? setting : themes.first().id();
    // OPENMV-DIFF //
    // The first entry is "Auto", not the default, so fall back to the default.
    if (settingValid)
        return setting;
    return Utils::contains(themes, Utils::equal(&ThemeEntry::id, defaultThemeId()))
               ? defaultThemeId()
               : themes.first().id();
    // OPENMV-DIFF //
}

Theme *ThemeEntry::createTheme(Id id)
{
    if (!id.isValid())
        return nullptr;
    // OPENMV-DIFF //
    // Resolve the synthetic "Auto" id to the flat theme matching the system
    // color scheme at startup (dark otherwise defaults to light).
    if (id == Id(themeAutoId)) {
        id = (Theme::systemColorScheme() == Qt::ColorScheme::Dark) ? Id("flat-dark")
                                                                   : Id("flat-light");
    }
    // OPENMV-DIFF //
    const ThemeEntry entry = Utils::findOrDefault(availableThemes(),
                                                  Utils::equal(&ThemeEntry::id, id));
    if (!entry.id().isValid())
        return nullptr;
    QSettings themeSettings(entry.filePath(), QSettings::IniFormat);
    Theme *theme = new Theme(entry.id().toString());
    theme->readSettings(themeSettings);
    return theme;
}

} // namespace Core::Internal
