#include "OpenRGBNeuroLavaLampPlugin.h"
#include "NeuroLavaLampWidget.h"
#include "ResourceManagerCallback.h"
#include <QHBoxLayout>
#include <QMetaObject>

OpenRGBPluginAPIInterface* OpenRGBNeuroLavaLampPlugin::RMPointer = nullptr;

OpenRGBPluginInfo OpenRGBNeuroLavaLampPlugin::GetPluginInfo()
{
    printf("[OpenRGBNeuroLavaLampPlugin] Loading plugin info.\n");

    OpenRGBPluginInfo info;
    info.Name         = "NeuroLavaLamp Plugin";
    info.Description  = "Synchronizes OpenRGB with Neuro-sama's lava lamp";
    info.Version      = VERSION_STRING;
    info.Commit       = GIT_COMMIT_ID;
    info.URL          = "https://github.com/bw8686/OpenRGBNeuroLavaLampPlugin";
    info.Icon.load(":/OpenRGBNeuroLavaLampPlugin.png");

    info.Location     =  OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label        =  "NeuroLavaLamp";
    info.TabIconString=  "NeuroLavaLamp";
    info.TabIcon.load(":/OpenRGBNeuroLavaLampPlugin.png");

    info.ProtocolVersion = 2;

    return info;
}

unsigned int OpenRGBNeuroLavaLampPlugin::GetPluginAPIVersion()
{
    printf("[OpenRGBNeuroLavaLampPlugin] Loading plugin API version.\n");
    return OPENRGB_PLUGIN_API_VERSION;
}

void OpenRGBNeuroLavaLampPlugin::Load(OpenRGBPluginAPIInterface* plugin_api_ptr)
{
    printf("[OpenRGBNeuroLavaLampPlugin] Loading plugin.\n");
    RMPointer = plugin_api_ptr;
}

QWidget* OpenRGBNeuroLavaLampPlugin::GetWidget()
{
    printf("[OpenRGBNeuroLavaLampPlugin] Creating widget.\n");
    widget = new NeuroLavaLampWidget(RMPointer);
    return widget;
}

QMenu* OpenRGBNeuroLavaLampPlugin::GetTrayMenu()
{
    return nullptr;
}

void OpenRGBNeuroLavaLampPlugin::Unload()
{
    printf("[OpenRGBNeuroLavaLampPlugin] Unload called.\n");
}

OpenRGBNeuroLavaLampPlugin::OpenRGBNeuroLavaLampPlugin()
{
    printf("[OpenRGBNeuroLavaLampPlugin] Constructor.\n");
}

OpenRGBNeuroLavaLampPlugin::~OpenRGBNeuroLavaLampPlugin()
{
    printf("[OpenRGBNeuroLavaLampPlugin] Destructor.\n");
}

void OpenRGBNeuroLavaLampPlugin::OnProfileAboutToLoad()
{
}

void OpenRGBNeuroLavaLampPlugin::OnProfileLoad(nlohmann::json profile_data)
{
}

nlohmann::json OpenRGBNeuroLavaLampPlugin::OnProfileSave()
{
    return nlohmann::json();
}

unsigned char* OpenRGBNeuroLavaLampPlugin::OnSDKCommand(unsigned int pkt_id, unsigned char* pkt_data, unsigned int* pkt_size)
{
    return nullptr;
}

void OpenRGBNeuroLavaLampPlugin::ProfileManagerUpdated(unsigned int update_reason)
{
}

void OpenRGBNeuroLavaLampPlugin::ResourceManagerUpdated(unsigned int update_reason)
{
    if (update_reason != RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED) {
        return;
    }

    /*-----------------------------------------------------*\
    | The controller list changed. This brackets the        |
    | VisualMap "hide member devices" flow: hiding happens  |
    | right before its virtual controller is registered, and|
    | unhiding right after it is unregistered. Ask the      |
    | widget to resync its selection list (hidden members   |
    | drop out, new/unhidden devices reappear).             |
    \*-----------------------------------------------------*/
    if (!widget) {
        return; // Tab was never created; nothing to update
    }

    /*-----------------------------------------------------*\
    | This callback may fire from a worker thread (e.g.     |
    | RegisterVirtualRGBControllerInThread), so hop to the  |
    | widget's GUI thread.                                  |
    \*-----------------------------------------------------*/
    QMetaObject::invokeMethod(widget, "refreshDeviceList", Qt::QueuedConnection);
}

void OpenRGBNeuroLavaLampPlugin::SettingsManagerUpdated(unsigned int update_reason)
{
}
