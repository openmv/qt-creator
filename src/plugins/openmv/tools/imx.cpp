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

#include <QtCore>
#include <QtWidgets>

#include <QTextCodec>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "loaderdialog.h"
#include "openmvtr.h"
#include "openmvromfs.h"

namespace OpenMV {
namespace Internal {

QMutex imx_working;

// The bundled python interpreter and the spsdk package dir for this host, or
// empty FilePaths when unsupported. (Factored out of the three call sites that
// duplicated this OS switch.)
static bool imxResolvePython(Utils::FilePath *python, Utils::FilePath *spsdk)
{
    if(Utils::HostOsInfo::isWindowsHost())
    {
        *spsdk = Core::ICore::resourcePath(QStringLiteral("spsdk/windows"));
        *python = Core::ICore::resourcePath(QStringLiteral("python/win/python.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        *spsdk = Core::ICore::resourcePath(QStringLiteral("spsdk/mac"));
        *python = Core::ICore::resourcePath(QStringLiteral("python/mac/bin/python"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            *spsdk = Core::ICore::resourcePath(QStringLiteral("spsdk/linux-x86_64"));
            *python = Core::ICore::resourcePath(QStringLiteral("python/linux-x86_64/bin/python"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            *spsdk = Core::ICore::resourcePath(QStringLiteral("spsdk/aarch64"));
            *python = Core::ICore::resourcePath(QStringLiteral("python/linux-arm64/bin/python"));
        }
    }

    return (!python->isEmpty()) && (!spsdk->isEmpty());
}

// Runs on the bundled python with spsdk on PYTHONPATH. Imports spsdk (the slow
// part), prints READY, then hot-loops MbootUSBInterface.scan() and -- in "claim"
// mode -- opens McuBoot and reads CURRENT_VERSION the instant the device appears
// (retrying the open for a grace period; Windows can lag attaching the HID after
// the scan first sees it). "wait" mode just reports the device is present.
// argv: <mode> <device_id vid:pid> <timeout_s> <expected_version_int>
static const char IMX_CATCHER_SCRIPT[] =
    "import sys, time, importlib\n"
    "mode, dev, t, want = sys.argv[1], sys.argv[2], float(sys.argv[3]), int(sys.argv[4])\n"
    "Mboot = importlib.import_module('spsdk.mboot.mcuboot').McuBoot\n"
    "Iface = importlib.import_module('spsdk.mboot.interfaces.usb').MbootUSBInterface\n"
    "UsbDevice = importlib.import_module('spsdk.utils.interfaces.device.usb_device').UsbDevice\n"
    "CURRENT_VERSION = 1\n"
    // Scan by VID:PID only. MbootUSBInterface.scan() would call get_devices()
    // -> the spsdk device database, which loads under a FileLock(timeout=10) --
    // two spsdk processes contending on it stall a full 10s. We already know the
    // exact VID:PID, so enumerate directly (no database, no lock) and wrap the
    // matches as mboot interfaces.
    "def find():\n"
    "    return [Iface(d) for d in UsbDevice.scan(device_id=dev)]\n"
    // One throwaway scan warms libusbsio / HIDAPI before READY, so the
    // post-reset loop is a pure hot path (try/except so a transient enumerate
    // hiccup can't abort the arm).
    "try:\n"
    "    find()\n"
    "except Exception:\n"
    "    pass\n"
    "print('READY', flush=True)\n"
    // t <= 0 means wait indefinitely (until claimed) -- the parent kills this
    // process on cancel/disconnect, matching the old probe loop that waited
    // until the user hit Cancel. t > 0 keeps a ceiling for non-interactive use.
    "deadline = (time.time() + t) if t > 0 else None\n"
    "while deadline is None or time.time() < deadline:\n"
    "    ifaces = find()\n"
    "    if ifaces:\n"
    "        if mode == 'wait':\n"
    "            print('FOUND', flush=True); sys.exit(0)\n"
    "        grace = time.time() + 1.0\n"
    "        while time.time() < grace:\n"
    "            try:\n"
    "                with Mboot(ifaces[0]) as mb:\n"
    "                    vals = mb.get_property(CURRENT_VERSION)\n"
    "                    if vals and vals[0] == want:\n"
    "                        print('CLAIMED %d' % vals[0], flush=True); sys.exit(0)\n"
    "            except Exception:\n"
    "                pass\n"
    "            ifaces = find() or ifaces\n"
    "            time.sleep(0.02)\n"
    "    time.sleep(0.05)\n"
    "sys.stderr.write('imx catcher: %s did not enumerate within %ss\\n' % (dev, sys.argv[3]))\n"
    "sys.exit(2)\n";

// The SBL/flashloader's CURRENT_VERSION property value (K2.8.0), matching the
// existing imxGetDevice() acceptance check (0x4B020800).
static const int IMX_EXPECTED_VERSION = 1258424320;

Utils::Process *imxArmCatcher(const QJsonObject &obj, const QString &pidvidKey,
                              const char *mode, int timeoutS, const bool *canceled)
{
    Utils::FilePath python, spsdk;

    if(!imxResolvePython(&python, &spsdk))
    {
        return nullptr;
    }

    // The *_pidvid settings are "VID,PID" (e.g. "0x15A2,0x0073") -- the same
    // form blhost's -u takes -- and spsdk's device filter accepts a comma or
    // colon separated "VID,PID"/"VID:PID". So pass it through (spaces stripped);
    // do NOT reorder it (an earlier swap scanned for a device that never
    // existed and the catcher never claimed anything).
    QString deviceId = obj.value(pidvidKey).toString();
    deviceId.remove(QLatin1Char(' '));

    if(deviceId.split(QLatin1Char(',')).size() != 2)
    {
        return nullptr;
    }

    Utils::Process *process = new Utils::Process;
    process->setStdOutCodec(QTextCodec::codecForName("UTF-8"));
    process->setStdErrCodec(QTextCodec::codecForName("UTF-8"));

    // Mirror the process setup the alif tools use (alif.cpp), which reliably
    // streams live text back from a console child on all hosts: both text
    // channels in MultiLine mode (textOnStandardOutput/Error only fire when a
    // mode is set) and Writer process mode (Reader mode closes the child's
    // stdin during startup -- a code path the working alif setup never takes).
    process->setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process->setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process->setProcessMode(Utils::ProcessMode::Writer);

    Utils::Environment env = process->environment();
    env.prependOrSet("PYTHONIOENCODING", QStringLiteral("utf-8"));
    env.prependOrSet("PYTHONPYCACHEPREFIX", Core::ICore::allUsersResourcePath(QStringLiteral("pycache")).toString());
    env.prependOrSet("PYTHONPATH", spsdk.path());
    process->setEnvironment(env);

    process->setCommand(Utils::CommandLine(python, QStringList()
        << QStringLiteral("-u")
        << QStringLiteral("-c")
        << QString::fromLatin1(IMX_CATCHER_SCRIPT)
        << QString::fromLatin1(mode)
        << deviceId
        << QString::number(timeoutS)
        << QString::number(IMX_EXPECTED_VERSION)));

    // Block (pumping events) until the catcher prints READY -- i.e. spsdk is
    // imported and it is actively scanning -- so the caller only triggers the
    // reset/jump once the hot loop is live. The slow import happens here, off
    // the device's ~1s window.
    bool armed = false;
    bool *armedPtr = &armed;
    QString stdOutBuffer;
    QString *stdOutBufferPtr = &stdOutBuffer;
    QString stdErrBuffer;
    QString *stdErrBufferPtr = &stdErrBuffer;

    QEventLoop loop;

    // Watch both channels -- the marker is printed on stdout, but watch stderr
    // too in case output lands on the wrong channel. The loop is the receiver
    // context, so these connections die with it.
    QObject::connect(process, &Utils::Process::textOnStandardOutput,
        &loop, [armedPtr, stdOutBufferPtr, &loop] (const QString &text) {
        stdOutBufferPtr->append(text);

        if(stdOutBufferPtr->contains(QStringLiteral("READY")))
        {
            *armedPtr = true;
            loop.quit();
        }
    });

    QObject::connect(process, &Utils::Process::textOnStandardError,
        &loop, [armedPtr, stdErrBufferPtr, &loop] (const QString &text) {
        stdErrBufferPtr->append(text);

        if(stdErrBufferPtr->contains(QStringLiteral("READY")))
        {
            *armedPtr = true;
            loop.quit();
        }
    });

    QObject::connect(process, &Utils::Process::done,
        &loop, &QEventLoop::quit);

    // Queue the start() so it executes after the nested event loop is running.
    // Process::runBlocking() (which the working alif flow goes through) does
    // exactly this -- starting before the nested loop breaks the process's
    // signal delivery on Windows with QProcessImpl (QTCREATORBUG-30066), which
    // is why READY never used to arrive here.
    QMetaObject::invokeMethod(process, [process] {
        process->start();
    }, Qt::QueuedConnection);

    // Poll the cancel flag so the user can bail during the slow import; the
    // caller checks *canceled after this returns and skips the board reset.
    QTimer cancelPoll;

    if(canceled)
    {
        QObject::connect(&cancelPoll, &QTimer::timeout, &loop, [canceled, &loop] {
            if(*canceled)
            {
                loop.quit();
            }
        });
        cancelPoll.start(50);
    }

    // Generous arm timeout -- a cold spsdk import on a loaded host can take
    // several seconds; this is all off the device's critical window.
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();

    cancelPoll.stop();

    if((!armed) || (canceled && *canceled))
    {
        process->stop();
        process->waitForFinished();
        delete process;
        return nullptr;
    }

    return process;
}

bool imxAwaitCatcher(Utils::Process *proc, const bool *canceled)
{
    if(!proc)
    {
        return false;
    }

    QEventLoop loop;

    QMetaObject::Connection doneConn = QObject::connect(proc, &Utils::Process::done,
        &loop, &QEventLoop::quit);

    // The catcher runs its own timeout; poll the cancel flag so the Cancel
    // button (owned by the caller's dialog) can abort the wait.
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [canceled, &loop] {
        if(*canceled)
        {
            loop.quit();
        }
    });
    poll.start(50);

    if(proc->state() != QProcess::NotRunning)
    {
        loop.exec();
    }

    QObject::disconnect(doneConn);
    poll.stop();

    const bool claimed = (proc->state() == QProcess::NotRunning)
        && (proc->exitCode() == 0)
        && (proc->result() == Utils::ProcessResult::FinishedWithSuccess);

    if(proc->state() != QProcess::NotRunning)
    {
        proc->stop();
        proc->waitForFinished();
    }

    delete proc;
    return claimed && (!*canceled);
}

QList<QPair<int, int> > imxVidPidList(const QJsonDocument &settings, bool spd_host, bool bl_host)
{
    QList<QPair<int, int> > pidvidlist;

    for(const QJsonValue &val : settings.object().value(QStringLiteral("boards")).toArray())
    {
        QJsonObject obj = val.toObject();

        if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("imx"))
        {
            QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();

            if(spd_host)
            {
                QStringList pidvid = bootloaderSettings.value(QStringLiteral("sdphost_pidvid")).toString().split(QChar(','));

                if(pidvid.size() == 2)
                {
                    bool okay_pid; int pid = pidvid.at(0).toInt(&okay_pid, 16);
                    bool okay_vid; int vid = pidvid.at(1).toInt(&okay_vid, 16);

                    if(okay_pid && okay_vid)
                    {
                        QPair<int, int> entry(pid, vid);

                        if(!pidvidlist.contains(entry))
                        {
                            pidvidlist.append(entry);
                        }
                    }
                }
            }

            if(bl_host)
            {
                QStringList pidvid = bootloaderSettings.value(QStringLiteral("blhost_pidvid")).toString().split(QChar(','));

                if(pidvid.size() == 2)
                {
                    bool okay_pid; int pid = pidvid.at(0).toInt(&okay_pid, 16);
                    bool okay_vid; int vid = pidvid.at(1).toInt(&okay_vid, 16);

                    if(okay_pid && okay_vid)
                    {
                        QPair<int, int> entry(pid, vid);

                        if(!pidvidlist.contains(entry))
                        {
                            pidvidlist.append(entry);
                        }
                    }
                }
            }
        }
    }

    return pidvidlist;
}

QStringList imxGetAllDevices(const QJsonDocument &settings, bool spd_host, bool bl_host)
{
    QList<QPair<int, int> > pidvidlist = imxVidPidList(settings, spd_host, bl_host);

    if(!imx_working.tryLock()) return QStringList();

    QStringList devices;

    if(!pidvidlist.isEmpty())
    {
#ifdef Q_OS_WIN
        UINT nDevices = 0;

        if((GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)) == 0) && (nDevices > 0))
        {
            PRAWINPUTDEVICELIST pRawInputDeviceList = new RAWINPUTDEVICELIST[sizeof(RAWINPUTDEVICELIST) * nDevices];

            if(pRawInputDeviceList)
            {
                if(GetRawInputDeviceList(pRawInputDeviceList, &nDevices, sizeof(RAWINPUTDEVICELIST)) == nDevices)
                {
                    for(UINT i = 0; i < nDevices; i++)
                    {
                        RID_DEVICE_INFO rdiDeviceInfo;
                        UINT nBufferSize = sizeof(RID_DEVICE_INFO);

                        if(GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice, RIDI_DEVICEINFO, &rdiDeviceInfo, &nBufferSize) == sizeof(RID_DEVICE_INFO))
                        {
                            if(rdiDeviceInfo.dwType == RIM_TYPEHID)
                            {
                                QPair<int, int> entry(rdiDeviceInfo.hid.dwVendorId, rdiDeviceInfo.hid.dwProductId);

                                if(pidvidlist.contains(entry))
                                {
                                    devices.append(QString(QStringLiteral("%1:%2,NULL")).
                                                   arg(rdiDeviceInfo.hid.dwVendorId, 4, 16, QChar('0')).
                                                   arg(rdiDeviceInfo.hid.dwProductId, 4, 16, QChar('0')));
                                }
                            }
                        }
                    }
                }

                delete[] pRawInputDeviceList;
            }
        }
#elif defined(Q_OS_LINUX)
        Utils::Process process;
        std::chrono::seconds timeout(10);
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(QStringLiteral("lsusb")), QStringList()));
        process.runBlocking(timeout, Utils::EventLoopMode::On);

        if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
        {
            QRegularExpression regex(QStringLiteral("ID ([0-9a-zA-Z]+):([0-9a-zA-Z]+)"));

            for(const QString &s : process.stdOut().split(QRegularExpression(QStringLiteral("\n|\r\n|\r")), Qt::SkipEmptyParts))
            {
                QRegularExpressionMatch match = regex.match(s);

                if(match.hasMatch())
                {
                    bool vidOk, pidOk;
                    int vid = match.captured(1).toInt(&vidOk, 16);
                    int pid = match.captured(2).toInt(&pidOk, 16);

                    if(vidOk && pidOk)
                    {
                        QPair<int, int> entry(vid, pid);

                        if(pidvidlist.contains(entry))
                        {
                            devices.append(QString(QStringLiteral("%1:%2,NULL")).
                                           arg(vid, 4, 16, QChar('0')).
                                           arg(pid, 4, 16, QChar('0')));
                        }
                    }
                }
            }
        }
#elif defined(Q_OS_MAC)
        Utils::Process process;
        std::chrono::seconds timeout(10);
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(QStringLiteral("ioreg")), QStringList()
                                              << QStringLiteral("-p")
                                              << QStringLiteral("IOUSB")
                                              << QStringLiteral("-w0")
                                              << QStringLiteral("-l")));
        process.runBlocking(timeout, Utils::EventLoopMode::On);

        if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
        {
            QRegularExpressionMatchIterator matches = QRegularExpression(QStringLiteral("{.*?\"idProduct\" = (\\d+).*?\"idVendor\" = (\\d+).*?}"),
                                                                         QRegularExpression::DotMatchesEverythingOption).globalMatch(process.stdOut());

            while(matches.hasNext())
            {
                QRegularExpressionMatch match = matches.next();

                bool vidOk, pidOk;
                int pid = match.captured(1).toInt(&pidOk);
                int vid = match.captured(2).toInt(&vidOk);

                if(vidOk && pidOk)
                {
                    QPair<int, int> entry(vid, pid);

                    if(pidvidlist.contains(entry))
                    {
                        devices.append(QString(QStringLiteral("%1:%2,NULL")).
                                       arg(vid, 4, 16, QChar('0')).
                                       arg(pid, 4, 16, QChar('0')));
                    }
                }
            }
        }
#endif
    }

    imx_working.unlock();
    return devices;
}

bool imxGetDevice(QJsonObject &obj)
{
    QMutexLocker locker(&imx_working);

    Utils::Process process;
    process.setStdOutCodec(QTextCodec::codecForName("UTF-8"));
    process.setStdErrCodec(QTextCodec::codecForName("UTF-8"));

    Utils::Environment env = process.environment();
    env.prependOrSet("PYTHONIOENCODING", QStringLiteral("utf-8"));
    env.prependOrSet("PYTHONPYCACHEPREFIX", Core::ICore::allUsersResourcePath(QStringLiteral("pycache")).toString());

    std::chrono::seconds timeout(10);
    process.setProcessChannelMode(QProcess::MergedChannels);

    Utils::FilePath pythonPath, binary;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/windows"));
        binary = Core::ICore::resourcePath(QStringLiteral("python/win/python.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/mac"));
        binary = Core::ICore::resourcePath(QStringLiteral("python/mac/bin/python"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/linux-x86_64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-x86_64/bin/python"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/aarch64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-arm64/bin/python"));
        }
    }

    if(pythonPath.isEmpty() || binary.isEmpty())
    {
        return false;
    }

    env.prependOrSet("PYTHONPATH", pythonPath.path());
    process.setEnvironment(env);

    int responseStatus = -1;
    int responseWord = 0;
    QString currentVersion;

    QRegularExpression statusRegex("Response status = (\\d+)");
    QRegularExpression wordRegex("Response word 1 = (\\d+)");
    QRegularExpression versionRegex("Current Version = ([^\\s]+)");

    QStringList args = QStringList() <<
                       QStringLiteral("-u") <<
                       QStringLiteral("-m") <<
                       QStringLiteral("spsdk.apps.blhost") <<
                       QStringLiteral("-u") <<
                       obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                       QStringLiteral("--") <<
                       QStringLiteral("get-property") <<
                       QStringLiteral("1");

    process.setCommand(Utils::CommandLine(binary, args));
    process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

    if((process.result() == Utils::ProcessResult::FinishedWithSuccess) || (process.result() == Utils::ProcessResult::FinishedWithError))
    {
        QStringList in = process.stdOut().split(QRegularExpression(QStringLiteral("\n|\r\n|\r")), Qt::SkipEmptyParts);

        for (const QString &line : in)
        {
            if (statusRegex.match(line).hasMatch())
            {
                responseStatus = statusRegex.match(line).captured(1).toInt();
            }
            else if (wordRegex.match(line).hasMatch())
            {
                responseWord = wordRegex.match(line).captured(1).toInt();
            }
            else if (versionRegex.match(line).hasMatch())
            {
                currentVersion = versionRegex.match(line).captured(1);
            }
        }

        return ((responseStatus == 0) && (responseWord == 1258424320) && (currentVersion == QStringLiteral("K2.8.0")));
    }
    else
    {
        return false;
    }
}

bool imxDownloadBootloaderAndFirmware(QJsonObject &obj, bool forceFlashFSErase, bool justEraseFlashFs, OpenMVROMFSAccess romfsAccess)
{
    QMutexLocker locker(&imx_working);

    bool result = true;
    Utils::Process process;
    process.setStdOutCodec(QTextCodec::codecForName("UTF-8"));
    process.setStdErrCodec(QTextCodec::codecForName("UTF-8"));

    Utils::Environment env = process.environment();
    env.prependOrSet("PYTHONIOENCODING", QStringLiteral("utf-8"));
    env.prependOrSet("PYTHONPYCACHEPREFIX", Core::ICore::allUsersResourcePath(QStringLiteral("pycache")).toString());

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("NXP IMX"), Tr::tr("Flashing Firmware"), process, settings, QStringLiteral(LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY),
                                            Core::ICore::dialogParent());

    int ok = true;
    int *okPtr = &ok;

    QEventLoop loop;

    QMetaObject::Connection conn = QObject::connect(dialog, &QDialog::finished,
        &loop, [okPtr] () {
        *okPtr = false;
    });

    QObject::connect(dialog, &QDialog::finished, &loop, &QEventLoop::quit);

    // Pre-armed flashloader detector (armed before the jump below); declared here
    // so the goto-cleanup paths can tear it down. See the "Start Flash Loader" step.
    Utils::Process *flashloaderCatcher = nullptr;

    QString stdOutBuffer = QString();
    QString *stdOutBufferPtr = &stdOutBuffer;
    bool stdOutFirstTime = true;
    bool *stdOutFirstTimePtr = &stdOutFirstTime;

    QObject::connect(&process, &Utils::Process::textOnStandardOutput, dialog, [dialog, stdOutBufferPtr, stdOutFirstTimePtr] (const QString &text) {
        stdOutBufferPtr->append(text);
        QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdOutBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.isEmpty())
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("(1/1)")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\(1/1\\)\\s*(\\d+)%")).match(out);

                if(m.hasMatch())
                {
                    dialog->setProgressBarLabel(Tr::tr("Downloading..."));
                    dialog->setProgressBarRange(0, 100);
                    dialog->setProgressBarValue(m.captured(1).toInt());
                }

                if(!*stdOutFirstTimePtr)
                {
                    QTextCursor cursor = dialog->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    cursor.removeSelectedText();
                    dialog->setTextCursor(cursor);
                }

                *stdOutFirstTimePtr = false;
            }

            dialog->appendPlainText(out);
        }
    });

    QString stdErrBuffer = QString();
    QString *stdErrBufferPtr = &stdErrBuffer;
    bool stdErrFirstTime = true;
    bool *stdErrFirstTimePtr = &stdErrFirstTime;

    QObject::connect(&process, &Utils::Process::textOnStandardError, dialog, [dialog, stdErrBufferPtr, stdErrFirstTimePtr] (const QString &text) {
        stdErrBufferPtr->append(text);
        QStringList list = stdErrBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdErrBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.isEmpty())
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("(1/1)")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\(1/1\\)\\s*(\\d+)%")).match(out);

                if(m.hasMatch())
                {
                    dialog->setProgressBarLabel(Tr::tr("Downloading..."));
                    dialog->setProgressBarRange(0, 100);
                    dialog->setProgressBarValue(m.captured(1).toInt());
                }

                if(!*stdErrFirstTimePtr)
                {
                    QTextCursor cursor = dialog->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    cursor.removeSelectedText();
                    dialog->setTextCursor(cursor);
                }

                *stdErrFirstTimePtr = false;
            }

            dialog->appendColoredText(out);
        }
    });

    Utils::FilePath pythonPath, binary;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/windows"));
        binary = Core::ICore::resourcePath(QStringLiteral("python/win/python.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/mac"));
        binary = Core::ICore::resourcePath(QStringLiteral("python/mac/bin/python"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/linux-x86_64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-x86_64/bin/python"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/aarch64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-arm64/bin/python"));
        }
    }

    if(pythonPath.isEmpty() || binary.isEmpty())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("NXP IMX"),
            Tr::tr("This feature is not supported on this machine!"));

        result = false;
        goto cleanup;
    }

    env.prependOrSet("PYTHONPATH", pythonPath.path());
    process.setEnvironment(env);

    dialog->show();

    // Load Flash Loader
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.sdphost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("sdphost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("write-file") <<
                           obj.value(QStringLiteral("sdphost_flash_loader_address")).toString() <<
                           QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("sdphost_flash_loader_path")).toString()));

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Arm a pre-imported, hot-scanning catcher for the flashloader BEFORE the
    // jump, so "Wait for Flash Loader" below is an instant scan hit rather than
    // a single get-property that can miss if the flashloader hasn't finished
    // enumerating (spsdk's import cost is paid here, before the jump). Falls
    // back to the get-property probe when the catcher can't arm.
    dialog->appendColoredText(Tr::tr("Preparing the bootloader tools... (this can take a few seconds)"));
    flashloaderCatcher = imxArmCatcher(obj, QStringLiteral("blhost_pidvid"), "claim", 300, nullptr);

    // Start Flash Loader
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.sdphost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("sdphost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("jump-address") <<
                           obj.value(QStringLiteral("sdphost_flash_loader_address")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(conn);

    if(!ok)
    {
        result = false;
        goto cleanup;
    }

    // Wait for Flash Loader
    if(flashloaderCatcher)
    {
        dialog->appendColoredText(Tr::tr("Waiting for the flashloader to enumerate..."));

        bool no = false;
        bool claimed = imxAwaitCatcher(flashloaderCatcher, &no);
        flashloaderCatcher = nullptr; // consumed (deleted) by imxAwaitCatcher

        if(!claimed)
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(Tr::tr("The i.MX flashloader did not enumerate."));
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
    }
    else
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("get-property") <<
                           QStringLiteral("1");

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Write Memory Configuration
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("fill-memory") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_address")).toString() <<
                           QStringLiteral("4") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_spi")).toString() <<
                           QStringLiteral("word");

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Load Memory Configuration
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("configure-memory") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_type")).toString() <<
                           obj.value(QStringLiteral("blhost_memory_configuration_address")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Erase Memory
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("-t") <<
                           QStringLiteral("120000") <<
                           QStringLiteral("--") <<
                           QStringLiteral("flash-erase-region") <<
                           obj.value(QStringLiteral("blhost_secure_bootloader_fcb_address")).toString() <<
                           obj.value(QStringLiteral("blhost_secure_bootloader_fcb_length")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Write FCB Configuration
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("fill-memory") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_address")).toString() <<
                           QStringLiteral("4") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_fcb")).toString() <<
                           QStringLiteral("word");

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Load Memory Configuration Again
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("configure-memory") <<
                           obj.value(QStringLiteral("blhost_memory_configuration_type")).toString() <<
                           obj.value(QStringLiteral("blhost_memory_configuration_address")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Erase Memory
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("-t") <<
                           QStringLiteral("120000") <<
                           QStringLiteral("--") <<
                           QStringLiteral("flash-erase-region") <<
                           obj.value(QStringLiteral("blhost_secure_bootloader_address")).toString() <<
                           obj.value(QStringLiteral("blhost_secure_bootloader_length")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Write Image
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("write-memory") <<
                           obj.value(QStringLiteral("blhost_secure_bootloader_address")).toString() <<
                           QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("blhost_secure_bootloader_path")).toString()));

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    if(forceFlashFSErase
    && (obj.value(QStringLiteral("blhost_disk_address")).toString().toInt(nullptr, 16) != 0)
    && (obj.value(QStringLiteral("blhost_disk_size_mbr")).toString().toInt(nullptr, 16) > 0))
    {
        dialog->appendColoredText(Tr::tr("This command takes a while to execute. Please be patient."), true);

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("120000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_disk_address")).toString() <<
                               obj.value(QStringLiteral("blhost_disk_size_mbr")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if ((romfsAccess == OPENMV_ROMFS_RESET)
    && (obj.value(QStringLiteral("blhost_romfs_address")).toString().toInt(nullptr, 16) != 0)
    && (obj.value(QStringLiteral("blhost_romfs_size")).toString().toInt(nullptr, 16) > 0))
    {
        dialog->appendColoredText(Tr::tr("This command takes a while to execute. Please be patient."), true);

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("120000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_romfs_address")).toString() <<
                               obj.value(QStringLiteral("blhost_romfs_length")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(900); // 15 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }

        // Write Image
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("--") <<
                               QStringLiteral("write-memory") <<
                               obj.value(QStringLiteral("blhost_romfs_address")).toString() <<
                               QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("blhost_romfs_path")).toString()));

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(900); // 15 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if(!justEraseFlashFs)
    {
        dialog->appendColoredText(Tr::tr("This command takes a while to execute. Please be patient."), true);

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("120000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_firmware_address")).toString() <<
                               obj.value(QStringLiteral("blhost_firmware_length")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }

        // Write Image
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("--") <<
                               QStringLiteral("write-memory") <<
                               obj.value(QStringLiteral("blhost_firmware_address")).toString() <<
                               QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("blhost_firmware_path")).toString()));

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    // Burn E-Fuse
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("efuse-program-once") <<
                           obj.value(QStringLiteral("blhost_efuse_burn_address")).toString() <<
                           obj.value(QStringLiteral("blhost_efuse_burn_data")).toString();

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // Reset
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("reset");

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

cleanup:

    // A goto that fired between arming and awaiting leaves the catcher running;
    // an already-true cancel makes imxAwaitCatcher stop+delete it immediately.
    if(flashloaderCatcher)
    {
        bool kill = true;
        imxAwaitCatcher(flashloaderCatcher, &kill);
        flashloaderCatcher = nullptr;
    }

    delete dialog;

    return result;
}

bool imxDownloadFirmware(QJsonObject &obj, bool forceFlashFSErase, bool justEraseFlashFs, OpenMVROMFSAccess romfsAccess)
{
    QMutexLocker locker(&imx_working);

    bool result = true;
    Utils::Process process;
    process.setStdOutCodec(QTextCodec::codecForName("UTF-8"));
    process.setStdErrCodec(QTextCodec::codecForName("UTF-8"));

    Utils::Environment env = process.environment();
    env.prependOrSet("PYTHONIOENCODING", QStringLiteral("utf-8"));
    env.prependOrSet("PYTHONPYCACHEPREFIX", Core::ICore::allUsersResourcePath(QStringLiteral("pycache")).toString());

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("NXP IMX"), ((romfsAccess == OPENMV_ROMFS_READ) ? Tr::tr("Read ROMFS") :
                                                                    ((romfsAccess == OPENMV_ROMFS_WRITE) ? Tr::tr("Write ROMFS") :
                                                                        Tr::tr("Flashing Firmware"))),
                                            process, settings, QStringLiteral(LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY),
                                            Core::ICore::dialogParent());

    QString stdOutBuffer = QString();
    QString *stdOutBufferPtr = &stdOutBuffer;
    bool stdOutFirstTime = true;
    bool *stdOutFirstTimePtr = &stdOutFirstTime;

    QObject::connect(&process, &Utils::Process::textOnStandardOutput, dialog, [dialog, stdOutBufferPtr, stdOutFirstTimePtr, romfsAccess] (const QString &text) {
        stdOutBufferPtr->append(text);
        QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdOutBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.isEmpty())
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("(1/1)")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\(1/1\\)\\s*(\\d+)%")).match(out);

                if(m.hasMatch())
                {
                    dialog->setProgressBarLabel((romfsAccess == OPENMV_ROMFS_READ) ? Tr::tr("Uploading...") : Tr::tr("Downloading..."));
                    dialog->setProgressBarRange(0, 100);
                    dialog->setProgressBarValue(m.captured(1).toInt());
                }

                if(!*stdOutFirstTimePtr)
                {
                    QTextCursor cursor = dialog->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    cursor.removeSelectedText();
                    dialog->setTextCursor(cursor);
                }

                *stdOutFirstTimePtr = false;
            }

            dialog->appendPlainText(out);
        }
    });

    QString stdErrBuffer = QString();
    QString *stdErrBufferPtr = &stdErrBuffer;
    bool stdErrFirstTime = true;
    bool *stdErrFirstTimePtr = &stdErrFirstTime;

    QObject::connect(&process, &Utils::Process::textOnStandardError, dialog, [dialog, stdErrBufferPtr, stdErrFirstTimePtr, romfsAccess] (const QString &text) {
        stdErrBufferPtr->append(text);
        QStringList list = stdErrBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdErrBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.isEmpty())
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("(1/1)")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\(1/1\\)\\s*(\\d+)%")).match(out);

                if(m.hasMatch())
                {
                    dialog->setProgressBarLabel((romfsAccess == OPENMV_ROMFS_READ) ? Tr::tr("Uploading...") : Tr::tr("Downloading..."));
                    dialog->setProgressBarRange(0, 100);
                    dialog->setProgressBarValue(m.captured(1).toInt());
                }

                if(!*stdErrFirstTimePtr)
                {
                    QTextCursor cursor = dialog->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    cursor.removeSelectedText();
                    dialog->setTextCursor(cursor);
                }

                *stdErrFirstTimePtr = false;
            }

            dialog->appendColoredText(out);
        }
    });

    Utils::FilePath pythonPath, binary;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/windows"));
        binary = Core::ICore::resourcePath(QStringLiteral("python/win/python.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/mac"));
        binary = Core::ICore::resourcePath(QStringLiteral("python/mac/bin/python"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/linux-x86_64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-x86_64/bin/python"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("spsdk/aarch64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-arm64/bin/python"));
        }
    }

    if(pythonPath.isEmpty() || binary.isEmpty())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("NXP IMX"),
            Tr::tr("This feature is not supported on this machine!"));

        result = false;
        goto cleanup;
    }

    env.prependOrSet("PYTHONPATH", pythonPath.path());
    process.setEnvironment(env);

    dialog->show();

    if(forceFlashFSErase
    && (obj.value(QStringLiteral("blhost_disk_address")).toString().toInt(nullptr, 16) != 0)
    && (obj.value(QStringLiteral("blhost_disk_size_mbr")).toString().toInt(nullptr, 16) > 0)
    && (romfsAccess != OPENMV_ROMFS_READ) && (romfsAccess != OPENMV_ROMFS_WRITE))
    {
        dialog->appendColoredText(Tr::tr("This command takes a while to execute. Please be patient."), true);

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("120000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_disk_address")).toString() <<
                               obj.value(QStringLiteral("blhost_disk_size_mbr")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if ((romfsAccess == OPENMV_ROMFS_READ)
    && (obj.value(QStringLiteral("blhost_romfs_address")).toString().toInt(nullptr, 16) != 0)
    && (obj.value(QStringLiteral("blhost_romfs_size")).toString().toInt(nullptr, 16) > 0))
    {
        dialog->appendColoredText(Tr::tr("This command takes a while to execute. Please be patient."), true);

        // Read Image
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("--") <<
                               QStringLiteral("read-memory") <<
                               obj.value(QStringLiteral("blhost_romfs_address")).toString() <<
                               obj.value(QStringLiteral("blhost_romfs_size")).toString() <<
                               QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("blhost_romfs_path")).toString()));

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(900); // 15 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if ((romfsAccess == OPENMV_ROMFS_WRITE)
    && (obj.value(QStringLiteral("blhost_romfs_address")).toString().toInt(nullptr, 16) != 0)
    && (obj.value(QStringLiteral("blhost_romfs_size")).toString().toInt(nullptr, 16) > 0))
    {
        dialog->appendColoredText(Tr::tr("This command takes a while to execute. Please be patient."), true);

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("120000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_romfs_address")).toString() <<
                               obj.value(QStringLiteral("blhost_romfs_length")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(900); // 15 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }

        // Write Image
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("--") <<
                               QStringLiteral("write-memory") <<
                               obj.value(QStringLiteral("blhost_romfs_address")).toString() <<
                               QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("blhost_romfs_path")).toString()));

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(900); // 15 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if ((romfsAccess == OPENMV_ROMFS_RESET)
    && (obj.value(QStringLiteral("blhost_romfs_address")).toString().toInt(nullptr, 16) != 0)
    && (obj.value(QStringLiteral("blhost_romfs_size")).toString().toInt(nullptr, 16) > 0)
    && (romfsAccess != OPENMV_ROMFS_READ) && (romfsAccess != OPENMV_ROMFS_WRITE))
    {
        dialog->appendColoredText(Tr::tr("This command takes a while to execute. Please be patient."), true);

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("120000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_romfs_address")).toString() <<
                               obj.value(QStringLiteral("blhost_romfs_length")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(900); // 15 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }

        // Write Image
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("--") <<
                               QStringLiteral("write-memory") <<
                               obj.value(QStringLiteral("blhost_romfs_address")).toString() <<
                               QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("blhost_romfs_path")).toString()));

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(900); // 15 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if((!justEraseFlashFs)    
    && (romfsAccess != OPENMV_ROMFS_READ) && (romfsAccess != OPENMV_ROMFS_WRITE))
    {
        dialog->appendColoredText(Tr::tr("This command takes a while to execute. Please be patient."), true);

        // Erase Memory
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("-t") <<
                               QStringLiteral("120000") <<
                               QStringLiteral("--") <<
                               QStringLiteral("flash-erase-region") <<
                               obj.value(QStringLiteral("blhost_firmware_address")).toString() <<
                               obj.value(QStringLiteral("blhost_firmware_length")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }

        // Write Image
        {
            QStringList args = QStringList() <<
                               QStringLiteral("-u") <<
                               QStringLiteral("-m") <<
                               QStringLiteral("spsdk.apps.blhost") <<
                               QStringLiteral("-u") <<
                               obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                               QStringLiteral("--") <<
                               QStringLiteral("write-memory") <<
                               obj.value(QStringLiteral("blhost_firmware_address")).toString() <<
                               QDir::toNativeSeparators(QDir::cleanPath(obj.value(QStringLiteral("blhost_firmware_path")).toString()));

            QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    // Reset
    {
        QStringList args = QStringList() <<
                           QStringLiteral("-u") <<
                           QStringLiteral("-m") <<
                           QStringLiteral("spsdk.apps.blhost") <<
                           QStringLiteral("-u") <<
                           obj.value(QStringLiteral("blhost_pidvid")).toString() <<
                           QStringLiteral("--") <<
                           QStringLiteral("reset");

        QString command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("NXP IMX"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

cleanup:

    delete dialog;

    return result;
}

} // namespace Internal
} // namespace OpenMV
