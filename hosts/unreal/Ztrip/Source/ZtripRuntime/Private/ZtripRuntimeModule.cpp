#include "ZtripPlayer.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectGlobals.h"

class FZtripRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		PreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddStatic(&FZtripRuntimeModule::OnObjectPreSave);
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);
#endif
	}

private:
#if WITH_EDITOR
	// Never save graph-driven values into a level: when a world, a level or
	// an actor (one file per actor) is about to be saved, every player in
	// that world puts its targets back. They are driven again next tick.
	static void OnObjectPreSave(UObject* Object, FObjectPreSaveContext Context)
	{
		UWorld* World = nullptr;
		if (UWorld* AsWorld = Cast<UWorld>(Object))
		{
			World = AsWorld;
		}
		else if (ULevel* Level = Cast<ULevel>(Object))
		{
			World = Level->OwningWorld;
		}
		else if (AActor* Actor = Cast<AActor>(Object))
		{
			World = Actor->GetWorld();
		}
		if (!World)
		{
			return;
		}
		for (TActorIterator<AZtripPlayer> It(World); It; ++It)
		{
			It->RestoreTargets();
		}
	}

	FDelegateHandle PreSaveHandle;
#endif
};

IMPLEMENT_MODULE(FZtripRuntimeModule, ZtripRuntime)
