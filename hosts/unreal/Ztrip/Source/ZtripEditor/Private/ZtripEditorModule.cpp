#include "ZtripPlayer.h"
#include "ZtripSequencerTools.h"

#include "Modules/ModuleManager.h"

class FZtripEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		AZtripPlayer::EditorHooks.SetUpLevelSequence = [](AZtripPlayer& Player) { ZtripSequencer::SetUp(Player); };
		AZtripPlayer::EditorHooks.BakeToLevelSequence = [](AZtripPlayer& Player) { ZtripSequencer::Bake(Player); };
	}

	virtual void ShutdownModule() override
	{
		AZtripPlayer::EditorHooks.SetUpLevelSequence = nullptr;
		AZtripPlayer::EditorHooks.BakeToLevelSequence = nullptr;
	}
};

IMPLEMENT_MODULE(FZtripEditorModule, ZtripEditor)
