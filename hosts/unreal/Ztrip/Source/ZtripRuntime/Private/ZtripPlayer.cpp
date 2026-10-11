#include "ZtripPlayer.h"

#include "ZtripBinder.h"
#include "ZtripStrings.h"

#include "Components/AudioComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "LevelSequence.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

#include "ztrip/host/bake.hpp"
#include "ztrip/host/session.hpp"

#include <algorithm>
#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogZtrip, Log, All);

AZtripPlayer::FEditorHooks AZtripPlayer::EditorHooks;

AZtripPlayer::AZtripPlayer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
#if WITH_EDITORONLY_DATA
	if (UBillboardComponent* Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite")))
	{
		static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Icon(TEXT("/Engine/EditorResources/S_Note"));
		Sprite->Sprite = Icon.Get();
		Sprite->SetupAttachment(RootComponent);
		Sprite->bIsScreenSizeScaled = true;
	}
#endif

	ztrip::host::SessionSettings Settings;
	Settings.threaded = true;
	Settings.cache_dir = Ztrip::ToPath(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Ztrip") / TEXT("Cache")));
	Session = std::make_shared<ztrip::host::Session>(Settings);
	Binder = std::make_shared<FZtripBinder>();
}

AZtripPlayer::~AZtripPlayer() = default;

// ---- Paths -----------------------------------------------------------------------

FString AZtripPlayer::ResolvePath(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return Path;
	}
	return FPaths::IsRelative(Path) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path) : Path;
}

FString AZtripPlayer::GetResolvedAudioFile() const
{
	if (!AudioFile.FilePath.IsEmpty())
	{
		return ResolvePath(AudioFile.FilePath);
	}
#if WITH_EDITORONLY_DATA
	if (const USoundWave* Wave = Cast<USoundWave>(Sound))
	{
		if (Wave->AssetImportData)
		{
			return Wave->AssetImportData->GetFirstFilename();
		}
	}
#endif
	return FString();
}

// ---- Loading ---------------------------------------------------------------------

void AZtripPlayer::EnsureStarted()
{
	if (bStarted)
	{
		return;
	}
	bStarted = true;
	LoadTimeline();
	LoadAudio();
	LoadGraph();
	RefreshStatus();
}

void AZtripPlayer::Reload()
{
	bStarted = true;
	RestoreTargets();
	LoadTimeline();
	LoadAudio();
	LoadGraph();
	RefreshStatus();
}

void AZtripPlayer::LoadAudio()
{
	LoadedAudioFile = GetResolvedAudioFile();
	if (LoadedAudioFile.IsEmpty())
	{
		Session->clear_audio();
	}
	else
	{
		Session->load_audio_file(Ztrip::ToPath(LoadedAudioFile));
	}
	bApplied = false;
}

void AZtripPlayer::LoadGraph()
{
	const auto Changes = GraphFile.FilePath.IsEmpty() ? Session->clear_graph()
		: Session->load_graph_file(Ztrip::ToPath(ResolvePath(GraphFile.FilePath)));
	if (!Session->graph_error().empty())
	{
		UE_LOG(LogZtrip, Warning, TEXT("%s: %s"), *GetName(), *Ztrip::ToFString(Session->graph_error()));
	}
	HandleChanges(Changes.analysis, Changes.graph, Changes.timeline);
}

void AZtripPlayer::LoadTimeline()
{
	const auto Changes = Session->set_timeline_files(Ztrip::ToPath(ResolvePath(TempoFile.FilePath)),
		Ztrip::ToPath(ResolvePath(SequenceFile.FilePath)));
	HandleChanges(Changes.analysis, Changes.graph, Changes.timeline);
}

void AZtripPlayer::HandleChanges(bool bAnalysis, bool bGraph, bool bTimeline)
{
	if (bGraph)
	{
		// Targets may have changed: put the level back and bind afresh.
		RestoreTargets();
		PullMacros();
		OnGraphLoaded.Broadcast();
	}
	if (bAnalysis)
	{
		if (!Session->audio_error().empty())
		{
			UE_LOG(LogZtrip, Warning, TEXT("%s: %s"), *GetName(), *Ztrip::ToFString(Session->audio_error()));
		}
		else if (Session->analysis() && !Session->loading())
		{
			OnAnalysisLoaded.Broadcast();
			if (bPlayWhenLoaded)
			{
				bPlayWhenLoaded = false;
				Play(0.0f);
			}
		}
	}
	if (bAnalysis || bGraph || bTimeline)
	{
		bApplied = false;
		RefreshStatus();
	}
}

// ---- Macros ----------------------------------------------------------------------

void AZtripPlayer::PullMacros()
{
	// Values kept with the level win over the graph's defaults.
	TMap<FString, float> Next;
	for (const ztrip::host::MacroInfo& Macro : Session->macros())
	{
		const FString Name = Ztrip::ToFString(Macro.name);
		if (const float* Kept = Macros.Find(Name))
		{
			Session->set_macro(Macro.name, *Kept);
		}
		Next.Add(Name, static_cast<float>(Session->macro(Macro.name).value_or(Macro.value)));
	}
	Macros = MoveTemp(Next);
}

void AZtripPlayer::PushMacros()
{
	for (TPair<FString, float>& Macro : Macros)
	{
		const std::string Name = Ztrip::ToStd(Macro.Key);
		if (Session->set_macro(Name, Macro.Value))
		{
			Macro.Value = static_cast<float>(Session->macro(Name).value_or(Macro.Value)); // clamped
		}
	}
	bApplied = false;
}

bool AZtripPlayer::SetMacro(const FString& Name, float Value)
{
	const std::string Key = Ztrip::ToStd(Name);
	if (!Session->set_macro(Key, Value))
	{
		return false;
	}
	Macros.Add(Name, static_cast<float>(Session->macro(Key).value_or(Value)));
	bApplied = false;
	return true;
}

float AZtripPlayer::GetMacro(const FString& Name) const
{
	return static_cast<float>(Session->macro(Ztrip::ToStd(Name)).value_or(0.0));
}

// ---- Playback --------------------------------------------------------------------

void AZtripPlayer::Play(float From)
{
	EnsureStarted();
	if (From >= 0.0f)
	{
		SongTime = From;
	}
	bPlaying = true;
	if (Sound)
	{
		if (!AudioComponent)
		{
			AudioComponent = NewObject<UAudioComponent>(this, NAME_None, RF_Transient);
			AudioComponent->bAutoActivate = false;
			AudioComponent->bAllowSpatialization = false;
			AudioComponent->bIsUISound = true; // audible in the editor and while paused
			AudioComponent->RegisterComponentWithWorld(GetWorld());
		}
		AudioComponent->SetSound(Sound);
		AudioComponent->Play(SongTime);
	}
	RefreshStatus();
}

void AZtripPlayer::Stop()
{
	bPlaying = false;
	bPlayWhenLoaded = false;
	if (AudioComponent)
	{
		AudioComponent->Stop();
	}
	RefreshStatus();
}

void AZtripPlayer::SetSongTime(float Seconds)
{
	SongTime = FMath::Max(0.0f, FMath::IsFinite(Seconds) ? Seconds : 0.0f);
	if (bPlaying && AudioComponent && AudioComponent->IsPlaying())
	{
		AudioComponent->Play(SongTime);
	}
	// Sequencer and Movie Render Queue set the time and render straight
	// away, so evaluate now rather than on the next tick.
	if (bStarted)
	{
		EvaluateAndApply();
	}
}

void AZtripPlayer::SetActive(bool bNewActive)
{
	bActive = bNewActive;
	if (!bActive)
	{
		RestoreTargets();
	}
	else
	{
		bApplied = false;
	}
	RefreshStatus();
}

void AZtripPlayer::RestoreTargets()
{
	if (Binder)
	{
		Binder->Restore();
	}
	bApplied = false;
}

void AZtripPlayer::WaitUntilLoaded()
{
	EnsureStarted();
	const auto Changes = Session->wait();
	HandleChanges(Changes.analysis, Changes.graph, Changes.timeline);
}

void AZtripPlayer::UpdateNow()
{
	EnsureStarted();
	EvaluateAndApply();
}

void AZtripPlayer::EvaluateAndApply()
{
	Session->evaluate(SongTime);
	LastEvaluated = SongTime;
	if (!bActive || !GetWorld())
	{
		return;
	}
	const int32 ProblemsBefore = Binder->Problems().Num();
	const int32 BoundBefore = Binder->BoundCount();
	Binder->Apply(*this, Session->outputs());
	bApplied = true;

	const TArray<FString> Problems = Binder->Problems();
	if (Problems.Num() != ProblemsBefore)
	{
		for (int32 i = ProblemsBefore; i < Problems.Num(); ++i)
		{
			UE_LOG(LogZtrip, Warning, TEXT("%s target %s"), *GetName(), *Problems[i]);
		}
	}
#if WITH_EDITOR
	// Remember the actors found by label, for packaged games.
	if (Binder->BoundCount() != BoundBefore && !GetWorld()->IsGameWorld())
	{
		Binder->ForEachTarget([this](const FZtripResolvedTarget& Target)
		{
			AActor* Actor = Target.Actor.Get();
			if (!Target.IsOk() || !Actor || Target.Parsed.actor.empty())
			{
				return;
			}
			const FString Name = Ztrip::ToFString(Target.Parsed.actor);
			const TSoftObjectPtr<AActor>* Cached = ActorCache.Find(Name);
			if (!Cached || Cached->Get() != Actor)
			{
				Modify();
				ActorCache.Add(Name, Actor);
			}
		});
	}
#endif
	if (Problems.Num() != ProblemsBefore || Binder->BoundCount() != BoundBefore)
	{
		RefreshStatus();
	}
}

void AZtripPlayer::Advance(float DeltaSeconds)
{
	EnsureStarted();
	const auto Changes = Session->poll(DeltaSeconds);
	HandleChanges(Changes.analysis, Changes.graph, Changes.timeline);

	if (bPlaying)
	{
		SongTime += DeltaSeconds;
		const float Duration = GetDuration();
		if (Duration > 0.0f && SongTime >= Duration)
		{
			SongTime = Duration;
			Stop();
			OnFinished.Broadcast();
		}
	}
	if (bPlaying || !bApplied || SongTime != LastEvaluated)
	{
		EvaluateAndApply();
	}
}

void AZtripPlayer::RefreshStatus()
{
	TArray<FString> Lines;
	for (const std::string& Line : Session->status_lines())
	{
		Lines.Add(Ztrip::ToFString(Line));
	}
	if (Session->has_graph())
	{
		Lines.Add(FString::Printf(TEXT("Targets: %d bound%s"), Binder->BoundCount(), bActive ? TEXT("") : TEXT(" (inactive)")));
	}
	for (const FString& Problem : Binder->Problems())
	{
		Lines.Add(TEXT("Target ") + Problem);
	}
	if (bPlaying)
	{
		Lines.Add(TEXT("Playing"));
	}
	Status = FString::Join(Lines, TEXT("\n"));
}

// ---- Timeline --------------------------------------------------------------------

void AZtripPlayer::DraftTimeline()
{
	EnsureStarted();
	const auto Changes = Session->draft_timeline();
	HandleChanges(Changes.analysis, Changes.graph, Changes.timeline);
	RefreshStatus();
}

void AZtripPlayer::SaveTimeline()
{
	EnsureStarted();
	FString Tempo = ResolvePath(TempoFile.FilePath);
	FString Sequence = ResolvePath(SequenceFile.FilePath);
	const FString Song = Ztrip::ToFString(Session->audio_label());
	if ((Tempo.IsEmpty() || Sequence.IsEmpty()) && Song.IsEmpty())
	{
		Status = TEXT("Timeline: set Tempo File and Sequence File, or load a song, to save the timeline.");
		return;
	}
	const FString Base = FPaths::Combine(FPaths::GetPath(Song), FPaths::GetBaseFilename(Song));
	auto Keep = [this](FFilePath& Setting, FString& Path, const FString& Default)
	{
		if (!Path.IsEmpty())
		{
			return;
		}
		Path = Default;
		FString Relative = Path;
		if (FPaths::MakePathRelativeTo(Relative, *FPaths::ProjectDir()))
		{
			Modify();
			Setting.FilePath = Relative;
		}
		else
		{
			Modify();
			Setting.FilePath = Path;
		}
	};
	Keep(TempoFile, Tempo, Base + TEXT(".tempo.json"));
	Keep(SequenceFile, Sequence, Base + TEXT(".nseq.json"));
	if (!Session->save_timeline_as(Ztrip::ToPath(Tempo), Ztrip::ToPath(Sequence)))
	{
		UE_LOG(LogZtrip, Warning, TEXT("%s: %s"), *GetName(), *Ztrip::ToFString(Session->timeline_error()));
	}
	RefreshStatus();
}

// ---- Level Sequence --------------------------------------------------------------

void AZtripPlayer::SetUpLevelSequence()
{
	EnsureStarted();
	if (EditorHooks.SetUpLevelSequence)
	{
		EditorHooks.SetUpLevelSequence(*this);
	}
}

void AZtripPlayer::BakeToLevelSequence()
{
	EnsureStarted();
	if (EditorHooks.BakeToLevelSequence)
	{
		EditorHooks.BakeToLevelSequence(*this);
	}
}

TArray<double> AZtripPlayer::GetBarTimes() const
{
	TArray<double> Out;
	if (!Session->has_tempo())
	{
		return Out;
	}
	const ztrip::time::TempoMap& Tempo = Session->tempo();
	const double Duration = Session->duration();
	for (int Bar = 0; Bar < 100000; ++Bar)
	{
		const double Seconds = Tempo.seconds_at(Tempo.bar_start_beat(Bar));
		if (Seconds > Duration)
		{
			break;
		}
		if (Seconds >= 0.0)
		{
			Out.Add(Seconds);
		}
	}
	return Out;
}

std::vector<ztrip::host::BakedCurve> AZtripPlayer::BakeCurves(const ztrip::host::BakeSettings& Settings)
{
	WaitUntilLoaded();
	if (!Session->evaluator())
	{
		return {};
	}
	return ztrip::host::bake(*Session->evaluator(), Session->duration(), Settings);
}

// ---- Queries ---------------------------------------------------------------------

float AZtripPlayer::GetOutput(const FString& Target) const
{
	return static_cast<float>(Session->output(Ztrip::ToStd(Target)).value_or(0.0));
}

float AZtripPlayer::GetPortValue(const FString& Node, const FString& Port) const
{
	const auto Value = Session->port_value(Ztrip::ToStd(Node), Ztrip::ToStd(Port));
	return Value ? static_cast<float>(Value->v) : 0.0f;
}

bool AZtripPlayer::IsReady() const { return Session->has_graph() && Session->analysis() != nullptr; }
float AZtripPlayer::GetDuration() const { return static_cast<float>(Session->duration()); }
float AZtripPlayer::GetBPM() const { return static_cast<float>(Session->bpm()); }

// ---- Actor -----------------------------------------------------------------------

void AZtripPlayer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Advance(DeltaSeconds);
}

void AZtripPlayer::BeginPlay()
{
	Super::BeginPlay();
	EnsureStarted();
	if (bAutoplay)
	{
		if (Session->loading() || !Session->analysis())
		{
			bPlayWhenLoaded = true; // starts once the song is analysed
		}
		else
		{
			Play(0.0f);
		}
	}
}

void AZtripPlayer::EndPlay(const EEndPlayReason::Type Reason)
{
	Stop();
	RestoreTargets();
	Super::EndPlay(Reason);
}

void AZtripPlayer::Destroyed()
{
	RestoreTargets();
	Super::Destroyed();
}

#if WITH_EDITOR
void AZtripPlayer::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	const FName Name = Event.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(AZtripPlayer, AudioFile) || Name == GET_MEMBER_NAME_CHECKED(AZtripPlayer, Sound))
	{
		if (GetResolvedAudioFile() != LoadedAudioFile)
		{
			bStarted = true;
			LoadAudio();
			RefreshStatus();
		}
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(AZtripPlayer, GraphFile))
	{
		bStarted = true;
		LoadGraph();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(AZtripPlayer, TempoFile) || Name == GET_MEMBER_NAME_CHECKED(AZtripPlayer, SequenceFile))
	{
		bStarted = true;
		LoadTimeline();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(AZtripPlayer, TargetMap) || Name == GET_MEMBER_NAME_CHECKED(AZtripPlayer, ParameterCollections))
	{
		RestoreTargets();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(AZtripPlayer, Macros))
	{
		PushMacros();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(AZtripPlayer, bActive))
	{
		SetActive(bActive);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(AZtripPlayer, SongTime))
	{
		SetSongTime(SongTime);
	}
}
#endif
