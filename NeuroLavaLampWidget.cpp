#include "NeuroLavaLampWidget.h"
#include <set>
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

NeuroLavaLampWidget::NeuroLavaLampWidget(OpenRGBPluginAPIInterface* api, QWidget *parent)
    : QWidget(parent)
    , plugin_api(api)
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
    schedule_url_input = new QLineEdit("https://schedule-api.nwero.net/schedule");
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
    settings_layout->addRow("Schedule API URL:", schedule_url_input);
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
    
    disable_evil_input = new QCheckBox("Disable Evil Takeover");
    disable_evil_input->setToolTip("Prevents the plugin from applying colors if the schedule indicates it is an Evil-only stream.");
    
    extra_layout->addRow("Plasma Globe Rule:", disable_evil_input);
    extra_layout->addRow(zero_color_label, zero_color_behavior_input);
    extra_layout->addRow("Color Effect:", color_effect_input);
    extra_layout->addRow("Animation FPS:", animation_fps_input);
    
    left_layout->addWidget(extra_group);

    connect(url_input, &QLineEdit::editingFinished, this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(sse_url_input, &QLineEdit::editingFinished, this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(schedule_url_input, &QLineEdit::editingFinished, this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(offline_interval_input, QOverload<int>::of(&QSpinBox::valueChanged), this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(live_interval_input, QOverload<int>::of(&QSpinBox::valueChanged), this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(zero_color_behavior_input, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(color_effect_input, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(animation_fps_input, QOverload<int>::of(&QSpinBox::valueChanged), this, &NeuroLavaLampWidget::onSettingsChanged);
    connect(disable_evil_input, &QCheckBox::toggled, this, &NeuroLavaLampWidget::onSettingsChanged);
    
    // Status Labels
    status_label = new QLabel("Neuro Lava Lamp: Status Unknown");
    schedule_status_label = new QLabel("Schedule: Not yet fetched");
    left_layout->addWidget(status_label);
    left_layout->addWidget(schedule_status_label);
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
    
    schedule_network_manager = new QNetworkAccessManager(this);
    connect(schedule_network_manager, &QNetworkAccessManager::finished, this, &NeuroLavaLampWidget::onScheduleReply);

    poll_timer = new QTimer(this);
    connect(poll_timer, &QTimer::timeout, this, &NeuroLavaLampWidget::pollApi);
    poll_timer->start(offline_interval_input->value());
    
    animation_timer = new QTimer(this);
    connect(animation_timer, &QTimer::timeout, this, &NeuroLavaLampWidget::animationLoop);
    is_zero_color_override = false;
    animation_progress = 0;
    
    is_evil_only_stream = false;
    schedule_timer = new QTimer(this);
    connect(schedule_timer, &QTimer::timeout, this, &NeuroLavaLampWidget::fetchSchedule);
    schedule_timer->start(3600000); // Poll every 1 hour when offline
    
    updateDeviceList();
    
    // Delay the initial connection to ensure other plugins (like Effects) have fully loaded
    QTimer::singleShot(2000, this, [this]() {
        fetchSchedule();
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
    json settings = plugin_api->GetSettings("NeuroLavaLamp");
    
    url_input->blockSignals(true);
    sse_url_input->blockSignals(true);
    schedule_url_input->blockSignals(true);
    offline_interval_input->blockSignals(true);
    live_interval_input->blockSignals(true);
    zero_color_behavior_input->blockSignals(true);
    color_effect_input->blockSignals(true);
    animation_fps_input->blockSignals(true);
    disable_evil_input->blockSignals(true);
    
    if(settings.contains("url") && settings["url"].is_string())
    {
        url_input->setText(QString::fromStdString(settings["url"].get<std::string>()));
    }
    if(settings.contains("sse_url") && settings["sse_url"].is_string())
    {
        sse_url_input->setText(QString::fromStdString(settings["sse_url"].get<std::string>()));
    }
    if(settings.contains("schedule_url") && settings["schedule_url"].is_string())
    {
        schedule_url_input->setText(QString::fromStdString(settings["schedule_url"].get<std::string>()));
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
    if(settings.contains("disable_evil") && settings["disable_evil"].is_boolean()) {
        disable_evil_input->setChecked(settings["disable_evil"].get<bool>());
    }
    
    url_input->blockSignals(false);
    sse_url_input->blockSignals(false);
    schedule_url_input->blockSignals(false);
    offline_interval_input->blockSignals(false);
    live_interval_input->blockSignals(false);
    zero_color_behavior_input->blockSignals(false);
    color_effect_input->blockSignals(false);
    animation_fps_input->blockSignals(false);
    disable_evil_input->blockSignals(false);
}

void NeuroLavaLampWidget::saveSettings()
{
    json settings;
    settings["url"] = url_input->text().toStdString();
    settings["sse_url"] = sse_url_input->text().toStdString();
    settings["schedule_url"] = schedule_url_input->text().toStdString();
    settings["interval"] = offline_interval_input->value();
    settings["live_interval"] = live_interval_input->value();
    
    settings["zero_color_behavior"] = zero_color_behavior_input->currentIndex();
    settings["color_effect"] = color_effect_input->currentIndex();
    settings["animation_fps"] = animation_fps_input->value();
    settings["disable_evil"] = disable_evil_input->isChecked();
    
    json selected_zones = json::array();
    for (const auto& item : device_items) {
        for (const auto& zone : item.zones) {
            if (zone.checkbox->isChecked()) {
                std::string unique_id = item.controller->GetName() + ":" + item.controller->GetSerial() + ":" + std::to_string(zone.zone_idx);
                selected_zones.push_back(unique_id);
            }
        }
    }
    settings["selected_zones"] = selected_zones;
    
    plugin_api->SetSettings("NeuroLavaLamp", settings);
    plugin_api->SaveSettings();
}

void NeuroLavaLampWidget::onSettingsChanged()
{
    saveSettings();
    
    // Only restart SSE if the URL actually changed (or if it's dead)
    if (!sse_reply || sse_url_input->text() != sse_reply->url().toString()) {
        startSseConnection();
    }
    
    poll_timer->setInterval(is_live ? live_interval_input->value() : offline_interval_input->value());
    
    // Evaluate if the toggle state requires us to re-process the last event to seize/drop control instantly
    if (!last_raw_event_data.isEmpty()) {
        processEventData(last_raw_event_data);
    } else {
        // If we're currently live (and no event data exists for some reason), re-trigger the animation
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
}

void NeuroLavaLampWidget::updateDeviceList()
{
    json settings = plugin_api->GetSettings("NeuroLavaLamp");
    std::vector<std::string> saved_zones;
    if (settings.contains("selected_zones") && settings["selected_zones"].is_array()) {
        for (auto& z : settings["selected_zones"]) {
            if (z.is_string()) saved_zones.push_back(z.get<std::string>());
        }
    }

    rebuildDeviceList(saved_zones);
}

void NeuroLavaLampWidget::rebuildDeviceList(const std::vector<std::string>& initially_checked_zones)
{
    /*-----------------------------------------------------*\
    | Clear the current list (widgets and bookkeeping)      |
    \*-----------------------------------------------------*/
    while (device_list_layout->count() > 0) {
        QLayoutItem* layout_item = device_list_layout->takeAt(0);

        if (layout_item->widget()) {
            layout_item->widget()->deleteLater();
        }

        delete layout_item;
    }

    device_items.clear();

    std::vector<RGBControllerInterface*> controllers = plugin_api->GetRGBControllers();

    for (RGBControllerInterface* dev : controllers) {
        /*-------------------------------------------------*\
        | Skip hidden devices. The VisualMap plugin hides   |
        | its member devices while it aggregates them into  |
        | a virtual controller; those members are owned by  |
        | the map and must not be selectable here (Select   |
        | All included).                                    |
        \*-------------------------------------------------*/
        if (dev->GetHidden()) {
            continue;
        }

        QGroupBox* dev_box = new QGroupBox();
        QVBoxLayout* dev_layout = new QVBoxLayout(dev_box);
        
        QCheckBox* dev_cb = new QCheckBox(QString::fromStdString(dev->GetName()));
        dev_layout->addWidget(dev_cb);
        
        // Add left margin for zones
        QVBoxLayout* zones_layout = new QVBoxLayout();
        zones_layout->setContentsMargins(20, 0, 0, 0);
        
        DeviceItem item;
        item.controller = dev;
        item.device_checkbox = dev_cb;
        
        bool any_zone_checked = false;
        
        for (size_t z = 0; z < dev->GetZoneCount(); z++) {
            QCheckBox* zone_cb = new QCheckBox(QString::fromStdString(dev->GetZoneName(z)));
            zones_layout->addWidget(zone_cb);
            
            ZoneItem z_item;
            z_item.zone_idx = z;
            z_item.checkbox = zone_cb;
            
            std::string unique_id = dev->GetName() + ":" + dev->GetSerial() + ":" + std::to_string(z);
            if (std::find(initially_checked_zones.begin(), initially_checked_zones.end(), unique_id) != initially_checked_zones.end()) {
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

void NeuroLavaLampWidget::onDeviceCheckboxToggled(bool checked, RGBControllerInterface* dev)
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

void NeuroLavaLampWidget::onZoneCheckboxToggled(RGBControllerInterface* dev)
{
    saveSettings();
    if (is_live) {
        refreshDeviceLiveState(dev);
    }
}

void NeuroLavaLampWidget::onSelectAllClicked()
{
    /*-----------------------------------------------------*\
    | Only devices currently in the list are selectable,   |
    | and hidden devices (e.g. VisualMap members) never    |
    | make it into the list, so they can't be selected     |
    | here.                                                |
    \*-----------------------------------------------------*/
    for (auto& item : device_items) {
        item.device_checkbox->setChecked(true);
    }
}

void NeuroLavaLampWidget::refreshDeviceList()
{
    std::vector<RGBControllerInterface*> controllers = plugin_api->GetRGBControllers();

    /*-----------------------------------------------------*\
    | Nothing to do if the set of visible devices matches  |
    | what we already show.                                 |
    \*-----------------------------------------------------*/
    bool changed = false;

    for (const auto& item : device_items) {
        if (item.controller->GetHidden()) {
            // A listed device got hidden (e.g. by VisualMap taking it over)
            changed = true;
            break;
        }
    }

    if (!changed) {
        std::set<RGBControllerInterface*> listed;
        for (const auto& item : device_items) {
            listed.insert(item.controller);
        }

        for (RGBControllerInterface* dev : controllers) {
            if (!dev->GetHidden() && !listed.count(dev)) {
                // A new visible device appeared, or one of ours got unhidden
                changed = true;
                break;
            }
        }
    }

    if (!changed) {
        return;
    }

    /*-----------------------------------------------------*\
    | Release control of any selected devices that are     |
    | about to disappear (they became hidden), so the      |
    | plugin that hid them can own their LEDs cleanly.     |
    \*-----------------------------------------------------*/
    for (auto& item : device_items) {
        if (!item.controller->GetHidden()) {
            continue;
        }

        bool has_selected = false;
        for (const auto& zone : item.zones) {
            if (zone.checkbox->isChecked()) {
                has_selected = true;
                break;
            }
        }

        if (has_selected && device_backups.contains(item.controller)) {
            setEffectsPluginDeviceState(item, true); // Re-enable the effects plugin state
            restoreDevice(item.controller);
        }
    }

    /*-----------------------------------------------------*\
    | Preserve the current selection of devices that stay  |
    | visible; hidden ones drop out (and with them their   |
    | saved selection).                                    |
    \*-----------------------------------------------------*/
    std::vector<std::string> preserved_zones;
    for (const auto& item : device_items) {
        if (item.controller->GetHidden()) {
            continue;
        }

        for (const auto& zone : item.zones) {
            if (zone.checkbox->isChecked()) {
                preserved_zones.push_back(item.controller->GetName() + ":" + item.controller->GetSerial() + ":" + std::to_string(zone.zone_idx));
            }
        }
    }

    rebuildDeviceList(preserved_zones);

    // Persist the pruned selection so hidden devices don't come back selected on restart
    saveSettings();
}

void NeuroLavaLampWidget::refreshDeviceLiveState(RGBControllerInterface* dev)
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
                dev->SetAllZoneColors(zone.zone_idx, current_live_color);
            }
        }
        dev->UpdateLEDs();
    }
}

void NeuroLavaLampWidget::pollApi()
{
    /*-----------------------------------------------------*\
    | Keep the device list in sync with visibility changes  |
    | (e.g. VisualMap hiding/unhiding member devices).      |
    | Cheap no-op unless something actually changed.        |
    \*-----------------------------------------------------*/
    refreshDeviceList();

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
                // If it's a normal live stream (not disabled by rule), keep effects off
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

void NeuroLavaLampWidget::fetchSchedule()
{
    QString schedule_url = schedule_url_input->text();
    if (schedule_url.isEmpty()) return;
    
    QNetworkRequest request((QUrl(schedule_url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Accept", "application/json");
    schedule_network_manager->get(request);
}

void NeuroLavaLampWidget::onScheduleReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        schedule_status_label->setText(QString("Schedule: Fetch failed — %1").arg(reply->errorString()));
        return;
    }
    
    QByteArray response_data = reply->readAll();
    
    try {
        json j = json::parse(response_data.toStdString());
        if (!j.is_array()) {
            schedule_status_label->setText("Schedule: Fetch failed (invalid response)");
            return;
        }
        
        QDateTime now = QDateTime::currentDateTimeUtc();
        QDateTime now_plus_margin = now.addSecs(3600 * 2); // 2 hours leeway for early starts
        QDateTime now_minus_margin = now.addSecs(-3600 * 18); // 18 hours limit so we don't pick a stream from last week
        
        bool found_stream = false;
        json last_valid_stream;
        
        for (const auto& item : j) {
            if (!item.contains("timestamp") || !item["timestamp"].is_string()) continue;
            
            QString timestamp_str = QString::fromStdString(item["timestamp"].get<std::string>());
            // Qt::ISODate parses ISO 8601 with timezone info (the Z suffix) correctly
            QDateTime dt = QDateTime::fromString(timestamp_str, Qt::ISODate).toUTC();
            if (!dt.isValid()) continue;
            
            // Only consider it the "current" stream if it's within the recent window
            if (dt <= now_plus_margin && dt >= now_minus_margin) {
                last_valid_stream = item;
                found_stream = true;
            }
        }
        
        is_evil_only_stream = false;
        
        if (found_stream && last_valid_stream.contains("live") && last_valid_stream["live"].is_boolean() && last_valid_stream["live"].get<bool>()) {
            if (last_valid_stream.contains("streamers") && last_valid_stream["streamers"].is_array()) {
                bool has_evil = false;
                bool has_neuro = false;
                
                for (const auto& streamer : last_valid_stream["streamers"]) {
                    if (streamer.is_string()) {
                        std::string name = streamer.get<std::string>();
                        if (name == "Evil") has_evil = true;
                        if (name == "Neuro") has_neuro = true;
                    }
                }
                
                std::string title = last_valid_stream.contains("title") ? last_valid_stream["title"].get<std::string>() : "Unknown";
                
                if (has_evil && !has_neuro) {
                    is_evil_only_stream = true;
                    schedule_status_label->setText(QString("Schedule: Evil-only stream detected (\"%1\")").arg(QString::fromStdString(title)));
                } else {
                    schedule_status_label->setText(QString("Schedule: Neuro stream (\"%1\")").arg(QString::fromStdString(title)));
                }
            } else {
                schedule_status_label->setText("Schedule: Live stream with no streamer data");
            }
        } else if (found_stream) {
            schedule_status_label->setText("Schedule: No live stream scheduled");
        } else {
            schedule_status_label->setText("Schedule: No recent stream found in schedule");
        }
    } catch (...) {
        schedule_status_label->setText("Schedule: Parse error");
    }
}

void NeuroLavaLampWidget::processEventData(const QByteArray& data)
{
    last_raw_event_data = data;
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }

    QJsonObject obj = doc.object();
    bool new_live = obj["live"].toBool();
    
    int r = 0, g = 0, b = 0;
    bool has_color = false;
    
    if (new_live) {
        QJsonArray rgb_arr = obj["rgb"].toArray();
        if (rgb_arr.size() == 3) {
            r = rgb_arr[0].toInt();
            g = rgb_arr[1].toInt();
            b = rgb_arr[2].toInt();
            has_color = true;
        }
    }

    if (disable_evil_input->isChecked() && is_evil_only_stream) {
        if (has_color) {
            status_label->setText(QString("Neuro Lava Lamp: Disabled (Plasma Globe rule active) (RGB: %1, %2, %3)").arg(r).arg(g).arg(b));
            current_live_color = ToRGBColor(r, g, b);
        } else {
            status_label->setText("Neuro Lava Lamp: Disabled (Plasma Globe rule active)");
        }
        
        if (is_live) {
            is_live = false;
            animation_timer->stop();
            poll_timer->setInterval(offline_interval_input->value());
            schedule_timer->setInterval(3600000);
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
    
    if (new_live) {
        if (has_color) {
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
            
            bool transitioned_to_live = false;
            if (!is_live) {
                // Transition to live: backup state
                is_live = true;
                transitioned_to_live = true;
                poll_timer->setInterval(live_interval_input->value());
                schedule_timer->setInterval(1800000); // Poll schedule every 30 mins while live
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
            if (color_changed || transitioned_to_live) {
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
            schedule_timer->setInterval(3600000); // Poll schedule every 1 hour while offline
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
    /*-----------------------------------------------------*\
    | Zones of this device that we are taking control over  |
    | (all zones when restoring so every previously         |
    | disabled effect can be re-enabled)                    |
    \*-----------------------------------------------------*/
    std::vector<int> selected_zones;

    for (const auto& zone : item.zones) {
        if (restoring || zone.checkbox->isChecked()) {
            selected_zones.push_back(zone.zone_idx);
        }
    }

    applyEffectsPluginState(item.controller, selected_zones, restoring);

    /*-----------------------------------------------------*\
    | Also handle controllers that share physical LEDs with |
    | this one. A VisualMap virtual controller aggregates   |
    | its member devices: an effect running on it keeps     |
    | writing to every member (and vice versa when we       |
    | control the virtual controller itself), which fights  |
    | our color updates and causes flickering.              |
    \*-----------------------------------------------------*/
    for (RGBControllerInterface* other : findConflictingControllers(item.controller)) {
        std::vector<int> all_zones;

        for (unsigned int z = 0; z < other->GetZoneCount(); ++z) {
            all_zones.push_back(z);
        }

        applyEffectsPluginState(other, all_zones, restoring);
    }
}

void NeuroLavaLampWidget::applyEffectsPluginState(RGBControllerInterface* controller, const std::vector<int>& selected_zones, bool restoring)
{
    if (selected_zones.empty()) {
        return;
    }

    QString deviceName = QString::fromStdString(controller->GetName());
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
                    } else {
                        for (QWidget* zoneWidget : zoneWidgets) {
                            QLabel* zoneNameLabel = zoneWidget->findChild<QLabel*>("zone_name");
                            if (zoneNameLabel) {
                                QString zName = zoneNameLabel->text();
                                bool matches = false;
                                for (int zIdx : selected_zones) {
                                    QString expectedPrefix = QString::fromStdString("• " + controller->GetName() + ": " + controller->GetZoneName(zIdx));
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

bool NeuroLavaLampWidget::sharesPhysicalLEDs(RGBControllerInterface* a, RGBControllerInterface* b)
{
    if (!a || !b || a == b) {
        return false;
    }

    /*-----------------------------------------------------*\
    | Only virtual controllers aggregate other devices'     |
    | LEDs (e.g. the VisualMap controller). Two non-virtual |
    | controllers never drive the same physical LEDs, so    |
    | they cannot conflict even if their LED names happen   |
    | to be identical.                                      |
    \*-----------------------------------------------------*/
    bool a_virtual = (a->GetDeviceType() == DEVICE_TYPE_VIRTUAL);
    bool b_virtual = (b->GetDeviceType() == DEVICE_TYPE_VIRTUAL);

    if (!a_virtual && !b_virtual) {
        return false;
    }

    /*-----------------------------------------------------*\
    | A virtual controller's LED names are copies of the    |
    | member devices' LED names, so a name overlap means    |
    | both controllers drive some of the same LEDs.         |
    \*-----------------------------------------------------*/
    std::set<std::string> led_names;

    for (unsigned int i = 0; i < a->GetLEDCount(); ++i) {
        std::string name = a->GetLEDName(i);
        if (!name.empty()) {
            led_names.insert(name);
        }
    }

    for (unsigned int i = 0; i < b->GetLEDCount(); ++i) {
        std::string name = b->GetLEDName(i);
        if (!name.empty() && led_names.count(name)) {
            return true;
        }
    }

    return false;
}

std::vector<RGBControllerInterface*> NeuroLavaLampWidget::findConflictingControllers(RGBControllerInterface* dev)
{
    std::vector<RGBControllerInterface*> result;
    std::set<RGBControllerInterface*> seen;

    auto add = [&](RGBControllerInterface* controller) {
        if (controller && controller != dev && seen.insert(controller).second) {
            result.push_back(controller);
        }
    };

    for (RGBControllerInterface* other : plugin_api->GetRGBControllers()) {
        if (sharesPhysicalLEDs(dev, other)) {
            add(other);
        }
    }

    /*-----------------------------------------------------*\
    | Fallback: a hidden device is a member of some virtual |
    | controller. If LED-name matching could not identify   |
    | it (e.g. overlapping map pixels rename the LEDs),     |
    | pause every virtual controller to be safe.            |
    \*-----------------------------------------------------*/
    if (result.empty() && dev->GetHidden()) {
        for (RGBControllerInterface* other : plugin_api->GetRGBControllers()) {
            if (other->GetDeviceType() == DEVICE_TYPE_VIRTUAL) {
                add(other);
            }
        }
    }

    return result;
}

void NeuroLavaLampWidget::backupDevice(RGBControllerInterface* dev)
{
    DeviceStateBackup backup;
    backup.active_mode = dev->GetActiveMode();
    backup.colors.assign(dev->GetColorsPointer(), dev->GetColorsPointer() + dev->GetLEDCount());
    device_backups.insert(dev, backup);
}

void NeuroLavaLampWidget::restoreDevice(RGBControllerInterface* dev)
{
    DeviceStateBackup backup = device_backups[dev];
    
    dev->SetActiveMode(backup.active_mode);
    dev->UpdateMode();
    
    for (size_t i = 0; i < backup.colors.size() && i < dev->GetLEDCount(); i++) {
        dev->SetColor(i, backup.colors[i]);
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
                item.controller->SetAllZoneColors(zone.zone_idx, color);
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
                    unsigned int leds_count = item.controller->GetZoneLEDsCount(zone.zone_idx);
                    for (unsigned int led_idx = 0; led_idx < leds_count; led_idx++) {
                        float led_pos = 0.0f;
                        if (leds_count > 1) {
                            led_pos = (float)led_idx / (float)(leds_count - 1);
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
                        
                        item.controller->SetZoneColor(zone.zone_idx, led_idx, ToRGBColor(r,g,b));
                    }
                } else {
                    float zone_t = t;
                    unsigned char r = sr + (tr - sr) * zone_t;
                    unsigned char g = sg + (tg - sg) * zone_t;
                    unsigned char b = sb + (tb - sb) * zone_t;
                    
                    item.controller->SetAllZoneColors(zone.zone_idx, ToRGBColor(r,g,b));
                }
            }
        }
        
        if (has_selected) {
            item.controller->UpdateLEDs();
        }
    }
}
