#include "edgeimpulse.h"

static void uploadProject(const QString &apiKey, const QString &hmacKey, OpenMVDatasetEditor *editor)
{
    QDialog dialog(Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog.setWindowTitle(QObject::tr("Dataset Split"));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel(QObject::tr("Please choose how to split the data to upload.\nOpenMV recommends leaving this at the default 80/20% split.")));

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(EDGE_IMPULSE_SETTINGS_GROUP));

    int split = settings->value(QStringLiteral(LAST_TRAIN_TEST_SPLIT), 80).toInt();

    QHBoxLayout *layout2 = new QHBoxLayout();

    QLabel *trainLabel = new QLabel(QObject::tr("Training Data\nPercentage\n%1%").arg(split));
    layout2->addWidget(trainLabel);

    QSlider *splitSlider = new QSlider(Qt::Horizontal);
    splitSlider->setRange(0, 100);
    splitSlider->setValue(split);
    layout2->addWidget(splitSlider);

    QLabel *testLabel =  new QLabel(QObject::tr("Test Data\nPercentage\n%1%").arg(100 - split));
    layout2->addWidget(testLabel);

    QWidget *temp = new QWidget();
    temp->setLayout(layout2);
    layout->addWidget(temp);

    QObject::connect(splitSlider, &QSlider::valueChanged, [trainLabel, testLabel] (int value) {
        trainLabel->setText(QObject::tr("Training Data\nPercentage\n%1%").arg(value));
        testLabel->setText(QObject::tr("Test Data\nPercentage\n%1%").arg(100 - value));
    });

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(box);

    bool ok = dialog.exec() == QDialog::Accepted;

    if(ok)
    {
        settings->setValue(QStringLiteral(LAST_TRAIN_TEST_SPLIT), splitSlider->value());
    }

    settings->endGroup();

    if(ok)
    {
        // https://stackoverflow.com/a/51848922

        QString localHostName = QHostInfo::localHostName();
        QString localHostIP;

        QList<QHostAddress> hostList = QHostInfo::fromName(localHostName).addresses();

        foreach(const QHostAddress& address, hostList) {
           if((address.protocol() == QAbstractSocket::IPv4Protocol) && ((address.toIPv4Address() & 0xff) != 0x01) && (!address.isLoopback())) {
                localHostIP = address.toString();
                break;
           }
        }

        QString localMacAddress;
        QString localSubnetMask;

        foreach(const QNetworkInterface& networkInterface, QNetworkInterface::allInterfaces()) {
           foreach(const QNetworkAddressEntry& entry, networkInterface.addressEntries()) {
               if(entry.ip().toString() == localHostIP) {
                   localMacAddress = networkInterface.hardwareAddress();
                   localSubnetMask = entry.netmask().toString();
                   break;
               }
           }
        }

        QList< QPair<QString, QString> > list;

        foreach(const QString &className, editor->classFolderList())
        {
            foreach(const QString &snapshotName, editor->snapshotList(className))
            {
                list.append(QPair<QString, QString>(QDir::cleanPath(QDir::fromNativeSeparators(editor->rootPath() + QDir::separator() + className + QDir::separator() + snapshotName)), QString(className).remove(QStringLiteral(".class")) + QLatin1Char('.') + snapshotName));
            }
        }

        if(!list.isEmpty())
        {
            QProgressDialog *progress = new QProgressDialog(QObject::tr("Uploading..."), QObject::tr("Cancel"), 0, list.size() - 1, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));

            progress->setWindowModality(Qt::ApplicationModal);
            progress->show();

            QNetworkAccessManager *manager = new QNetworkAccessManager();

            int reply_count = 0;
            int new_count = 0;
            int dup_count = 0;
            int *reply_count_ptr = &reply_count;
            int *new_count_ptr = &new_count;
            int *dup_count_ptr = &dup_count;

            QObject::connect(manager, &QNetworkAccessManager::finished, [progress, reply_count_ptr, new_count_ptr, dup_count_ptr] (QNetworkReply *reply) {
                *reply_count_ptr += 1;

                if(!progress->wasCanceled())
                {
                    if(reply->error() != QNetworkReply::NoError)
                    {
                        QByteArray data = reply->readAll();

                        if(!data.startsWith(QByteArrayLiteral("An item with this hash already exists")))
                        {
                            progress->cancel();

                            QMessageBox::critical(Core::ICore::dialogParent(),
                                QObject::tr("Uploading Dataset"),
                                reply->errorString());
                        }
                        else
                        {
                            progress->setValue(progress->value() + 1);
                            *dup_count_ptr += 1;
                        }
                    }
                    else
                    {
                        progress->setValue(progress->value() + 1);
                        *new_count_ptr += 1;
                    }
                }

                reply->deleteLater();
            });

            QPair<QString, QString> pair; foreach(pair, list)
            {
                QFile file(pair.first);

                if(file.open(QIODevice::ReadOnly))
                {
                    QByteArray fileData;
                    QBuffer buffer(&fileData);

                    buffer.open(QIODevice::WriteOnly);
                    QImage::fromData(file.readAll()).save(&buffer, "JPG");
                    buffer.close();

                    QByteArray hash = QCryptographicHash::hash(fileData, QCryptographicHash::Md5);
                    int index = (hash[0] + (hash[1] << 8)) % 100;
                    QString endpoint = (index >= splitSlider->value()) ? QStringLiteral("testing") : QStringLiteral("training");

                    QNetworkRequest request = QNetworkRequest(QUrl(QString(QStringLiteral("https://ingestion.edgeimpulse.com/api/%1/data")).arg(endpoint)));
                    request.setRawHeader(QByteArrayLiteral("x-api-key"), apiKey.toUtf8());
                    request.setRawHeader(QByteArrayLiteral("x-file-name"), pair.second.toUtf8());
                    request.setRawHeader(QByteArrayLiteral("x-label"), QFileInfo(pair.second).baseName().toUtf8());
                    request.setRawHeader(QByteArrayLiteral("x-disallow-duplicates"), QByteArrayLiteral("1"));

                    QJsonObject protectedObj;
                    protectedObj.insert(QStringLiteral("ver"), QStringLiteral("v1"));
                    protectedObj.insert(QStringLiteral("alg"), QStringLiteral("HS256"));
    #ifdef Q_OS_WIN
                    protectedObj.insert(QStringLiteral("iat"), QFileInfo(pair.first).lastModified().toSecsSinceEpoch());
    #else
                    protectedObj.insert(QStringLiteral("iat"), QFileInfo(pair.first).lastModified().toMSecsSinceEpoch() / 1000);
    #endif

                    QJsonObject sensorsObject;
                    sensorsObject.insert(QStringLiteral("name"), QStringLiteral("image"));
                    sensorsObject.insert(QStringLiteral("units"), QStringLiteral("rgba"));

                    QJsonObject metadataObj;
                    metadataObj.insert(QStringLiteral("hostname"), localHostName);
                    metadataObj.insert(QStringLiteral("ip"), localHostIP);
                    metadataObj.insert(QStringLiteral("username"), Utils::Environment::systemEnvironment().toDictionary().userName());

                    QJsonObject payloadObj;
                    payloadObj.insert(QStringLiteral("device_name"), localMacAddress);
                    payloadObj.insert(QStringLiteral("device_type"), QStringLiteral("OPENMV_IDE"));
                    payloadObj.insert(QStringLiteral("interval_ms"), 0);
                    payloadObj.insert(QStringLiteral("sensors"), QJsonArray() << sensorsObject);
                    payloadObj.insert(QStringLiteral("values"), QJsonArray() << QJsonValue(QString(QStringLiteral("Ref-BINARY-image/jpeg (%1 bytes) %2")).arg(fileData.size()).arg(QString::fromUtf8(QCryptographicHash::hash(fileData, QCryptographicHash::Sha256).toHex()))));
                    payloadObj.insert(QStringLiteral("metadata"), metadataObj);

                    QJsonObject temp;
                    temp.insert(QStringLiteral("protected"), protectedObj);
                    temp.insert(QStringLiteral("signature"), QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000"));
                    temp.insert(QStringLiteral("payload"), payloadObj);

                    QJsonObject body;
                    body.insert(QStringLiteral("protected"), protectedObj);
                    body.insert(QStringLiteral("signature"), QString::fromUtf8(QMessageAuthenticationCode::hash(QJsonDocument(temp).toJson(QJsonDocument::Compact), hmacKey.toUtf8(), QCryptographicHash::Sha256).toHex()));
                    body.insert(QStringLiteral("payload"), payloadObj);

                    QHttpMultiPart *parts = new QHttpMultiPart(QHttpMultiPart::FormDataType);

                    QHttpPart jsonPart;
                    jsonPart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
                    jsonPart.setHeader(QNetworkRequest::ContentDispositionHeader, QString(QStringLiteral("form-data; name=\"attachments[]\"; filename=\"metadata.json\"")));
                    jsonPart.setBody(QJsonDocument(body).toJson(QJsonDocument::Compact));
                    parts->append(jsonPart);

                    QHttpPart imagePart;
                    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/jpeg"));
                    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader, QString(QStringLiteral("form-data; name=\"attachments[]\"; filename=\"%1.jpg\"")).arg(QFileInfo(pair.second).completeBaseName()));
                    imagePart.setBody(fileData);
                    parts->append(imagePart);

                    QNetworkReply *reply = manager->post(request, parts);

                    if(reply)
                    {
                        QObject::connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
                        parts->setParent(reply);

                        QApplication::processEvents();
                    }
                    else
                    {
                        progress->cancel();
                        delete parts;

                        QMessageBox::critical(Core::ICore::dialogParent(),
                            QObject::tr("Uploading Dataset"),
                            QObject::tr("Error posting!"));
                    }
                }
                else
                {
                    progress->cancel();

                    QMessageBox::critical(Core::ICore::dialogParent(),
                        QObject::tr("Uploading Dataset"),
                        QObject::tr("Error: %L1!").arg(file.errorString()));
                }

                if(progress->wasCanceled())
                {
                    break;
                }
            }

            while((!progress->wasCanceled()) && progress->isVisible())
            {
                QApplication::processEvents();
            }

            delete manager;
            delete progress;

            QMessageBox::information(Core::ICore::dialogParent(),
                QObject::tr("Uploading Dataset"),
                QObject::tr("Upload Statistics:\n\n"
                            "%L1 Files Uploaded\n"
                            "%L2 Responses from Edge Impulse\n\n"
                            "%L3 New Images Added\n"
                            "%L4 Marked as Duplicates").arg(list.size()).arg(reply_count).arg(new_count).arg(dup_count));
        }
        else
        {
            QMessageBox::information(Core::ICore::dialogParent(),
                QObject::tr("Uploading Dataset"),
                QObject::tr("Nothing to upload\n\n"
                            "Only jpg/png/bmp images with a numeric name (e.g. \"00001.jpg\")\nin class folders (\"*.class\") can be uploaded."));
        }
    }
}

static void uploadProject(const QString &apiKey, OpenMVDatasetEditor *editor)
{
    QNetworkAccessManager *manager = new QNetworkAccessManager();

    QObject::connect(manager, &QNetworkAccessManager::finished, [manager, apiKey, editor] (QNetworkReply *reply) {
        QByteArray data = reply->readAll();

        if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
        {
            QJsonObject body = QJsonDocument::fromJson(data).object();
            bool success = body.value(QStringLiteral("success")).toBool();
            QString error = body.value(QStringLiteral("error")).toString();

            if(success)
            {
                QJsonArray array = body.value(QStringLiteral("projects")).toArray();

                if(array.size() == 1)
                {
                    QNetworkAccessManager *manager2 = new QNetworkAccessManager();

                    QObject::connect(manager2, &QNetworkAccessManager::finished, [manager2, apiKey, editor] (QNetworkReply *reply2) {
                        QByteArray data2 = reply2->readAll();

                        if((reply2->error() == QNetworkReply::NoError) && (!data2.isEmpty()))
                        {
                            QJsonObject body2 = QJsonDocument::fromJson(data2).object();
                            bool success2 = body2.value(QStringLiteral("success")).toBool();
                            QString error2 = body2.value(QStringLiteral("error")).toString();

                            if(success2)
                            {
                                uploadProject(body2.value(QStringLiteral("apiKey")).toString(), body2.value(QStringLiteral("hmacKey")).toString(), editor);
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    QObject::tr("Edge Impulse Projects"),
                                    error2);
                            }
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                QObject::tr("Edge Impulse Projects"),
                                (reply2->error() == QNetworkReply::NoError) ? QObject::tr("No request data received") : reply2->errorString());
                        }

                        QObject::connect(reply2, &QNetworkReply::destroyed, manager2, &QNetworkAccessManager::deleteLater); reply2->deleteLater();
                    });

                    QNetworkRequest request2 = QNetworkRequest(QUrl(QString(QStringLiteral("https://studio.edgeimpulse.com/v1/api/%1/devkeys")).arg(array.first().toObject().value(QStringLiteral("id")).toInt())));
                    request2.setRawHeader(QByteArrayLiteral("accept"), QByteArrayLiteral("application/json"));
                    request2.setRawHeader(QByteArrayLiteral("x-api-key"), apiKey.toUtf8());

                    QNetworkReply *reply2 = manager2->get(request2);

                    if(reply2)
                    {
                        QObject::connect(reply2, &QNetworkReply::sslErrors, reply2, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        QObject::tr("Edge Impulse Projects"),
                        QObject::tr("An unkown error occured"));
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    QObject::tr("Edge Impulse Projects"),
                    error);
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                QObject::tr("Edge Impulse Projects"),
                (reply->error() == QNetworkReply::NoError) ? QObject::tr("No request data received") : reply->errorString());
        }

        QObject::connect(reply, &QNetworkReply::destroyed, manager, &QNetworkAccessManager::deleteLater); reply->deleteLater();
    });

    QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("https://studio.edgeimpulse.com/v1/api/projects")));
    request.setRawHeader(QByteArrayLiteral("accept"), QByteArrayLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("x-api-key"), apiKey.toUtf8());

    QNetworkReply *reply = manager->get(request);

    if(reply)
    {
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
    }
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

void loginToEdgeImpulse(OpenMVDatasetEditor *editor)
{
    QNetworkAccessManager *manager = new QNetworkAccessManager();

    QObject::connect(manager, &QNetworkAccessManager::finished, [manager, editor] (QNetworkReply *reply) {
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

                if(editor->rootPath().isEmpty())
                {
                    QMessageBox::information(Core::ICore::dialogParent(),
                        QObject::tr("Edge Impulse Login"),
                        QObject::tr("Sucessfully logged into your Edge Impulse account.\n\nOpen a data set to upload it."));
                }
                else
                {
                    uploadToSelectedProject(editor);
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    QObject::tr("Edge Impulse Login"),
                    error);
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                QObject::tr("Edge Impulse Login"),
                (reply->error() == QNetworkReply::NoError) ? QObject::tr("No request data received") : reply->errorString());
        }

        QObject::connect(reply, &QNetworkReply::destroyed, manager, &QNetworkAccessManager::deleteLater); reply->deleteLater();
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
    passwordBox->setPlaceholderText(QStringLiteral("********************"));
    layout->addRow(QObject::tr("Password"), passwordBox);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(box);

    if(dialog.exec() == QDialog::Accepted)
    {
        settings->setValue(QStringLiteral(LAST_JWT_TOKEN_EMAIL), usernameBox->text());

        QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("https://studio.edgeimpulse.com/v1/api-login")));
        request.setRawHeader(QByteArrayLiteral("accept"), QByteArrayLiteral("application/json"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        QJsonObject body;
        body.insert(QStringLiteral("username"), usernameBox->text());
        body.insert(QStringLiteral("password"), passwordBox->text());
        QNetworkReply *reply = manager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

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

void uploadToSelectedProject(OpenMVDatasetEditor *editor)
{
    QNetworkAccessManager *manager = new QNetworkAccessManager();

    QObject::connect(manager, &QNetworkAccessManager::finished, [manager, editor] (QNetworkReply *reply) {
        QByteArray data = reply->readAll();

        if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
        {
            QJsonObject body = QJsonDocument::fromJson(data).object();
            bool success = body.value(QStringLiteral("success")).toBool();
            QString error = body.value(QStringLiteral("error")).toString();

            if(success)
            {
                QMap<int, QString> map;

                QJsonArray temp = body.value(QStringLiteral("projects")).toArray(); foreach(const QJsonValue project, temp)
                {
                    map.insert(project.toObject().value(QStringLiteral("id")).toInt(),
                               QString(QStringLiteral("\"%1\" - #%2")).arg(project.toObject().value(QStringLiteral("name")).toString()).arg(project.toObject().value(QStringLiteral("id")).toInt()));
                }

                if(map.size())
                {
                    QSettings *settings = ExtensionSystem::PluginManager::settings();
                    settings->beginGroup(QStringLiteral(EDGE_IMPULSE_SETTINGS_GROUP));

                    int id = settings->value(QStringLiteral(LAST_PROJECT_ID)).toInt();
                    int index = map.contains(id) ? map.values().indexOf(map.value(id)) : -1;

                    bool ok;
                    id = map.key(QInputDialog::getItem(Core::ICore::dialogParent(),
                        QObject::tr("Edge Impulse Projects"), QObject::tr("Please select a project"),
                        map.values(), (index != -1) ? index : 0, false, &ok,
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint)));

                    if(ok)
                    {
                        settings->setValue(QStringLiteral(LAST_PROJECT_ID), id);

                        QNetworkAccessManager *manager2 = new QNetworkAccessManager();

                        QObject::connect(manager2, &QNetworkAccessManager::finished, [manager2, editor] (QNetworkReply *reply2) {
                            QByteArray data2 = reply2->readAll();

                            if((reply2->error() == QNetworkReply::NoError) && (!data2.isEmpty()))
                            {
                                QJsonObject body2 = QJsonDocument::fromJson(data2).object();
                                bool success2 = body2.value(QStringLiteral("success")).toBool();
                                QString error2 = body2.value(QStringLiteral("error")).toString();

                                if(success2)
                                {
                                    uploadProject(body2.value(QStringLiteral("apiKey")).toString(), body2.value(QStringLiteral("hmacKey")).toString(), editor);
                                }
                                else
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        QObject::tr("Edge Impulse Projects"),
                                        error2);
                                }
                            }
                            else
                            {
                                logoutFromEdgeImpulse();

                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    QObject::tr("Edge Impulse Projects"),
                                    (reply2->error() == QNetworkReply::NoError) ? QObject::tr("No request data received") : reply2->errorString());
                            }

                            QObject::connect(reply2, &QNetworkReply::destroyed, manager2, &QNetworkAccessManager::deleteLater); reply2->deleteLater();
                        });

                        QNetworkRequest request2 = QNetworkRequest(QUrl(QString(QStringLiteral("https://studio.edgeimpulse.com/v1/api/%1/devkeys")).arg(id)));
                        request2.setRawHeader(QByteArrayLiteral("accept"), QByteArrayLiteral("application/json"));
                        request2.setRawHeader(QByteArrayLiteral("cookie"), QByteArrayLiteral("jwt=") + settings->value(QStringLiteral(LAST_JWT_TOKEN)).toByteArray());

                        QNetworkReply *reply2 = manager2->get(request2);

                        if(reply2)
                        {
                            QObject::connect(reply2, &QNetworkReply::sslErrors, reply2, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
                        }
                    }

                    settings->endGroup();
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        QObject::tr("Edge Impulse Projects"),
                        QObject::tr("No projects found"));
                }
            }
            else
            {
                logoutFromEdgeImpulse();

                QMessageBox::critical(Core::ICore::dialogParent(),
                    QObject::tr("Edge Impulse Projects"),
                    error);
            }
        }
        else
        {
            logoutFromEdgeImpulse();

            QMessageBox::critical(Core::ICore::dialogParent(),
                QObject::tr("Edge Impulse Projects"),
                (reply->error() == QNetworkReply::NoError) ? QObject::tr("No request data received") : reply->errorString());
        }

        QObject::connect(reply, &QNetworkReply::destroyed, manager, &QNetworkAccessManager::deleteLater); reply->deleteLater();
    });

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(EDGE_IMPULSE_SETTINGS_GROUP));

    QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("https://studio.edgeimpulse.com/v1/api/projects")));
    request.setRawHeader(QByteArrayLiteral("accept"), QByteArrayLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("cookie"), QByteArrayLiteral("jwt=") + settings->value(QStringLiteral(LAST_JWT_TOKEN)).toByteArray());

    QNetworkReply *reply = manager->get(request);

    if(reply)
    {
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
    }

    settings->endGroup();
}

void uploadProjectByAPIKey(OpenMVDatasetEditor *editor)
{
    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(EDGE_IMPULSE_SETTINGS_GROUP));

    bool ok;
    QString apiKey = QInputDialog::getText(Core::ICore::dialogParent(),
        QObject::tr("Upload Project"), QObject::tr("Please enter an Edge Impluse Project API Key"),
        QLineEdit::Normal, settings->value(QStringLiteral(LAST_API_KEY)).toString(), &ok,
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

    ok = ok && (!apiKey.isEmpty());

    if(ok)
    {
        settings->setValue(QStringLiteral(LAST_API_KEY), apiKey);
    }

    settings->endGroup();

    if(ok)
    {
        uploadProject(apiKey, editor);
    }
}
