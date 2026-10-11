// AZtripPlayer: plays a song through a Motion Graph and drives level
// properties from it, in the editor and in game.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "ZtripTypes.h"

#include <memory>
#include <vector>

#include "ZtripPlayer.generated.h"

class UAudioComponent;
class ULevelSequence;
class UMaterialParameterCollection;
class USoundBase;
class FZtripBinder;

namespace ztrip::host
{
	class Session;
	struct BakeSettings;
	struct BakedCurve;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FZtripPlayerEvent);

// Drop one in a level, give it a song (a WAV file, plus the Sound to hear)
// and a .mgraph.json, and it analyses the song, evaluates the graph at the
// song time and writes the graph's PropertyOut targets to actors in the
// level. It runs in the editor as well: drag Song Time to scrub, or press
// Play in its details. Song Time can be animated by a Level Sequence ("Set
// Up Level Sequence" adds the song and that track), so the level follows
// Sequencer, and "Bake To Level Sequence" writes the graph's motion as
// keyframes that play without ztrip installed.
//
// Targets are "<actor>[/<component>]:<property>": see
// ztrip/host/unreal_target.hpp, or the plugin's README, for the syntax.
// Values the graph overrides are put back when the player is turned off,
// removed, or the level is saved, so they never end up saved in the level.
UCLASS(hidecategories = (Rendering, Replication, Collision, HLOD, Input, Physics, Networking, LevelInstance, Cooking, DataLayers, WorldPartition))
class ZTRIPRUNTIME_API AZtripPlayer : public AActor
{
	GENERATED_BODY()

public:
	AZtripPlayer();
	virtual ~AZtripPlayer() override;

	// ---- Song ---------------------------------------------------------------

	// The song to analyse, a WAV file (16 or 24-bit PCM or float). A path
	// relative to the project folder works in the editor and, when its folder
	// is added to Project Settings > Packaging > Additional Non-Asset
	// Directories To Package, in packaged games. Empty: the source file the
	// Sound was imported from (editor only).
	UPROPERTY(EditAnywhere, Category = "ztrip|Song", meta = (FilePathFilter = "WAV files (*.wav)|*.wav", RelativeToGameDir))
	FFilePath AudioFile;

	// What you hear when the player plays on its own. Under a Level Sequence
	// the sequence's audio track plays the song instead.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ztrip|Song")
	TObjectPtr<USoundBase> Sound;

	// The Motion Graph to run. Saving the file (from any editor) reloads it
	// within half a second, keeping node state and macro values.
	UPROPERTY(EditAnywhere, Category = "ztrip|Song", meta = (FilePathFilter = "Motion Graphs (*.mgraph.json)|*.mgraph.json", RelativeToGameDir))
	FFilePath GraphFile;

	// The song's tempo map and Note Sequence. Loaded when they exist;
	// otherwise drafted from the analysis. "Save Timeline" writes them.
	UPROPERTY(EditAnywhere, Category = "ztrip|Song", meta = (FilePathFilter = "Tempo maps (*.tempo.json)|*.tempo.json", RelativeToGameDir))
	FFilePath TempoFile;

	UPROPERTY(EditAnywhere, Category = "ztrip|Song", meta = (FilePathFilter = "Note Sequences (*.nseq.json)|*.nseq.json", RelativeToGameDir))
	FFilePath SequenceFile;

	// ---- Targets ------------------------------------------------------------

	// Graph targets sent elsewhere, e.g. to run a graph written for another
	// scene. Targets not listed are used as they are.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ztrip|Targets")
	TArray<FZtripTargetRemap> TargetMap;

	// Collections "@mpc:<parameter>" targets write to (the first one, or the
	// one named by "@mpc/<collection>:<parameter>").
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ztrip|Targets")
	TArray<TObjectPtr<UMaterialParameterCollection>> ParameterCollections;

	// Actors found by label in the editor, kept so packaged games (which
	// have no labels) find them too.
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "ztrip|Targets")
	TMap<FString, TSoftObjectPtr<AActor>> ActorCache;

	// ---- Playback -----------------------------------------------------------

	// The graph's macro controls. Filled in when the graph loads; values are
	// kept with the level.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ztrip|Playback")
	TMap<FString, float> Macros;

	// Off puts every driven property back the way it was.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ztrip|Playback", BlueprintSetter = SetActive)
	bool bActive = true;

	// The song position in seconds. Drag it to scrub, or animate it from a
	// Level Sequence.
	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, Category = "ztrip|Playback", BlueprintSetter = SetSongTime, meta = (ClampMin = "0", UIMin = "0", UIMax = "600", Units = "s"))
	float SongTime = 0.0f;

	// Play from the start when the game starts.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ztrip|Playback")
	bool bAutoplay = false;

	// What was loaded, how many targets are bound, and anything wrong.
	UPROPERTY(VisibleAnywhere, Transient, Category = "ztrip|Playback", meta = (MultiLine = true))
	FString Status;

	// ---- Level Sequence -----------------------------------------------------

	// The sequence "Set Up Level Sequence" and "Bake To Level Sequence" write to.
	UPROPERTY(EditAnywhere, Category = "ztrip|Level Sequence")
	TSoftObjectPtr<ULevelSequence> LevelSequence;

	// Song time range to bake, in seconds. An end of 0 bakes to the song's end.
	UPROPERTY(EditAnywhere, Category = "ztrip|Level Sequence", meta = (ClampMin = "0", Units = "s"))
	float BakeStart = 0.0f;

	UPROPERTY(EditAnywhere, Category = "ztrip|Level Sequence", meta = (ClampMin = "0", Units = "s"))
	float BakeEnd = 0.0f;

	// Keys are dropped while the curve stays within this of every frame's
	// value; 0 keeps a key on every frame that isn't on a straight line.
	UPROPERTY(EditAnywhere, Category = "ztrip|Level Sequence", meta = (ClampMin = "0"))
	float BakeTolerance = 0.01f;

	// ---- Blueprint ----------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "ztrip")
	void Play(float From = -1.0f);

	UFUNCTION(BlueprintCallable, Category = "ztrip")
	void Stop();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ztrip")
	bool IsPlaying() const { return bPlaying; }

	UFUNCTION(BlueprintCallable, Category = "ztrip")
	void SetSongTime(float Seconds);

	UFUNCTION(BlueprintCallable, Category = "ztrip")
	void SetActive(bool bNewActive);

	// Re-reads the song (re-analysing if it changed), the graph and the timeline.
	UFUNCTION(BlueprintCallable, Category = "ztrip")
	void Reload();

	// Sets a macro, clamped to its range. False for unknown names.
	UFUNCTION(BlueprintCallable, Category = "ztrip")
	bool SetMacro(const FString& Name, float Value);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ztrip")
	float GetMacro(const FString& Name) const;

	// A target's value after the last evaluation (0 if unknown).
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ztrip")
	float GetOutput(const FString& Target) const;

	// Any node port's value after the last evaluation (0 if unknown), for HUDs.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ztrip")
	float GetPortValue(const FString& Node, const FString& Port) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ztrip")
	bool IsReady() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ztrip")
	float GetDuration() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ztrip")
	float GetBPM() const;

	// Blocks until the song is analysed (for scripts and tools).
	UFUNCTION(BlueprintCallable, Category = "ztrip")
	void WaitUntilLoaded();

	// Evaluates and writes the targets now instead of on the next tick.
	UFUNCTION(BlueprintCallable, Category = "ztrip")
	void UpdateNow();

	UPROPERTY(BlueprintAssignable, Category = "ztrip")
	FZtripPlayerEvent OnAnalysisLoaded;

	UPROPERTY(BlueprintAssignable, Category = "ztrip")
	FZtripPlayerEvent OnGraphLoaded;

	UPROPERTY(BlueprintAssignable, Category = "ztrip")
	FZtripPlayerEvent OnFinished;

	// ---- Editor buttons -----------------------------------------------------

	UFUNCTION(CallInEditor, Category = "ztrip|Playback", meta = (DisplayName = "Play"))
	void EditorPlay() { Play(); }

	UFUNCTION(CallInEditor, Category = "ztrip|Playback", meta = (DisplayName = "Stop"))
	void EditorStop() { Stop(); }

	UFUNCTION(CallInEditor, Category = "ztrip|Song", meta = (DisplayName = "Reload"))
	void EditorReload() { Reload(); }

	// Drafts the tempo map and the Kick, Snare and Hats lanes again.
	UFUNCTION(CallInEditor, Category = "ztrip|Song")
	void DraftTimeline();

	// Writes the tempo map and notes to Tempo File and Sequence File (next
	// to the song when they are empty).
	UFUNCTION(CallInEditor, Category = "ztrip|Song")
	void SaveTimeline();

	// Adds the song, a Song Time track and bar markers to the Level Sequence.
	UFUNCTION(CallInEditor, Category = "ztrip|Level Sequence")
	void SetUpLevelSequence();

	// Writes the graph's motion into the Level Sequence as keyframes.
	UFUNCTION(CallInEditor, Category = "ztrip|Level Sequence")
	void BakeToLevelSequence();

	// ---- C++ ----------------------------------------------------------------

	ztrip::host::Session& GetSession() const { return *Session; }
	FZtripBinder& GetBinder() const { return *Binder; }
	// Puts every driven property back; the next update binds them again.
	void RestoreTargets();
	// Resolves a path setting (relative to the project folder) to a full path.
	static FString ResolvePath(const FString& Path);
	FString GetResolvedAudioFile() const;
	// Song time of every bar start in the song, from the tempo map.
	TArray<double> GetBarTimes() const;
	// Evaluates the graph offline over a range (see ztrip/host/bake.hpp)
	// without writing to the level. UpdateNow() afterwards returns the graph
	// to the current time.
	std::vector<ztrip::host::BakedCurve> BakeCurves(const ztrip::host::BakeSettings& Settings);

	// Set by the editor module: what the Level Sequence buttons do.
	struct FEditorHooks
	{
		TFunction<void(AZtripPlayer&)> SetUpLevelSequence;
		TFunction<void(AZtripPlayer&)> BakeToLevelSequence;
	};
	static FEditorHooks EditorHooks;

	// AActor
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	void EnsureStarted();
	void LoadAudio();
	void LoadGraph();
	void LoadTimeline();
	void HandleChanges(bool bAnalysis, bool bGraph, bool bTimeline);
	void PushMacros();
	void PullMacros();
	void EvaluateAndApply();
	void RefreshStatus();
	void Advance(float DeltaSeconds);

	// Shared pointers so this header needs neither type complete.
	std::shared_ptr<ztrip::host::Session> Session;
	std::shared_ptr<FZtripBinder> Binder;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AudioComponent;

	bool bStarted = false;
	bool bPlaying = false;
	bool bPlayWhenLoaded = false; // autoplay waiting for the analysis
	bool bApplied = false;
	float LastEvaluated = -1.0f;
	FString LoadedAudioFile;
};
