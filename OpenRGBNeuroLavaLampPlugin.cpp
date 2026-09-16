#include "OpenRGBNeuroLavaLampPlugin.h"
#include "NeuroLavaLampWidget.h"
#include <QHBoxLayout>

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
    return new NeuroLavaLampWidget(RMPointer);
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
}

void OpenRGBNeuroLavaLampPlugin::SettingsManagerUpdated(unsigned int update_reason)
{
}
