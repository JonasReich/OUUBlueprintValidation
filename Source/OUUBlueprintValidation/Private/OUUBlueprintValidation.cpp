// Copyright (c) 2026 Jonas Reich & Contributors

#include "BlueprintCompilationManager.h"
#include "Modules/ModuleManager.h"
#include "OUUBlueprintValidationCompilerExtension.h"

class FOUUBlueprintValidationModule : public IModuleInterface
{
public:
	void StartupModule()
	{
		FBlueprintCompilationManager::RegisterCompilerExtension(
			UBlueprint::StaticClass(),
			NewObject<UOUUBlueprintValidationCompilerExtension>());
	}
};

IMPLEMENT_MODULE(FOUUBlueprintValidationModule, OUUBlueprintValidation)
