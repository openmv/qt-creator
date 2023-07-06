/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt SDK.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

// constructor
function Component()
{
    component.loaded.connect(this, Component.prototype.installerLoaded);
    installer.setDefaultPageVisible(QInstaller.TargetDirectory, false);
    installer.installationFinished.connect(this, Component.prototype.installationFinishedPageIsShown);
    installer.finishButtonClicked.connect(this, Component.prototype.installationFinished);
    installer.setDefaultPageVisible(QInstaller.ComponentSelection, false);
}

Component.prototype.createOperationsForArchive = function(archive)
{
    // if there are additional plugin 7zips, these must be extracted in .app/Contents on OS X
    if (systemInfo.productType !== "osx" || archive.indexOf('qtcreator.7z') !== -1)
        component.addOperation("Extract", archive, "@TargetDir@");
    else
        component.addOperation("Extract", archive, "@TargetDir@/OpenMV IDE.app/Contents");
}

Component.prototype.beginInstallation = function()
{
    component.qtCreatorBinaryPath = installer.value("TargetDir");

    if (installer.value("os") == "win") {
        component.qtCreatorBinaryPath = component.qtCreatorBinaryPath + "\\bin\\openmvide.exe";
        component.qtCreatorBinaryPath = component.qtCreatorBinaryPath.replace(/\//g, "\\");
    }
    else if (installer.value("os") == "x11") {
        component.qtCreatorBinaryPath = component.qtCreatorBinaryPath + "/bin/openmvide";
    }
    else if (installer.value("os") == "mac") {
        component.qtCreatorBinaryPath = component.qtCreatorBinaryPath + "/OpenMV IDE.app/Contents/MacOS/OpenMV IDE";
    }

    if ( installer.value("os") === "win" )
        component.setStopProcessForUpdateRequest(component.qtCreatorBinaryPath, true);
}

Component.prototype.createOperations = function()
{
    // Call the base createOperations and afterwards set some registry settings
    component.createOperations();
    if ( installer.value("os") == "win" )
    {
        component.addOperation( "CreateShortcut",
                                component.qtCreatorBinaryPath,
                                "@StartMenuDir@/OpenMV IDE.lnk",
                                "workingDirectory=@homeDir@" );
        component.addOperation( "CreateShortcut",
                                component.qtCreatorBinaryPath,
                                "@DesktopDir@/OpenMV IDE.lnk",
                                "workingDirectory=@homeDir@" );
        component.addOperation( "CreateShortcut",
                                "@TargetDir@/OpenMVIDEUninst.exe",
                                "@StartMenuDir@/Uninstall.lnk",
                                "workingDirectory=@homeDir@" );
        component.addElevatedOperation("Execute", "{2,512}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\ftdi\\ftdi.cmd");
        component.addElevatedOperation("Execute", "{1,256}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\openmv\\openmv.cmd");
        component.addElevatedOperation("Execute", "{2,768}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\arduino\\arduino.cmd");
        // component.addElevatedOperation("Execute", "{1,256}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\pybcdc\\pybcdc.cmd");
        component.addElevatedOperation("Execute", "{1,256}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\dfuse.cmd");
        component.addElevatedOperation("Execute", "{0,3010}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\vcr.cmd");
    }
    if ( installer.value("os") == "x11" )
    {
        component.addOperation( "InstallIcons", "@TargetDir@/share/icons" );
        component.addOperation( "CreateDesktopEntry",
                                "OpenMV-openmvide.desktop",
                                "Type=Application\nExec=" + component.qtCreatorBinaryPath + " %F\nPath=@TargetDir@\nName=OpenMV IDE\nGenericName=The IDE of choice for OpenMV Cam Development.\nX-KDE-StartupNotify=true\nIcon=OpenMV-openmvide\nStartupWMClass=openmvide\nTerminal=false\nCategories=Development;IDE;OpenMV;\nMimeType=text/x-python;"
                                );
        if (installer.fileExists("/usr/share/metadata")) {
            component.addElevatedOperation( "Move",
                                            "@TargetDir@/share/metadata",
                                            "/usr/share/metadata"
                                            );
        }
        component.addElevatedOperation( "Execute", "{0}", "apt-get", "install", "-y",
                                        "libpng16-16",
                                        "libusb-1.0",
                                        "python3",
                                        "python3-pip",
                                        "libfontconfig1",
                                        "libfreetype6",
                                        "libxcb1",
                                        "libxcb-glx0",
                                        "libxcb-keysyms1",
                                        "libxcb-image0",
                                        "libxcb-shm0",
                                        "libxcb-icccm4",
                                        "libxcb-xfixes0",
                                        "libxcb-shape0",
                                        "libxcb-randr0",
                                        "libxcb-render-util0",
                                        "libxcb-xinerama0",
                                        );

        component.addElevatedOperation( "Execute", "{0}", "pip3", "install", "pyusb" );
        if (!installer.fileExists("/etc/udev/rules.d/99-openmv.rules")) {
            component.addElevatedOperation( "Copy", "@TargetDir@/share/qtcreator/pydfu/99-openmv.rules", "/etc/udev/rules.d/99-openmv.rules" );
        }
        if (!installer.fileExists("/etc/udev/rules.d/99-openmv-arduino.rules")) {
            component.addElevatedOperation( "Copy", "@TargetDir@/share/qtcreator/pydfu/99-openmv-arduino.rules", "/etc/udev/rules.d/99-openmv-arduino.rules" );
        }
        if (!installer.fileExists("/etc/udev/rules.d/99-openmv-nxp.rules")) {
            component.addElevatedOperation( "Copy", "@TargetDir@/share/qtcreator/pydfu/99-openmv-nxp.rules", "/etc/udev/rules.d/99-openmv-nxp.rules" );
        }
        component.addElevatedOperation( "Execute", "{0}", "udevadm", "trigger" );
        component.addElevatedOperation( "Execute", "{0}", "udevadm", "control", "--reload-rules" );
    }
}

function isRoot()
{
    if (installer.value("os") == "x11" || installer.value("os") == "mac")
    {
        var id = installer.execute("/usr/bin/id", new Array("-u"))[0];
        id = id.replace(/(\r\n|\n|\r)/gm,"");
        if (id === "0")
        {
            return true;
        }
    }
    return false;
}

Component.prototype.installationFinishedPageIsShown = function()
{
    isroot = isRoot();
    try {
        if (component.installed && installer.isInstaller() && installer.status == QInstaller.Success && !isroot) {
            installer.addWizardPageItem( component, "LaunchQtCreatorCheckBoxForm", QInstaller.InstallationFinished );
        }
    } catch(e) {
        print(e);
    }
}

Component.prototype.installationFinished = function()
{
    try {
        if (component.installed && installer.isInstaller() && installer.status == QInstaller.Success && !isroot) {
            var isLaunchQtCreatorCheckBoxChecked = component.userInterface("LaunchQtCreatorCheckBoxForm").launchQtCreatorCheckBox.checked;
            if (isLaunchQtCreatorCheckBoxChecked)
                installer.executeDetached(component.qtCreatorBinaryPath, new Array(), "@homeDir@");
        }
    } catch(e) {
        print(e);
    }
}

Component.prototype.installerLoaded = function()
{
    if (installer.addWizardPage(component, "TargetWidget", QInstaller.TargetDirectory)) {
        var widget = gui.pageWidgetByObjectName("DynamicTargetWidget");
        if (widget != null) {
            widget.targetChooser.clicked.connect(this, this.chooseTarget);
            widget.targetDirectory.textChanged.connect(this, Component.prototype.targetChanged);
            widget.windowTitle = "Installation Folder";
            widget.targetDirectory.setText(installer.value("TargetDir"));
        }
    }

    gui.pageById(QInstaller.LicenseCheck).entered.connect(this, Component.prototype.licenseCheckPageEntered);
}

Component.prototype.targetChanged = function(text)
{
    var widget = gui.pageWidgetByObjectName("DynamicTargetWidget");
    if (widget != null) {
        if (text != "") {
            widget.complete = true;

            if (installer.value("os") == "win") {
                if (installer.fileExists(text) && installer.fileExists(text + "/OpenMVIDEUninst.exe")) {
                    widget.warning.setText("<p style=\"color: red\">Existing installation detected and will be overwritten.</p>");
                }
                else if (installer.fileExists(text)) {
                    widget.warning.setText("<p style=\"color: red\">Installing in existing directory. It will be wiped on uninstallation.</p>");
                }
                else {
                    widget.warning.setText("");
                }
            }
            else if (installer.value("os") == "x11") {
                if (installer.fileExists(text) && installer.fileExists(text + "/OpenMVIDEUninstaller")) {
                    widget.warning.setText("<p style=\"color: red\">Existing installation detected and will be overwritten.</p>");
                }
                else if (installer.fileExists(text)) {
                    widget.warning.setText("<p style=\"color: red\">Installing in existing directory. It will be wiped on uninstallation.</p>");
                }
                else {
                    widget.warning.setText("");
                }
            }
            else if (installer.value("os") == "mac") {
                if (installer.fileExists(text)) {
                    widget.warning.setText("<p style=\"color: red\">Installing in existing directory. It will be wiped on uninstallation.</p>");
                }
                else {
                    widget.warning.setText("");
                }
            }

            installer.setValue("TargetDir", text);
            return;
        }

        widget.complete = false;
    }
}

Component.prototype.chooseTarget = function()
{
    var widget = gui.pageWidgetByObjectName("DynamicTargetWidget");
    if (widget != null) {
        var newTarget = QFileDialog.getExistingDirectory("Select Installation Folder", widget.targetDirectory.text);
        if (newTarget != "") {
            widget.targetDirectory.setText(newTarget);
        }
    }
}

Component.prototype.licenseCheckPageEntered = function()
{
    var temp = QDesktopServices.storageLocation(QDesktopServices.TempLocation);
    var dir = installer.value("TargetDir");

    if (installer.value("os") == "win") {
        if (installer.fileExists(dir) && installer.fileExists(dir + "/OpenMVIDEUninst.exe")) {
            installer.execute("cmd.exe", ["/c", "echo function Controller(){gui.clickButton(buttons.NextButton);gui.clickButton(buttons.NextButton);installer.uninstallationFinished.connect(this,this.uninstallationFinished);}Controller.prototype.uninstallationFinished=function(){gui.clickButton(buttons.NextButton);}Controller.prototype.FinishedPageCallback=function(){gui.clickButton(buttons.FinishButton);}> %Temp%\\auto_uninstall.qs"])
            installer.execute(dir + "/OpenMVIDEUninst.exe", "--script=" + temp + "/auto_uninstall.qs");
        }
    }
    else if (installer.value("os") == "x11") {
        if (installer.fileExists(dir) && installer.fileExists(dir + "/OpenMVIDEUninstaller")) {
            installer.execute("bash", ["-c", "echo 'function Controller(){gui.clickButton(buttons.NextButton);gui.clickButton(buttons.NextButton);installer.uninstallationFinished.connect(this,this.uninstallationFinished);}Controller.prototype.uninstallationFinished=function(){gui.clickButton(buttons.NextButton);}Controller.prototype.FinishedPageCallback=function(){gui.clickButton(buttons.FinishButton);}' > /tmp/auto_uninstall.qs"])
            installer.execute(dir + "/OpenMVIDEUninstaller", "--script=" + temp + "/auto_uninstall.qs");
        }
    }
}
