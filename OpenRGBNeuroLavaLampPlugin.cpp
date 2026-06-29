#include "OpenRGBNeuroLavaLampPlugin.h"
#include "NeuroLavaLampWidget.h"
#include <QHBoxLayout>

ResourceManagerInterface* OpenRGBNeuroLavaLampPlugin::RMPointer = nullptr;

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

    return info;
}

unsigned int OpenRGBNeuroLavaLampPlugin::GetPluginAPIVersion()
{
    printf("[OpenRGBNeuroLavaLampPlugin] Loading plugin API version.\n");
    return OPENRGB_PLUGIN_API_VERSION;
}

void OpenRGBNeuroLavaLampPlugin::Load(ResourceManagerInterface* resource_manager_ptr)
{
    printf("[OpenRGBNeuroLavaLampPlugin] Loading plugin.\n");
    RMPointer = resource_manager_ptr;
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
