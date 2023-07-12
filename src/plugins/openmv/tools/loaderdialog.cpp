#include "loaderdialog.h"

#include <coreplugin/fancytabwidget.h>
#include <coreplugin/mainwindow.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/theme/theme.h>

#include "openmvtr.h"

namespace OpenMV {
namespace Internal {

LoaderDialog::LoaderDialog(const QString &title,
                           const QString &details,
                           Utils::QtcProcess &process,
                           QSettings *settings,
                           const QString &settingsName,
                           QWidget *parent) : QDialog(parent), m_settings(settings), m_settingsName(settingsName)
{
    setWindowFlags(windowFlags() | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)) |
                   (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowTitle(details + QStringLiteral(" - ") + title);

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_warningLabel = new QLabel();
    m_warningLabel->setVisible(false);
    layout->addWidget(m_warningLabel);

    connect(&process, &Utils::QtcProcess::done, m_warningLabel, [this, layout] {
        m_warningLabel->setVisible(false);
        m_warningLabel->setText(QString());
        if (!m_detailsButton->isChecked()) {
            QTimer::singleShot(0, this, [this] { resize(width(), minimumSizeHint().height()); });
        }
    });

    QHBoxLayout *progressLayout = new QHBoxLayout();

    m_progressBarLabel = new QLabel(Tr::tr("Busy"));
    progressLayout->addWidget(m_progressBarLabel);

    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 0);
    progressLayout->addWidget(m_progressBar);

    m_detailsButton = new QToolButton();
    m_detailsButton->setText(Tr::tr("Show Details"));
    m_detailsButton->setCheckable(true);
    progressLayout->addWidget(m_detailsButton);

    layout->addLayout(progressLayout);

    m_plainTextEdit = new QPlainTextEdit();
    m_plainTextEdit->setReadOnly(true);
    m_plainTextEdit->setFont(TextEditor::TextEditorSettings::fontSettings().defaultFixedFontFamily());
    m_plainTextEdit->hide();

    QMainWindow *mainWindow = q_check_ptr(qobject_cast<QMainWindow *>(Core::ICore::mainWindow()));
    Core::Internal::FancyTabWidget *widget = qobject_cast<Core::Internal::FancyTabWidget *>(mainWindow->centralWidget());
    if(!widget) widget = qobject_cast<Core::Internal::FancyTabWidget *>(mainWindow->centralWidget()->layout()->itemAt(1)->widget()); // for tabbededitor
    widget = q_check_ptr(widget);
    m_plainTextEdit->setStyleSheet(widget->styleSheet());

    layout->addWidget(m_plainTextEdit);

    connect(m_detailsButton, &QToolButton::toggled, this, [this, layout] (bool checked) {
        if (!checked) m_maxHeight = height();
        m_detailsButton->setText(checked ? Tr::tr("Hide Details") : Tr::tr("Show Details"));
        m_plainTextEdit->setVisible(checked);
        setSizeGripEnabled(checked);
        if (!checked) {
            layout->addStretch(1);
            QTimer::singleShot(0, this, [this] { resize(width(), minimumSizeHint().height()); });
        } else {;
            layout->removeItem(layout->itemAt(layout->count() - 1)); // remove strech
            QTimer::singleShot(0, this, [this] { resize(width(), m_maxHeight); });
        }
    });

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, &LoaderDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &LoaderDialog::reject);
    layout->addWidget(box);

    layout->addStretch(1);

    QObject::connect(this, &LoaderDialog::rejected, [&process] { process.kill(); });

    if(m_settings->contains(m_settingsName))
    {
        QByteArray in = m_settings->value(m_settingsName).toByteArray();
        QDataStream s(&in, QIODeviceBase::ReadOnly);
        QByteArray g; s >> g;
        bool c; s >> c;
        s >> m_maxHeight;

        restoreGeometry(g);
        m_detailsButton->setChecked(c); // if c is false then this does nothing since there was no toggle

        if (!c) QTimer::singleShot(0, this, [this] { resize(width(), minimumSizeHint().height()); });
    }
    else
    {
        resize(640, minimumSizeHint().height());
        m_maxHeight = 480;
    }
}

LoaderDialog::~LoaderDialog()
{
    QByteArray out;
    QDataStream s(&out, QIODeviceBase::WriteOnly);
    s << saveGeometry();
    s << m_detailsButton->isChecked();
    s << (m_detailsButton->isChecked() ? height() : m_maxHeight);

    m_settings->setValue(m_settingsName, out);
}

void LoaderDialog::appendColoredText(const QString &text, bool warning)
{
    m_plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                           arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface)
                               ? (warning ? QStringLiteral("lightseagreen") : QStringLiteral("lightblue"))
                               : (warning ? QStringLiteral("seagreen") : QStringLiteral("blue"))).
                           arg(text));

    if(warning)
    {
        m_warningLabel->setVisible(true);
        m_warningLabel->setText(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface)
                                    ? QStringLiteral("lightseagreen")
                                    : QStringLiteral("seagreen")).
                                arg(text));
    }
}

} // namespace Internal
} // namespace OpenMV
