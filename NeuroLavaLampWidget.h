#pragma once

#include <QWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QMap>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QPushButton>
#include <vector>

#include "OpenRGBPluginInterface.h"
#include <nlohmann/json.hpp>

struct DeviceStateBackup {
    int active_mode;
    std::vector<RGBColor> colors;
};

struct ZoneItem {
    int zone_idx;
    QCheckBox* checkbox;
};

struct DeviceItem {
    RGBControllerInterface* controller;
    QCheckBox* device_checkbox;
    std::vector<ZoneItem> zones;
};

class NeuroLavaLampWidget : public QWidget
{
    Q_OBJECT
public:
    NeuroLavaLampWidget(OpenRGBPluginAPIInterface* api, QWidget *parent = nullptr);
    ~NeuroLavaLampWidget();

private slots:
    void loadSettings();
    void saveSettings();
    void updateDeviceList();
    void applyColor(RGBColor color, bool instant = false);
    
    void startSseConnection();
    void pollApi();
    void onNetworkReply(QNetworkReply* reply);
    void onSseReadyRead();
    void onSseFinished();
    void onSettingsChanged();
    void onSelectAllClicked();
    
    void fetchSchedule();
    void onScheduleReply(QNetworkReply* reply);
    
    void animationLoop();
    void onDeviceCheckboxToggled(bool checked, RGBControllerInterface* dev);
    void onZoneCheckboxToggled(RGBControllerInterface* dev);
    void refreshDeviceLiveState(RGBControllerInterface* dev);

private:
    void backupDevice(RGBControllerInterface* dev);
    void restoreDevice(RGBControllerInterface* dev);
    void processEventData(const QByteArray& data);
    
    void setEffectsPluginDeviceState(const DeviceItem& item, bool restoring);

    OpenRGBPluginAPIInterface* plugin_api;
    QNetworkAccessManager* network_manager;
    QNetworkAccessManager* schedule_network_manager;
    QTimer* poll_timer;
    
    QNetworkReply* sse_reply;
    QByteArray sse_buffer;
    
    QHBoxLayout* layout;
    QVBoxLayout* device_list_layout;
    QLineEdit* url_input;
    QLineEdit* sse_url_input;
    QLineEdit* schedule_url_input;
    QSpinBox* offline_interval_input;
    QSpinBox* live_interval_input;
    
    QComboBox* zero_color_behavior_input;
    QComboBox* color_effect_input;
    QSpinBox* animation_fps_input;
    QCheckBox* disable_evil_input;
    
    QLabel* status_label;
    QLabel* schedule_status_label;
    
    QString current_connection_mode;
    RGBColor current_live_color;
    QByteArray last_raw_event_data;
    
    QTimer* animation_timer;
    bool is_zero_color_override;
    
    QTimer* schedule_timer;
    bool is_evil_only_stream;
    
    RGBColor animation_start_color;
    RGBColor animation_target_color;
    int animation_progress;
    int animation_duration;
    
    bool is_live;
    QMap<RGBControllerInterface*, DeviceStateBackup> device_backups;
    QMap<QString, QMap<int, QMap<QString, bool>>> original_effects_state;
    
    std::vector<DeviceItem> device_items;
};
