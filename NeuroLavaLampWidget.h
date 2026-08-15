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

#include "ResourceManagerInterface.h"
#include "RGBController.h"
#include "SettingsManager.h"
#include "json.hpp"

struct DeviceStateBackup {
    int active_mode;
    std::vector<RGBColor> colors;
};

struct ZoneItem {
    int zone_idx;
    QCheckBox* checkbox;
};

struct DeviceItem {
    RGBController* controller;
    QCheckBox* device_checkbox;
    std::vector<ZoneItem> zones;
};

class NeuroLavaLampWidget : public QWidget
{
    Q_OBJECT
public:
    NeuroLavaLampWidget(ResourceManagerInterface* rm, QWidget *parent = nullptr);
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
    void onDeviceCheckboxToggled(bool checked, RGBController* dev);
    void onZoneCheckboxToggled(RGBController* dev);
    void refreshDeviceLiveState(RGBController* dev);

private:
    void backupDevice(RGBController* dev);
    void restoreDevice(RGBController* dev);
    void processEventData(const QByteArray& data);
    
    void setEffectsPluginDeviceState(const DeviceItem& item, bool restoring);

    ResourceManagerInterface* resource_manager;
    QNetworkAccessManager* network_manager;
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
    
    QString current_connection_mode;
    RGBColor current_live_color;
    
    QTimer* animation_timer;
    bool is_zero_color_override;
    
    QTimer* schedule_timer;
    bool is_evil_only_stream;
    
    RGBColor animation_start_color;
    RGBColor animation_target_color;
    int animation_progress;
    int animation_duration;
    
    bool is_live;
    QMap<RGBController*, DeviceStateBackup> device_backups;
    QMap<QString, QMap<int, QMap<QString, bool>>> original_effects_state;
    
    std::vector<DeviceItem> device_items;
};
