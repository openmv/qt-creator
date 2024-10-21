[1mdiff --git a/src/plugins/openmv/openmvplugin.cpp b/src/plugins/openmv/openmvplugin.cpp[m
[1mindex d1c55949ff1..72e8c85bee1 100644[m
[1m--- a/src/plugins/openmv/openmvplugin.cpp[m
[1m+++ b/src/plugins/openmv/openmvplugin.cpp[m
[36m@@ -228,13 +228,7 @@[m [mbool OpenMVPlugin::initialize(const QStringList &arguments, QString *errorMessag[m
         {[m
             MyQSerialPortInfo port(raw_port);[m
 [m
[31m-            if(port.hasVendorIdentifier() && port.hasProductIdentifier() && (((port.vendorIdentifier() == OPENMVCAM_VID) && (port.productIdentifier() == OPENMVCAM_PID) && (port.serialNumber() != QStringLiteral("000000000010")) && (port.serialNumber() != QStringLiteral("000000000011")))[m
[31m-            || ((port.vendorIdentifier() == ARDUINOCAM_VID) && (((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||[m
[31m-                                                                ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||[m
[31m-                                                                ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||[m
[31m-                                                                ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID) ||[m
[31m-                                                                ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_GH7_PID)))[m
[31m-            || ((port.vendorIdentifier() == RPI2040_VID) && (port.productIdentifier() == RPI2040_PID))))[m
[32m+[m[32m            if(validPort(m_firmwareSettings, QString(), port))[m
             {[m
                 stringList.append(port.portName());[m
             }[m
[36m@@ -456,6 +450,34 @@[m [mbool OpenMVPlugin::initialize(const QStringList &arguments, QString *errorMessag[m
 [m
     ///////////////////////////////////////////////////////////////////////////[m
 [m
[32m+[m[32m    QFile firmwareSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/settings.json")).toString());[m
[32m+[m
[32m+[m[32m    if(firmwareSettings.open(QIODevice::ReadOnly))[m
[32m+[m[32m    {[m
[32m+[m[32m        QJsonParseError error;[m
[32m+[m
[32m+[m[32m        m_firmwareSettings = QJsonDocument::fromJson(firmwareSettings.readAll(), &error);[m
[32m+[m
[32m+[m[32m        if(error.error == QJsonParseError::NoError)[m
[32m+[m[32m        {[m
[32m+[m
[32m+[m[32m        }[m
[32m+[m[32m        else[m
[32m+[m[32m        {[m
[32m+[m[32m            QMessageBox::critical(Q_NULLPTR, QString(),[m
[32m+[m[32m                Tr::tr("Error reading firmware settings - %L1!").arg(error.errorString()));[m
[32m+[m[32m            exit(-1);[m
[32m+[m[32m        }[m
[32m+[m[32m    }[m
[32m+[m[32m    else[m
[32m+[m[32m    {[m
[32m+[m[32m        QMessageBox::critical(Q_NULLPTR, QString(),[m
[32m+[m[32m            Tr::tr("Error reading firmware settings: %L1").arg(firmwareSettings.errorString()));[m
[32m+[m[32m        exit(-1);[m
[32m+[m[32m    }[m
[32m+[m
[32m+[m[32m    ///////////////////////////////////////////////////////////////////////////[m
[32m+[m
     loadDocs();[m
 [m
     ///////////////////////////////////////////////////////////////////////////[m
[36m@@ -2302,7 +2324,7 @@[m [mbool OpenMVPlugin::delayedInitialize()[m
     // Scan Serial Ports[m
     {[m
         QThread *thread = new QThread;[m
[31m-        ScanSerialPortsThread *scanSerialPortsThread = new ScanSerialPortsThread(m_serialNumberFilter);[m
[32m+[m[32m        ScanSerialPortsThread *scanSerialPortsThread = new ScanSerialPortsThread(m_firmwareSettings, m_serialNumberFilter);[m
         scanSerialPortsThread->moveToThread(thread);[m
         QTimer *timer = new QTimer(this);[m
 [m
[1mdiff --git a/src/plugins/openmv/openmvplugin.h b/src/plugins/openmv/openmvplugin.h[m
[1mindex 320ba643379..b60edac0add 100644[m
[1m--- a/src/plugins/openmv/openmvplugin.h[m
[1m+++ b/src/plugins/openmv/openmvplugin.h[m
[36m@@ -302,7 +302,10 @@[m [mpublic:[m
     }[m
 };[m
 [m
[31m-QPair<QStringList, QStringList> filterPorts(const QString &serialNumberFilter,[m
[32m+[m[32mbool validPort(const QJsonDocument &settings, const QString &serialNumberFilter, const MyQSerialPortInfo &port);[m
[32m+[m
[32m+[m[32mQPair<QStringList, QStringList> filterPorts(const QJsonDocument &settings,[m
[32m+[m[32m                                            const QString &serialNumberFilter,[m
                                             bool forceBootloader,[m
                                             const QList<wifiPort_t> &availableWifiPorts);[m
 [m
[36m@@ -310,10 +313,14 @@[m [mclass ScanSerialPortsThread: public QObject[m
 {[m
     Q_OBJECT[m
 [m
[31m-    public: explicit ScanSerialPortsThread(const QString &serialNumberFilter) { m_serialNumberFilter = serialNumberFilter; }[m
[31m-    public slots: void scanSerialPortsSlot() { emit serialPorts(filterPorts(m_serialNumberFilter, true, QList<wifiPort_t>())); }[m
[32m+[m[32m    public: explicit ScanSerialPortsThread(const QJsonDocument &settings, const QString &serialNumberFilter) {[m
[32m+[m[32m        m_firmwareSettings = settings; m_serialNumberFilter = serialNumberFilter;[m
[32m+[m[32m    }[m
[32m+[m[32m    public slots: void scanSerialPortsSlot() {[m
[32m+[m[32m        emit serialPorts(filterPorts(m_firmwareSettings, m_serialNumberFilter, true, QList<wifiPort_t>()));[m
[32m+[m[32m    }[m
     signals: void serialPorts(const QPair<QStringList, QStringList> &output);[m
[31m-    private: QString m_serialNumberFilter;[m
[32m+[m[32m    private: QJsonDocument m_firmwareSettings; QString m_serialNumberFilter;[m
 };[m
 [m
 class OpenMVPlugin : public ExtensionSystem::IPlugin[m
[36m@@ -377,6 +384,8 @@[m [mprivate:[m
 [m
     bool getTheLatestDevelopmentFirmware(const QString &arch, QString *path);[m
 [m
[32m+[m[32m    QJsonDocument m_firmwareSettings;[m
[32m+[m
     bool m_viewerMode;[m
 [m
     bool m_autoConnect;[m
[1mdiff --git a/src/plugins/openmv/openmvpluginconnect.cpp b/src/plugins/openmv/openmvpluginconnect.cpp[m
[1mindex 2485b9a7270..2ca8b78c19a 100644[m
[1m--- a/src/plugins/openmv/openmvpluginconnect.cpp[m
[1m+++ b/src/plugins/openmv/openmvpluginconnect.cpp[m
[36m@@ -589,7 +589,36 @@[m [mbool OpenMVPlugin::getTheLatestDevelopmentFirmware(const QString &arch, QString[m
     return false;[m
 }[m
 [m
[31m-QPair<QStringList, QStringList> filterPorts(const QString &serialNumberFilter,[m
[32m+[m[32mbool validPort(const QJsonDocument &settings, const QString &serialNumberFilter, const MyQSerialPortInfo &port)[m
[32m+[m[32m{[m
[32m+[m[32m    for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())[m
[32m+[m[32m    {[m
[32m+[m[32m        QJsonObject object = value.toObject();[m
[32m+[m[32m        QStringList vidpid = object.value(QStringLiteral("boardVidPid")).toString().split(QStringLiteral(":"));[m
[32m+[m[32m        int mask = object.value(QStringLiteral("boardPidMask")).toString().toInt(nullptr, 16);[m
[32m+[m[32m        QStringList serialNumbersInverseFilters;[m
[32m+[m
[32m+[m[32m        for (const QJsonValue &value : object.value(QStringLiteral("boardSerialNumberInverseFilters")).toArray())[m
[32m+[m[32m        {[m
[32m+[m[32m            serialNumbersInverseFilters.append(value.toString());[m
[32m+[m[32m        }[m
[32m+[m
[32m+[m[32m        if((vidpid.size() == 2)[m
[32m+[m[32m        && (serialNumberFilter.isEmpty() || (serialNumberFilter == port.serialNumber().toUpper()))[m
[32m+[m[32m        && port.hasVendorIdentifier()[m
[32m+[m[32m        && port.vendorIdentifier() == vidpid.at(0).toInt(nullptr, 16)[m
[32m+[m[32m        && port.hasProductIdentifier()[m
[32m+[m[32m        && (port.productIdentifier() & mask) == vidpid.at(1).toInt(nullptr, 16))[m
[32m+[m[32m        {[m
[32m+[m[32m            return serialNumbersInverseFilters.isEmpty() || (!serialNumbersInverseFilters.contains(port.serialNumber()));[m
[32m+[m[32m        }[m
[32m+[m[32m    }[m
[32m+[m
[32m+[m[32m    return false;[m
[32m+[m[32m}[m
[32m+[m
[32m+[m[32mQPair<QStringList, QStringList> filterPorts(const QJsonDocument &settings,[m
[32m+[m[32m                                            const QString &serialNumberFilter,[m
                                             bool forceBootloader,[m
                                             const QList<wifiPort_t> &availableWifiPorts)[m
 {[m
[36m@@ -599,15 +628,7 @@[m [mQPair<QStringList, QStringList> filterPorts(const QString &serialNumberFilter,[m
     {[m
         MyQSerialPortInfo port(raw_port);[m
 [m
[31m-        if(port.hasVendorIdentifier() && port.hasProductIdentifier()[m
[31m-        && (serialNumberFilter.isEmpty() || (serialNumberFilter == port.serialNumber().toUpper()))[m
[31m-        && (((port.vendorIdentifier() == OPENMVCAM_VID) && (port.productIdentifier() == OPENMVCAM_PID) && (port.serialNumber() != QStringLiteral("000000000010")) && (port.serialNumber() != QStringLiteral("000000000011")))[m
[31m-        || ((port.vendorIdentifier() == ARDUINOCAM_VID) && (((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||[m
[31m-                                                            ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||[m
[31m-                                                            ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||[m
[31m-                                                            ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID) ||[m
[31m-                                                            ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_GH7_PID)))[m
[31m-        || ((port.vendorIdentifier() == RPI2040_VID) && (port.productIdentifier() == RPI2040_PID))))[m
[32m+[m[32m        if(validPort(settings, serialNumberFilter, port))[m
         {[m
             stringList.append(port.portName());[m
         }[m
[36m@@ -626,7 +647,7 @@[m [mQPair<QStringList, QStringList> filterPorts(const QString &serialNumberFilter,[m
         }[m
     }[m
 [m
[31m-    dfuDevices = picotoolGetDevices() + imxGetAllDevices();[m
[32m+[m[32m    dfuDevices = picotoolGetDevices() + imxGetAllDevices(settings);[m
 [m
     for(const QString &device : getDevices())[m
     {[m
[36m@@ -659,12 +680,42 @@[m [mQPair<QStringList, QStringList> filterPorts(const QString &serialNumberFilter,[m
             QSerialPortInfo raw_info = QSerialPortInfo(*it);[m
             MyQSerialPortInfo info(raw_info);[m
 [m
[31m-            if(info.hasVendorIdentifier()[m
[31m-            && (info.vendorIdentifier() == ARDUINOCAM_VID)[m
[31m-            && info.hasProductIdentifier()[m
[31m-            && ((info.productIdentifier() == NRF_OLD_PID) || (info.productIdentifier() == NRF_LDR_PID) || (info.productIdentifier() == RPI_OLD_PID) || (info.productIdentifier() == RPI_LDR_PID)))[m
[32m+[m[32m            bool vidpidMatch = false, altvidpidMatch = false;[m
[32m+[m
[32m+[m[32m            for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())[m
[32m+[m[32m            {[m
[32m+[m[32m                QJsonObject object = value.toObject();[m
[32m+[m[32m                QString bootloaderType = object.value(QStringLiteral("bootloaderType")).toString();[m
[32m+[m[32m                if ((bootloaderType == QStringLiteral("picotool")) || (bootloaderType == QStringLiteral("bossac")))[m
[32m+[m[32m                {[m
[32m+[m[32m                    QJsonObject bootloaderSettings = object.value(QStringLiteral("bootloaderSettings")).toObject();[m
[32m+[m[32m                    QStringList vidpid = bootloaderSettings.value(QStringLiteral("vidpid")).toString().split(QStringLiteral(":"));[m
[32m+[m[32m                    QStringList altvidpid = bootloaderSettings.value(QStringLiteral("altvidpid")).toString().split(QStringLiteral(":"));[m
[32m+[m
[32m+[m[32m                    if((vidpid.size() == 2)[m
[32m+[m[32m                    && info.hasVendorIdentifier()[m
[32m+[m[32m                    && info.vendorIdentifier() == vidpid.at(0).toInt(nullptr, 16)[m
[32m+[m[32m                    && info.hasProductIdentifier()[m
[32m+[m[32m                    && info.productIdentifier()== vidpid.at(1).toInt(nullptr, 16))[m
[32m+[m[32m                    {[m
[32m+[m[32m                        dfuDevices.append(QString(QStringLiteral("%1:%2").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0')).arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))));[m
[32m+[m[32m                        vidpidMatch = true;[m
[32m+[m[32m                    }[m
[32m+[m
[32m+[m[32m                    if((altvidpid.size() == 2)[m
[32m+[m[32m                    && info.hasVendorIdentifier()[m
[32m+[m[32m                    && info.vendorIdentifier() == altvidpid.at(0).toInt(nullptr, 16)[m
[32m+[m[32m                    && info.hasProductIdentifier()[m
[32m+[m[32m                    && info.productIdentifier()== altvidpid.at(1).toInt(nullptr, 16))[m
[32m+[m[32m                    {[m
[32m+[m[32m                        dfuDevices.append(QString(QStringLiteral("%1:%2").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0')).arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))));[m
[32m+[m[32m                        altvidpidMatch = true;[m
[32m+[m[32m                    }[m
[32m+[m[32m                }[m
[32m+[m[32m            }[m
[32m+[m
[32m+[m[32m            if(vidpidMatch || altvidpidMatch)[m
             {[m
[31m-                dfuDevices.append(QString(QStringLiteral("%1:%2").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0')).arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))));[m
                 it = stringList.erase(it);[m
             }[m
             else[m
[36m@@ -708,7 +759,7 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
 [m
         do[m
         {[m
[31m-            QPair<QStringList, QStringList> output = filterPorts(m_serialNumberFilter, forceBootloader, m_availableWifiPorts);[m
[32m+[m[32m            QPair<QStringList, QStringList> output = filterPorts(m_firmwareSettings, m_serialNumberFilter, forceBootloader, m_availableWifiPorts);[m
             stringList = output.first;[m
             dfuDevices = output.second;[m
             if(waitForCamera && stringList.isEmpty()) QApplication::processEvents();[m
[36m@@ -748,95 +799,84 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
 [m
                 if(!dfuDevices.isEmpty())[m
                 {[m
[31m-                    QFile file(Core::ICore::userResourcePath(QStringLiteral("firmware/firmware.txt")).toString());[m
[31m-[m
[31m-                    if(file.open(QIODevice::ReadOnly))[m
[31m-                    {[m
[31m-                        QByteArray data = file.readAll();[m
[32m+[m[32m                    QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).[m
[32m+[m[32m                            match(m_firmwareSettings.object().value(QStringLiteral("firmware_version")).toString());[m
 [m
[31m-                        if((file.error() == QFile::NoError) && (!data.isEmpty()))[m
[31m-                        {[m
[31m-                            file.close();[m
[31m-[m
[31m-                            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).match(QString::fromUtf8(data));[m
[31m-[m
[31m-                            QDialog *dialog = new QDialog(Core::ICore::dialogParent(),[m
[31m-                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |[m
[31m-                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));[m
[31m-                            dialog->setWindowTitle(Tr::tr("Connect"));[m
[31m-                            QFormLayout *layout = new QFormLayout(dialog);[m
[31m-                            layout->setVerticalSpacing(0);[m
[31m-[m
[31m-                            layout->addWidget(new QLabel(Tr::tr("A board in DFU mode was detected. What would you like to do?")));[m
[31m-                            layout->addItem(new QSpacerItem(0, 6));[m
[31m-[m
[31m-                            QComboBox *combo = new QComboBox();[m
[31m-                            combo->addItem(Tr::tr("Install the lastest release firmware (v%L1.%L2.%L3)").arg(match.captured(1).toInt()).arg(match.captured(2).toInt()).arg(match.captured(3).toInt()));[m
[31m-                            combo->addItem(Tr::tr("Load a specific firmware"));[m
[31m-                            combo->addItem(Tr::tr("Just erase the interal file system"));[m
[31m-                            combo->setCurrentIndex(settings->value(LAST_DFU_ACTION, 0).toInt());[m
[31m-                            layout->addWidget(combo);[m
[31m-                            layout->addItem(new QSpacerItem(0, 6));[m
[31m-[m
[31m-                            QHBoxLayout *layout2 = new QHBoxLayout;[m
[31m-                            layout2->setContentsMargins(0, 0, 0, 0);[m
[31m-                            QWidget *widget = new QWidget;[m
[31m-                            widget->setLayout(layout2);[m
[31m-[m
[31m-                            QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal file system"));[m
[31m-                            checkBox->setChecked(settings->value(LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());[m
[31m-                            layout2->addWidget(checkBox);[m
[31m-                            checkBox->setVisible(combo->currentIndex() == 0);[m
[31m-                            checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal flash drive will be deleted. "[m
[31m-                                                    "This does not erase files on any removable SD card (if inserted)."));[m
[31m-[m
[31m-                            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);[m
[31m-                            layout2->addSpacing(160);[m
[31m-                            layout2->addWidget(box);[m
[31m-                            layout->addRow(widget);[m
[31m-[m
[31m-                            connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);[m
[31m-                            connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);[m
[31m-                            connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, dialog, checkBox] (int index) {[m
[31m-                                checkBox->setVisible(index == 0);[m
[31m-                                QTimer::singleShot(0, this, [dialog] { dialog->adjustSize(); });[m
[31m-                            });[m
[31m-[m
[31m-                            // If a board is in DFU mode we install the release firmware.[m
[31m-                            if((m_autoUpdate == QStringLiteral("release"))[m
[31m-                            || (m_autoUpdate == QStringLiteral("developement")))[m
[31m-                            {[m
[31m-                                combo->setCurrentIndex(0);[m
[31m-                                checkBox->setChecked(m_autoErase);[m
[31m-                            }[m
[32m+[m[32m                    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),[m
[32m+[m[32m                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |[m
[32m+[m[32m                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));[m
[32m+[m[32m                    dialog->setWindowTitle(Tr::tr("Connect"));[m
[32m+[m[32m                    QFormLayout *layout = new QFormLayout(dialog);[m
[32m+[m[32m                    layout->setVerticalSpacing(0);[m
[32m+[m
[32m+[m[32m                    layout->addWidget(new QLabel(Tr::tr("A board in DFU mode was detected. What would you like to do?")));[m
[32m+[m[32m                    layout->addItem(new QSpacerItem(0, 6));[m
[32m+[m
[32m+[m[32m                    QComboBox *combo = new QComboBox();[m
[32m+[m[32m                    combo->addItem(Tr::tr("Install the lastest release firmware (v%L1.%L2.%L3)").arg(match.captured(1).toInt()).arg(match.captured(2).toInt()).arg(match.captured(3).toInt()));[m
[32m+[m[32m                    combo->addItem(Tr::tr("Load a specific firmware"));[m
[32m+[m[32m                    combo->addItem(Tr::tr("Just erase the interal file system"));[m
[32m+[m[32m                    combo->setCurrentIndex(settings->value(LAST_DFU_ACTION, 0).toInt());[m
[32m+[m[32m                    layout->addWidget(combo);[m
[32m+[m[32m                    layout->addItem(new QSpacerItem(0, 6));[m
[32m+[m
[32m+[m[32m                    QHBoxLayout *layout2 = new QHBoxLayout;[m
[32m+[m[32m                    layout2->setContentsMargins(0, 0, 0, 0);[m
[32m+[m[32m                    QWidget *widget = new QWidget;[m
[32m+[m[32m                    widget->setLayout(layout2);[m
[32m+[m
[32m+[m[32m                    QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal file system"));[m
[32m+[m[32m                    checkBox->setChecked(settings->value(LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());[m
[32m+[m[32m                    layout2->addWidget(checkBox);[m
[32m+[m[32m                    checkBox->setVisible(combo->currentIndex() == 0);[m
[32m+[m[32m                    checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal flash drive will be deleted. "[m
[32m+[m[32m                                            "This does not erase files on any removable SD card (if inserted)."));[m
[32m+[m
[32m+[m[32m                    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);[m
[32m+[m[32m                    layout2->addSpacing(160);[m
[32m+[m[32m                    layout2->addWidget(box);[m
[32m+[m[32m                    layout->addRow(widget);[m
[32m+[m
[32m+[m[32m                    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);[m
[32m+[m[32m                    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);[m
[32m+[m[32m                    connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, dialog, checkBox] (int index) {[m
[32m+[m[32m                        checkBox->setVisible(index == 0);[m
[32m+[m[32m                        QTimer::singleShot(0, this, [dialog] { dialog->adjustSize(); });[m
[32m+[m[32m                    });[m
 [m
[31m-                            if((m_autoUpdate == QStringLiteral("release"))[m
[31m-                            || (m_autoUpdate == QStringLiteral("developement"))[m
[31m-                            || (dialog->exec() == QDialog::Accepted))[m
[31m-                            {[m
[31m-                                settings->setValue(LAST_DFU_ACTION, combo->currentIndex());[m
[31m-                                settings->setValue(LAST_DFU_FLASH_FS_ERASE_STATE, checkBox->isChecked());[m
[32m+[m[32m                    // If a board is in DFU mode we install the release firmware.[m
[32m+[m[32m                    if((m_autoUpdate == QStringLiteral("release"))[m
[32m+[m[32m                    || (m_autoUpdate == QStringLiteral("developement")))[m
[32m+[m[32m                    {[m
[32m+[m[32m                        combo->setCurrentIndex(0);[m
[32m+[m[32m                        checkBox->setChecked(m_autoErase);[m
[32m+[m[32m                    }[m
 [m
[31m-                                if(combo->currentIndex() == 0)[m
[31m-                                {[m
[31m-                                    dfuDeviceResetToRelease = true;[m
[31m-                                    dfuDeviceEraseFlash = checkBox->isChecked();[m
[31m-                                    dfuNoDialogs = true;[m
[31m-                                }[m
[31m-                                else if(combo->currentIndex() == 1)[m
[31m-                                {[m
[31m-                                    QTimer::singleShot(0, m_bootloaderAction, &QAction::trigger);[m
[31m-                                }[m
[31m-                                else if(combo->currentIndex() == 2)[m
[31m-                                {[m
[31m-                                    QTimer::singleShot(0, m_eraseAction, &QAction::trigger);[m
[31m-                                }[m
[31m-                            }[m
[32m+[m[32m                    if((m_autoUpdate == QStringLiteral("release"))[m
[32m+[m[32m                    || (m_autoUpdate == QStringLiteral("developement"))[m
[32m+[m[32m                    || (dialog->exec() == QDialog::Accepted))[m
[32m+[m[32m                    {[m
[32m+[m[32m                        settings->setValue(LAST_DFU_ACTION, combo->currentIndex());[m
[32m+[m[32m                        settings->setValue(LAST_DFU_FLASH_FS_ERASE_STATE, checkBox->isChecked());[m
 [m
[31m-                            delete dialog;[m
[32m+[m[32m                        if(combo->currentIndex() == 0)[m
[32m+[m[32m                        {[m
[32m+[m[32m                            dfuDeviceResetToRelease = true;[m
[32m+[m[32m                            dfuDeviceEraseFlash = checkBox->isChecked();[m
[32m+[m[32m                            dfuNoDialogs = true;[m
[32m+[m[32m                        }[m
[32m+[m[32m                        else if(combo->currentIndex() == 1)[m
[32m+[m[32m                        {[m
[32m+[m[32m                            QTimer::singleShot(0, m_bootloaderAction, &QAction::trigger);[m
[32m+[m[32m                        }[m
[32m+[m[32m                        else if(combo->currentIndex() == 2)[m
[32m+[m[32m                        {[m
[32m+[m[32m                            QTimer::singleShot(0, m_eraseAction, &QAction::trigger);[m
                         }[m
                     }[m
 [m
[32m+[m[32m                    delete dialog;[m
[32m+[m
                     if(!dfuDeviceResetToRelease)[m
                     {[m
                         settings->endGroup();[m
[36m@@ -848,129 +888,127 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
                     Tr::tr("Connect"),[m
                     Tr::tr("No OpenMV Cams found!"));[m
 [m
[31m-                QFile boards(Core::ICore::userResourcePath(QStringLiteral("firmware/boards.txt")).toString());[m
[32m+[m[32m                QMap<QString, QString> mappings;[m
[32m+[m[32m                QMap<QString, QPair<int, int> > eraseMappings;[m
[32m+[m[32m                QMap<QString, QPair<int, int> > eraseAllMappings;[m
[32m+[m[32m                QMap<QString, QString> vidpidMappings;[m
 [m
[31m-                if(boards.open(QIODevice::ReadOnly))[m
[32m+[m[32m                for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
                 {[m
[31m-                    QMap<QString, QString> mappings;[m
[31m-                    QMap<QString, QPair<int, int> > eraseMappings;[m
[31m-                    QMap<QString, QPair<int, int> > eraseAllMappings;[m
[31m-                    QMap<QString, QString> vidpidMappings;[m
[32m+[m[32m                    QString a = value.toObject().value(QStringLiteral("boardDisplayName")).toString();[m
[32m+[m[32m                    mappings.insert(a, value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());[m
[32m+[m[32m                    vidpidMappings.insert(a, value.toObject().value(QStringLiteral("boardVidPid")).toString());[m
 [m
[31m-                    forever[m
[32m+[m[32m                    if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("internal"))[m
                     {[m
[31m-                        QByteArray data = boards.readLine();[m
[31m-[m
[31m-                        if((boards.error() == QFile::NoError) && (!data.isEmpty()))[m
[31m-                        {[m
[31m-                            QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\S+)")).match(QString::fromUtf8(data));[m
[31m-                            QString temp = mapping.captured(2).replace(QStringLiteral("_"), QStringLiteral(" "));[m
[31m-                            mappings.insert(temp, mapping.captured(3));[m
[31m-                            eraseMappings.insert(temp, QPair<int, int>(mapping.captured(5).toInt(), mapping.captured(6).toInt()));[m
[31m-                            eraseAllMappings.insert(temp, QPair<int, int>(mapping.captured(4).toInt(), mapping.captured(6).toInt()));[m
[31m-                            vidpidMappings.insert(temp, mapping.captured(7));[m
[31m-                        }[m
[31m-                        else[m
[31m-                        {[m
[31m-                            boards.close();[m
[31m-                            break;[m
[31m-                        }[m
[32m+[m[32m                        QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();[m
[32m+[m[32m                        int eraseSectorStart = bootloaderSettings.value(QStringLiteral("eraseSectorStart")).toString().toInt();[m
[32m+[m[32m                        int eraseSectorEnd = bootloaderSettings.value(QStringLiteral("eraseSectorEnd")).toString().toInt();[m
[32m+[m[32m                        int eraseAllSectorStart = bootloaderSettings.value(QStringLiteral("eraseAllSectorStart")).toString().toInt();[m
[32m+[m[32m                        int eraseAllSectorEnd = bootloaderSettings.value(QStringLiteral("eraseAllSectorEnd")).toString().toInt();[m
[32m+[m[32m                        eraseMappings.insert(a, QPair<int, int>(eraseSectorStart, eraseSectorEnd));[m
[32m+[m[32m                        eraseAllMappings.insert(a, QPair<int, int>(eraseAllSectorStart, eraseAllSectorEnd));[m
                     }[m
[32m+[m[32m                    else[m
[32m+[m[32m                    {[m
[32m+[m[32m                        eraseMappings.insert(a, QPair<int, int>(0, 0));[m
[32m+[m[32m                        eraseAllMappings.insert(a, QPair<int, int>(0, 0));[m
[32m+[m[32m                    }[m
[32m+[m[32m                }[m
 [m
[31m-                    if(!mappings.isEmpty())[m
[32m+[m[32m                if(!mappings.isEmpty())[m
[32m+[m[32m                {[m
[32m+[m[32m                    if(forceBootloader || dfuDeviceResetToRelease || (QMessageBox::question(Core::ICore::dialogParent(),[m
[32m+[m[32m                        Tr::tr("Connect"),[m
[32m+[m[32m                        Tr::tr("Do you have an OpenMV Cam connected and is it bricked?"),[m
[32m+[m[32m                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)[m
[32m+[m[32m                    == QMessageBox::Yes))[m
                     {[m
[31m-                        if(forceBootloader || dfuDeviceResetToRelease || (QMessageBox::question(Core::ICore::dialogParent(),[m
[31m-                            Tr::tr("Connect"),[m
[31m-                            Tr::tr("Do you have an OpenMV Cam connected and is it bricked?"),[m
[31m-                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)[m
[31m-                        == QMessageBox::Yes))[m
[32m+[m[32m                        if(dfuDeviceResetToRelease)[m
                         {[m
[31m-                            if(dfuDeviceResetToRelease)[m
[31m-                            {[m
[31m-                                mappings.insert(QStringLiteral("NANO33_M4_OLD"), QStringLiteral("NANO33"));[m
[31m-                                eraseMappings.insert(QStringLiteral("NANO33_M4_OLD"), QPair<int, int>(0, 0));[m
[31m-                                eraseAllMappings.insert(QStringLiteral("NANO33_M4_OLD"), QPair<int, int>(0, 0));[m
[31m-                                vidpidMappings.insert(QStringLiteral("NANO33_M4_OLD"), QStringLiteral("2341:805a"));[m
[32m+[m[32m                            mappings.insert(QStringLiteral("NANO33_M4_OLD"), QStringLiteral("NANO33"));[m
[32m+[m[32m                            eraseMappings.insert(QStringLiteral("NANO33_M4_OLD"), QPair<int, int>(0, 0));[m
[32m+[m[32m                            eraseAllMappings.insert(QStringLiteral("NANO33_M4_OLD"), QPair<int, int>(0, 0));[m
[32m+[m[32m                            vidpidMappings.insert(QStringLiteral("NANO33_M4_OLD"), QStringLiteral("2341:805a"));[m
 [m
[31m-                                mappings.insert(QStringLiteral("PICO_M0_OLD"), QStringLiteral("PICO"));[m
[31m-                                eraseMappings.insert(QStringLiteral("PICO_M0_OLD"), QPair<int, int>(0, 0));[m
[31m-                                eraseAllMappings.insert(QStringLiteral("PICO_M0_OLD"), QPair<int, int>(0, 0));[m
[31m-                                vidpidMappings.insert(QStringLiteral("PICO_M0_OLD"), QStringLiteral("2341:805e"));[m
[32m+[m[32m                            mappings.insert(QStringLiteral("PICO_M0_OLD"), QStringLiteral("PICO"));[m
[32m+[m[32m                            eraseMappings.insert(QStringLiteral("PICO_M0_OLD"), QPair<int, int>(0, 0));[m
[32m+[m[32m                            eraseAllMappings.insert(QStringLiteral("PICO_M0_OLD"), QPair<int, int>(0, 0));[m
[32m+[m[32m                            vidpidMappings.insert(QStringLiteral("PICO_M0_OLD"), QStringLiteral("2341:805e"));[m
 [m
[31m-                                for(QMap<QString, QString>::iterator it = mappings.begin(); it != mappings.end(); )[m
[31m-                                {[m
[31m-                                    bool found = false;[m
[32m+[m[32m                            for(QMap<QString, QString>::iterator it = mappings.begin(); it != mappings.end(); )[m
[32m+[m[32m                            {[m
[32m+[m[32m                                bool found = false;[m
 [m
[31m-                                    for(const QString &device : dfuDevices)[m
[32m+[m[32m                                for(const QString &device : dfuDevices)[m
[32m+[m[32m                                {[m
[32m+[m[32m                                    if(device.split(QStringLiteral(",")).first() == vidpidMappings.value(it.key()))[m
                                     {[m
[31m-                                        if(device.split(QStringLiteral(",")).first() == vidpidMappings.value(it.key()))[m
[31m-                                        {[m
[31m-                                            found = true;[m
[31m-                                            break;[m
[31m-                                        }[m
[32m+[m[32m                                        found = true;[m
[32m+[m[32m                                        break;[m
                                     }[m
[32m+[m[32m                                }[m
 [m
[31m-                                    if(!found)[m
[31m-                                    {[m
[31m-                                        eraseMappings.remove(it.key());[m
[31m-                                        eraseAllMappings.remove(it.key());[m
[31m-                                        vidpidMappings.remove(it.key());[m
[31m-                                        it = mappings.erase(it);[m
[31m-                                    }[m
[31m-                                    else[m
[31m-                                    {[m
[31m-                                        it++;[m
[31m-                                    }[m
[32m+[m[32m                                if(!found)[m
[32m+[m[32m                                {[m
[32m+[m[32m                                    eraseMappings.remove(it.key());[m
[32m+[m[32m                                    eraseAllMappings.remove(it.key());[m
[32m+[m[32m                                    vidpidMappings.remove(it.key());[m
[32m+[m[32m                                    it = mappings.erase(it);[m
[32m+[m[32m                                }[m
[32m+[m[32m                                else[m
[32m+[m[32m                                {[m
[32m+[m[32m                                    it++;[m
                                 }[m
                             }[m
[32m+[m[32m                        }[m
 [m
[31m-                            if(mappings.size())[m
[32m+[m[32m                        if(mappings.size())[m
[32m+[m[32m                        {[m
[32m+[m[32m                            int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE).toString());[m
[32m+[m
[32m+[m[32m                            bool previousMappingAvailable = previousMappingSet && mappings.contains(previousMapping);[m
[32m+[m[32m                            bool ok = previousMappingAvailable || (mappings.size() == 1);[m
[32m+[m[32m                            QString temp = previousMappingAvailable ? previousMapping :[m
[32m+[m[32m                                ((mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),[m
[32m+[m[32m                                Tr::tr("Connect"), Tr::tr("Please select the board type"),[m
[32m+[m[32m                                mappings.keys(), (index != -1) ? index : 0, false, &ok,[m
[32m+[m[32m                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |[m
[32m+[m[32m                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint)));[m
[32m+[m
[32m+[m[32m                            if(ok)[m
                             {[m
[31m-                                int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE).toString());[m
[32m+[m[32m                                settings->setValue(LAST_BOARD_TYPE_STATE, temp);[m
 [m
[31m-                                bool previousMappingAvailable = previousMappingSet && mappings.contains(previousMapping);[m
[31m-                                bool ok = previousMappingAvailable || (mappings.size() == 1);[m
[31m-                                QString temp = previousMappingAvailable ? previousMapping :[m
[31m-                                    ((mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),[m
[31m-                                    Tr::tr("Connect"), Tr::tr("Please select the board type"),[m
[31m-                                    mappings.keys(), (index != -1) ? index : 0, false, &ok,[m
[31m-                                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |[m
[31m-                                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint)));[m
[32m+[m[32m                                int answer = ((forceBootloader && previousMappingSet) ? (forceFlashFSErase ? QMessageBox::Yes : QMessageBox::No) :[m
[32m+[m[32m                                    (dfuDeviceResetToRelease ? (dfuDeviceEraseFlash ? QMessageBox::Yes : QMessageBox::No) :[m
[32m+[m[32m                                        QMessageBox::question(Core::ICore::dialogParent(),[m
[32m+[m[32m                                                              Tr::tr("Connect"),[m
[32m+[m[32m                                                              Tr::tr("Erase the internal file system?"),[m
[32m+[m[32m                                                              QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)));[m
 [m
[31m-                                if(ok)[m
[32m+[m[32m                                if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))[m
                                 {[m
[31m-                                    settings->setValue(LAST_BOARD_TYPE_STATE, temp);[m
[31m-[m
[31m-                                    int answer = ((forceBootloader && previousMappingSet) ? (forceFlashFSErase ? QMessageBox::Yes : QMessageBox::No) :[m
[31m-                                        (dfuDeviceResetToRelease ? (dfuDeviceEraseFlash ? QMessageBox::Yes : QMessageBox::No) :[m
[31m-                                            QMessageBox::question(Core::ICore::dialogParent(),[m
[31m-                                                                  Tr::tr("Connect"),[m
[31m-                                                                  Tr::tr("Erase the internal file system?"),[m
[31m-                                                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)));[m
[31m-[m
[31m-                                    if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))[m
[31m-                                    {[m
[31m-                                        previousMapping = temp;[m
[31m-                                        originalFirmwareFolder = mappings.value(temp);[m
[31m-                                        firmwarePath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(QStringLiteral("firmware.bin")).toString();[m
[31m-                                        if (forceBootloader && (!forceFirmwarePath.isEmpty())) firmwarePath = forceFirmwarePath;[m
[31m-                                        forceBootloader = true;[m
[31m-                                        forceFlashFSErase = answer == QMessageBox::Yes;[m
[31m-                                        forceBootloaderBricked = true;[m
[31m-                                        originalEraseFlashSectorStart = eraseMappings.value(temp).first;[m
[31m-                                        originalEraseFlashSectorEnd = eraseMappings.value(temp).second;[m
[31m-                                        originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;[m
[31m-                                        originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;[m
[31m-                                        originalDfuVidPid = vidpidMappings.value(temp);[m
[31m-                                    }[m
[32m+[m[32m                                    previousMapping = temp;[m
[32m+[m[32m                                    originalFirmwareFolder = mappings.value(temp);[m
[32m+[m[32m                                    firmwarePath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(QStringLiteral("firmware.bin")).toString();[m
[32m+[m[32m                                    if (forceBootloader && (!forceFirmwarePath.isEmpty())) firmwarePath = forceFirmwarePath;[m
[32m+[m[32m                                    forceBootloader = true;[m
[32m+[m[32m                                    forceFlashFSErase = answer == QMessageBox::Yes;[m
[32m+[m[32m                                    forceBootloaderBricked = true;[m
[32m+[m[32m                                    originalEraseFlashSectorStart = eraseMappings.value(temp).first;[m
[32m+[m[32m                                    originalEraseFlashSectorEnd = eraseMappings.value(temp).second;[m
[32m+[m[32m                                    originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;[m
[32m+[m[32m                                    originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;[m
[32m+[m[32m                                    originalDfuVidPid = vidpidMappings.value(temp);[m
                                 }[m
                             }[m
[31m-                            else[m
[31m-                            {[m
[31m-                                QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                                                      Tr::tr("Connect"),[m
[31m-                                                      Tr::tr("No released firmware available for the attached board!"));[m
[31m-                            }[m
[32m+[m[32m                        }[m
[32m+[m[32m                        else[m
[32m+[m[32m                        {[m
[32m+[m[32m                            QMessageBox::critical(Core::ICore::dialogParent(),[m
[32m+[m[32m                                                  Tr::tr("Connect"),[m
[32m+[m[32m                                                  Tr::tr("No released firmware available for the attached board!"));[m
                         }[m
                     }[m
                 }[m
[36m@@ -1094,7 +1132,7 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
         {[m
             QStringList vidpid = selectedDfuDevice.split(QStringLiteral(",")).first().split(QStringLiteral(":"));[m
 [m
[31m-            isIMX = imxVidPidList().contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));[m
[32m+[m[32m            isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));[m
             isArduino = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||[m
                                                                                  ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||[m
                                                                                  ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||[m
[36m@@ -1131,7 +1169,7 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
         {[m
             QStringList vidpid = originalDfuVidPid.split(QStringLiteral(":"));[m
 [m
[31m-            isIMX = imxVidPidList().contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));[m
[32m+[m[32m            isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));[m
             isArduino = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||[m
                                                                                  ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||[m
                                                                                  ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||[m
[36m@@ -1413,115 +1451,105 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
 [m
                     if(!arch2.isEmpty())[m
                     {[m
[31m-                        QFile boards(Core::ICore::userResourcePath(QStringLiteral("firmware/boards.txt")).toString());[m
[32m+[m[32m                        QMap<QString, QString> mappings;[m
[32m+[m[32m                        QMap<QString, QString> mappingsHumanReadable;[m
[32m+[m[32m                        QMap<QString, QPair<int, int> > eraseMappings;[m
[32m+[m[32m                        QMap<QString, QPair<int, int> > eraseAllMappings;[m
[32m+[m[32m                        QMap<QString, QString> vidpidMappings;[m
 [m
[31m-                        if(boards.open(QIODevice::ReadOnly))[m
[32m+[m[32m                        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
                         {[m
[31m-                            QMap<QString, QString> mappings;[m
[31m-                            QMap<QString, QString> mappingsHumanReadable;[m
[31m-                            QMap<QString, QPair<int, int> > eraseMappings;[m
[31m-                            QMap<QString, QPair<int, int> > eraseAllMappings;[m
[31m-                            QMap<QString, QString> vidpidMappings;[m
[32m+[m[32m                            QString a = value.toObject().value(QStringLiteral("boardArchString")).toString();[m
[32m+[m[32m                            mappings.insert(a, value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());[m
[32m+[m[32m                            mappingsHumanReadable.insert(value.toObject().value(QStringLiteral("boardDisplayName")).toString(), a);[m
[32m+[m[32m                            vidpidMappings.insert(a, value.toObject().value(QStringLiteral("boardVidPid")).toString());[m
 [m
[31m-                            forever[m
[32m+[m[32m                            if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("internal"))[m
                             {[m
[31m-                                QByteArray data = boards.readLine();[m
[31m-[m
[31m-                                if((boards.error() == QFile::NoError) && (!data.isEmpty()))[m
[31m-                                {[m
[31m-                                    QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\S+)")).match(QString::fromUtf8(data));[m
[31m-                                    QString temp = mapping.captured(1).replace(QStringLiteral("_"), QStringLiteral(" "));[m
[31m-                                    mappings.insert(temp, mapping.captured(3));[m
[31m-                                    mappingsHumanReadable.insert(mapping.captured(2).replace(QStringLiteral("_"), QStringLiteral(" ")), temp);[m
[31m-                                    eraseMappings.insert(temp, QPair<int, int>(mapping.captured(5).toInt(), mapping.captured(6).toInt()));[m
[31m-                                    eraseAllMappings.insert(temp, QPair<int, int>(mapping.captured(4).toInt(), mapping.captured(6).toInt()));[m
[31m-                                    vidpidMappings.insert(temp, mapping.captured(7));[m
[31m-                                }[m
[31m-                                else[m
[31m-                                {[m
[31m-                                    boards.close();[m
[31m-                                    break;[m
[31m-                                }[m
[32m+[m[32m                                QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();[m
[32m+[m[32m                                int eraseSectorStart = bootloaderSettings.value(QStringLiteral("eraseSectorStart")).toString().toInt();[m
[32m+[m[32m                                int eraseSectorEnd = bootloaderSettings.value(QStringLiteral("eraseSectorEnd")).toString().toInt();[m
[32m+[m[32m                                int eraseAllSectorStart = bootloaderSettings.value(QStringLiteral("eraseAllSectorStart")).toString().toInt();[m
[32m+[m[32m                                int eraseAllSectorEnd = bootloaderSettings.value(QStringLiteral("eraseAllSectorEnd")).toString().toInt();[m
[32m+[m[32m                                eraseMappings.insert(a, QPair<int, int>(eraseSectorStart, eraseSectorEnd));[m
[32m+[m[32m                                eraseAllMappings.insert(a, QPair<int, int>(eraseAllSectorStart, eraseAllSectorEnd));[m
[32m+[m[32m                            }[m
[32m+[m[32m                            else[m
[32m+[m[32m                            {[m
[32m+[m[32m                                eraseMappings.insert(a, QPair<int, int>(0, 0));[m
[32m+[m[32m                                eraseAllMappings.insert(a, QPair<int, int>(0, 0));[m
                             }[m
[32m+[m[32m                        }[m
 [m
[31m-                            QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified().replace(QStringLiteral("_"), QStringLiteral(" "));[m
[32m+[m[32m                        QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();[m
 [m
[31m-                            if(!mappings.contains(temp))[m
[31m-                            {[m
[31m-                                int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE).toString());[m
[32m+[m[32m                        if(!mappings.contains(temp))[m
[32m+[m[32m                        {[m
[32m+[m[32m                            int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE).toString());[m
 [m
[31m-                                bool ok = mappings.size() == 1;[m
[31m-                                temp = (mappings.size() == 1) ? mappingsHumanReadable.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),[m
[31m-                                    Tr::tr("Connect"), Tr::tr("Please select the board type"),[m
[31m-                                    mappingsHumanReadable.keys(), (index != -1) ? index : 0, false, &ok,[m
[31m-                                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |[m
[31m-                                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));[m
[32m+[m[32m                            bool ok = mappings.size() == 1;[m
[32m+[m[32m                            temp = (mappings.size() == 1) ? mappingsHumanReadable.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),[m
[32m+[m[32m                                Tr::tr("Connect"), Tr::tr("Please select the board type"),[m
[32m+[m[32m                                mappingsHumanReadable.keys(), (index != -1) ? index : 0, false, &ok,[m
[32m+[m[32m                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |[m
[32m+[m[32m                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));[m
 [m
[31m-                                temp = mappingsHumanReadable.value(temp); // Get mappings key.[m
[32m+[m[32m                            temp = mappingsHumanReadable.value(temp); // Get mappings key.[m
 [m
[31m-                                if(ok)[m
[31m-                                {[m
[31m-                                    settings->setValue(LAST_BOARD_TYPE_STATE, temp);[m
[31m-                                }[m
[31m-                                else[m
[31m-                                {[m
[31m-                                    CLOSE_CONNECT_END();[m
[31m-                                }[m
[32m+[m[32m                            if(ok)[m
[32m+[m[32m                            {[m
[32m+[m[32m                                settings->setValue(LAST_BOARD_TYPE_STATE, temp);[m
                             }[m
[31m-[m
[31m-                            if(firmwarePath.isEmpty())[m
[32m+[m[32m                            else[m
                             {[m
[31m-                                originalFirmwareFolder = mappings.value(temp);[m
[31m-                                firmwarePath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(QStringLiteral("firmware.bin")).toString();[m
[31m-                                if (forceBootloader && (!forceFirmwarePath.isEmpty())) firmwarePath = forceFirmwarePath;[m
[31m-                                originalEraseFlashSectorStart = eraseMappings.value(temp).first;[m
[31m-                                originalEraseFlashSectorEnd = eraseMappings.value(temp).second;[m
[31m-                                originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;[m
[31m-                                originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;[m
[31m-[m
[31m-                                if(installTheLatestDevelopmentFirmware)[m
[31m-                                {[m
[31m-                                    if(!getTheLatestDevelopmentFirmware(mappings.value(temp), &firmwarePath))[m
[31m-                                    {[m
[31m-                                        CLOSE_CONNECT_END();[m
[31m-                                    }[m
[31m-                                }[m
[32m+[m[32m                                CLOSE_CONNECT_END();[m
                             }[m
[32m+[m[32m                        }[m
 [m
[31m-                            QStringList vidpid = vidpidMappings.value(temp).split(QStringLiteral(":"));[m
[31m-                            isIMX = imxVidPidList().contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));[m
[31m-                            isArduino = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||[m
[31m-                                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||[m
[31m-                                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||[m
[31m-                                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID) ||[m
[31m-                                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_GH7_PID))) ||[m
[31m-                                       ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));[m
[31m-                            isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);[m
[31m-                            isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);[m
[31m-                            isGiga = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == GIGA_LDR_PID);[m
[31m-                            isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));[m
[31m-                            isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||[m
[31m-                                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));[m
[31m-[m
[31m-                            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(.+?)\\[(.+?):(.+?)\\]")).match(arch2);[m
[31m-[m
[31m-                            if(match.hasMatch())[m
[32m+[m[32m                        if(firmwarePath.isEmpty())[m
[32m+[m[32m                        {[m
[32m+[m[32m                            originalFirmwareFolder = mappings.value(temp);[m
[32m+[m[32m                            firmwarePath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(QStringLiteral("firmware.bin")).toString();[m
[32m+[m[32m                            if (forceBootloader && (!forceFirmwarePath.isEmpty())) firmwarePath = forceFirmwarePath;[m
[32m+[m[32m                            originalEraseFlashSectorStart = eraseMappings.value(temp).first;[m
[32m+[m[32m                            originalEraseFlashSectorEnd = eraseMappings.value(temp).second;[m
[32m+[m[32m                            originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;[m
[32m+[m[32m                            originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;[m
[32m+[m
[32m+[m[32m                            if(installTheLatestDevelopmentFirmware)[m
                             {[m
[31m-                                m_boardTypeFolder = originalFirmwareFolder;[m
[31m-                                m_fullBoardType = match.captured(1).trimmed();[m
[31m-                                m_boardType = match.captured(2);[m
[31m-                                m_boardId = match.captured(3);[m
[31m-                                m_boardVID = vidpid.at(0).toInt(nullptr, 16);[m
[31m-                                m_boardPID = vidpid.at(1).toInt(nullptr, 16);[m
[32m+[m[32m                                if(!getTheLatestDevelopmentFirmware(mappings.value(temp), &firmwarePath))[m
[32m+[m[32m                                {[m
[32m+[m[32m                                    CLOSE_CONNECT_END();[m
[32m+[m[32m                                }[m
                             }[m
                         }[m
[31m-                        else[m
[31m-                        {[m
[31m-                            QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                                Tr::tr("Connect"),[m
[31m-                                Tr::tr("Error: %L1!").arg(boards.errorString()));[m
 [m
[31m-                            CLOSE_CONNECT_END();[m
[32m+[m[32m                        QStringList vidpid = vidpidMappings.value(temp).split(QStringLiteral(":"));[m
[32m+[m[32m                        isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));[m
[32m+[m[32m                        isArduino = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||[m
[32m+[m[32m                                                                                             ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||[m
[32m+[m[32m                                                                                             ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||[m
[32m+[m[32m                                                                                             ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID) ||[m
[32m+[m[32m                                                                                             ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_GH7_PID))) ||[m
[32m+[m[32m                                   ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));[m
[32m+[m[32m                        isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);[m
[32m+[m[32m                        isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);[m
[32m+[m[32m                        isGiga = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == GIGA_LDR_PID);[m
[32m+[m[32m                        isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));[m
[32m+[m[32m                        isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||[m
[32m+[m[32m                                    ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));[m
[32m+[m
[32m+[m[32m                        QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(.+?)\\[(.+?):(.+?)\\]")).match(arch2);[m
[32m+[m
[32m+[m[32m                        if(match.hasMatch())[m
[32m+[m[32m                        {[m
[32m+[m[32m                            m_boardTypeFolder = originalFirmwareFolder;[m
[32m+[m[32m                            m_fullBoardType = match.captured(1).trimmed();[m
[32m+[m[32m                            m_boardType = match.captured(2);[m
[32m+[m[32m                            m_boardId = match.captured(3);[m
[32m+[m[32m                            m_boardVID = vidpid.at(0).toInt(nullptr, 16);[m
[32m+[m[32m                            m_boardPID = vidpid.at(1).toInt(nullptr, 16);[m
                         }[m
                     }[m
                     else[m
[36m@@ -2033,114 +2061,87 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
 [m
             if(isIMX)[m
             {[m
[31m-                QFile imxSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/imx.txt")).toString());[m
                 QJsonObject outObj;[m
 [m
[31m-                if(imxSettings.open(QIODevice::ReadOnly))[m
[32m+[m[32m                if(originalFirmwareFolder.isEmpty())[m
                 {[m
[31m-                    QJsonParseError error;[m
[31m-                    QJsonDocument doc = QJsonDocument::fromJson(imxSettings.readAll(), &error);[m
[32m+[m[32m                    QList<QPair<int, int> > vidpidlist = imxVidPidList(m_firmwareSettings, false, true);[m
[32m+[m[32m                    QMap<QString, QString> mappings;[m
 [m
[31m-                    if(error.error == QJsonParseError::NoError)[m
[32m+[m[32m                    for (const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
                     {[m
[31m-                        if(originalFirmwareFolder.isEmpty())[m
[31m-                        {[m
[31m-                            QFile boards(Core::ICore::userResourcePath(QStringLiteral("firmware/boards.txt")).toString());[m
[31m-[m
[31m-                            if(boards.open(QIODevice::ReadOnly))[m
[31m-                            {[m
[31m-                                QList<QPair<int, int> > vidpidlist = imxVidPidList(false, true);[m
[31m-                                QMap<QString, QString> mappings;[m
[31m-[m
[31m-                                forever[m
[31m-                                {[m
[31m-                                    QByteArray data = boards.readLine();[m
[31m-[m
[31m-                                    if((boards.error() == QFile::NoError) && (!data.isEmpty()))[m
[31m-                                    {[m
[31m-                                        QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\S+)")).match(QString::fromUtf8(data));[m
[31m-                                        QString temp = mapping.captured(2).replace(QStringLiteral("_"), QStringLiteral(" "));[m
[31m-                                        QStringList vidpid = mapping.captured(7).split(QStringLiteral(":"));[m
[31m-                                        if(vidpidlist.contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)))) mappings.insert(temp, mapping.captured(3));[m
[31m-                                    }[m
[31m-                                    else[m
[31m-                                    {[m
[31m-                                        boards.close();[m
[31m-                                        break;[m
[31m-                                    }[m
[31m-                                }[m
[31m-[m
[31m-                                if(!mappings.isEmpty())[m
[31m-                                {[m
[31m-                                    int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE).toString());[m
[31m-                                    bool ok = mappings.size() == 1;[m
[31m-                                    QString temp = (mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),[m
[31m-                                        Tr::tr("Connect"), Tr::tr("Please select the board type"),[m
[31m-                                        mappings.keys(), (index != -1) ? index : 0, false, &ok,[m
[31m-                                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |[m
[31m-                                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));[m
[31m-[m
[31m-                                    if(ok)[m
[31m-                                    {[m
[31m-                                        settings->setValue(LAST_BOARD_TYPE_STATE, temp);[m
[31m-                                        originalFirmwareFolder = mappings.value(temp);[m
[31m-                                    }[m
[31m-                                    else[m
[31m-                                    {[m
[31m-                                        CONNECT_END();[m
[31m-                                    }[m
[31m-                                }[m
[31m-                            }[m
[31m-                        }[m
[32m+[m[32m                        QJsonObject obj = val.toObject();[m
 [m
[31m-                        if(!originalFirmwareFolder.isEmpty())[m
[32m+[m[32m                        if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("imx"))[m
                         {[m
[31m-                            bool foundMatch = false;[m
[32m+[m[32m                            QString a = val.toObject().value(QStringLiteral("boardDisplayName")).toString();[m
[32m+[m[32m                            QStringList vidpid = val.toObject().value(QStringLiteral("boardVidPid")).toString().split(QStringLiteral(":"));[m
 [m
[31m-                            for(const QJsonValue &val : doc.array())[m
[32m+[m[32m                            if(vidpidlist.contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16))))[m
                             {[m
[31m-                                QJsonObject obj = val.toObject();[m
[31m-[m
[31m-                                if(originalFirmwareFolder == obj.value(QStringLiteral("firmware_folder")).toString())[m
[31m-                                {[m
[31m-                                    QString secureBootloaderPath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(obj.value(QStringLiteral("sdphost_flash_loader_path")).toString()).toString();[m
[31m-                                    QString bootloaderPath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(obj.value(QStringLiteral("blhost_secure_bootloader_path")).toString()).toString();[m
[31m-                                    outObj = obj;[m
[31m-                                    outObj.insert(QStringLiteral("sdphost_flash_loader_path"), secureBootloaderPath);[m
[31m-                                    outObj.insert(QStringLiteral("blhost_secure_bootloader_path"), bootloaderPath);[m
[31m-                                    outObj.insert(QStringLiteral("blhost_secure_bootloader_length"),[m
[31m-                                            QString::number(QFileInfo(bootloaderPath).size(), 16).prepend(QStringLiteral("0x")));[m
[31m-                                    outObj.insert(QStringLiteral("blhost_firmware_path"), firmwarePath);[m
[31m-                                    outObj.insert(QStringLiteral("blhost_firmware_length"),[m
[31m-                                            QString::number(QFileInfo(firmwarePath).size(), 16).prepend(QStringLiteral("0x")));[m
[31m-                                    foundMatch = true;[m
[31m-                                    break;[m
[31m-                                }[m
[32m+[m[32m                                mappings.insert(a, val.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());[m
                             }[m
[32m+[m[32m                        }[m
[32m+[m[32m                    }[m
 [m
[31m-                            if(!foundMatch)[m
[31m-                            {[m
[31m-                                QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                                    Tr::tr("Connect"),[m
[31m-                                    Tr::tr("No IMX settings for the selected board type %L1!").arg(originalFirmwareFolder));[m
[32m+[m[32m                    if(!mappings.isEmpty())[m
[32m+[m[32m                    {[m
[32m+[m[32m                        int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE).toString());[m
[32m+[m[32m                        bool ok = mappings.size() == 1;[m
[32m+[m[32m                        QString temp = (mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),[m
[32m+[m[32m                            Tr::tr("Connect"), Tr::tr("Please select the board type"),[m
[32m+[m[32m                            mappings.keys(), (index != -1) ? index : 0, false, &ok,[m
[32m+[m[32m                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |[m
[32m+[m[32m                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));[m
 [m
[31m-                                CONNECT_END();[m
[31m-                            }[m
[32m+[m[32m                        if(ok)[m
[32m+[m[32m                        {[m
[32m+[m[32m                            settings->setValue(LAST_BOARD_TYPE_STATE, temp);[m
[32m+[m[32m                            originalFirmwareFolder = mappings.value(temp);[m
                         }[m
                         else[m
                         {[m
[31m-                            QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                                Tr::tr("Connect"),[m
[31m-                                Tr::tr("No IMX settings found!"));[m
[31m-[m
                             CONNECT_END();[m
                         }[m
                     }[m
[31m-                    else[m
[32m+[m[32m                }[m
[32m+[m
[32m+[m[32m                if(!originalFirmwareFolder.isEmpty())[m
[32m+[m[32m                {[m
[32m+[m[32m                    bool foundMatch = false;[m
[32m+[m
[32m+[m[32m                    for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
[32m+[m[32m                    {[m
[32m+[m[32m                        QJsonObject obj = val.toObject();[m
[32m+[m
[32m+[m[32m                        if((originalFirmwareFolder == obj.value(QStringLiteral("boardFirmwareFolder")).toString())[m
[32m+[m[32m                        && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("imx")))[m
[32m+[m[32m                        {[m
[32m+[m[32m                            QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();[m
[32m+[m[32m                            QString secureBootloaderPath = Core::ICore::userResourcePath(QStringLiteral("firmware")).[m
[32m+[m[32m                                    pathAppended(originalFirmwareFolder).[m
[32m+[m[32m                                    pathAppended(bootloaderSettings.value(QStringLiteral("sdphost_flash_loader_path")).toString()).toString();[m
[32m+[m[32m                            QString bootloaderPath = Core::ICore::userResourcePath(QStringLiteral("firmware")).[m
[32m+[m[32m                                    pathAppended(originalFirmwareFolder).[m
[32m+[m[32m                                    pathAppended(bootloaderSettings.value(QStringLiteral("blhost_secure_bootloader_path")).toString()).toString();[m
[32m+[m[32m                            outObj = bootloaderSettings;[m
[32m+[m[32m                            outObj.insert(QStringLiteral("sdphost_flash_loader_path"), secureBootloaderPath);[m
[32m+[m[32m                            outObj.insert(QStringLiteral("blhost_secure_bootloader_path"), bootloaderPath);[m
[32m+[m[32m                            outObj.insert(QStringLiteral("blhost_secure_bootloader_length"),[m
[32m+[m[32m                                    QString::number(QFileInfo(bootloaderPath).size(), 16).prepend(QStringLiteral("0x")));[m
[32m+[m[32m                            outObj.insert(QStringLiteral("blhost_firmware_path"), firmwarePath);[m
[32m+[m[32m                            outObj.insert(QStringLiteral("blhost_firmware_length"),[m
[32m+[m[32m                                    QString::number(QFileInfo(firmwarePath).size(), 16).prepend(QStringLiteral("0x")));[m
[32m+[m[32m                            foundMatch = true;[m
[32m+[m[32m                            break;[m
[32m+[m[32m                        }[m
[32m+[m[32m                    }[m
[32m+[m
[32m+[m[32m                    if(!foundMatch)[m
                     {[m
                         QMessageBox::critical(Core::ICore::dialogParent(),[m
                             Tr::tr("Connect"),[m
[31m-                            Tr::tr("Error: %L1!").arg(error.errorString()));[m
[32m+[m[32m                            Tr::tr("No IMX settings for the selected board type %L1!").arg(originalFirmwareFolder));[m
 [m
                         CONNECT_END();[m
                     }[m
[36m@@ -2149,7 +2150,7 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
                 {[m
                     QMessageBox::critical(Core::ICore::dialogParent(),[m
                         Tr::tr("Connect"),[m
[31m-                        Tr::tr("Error: %L1!").arg(imxSettings.errorString()));[m
[32m+[m[32m                        Tr::tr("No IMX settings found!"));[m
 [m
                     CONNECT_END();[m
                 }[m
[36m@@ -2295,7 +2296,7 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
                     QPair<int , int> entry(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16));[m
 [m
                     // SPD Mode (SBL)[m
[31m-                    if(imxVidPidList(true, false).contains(entry))[m
[32m+[m[32m                    if(imxVidPidList(m_firmwareSettings, true, false).contains(entry))[m
                     {[m
                         if(imxDownloadBootloaderAndFirmware(outObj, forceFlashFSErase, justEraseFlashFs))[m
                         {[m
[36m@@ -2314,7 +2315,7 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
                         }[m
                     }[m
                     // BL Mode[m
[31m-                    else if(imxVidPidList(false, true).contains(entry))[m
[32m+[m[32m                    else if(imxVidPidList(m_firmwareSettings, false, true).contains(entry))[m
                     {[m
                         if(imxDownloadFirmware(outObj, forceFlashFSErase, justEraseFlashFs))[m
                         {[m
[36m@@ -2416,133 +2417,121 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
                     QString selectedDfuDeviceVidPid = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice.split(QStringLiteral(",")).first();[m
                     QString selectedDfuDeviceSerialNumber = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice.split(QStringLiteral(",")).last();[m
 [m
[31m-                    QFile dfuSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/dfu.txt")).toString());[m
                     QString boardTypeToDfuDeviceVidPid;[m
                     QStringList eraseCommands, extraProgramAddrCommands, extraProgramPathCommands;[m
                     QString binProgramCommand, dfuProgramCommand;[m
 [m
[31m-                    if(dfuSettings.open(QIODevice::ReadOnly))[m
[32m+[m[32m                    if(selectedDfuDevice.isEmpty())[m
                     {[m
[31m-                        QJsonParseError error;[m
[31m-                        QJsonDocument doc = QJsonDocument::fromJson(dfuSettings.readAll(), &error);[m
[32m+[m[32m                        bool foundMatch = false;[m
 [m
[31m-                        if(error.error == QJsonParseError::NoError)[m
[32m+[m[32m                        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
                         {[m
[31m-                            if(selectedDfuDevice.isEmpty())[m
[31m-                            {[m
[31m-                                bool foundMatch = false;[m
[32m+[m[32m                            QJsonObject obj = val.toObject();[m
 [m
[31m-                                for(const QJsonValue &val : doc.array())[m
[31m-                                {[m
[31m-                                    QJsonObject obj = val.toObject();[m
[32m+[m[32m                            if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("arduino_dfu"))[m
[32m+[m[32m                            {[m
[32m+[m[32m                                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();[m
 [m
[31m-                                    QJsonArray appvidpidArray = obj.value(QStringLiteral("appvidpid")).toArray();[m
[31m-                                    bool isApp = appvidpidArray.isEmpty();[m
[32m+[m[32m                                QJsonArray appvidpidArray = bootloaderSettings.value(QStringLiteral("appvidpid")).toArray();[m
[32m+[m[32m                                bool isApp = appvidpidArray.isEmpty();[m
 [m
[31m-                                    if(!isApp)[m
[31m-                                    {[m
[31m-                                        for(const QJsonValue &appvidpid : appvidpidArray)[m
[31m-                                        {[m
[31m-                                            QStringList vidpid = appvidpid.toString().split(QStringLiteral(":"));[m
[31m-[m
[31m-                                            if((vidpid.at(0).toInt(nullptr, 16) == m_boardVID) && (vidpid.at(1).toInt(nullptr, 16) == m_boardPID))[m
[31m-                                            {[m
[31m-                                                isApp = true;[m
[31m-                                                break;[m
[31m-                                            }[m
[31m-                                        }[m
[31m-                                    }[m
[31m-[m
[31m-                                    if(isApp && (m_boardType == obj.value(QStringLiteral("boardType")).toString()))[m
[32m+[m[32m                                if(!isApp)[m
[32m+[m[32m                                {[m
[32m+[m[32m                                    for(const QJsonValue &appvidpid : appvidpidArray)[m
                                     {[m
[31m-                                        boardTypeToDfuDeviceVidPid = obj.value(QStringLiteral("vidpid")).toString();[m
[32m+[m[32m                                        QStringList vidpid = appvidpid.toString().split(QStringLiteral(":"));[m
 [m
[31m-                                        QJsonArray eraseCommandsArray = obj.value(QStringLiteral("eraseCommands")).toArray(); for(const QJsonValue &command : eraseCommandsArray)[m
[32m+[m[32m                                        if((vidpid.at(0).toInt(nullptr, 16) == m_boardVID) && (vidpid.at(1).toInt(nullptr, 16) == m_boardPID))[m
                                         {[m
[31m-                                            eraseCommands.append(command.toString());[m
[31m-                                        }[m
[31m-[m
[31m-                                        QJsonArray extraProgramCommandsArray = obj.value(QStringLiteral("extraProgramCommands")).toArray(); for(const QJsonValue &command : extraProgramCommandsArray)[m
[31m-                                        {[m
[31m-                                            QJsonObject obj = command.toObject();[m
[31m-                                            extraProgramAddrCommands.append(obj.value(QStringLiteral("addr")).toString());[m
[31m-                                            extraProgramPathCommands.append(obj.value(QStringLiteral("path")).toString());[m
[32m+[m[32m                                            isApp = true;[m
[32m+[m[32m                                            break;[m
                                         }[m
[31m-[m
[31m-                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();[m
[31m-                                        dfuProgramCommand = obj.value(QStringLiteral("dfuProgramCommand")).toString();[m
[31m-                                        foundMatch = true;[m
[31m-                                        break;[m
                                     }[m
                                 }[m
 [m
[31m-                                if(!foundMatch)[m
[32m+[m[32m                                if(isApp && (m_boardType == obj.value(QStringLiteral("boardType")).toString()))[m
                                 {[m
[31m-                                    QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                                        Tr::tr("Connect"),[m
[31m-                                        Tr::tr("No DFU settings for the selected board type!") + QString(QStringLiteral("\n\nVID: %1, PID: %2")).arg(m_boardVID).arg(m_boardPID));[m
[32m+[m[32m                                    boardTypeToDfuDeviceVidPid = bootloaderSettings.value(QStringLiteral("vidpid")).toString();[m
 [m
[31m-                                    CONNECT_END();[m
[32m+[m[32m                                    QJsonArray eraseCommandsArray = bootloaderSettings.value(QStringLiteral("eraseCommands")).toArray();[m
[32m+[m[32m                                    for(const QJsonValue &command : eraseCommandsArray)[m
[32m+[m[32m                                    {[m
[32m+[m[32m                                        eraseCommands.append(command.toString());[m
[32m+[m[32m                                    }[m
[32m+[m
[32m+[m[32m                                    QJsonArray extraProgramCommandsArray = bootloaderSettings.value(QStringLiteral("extraProgramCommands")).toArray();[m
[32m+[m[32m                                    for(const QJsonValue &command : extraProgramCommandsArray)[m
[32m+[m[32m                                    {[m
[32m+[m[32m                                        QJsonObject obj2 = command.toObject();[m
[32m+[m[32m                                        extraProgramAddrCommands.append(obj2.value(QStringLiteral("addr")).toString());[m
[32m+[m[32m                                        extraProgramPathCommands.append(obj2.value(QStringLiteral("path")).toString());[m
[32m+[m[32m                                    }[m
[32m+[m
[32m+[m[32m                                    binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();[m
[32m+[m[32m                                    dfuProgramCommand = bootloaderSettings.value(QStringLiteral("dfuProgramCommand")).toString();[m
[32m+[m[32m                                    foundMatch = true;[m
[32m+[m[32m                                    break;[m
                                 }[m
                             }[m
[31m-                            else[m
[31m-                            {[m
[31m-                                bool foundMatch = false;[m
[32m+[m[32m                        }[m
 [m
[31m-                                for(const QJsonValue &val : doc.array())[m
[31m-                                {[m
[31m-                                    QJsonObject obj = val.toObject();[m
[32m+[m[32m                        if(!foundMatch)[m
[32m+[m[32m                        {[m
[32m+[m[32m                            QMessageBox::critical(Core::ICore::dialogParent(),[m
[32m+[m[32m                                Tr::tr("Connect"),[m
[32m+[m[32m                                Tr::tr("No DFU settings for the selected board type!") + QString(QStringLiteral("\n\nVID: %1, PID: %2")).arg(m_boardVID).arg(m_boardPID));[m
 [m
[31m-                                    if(selectedDfuDeviceVidPid == obj.value(QStringLiteral("vidpid")).toString())[m
[31m-                                    {[m
[31m-                                        QJsonArray eraseCommandsArray = obj.value(QStringLiteral("eraseCommands")).toArray(); for(const QJsonValue &command : eraseCommandsArray)[m
[31m-                                        {[m
[31m-                                            eraseCommands.append(command.toString());[m
[31m-                                        }[m
[32m+[m[32m                            CONNECT_END();[m
[32m+[m[32m                        }[m
[32m+[m[32m                    }[m
[32m+[m[32m                    else[m
[32m+[m[32m                    {[m
[32m+[m[32m                        bool foundMatch = false;[m
 [m
[31m-                                        QJsonArray extraProgramCommandsArray = obj.value(QStringLiteral("extraProgramCommands")).toArray(); for(const QJsonValue &command : extraProgramCommandsArray)[m
[31m-                                        {[m
[31m-                                            QJsonObject obj = command.toObject();[m
[31m-                                            extraProgramAddrCommands.append(obj.value(QStringLiteral("addr")).toString());[m
[31m-                                            extraProgramPathCommands.append(obj.value(QStringLiteral("path")).toString());[m
[31m-                                        }[m
[32m+[m[32m                        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
[32m+[m[32m                        {[m
[32m+[m[32m                            QJsonObject obj = val.toObject();[m
 [m
[31m-                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();[m
[31m-                                        dfuProgramCommand = obj.value(QStringLiteral("dfuProgramCommand")).toString();[m
[31m-                                        foundMatch = true;[m
[31m-                                        break;[m
[31m-                                    }[m
[31m-                                }[m
[32m+[m[32m                            if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("arduino_dfu"))[m
[32m+[m[32m                            {[m
[32m+[m[32m                                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();[m
 [m
[31m-                                if(!foundMatch)[m
[32m+[m[32m                                if(selectedDfuDeviceVidPid == bootloaderSettings.value(QStringLiteral("vidpid")).toString())[m
                                 {[m
[31m-                                    QStringList dfuDeviceVidPidList = selectedDfuDeviceVidPid.split(QLatin1Char(':'));[m
[32m+[m[32m                                    QJsonArray eraseCommandsArray = bootloaderSettings.value(QStringLiteral("eraseCommands")).toArray();[m
[32m+[m[32m                                    for(const QJsonValue &command : eraseCommandsArray)[m
[32m+[m[32m                                    {[m
[32m+[m[32m                                        eraseCommands.append(command.toString());[m
[32m+[m[32m                                    }[m
 [m
[31m-                                    QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                                        Tr::tr("Connect"),[m
[31m-                                        Tr::tr("No DFU settings for the selected device!") + QString(QStringLiteral("\n\nVID: %1, PID: %2")).arg(dfuDeviceVidPidList.first().toInt(nullptr, 16)).arg(dfuDeviceVidPidList.last().toInt(nullptr, 16)));[m
[32m+[m[32m                                    QJsonArray extraProgramCommandsArray = bootloaderSettings.value(QStringLiteral("extraProgramCommands")).toArray();[m
[32m+[m[32m                                    for(const QJsonValue &command : extraProgramCommandsArray)[m
[32m+[m[32m                                    {[m
[32m+[m[32m                                        QJsonObject obj2 = command.toObject();[m
[32m+[m[32m                                        extraProgramAddrCommands.append(obj2.value(QStringLiteral("addr")).toString());[m
[32m+[m[32m                                        extraProgramPathCommands.append(obj2.value(QStringLiteral("path")).toString());[m
[32m+[m[32m                                    }[m
 [m
[31m-                                    CONNECT_END();[m
[32m+[m[32m                                    binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();[m
[32m+[m[32m                                    dfuProgramCommand = bootloaderSettings.value(QStringLiteral("dfuProgramCommand")).toString();[m
[32m+[m[32m                                    foundMatch = true;[m
[32m+[m[32m                                    break;[m
                                 }[m
                             }[m
                         }[m
[31m-                        else[m
[32m+[m
[32m+[m[32m                        if(!foundMatch)[m
                         {[m
[32m+[m[32m                            QStringList dfuDeviceVidPidList = selectedDfuDeviceVidPid.split(QLatin1Char(':'));[m
[32m+[m
                             QMessageBox::critical(Core::ICore::dialogParent(),[m
                                 Tr::tr("Connect"),[m
[31m-                                Tr::tr("Error: %L1!").arg(error.errorString()));[m
[32m+[m[32m                                Tr::tr("No DFU settings for the selected device!") + QString(QStringLiteral("\n\nVID: %1, PID: %2")).arg(dfuDeviceVidPidList.first().toInt(nullptr, 16)).arg(dfuDeviceVidPidList.last().toInt(nullptr, 16)));[m
 [m
                             CONNECT_END();[m
                         }[m
                     }[m
[31m-                    else[m
[31m-                    {[m
[31m-                        QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                            Tr::tr("Connect"),[m
[31m-                            Tr::tr("Error: %L1!").arg(dfuSettings.errorString()));[m
[31m-[m
[31m-                        CONNECT_END();[m
[31m-                    }[m
 [m
                     QString dfuDeviceVidPid = selectedDfuDevice.isEmpty() ? boardTypeToDfuDeviceVidPid : selectedDfuDeviceVidPid;[m
                     QString dfuDeviceSerial = QString(QStringLiteral(" -S %1")).arg(selectedDfuDevice.isEmpty()[m
[36m@@ -2749,87 +2738,67 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
                         }[m
                     }[m
 [m
[31m-                    QFile bossacSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/bossac.txt")).toString());[m
                     QString boardTypeToDfuDeviceVidPid;[m
                     QString binProgramCommand;[m
 [m
[31m-                    if(bossacSettings.open(QIODevice::ReadOnly))[m
[32m+[m[32m                    if(selectedDfuDevice.isEmpty())[m
                     {[m
[31m-                        QJsonParseError error;[m
[31m-                        QJsonDocument doc = QJsonDocument::fromJson(bossacSettings.readAll(), &error);[m
[32m+[m[32m                        bool foundMatch = false;[m
 [m
[31m-                        if(error.error == QJsonParseError::NoError)[m
[32m+[m[32m                        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
                         {[m
[31m-                            if(selectedDfuDevice.isEmpty())[m
[31m-                            {[m
[31m-                                bool foundMatch = false;[m
[32m+[m[32m                            QJsonObject obj = val.toObject();[m
 [m
[31m-                                for(const QJsonValue &val : doc.array())[m
[31m-                                {[m
[31m-                                    QJsonObject obj = val.toObject();[m
[32m+[m[32m                            if((m_boardType == obj.value(QStringLiteral("boardType")).toString())[m
[32m+[m[32m                            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("bossac")))[m
[32m+[m[32m                            {[m
[32m+[m[32m                                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();[m
[32m+[m[32m                                boardTypeToDfuDeviceVidPid = bootloaderSettings.value(QStringLiteral("vidpid")).toString();[m
[32m+[m[32m                                binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();[m
[32m+[m[32m                                foundMatch = true;[m
[32m+[m[32m                                break;[m
[32m+[m[32m                            }[m
[32m+[m[32m                        }[m
 [m
[31m-                                    if(m_boardType == obj.value(QStringLiteral("boardType")).toString())[m
[31m-                                    {[m
[31m-                                        boardTypeToDfuDeviceVidPid = obj.value(QStringLiteral("vidpid")).toString();[m
[32m+[m[32m                        if(!foundMatch)[m
[32m+[m[32m                        {[m
[32m+[m[32m                            QMessageBox::critical(Core::ICore::dialogParent(),[m
[32m+[m[32m                                Tr::tr("Connect"),[m
[32m+[m[32m                                Tr::tr("No BOSSAC settings for the selected board type!"));[m
 [m
[31m-                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();[m
[31m-                                        foundMatch = true;[m
[31m-                                        break;[m
[31m-                                    }[m
[31m-                                }[m
[32m+[m[32m                            CONNECT_END();[m
[32m+[m[32m                        }[m
[32m+[m[32m                    }[m
[32m+[m[32m                    else[m
[32m+[m[32m                    {[m
[32m+[m[32m                        bool foundMatch = false;[m
 [m
[31m-                                if(!foundMatch)[m
[31m-                                {[m
[31m-                                    QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                                        Tr::tr("Connect"),[m
[31m-                                        Tr::tr("No BOSSAC settings for the selected board type!"));[m
[32m+[m[32m                        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
[32m+[m[32m                        {[m
[32m+[m[32m                            QJsonObject obj = val.toObject();[m
 [m
[31m-                                    CONNECT_END();[m
[31m-                                }[m
[31m-                            }[m
[31m-                            else[m
[32m+[m[32m                            if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("bossac"))[m
                             {[m
[31m-                                bool foundMatch = false;[m
[31m-[m
[31m-                                for(const QJsonValue &val : doc.array())[m
[31m-                                {[m
[31m-                                    QJsonObject obj = val.toObject();[m
[31m-[m
[31m-                                    if(selectedDfuDeviceVidPid == obj.value(QStringLiteral("vidpid")).toString())[m
[31m-                                    {[m
[31m-                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();[m
[31m-                                        foundMatch = true;[m
[31m-                                        break;[m
[31m-                                    }[m
[31m-                                }[m
[32m+[m[32m                                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();[m
 [m
[31m-                                if(!foundMatch)[m
[32m+[m[32m                                if(selectedDfuDeviceVidPid == bootloaderSettings.value(QStringLiteral("vidpid")).toString())[m
                                 {[m
[31m-                                    QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                                        Tr::tr("Connect"),[m
[31m-                                        Tr::tr("No BOSSAC settings for the selected device!"));[m
[31m-[m
[31m-                                    CONNECT_END();[m
[32m+[m[32m                                    binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();[m
[32m+[m[32m                                    foundMatch = true;[m
[32m+[m[32m                                    break;[m
                                 }[m
                             }[m
                         }[m
[31m-                        else[m
[32m+[m
[32m+[m[32m                        if(!foundMatch)[m
                         {[m
                             QMessageBox::critical(Core::ICore::dialogParent(),[m
                                 Tr::tr("Connect"),[m
[31m-                                Tr::tr("Error: %L1!").arg(error.errorString()));[m
[32m+[m[32m                                Tr::tr("No BOSSAC settings for the selected device!"));[m
 [m
                             CONNECT_END();[m
                         }[m
                     }[m
[31m-                    else[m
[31m-                    {[m
[31m-                        QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                            Tr::tr("Connect"),[m
[31m-                            Tr::tr("Error: %L1!").arg(bossacSettings.errorString()));[m
[31m-[m
[31m-                        CONNECT_END();[m
[31m-                    }[m
 [m
                     if(forceFlashFSErase && justEraseFlashFs)[m
                     {[m
[36m@@ -2910,95 +2879,80 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
 [m
                     QString selectedDfuDeviceVidPid = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice.split(QStringLiteral(",")).first();[m
 [m
[31m-                    QFile dfuSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/picotool.txt")).toString());[m
                     QStringList eraseCommands;[m
                     QString binProgramCommand;[m
 [m
[31m-                    if(dfuSettings.open(QIODevice::ReadOnly))[m
[32m+[m[32m                    if(selectedDfuDevice.isEmpty())[m
                     {[m
[31m-                        QJsonParseError error;[m
[31m-                        QJsonDocument doc = QJsonDocument::fromJson(dfuSettings.readAll(), &error);[m
[32m+[m[32m                        bool foundMatch = false;[m
 [m
[31m-                        if(error.error == QJsonParseError::NoError)[m
[32m+[m[32m                        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
                         {[m
[31m-                            if(selectedDfuDevice.isEmpty())[m
[32m+[m[32m                            QJsonObject obj = val.toObject();[m
[32m+[m
[32m+[m[32m                            if((m_boardType == obj.value(QStringLiteral("boardType")).toString())[m
[32m+[m[32m                            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("picotool")))[m
                             {[m
[31m-                                bool foundMatch = false;[m
[32m+[m[32m                                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();[m
[32m+[m[32m                                QJsonArray eraseCommandsArray = bootloaderSettings.value(QStringLiteral("eraseCommands")).toArray();[m
 [m
[31m-                                for(const QJsonValue &val : doc.array())[m
[32m+[m[32m                                for(const QJsonValue &command : eraseCommandsArray)[m
                                 {[m
[31m-                                    QJsonObject obj = val.toObject();[m
[32m+[m[32m                                    eraseCommands.append(command.toString());[m
[32m+[m[32m                                }[m
 [m
[31m-                                    if(m_boardType == obj.value(QStringLiteral("boardType")).toString())[m
[31m-                                    {[m
[31m-                                        QJsonArray eraseCommandsArray = obj.value(QStringLiteral("eraseCommands")).toArray(); for(const QJsonValue &command : eraseCommandsArray)[m
[31m-                                        {[m
[31m-                                            eraseCommands.append(command.toString());[m
[31m-                                        }[m
[32m+[m[32m                                binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();[m
[32m+[m[32m                                foundMatch = true;[m
[32m+[m[32m                                break;[m
[32m+[m[32m                            }[m
[32m+[m[32m                        }[m
 [m
[31m-                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();[m
[31m-                                        foundMatch = true;[m
[31m-                                        break;[m
[31m-                                    }[m
[31m-                                }[m
[32m+[m[32m                        if(!foundMatch)[m
[32m+[m[32m                        {[m
[32m+[m[32m                            QMessageBox::critical(Core::ICore::dialogParent(),[m
[32m+[m[32m                                Tr::tr("Connect"),[m
[32m+[m[32m                                Tr::tr("No PicoTool settings for the selected board type!"));[m
 [m
[31m-                                if(!foundMatch)[m
[31m-                                {[m
[31m-                                    QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                                        Tr::tr("Connect"),[m
[31m-                                        Tr::tr("No PicoTool settings for the selected board type!"));[m
[32m+[m[32m                            CONNECT_END();[m
[32m+[m[32m                        }[m
[32m+[m[32m                    }[m
[32m+[m[32m                    else[m
[32m+[m[32m                    {[m
[32m+[m[32m                        bool foundMatch = false;[m
 [m
[31m-                                    CONNECT_END();[m
[31m-                                }[m
[31m-                            }[m
[31m-                            else[m
[32m+[m[32m                        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
[32m+[m[32m                        {[m
[32m+[m[32m                            QJsonObject obj = val.toObject();[m
[32m+[m
[32m+[m[32m                            if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("picotool"))[m
                             {[m
[31m-                                bool foundMatch = false;[m
[32m+[m[32m                                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();[m
 [m
[31m-                                for(const QJsonValue &val : doc.array())[m
[32m+[m[32m                                if(selectedDfuDeviceVidPid == bootloaderSettings.value(QStringLiteral("vidpid")).toString())[m
                                 {[m
[31m-                                    QJsonObject obj = val.toObject();[m
[32m+[m[32m                                    QJsonArray eraseCommandsArray = bootloaderSettings.value(QStringLiteral("eraseCommands")).toArray();[m
 [m
[31m-                                    if(selectedDfuDeviceVidPid == obj.value(QStringLiteral("vidpid")).toString())[m
[32m+[m[32m                                    for(const QJsonValue &command : eraseCommandsArray)[m
                                     {[m
[31m-                                        QJsonArray eraseCommandsArray = obj.value(QStringLiteral("eraseCommands")).toArray(); for(const QJsonValue &command : eraseCommandsArray)[m
[31m-                                        {[m
[31m-                                            eraseCommands.append(command.toString());[m
[31m-                                        }[m
[31m-[m
[31m-                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();[m
[31m-                                        foundMatch = true;[m
[31m-                                        break;[m
[32m+[m[32m                                        eraseCommands.append(command.toString());[m
                                     }[m
[31m-                                }[m
 [m
[31m-                                if(!foundMatch)[m
[31m-                                {[m
[31m-                                    QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                                        Tr::tr("Connect"),[m
[31m-                                        Tr::tr("No PicoTool settings for the selected device!"));[m
[31m-[m
[31m-                                    CONNECT_END();[m
[32m+[m[32m                                    binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();[m
[32m+[m[32m                                    foundMatch = true;[m
[32m+[m[32m                                    break;[m
                                 }[m
                             }[m
                         }[m
[31m-                        else[m
[32m+[m
[32m+[m[32m                        if(!foundMatch)[m
                         {[m
                             QMessageBox::critical(Core::ICore::dialogParent(),[m
                                 Tr::tr("Connect"),[m
[31m-                                Tr::tr("Error: %L1!").arg(error.errorString()));[m
[32m+[m[32m                                Tr::tr("No PicoTool settings for the selected device!"));[m
 [m
                             CONNECT_END();[m
                         }[m
                     }[m
[31m-                    else[m
[31m-                    {[m
[31m-                        QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                            Tr::tr("Connect"),[m
[31m-                            Tr::tr("Error: %L1!").arg(dfuSettings.errorString()));[m
[31m-[m
[31m-                        CONNECT_END();[m
[31m-                    }[m
 [m
                     if(forceFlashFSErase)[m
                     {[m
[36m@@ -3241,37 +3195,21 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
                 }[m
                 else if(id2)[m
                 {[m
[31m-                    QFile sensors(Core::ICore::userResourcePath(QStringLiteral("firmware/sensors.txt")).toString());[m
[32m+[m[32m                    bool found = false;[m
 [m
[31m-                    if(sensors.open(QIODevice::ReadOnly))[m
[32m+[m[32m                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("sensors")).toArray())[m
                     {[m
[31m-                        QMap<int, QString> mappings;[m
[31m-[m
[31m-                        forever[m
[32m+[m[32m                        if (int(value.toObject().value(QStringLiteral("id")).toString().toLongLong(nullptr, 0)) == id2)[m
                         {[m
[31m-                            QByteArray data = sensors.readLine();[m
[31m-[m
[31m-                            if((sensors.error() == QFile::NoError) && (!data.isEmpty()))[m
[31m-                            {[m
[31m-                                QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)")).match(QString::fromUtf8(data));[m
[31m-                                mappings.insert(mapping.captured(2).toInt(nullptr, 0), mapping.captured(1));[m
[31m-                            }[m
[31m-                            else[m
[31m-                            {[m
[31m-                                sensors.close();[m
[31m-                                break;[m
[31m-                            }[m
[32m+[m[32m                            m_sensorType = value.toObject().value(QStringLiteral("name")).toString();[m
[32m+[m[32m                            found = true;[m
[32m+[m[32m                            break;[m
                         }[m
[31m-[m
[31m-                        m_sensorType = mappings.value(id2, Tr::tr("Unknown"));[m
                     }[m
[31m-                    else[m
[31m-                    {[m
[31m-                        QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                            Tr::tr("Connect"),[m
[31m-                            Tr::tr("Error: %L1!").arg(sensors.errorString()));[m
 [m
[31m-                        CLOSE_CONNECT_END();[m
[32m+[m[32m                    if (!found)[m
[32m+[m[32m                    {[m
[32m+[m[32m                        m_sensorType = Tr::tr("Unknown");[m
                     }[m
                 }[m
                 else[m
[36m@@ -3371,40 +3309,15 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
 [m
                     boardTypeLabel = board;[m
 [m
[31m-                    QFile boards(Core::ICore::userResourcePath(QStringLiteral("firmware/boards.txt")).toString());[m
[32m+[m[32m                    QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();[m
 [m
[31m-                    if(boards.open(QIODevice::ReadOnly))[m
[32m+[m[32m                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())[m
                     {[m
[31m-                        QMap<QString, QString> mappings;[m
[31m-                        QMap<QString, QString> folderMappings;[m
[31m-[m
[31m-                        forever[m
[32m+[m[32m                        if (value.toObject().value(QStringLiteral("boardArchString")).toString() == temp)[m
                         {[m
[31m-                            QByteArray data = boards.readLine();[m
[31m-[m
[31m-                            if((boards.error() == QFile::NoError) && (!data.isEmpty()))[m
[31m-                            {[m
[31m-                                QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\S+)")).match(QString::fromUtf8(data));[m
[31m-                                QString temp = mapping.captured(1).replace(QStringLiteral("_"), QStringLiteral(" "));[m
[31m-                                mappings.insert(temp, mapping.captured(2));[m
[31m-                                folderMappings.insert(temp, mapping.captured(3));[m
[31m-                            }[m
[31m-                            else[m
[31m-                            {[m
[31m-                                boards.close();[m
[31m-                                break;[m
[31m-                            }[m
[31m-                        }[m
[31m-[m
[31m-                        if(!mappings.isEmpty())[m
[31m-                        {[m
[31m-                            QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified().replace(QStringLiteral("_"), QStringLiteral(" "));[m
[31m-[m
[31m-                            if(mappings.contains(temp))[m
[31m-                            {[m
[31m-                                boardTypeLabel = mappings.value(temp).replace(QStringLiteral("_"), QStringLiteral(" "));[m
[31m-                                m_boardTypeFolder = folderMappings.value(temp);[m
[31m-                            }[m
[32m+[m[32m                            boardTypeLabel = value.toObject().value(QStringLiteral("boardDisplayName")).toString();[m
[32m+[m[32m                            m_boardTypeFolder = value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString();[m
[32m+[m[32m                            break;[m
                         }[m
                     }[m
 [m
[36m@@ -3677,41 +3590,30 @@[m [mvoid OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePat[m
 [m
         // Check Version //////////////////////////////////////////////////////[m
 [m
[31m-        QFile file(Core::ICore::userResourcePath(QStringLiteral("firmware/firmware.txt")).toString());[m
[32m+[m[32m        QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).[m
[32m+[m[32m                match(m_firmwareSettings.object().value(QStringLiteral("firmware_version")).toString());[m
 [m
[31m-        if(file.open(QIODevice::ReadOnly))[m
[32m+[m[32m        if((major2 < match.captured(1).toInt())[m
[32m+[m[32m        || ((major2 == match.captured(1).toInt()) && (minor2 < match.captured(2).toInt()))[m
[32m+[m[32m        || ((major2 == match.captured(1).toInt()) && (minor2 == match.captured(2).toInt()) && (patch2 < match.captured(3).toInt())))[m
         {[m
[31m-            QByteArray data = file.readAll();[m
[31m-[m
[31m-            if((file.error() == QFile::NoError) && (!data.isEmpty()))[m
[31m-            {[m
[31m-                file.close();[m
[31m-[m
[31m-                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).match(QString::fromUtf8(data));[m
[32m+[m[32m            m_versionButton->setText(m_versionButton->text().append(Tr::tr(" - [ out of date - click here to updgrade ]")));[m
 [m
[31m-                if((major2 < match.captured(1).toInt())[m
[31m-                || ((major2 == match.captured(1).toInt()) && (minor2 < match.captured(2).toInt()))[m
[31m-                || ((major2 == match.captured(1).toInt()) && (minor2 == match.captured(2).toInt()) && (patch2 < match.captured(3).toInt())))[m
[31m-                {[m
[31m-                    m_versionButton->setText(m_versionButton->text().append(Tr::tr(" - [ out of date - click here to updgrade ]")));[m
[31m-[m
[31m-                    QTimer::singleShot(1, this, [this] {[m
[31m-                        if ((!m_autoConnect) && Utils::CheckableMessageBox::question(Core::ICore::dialogParent(),[m
[31m-                                Tr::tr("Connect"),[m
[31m-                                Tr::tr("Your OpenMV Cam's firmware is out of date. Would you like to upgrade?"),[m
[31m-                                Utils::CheckableDecider(DONT_SHOW_UPGRADE_FW_AGAIN),[m
[31m-                                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,[m
[31m-                                QMessageBox::Yes, QMessageBox::No, {}, {}, QMessageBox::Yes) == QMessageBox::Yes)[m
[31m-                        {[m
[31m-                            OpenMVPlugin::updateCam(true);[m
[31m-                        }[m
[31m-                    });[m
[31m-                }[m
[31m-                else[m
[32m+[m[32m            QTimer::singleShot(1, this, [this] {[m
[32m+[m[32m                if ((!m_autoConnect) && Utils::CheckableMessageBox::question(Core::ICore::dialogParent(),[m
[32m+[m[32m                        Tr::tr("Connect"),[m
[32m+[m[32m                        Tr::tr("Your OpenMV Cam's firmware is out of date. Would you like to upgrade?"),[m
[32m+[m[32m                        Utils::CheckableDecider(DONT_SHOW_UPGRADE_FW_AGAIN),[m
[32m+[m[32m                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,[m
[32m+[m[32m                        QMessageBox::Yes, QMessageBox::No, {}, {}, QMessageBox::Yes) == QMessageBox::Yes)[m
                 {[m
[31m-                    m_versionButton->setText(m_versionButton->text().append(Tr::tr(" - [ latest ]")));[m
[32m+[m[32m                    OpenMVPlugin::updateCam(true);[m
                 }[m
[31m-            }[m
[32m+[m[32m            });[m
[32m+[m[32m        }[m
[32m+[m[32m        else[m
[32m+[m[32m        {[m
[32m+[m[32m            m_versionButton->setText(m_versionButton->text().append(Tr::tr(" - [ latest ]")));[m
         }[m
 [m
         ///////////////////////////////////////////////////////////////////////[m
[36m@@ -4303,91 +4205,62 @@[m [mvoid OpenMVPlugin::updateCam(bool forceYes)[m
 {[m
     if(!m_working)[m
     {[m
[31m-        QFile file(Core::ICore::userResourcePath(QStringLiteral("firmware/firmware.txt")).toString());[m
[32m+[m[32m        QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).[m
[32m+[m[32m            match(m_firmwareSettings.object().value(QStringLiteral("firmware_version")).toString());[m
 [m
[31m-        if(file.open(QIODevice::ReadOnly))[m
[32m+[m[32m        if((m_major < match.captured(1).toInt())[m
[32m+[m[32m        || ((m_major == match.captured(1).toInt()) && (m_minor < match.captured(2).toInt()))[m
[32m+[m[32m        || ((m_major == match.captured(1).toInt()) && (m_minor == match.captured(2).toInt()) && (m_patch < match.captured(3).toInt())))[m
         {[m
[31m-            QByteArray data = file.readAll();[m
[31m-[m
[31m-            if((file.error() == QFile::NoError) && (!data.isEmpty()))[m
[32m+[m[32m            if(forceYes || (QMessageBox::warning(Core::ICore::dialogParent(),[m
[32m+[m[32m                Tr::tr("Firmware Update"),[m
[32m+[m[32m                Tr::tr("Update your OpenMV Cam's firmware to the latest version?"),[m
[32m+[m[32m                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)[m
[32m+[m[32m            == QMessageBox::Ok))[m
             {[m
[31m-                file.close();[m
[31m-[m
[31m-                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).match(QString::fromUtf8(data));[m
[32m+[m[32m                int answer = QMessageBox::question(Core::ICore::dialogParent(),[m
[32m+[m[32m                    Tr::tr("Firmware Update"),[m
[32m+[m[32m                    Tr::tr("Erase the internal file system?"),[m
[32m+[m[32m                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);[m
 [m
[31m-                if((m_major < match.captured(1).toInt())[m
[31m-                || ((m_major == match.captured(1).toInt()) && (m_minor < match.captured(2).toInt()))[m
[31m-                || ((m_major == match.captured(1).toInt()) && (m_minor == match.captured(2).toInt()) && (m_patch < match.captured(3).toInt())))[m
[32m+[m[32m                if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))[m
                 {[m
[31m-                    if(forceYes || (QMessageBox::warning(Core::ICore::dialogParent(),[m
[31m-                        Tr::tr("Firmware Update"),[m
[31m-                        Tr::tr("Update your OpenMV Cam's firmware to the latest version?"),[m
[31m-                        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)[m
[31m-                    == QMessageBox::Ok))[m
[31m-                    {[m
[31m-                        int answer = QMessageBox::question(Core::ICore::dialogParent(),[m
[31m-                            Tr::tr("Firmware Update"),[m
[31m-                            Tr::tr("Erase the internal file system?"),[m
[31m-                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);[m
[31m-[m
[31m-                        if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))[m
[31m-                        {[m
[31m-                            disconnectClicked();[m
[32m+[m[32m                    disconnectClicked();[m
 [m
[31m-                            if(ExtensionSystem::PluginManager::specForPlugin(this)->state() != ExtensionSystem::PluginSpec::Stopped)[m
[31m-                            {[m
[31m-                                connectClicked(true, QString(), answer == QMessageBox::Yes);[m
[31m-                            }[m
[31m-                        }[m
[31m-                    }[m
[31m-                }[m
[31m-                else[m
[31m-                {[m
[31m-                    QMessageBox::information(Core::ICore::dialogParent(),[m
[31m-                        Tr::tr("Firmware Update"),[m
[31m-                        Tr::tr("Your OpenMV Cam's firmware is up to date."));[m
[31m-[m
[31m-                    if(QMessageBox::question(Core::ICore::dialogParent(),[m
[31m-                        Tr::tr("Firmware Update"),[m
[31m-                        Tr::tr("Need to reset your OpenMV Cam's firmware to the release version?"),[m
[31m-                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)[m
[31m-                    == QMessageBox::Yes)[m
[32m+[m[32m                    if(ExtensionSystem::PluginManager::specForPlugin(this)->state() != ExtensionSystem::PluginSpec::Stopped)[m
                     {[m
[31m-                        int answer = QMessageBox::question(Core::ICore::dialogParent(),[m
[31m-                            Tr::tr("Firmware Update"),[m
[31m-                            Tr::tr("Erase the internal file system?"),[m
[31m-                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);[m
[31m-[m
[31m-                        if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))[m
[31m-                        {[m
[31m-                            disconnectClicked();[m
[31m-[m
[31m-                            if(ExtensionSystem::PluginManager::specForPlugin(this)->state() != ExtensionSystem::PluginSpec::Stopped)[m
[31m-                            {[m
[31m-                                connectClicked(true, QString(), answer == QMessageBox::Yes);[m
[31m-                            }[m
[31m-                        }[m
[32m+[m[32m                        connectClicked(true, QString(), answer == QMessageBox::Yes);[m
                     }[m
                 }[m
             }[m
[31m-            else if(file.error() != QFile::NoError)[m
[31m-            {[m
[31m-                QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                    Tr::tr("Firmware Update"),[m
[31m-                    Tr::tr("Error: %L1!").arg(file.errorString()));[m
[31m-            }[m
[31m-            else[m
[31m-            {[m
[31m-                QMessageBox::critical(Core::ICore::dialogParent(),[m
[31m-                    Tr::tr("Firmware Update"),[m
[31m-                    Tr::tr("Cannot open firmware.txt!"));[m
[31m-            }[m
         }[m
         else[m
         {[m
[31m-            QMessageBox::critical(Core::ICore::dialogParent(),[m
[32m+[m[32m            QMessageBox::information(Core::ICore::dialogParent(),[m
                 Tr::tr("Firmware Update"),[m
[31m-                Tr::tr("Error: %L1!").arg(file.errorString()));[m
[32m+[m[32m                Tr::tr("Your OpenMV Cam's firmware is up to date."));[m
[32m+[m
[32m+[m[32m            if(QMessageBox::question(Core::ICore::dialogParent(),[m
[32m+[m[32m                Tr::tr("Firmware Update"),[m
[32m+[m[32m                Tr::tr("Need to reset your OpenMV Cam's firmware to the release version?"),[m
[32m+[m[32m                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)[m
[32m+[m[32m            == QMessageBox::Yes)[m
[32m+[m[32m            {[m
[32m+[m[32m                int answer = QMessageBox::question(Core::ICore::dialogParent(),[m
[32m+[m[32m                    Tr::tr("Firmware Update"),[m
[32m+[m[32m                    Tr::tr("Erase the internal file system?"),[m
[32m+[m[32m                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);[m
[32m+[m
[32m+[m[32m                if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))[m
[32m+[m[32m                {[m
[32m+[m[32m                    disconnectClicked();[m
[32m+[m
[32m+[m[32m                    if(ExtensionSystem::PluginManager::specForPlugin(this)->state() != ExtensionSystem::PluginSpec::Stopped)[m
[32m+[m[32m                    {[m
[32m+[m[32m                        connectClicked(true, QString(), answer == QMessageBox::Yes);[m
[32m+[m[32m                    }[m
[32m+[m[32m                }[m
[32m+[m[32m            }[m
         }[m
     }[m
     else[m
[1mdiff --git a/src/plugins/openmv/tools/imx.cpp b/src/plugins/openmv/tools/imx.cpp[m
[1mindex a5e7d93189b..7c6ba2f9ced 100644[m
[1m--- a/src/plugins/openmv/tools/imx.cpp[m
[1m+++ b/src/plugins/openmv/tools/imx.cpp[m
[36m@@ -21,61 +21,55 @@[m [mnamespace Internal {[m
 [m
 QMutex imx_working;[m
 [m
[31m-QList<QPair<int, int> > imxVidPidList(bool spd_host, bool bl_host)[m
[32m+[m[32mQList<QPair<int, int> > imxVidPidList(const QJsonDocument &settings, bool spd_host, bool bl_host)[m
 {[m
     QList<QPair<int, int> > pidvidlist;[m
 [m
[31m-    QFile imxSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/imx.txt")).toString());[m
[31m-[m
[31m-    if(imxSettings.open(QIODevice::ReadOnly))[m
[32m+[m[32m    for(const QJsonValue &val : settings.object().value(QStringLiteral("boards")).toArray())[m
     {[m
[31m-        QJsonParseError error;[m
[31m-        QJsonDocument doc = QJsonDocument::fromJson(imxSettings.readAll(), &error);[m
[32m+[m[32m        QJsonObject obj = val.toObject();[m
 [m
[31m-        if(error.error == QJsonParseError::NoError)[m
[32m+[m[32m        if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("imx"))[m
         {[m
[31m-            for(const QJsonValue &val : doc.array())[m
[32m+[m[32m            QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();[m
[32m+[m
[32m+[m[32m            if(spd_host)[m
             {[m
[31m-                QJsonObject obj = val.toObject();[m
[32m+[m[32m                QStringList pidvid = bootloaderSettings.value(QStringLiteral("sdphost_pidvid")).toString().split(QChar(','));[m
 [m
[31m-                if(spd_host)[m
[32m+[m[32m                if(pidvid.size() == 2)[m
                 {[m
[31m-                    QStringList pidvid = obj.value(QStringLiteral("sdphost_pidvid")).toString().split(QChar(','));[m
[32m+[m[32m                    bool okay_pid; int pid = pidvid.at(0).toInt(&okay_pid, 16);[m
[32m+[m[32m                    bool okay_vid; int vid = pidvid.at(1).toInt(&okay_vid, 16);[m
 [m
[31m-                    if(pidvid.size() == 2)[m
[32m+[m[32m                    if(okay_pid && okay_vid)[m
                     {[m
[31m-                        bool okay_pid; int pid = pidvid.at(0).toInt(&okay_pid, 16);[m
[31m-                        bool okay_vid; int vid = pidvid.at(1).toInt(&okay_vid, 16);[m
[32m+[m[32m                        QPair<int, int> entry(pid, vid);[m
 [m
[31m-                        if(okay_pid && okay_vid)[m
[32m+[m[32m                        if(!pidvidlist.contains(entry))[m
                         {[m
[31m-                            QPair<int, int> entry(pid, vid);[m
[31m-[m
[31m-                            if(!pidvidlist.contains(entry))[m
[31m-                            {[m
[31m-                                pidvidlist.append(entry);[m
[31m-                            }[m
[32m+[m[32m                            pidvidlist.append(entry);[m
                         }[m
                     }[m
                 }[m
[32m+[m[32m            }[m
 [m
[31m-                if(bl_host)[m
[32m+[m[32m            if(bl_host)[m
[32m+[m[32m            {[m
[32m+[m[32m                QStringList pidvid = bootloaderSettings.value(QStringLiteral("blhost_pidvid")).toString().split(QChar(','));[m
[32m+[m
[32m+[m[32m                if(pidvid.size() == 2)[m
                 {[m
[31m-                    QStringList pidvid = obj.value(QStringLiteral("blhost_pidvid")).toString().split(QChar(','));[m
[32m+[m[32m                    bool okay_pid; int pid = pidvid.at(0).toInt(&okay_pid, 16);[m
[32m+[m[32m                    bool okay_vid; int vid = pidvid.at(1).toInt(&okay_vid, 16);[m
 [m
[31m-                    if(pidvid.size() == 2)[m
[32m+[m[32m                    if(okay_pid && okay_vid)[m
                     {[m
[31m-                        bool okay_pid; int pid = pidvid.at(0).toInt(&okay_pid, 16);[m
[31m-                        bool okay_vid; int vid = pidvid.at(1).toInt(&okay_vid, 16);[m
[32m+[m[32m                        QPair<int, int> entry(pid, vid);[m
 [m
[31m-                        if(okay_pid && okay_vid)[m
[32m+[m[32m                        if(!pidvidlist.contains(entry))[m
                         {[m
[31m-                            QPair<int, int> entry(pid, vid);[m
[31m-[m
[31m-                            if(!pidvidlist.contains(entry))[m
[31m-                            {[m
[31m-                                pidvidlist.append(entry);[m
[31m-                            }[m
[32m+[m[32m                            pidvidlist.append(entry);[m
                         }[m
                     }[m
                 }[m
[36m@@ -86,9 +80,9 @@[m [mQList<QPair<int, int> > imxVidPidList(bool spd_host, bool bl_host)[m
     return pidvidlist;[m
 }[m
 [m
[31m-QStringList imxGetAllDevices(bool spd_host, bool bl_host)[m
[32m+[m[32mQStringList imxGetAllDevices(const QJsonDocument &settings, bool spd_host, bool bl_host)[m
 {[m
[31m-    QList<QPair<int, int> > pidvidlist = imxVidPidList(spd_host, bl_host);[m
[32m+[m[32m    QList<QPair<int, int> > pidvidlist = imxVidPidList(settings, spd_host, bl_host);[m
 [m
     if(!imx_working.tryLock()) return QStringList();[m
 [m
[1mdiff --git a/src/plugins/openmv/tools/imx.h b/src/plugins/openmv/tools/imx.h[m
[1mindex c9351fc4275..8ac9f8d6878 100644[m
[1m--- a/src/plugins/openmv/tools/imx.h[m
[1m+++ b/src/plugins/openmv/tools/imx.h[m
[36m@@ -8,9 +8,9 @@[m
 namespace OpenMV {[m
 namespace Internal {[m
 [m
[31m-QList<QPair<int, int> > imxVidPidList(bool spd_host = true, bool bl_host = true);[m
[32m+[m[32mQList<QPair<int, int> > imxVidPidList(const QJsonDocument &settings, bool spd_host = true, bool bl_host = true);[m
 // Returns PID/VID of SPD and BL bootloaders on the system.[m
[31m-QStringList imxGetAllDevices(bool spd_host = true, bool bl_host = true);[m
[32m+[m[32mQStringList imxGetAllDevices(const QJsonDocument &settings, bool spd_host = true, bool bl_host = true);[m
 // Returns true if the BL bootloader is present.[m
 bool imxGetDeviceSupported();[m
 bool imxGetDevice(QJsonObject &obj);[m
