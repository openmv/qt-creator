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
    //OPENMV-DIFF//
    //component.loaded.connect(this, Component.prototype.loaded);
    //OPENMV-DIFF//
    installer.installationFinished.connect(this, Component.prototype.installationFinishedPageIsShown);
    installer.finishButtonClicked.connect(this, Component.prototype.installationFinished);
    installer.setDefaultPageVisible(QInstaller.ComponentSelection, false);
}

Component.prototype.loaded = function()
{
    try {
        if (installer.value("os") == "win" && installer.isInstaller())
            installer.addWizardPageItem(component, "AssociateCommonFiletypesForm", QInstaller.TargetDirectory);
    } catch(e) {
        print(e);
    }
}

Component.prototype.createOperationsForArchive = function(archive)
{
    // if there are additional plugin 7zips, these must be extracted in .app/Contents on OS X
    if (systemInfo.productType !== "osx" || archive.indexOf('qtcreator.7z') !== -1)
        component.addOperation("Extract", archive, "@TargetDir@");
    else
        //OPENMV-DIFF//
        //component.addOperation("Extract", archive, "@TargetDir@/Qt Creator.app/Contents");
        //OPENMV-DIFF//
        component.addOperation("Extract", archive, "@TargetDir@/OpenMV IDE.app/Contents");
        //OPENMV-DIFF//
}

Component.prototype.beginInstallation = function()
{
    component.qtCreatorBinaryPath = installer.value("TargetDir");
    component.qtCreatorIconPath = installer.value("TargetDir");

    if (installer.value("os") == "win") {
        //OPENMV-DIFF//
        //component.qtCreatorBinaryPath = component.qtCreatorBinaryPath + "\\bin\\qtcreator.exe";
        //OPENMV-DIFF//
        component.qtCreatorBinaryPath = component.qtCreatorBinaryPath + "\\bin\\openmvide.exe";
        //OPENMV-DIFF//
        component.qtCreatorBinaryPath = component.qtCreatorBinaryPath.replace(/\//g, "\\");
    }
    else if (installer.value("os") == "x11")
        //OPENMV-DIFF//
        //component.qtCreatorBinaryPath = component.qtCreatorBinaryPath + "/bin/qtcreator";
        //OPENMV-DIFF//
        component.qtCreatorBinaryPath = component.qtCreatorBinaryPath + "/bin/openmvide";
        component.qtCreatorIconPath = component.qtCreatorIconPath + "/bin/openmv.png";
        //OPENMV-DIFF//
    else if (installer.value("os") == "mac")
        //OPENMV-DIFF//
        //component.qtCreatorBinaryPath = component.qtCreatorBinaryPath + "/Qt Creator.app/Contents/MacOS/Qt Creator";
        //OPENMV-DIFF//
        component.qtCreatorBinaryPath = component.qtCreatorBinaryPath + "/OpenMV IDE.app/Contents/MacOS/OpenMV IDE";
        //OPENMV-DIFF//

    if ( installer.value("os") === "win" )
        component.setStopProcessForUpdateRequest(component.qtCreatorBinaryPath, true);
}

registerCommonWindowsFileTypeExtensions = function()
{
    var headerExtensions = new Array("h", "hh", "hxx", "h++", "hpp");

    for (var i = 0; i < headerExtensions.length; ++i) {
        component.addOperation( "RegisterFileType",
                                headerExtensions[i],
                                component.qtCreatorBinaryPath + " -client \"%1\"",
                                "C++ Header file",
                                "text/plain",
                                component.qtCreatorBinaryPath + ",3",
                                "ProgId=QtProject.QtCreator." + headerExtensions[i]);
    }

    var cppExtensions = new Array("cc", "cxx", "c++", "cp", "cpp");

    for (var i = 0; i < cppExtensions.length; ++i) {
        component.addOperation( "RegisterFileType",
                                cppExtensions[i],
                                component.qtCreatorBinaryPath + " -client \"%1\"",
                                "C++ Source file",
                                "text/plain",
                                component.qtCreatorBinaryPath + ",2",
                                "ProgId=QtProject.QtCreator." + cppExtensions[i]);
    }

    component.addOperation( "RegisterFileType",
                            "c",
                            component.qtCreatorBinaryPath + " -client \"%1\"",
                            "C Source file",
                            "text/plain",
                            component.qtCreatorBinaryPath + ",1",
                            "ProgId=QtProject.QtCreator.c");
}

registerWindowsFileTypeExtensions = function()
{
    component.addOperation( "RegisterFileType",
                            "ui",
                            component.qtCreatorBinaryPath + " -client \"%1\"",
                            "Qt UI file",
                            "text/plain",
                            component.qtCreatorBinaryPath + ",4",
                            "ProgId=QtProject.QtCreator.ui");
    component.addOperation( "RegisterFileType",
                            "pro",
                            component.qtCreatorBinaryPath + " \"%1\"",
                            "Qt Project file",
                            "text/plain",
                            component.qtCreatorBinaryPath + ",5",
                            "ProgId=QtProject.QtCreator.pro");
    component.addOperation( "RegisterFileType",
                            "pri",
                            component.qtCreatorBinaryPath + " -client \"%1\"",
                            "Qt Project Include file",
                            "text/plain",
                            component.qtCreatorBinaryPath + ",6",
                            "ProgId=QtProject.QtCreator.pri");
    component.addOperation( "RegisterFileType",
                            "qbs",
                            component.qtCreatorBinaryPath + " \"%1\"",
                            "Qbs Project file",
                            "text/plain",
                            component.qtCreatorBinaryPath + ",5",
                            "ProgId=QtProject.QtCreator.qbs");
    component.addOperation( "RegisterFileType",
                            "qs",
                            component.qtCreatorBinaryPath + " -client \"%1\"",
                            "Qt Script file",
                            "text/plain",
                            component.qtCreatorBinaryPath + ",0",
                            "ProgId=QtProject.QtCreator.qs");
    component.addOperation( "RegisterFileType",
                            "qml",
                            component.qtCreatorBinaryPath + " -client \"%1\"",
                            "Qt Quick Markup language file",
                            "text/plain",
                            component.qtCreatorBinaryPath + ",7",
                            "ProgId=QtProject.QtCreator.qml");
}

Component.prototype.createOperations = function()
{
    // Call the base createOperations and afterwards set some registry settings
    component.createOperations();
    if ( installer.value("os") == "win" )
    {
        component.addOperation( "CreateShortcut",
                                component.qtCreatorBinaryPath,
                                //OPENMV-DIFF//
                                //"@StartMenuDir@/Qt Creator " + installer.value("ProductVersion") + ".lnk",
                                //OPENMV-DIFF//
                                "@StartMenuDir@/OpenMV IDE.lnk",
                                //OPENMV-DIFF//
                                "workingDirectory=@homeDir@" );
        //OPENMV-DIFF//
        //component.addOperation( "CreateShortcut",
        //                        component.qtCreatorBinaryPath,
        //                        "@DesktopDir@/OpenMV IDE.lnk",
        //                        "workingDirectory=@homeDir@" );
        component.addOperation( "CreateShortcut",
                                "@TargetDir@/OpenMVIDEUninst.exe",
                                "@StartMenuDir@/Uninstall.lnk",
                                "workingDirectory=@homeDir@" );
        component.addElevatedOperation("Execute", "{2,512}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\ftdi\\ftdi.cmd");
        component.addElevatedOperation("Execute", "{1,256}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\openmv\\openmv.cmd");
        //component.addElevatedOperation("Execute", "{1,256}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\pybcdc\\pybcdc.cmd");
        component.addElevatedOperation("Execute", "{1,256}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\dfuse.cmd");
        component.addElevatedOperation("Execute", "{0,3010}", "cmd", "/c", "@TargetDir@\\share\\qtcreator\\drivers\\vcr.cmd");
        //OPENMV-DIFF//

        //OPENMV-DIFF//
        // only install c runtime if it is needed, no minor version check of the c runtime till we need it
        //if (installer.value("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\12.0\\VC\\Runtimes\\x86\\Installed") != 1) {
        //   // return value 3010 means it need a reboot, but in most cases it is not needed for run Qt application
        //   // return value 5100 means there's a newer version of the runtime already installed
        //   component.addElevatedOperation("Execute", "{0,1638,3010,5100}", "@TargetDir@\\lib\\vcredist_msvc2013\\vcredist_x86.exe", "/norestart", "/q");
        //}
        //OPENMV-DIFF//

        //OPENMV-DIFF//
        //registerWindowsFileTypeExtensions();
        //OPENMV-DIFF//

        //OPENMV-DIFF//
        //if (component.userInterface("AssociateCommonFiletypesForm").AssociateCommonFiletypesCheckBox
        //    .checked) {
        //        registerCommonWindowsFileTypeExtensions();
        //}
        //OPENMV-DIFF//
    }
    if ( installer.value("os") == "x11" )
    {
        component.addOperation( "InstallIcons", "@TargetDir@/share/icons" );
        component.addOperation( "CreateDesktopEntry",
                                //OPENMV-DIFF//
                                //"QtProject-qtcreator.desktop",
                                //OPENMV-DIFF//
                                "OpenMV-openmvide.desktop",
                                //OPENMV-DIFF//
                                //OPENMV-DIFF//
                                //"Type=Application\nExec=" + component.qtCreatorBinaryPath + "\nPath=@TargetDir@\nName=Qt Creator\nGenericName=The IDE of choice for Qt development.\nGenericName[de]=Die IDE der Wahl zur Qt Entwicklung\nIcon=QtProject-qtcreator\nTerminal=false\nCategories=Development;IDE;Qt;\nMimeType=text/x-c++src;text/x-c++hdr;text/x-xsrc;application/x-designer;application/vnd.nokia.qt.qmakeprofile;application/vnd.nokia.xml.qt.resource;text/x-qml;text/x-qt.qml;text/x-qt.qbs;"
                                //OPENMV-DIFF//
                                "Type=Application\nExec=" + component.qtCreatorBinaryPath + "\nPath=@TargetDir@\nName=OpenMV IDE\nGenericName=The IDE of choice for OpenMV Cam Development.\nGenericName[de]=Die IDE der Wahl zur OpenMV Cam Entwicklung\nIcon=" + qtCreatorIconPath + "\nTerminal=false\nCategories=Development;IDE;\nMimeType=text/x-python;"
                                //OPENMV-DIFF//
                                );
        //OPENMV-DIFF//
        //component.addElevatedOperation( "Copy", "@TargetDir@/share/qtcreator/pydfu/50-openmv.rules", "/etc/udev/rules.d/50-openmv.rules" );
        //component.addElevatedOperation( "Execute", "{0}", "udevadm", "control", "--reload-rules" );
        //OPENMV-DIFF//
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
