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

#ifndef OPENMVPLUGIN_H
#define OPENMVPLUGIN_H

#include <QtConcurrent>
#include <QtCore>
#include <QtGui>
#include <QtCore>
#include "qzip/qzipreader.h"
#include "qzip/qzipwriter.h"
#include <QtNetwork>
#include <QtWidgets>

#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/fileutils.h>
#include <coreplugin/fancyactionbar.h>
#include <coreplugin/fancytabwidget.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/statusbarmanager.h>
#include <coreplugin/openmvpluginescapecodeparser.h>
#include <coreplugin/outputwindow.h>
#include <syntax-highlighting/src/lib/definition_p.h>
#include <syntax-highlighting/src/lib/keywordlist_p.h>
#include <texteditor/highlighterhelper.h>
#include <texteditor/highlighter.h>
#include <texteditor/codeassist/completionassistprovider.h>
#include <texteditor/codeassist/keywordscompletionassist.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>
#include <python/pythonlanguageclient.h>
#include <utils/appmainwindow.h>
#include <utils/checkablemessagebox.h>
#include <utils/elidinglabel.h>
#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/mimeutils.h>
#include <utils/utilsicons.h>
#include <utils/pathchooser.h>
#include <utils/proxyaction.h>
#include <utils/styledbar.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>
#include <utils/tooltip/tooltip.h>

#if defined(Q_OS_WIN)
    #include "openmveject.h"
#elif defined(Q_OS_LINUX)
    #include <dirent.h>
    #include <unistd.h>
#elif defined(Q_OS_MAC)
    #include <unistd.h>
#endif

#include "openmvcamerasettings.h"
#include "openmvdataseteditor.h"
#include "openmvmodelzoo.h"
#include "openmvpluginserialport.h"
#include "openmvpluginio.h"
#include "openmvpluginfb.h"
#include "openmvprofile.h"
#include "openmvromfs.h"
#include "openmvterminal.h"
#include "histogram/openmvpluginhistogram.h"
#include "tools/alif.h"
#include "tools/bossac.h"
#include "tools/dfu-util.h"
#include "tools/driveserialnumber.h"
#include "tools/edgeimpulse.h"
#include "tools/hardwaremonitor.h"
#include "tools/imx.h"
#include "tools/keypointseditor.h"
#include "tools/loaderdialog.h"
#include "tools/myqserialportinfo.h"
#include "tools/picotool.h"
#include "tools/settingseditor.h"
#include "tools/stcubeprogrammer.h"
#include "tools/tag16h5.h"
#include "tools/tag25h7.h"
#include "tools/tag25h9.h"
#include "tools/tag36h10.h"
#include "tools/tag36h11.h"
#include "tools/tag36artoolkit.h"
#include "tools/thresholdeditor.h"
#include "tools/videotools.h"

#define LIGHT_SPLASH_PATH ":/openmv/openmv-media/splash/openmv-splash/splash-small.png"
#define LIGHT_SPLASH_HIDPI_PATH ":/openmv/openmv-media/splash/openmv-splash/splash-large.png"
#define DARK_SPLASH_PATH ":/openmv/openmv-media/splash/openmv-splash-slate/splash-small.png"
#define DARK_SPLASH_HIDPI_PATH ":/openmv/openmv-media/splash/openmv-splash-slate/splash-large.png"
#define CONNECT_PATH ":/openmv/images/connect.png"
#define CONNECT_USB_DARK_PATH ":/openmv/images/connect-usb-dark.png"
#define CONNECT_WIFI_DARK_PATH ":/openmv/images/connect-wifi-dark.png"
#define CONNECT_USB_WIFI_DARK_PATH ":/openmv/images/connect-usb-wifi-dark.png"
#define CONNECT_USB_LIGHT_PATH ":/openmv/images/connect-usb-light.png"
#define CONNECT_WIFI_LIGHT_PATH ":/openmv/images/connect-wifi-light.png"
#define CONNECT_USB_WIFI_LIGHT_PATH ":/openmv/images/connect-usb-wifi-light.png"
#define DISCONNECT_PATH ":/openmv/images/disconnect.png"
#define START_PATH ":/openmv/projectexplorer/images/run.png"
#define STOP_PATH ":/openmv/images/application-exit.png"
#define NEW_FOLDER_PATH ":/openmv/images/new-folder.png"
#define SNAPSHOT_PATH ":/openmv/images/snapshot.png"

#define SETTINGS_GROUP "OpenMV"
#define EDITOR_MANAGER_STATE "EditorManagerState"
#define MSPLITTER_STATE "MSplitterState"
#define HSPLITTER_STATE "HSplitterState"
#define VSPLITTER_STATE "VSplitterState"
#define AUTO_RECONNECT_STATE "AutoReconnectState"
#define STOP_SCRIPT_CONNECT_DISCONNECT_STATE "StopScriptConnectDisconnect"
#define VIEWER_STOP_SCRIPT_CONNECT_DISCONNECT_STATE "ViewerStopScriptConnectDisconnect"
#define ENABLE_SYNCING_IMPORTS_STATE "EnableSyncingImports"
#define ENABLE_FILTERING_EXAMPLES_STATE "EnableFilteringExamples"
#define ZOOM_STATE "ZoomState"
#define OUTPUT_WINDOW_FONT_ZOOM_STATE "OutputWindowFontZoomState"
#define JPG_COMPRESS_STATE "JPGCompressState"
#define DISABLE_FRAME_BUFFER_STATE "DisableFrameBufferState"
#define HISTOGRAM_COLOR_SPACE_STATE "HistogramColorSpace"
#define DONT_SHOW_EXAMPLES_AGAIN "DontShowExamplesAgain"
#define DONT_SHOW_COPILOT_AGAIN "DontShowCopilotAgain"
#define DONT_SHOW_LED_STATES_AGAIN "DontShowLEDStatesAgain"
#define DONT_SHOW_UPGRADE_FW_AGAIN "DontShowUpgradeFWAgain"
#define LAST_FORM_KEY "LastFormKey"
#define LAST_FIRMWARE_PATH "LastFirmwarePath"
#define LAST_FIRMWARE_HISTORY "LastFirmwareHistory"
#define LAST_DFU_ACTION "LastDFUAction"
#define LAST_DFU_FLASH_FS_ERASE_STATE "LastDFUFlashFSEraseState"
#define LAST_DFU_RESET_ROM_FS_STATE "LastDFUResetROMFSState"
#define LAST_DFU_UPDATE_ROM_FS_STATE "LastDFUUpdateROMFSState"
#define LAST_BOARD_TYPE_STATE "LastBoardTypeState"
#define LAST_BOARD_TYPE_STATE_2 "LastBoardTypeState2"
#define LAST_BOARD_TYPE_STATE_GET "LastBoardTypeStateGet"
#define LAST_BOARD_TYPE_STATE_ROMFS "LastBoardTypeStateROMFS"
#define LAST_BOARD_TYPE_STATE_IMX "LastBoardTypeStateIMX"
#define LAST_BOARD_TYPE_STATE_ALIF "LastBoardTypeStateAlif"
#define LAST_SERIAL_PORT_STATE "LastSerialPortState"
#define LAST_DFU_PORT_STATE "LastDFUPortState"
#define LAST_SAVE_IMAGE_PATH "LastSaveImagePath"
#define LAST_SAVE_TEMPLATE_PATH "LastSaveTemplatePath"
#define LAST_SAVE_DESCRIPTOR_PATH "LastSaveDescriptorPath"
#define LAST_OPEN_TERMINAL_SELECT "LastOpenTerminalSelect"
#define LAST_OPEN_TERMINAL_SERIAL_PORT "LastOpenTerminalSerialPort"
#define LAST_OPEN_TERMINAL_SERIAL_PORT_BAUD_RATE "LastOpenTerminalSerialPortBaudRate"
#define LAST_OPEN_TERMINAL_UDP_TYPE_SELECT "LastOpenTerminalUDPTypeSelect"
#define LAST_OPEN_TERMINAL_UDP_PORT "LastOpenTerminalUDPPort"
#define LAST_OPEN_TERMINAL_UDP_SERVER_PORT "LastOpenTerminalSereverUDPPort"
#define LAST_OPEN_TERMINAL_TCP_TYPE_SELECT "LastOpenTerminalTCPTypeSelect"
#define LAST_OPEN_TERMINAL_TCP_PORT "LastOpenTerminalTCPPort"
#define LAST_OPEN_TERMINAL_TCP_SERVER_PORT "LastOpenTerminalSereverTCPPort"
#define LAST_THRESHOLD_EDITOR_STATE "LastThresholdEditorState"
#define LAST_THRESHOLD_EDITOR_PATH "LastThresholdEditorPath"
#define LAST_EDIT_KEYPOINTS_STATE "LastEditKeyointsState"
#define LAST_EDIT_KEYPOINTS_PATH "LastEditKeypointsPath"
#define LAST_MERGE_KEYPOINTS_OPEN_PATH "LastMergeKeypointsOpenPath"
#define LAST_MERGE_KEYPOINTS_SAVE_PATH "LastMergeKeypointsSavePath"
#define LAST_APRILTAG_RANGE_MIN "LastAprilTagRangeMin"
#define LAST_APRILTAG_RANGE_MAX "LastAprilTagRangeMax"
#define LAST_APRILTAG_INCLUDE "LastAprilTagInclude"
#define LAST_APRILTAG_PATH "LastAprilTagPath"
#define LAST_COPY_SCRIPT_NO_CAM_PATH "LastCopyScriptNoCamPath"
#define LAST_COPY_SCRIPT_WITH_CAM_PATH "LastCopyScriptWithCamPath"
#define LAST_COPY_SCRIPT_OPEN_PATH "LastCopyScriptOpenPath"
#define LAST_VIEWER_RUN_SCRIPT_PATH "LastViewerRunScriptPath"
#define LAST_MODEL_NO_CAM_PATH "LastModelNoCamPath"
#define LAST_MODEL_WITH_CAM_PATH "LastModelWithCamPath"
#define LAST_MODEL_CONVERT_OPEN_PATH "LastModelConvertOpenPath"
#define LAST_DATASET_EDITOR_PATH "LastDatasetEditorPath"
#define LAST_DATASET_EDITOR_LOADED "LastDatasetEditorLoaded"
#define LAST_DATASET_EDITOR_EXPORT_PATH "LastDatasetEditorExportPath"
#define LAST_USE_GET_STATE "LastUseGetState"
#define LAST_FRAME_DUMP_SPACING "LastFrameDumpSpacing"
#define LAST_GET_SCRIPT_RUNNING_SPACING "LastGetScriptRunningSpacing"
#define LAST_GET_TX_BUFFER_SPACING "LastGetTxBufferSpacing"
#define LAST_GET_STATE_SPACING "LastGetStateSpacing"
#define LAST_READ_PROFILE_SPACING "LastReadProfileSpacing"
#define LAST_DYNAMIC_FRAME_READING "LastDynamicFrameReading"
#define LAST_ROMFS_DIALOG_GEOMETRY "LastROMFSDialogGeometry"
#define LAST_ROMFS_DIALOG_OPEN_FILE_PATH "LastROMFSDialogFilePath"
#define LAST_ROMFS_DIALOG_NEW_FOLDER_NAME "LastROMFSDialogNewFolderName"
#define LAST_ROMFS_DIALOG_SAVE_AS_PATH "LastROMFSDialogSaveAsPath"
#define LAST_ROMFS_DIALOG_SAVE_PATH "LastROMFSDialogSavePath"
#define LAST_ROMFS_DIALOG_OPEN_PATH "LastROMFSDialogOpenPath"
#define LAST_ROMFS_DIALOG_ACTION "LastROMFSDialogAction"
#define RESOURCES_MAJOR "ResourcesMajor"
#define RESOURCES_MINOR "ResourcesMinor"
#define RESOURCES_PATCH "ResourcesPatch"

#define SERIAL_PORT_SETTINGS_GROUP "OpenMVSerialPort"
#define OPEN_TERMINAL_SETTINGS_GROUP "OpenMVOpenTerminal"
#define OPEN_TERMINAL_DISPLAY_NAME "DisplayName"
#define OPEN_TERMINAL_OPTION_INDEX "OptionIndex"
#define OPEN_TERMINAL_COMMAND_STR "CommandStr"
#define OPEN_TERMINAL_COMMAND_VAL "CommandVal"

#define RECONNECTS_MAX 2
#define OLD_API_MAJOR 1
#define OLD_API_MINOR 7
#define OLD_API_PATCH 0
#define OLD_API_BOARD "OPENMV2"

#define LEARN_MTU_ADDED_MAJOR 9
#define LEARN_MTU_ADDED_MINOR 9
#define LEARN_MTU_ADDED_PATCH 9

#define OPENMV_DISK_ADDED_MAJOR 3
#define OPENMV_DISK_ADDED_MINOR 2
#define OPENMV_DISK_ADDED_PATCH 0
#define OPENMV_DISK_ADDED_NAME "/.openmv_disk"

#define OPENMV_DBG_PROTOCOL_CHNAGE_MAJOR 3
#define OPENMV_DBG_PROTOCOL_CHNAGE_MINOR 5
#define OPENMV_DBG_PROTOCOL_CHNAGE_PATCH 3

#define OPENMV_RGB565_BYTE_REVERSAL_FIXED_MAJOR 3
#define OPENMV_RGB565_BYTE_REVERSAL_FIXED_MINOR 8
#define OPENMV_RGB565_BYTE_REVERSAL_FIXED_PATCH 0

#define OPENMV_NEW_PIXFORMAT_MAJOR 4
#define OPENMV_NEW_PIXFORMAT_MINOR 1
#define OPENMV_NEW_PIXFORMAT_PATCH 3

#define OPENMV_ADD_MAIN_TERMINAL_INPUT_MAJOR 4
#define OPENMV_ADD_MAIN_TERMINAL_INPUT_MINOR 3
#define OPENMV_ADD_MAIN_TERMINAL_INPUT_PATCH 2

#define OPENMV_ADD_TIME_INPUT_MAJOR 4
#define OPENMV_ADD_TIME_INPUT_MINOR 3
#define OPENMV_ADD_TIME_INPUT_PATCH 2

#define OPENMV_ADD_GET_STATE_MAJOR 4
#define OPENMV_ADD_GET_STATE_MINOR 5
#define OPENMV_ADD_GET_STATE_PATCH 6

#define OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MAJOR 4
#define OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MINOR 6
#define OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_PATCH 1

#define OPENMV_FORCE_ROMFS_UPGRADE_MAJOR 4
#define OPENMV_FORCE_ROMFS_UPGRADE_MINOR 7
#define OPENMV_FORCE_ROMFS_UPGRADE_PATCH 0

#define FRAME_SIZE_DUMP_SPACING     5 // in ms
#define GET_SCRIPT_RUNNING_SPACING  100 // in ms
#define GET_TX_BUFFER_SPACING       5 // in ms
#define GET_STATE_SPACING           25 // in ms
#define READ_PROFILE_SPACING        500 // in ms

#define FPS_AVERAGE_BUFFER_DEPTH    100 // in samples
#define WIFI_PORT_RETIRE            20 // in seconds
#define ERROR_FILTER_MAX_SIZE       1000 // in chars
#define FPS_TIMER_EXPIRATION_TIME   2000 // in milliseconds
#define RESET_TO_DFU_SEARCH_TIME    2000 // in milliseconds

#define FILE_FLUSH_BYTES            1024 // Extra disk activity to flush changes...
#define FLASH_SECTOR_ERASE          4096 // Flash sector size in bytes.
#define FOLDER_SCAN_TIME            10000 // in ms

#define FORCE_SHUTDOWN_TIMEOUT      10000 // in ms

namespace OpenMV {
namespace Internal {

class OpenMVPluginCompletionAssistProvider : public TextEditor::CompletionAssistProvider
{

public:

    OpenMVPluginCompletionAssistProvider(const QStringList &variables,
                                         const QStringList &classes, const QMap<QString, QStringList> &classArgs,
                                         const QStringList &functions, const QMap<QString, QStringList> &functionArgs,
                                         const QStringList &methods, const QMap<QString, QStringList> &methodArgs,
                                         QObject *parent) : CompletionAssistProvider(parent)
    {
        m_keywords = TextEditor::Keywords(variables,
                                          classes, classArgs,
                                          functions, functionArgs,
                                          methods, methodArgs);
    }

    TextEditor::IAssistProcessor *createProcessor(const TextEditor::AssistInterface *assistInterface) const
    {
        Q_UNUSED(assistInterface)

        return new TextEditor::KeywordsCompletionAssistProcessor(m_keywords);
    }

    int activationCharSequenceLength() const
    {
        return 1;
    }

    bool isActivationCharSequence(const QString &sequence) const
    {
        return (sequence.at(0) == QLatin1Char('.')) || (sequence.at(0) == QLatin1Char('(')) || (sequence.at(0) == QLatin1Char(','));
    }

private:

    TextEditor::Keywords m_keywords;
};

typedef struct importData
{
    QString moduleName, modulePath;
    QByteArray moduleHash;
}
importData_t;

typedef QList<importData_t> importDataList_t;

QByteArray loadFilter(const QByteArray &data, bool stripComments = true);
importDataList_t loadFolder(const QString &rootPath, const QString &path, bool flat);

class LoadFolderThread: public QObject
{
    Q_OBJECT

    public: explicit LoadFolderThread(const QString &path, bool flat) { m_path = path; m_flat = flat; }
    public slots: void loadFolderSlot() { emit folderLoaded(loadFolder(m_path, m_path, m_flat)); }
    signals: void folderLoaded(const importDataList_t &output);
    private: QString m_path; bool m_flat;
};

class wifiPort_t
{

public:

    QString addressAndPort;
    QString name;
    QTime time;

    bool operator ==(const wifiPort_t &port) const
    {
        return (addressAndPort == port.addressAndPort) && (name == port.name);
    }
};

bool validPort(const QJsonDocument &settings, const QString &serialNumberFilter, const MyQSerialPortInfo &port);

QPair<QStringList, QStringList> filterPorts(const QJsonDocument &settings,
                                            const QString &serialNumberFilter,
                                            bool forceBootloader,
                                            const QList<wifiPort_t> &availableWifiPorts);

class ScanSerialPortsThread: public QObject
{
    Q_OBJECT

    public: explicit ScanSerialPortsThread(const QJsonDocument &settings, const QString &serialNumberFilter) {
        m_firmwareSettings = settings; m_serialNumberFilter = serialNumberFilter;
    }
    public slots: void scanSerialPortsSlot() {
        emit serialPorts(filterPorts(m_firmwareSettings, m_serialNumberFilter, true, QList<wifiPort_t>()));
    }
    signals: void serialPorts(const QPair<QStringList, QStringList> &output);
    private: QJsonDocument m_firmwareSettings; QString m_serialNumberFilter;
};

class ScanDriveThread: public QObject
{
    Q_OBJECT

    public: explicit ScanDriveThread() {
    }
    public slots: void scanDrivesSlot() {
        QList<QPair<QString, QString> > drives;
        for(const QStorageInfo &info : QStorageInfo::mountedVolumes()) {
            if(info.isValid()
            && info.isReady()
            && (!info.isRoot())
            && (!info.isReadOnly())
            && (QString::fromUtf8(info.fileSystemType()).contains(QStringLiteral("fat"), Qt::CaseInsensitive) ||
                QString::fromUtf8(info.fileSystemType()).contains(QStringLiteral("msdos"), Qt::CaseInsensitive) ||
                QString::fromUtf8(info.fileSystemType()).contains(QStringLiteral("fuseblk"), Qt::CaseInsensitive))
            && ((!Utils::HostOsInfo::isMacHost()) || info.rootPath().startsWith(QStringLiteral("/volumes/"), Qt::CaseInsensitive))
            && ((!Utils::HostOsInfo::isLinuxHost()) || info.rootPath().startsWith(QStringLiteral("/media/"), Qt::CaseInsensitive) ||
                info.rootPath().startsWith(QStringLiteral("/mnt/"), Qt::CaseInsensitive) ||
                info.rootPath().startsWith(QStringLiteral("/run/"), Qt::CaseInsensitive)))
            {
                drives.append(QPair<QString, QString>(info.rootPath(), driveSerialNumber(info.rootPath())));
            }
        }
        emit driveScanned(drives);
    }
    signals: void driveScanned(const QList<QPair<QString, QString> > &output);
};

// New version stamps from a development-resource sync (empty field = unchanged).
// Produced by the worker thread, applied (settings + filter reload) on the GUI thread.
struct DevSyncOutcome
{
    QString examplesVersion;
    QString docsStamp;
    QString firmwareVersion;
};

class OpenMVPlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "OpenMV.json")

public:

    explicit OpenMVPlugin();
    bool initialize(const QStringList &arguments, QString *errorMessage);
    void extensionsInitialized();
    bool delayedInitialize();
    ExtensionSystem::IPlugin::ShutdownFlag aboutToShutdown();
    QObject *remoteCommand(const QStringList &options, const QString &workingDirectory, const QStringList &arguments);
    QList<QObject *> createTestObjects() const { return QList<QObject *>(); }

public slots: // private

    void registerOpenMVCam(const QString board, const QString id);
    bool registerOpenMVCamDialog(const QString board, const QString id);
    void packageUpdate();
    void bootloaderClicked();
    void editRomfsClicked(bool fromConnect = false, bool newRomfs = false);
    void resetRomfsClicked();
    void installTheLatestDevelopmentRelease();
    void connectClicked(bool forceBootloader = false,
                        QString forceFirmwarePath = QString(),
                        bool forceFlashFSErase = false,
                        bool justEraseFlashFs = false,
                        bool installTheLatestDevelopmentFirmware = false,
                        bool waitForCamera = false,
                        QString previousMapping = QString(),
                        OpenMVROMFSAccess romfsAccess = OPENMV_ROMFS_NONE,
                        bool forceBootloaderEntry = false);
    void disconnectClicked(bool reset = false, bool enterBootloader = false);
    void startClicked();
    void stopClicked();
    void processEvents();
    void refreshFpsButton(); // renders m_fpsButton from m_fpsIde / m_fpsCamera / m_fpsCameraValid
    void errorFilter(const QByteArray &data);
    void configureSettings();
    void saveScript();
    void saveImage(const QPixmap &data);
    void saveTemplate(const QRect &rect);
    void saveDescriptor(const QRect &rect);
    QMultiMap<QString, QAction *> aboutToShowExamplesRecursive(const QString &path, QMenu *parent, bool notExamples = false);
    void updateCam(bool forceYes = false);
    void setPortPath(bool silent = false);
    void setSpacing();
    void openTerminalAboutToShow();
    QList<int> openThresholdEditor(const QVariant parameters = QVariant());
    void openKeypointsEditor();
    void openAprilTagGenerator(apriltag_family_t *family);

    void showCopilotDialog();
    void showLEDStatesDialog();
    void showExamplesDialog();

signals:

    void workingDone(); // private
    void disconnectDone(); // private

private:

    bool getTheLatestDevelopmentFirmware(const QString &arch, QString *path, const QString &firmwareFileName, const QString &originalFirmwareFolder);
    QList<QPair<QString, QString> > querySerialPorts(const QStringList &portList);

    // Release-notes (changelog) links. Web URLs target the rolling-latest "dev"
    // docs channel (used by the update notifications, since the announced
    // version is not installed yet). Local URLs point at the bundled docs that
    // shipped with this IDE (used by the About dialog and Help menu), falling
    // back to the per-product changelog index if the exact version page is
    // missing. product is "ide" or "firmware".
    static QUrl webChangelogUrl(const QString &product, int major, int minor, int patch);
    static QUrl localChangelogUrl(const QString &product, const QString &version);
    static void openUrlOrWarn(const QUrl &url);

    // --- Development resource cache ------------------------------------------
    // Dev examples/docs/firmware are published separately from the released
    // resources (examples + firmware via download.openmv.io/studio/manifest.json,
    // docs via the openmv-doc "development" release). They are cached next to the
    // released folders with a "-dev" suffix and used when a development cam is
    // attached. See openmvpluginconnect.cpp.

    // Returns "<name>-dev" when a development cam is attached and that cache exists,
    // otherwise "<name>" -- so callers transparently read dev or released resources.
    QString devResourceFolder(const QString &name) const;

    // Load the example board/sensor filters from <examplesFolder>/index.csv (the
    // released "examples" folder, or "examples-dev" for a development cam).
    void loadExampleFilters(const QString &examplesFolder);

public:
    enum DevResourcePart { DevExamples = 1, DevDocs = 2, DevFirmware = 4 };
private:

    // Sync the requested dev caches on a worker thread (download + extract happen off
    // the GUI thread), reporting to the Qt Creator progress popup. Non-blocking.
    void backgroundSyncDevResources(int parts);

    // Apply a finished sync's results on the GUI thread: persist the version stamps and
    // reload the dev example filters if the examples were refreshed.
    void applyDevSyncOutcome(const DevSyncOutcome &out);

    // Sync the cached dev firmware (54.7 MB, only when the dev version changed) with a
    // modal progress dialog and wait for it -- used right before flashing dev firmware
    // so the user sees they must wait. Returns true when firmware-dev is ready.
    bool syncDevFirmwareBlocking();

    void openmvInternalBootloader(const QString &forceFirmwarePath,
                                  bool forceFlashFSErase,
                                  bool justEraseFlashFs,
                                  bool installTheLatestDevelopmentFirmware,
                                  const QString &previousMapping,
                                  const QString &selectedPort,
                                  bool forceBootloaderBricked,
                                  bool previousMappingSet,
                                  const QString &originalFirmwareFolder,
                                  const QString &firmwarePath,
                                  int originalEraseFlashSectorStart,
                                  int originalEraseFlashSectorEnd,
                                  int originalEraseFlashSectorAllStart,
                                  int originalEraseFlashSectorAllEnd,
                                  const QJsonObject &originalFallbackBootloaderSettings,
                                  const QString &originalDfuVidPid,
                                  bool dfuNoDialogs,
                                  OpenMVROMFSAccess romfsAccess = OPENMV_ROMFS_NONE,
                                  bool forceBootloaderEntry = false);
    void openmvRepairingBootloader(bool forceFlashFSErase,
                                   QString previousMapping,
                                   const QString &originalDfuVidPid,
                                   bool dfuNoDialogs,
                                   const QString &firmwarePath,
                                   bool repairingBootloader,
                                   bool useSTCubeProgrammer = false);
    void openmvDFUBootloader(bool forceFlashFSErase,
                             bool justEraseFlashFs,
                             bool installTheLatestDevelopmentFirmware,
                             const QString &firmwarePath,
                             const QString &selectedDfuDevice,
                             OpenMVROMFSAccess romfsAccess = OPENMV_ROMFS_NONE,
                             const QString &extraMessage = QString(),
                             bool forceBootloaderEntry = false);
    void openmvIMXBootloader(const QString &forceFirmwarePath,
                             bool forceFlashFSErase,
                             bool justEraseFlashFs,
                             bool installTheLatestDevelopmentFirmware,
                             const QString &firmwarePath,
                             Utils::QtcSettings *settings,
                             bool forceBootloaderBricked,
                             QString originalFirmwareFolder,
                             const QString &selectedDfuDevice,
                             OpenMVROMFSAccess romfsAccess = OPENMV_ROMFS_NONE);
    void openmvAlifBootloader(const QString &forceFirmwarePath,
                              bool forceFlashFSErase,
                              bool justEraseFlashFs,
                              Utils::QtcSettings *settings,
                              QString originalFirmwareFolder,
                              const QString &selectedDfuDevice,
                              bool forceBootloaderEntry = false);
    void openmvArduinoDFUBootloader(bool forceFlashFSErase,
                                    bool justEraseFlashFs,
                                    bool installTheLatestDevelopmentFirmware,
                                    const QString &firmwarePath,
                                    const QString &selectedDfuDevice,
                                    OpenMVROMFSAccess romfsAccess = OPENMV_ROMFS_NONE);
    void openmvBossacBootloader(bool forceFlashFSErase,
                                bool justEraseFlashFs,
                                const QString &firmwarePath,
                                const QString &selectedDfuDevice,
                                OpenMVROMFSAccess romfsAccess = OPENMV_ROMFS_NONE);
    void openmvPictotoolBootloader(bool forceFlashFSErase,
                                   bool justEraseFlashFs,
                                   const QString &firmwarePath,
                                   const QString &selectedDfuDevice,
                                   OpenMVROMFSAccess romfsAccess = OPENMV_ROMFS_NONE);
    QString dfuInterfaceErrorText(const QJsonObject &boardObject,
                                  const QString &selectedFileName);

    QStringList m_resourceFoldersToCopy;
    QStringList m_resourceFoldersToDelete;

    QJsonDocument m_firmwareSettings;

    bool m_viewerMode;

    bool m_autoConnect;
    QString m_autoUpdate;
    bool m_autoErase;
    bool m_autoRun;
    bool m_disableStop;

    QTemporaryDir m_tempDir;

    OpenMVPluginSerialPort *m_ioport;
    OpenMVPluginIO *m_iodevice;

    QElapsedTimer m_frameSizeDumpTimer;
    QElapsedTimer m_getScriptRunningTimer;
    QElapsedTimer m_getTxBufferTimer;
    QElapsedTimer m_getStateTimer;
    QElapsedTimer m_readProfileTimer;

    QElapsedTimer m_timer;
    QQueue<qint64> m_queue;
    QQueue<double> m_cameraQueue; // sliding window of on-camera FPS samples (same depth as m_queue)
    QTimer *m_processEventsTimer;
    ScanDriveThread *m_scanDriveThread;
    HardwareMonitor *m_hardwareMonitor;
    QTimer *m_serialScanTimer;
    QTimer *m_driveScanTimer;
    LoadFolderThread *examplesLoadFolderThread;
    QTimer *m_scanExamplesTimer;
    LoadFolderThread *documentsLoadFolderThread;
    QTimer *m_scanDocumentsTimer;

    QList<bool> m_boardPresentStringListHistory;
    QList<bool> m_boardPresentDFUDevicesHistory;
    bool m_nonDFUBoardPresent;
    bool m_boardPresent;
    bool m_working;
    bool m_connected;
    bool m_running;
    QMetaObject::Connection m_connect_disconnect;
    int m_major;
    int m_minor;
    int m_patch;
    // True when the attached cam reports a firmware version newer than the released
    // firmware we ship -- i.e. it is running a development build, so the IDE prefers
    // the cached dev examples/docs that match it.
    bool m_developmentCam;
    QString m_boardTypeFolder;
    QString m_fullBoardType;
    QString m_boardType;
    QString m_boardId;
    int m_boardVID;
    int m_boardPID;
    QString m_sensorType;
    int m_reconnects;
    // Sticky for the whole firmware-update/bootloader operation, which tears
    // m_working/m_connected down and back up across its re-entrant stages.
    // Auto-reconnect must not hijack one of those transient idle windows.
    bool m_firmwareUpdateInProgress;
    QString m_portName;
    QString m_portPath;
    QString m_portDriveSerialNumber;
    QString m_formKey;

    QString m_serialNumberFilter;
    QRegularExpression m_errorFilterRegex;
    QString m_errorFilterString;
    bool m_useGetState;
    int m_frameSizeDumpSpacing;
    int m_getScriptRunningSpacing;
    int m_getTxBufferSpacing;
    int m_getStateSpacing;
    int m_readProfileSpacing;
    bool m_dynamicFrameReading, m_dynamicFrameReadingLock, m_dynamicFrameReadingPending;

    QAction *m_bootloaderAction;
    QAction *m_eraseAction;
    QAction *m_autoReconnectAction;
    QAction *m_stopOnConnectDiconnectionAction;
    QAction *m_enableSyncingImportsAction;
    QAction *m_enableFilteringExamplesAction;

    Core::Command *m_openDriveFolderCommand; QAction *m_openDriveFolderAction;
    Core::Command *m_configureSettingsCommand; QAction *m_configureSettingsAction;
    Core::Command *m_saveCommand; QAction *m_saveAction;
    Core::Command *m_resetCommand; QAction *m_resetAction;
    Core::Command *m_developmentReleaseCommand; QAction *m_developmentReleaseAction;
    Core::Command *m_enterBootloaderCommand; QAction *m_enterBootloaderAction;
    Core::ActionContainer *m_openTerminalMenu;
    Core::Command *m_connectCommand; QAction *m_connectAction;
    Core::Command *m_disconnectCommand; QAction *m_disconnectAction;
    Core::Command *m_startCommand; QAction *m_startAction;
    Core::Command *m_stopCommand; QAction *m_stopAction;

    QToolButton *m_jpgCompress;
    QToolButton *m_disableFrameBuffer;
    OpenMVDatasetEditor *m_datasetEditor;
    OpenMVPluginFB *m_frameBuffer;
    OpenMVPluginHistogram *m_histogram;
    QPointer<OpenMVProfileView> m_profile;

    Utils::ElidingLabel *m_boardLabel;
    Utils::ElidingToolButton *m_registerButton;
    QLabel *m_registerButtonSpacer;
    Utils::ElidingLabel *m_sensorLabel;
    Utils::ElidingToolButton *m_versionButton;
    Utils::ElidingLabel *m_portLabel;
    Utils::ElidingToolButton *m_pathButton;
    Utils::ElidingToolButton *m_fpsButton;
    double m_fpsIde = 0.0;          // last IDE-measured FPS (PC-side frame-arrival timing)
    double m_fpsCamera = 0.0;       // last on-camera FPS from the v5.0.0 stream header
    bool m_fpsCameraValid = false;  // camera reports FPS (v5.0.0) -> show both values

    ///////////////////////////////////////////////////////////////////////////

    typedef struct documentation
    {
        QString moduleName;
        QString className;
        QString name;
        QString text;
    }
    documentation_t;

    QSet<QString> m_knownModules;
    QHash<QString, QString> m_docUrls; // fully-qualified name -> docs page (relative to html/), with #anchor
    QList<documentation_t> m_modules;
    QList<documentation_t> m_classes;
    QList<documentation_t> m_datas;
    QList<documentation_t> m_functions;
    QList<documentation_t> m_methods;
    QSet<QString> m_arguments;
    QMap<QStringList, QStringList> m_argumentsByHierarchy;
    QMap<QStringList, QString> m_returnTypesByHierarchy;
    QList<wifiPort_t> m_availableWifiPorts;
    QList<QPair<QString, QString> > m_availableDrives;

    typedef struct openTerminalMenuData
    {
        QString displayName;
        int optionIndex;
        QString commandStr;
        int commandVal;
    }
    openTerminalMenuData_t;

    QList<openTerminalMenuData_t> m_openTerminalMenuData;

    bool openTerminalMenuDataContains(const QString &displayName)
    {
        for(const openTerminalMenuData_t &data : m_openTerminalMenuData)
        {
            if(data.displayName == displayName)
            {
                return true;
            }
        }

        return false;
    }

    ///////////////////////////////////////////////////////////////////////////

    importDataList_t m_exampleModules;
    importDataList_t m_documentsModules;

    QRegularExpression m_emRegEx;
    QRegularExpression m_spanRegEx;
    QRegularExpression m_anchorRegEx;
    QRegularExpression m_preRexEx;
    QRegularExpression m_classRegEx;
    QRegularExpression m_cdfmRegExInside;
    QRegularExpression m_cdfmRegExShared;
    QRegularExpression m_argumentRegEx;
    QRegularExpression m_returnTypeRegEx;
    QRegularExpression m_dataReturnTypeRexEx;
    QRegularExpression m_tupleRegEx;
    QRegularExpression m_listRegEx;
    QRegularExpression m_dictionaryRegEx;
    QRegularExpression m_typeHintRegEx;
    QRegularExpression m_tagRegEx;

    QStringList processArgumentSplitting(const QString &args);
    void processDocumentationMatch(const QRegularExpressionMatch &match,
                                   QStringList &providerVariables,
                                   QStringList &providerClasses, QMap<QString, QStringList> &providerClassArgs,
                                   QStringList &providerFunctions, QMap<QString, QStringList> &providerFunctionArgs,
                                   QStringList &providerMethods, QMap<QString, QStringList> &providerMethodArgs);
    void loadStubs(const Utils::FilePath &stubsPath,
                   QStringList &providerVariables,
                   QStringList &providerClasses, QMap<QString, QStringList> &providerClassArgs,
                   QStringList &providerFunctions, QMap<QString, QStringList> &providerFunctionArgs,
                   QStringList &providerMethods, QMap<QString, QStringList> &providerMethodArgs);
    bool loadDocs(bool update_resoruces, bool update_editors);
    void loadDocUrls();
    QList<const documentation_t *> resolveDocSymbol(const QString &word, const QString &qualifier, const QChar &nextChar, bool isAttr) const;
    bool openHelpForCursor(TextEditor::TextEditorWidget *widget);

    void parseImports(const QString &fileText, const QString &moduleFolder, const QStringList &builtInModules, importDataList_t &targetModules, QStringList &errorModules);
    bool importHelper(const QByteArray &text);

    ///////////////////////////////////////////////////////////////////////////

    typedef struct exampleFilter
    {
        QRegularExpression path;
        QRegularExpression boardType;
        QRegularExpression sensorType;
        QString flatten;
    }
    exampleFilter_t;

    QList<exampleFilter_t> m_exampleFilters;

    bool matchFlatten(const QString &filePath, const QSet<QString> &flattenSet);
    bool matchExample(const QString &filePath, QString *flattenRegex);

    QByteArray fixScriptForSensor(QByteArray data, bool notExamples = false, bool increaseResolution = false);
    void flushPortPath();
    bool writeFileToDriveAndFlush(const QString &filePath, const QByteArray &data, QString *errOut);

    QString tempFileForPythonEditor(const QByteArray &data, const QString &titlePattern);
    QJsonObject getBoardSettings(const QString &title, Utils::QtcSettings *settings, bool autoConnectToBoard = false);

    ///////////////////////////////////////////////////////////////////////////

    using DeferredFn = std::function<void()>;

    QQueue<DeferredFn> m_deferredHigh;
    QQueue<DeferredFn> m_deferredNormal;

    // Latest-wins bucket (keyed). Stable order via m_latestOrder.
    QHash<QString, DeferredFn> m_deferredLatest;
    QStringList m_latestOrder;

    bool m_deferredDrainPosted = false;

    void deferNormal(DeferredFn fn);
    void deferHigh(DeferredFn fn);
    void deferLatest(const QString &key, DeferredFn fn);

    void postDrain();
    void drainDeferred();

    void clearDeferred();
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVPLUGIN_H
