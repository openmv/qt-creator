#include "edgeimpulse.h"

static void uploadProject(const QString &apiKey, const QString &path)
{

}

QString loggedIntoEdgeImpulse()
{
    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(EDGE_IMPULSE_SETTINGS_GROUP));
    bool ok = !settings->value(QStringLiteral(LAST_JWT_TOKEN)).toString().isEmpty();
    QString accountName = settings->value(QStringLiteral(LAST_JWT_TOKEN_EMAIL)).toString();
    settings->endGroup();
    return ok ? accountName : QString();
}

void loginToEdgeImpulse()
{
    QNetworkAccessManager *manager = new QNetworkAccessManager();

    QObject::connect(manager, &QNetworkAccessManager::finished, [=] (QNetworkReply *reply) {
        QByteArray data = reply->readAll();

        if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
        {
            QJsonObject body = QJsonDocument::fromJson(data).object();
            QString token = body.value(QStringLiteral("token")).toString();
            bool success = body.value(QStringLiteral("success")).toBool();
            QString error = body.value(QStringLiteral("error")).toString();

            if(success)
            {
                QSettings *settings = ExtensionSystem::PluginManager::settings();
                settings->beginGroup(QStringLiteral(EDGE_IMPULSE_SETTINGS_GROUP));
                settings->setValue(QStringLiteral(LAST_JWT_TOKEN), token);
                settings->endGroup();
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    QObject::tr("Edge Impulse Login"),
                    error);
            }
        }

        reply->deleteLater();
    });

    QDialog dialog(Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog.setWindowTitle(QObject::tr("Edge Impulse Login"));
    QFormLayout *layout = new QFormLayout(&dialog);

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(EDGE_IMPULSE_SETTINGS_GROUP));

    QLineEdit *usernameBox = new QLineEdit(settings->value(QStringLiteral(LAST_JWT_TOKEN_EMAIL)).toString());
    usernameBox->setPlaceholderText(QObject::tr("Email Address"));
    layout->addRow(QObject::tr("Username"), usernameBox);

    QLineEdit *passwordBox = new QLineEdit();
    passwordBox->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    passwordBox->setPlaceholderText(QObject::tr("********************"));
    layout->addRow(QObject::tr("Password"), passwordBox);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);

    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if(dialog.exec() == QDialog::Accepted)
    {
        settings->setValue(QStringLiteral(LAST_JWT_TOKEN_EMAIL), usernameBox->text());

        QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("https://studio.edgeimpulse.com/v1/api-login")));
        request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
        request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif

        QJsonObject body;
        body.insert(QStringLiteral("username"), usernameBox->text());
        body.insert(QStringLiteral("password"), passwordBox->text());
        QNetworkReply *reply = manager->post(request, QJsonDocument(body).toJson());

        if(reply)
        {
            QObject::connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
        }
    }

    settings->endGroup();
}

void logoutFromEdgeImpulse()
{
    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(EDGE_IMPULSE_SETTINGS_GROUP));
    settings->setValue(QStringLiteral(LAST_JWT_TOKEN), QString());
    settings->endGroup();
}

void uploadToSelectedProject(const QString &path)
{

}

void uploadProjectByAPIKey(const QString &path)
{
    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(EDGE_IMPULSE_SETTINGS_GROUP));

    bool ok;
    QString apiKey = QInputDialog::getText(Core::ICore::dialogParent(),
        QObject::tr("Upload Project"), QObject::tr("Please enter an Edge Impluse Project API Key"),
        QLineEdit::Normal, settings->value(QStringLiteral(LAST_API_KEY)).toString(), &ok,
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

    if(ok && (!apiKey.isEmpty()))
    {
        settings->setValue(QStringLiteral(LAST_API_KEY), apiKey);

        uploadProject(apiKey, path);
    }

    settings->endGroup();
}
