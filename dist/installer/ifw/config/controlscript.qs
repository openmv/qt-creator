function Controller() {
    if (installer.isUninstaller()) {
        installer.setDefaultPageVisible(QInstaller.Introduction, true);
        installer.setDefaultPageVisible(QInstaller.ComponentSelection, false);
        installer.setDefaultPageVisible(QInstaller.LicenseCheck, false);
    } else {
        // No component declares a license, so the page auto-skips anyway --
        // hide it explicitly so it stops showing as a step in the sidebar.
        installer.setDefaultPageVisible(QInstaller.LicenseCheck, false);
    }
}

Controller.prototype.IntroductionPageCallback = function() {
    if (installer.isUninstaller()) {
        var widget = gui.currentPageWidget();
        if (widget != null) {
            widget.findChild("PackageManagerRadioButton").visible = false;
            widget.findChild("UpdaterRadioButton").visible = false;
            widget.findChild("UninstallerRadioButton").visible = false;
        }
    }
}

Controller.prototype.LicenseAgreementPageCallback = function() {
    var widget = gui.currentPageWidget();
    if (widget != null) {
        widget.AcceptLicenseRadioButton.checked = true;
    }
}
