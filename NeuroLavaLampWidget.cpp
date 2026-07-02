#include "NeuroLavaLampWidget.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVariant>
#include <QScrollArea>
#include <QPushButton>
#include <QApplication>
#include <QToolButton>
#include <QLabel>
#include <QTabWidget>
#include <QComboBox>
#include "SettingsManager.h"

using json = nlohmann::json;

NeuroLavaLampWidget::NeuroLavaLampWidget(ResourceManagerInterface* rm, QWidget *parent)
    : QWidget(parent)
    , resource_manager(rm)
    , sse_reply(nullptr)
    , is_live(false)
{
    layout = new QHBoxLayout(this);
    
    QVBoxLayout* left_layout = new QVBoxLayout();
    QVBoxLayout* right_layout = new QVBoxLayout();
    
    layout->addLayout(left_layout, 1);
    layout->addLayout(right_layout, 1);
    
    // Top Section: Settings
    QGroupBox* settings_group = new QGroupBox("API Settings");
    QFormLayout* settings_layout = new QFormLayout(settings_group);
    
    url_input = new QLineEdit("https://api.neurolavalamp.com/v1/rgb");
    sse_url_input = new QLineEdit("https://api.neurolavalamp.com/v1/events");
    offline_interval_input = new QSpinBox();
    offline_interval_input->setRange(100, 60000);
    offline_interval_input->setValue(10000);
    offline_interval_input->setSuffix(" ms");
    
    live_interval_input = new QSpinBox();
    live_interval_input->setRange(100, 60000);
    live_interval_input->setValue(3000);
    live_interval_input->setSuffix(" ms");
    
    settings_layout->addRow("SSE Stream URL:", sse_url_input);
    settings_layout->addRow("HTTP Polling URL:", url_input);
    settings_layout->addRow("Offline Poll Interval:", offline_interval_input);
    settings_layout->addRow("Live Poll Interval:", live_interval_input);
    left_layout->addWidget(settings_group);
    
    // Extra Options Group
    QGroupBox* extra_group = new QGroupBox("Extra Options");
    QFormLayout* extra_layout = new QFormLayout(extra_group);
    
    zero_color_behavior_input = new QComboBox();
    zero_color_behavior_input->addItem("Allow it (Turn Off)", 0);
    zero_color_behavior_input->addItem("Keep previous color", 1);
    zero_color_behavior_input->addItem("Restore background effects", 2);
    
    QLabel* zero_color_label = new QLabel("Zero-Color Behavior?");
    QString zc_tooltip = "What happens when Neuro sends a (0,0,0) completely black color:\n"
                         "Allow it: Turns the LEDs off.\n"
                         "Keep previous: Ignores the black color.\n"
                         "Restore background effects: Temporarily restores your standard OpenRGB effects until Neuro sends a real color again.";
    zero_color_label->setToolTip(zc_tooltip);
    zero_color_behavior_input->setToolTip(zc_tooltip);
    
    color_effect_input = new QComboBox();
    color_effect_input->addItem("No Effect (Instant)", 0);
    color_effect_input->addItem("Fade", 1);
    color_effect_input->addItem("Wave", 2);
    color_effect_input->addItem("Pulse", 3);
    color_effect_input->addItem("Flash", 4);
    
    animation_fps_input = new QSpinBox();
    animation_fps_input->setRange(1, 60);
    animation_fps_input->setValue(30);
    animation_fps_input->setSuffix(" FPS");
    
    extra_layout->addRow(zero_color_label, zero_color_behavior_input);
    extra_layout->addRow("Color Effect:", color_effect_input);
    extra_layout->addRow("Animation FPS:", animation_fps_input);
    
    left_layout->addWidget(extra_group);

    connect(url_input, &QLineEdit::editingFinished, this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(sse_url_input, &QLineEdit::editingFinished, this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(offline_interval_input, QOverload<int>::of(&QSpinBox::valueChanged), this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(live_interval_input, QOverload<int>::of(&QSpinBox::valueChanged), this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(zero_color_behavior_input, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(color_effect_input, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(animation_fps_input, QOverload<int>::of(&QSpinBox::valueChanged), this, &NeuroLavaLampWidget::onSettingsChanged);
    
    // Status Label
    status_label = new QLabel("Neuro Lava Lamp: Status Unknown");
    left_layout->addWidget(status_label);
    left_layout->addStretch();

    // Load Settings
    loadSettings();

    // Device List Section
    QGroupBox* devices_group = new QGroupBox("Apply to Devices");
    QVBoxLayout* devices_group_layout = new QVBoxLayout(devices_group);
    
    QHBoxLayout* device_buttons_layout = new QHBoxLayout();
    QPushButton* select_all_btn = new QPushButton("Select All");
    device_buttons_layout->addWidget(select_all_btn);
    device_buttons_layout->addStretch();
    devices_group_layout->addLayout(device_buttons_layout);
    
    connect(select_all_btn, &QPushButton::clicked, this, &NeuroLavaLampWidget::onSelectAllClicked);

    QScrollArea* scroll_area = new QScrollArea();
    scroll_area->setWidgetResizable(true);
    QWidget* scroll_content = new QWidget();
    device_list_layout = new QVBoxLayout(scroll_content);
    scroll_area->setWidget(scroll_content);
    devices_group_layout->addWidget(scroll_area);

    right_layout->addWidget(devices_group);

    network_manager = new QNetworkAccessManager(this);
    connect(network_manager, &QNetworkAccessManager::finished, this, &NeuroLavaLampWidget::onNetworkReply);

    poll_timer = new QTimer(this);
    connect(poll_timer, &QTimer::timeout, this, &NeuroLavaLampWidget::pollApi);
    poll_timer->start(offline_interval_input->value());
    
    animation_timer = new QTimer(this);
    connect(animation_timer, &QTimer::timeout, this, &NeuroLavaLampWidget::animationLoop);
    is_zero_color_override = false;
    animation_progress = 0;
    
    updateDeviceList();
    
    // Delay the initial connection to ensure other plugins (like Effects) have fully loaded
    QTimer::singleShot(2000, this, [this]() {
        startSseConnection();
        pollApi();
    });
}

NeuroLavaLampWidget::~NeuroLavaLampWidget()
{
    // Restore devices if we are closing while live
    if (is_live) {
        for (const auto& item : device_items) {
            bool has_any_zone = false;
            for (const auto& zone : item.zones) {
                if (zone.checkbox->isChecked()) {
                    has_any_zone = true;
                    break;
                }
            }
            if (has_any_zone && device_backups.contains(item.controller)) {
                restoreDevice(item.controller);
            }
        }
    }
}

void NeuroLavaLampWidget::loadSettings()
{
    json settings = resource_manager->GetSettingsManager()->GetSettings("NeuroLavaLamp");
    
    url_input->blockSignals(true);
    sse_url_input->blockSignals(true);
    offline_interval_input->blockSignals(true);
    live_interval_input->blockSignals(true);
    zero_color_behavior_input->blockSignals(true);
    color_effect_input->blockSignals(true);
    animation_fps_input->blockSignals(true);
    
    if(settings.contains("url") && settings["url"].is_string())
    {
        url_input->setText(QString::fromStdString(settings["url"].get<std::string>()));
    }
    if(settings.contains("sse_url") && settings["sse_url"].is_string())
    {
        sse_url_input->setText(QString::fromStdString(settings["sse_url"].get<std::string>()));
    }
    if (settings.contains("interval") && settings["interval"].is_number()) {
        offline_interval_input->setValue(settings["interval"].get<int>());
    }
    if (settings.contains("live_interval") && settings["live_interval"].is_number()) {
        live_interval_input->setValue(settings["live_interval"].get<int>());
    }
    
    if(settings.contains("zero_color_behavior") && settings["zero_color_behavior"].is_number_integer()) {
        zero_color_behavior_input->setCurrentIndex(settings["zero_color_behavior"].get<int>());
    }
    if(settings.contains("color_effect") && settings["color_effect"].is_number_integer()) {
        color_effect_input->setCurrentIndex(settings["color_effect"].get<int>());
    }
    if(settings.contains("animation_fps") && settings["animation_fps"].is_number_integer()) {
        animation_fps_input->setValue(settings["animation_fps"].get<int>());
    }
    
    url_input->blockSignals(false);
    sse_url_input->blockSignals(false);
    offline_interval_input->blockSignals(false);
    live_interval_input->blockSignals(false);
    zero_color_behavior_input->blockSignals(false);
    color_effect_input->blockSignals(false);
    animation_fps_input->blockSignals(false);
}

void NeuroLavaLampWidget::saveSettings()
{
    json settings;
    settings["url"] = url_input->text().toStdString();
    settings["sse_url"] = sse_url_input->text().toStdString();
    settings["interval"] = offline_interval_input->value();
    settings["live_interval"] = live_interval_input->value();
    
    settings["zero_color_behavior"] = zero_color_behavior_input->currentIndex();
    settings["color_effect"] = color_effect_input->currentIndex();
    settings["animation_fps"] = animation_fps_input->value();
    
    json selected_zones = json::array();
    for (const auto& item : device_items) {
        for (const auto& zone : item.zones) {
            if (zone.checkbox->isChecked()) {
                std::string unique_id = item.controller->name + ":" + item.controller->serial + ":" + std::to_string(zone.zone_idx);
                selected_zones.push_back(unique_id);
            }
        }
    }
    settings["selected_zones"] = selected_zones;
    
    resource_manager->GetSettingsManager()->SetSettings("NeuroLavaLamp", settings);
    resource_manager->GetSettingsManager()->SaveSettings();
}

void NeuroLavaLampWidget::onSettingsChanged()
{
    saveSettings();
    
    // Only restart SSE if the URL actually changed (or if it's dead)
    if (!sse_reply || sse_url_input->text() != sse_reply->url().toString()) {
        startSseConnection();
    }
    
    poll_timer->setInterval(is_live ? live_interval_input->value() : offline_interval_input->value());
    
    // If we're currently live, re-trigger the animation with the new effect/fps settings
    if (is_live) {
        animation_target_color = current_live_color;
        animation_progress = 0;
        animation_duration = 1000;
        
        int effect = color_effect_input->currentIndex();
        if (effect == 0) {
            applyColor(current_live_color);
            animation_timer->stop();
        } else {
            int fps = animation_fps_input->value();
            int interval = 1000 / fps;
            animation_timer->start(interval);
        }
    }
}

void NeuroLavaLampWidget::updateDeviceList()
{
    std::vector<RGBController*>& controllers = resource_manager->GetRGBControllers();
    
    json settings = resource_manager->GetSettingsManager()->GetSettings("NeuroLavaLamp");
    std::vector<std::string> saved_zones;
    if (settings.contains("selected_zones") && settings["selected_zones"].is_array()) {
        for (auto& z : settings["selected_zones"]) {
            if (z.is_string()) saved_zones.push_back(z.get<std::string>());
        }
    }
    
    for (RGBController* dev : controllers) {
        QGroupBox* dev_box = new QGroupBox();
        QVBoxLayout* dev_layout = new QVBoxLayout(dev_box);
        
        QCheckBox* dev_cb = new QCheckBox(QString::fromStdString(dev->name));
        dev_layout->addWidget(dev_cb);
        
        // Add left margin for zones
        QVBoxLayout* zones_layout = new QVBoxLayout();
        zones_layout->setContentsMargins(20, 0, 0, 0);
        
        DeviceItem item;
        item.controller = dev;
        item.device_checkbox = dev_cb;
        
        bool any_zone_checked = false;
        
        for (size_t z = 0; z < dev->zones.size(); z++) {
            QCheckBox* zone_cb = new QCheckBox(QString::fromStdString(dev->zones[z].name));
            zones_layout->addWidget(zone_cb);
            
            ZoneItem z_item;
            z_item.zone_idx = z;
            z_item.checkbox = zone_cb;
            
            std::string unique_id = dev->name + ":" + dev->serial + ":" + std::to_string(z);
            if (std::find(saved_zones.begin(), saved_zones.end(), unique_id) != saved_zones.end()) {
                zone_cb->setChecked(true);
                any_zone_checked = true;
            }
            
            connect(zone_cb, &QCheckBox::toggled, this, [this, dev](bool) {
                this->onZoneCheckboxToggled(dev);
            });
            
            item.zones.push_back(z_item);
        }
        
        if (any_zone_checked) {
            dev_cb->setChecked(true);
        }
        
        dev_layout->addLayout(zones_layout);
        device_list_layout->addWidget(dev_box);
        device_items.push_back(item);
        
        // Connect device checkbox to toggle all zones
        connect(dev_cb, &QCheckBox::toggled, this, [this, dev](bool checked) {
            this->onDeviceCheckboxToggled(checked, dev);
        });
    }
    
    device_list_layout->addStretch();
}

void NeuroLavaLampWidget::onDeviceCheckboxToggled(bool checked, RGBController* dev)
{
    for (auto& item : device_items) {
        if (item.controller == dev) {
            for (auto& zone : item.zones) {
                zone.checkbox->blockSignals(true);
                zone.checkbox->setChecked(checked);
                zone.checkbox->blockSignals(false);
            }
            break;
        }
    }
    saveSettings();
    if (is_live) {
        refreshDeviceLiveState(dev);
    }
}

void NeuroLavaLampWidget::onZoneCheckboxToggled(RGBController* dev)
{
    saveSettings();
    if (is_live) {
        refreshDeviceLiveState(dev);
    }
}

void NeuroLavaLampWidget::onSelectAllClicked()
{
    for (auto& item : device_items) {
        item.device_checkbox->setChecked(true);
    }
}

void NeuroLavaLampWidget::refreshDeviceLiveState(RGBController* dev)
{
    // Find the device item
    DeviceItem* target_item = nullptr;
    for (auto& item : device_items) {
        if (item.controller == dev) {
            target_item = &item;
            break;
        }
    }
    
    if (!target_item) return;

    // Determine if it has ANY selected zones
    bool has_selected = false;
    for (const auto& zone : target_item->zones) {
        if (zone.checkbox->isChecked()) {
            has_selected = true;
            break;
        }
    }

    // Temporarily "restore" the device to un-do the old UI automation and hardware state
    if (device_backups.contains(dev)) {
        setEffectsPluginDeviceState(*target_item, true); // True = restoring checkmarks
        restoreDevice(dev);
    }

    // If it STILL has selected zones after the toggle, re-enter live state
    if (has_selected) {
        backupDevice(dev);
        setEffectsPluginDeviceState(*target_item, false); // False = hiding checkmarks
        
        // Immediately apply the current live color to prevent it being stuck in the backup state
        dev->SetCustomMode();
        for (const auto& zone : target_item->zones) {
            if (zone.checkbox->isChecked()) {
                dev->SetAllZoneLEDs(zone.zone_idx, current_live_color);
            }
        }
        dev->UpdateLEDs();
    }
}

void NeuroLavaLampWidget::pollApi()
{
    // Check if SSE is alive. If not, try to restart it.
    if (!sse_reply || sse_reply->error() != QNetworkReply::NoError || !sse_reply->isOpen()) {
        startSseConnection();
        
        // Fallback polling request while SSE is down
        QString baseUrl = url_input->text();
        QUrl url(baseUrl);
        QNetworkRequest request(url);
        network_manager->get(request);
    }

    if (is_live) {
        for (auto& item : device_items) {
            bool has_any_zone = false;
            for (const auto& zone : item.zones) {
                if (zone.checkbox->isChecked()) {
                    has_any_zone = true;
                    break;
                }
            }
            if (has_any_zone) {
                setEffectsPluginDeviceState(item, false);
            }
        }
    }
}

void NeuroLavaLampWidget::onNetworkReply(QNetworkReply* reply)
{
    if (reply == sse_reply) {
        return; // Handled by SSE readyRead and finished slots
    }

    if (reply->error() != QNetworkReply::NoError) {
        status_label->setText("Neuro Lava Lamp: Network Error");
        reply->deleteLater();
        return;
    }

    QByteArray response_data = reply->readAll();
    reply->deleteLater();

    current_connection_mode = "HTTP Polling";
    processEventData(response_data);
}

void NeuroLavaLampWidget::processEventData(const QByteArray& data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }

    QJsonObject obj = doc.object();
    bool new_live = obj["live"].toBool();
    
    if (new_live) {
        QJsonArray rgb_arr = obj["rgb"].toArray();
        if (rgb_arr.size() == 3) {
            int r = rgb_arr[0].toInt();
            int g = rgb_arr[1].toInt();
            int b = rgb_arr[2].toInt();
            RGBColor color = ToRGBColor(r, g, b);
            
            // Check for zero-color behavior
            if (r == 0 && g == 0 && b == 0) {
                int zcb = zero_color_behavior_input->currentIndex();
                if (zcb == 1) { // Keep previous color
                    return; // Ignore this update completely
                } else if (zcb == 2) { // Restore background effects
                    if (!is_zero_color_override && is_live) {
                        is_zero_color_override = true;
                        // Temporarily restore device effects
                        for (const auto& item : device_items) {
                            bool has_selected = false;
                            for (const auto& zone : item.zones) {
                                if (zone.checkbox->isChecked()) {
                                    has_selected = true;
                                    break;
                                }
                            }
                            if (has_selected && device_backups.contains(item.controller)) {
                                setEffectsPluginDeviceState(item, true);
                                restoreDevice(item.controller);
                            }
                        }
                    }
                    return;
                }
            } else {
                if (is_zero_color_override) {
                    is_zero_color_override = false;
                    // Re-disable effects
                    for (const auto& item : device_items) {
                        bool has_selected = false;
                        for (const auto& zone : item.zones) {
                            if (zone.checkbox->isChecked()) {
                                has_selected = true;
                                break;
                            }
                        }
                        if (has_selected) {
                            backupDevice(item.controller);
                            setEffectsPluginDeviceState(item, false);
                        }
                    }
                }
            }
            
            bool color_changed = (current_live_color != color);
            
            animation_start_color = current_live_color;
            current_live_color = color;
            
            QString modeStr = current_connection_mode.isEmpty() ? "Unknown" : current_connection_mode;
            status_label->setText(QString("Neuro Lava Lamp: Live [%1] (RGB: %2, %3, %4)").arg(modeStr).arg(r).arg(g).arg(b));
            
            if (!is_live) {
                // Transition to live: backup state
                is_live = true;
                poll_timer->setInterval(live_interval_input->value());
                for (const auto& item : device_items) {
                    bool has_selected = false;
                    for (const auto& zone : item.zones) {
                        if (zone.checkbox->isChecked()) {
                            has_selected = true;
                            break;
                        }
                    }
                    if (has_selected) {
                        backupDevice(item.controller);
                        setEffectsPluginDeviceState(item, false);
                    }
                }
            }
            
            // Trigger animation or apply instantly
            if (color_changed || (!is_live && new_live)) {
                animation_target_color = color;
                animation_progress = 0;
                animation_duration = 1000; // 1 second duration
                
                int effect = color_effect_input->currentIndex();
                if (effect == 0) {
                    applyColor(color);
                    animation_timer->stop();
                } else {
                    int fps = animation_fps_input->value();
                    int interval = 1000 / fps;
                    animation_timer->start(interval);
                }
            }
        }
    } else {
        QString modeStr = current_connection_mode.isEmpty() ? "Unknown" : current_connection_mode;
        status_label->setText(QString("Neuro Lava Lamp: Offline [%1]").arg(modeStr));
        if (is_live) {
            // Transition to offline: restore state
            is_live = false;
            poll_timer->setInterval(offline_interval_input->value());
            for (const auto& item : device_items) {
                bool has_selected = false;
                for (const auto& zone : item.zones) {
                    if (zone.checkbox->isChecked()) {
                        has_selected = true;
                        break;
                    }
                }
                if (has_selected && device_backups.contains(item.controller)) {
                    setEffectsPluginDeviceState(item, true);
                    restoreDevice(item.controller);
                }
            }
        }
    }
}

void NeuroLavaLampWidget::startSseConnection()
{
    if (sse_reply) {
        sse_reply->abort();
        sse_reply->deleteLater();
        sse_reply = nullptr;
    }
    sse_buffer.clear();

    QString baseUrl = sse_url_input->text();

    QUrl url(baseUrl);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "text/event-stream");
    
    sse_reply = network_manager->get(request);
    connect(sse_reply, &QNetworkReply::readyRead, this, &NeuroLavaLampWidget::onSseReadyRead);
    connect(sse_reply, &QNetworkReply::finished, this, &NeuroLavaLampWidget::onSseFinished);
}

void NeuroLavaLampWidget::onSseReadyRead()
{
    if (!sse_reply) return;
    sse_buffer.append(sse_reply->readAll());

    int index = -1;
    while ((index = sse_buffer.indexOf("\n\n")) != -1) {
        QByteArray event = sse_buffer.left(index);
        sse_buffer.remove(0, index + 2);

        QList<QByteArray> lines = event.split('\n');
        for (const QByteArray& line : lines) {
            if (line.startsWith("data: ")) {
                current_connection_mode = "SSE Stream";
                processEventData(line.mid(6));
            } else if (line.startsWith("data:")) {
                current_connection_mode = "SSE Stream";
                processEventData(line.mid(5));
            }
        }
    }
}

void NeuroLavaLampWidget::onSseFinished()
{
    if (sse_reply) {
        sse_reply->deleteLater();
        sse_reply = nullptr;
    }
}

void NeuroLavaLampWidget::setEffectsPluginDeviceState(const DeviceItem& item, bool restoring)
{
    QString deviceName = QString::fromStdString(item.controller->name);
    QTabWidget* effectTabs = nullptr;
    QWidgetList allWidgets = QApplication::allWidgets();
    for (QWidget* widget : allWidgets) {
        if (widget->objectName() == "EffectTabs" && QString(widget->metaObject()->className()) == "QTabWidget") {
            effectTabs = qobject_cast<QTabWidget*>(widget);
            break;
        }
    }
    
    if (!effectTabs) return;

    int userIndex = effectTabs->currentIndex();

    for (int i = 1; i < effectTabs->count(); ++i) {
        effectTabs->setCurrentIndex(i);
        
        for (QWidget* widget : allWidgets) {
            if (QString(widget->metaObject()->className()) == "DeviceListItem") {
                QLabel* nameLabel = widget->findChild<QLabel*>("device_name");
                if (nameLabel && nameLabel->text() == deviceName) {
                    
                    QList<QWidget*> zoneWidgets;
                    for (QWidget* child : widget->findChildren<QWidget*>()) {
                        if (QString(child->metaObject()->className()) == "ZoneListItem") {
                            zoneWidgets.append(child);
                        }
                    }

                    if (zoneWidgets.isEmpty()) {
                        bool anySelected = false;
                        for (const ZoneItem& zItem : item.zones) {
                            if (restoring || zItem.checkbox->isChecked()) {
                                anySelected = true;
                                break;
                            }
                        }
                        
                        if (anySelected) {
                            QToolButton* enableBtn = widget->findChild<QToolButton*>("enable");
                            if (enableBtn) {
                                QString zoneKey = "GLOBAL";
                                if (!restoring) {
                                    if (enableBtn->isChecked()) {
                                        original_effects_state[deviceName][i][zoneKey] = true;
                                        enableBtn->setChecked(false);
                                    } else if (!original_effects_state[deviceName][i].contains(zoneKey)) {
                                        original_effects_state[deviceName][i][zoneKey] = false;
                                    }
                                } else {
                                    if (original_effects_state.contains(deviceName) && 
                                        original_effects_state[deviceName].contains(i) && 
                                        original_effects_state[deviceName][i].contains(zoneKey) && 
                                        original_effects_state[deviceName][i][zoneKey]) {
                                        enableBtn->setChecked(true);
                                    }
                                }
                            }
                        }
                    } else {
                        for (QWidget* zoneWidget : zoneWidgets) {
                            QLabel* zoneNameLabel = zoneWidget->findChild<QLabel*>("zone_name");
                            if (zoneNameLabel) {
                                QString zName = zoneNameLabel->text();
                                bool matches = false;
                                for (const ZoneItem& zItem : item.zones) {
                                    if (!restoring && !zItem.checkbox->isChecked()) continue;
                                    int zIdx = zItem.zone_idx;
                                    QString expectedPrefix = QString::fromStdString("• " + item.controller->name + ": " + item.controller->zones[zIdx].name);
                                    if (zName.startsWith(expectedPrefix)) {
                                        matches = true;
                                        break;
                                    }
                                }
                                
                                if (matches) {
                                    QToolButton* enableBtn = zoneWidget->findChild<QToolButton*>("enable");
                                    if (enableBtn) {
                                        QString zoneKey = zName;
                                        if (!restoring) {
                                            if (enableBtn->isChecked()) {
                                                original_effects_state[deviceName][i][zoneKey] = true;
                                                enableBtn->setChecked(false);
                                            } else if (!original_effects_state[deviceName][i].contains(zoneKey)) {
                                                original_effects_state[deviceName][i][zoneKey] = false;
                                            }
                                        } else {
                                            if (original_effects_state.contains(deviceName) && 
                                                original_effects_state[deviceName].contains(i) && 
                                                original_effects_state[deviceName][i].contains(zoneKey) && 
                                                original_effects_state[deviceName][i][zoneKey]) {
                                                enableBtn->setChecked(true);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (restoring) {
        original_effects_state.remove(deviceName);
    }

    effectTabs->setCurrentIndex(userIndex);
}

void NeuroLavaLampWidget::backupDevice(RGBController* dev)
{
    DeviceStateBackup backup;
    backup.active_mode = dev->active_mode;
    backup.colors = dev->colors;
    device_backups.insert(dev, backup);
}

void NeuroLavaLampWidget::restoreDevice(RGBController* dev)
{
    DeviceStateBackup backup = device_backups[dev];
    
    dev->SetMode(backup.active_mode);
    dev->UpdateMode();
    
    for (size_t i = 0; i < backup.colors.size() && i < dev->colors.size(); i++) {
        dev->colors[i] = backup.colors[i];
    }
    
    dev->UpdateLEDs();
    device_backups.remove(dev);
}

void NeuroLavaLampWidget::applyColor(RGBColor color, bool instant)
{
    for (const auto& item : device_items) {
        bool has_selected = false;
        
        for (const auto& zone : item.zones) {
            if (zone.checkbox->isChecked()) {
                if (!has_selected) {
                    // Only set mode to custom once per device
                    item.controller->SetCustomMode();
                    has_selected = true;
                }
                item.controller->SetAllZoneLEDs(zone.zone_idx, color);
            }
        }
        
        if (has_selected) {
            item.controller->UpdateLEDs();
        }
    }
}

void NeuroLavaLampWidget::animationLoop()
{
    if (!is_live || is_zero_color_override) {
        animation_timer->stop();
        return;
    }
    
    int fps = animation_fps_input->value();
    int interval = 1000 / fps;
    animation_progress += interval;
    
    if (animation_progress >= animation_duration) {
        animation_progress = animation_duration;
        animation_timer->stop();
    }
    
    float t = (float)animation_progress / animation_duration;
    int effect = color_effect_input->currentIndex();
    
    RGBColor base_start = animation_start_color;
    RGBColor base_target = animation_target_color;
    
    if (effect == 3) { // Pulse
        if (t < 0.5f) {
            base_target = ToRGBColor(0,0,0);
            t = t * 2.0f;
        } else {
            base_start = ToRGBColor(0,0,0);
            t = (t - 0.5f) * 2.0f;
        }
    } else if (effect == 4) { // Flash
        if (t < 0.2f) {
            base_target = ToRGBColor(255,255,255);
            t = t * 5.0f;
        } else {
            base_start = ToRGBColor(255,255,255);
            t = (t - 0.2f) * 1.25f;
        }
    }
    
    for (const auto& item : device_items) {
        bool has_selected = false;
        
        for (const auto& zone : item.zones) {
            if (zone.checkbox->isChecked()) {
                if (!has_selected) {
                    item.controller->SetCustomMode();
                    has_selected = true;
                }
                
                unsigned char sr = RGBGetRValue(base_start);
                unsigned char sg = RGBGetGValue(base_start);
                unsigned char sb = RGBGetBValue(base_start);
                
                unsigned char tr = RGBGetRValue(base_target);
                unsigned char tg = RGBGetGValue(base_target);
                unsigned char tb = RGBGetBValue(base_target);
                
                if (effect == 2) { // Wave (per-LED)
                    const ::zone* dev_zone = &item.controller->zones[zone.zone_idx];
                    for (unsigned int led_idx = 0; led_idx < dev_zone->leds_count; led_idx++) {
                        float led_pos = 0.0f;
                        if (dev_zone->leds_count > 1) {
                            led_pos = (float)led_idx / (float)(dev_zone->leds_count - 1);
                        }
                        
                        // Sharp wave front moving across the zone
                        float blend_width = 0.15f; // 15% of the zone width is blending
                        float W = -blend_width + t * (1.0f + 2.0f * blend_width);
                        float led_t = (W - led_pos) / blend_width + 0.5f;
                        
                        if (led_t < 0.0f) led_t = 0.0f;
                        if (led_t > 1.0f) led_t = 1.0f;
                        
                        unsigned char r = sr + (tr - sr) * led_t;
                        unsigned char g = sg + (tg - sg) * led_t;
                        unsigned char b = sb + (tb - sb) * led_t;
                        
                        dev_zone->colors[led_idx] = ToRGBColor(r,g,b);
                    }
                } else {
                    float zone_t = t;
                    unsigned char r = sr + (tr - sr) * zone_t;
                    unsigned char g = sg + (tg - sg) * zone_t;
                    unsigned char b = sb + (tb - sb) * zone_t;
                    
                    item.controller->SetAllZoneLEDs(zone.zone_idx, ToRGBColor(r,g,b));
                }
            }
        }
        
        if (has_selected) {
            item.controller->UpdateLEDs();
        }
    }
}
