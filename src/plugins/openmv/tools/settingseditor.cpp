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

#include "settingseditor.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <functional>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/hostosinfo.h>
#include <utils/qtcsettings.h>
#include <utils/utilsicons.h>

#include "openmvtr.h"

#define SETTINGS_EDITOR_GROUP "OpenMVSettingsEditor"
#define LAST_SETTINGS_PATH "LastSettingsPath"

namespace OpenMV {
namespace Internal {

// Generic simple-camera-app starter config. Exercises every control type so it doubles
// as a live example. Written verbatim by "Create Default Config", then opened in the editor.
static const char SCAFFOLD_JSON[] = R"json({
    "title": "Camera Settings",
    "controls": [
        { "type": "label", "align": "center", "text": "<b>This is a demo configuration.</b><br/>The controls below are examples that show off every setting type you can build &mdash; they are <i>not</i> a real device config. Replace them with the controls your own application needs, then edit the values, click <b>Save</b>, and read them back in your script with <code>json.load()</code>. See the <a href='https://docs.openmv.io'>documentation</a> for details." },
        { "type": "tabs", "tab_position": "north", "tabs": [
            { "title": "Camera", "tooltip": "Sensor and capture settings", "controls": [
                { "type": "label", "text": "<b>Image capture</b>" },
                { "type": "group", "title": "Manual Exposure", "checkable": true, "name": "manual_exposure", "value": false, "controls": [
                    { "type": "slider", "name": "exposure_us", "label": "Exposure", "value": 10000, "min": 0, "max": 33000, "step": 100, "suffix": " us", "ticks": 5500, "tooltip": "Shutter time in microseconds" },
                    { "type": "slider", "name": "gain_db", "label": "Gain", "value": 8, "min": 0, "max": 24, "suffix": " dB", "ticks": 4 }
                ] },
                { "type": "group", "title": "Format", "controls": [
                    { "type": "combobox", "name": "resolution", "label": "Resolution", "value": "qvga", "options": ["QQVGA", "QVGA", "VGA"], "values": ["qqvga", "qvga", "vga"] },
                    { "type": "combobox", "name": "pixformat", "label": "Pixel Format", "value": 1, "options": ["Grayscale", "RGB565"] },
                    { "type": "checkbox", "name": "h_mirror", "label": "Horizontal Mirror", "value": false },
                    { "type": "checkbox", "name": "v_flip", "label": "Vertical Flip", "value": false },
                    { "type": "slider", "name": "digital_zoom", "label": "Digital Zoom", "value": 1, "min": 1, "max": 8, "prefix": "x", "ticks": 1 }
                ] }
            ] },
            { "title": "Processing", "tooltip": "Detection and overlays", "controls": [
                { "type": "group", "title": "Detection", "controls": [
                    { "type": "radio", "name": "mode", "label": "Mode", "value": "idle", "options": ["Idle", "Track", "Record"], "values": ["idle", "track", "record"], "orientation": "horizontal" },
                    { "type": "slider", "name": "threshold", "label": "<b>Threshold</b>", "value": 50, "min": 0, "max": 100, "suffix": " %", "ticks": 25 },
                    { "type": "spinbox", "name": "min_area", "label": "Min Area", "value": 100, "min": 0, "max": 10000, "suffix": " px", "special_value_text": "Off", "group_separator": true },
                    { "type": "doublespinbox", "name": "sensitivity", "label": "Sensitivity", "value": 50, "min": 0, "max": 100, "step": 0.5, "decimals": 1, "suffix": " %", "special_value_text": "Auto" },
                    { "type": "checkbox", "name": "draw_overlays", "label": "Draw Overlays", "value": true }
                ] },
                { "type": "group", "title": "Recording", "controls": [
                    { "type": "radio", "name": "quality", "label": "Quality", "value": 1, "options": ["Low", "Medium", "High"] },
                    { "type": "checkbox", "name": "capture_mode", "label": "Capture (off / single / continuous)", "tristate": true, "value": 1 }
                ] }
            ] },
            { "title": "Network", "tooltip": "Wi-Fi and server", "controls": [
                { "type": "label", "text": "<i>Network configuration</i>" },
                { "type": "group", "title": "Wi-Fi", "checkable": true, "name": "wifi_enabled", "value": true, "controls": [
                    { "type": "lineedit", "name": "wifi_ssid", "label": "SSID", "value": "", "placeholder": "Network name", "max_length": 32, "clear_button": true },
                    { "type": "lineedit", "name": "wifi_password", "label": "Password", "value": "", "password": true, "clear_button": true },
                    { "type": "lineedit", "name": "static_ip", "label": "Static IP", "value": "192.168.001.100", "mask": "000.000.000.000;_", "tooltip": "Set to 0.0.0.0 for DHCP" }
                ] },
                { "type": "group", "title": "Server", "controls": [
                    { "type": "spinbox", "name": "server_port", "label": "Port", "value": 8080, "min": 0, "max": 65535, "group_separator": true },
                    { "type": "lineedit", "name": "hostname", "label": "Hostname", "value": "openmv-cam", "regex": "[A-Za-z0-9-]+", "placeholder": "letters, digits, dashes", "clear_button": true }
                ] }
            ] },
            { "title": "System", "tooltip": "Device and power", "controls": [
                { "type": "group", "title": "Device", "controls": [
                    { "type": "lineedit", "name": "device_name", "label": "Device Name", "value": "openmv-cam", "clear_button": true },
                    { "type": "combobox", "name": "led", "label": "LED", "value": 0, "options": ["Off", "Red", "Green", "Blue"] },
                    { "type": "spinbox", "name": "i2c_address", "label": "I2C Address", "value": 48, "min": 0, "max": 255, "base": 16, "prefix": "0x" },
                    { "type": "lineedit", "name": "firmware", "label": "Firmware", "value": "4.5.0", "enabled": false }
                ] },
                { "type": "group", "title": "Power", "controls": [
                    { "type": "radio", "name": "power_mode", "label": "Power Mode", "value": "balanced", "options": ["Performance", "Balanced", "Low Power"], "values": ["perf", "balanced", "low"] },
                    { "type": "slider", "name": "led_brightness", "label": "LED Brightness", "value": 80, "min": 0, "max": 100, "suffix": " %", "ticks": 20 }
                ] },
                { "type": "group", "title": "Storage", "controls": [
                    { "type": "doublespinbox", "name": "storage_limit", "label": "Storage Limit", "value": 1024.5, "min": 0, "max": 65536, "step": 0.5, "decimals": 1, "prefix": "max ", "suffix": " MB", "group_separator": true }
                ] }
            ] }
        ] },
        { "type": "label", "align": "right", "text": "<i>Edit the values, then click Save.</i>" }
    ]
}
)json";

// Value controls (carry a saved "value" the IDE harvests); everything else is a container
// ("tabs"/"group") or display-only ("label"/unknown). Both build and harvest classify with
// this so the per-control getters stay aligned.
static bool isValueType(const QString &type)
{
    return (type == QLatin1String("checkbox"))
        || (type == QLatin1String("combobox"))
        || (type == QLatin1String("radio"))
        || (type == QLatin1String("spinbox"))
        || (type == QLatin1String("doublespinbox"))
        || (type == QLatin1String("slider"))
        || (type == QLatin1String("lineedit"));
}

static Qt::WindowFlags dialogFlags()
{
    // Resizable (no fixed-size hint) -- configs can be long and the dialog scrolls.
    return Qt::WindowTitleHint | Qt::WindowSystemMenuHint
        | (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint);
}

static QString fallbackDir(const QString &drivePath)
{
    if (!drivePath.isEmpty()) {
        return drivePath;
    }

    const QString omv = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/OpenMV");

    return QDir(omv).exists() ? omv : QDir::homePath();
}

static void applyCommon(QWidget *widget, const QJsonObject &ctrl)
{
    if (ctrl.contains(QStringLiteral("tooltip"))) {
        widget->setToolTip(ctrl.value(QStringLiteral("tooltip")).toString());
    }

    if (ctrl.contains(QStringLiteral("enabled"))) {
        widget->setEnabled(ctrl.value(QStringLiteral("enabled")).toBool(true));
    }
}

namespace {

class SettingsEditorDialog : public QDialog
{
public:
    SettingsEditorDialog(const QJsonObject &root, QWidget *parent);
    QJsonObject updatedRoot();

private:
    void buildControls(const QJsonArray &controls, QFormLayout *form);
    QWidget *buildValueWidget(const QJsonObject &ctrl);
    QJsonArray harvestControls(const QJsonArray &controls);
    void verifyAndAccept();

    QJsonObject m_root;
    QList<std::function<QJsonValue()>> m_getters; // value controls, depth-first build order
    QList<std::function<QString()>> m_invalidChecks; // each returns an offending field label, or empty if ok
    int m_harvestIndex = 0;
};

SettingsEditorDialog::SettingsEditorDialog(const QJsonObject &root, QWidget *parent)
    : QDialog(parent, dialogFlags())
    , m_root(root)
{
    setWindowTitle(root.value(QStringLiteral("title")).toString(Tr::tr("OpenMV Cam Settings Editor")));

    QVBoxLayout *outer = new QVBoxLayout(this);

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    QWidget *content = new QWidget(scroll);
    QFormLayout *form = new QFormLayout(content);
    form->setContentsMargins(0, 0, 0, 0); // the dialog's outer layout already pads the window
    buildControls(root.value(QStringLiteral("controls")).toArray(), form);
    scroll->setWidget(content);
    outer->addWidget(scroll);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton *save = new QPushButton(Tr::tr("Save"), box);
    box->addButton(save, QDialogButtonBox::AcceptRole);
    outer->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, [this] { verifyAndAccept(); });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(480, 600);
}

void SettingsEditorDialog::buildControls(const QJsonArray &controls, QFormLayout *form)
{
    for (const QJsonValue &cv : controls) {
        const QJsonObject ctrl = cv.toObject();
        const QString type = ctrl.value(QStringLiteral("type")).toString();

        if (type == QLatin1String("tabs")) {
            QTabWidget *tabs = new QTabWidget(form->parentWidget());
            const QString pos = ctrl.value(QStringLiteral("tab_position")).toString();
            if (pos == QLatin1String("south")) tabs->setTabPosition(QTabWidget::South);
            else if (pos == QLatin1String("west")) tabs->setTabPosition(QTabWidget::West);
            else if (pos == QLatin1String("east")) tabs->setTabPosition(QTabWidget::East);
            else tabs->setTabPosition(QTabWidget::North);
            for (const QJsonValue &tv : ctrl.value(QStringLiteral("tabs")).toArray()) {
                const QJsonObject tab = tv.toObject();
                QWidget *page = new QWidget(tabs);
                QFormLayout *pageForm = new QFormLayout(page);
                buildControls(tab.value(QStringLiteral("controls")).toArray(), pageForm);
                const int idx = tabs->addTab(page, tab.value(QStringLiteral("title")).toString());
                if (tab.contains(QStringLiteral("tooltip"))) tabs->setTabToolTip(idx, tab.value(QStringLiteral("tooltip")).toString());
            }
            form->addRow(tabs); // full-width spanning row
        } else if (type == QLatin1String("group")) {
            QGroupBox *groupBox = new QGroupBox(ctrl.value(QStringLiteral("title")).toString(), form->parentWidget());
            if (ctrl.value(QStringLiteral("checkable")).toBool(false)) {
                // The title gets a checkbox that enables/disables every child and is itself
                // a saved bool value -- registered before the children so build/harvest align.
                groupBox->setCheckable(true);
                groupBox->setChecked(ctrl.value(QStringLiteral("value")).toBool(true));
                m_getters.append([groupBox] { return QJsonValue(groupBox->isChecked()); });
            }
            QFormLayout *groupForm = new QFormLayout(groupBox);
            buildControls(ctrl.value(QStringLiteral("controls")).toArray(), groupForm);
            applyCommon(groupBox, ctrl);
            form->addRow(groupBox); // full-width spanning row
        } else if (type == QLatin1String("label")) {
            QLabel *label = new QLabel(ctrl.value(QStringLiteral("text")).toString(), form->parentWidget());
            label->setWordWrap(true);
            label->setTextFormat(Qt::RichText);  // "text" may contain HTML markup
            label->setOpenExternalLinks(true);   // and clickable <a href="..."> links
            const QString align = ctrl.value(QStringLiteral("align")).toString();
            if (align == QLatin1String("center")) label->setAlignment(Qt::AlignHCenter);
            else if (align == QLatin1String("right")) label->setAlignment(Qt::AlignRight);
            applyCommon(label, ctrl);
            form->addRow(label); // full-width spanning row -- it IS the text
        } else if (type == QLatin1String("checkbox")) {
            // The label rides on the checkbox itself, spanning the row, so the box and its
            // text share one baseline -- a separate form-column label sits misaligned.
            form->addRow(buildValueWidget(ctrl));
        } else if (isValueType(type)) {
            QWidget *widget = buildValueWidget(ctrl);
            // Build the row label ourselves so it accepts rich text (HTML) like a `label`
            // control, rather than the plain string QFormLayout would create.
            QLabel *rowLabel = new QLabel(ctrl.value(QStringLiteral("label")).toString(), form->parentWidget());
            rowLabel->setTextFormat(Qt::RichText);
            rowLabel->setOpenExternalLinks(true);
            form->addRow(rowLabel, widget); // label left, widget right
        } else {
            QLabel *note = new QLabel(Tr::tr("Unknown control: %L1")
                .arg(type.isEmpty() ? Tr::tr("(missing type)") : type), form->parentWidget());
            note->setEnabled(false);
            form->addRow(note);
        }
    }
}

QWidget *SettingsEditorDialog::buildValueWidget(const QJsonObject &ctrl)
{
    const QString type = ctrl.value(QStringLiteral("type")).toString();
    QWidget *result = nullptr;

    if (type == QLatin1String("checkbox")) {
        QCheckBox *checkBox = new QCheckBox(ctrl.value(QStringLiteral("label")).toString());
        if (ctrl.value(QStringLiteral("tristate")).toBool(false)) {
            // Three states: value is the int check state 0 (off) / 1 (partial) / 2 (on).
            checkBox->setTristate(true);
            const QJsonValue v = ctrl.value(QStringLiteral("value"));
            const int state = v.isBool() ? (v.toBool() ? 2 : 0) : v.toInt(0);
            checkBox->setCheckState(static_cast<Qt::CheckState>(qBound(0, state, 2)));
            m_getters.append([checkBox] { return QJsonValue(static_cast<int>(checkBox->checkState())); });
        } else {
            checkBox->setChecked(ctrl.value(QStringLiteral("value")).toBool(false));
            m_getters.append([checkBox] { return QJsonValue(checkBox->isChecked()); });
        }
        result = checkBox;
    } else if (type == QLatin1String("combobox")) {
        QComboBox *comboBox = new QComboBox;
        for (const QJsonValue &option : ctrl.value(QStringLiteral("options")).toArray()) {
            comboBox->addItem(option.toString());
        }
        // Optional parallel "values" payloads: show options[i] but save/restore values[i]
        // (any JSON type) instead of the bare index. Falls back to the index when absent.
        const QJsonArray values = ctrl.value(QStringLiteral("values")).toArray();
        const QJsonValue current = ctrl.value(QStringLiteral("value"));
        int index = 0;
        if (!values.isEmpty()) {
            for (int i = 0; i < values.size(); i++) { if (values.at(i) == current) { index = i; break; } }
        } else {
            index = current.toInt(0);
        }
        if (comboBox->count() > 0) {
            comboBox->setCurrentIndex(qBound(0, index, comboBox->count() - 1));
        }
        m_getters.append([comboBox, values] {
            const int i = qMax(0, comboBox->currentIndex());
            return (!values.isEmpty() && (i < values.size())) ? QJsonValue(values.at(i)) : QJsonValue(i);
        });
        result = comboBox;
    } else if (type == QLatin1String("radio")) {
        QWidget *container = new QWidget;
        const bool horizontal = ctrl.value(QStringLiteral("orientation")).toString() == QLatin1String("horizontal");
        QBoxLayout *layout = horizontal
            ? static_cast<QBoxLayout *>(new QHBoxLayout(container))
            : static_cast<QBoxLayout *>(new QVBoxLayout(container));
        layout->setContentsMargins(0, 0, 0, 0);
        QButtonGroup *group = new QButtonGroup(container);
        const QJsonArray options = ctrl.value(QStringLiteral("options")).toArray();
        for (int i = 0; i < options.size(); i++) {
            QRadioButton *button = new QRadioButton(options.at(i).toString(), container);
            group->addButton(button, i);
            layout->addWidget(button);
        }
        // Same optional "values" payload mapping as combobox.
        const QJsonArray values = ctrl.value(QStringLiteral("values")).toArray();
        const QJsonValue current = ctrl.value(QStringLiteral("value"));
        int index = 0;
        if (!values.isEmpty()) {
            for (int i = 0; i < values.size(); i++) { if (values.at(i) == current) { index = i; break; } }
        } else {
            index = current.toInt(0);
        }
        if (QAbstractButton *button = group->button(qBound(0, index, qMax(0, options.size() - 1)))) {
            button->setChecked(true);
        }
        m_getters.append([group, values] {
            const int i = qMax(0, group->checkedId());
            return (!values.isEmpty() && (i < values.size())) ? QJsonValue(values.at(i)) : QJsonValue(i);
        });
        result = container;
    } else if (type == QLatin1String("spinbox")) {
        QSpinBox *spinBox = new QSpinBox;
        const int value = ctrl.value(QStringLiteral("value")).toInt(0);
        const int lo = ctrl.contains(QStringLiteral("min")) ? ctrl.value(QStringLiteral("min")).toInt() : qMin(0, value);
        const int hi = ctrl.contains(QStringLiteral("max")) ? ctrl.value(QStringLiteral("max")).toInt() : qMax(100, value);
        spinBox->setRange(lo, qMax(lo, hi));
        if (ctrl.contains(QStringLiteral("step"))) spinBox->setSingleStep(ctrl.value(QStringLiteral("step")).toInt(1));
        if (ctrl.contains(QStringLiteral("prefix"))) spinBox->setPrefix(ctrl.value(QStringLiteral("prefix")).toString());
        if (ctrl.contains(QStringLiteral("suffix"))) spinBox->setSuffix(ctrl.value(QStringLiteral("suffix")).toString());
        if (ctrl.contains(QStringLiteral("base"))) spinBox->setDisplayIntegerBase(ctrl.value(QStringLiteral("base")).toInt(10));
        if (ctrl.value(QStringLiteral("group_separator")).toBool(false)) spinBox->setGroupSeparatorShown(true);
        // Text shown in place of the number when at the minimum (e.g. 0 -> "Off"/"Auto").
        if (ctrl.contains(QStringLiteral("special_value_text"))) spinBox->setSpecialValueText(ctrl.value(QStringLiteral("special_value_text")).toString());
        spinBox->setValue(value);
        m_getters.append([spinBox] { return QJsonValue(spinBox->value()); });
        result = spinBox;
    } else if (type == QLatin1String("doublespinbox")) {
        QDoubleSpinBox *spinBox = new QDoubleSpinBox;
        const double value = ctrl.value(QStringLiteral("value")).toDouble(0.0);
        const double lo = ctrl.contains(QStringLiteral("min")) ? ctrl.value(QStringLiteral("min")).toDouble() : qMin(0.0, value);
        const double hi = ctrl.contains(QStringLiteral("max")) ? ctrl.value(QStringLiteral("max")).toDouble() : qMax(100.0, value);
        spinBox->setDecimals(ctrl.contains(QStringLiteral("decimals")) ? ctrl.value(QStringLiteral("decimals")).toInt(2) : 2);
        spinBox->setRange(lo, qMax(lo, hi));
        if (ctrl.contains(QStringLiteral("step"))) spinBox->setSingleStep(ctrl.value(QStringLiteral("step")).toDouble(1.0));
        if (ctrl.contains(QStringLiteral("prefix"))) spinBox->setPrefix(ctrl.value(QStringLiteral("prefix")).toString());
        if (ctrl.contains(QStringLiteral("suffix"))) spinBox->setSuffix(ctrl.value(QStringLiteral("suffix")).toString());
        if (ctrl.value(QStringLiteral("group_separator")).toBool(false)) spinBox->setGroupSeparatorShown(true);
        // Text shown in place of the number when at the minimum (e.g. 0.0 -> "Auto").
        if (ctrl.contains(QStringLiteral("special_value_text"))) spinBox->setSpecialValueText(ctrl.value(QStringLiteral("special_value_text")).toString());
        spinBox->setValue(value);
        m_getters.append([spinBox] { return QJsonValue(spinBox->value()); });
        result = spinBox;
    } else if (type == QLatin1String("slider")) {
        QWidget *container = new QWidget;
        QHBoxLayout *layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        QSlider *slider = new QSlider(Qt::Horizontal, container);
        const int value = ctrl.value(QStringLiteral("value")).toInt(0);
        const int lo = ctrl.contains(QStringLiteral("min")) ? ctrl.value(QStringLiteral("min")).toInt() : qMin(0, value);
        const int hi = ctrl.contains(QStringLiteral("max")) ? ctrl.value(QStringLiteral("max")).toInt() : qMax(100, value);
        const QString prefix = ctrl.value(QStringLiteral("prefix")).toString();
        const QString suffix = ctrl.value(QStringLiteral("suffix")).toString();
        slider->setRange(lo, qMax(lo, hi));
        const int step = ctrl.contains(QStringLiteral("step")) ? qMax(1, ctrl.value(QStringLiteral("step")).toInt(1)) : 1;
        slider->setSingleStep(step);
        if (ctrl.contains(QStringLiteral("ticks"))) {
            // "ticks" is the spacing between tick marks drawn below the groove.
            slider->setTickPosition(QSlider::TicksBelow);
            const int interval = ctrl.value(QStringLiteral("ticks")).toInt(0);
            if (interval > 0) slider->setTickInterval(interval);
        }
        if (step > 1) {
            // QSlider only applies the step to keyboard/page moves, not mouse drags. Snap the
            // value to the nearest multiple so a dragged slider still yields clean steps.
            connect(slider, &QSlider::valueChanged, slider, [slider, step](int v) {
                const int base = slider->minimum();
                const int snapped = qBound(slider->minimum(), base + qRound(double(v - base) / step) * step, slider->maximum());
                if (snapped != v) slider->setValue(snapped);
            });
        }
        slider->setValue(value); // snaps via the connection above when a step is set
        QLabel *readout = new QLabel(prefix + QString::number(slider->value()) + suffix, container);
        readout->setMinimumWidth(readout->fontMetrics().horizontalAdvance(prefix + QString::number(qMax(lo, hi)) + suffix) + 4);
        readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(slider, &QSlider::valueChanged, readout, [readout, prefix, suffix](int v) { readout->setText(prefix + QString::number(v) + suffix); });
        layout->addWidget(slider);
        layout->addWidget(readout);
        m_getters.append([slider] { return QJsonValue(slider->value()); });
        result = container;
    } else { // lineedit
        QLineEdit *lineEdit = new QLineEdit;
        // An input mask must be applied before the text, or non-matching text is rejected.
        if (ctrl.contains(QStringLiteral("mask"))) lineEdit->setInputMask(ctrl.value(QStringLiteral("mask")).toString());
        lineEdit->setText(ctrl.value(QStringLiteral("value")).toString());
        if (ctrl.contains(QStringLiteral("placeholder"))) lineEdit->setPlaceholderText(ctrl.value(QStringLiteral("placeholder")).toString());
        if (ctrl.contains(QStringLiteral("max_length"))) lineEdit->setMaxLength(ctrl.value(QStringLiteral("max_length")).toInt());
        if (ctrl.contains(QStringLiteral("regex"))) {
            // Restrict typed input to a regular expression (e.g. hostname/SSID charset).
            lineEdit->setValidator(new QRegularExpressionValidator(
                QRegularExpression(ctrl.value(QStringLiteral("regex")).toString()), lineEdit));
        }
        if (ctrl.value(QStringLiteral("clear_button")).toBool(false)) lineEdit->setClearButtonEnabled(true);
        if (ctrl.contains(QStringLiteral("mask")) || ctrl.contains(QStringLiteral("regex"))) {
            // A mask/validator can leave the field intermediate (e.g. a half-typed IP). Flag it
            // on Save so the user doesn't persist incomplete input. hasAcceptableInput() covers both.
            const QString fieldLabel = ctrl.value(QStringLiteral("label")).toString();
            m_invalidChecks.append([lineEdit, fieldLabel]() -> QString {
                return lineEdit->hasAcceptableInput() ? QString() : fieldLabel;
            });
        }
        if (ctrl.value(QStringLiteral("password")).toBool(false)) {
            lineEdit->setEchoMode(QLineEdit::Password);
            // A trailing eye icon that toggles the masked text on/off.
            QAction *reveal = lineEdit->addAction(Utils::Icons::EYE_CLOSED_TOOLBAR.icon(), QLineEdit::TrailingPosition);
            reveal->setToolTip(Tr::tr("Show text"));
            connect(reveal, &QAction::triggered, lineEdit, [lineEdit, reveal] {
                const bool hidden = (lineEdit->echoMode() == QLineEdit::Password);
                lineEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
                reveal->setIcon(hidden ? Utils::Icons::EYE_OPEN_TOOLBAR.icon() : Utils::Icons::EYE_CLOSED_TOOLBAR.icon());
                reveal->setToolTip(hidden ? Tr::tr("Hide text") : Tr::tr("Show text"));
            });
        }
        m_getters.append([lineEdit] { return QJsonValue(lineEdit->text()); });
        result = lineEdit;
    }

    applyCommon(result, ctrl);
    return result;
}

QJsonArray SettingsEditorDialog::harvestControls(const QJsonArray &controls)
{
    QJsonArray out;

    for (const QJsonValue &cv : controls) {
        QJsonObject ctrl = cv.toObject();
        const QString type = ctrl.value(QStringLiteral("type")).toString();

        if (type == QLatin1String("tabs")) {
            QJsonArray tabs;
            for (const QJsonValue &tv : ctrl.value(QStringLiteral("tabs")).toArray()) {
                QJsonObject tab = tv.toObject();
                tab[QStringLiteral("controls")] = harvestControls(tab.value(QStringLiteral("controls")).toArray());
                tabs.append(tab);
            }
            ctrl[QStringLiteral("tabs")] = tabs;
        } else if (type == QLatin1String("group")) {
            // Checkable groups carry a bool value (the title checkbox), harvested before the
            // children to match the build-order getter registration.
            if (ctrl.value(QStringLiteral("checkable")).toBool(false) && (m_harvestIndex < m_getters.size())) {
                ctrl[QStringLiteral("value")] = m_getters.at(m_harvestIndex++)();
            }
            ctrl[QStringLiteral("controls")] = harvestControls(ctrl.value(QStringLiteral("controls")).toArray());
        } else if (isValueType(type)) {
            if (m_harvestIndex < m_getters.size()) {
                ctrl[QStringLiteral("value")] = m_getters.at(m_harvestIndex++)();
            }
        }
        // "label" / unknown: nothing to harvest. Extra/unknown keys are preserved as-is.

        out.append(ctrl);
    }

    return out;
}

void SettingsEditorDialog::verifyAndAccept()
{
    QStringList invalid;
    for (const std::function<QString()> &check : m_invalidChecks) {
        const QString label = check();
        if (!label.isEmpty()) {
            invalid.append(label);
        }
    }

    if (!invalid.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
            Tr::tr("These fields have invalid or incomplete input:\n\n%L1\n\nFix or clear them before saving.")
                .arg(invalid.join(QLatin1Char('\n'))));
        return;
    }

    accept();
}

QJsonObject SettingsEditorDialog::updatedRoot()
{
    QJsonObject root = m_root;
    m_harvestIndex = 0;
    root[QStringLiteral("controls")] = harvestControls(m_root.value(QStringLiteral("controls")).toArray());
    return root;
}

// Read + parse + validate a config file, show the editor, and on Save write the updated
// JSON back to the same path. Shared by both menu actions.
static void openConfigFile(const QString &path, const ConfigFileWriter &writer)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(Core::ICore::dialogParent(), Tr::tr("OpenMV Cam Settings Editor"),
            Tr::tr("Unable to open:\n\n%L1\n\n%L2").arg(path, file.errorString()));
        return;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if ((error.error != QJsonParseError::NoError) || (!doc.isObject())) {
        QMessageBox::critical(Core::ICore::dialogParent(), Tr::tr("OpenMV Cam Settings Editor"),
            Tr::tr("Not a valid JSON config file:\n\n%L1").arg((error.error != QJsonParseError::NoError)
                ? error.errorString() : Tr::tr("the top level must be a JSON object.")));
        return;
    }

    const QJsonObject root = doc.object();

    if (!root.value(QStringLiteral("controls")).isArray()) {
        QMessageBox::critical(Core::ICore::dialogParent(), Tr::tr("OpenMV Cam Settings Editor"),
            Tr::tr("This JSON file has no \"controls\" array to build a GUI from."));
        return;
    }

    SettingsEditorDialog dialog(root, Core::ICore::dialogParent());

    if (dialog.exec() == QDialog::Accepted) {
        const QByteArray out = QJsonDocument(dialog.updatedRoot()).toJson(QJsonDocument::Indented);

        QString err;
        if (!writer(path, out, &err)) {
            QMessageBox::critical(Core::ICore::dialogParent(), Tr::tr("OpenMV Cam Settings Editor"),
                Tr::tr("Unable to save:\n\n%L1\n\n%L2").arg(path, err));
        }
    }
}

} // anonymous namespace

void settingsEditorAction(const QString &drivePath, const ConfigFileWriter &writer)
{
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

    const QString start = settings->value(SETTINGS_EDITOR_GROUP "/" LAST_SETTINGS_PATH,
        fallbackDir(drivePath)).toString();

    const QString path = QFileDialog::getOpenFileName(Core::ICore::dialogParent(),
        Tr::tr("Open Config File"), start, Tr::tr("JSON Files (*.json)"));

    if (path.isEmpty()) {
        return;
    }

    settings->setValue(SETTINGS_EDITOR_GROUP "/" LAST_SETTINGS_PATH, path);
    openConfigFile(path, writer);
}

void createDefaultConfigAction(const QString &drivePath, const ConfigFileWriter &writer)
{
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

    const QString last = settings->value(SETTINGS_EDITOR_GROUP "/" LAST_SETTINGS_PATH,
        fallbackDir(drivePath)).toString();
    const QFileInfo lastInfo(last);
    const QString dir = lastInfo.isDir() ? last : lastInfo.absolutePath();
    const QString suggested = QDir::cleanPath(dir + QStringLiteral("/config.json"));

    QString path = QFileDialog::getSaveFileName(Core::ICore::dialogParent(),
        Tr::tr("Create Default Config"), suggested, Tr::tr("JSON Files (*.json)"));

    if (path.isEmpty()) {
        return;
    }

    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".json");
    }

    const QByteArray scaffold = QByteArray(SCAFFOLD_JSON);

    QString err;
    if (!writer(path, scaffold, &err)) {
        QMessageBox::critical(Core::ICore::dialogParent(), Tr::tr("OpenMV Cam Settings Editor"),
            Tr::tr("Unable to create:\n\n%L1\n\n%L2").arg(path, err));
        return;
    }

    settings->setValue(SETTINGS_EDITOR_GROUP "/" LAST_SETTINGS_PATH, path);
    openConfigFile(path, writer);
}

} // namespace Internal
} // namespace OpenMV
