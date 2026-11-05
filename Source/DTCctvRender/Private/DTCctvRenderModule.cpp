#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FDTCctvRenderModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DTCctv"));
		check(Plugin.IsValid());
		AddShaderSourceDirectoryMapping(TEXT("/DTCctv"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
	}
};

IMPLEMENT_MODULE(FDTCctvRenderModule, DTCctvRender);
