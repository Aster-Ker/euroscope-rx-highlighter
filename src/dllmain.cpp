#include <windows.h>

#include "EuroScopePlugIn.h"
#include "RxHighlightPlugin.h"

namespace {
RxHighlight::RxHighlightPlugin* g_plugin = nullptr;
}

// Signatures must match the declarations in EuroScopePlugIn.h exactly.
void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
    g_plugin = new RxHighlight::RxHighlightPlugin();
    *ppPlugInInstance = g_plugin;
}

void __declspec(dllexport) EuroScopePlugInExit(void)
{
    delete g_plugin;
    g_plugin = nullptr;
}
