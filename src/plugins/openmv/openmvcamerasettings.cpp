#include "openmvcamerasettings.h"
#include "ui_openmvcamerasettings.h"

#define SETTINGS_GROUP "BoardConfig"
#define REPL_UART "REPLUart"
#define WIFI_DEBUG "WiFiDebug"

#define WIFI_SETTINGS_GROUP "WiFiConfig"
#define WIFI_MODE "Mode"
#define CLIENT_MODE_SSID "ClientSSID"
#define CLIENT_MODE_PASS "ClientKey"
#define CLIENT_MODE_TYPE "ClientSecurity"
#define CLIENT_MODE_CHANNEL "ClientChannel"
#define ACCESS_POINT_MODE_SSID "AccessPointSSID"
#define ACCESS_POINT_MODE_PASS "AccessPointKey"
#define ACCESS_POINT_MODE_TYPE "AccessPointSecurity"
#define ACESSS_POINT_MODE_CHANNEL "AccessPointChannel"
#define BOARD_NAME "BoardName"

OpenMVCameraSettings::OpenMVCameraSettings(const QString &fileName, QWidget *parent) : QDialog(parent), m_settings(new QSettings(fileName, QSettings::IniFormat, this)), m_ui(new Ui::OpenMVCameraSettings)
{
    m_ui->setupUi(this);
    setWindowFlags(Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

    m_settings->beginGroup(QStringLiteral(SETTINGS_GROUP));
    bool repluart = m_settings->value(QStringLiteral(REPL_UART)).toBool();
    bool wifiDebug = m_settings->value(QStringLiteral(WIFI_DEBUG)).toBool();
    m_settings->endGroup();

    m_settings->beginGroup(QStringLiteral(WIFI_SETTINGS_GROUP));
    int wifiMode = m_settings->value(QStringLiteral(WIFI_MODE)).toInt();
    QString clientModeSSID = m_settings->value(QStringLiteral(CLIENT_MODE_SSID)).toString();
    QString clientModePass = m_settings->value(QStringLiteral(CLIENT_MODE_PASS)).toString();
    int clientModeType = m_settings->value(QStringLiteral(CLIENT_MODE_TYPE)).toInt();
    QString accessPointModeSSID = m_settings->value(QStringLiteral(ACCESS_POINT_MODE_SSID)).toString();
    QString accessPointModePass = m_settings->value(QStringLiteral(ACCESS_POINT_MODE_PASS)).toString();
    int accessPointModeType = m_settings->value(QStringLiteral(ACCESS_POINT_MODE_TYPE)).toInt();
    QString boardName = m_settings->value(QStringLiteral(BOARD_NAME)).toString();
    m_settings->endGroup();

    m_ui->wifiSettingsBox->setChecked(wifiDebug);
    m_ui->clientModeButton->setChecked(!wifiMode);
    m_ui->accessPointModeButton->setChecked(wifiMode);

    m_ui->clientModeSSIDEntry->addItem(clientModeSSID.isEmpty() ? tr("Please enter your WiFi network here") : clientModeSSID);
    m_ui->clientModePasswordEntry->setText(clientModePass);
    m_ui->clientModeTypeEntry->setCurrentIndex(((1 <= clientModeType) && (clientModeType <= 3)) ? (clientModeType - 1) : 0);

    m_ui->accessPointModeSSIDEntry->setText(accessPointModeSSID);
    m_ui->accessPointModePasswordEntry->setText(accessPointModePass);
    m_ui->accessPointModeTypeEntry->setCurrentIndex((accessPointModeType == 3) ? 1 : 0);

    m_ui->boardNameEntry->setText(boardName);

    m_ui->clientModeWidget->setEnabled(m_ui->wifiSettingsBox->isChecked() && m_ui->clientModeButton->isChecked());
    m_ui->accessPointModeWidget->setEnabled(m_ui->wifiSettingsBox->isChecked() && m_ui->accessPointModeButton->isChecked());

    connect(m_ui->wifiSettingsBox, &QGroupBox::clicked, this, [this] {
        m_ui->clientModeWidget->setEnabled(m_ui->wifiSettingsBox->isChecked() && m_ui->clientModeButton->isChecked());
        m_ui->accessPointModeWidget->setEnabled(m_ui->wifiSettingsBox->isChecked() && m_ui->accessPointModeButton->isChecked());
    });

    connect(m_ui->clientModeButton, &QRadioButton::toggled,
            m_ui->clientModeWidget, &QWidget::setEnabled);

    connect(m_ui->accessPointModeButton, &QRadioButton::toggled,
            m_ui->accessPointModeWidget, &QWidget::setEnabled);

    m_ui->replBox->setChecked(repluart);
}

OpenMVCameraSettings::~OpenMVCameraSettings()
{
    delete m_ui;
}

void OpenMVCameraSettings::accept()
{
    m_settings->beginGroup(QStringLiteral(SETTINGS_GROUP));
    m_settings->setValue(QStringLiteral(REPL_UART), m_ui->replBox->isChecked());
    m_settings->setValue(QStringLiteral(WIFI_DEBUG), m_ui->wifiSettingsBox->isChecked());
    m_settings->endGroup();

    m_settings->beginGroup(QStringLiteral(WIFI_SETTINGS_GROUP));
    m_settings->setValue(QStringLiteral(WIFI_MODE), m_ui->accessPointModeButton->isChecked() ? 1 : 0);
    m_settings->setValue(QStringLiteral(CLIENT_MODE_SSID), m_ui->clientModeSSIDEntry->currentText().left(32));
    m_settings->setValue(QStringLiteral(CLIENT_MODE_PASS), m_ui->clientModePasswordEntry->text().left(64));
    m_settings->setValue(QStringLiteral(CLIENT_MODE_TYPE), m_ui->clientModeTypeEntry->currentIndex() + 1);
    m_settings->setValue(QStringLiteral(CLIENT_MODE_CHANNEL), m_settings->value(QStringLiteral(CLIENT_MODE_CHANNEL), 2).toInt());
    m_settings->setValue(QStringLiteral(ACCESS_POINT_MODE_SSID), m_ui->accessPointModeSSIDEntry->text().left(32));
    m_settings->setValue(QStringLiteral(ACCESS_POINT_MODE_PASS), m_ui->accessPointModePasswordEntry->text().left(64));
    m_settings->setValue(QStringLiteral(ACCESS_POINT_MODE_TYPE), m_ui->accessPointModeTypeEntry->currentIndex() ? 3 : 1);
    m_settings->setValue(QStringLiteral(ACESSS_POINT_MODE_CHANNEL), m_settings->value(QStringLiteral(ACESSS_POINT_MODE_CHANNEL), 2).toInt());
    m_settings->setValue(QStringLiteral(BOARD_NAME), m_ui->boardNameEntry->text().left(32).isEmpty() ? QStringLiteral("OpenMV Cam") : m_ui->boardNameEntry->text().left(32));
    m_settings->endGroup();

    QDialog::accept();
}
