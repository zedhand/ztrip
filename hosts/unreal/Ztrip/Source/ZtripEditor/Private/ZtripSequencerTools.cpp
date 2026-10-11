#include "ZtripSequencerTools.h"

#include "ZtripBinder.h"
#include "ZtripPlayer.h"
#include "ZtripStrings.h"

#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Components/SceneComponent.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneTimeHelpers.h"
#include "ScopedTransaction.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Sections/MovieSceneDoubleSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sound/SoundBase.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneDoubleTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "ztrip/host/bake.hpp"
#include "ztrip/host/session.hpp"

#include <map>
#include <vector>

#define LOCTEXT_NAMESPACE "ZtripSequencer"

DEFINE_LOG_CATEGORY_STATIC(LogZtripSequencer, Log, All);

namespace
{
	using Kind = ztrip::host::UnrealTarget::Kind;

	void Notify(const FString& Message, bool bSucceeded)
	{
		UE_LOG(LogZtripSequencer, Log, TEXT("%s"), *Message);
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = 8.0f;
		Info.bFireAndForget = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSucceeded ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	ULevelSequence* SequenceOf(AZtripPlayer& Player)
	{
		ULevelSequence* Sequence = Player.LevelSequence.LoadSynchronous();
		if (!Sequence || !Sequence->GetMovieScene())
		{
			Notify(TEXT("ztrip: set the player's Level Sequence first (create one with Cinematics > Add Level Sequence)."), false);
			return nullptr;
		}
		return Sequence;
	}

	FString BindingName(UObject* Object)
	{
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			return Actor->GetActorLabel();
		}
		return Object->GetName();
	}

	// The sequence's binding for an actor, or for a component under its
	// actor's binding, made when there is none yet.
	FGuid FindOrBind(ULevelSequence& Sequence, UObject& Object, AActor& Actor)
	{
		UMovieScene* MovieScene = Sequence.GetMovieScene();
		FGuid Parent;
		if (&Object != &Actor)
		{
			Parent = FindOrBind(Sequence, Actor, Actor);
		}
		const FString Name = BindingName(&Object);
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
			if (Possessable.GetName() == Name && Possessable.GetParent() == Parent &&
				Possessable.GetPossessedObjectClass() == Object.GetClass())
			{
				return Possessable.GetGuid();
			}
		}
		const FGuid Guid = MovieScene->AddPossessable(Name, Object.GetClass());
		if (Parent.IsValid())
		{
			if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Guid))
			{
				Possessable->SetParent(Parent, MovieScene);
			}
			Sequence.BindPossessableObject(Guid, Object, &Actor);
		}
		else
		{
			Sequence.BindPossessableObject(Guid, Object, Actor.GetWorld());
		}
		return Guid;
	}

	template <typename TrackType>
	TrackType* FindTrack(UMovieScene& MovieScene, const FGuid& Guid, const FString& PropertyPath)
	{
		if (const FMovieSceneBinding* Binding = MovieScene.FindBinding(Guid))
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				TrackType* Typed = Cast<TrackType>(Track);
				if (Typed && Typed->GetPropertyPath() == FName(*PropertyPath))
				{
					return Typed;
				}
			}
		}
		return nullptr;
	}

	// A fresh section, filling the whole sequence, on a new or emptied track.
	template <typename TrackType, typename SectionType>
	SectionType* FreshSection(UMovieScene& MovieScene, const FGuid& Guid, const FName& PropertyName, const FString& PropertyPath)
	{
		TrackType* Track = FindTrack<TrackType>(MovieScene, Guid, PropertyPath);
		if (Track)
		{
			Track->Modify();
			Track->RemoveAllAnimationData();
		}
		else
		{
			Track = MovieScene.AddTrack<TrackType>(Guid);
			Track->SetPropertyNameAndPath(PropertyName, PropertyPath);
		}
		SectionType* Section = Cast<SectionType>(Track->CreateNewSection());
		Section->SetRange(TRange<FFrameNumber>::All());
		Track->AddSection(*Section);
		return Section;
	}

	struct FFrameMapper
	{
		FFrameNumber Start;
		FFrameRate Tick;

		// Song time 0 sits at the start of the playback range.
		FFrameNumber operator()(double SongSeconds) const { return Start + Tick.AsFrameNumber(SongSeconds); }
	};

	FFrameMapper MapperFor(const UMovieScene& MovieScene)
	{
		return {UE::MovieScene::DiscreteInclusiveLower(MovieScene.GetPlaybackRange()), MovieScene.GetTickResolution()};
	}
}

// ---- Set up ----------------------------------------------------------------------

void ZtripSequencer::SetUp(AZtripPlayer& Player)
{
	ULevelSequence* Sequence = SequenceOf(Player);
	if (!Sequence)
	{
		return;
	}
	Player.WaitUntilLoaded();
	const ztrip::host::Session& Session = Player.GetSession();
	if (!Session.analysis())
	{
		Notify(TEXT("ztrip: the player has no analysed song yet. Set its Audio File (or a Sound imported from a WAV)."), false);
		return;
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FScopedTransaction Transaction(LOCTEXT("SetUp", "Set Up ztrip Level Sequence"));
	Sequence->Modify();
	MovieScene->Modify();

	const FFrameMapper Frame = MapperFor(*MovieScene);
	const double Duration = Session.duration();
	const FFrameNumber End = Frame(Duration);
	MovieScene->SetPlaybackRange(TRange<FFrameNumber>(Frame.Start, End));

	// The song.
	if (Player.Sound)
	{
		bool bHasSong = false;
		for (UMovieSceneTrack* Track : MovieScene->GetTracks())
		{
			if (const UMovieSceneAudioTrack* Audio = Cast<UMovieSceneAudioTrack>(Track))
			{
				for (UMovieSceneSection* Section : Audio->GetAllSections())
				{
					const UMovieSceneAudioSection* AudioSection = Cast<UMovieSceneAudioSection>(Section);
					bHasSong |= AudioSection && AudioSection->GetSound() == Player.Sound;
				}
			}
		}
		if (!bHasSong)
		{
			UMovieSceneAudioTrack* Audio = MovieScene->AddTrack<UMovieSceneAudioTrack>();
			Audio->AddNewSound(Player.Sound, Frame.Start);
		}
	}

	// Song Time follows the sequence.
	const FGuid PlayerGuid = FindOrBind(*Sequence, Player, Player);
	const FName SongTimeName = GET_MEMBER_NAME_CHECKED(AZtripPlayer, SongTime);
	UMovieSceneFloatSection* Section = FreshSection<UMovieSceneFloatTrack, UMovieSceneFloatSection>(
		*MovieScene, PlayerGuid, SongTimeName, SongTimeName.ToString());
	FMovieSceneFloatChannel* Channel = Section->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);
	Channel->AddLinearKey(Frame.Start, 0.0f);
	Channel->AddLinearKey(End, static_cast<float>(Duration));

	// A marker on every bar.
	const TArray<FMovieSceneMarkedFrame>& Marks = MovieScene->GetMarkedFrames();
	for (int32 i = Marks.Num() - 1; i >= 0; --i)
	{
		if (Marks[i].Label.StartsWith(TEXT("Bar ")))
		{
			MovieScene->DeleteMarkedFrame(i);
		}
	}
	const TArray<double> BarTimes = Player.GetBarTimes();
	for (int32 Bar = 0; Bar < BarTimes.Num(); ++Bar)
	{
		FMovieSceneMarkedFrame Mark(Frame(BarTimes[Bar]));
		Mark.Label = FString::Printf(TEXT("Bar %d"), Bar + 1);
		MovieScene->AddMarkedFrame(Mark);
	}
	const int32 Bars = BarTimes.Num();
	Notify(FString::Printf(TEXT("ztrip: set up %s with the song (%.1f s), Song Time and %d bar markers."),
		*Sequence->GetName(), Duration, Bars), true);
}

// ---- Bake ------------------------------------------------------------------------

void ZtripSequencer::Bake(AZtripPlayer& Player)
{
	ULevelSequence* Sequence = SequenceOf(Player);
	if (!Sequence)
	{
		return;
	}
	Player.WaitUntilLoaded();
	if (!Player.IsReady())
	{
		Notify(TEXT("ztrip: the player needs an analysed song and a graph to bake."), false);
		return;
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameMapper Frame = MapperFor(*MovieScene);

	// Read the level's own values (for the transform channels a target
	// doesn't drive) with nothing driven, then evaluate offline.
	Player.RestoreTargets();
	ztrip::host::BakeSettings Settings;
	Settings.start_s = Player.BakeStart;
	Settings.end_s = Player.BakeEnd > 0.0f ? Player.BakeEnd : -1.0;
	Settings.fps = MovieScene->GetDisplayRate().AsDecimal();
	Settings.tolerance = Player.BakeTolerance;
	const std::vector<ztrip::host::BakedCurve> Curves = Player.BakeCurves(Settings);

	const FScopedTransaction Transaction(LOCTEXT("Bake", "Bake ztrip to Level Sequence"));
	Sequence->Modify();
	MovieScene->Modify();

	struct FTransformKeys
	{
		USceneComponent* Scene = nullptr;
		AActor* Actor = nullptr;
		std::map<int32, const ztrip::host::BakedCurve*> Channels; // 0-2 location, 3-5 rotation, 6-8 scale
		std::map<int32, const FZtripResolvedTarget*> Targets;
	};
	TArray<FZtripResolvedTarget> Resolved;
	Resolved.Reserve(static_cast<int32>(Curves.size()));
	TMap<USceneComponent*, FTransformKeys> Transforms;
	TArray<FString> Skipped;
	int32 Baked = 0;
	int64 Keys = 0;

	for (const ztrip::host::BakedCurve& Curve : Curves)
	{
		const FZtripResolvedTarget& Target = Resolved.Add_GetRef(FZtripBinder::Resolve(Player, Ztrip::ToFString(Curve.target)));
		AActor* Actor = Target.Actor.Get();
		UObject* Object = Target.Object.Get();
		if (!Target.IsOk() || !Actor || !Object)
		{
			Skipped.Add(Target.Target + TEXT(" (") + (Target.IsOk() ? FString(TEXT("not found")) : Target.Problem) + TEXT(")"));
			continue;
		}
		switch (Target.Parsed.kind)
		{
		case Kind::Property:
		{
			const FProperty* Leaf = Target.Chain.Last();
			const FGuid Guid = FindOrBind(*Sequence, *Object, *Actor);
			if (CastField<FFloatProperty>(Leaf))
			{
				UMovieSceneFloatSection* Section = FreshSection<UMovieSceneFloatTrack, UMovieSceneFloatSection>(
					*MovieScene, Guid, Leaf->GetFName(), Target.PropertyPath);
				FMovieSceneFloatChannel* Channel = Section->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);
				for (const ztrip::host::BakeKey& Key : Curve.keys)
				{
					Channel->AddLinearKey(Frame(Key.time), static_cast<float>(Target.Map(Key.value)));
				}
			}
			else if (CastField<FDoubleProperty>(Leaf))
			{
				UMovieSceneDoubleSection* Section = FreshSection<UMovieSceneDoubleTrack, UMovieSceneDoubleSection>(
					*MovieScene, Guid, Leaf->GetFName(), Target.PropertyPath);
				FMovieSceneDoubleChannel* Channel = Section->GetChannelProxy().GetChannel<FMovieSceneDoubleChannel>(0);
				for (const ztrip::host::BakeKey& Key : Curve.keys)
				{
					Channel->AddLinearKey(Frame(Key.time), Target.Map(Key.value));
				}
			}
			else
			{
				Skipped.Add(Target.Target + TEXT(" (only float and double properties can be keyed)"));
				continue;
			}
			++Baked;
			Keys += static_cast<int64>(Curve.keys.size());
			break;
		}
		case Kind::Location:
		case Kind::Rotation:
		case Kind::Scale:
		{
			USceneComponent* Scene = Cast<USceneComponent>(Object);
			FTransformKeys& Entry = Transforms.FindOrAdd(Scene);
			Entry.Scene = Scene;
			Entry.Actor = Actor;
			const int32 Base = Target.Parsed.kind == Kind::Location ? 0 : Target.Parsed.kind == Kind::Rotation ? 3 : 6;
			Entry.Channels[Base + Target.Parsed.axis] = &Curve;
			Entry.Targets[Base + Target.Parsed.axis] = &Target;
			break;
		}
		default:
			Skipped.Add(Target.Target + TEXT(" (material, collection, Niagara, burst and call targets can't be keyed yet)"));
			break;
		}
	}

	for (TPair<USceneComponent*, FTransformKeys>& Pair : Transforms)
	{
		FTransformKeys& Entry = Pair.Value;
		// A root component is animated through its actor's transform track.
		UObject* Bound = Entry.Scene == Entry.Actor->GetRootComponent() ? static_cast<UObject*>(Entry.Actor) : static_cast<UObject*>(Entry.Scene);
		const FGuid Guid = FindOrBind(*Sequence, *Bound, *Entry.Actor);
		UMovieScene3DTransformSection* Section = FreshSection<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(
			*MovieScene, Guid, FName(TEXT("Transform")), TEXT("Transform"));
		TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		const FVector Location = Entry.Scene->GetRelativeLocation();
		const FRotator Rotation = Entry.Scene->GetRelativeRotation();
		const FVector Scale = Entry.Scene->GetRelativeScale3D();
		const double Current[9] = {Location.X, Location.Y, Location.Z, Rotation.Roll, Rotation.Pitch, Rotation.Yaw, Scale.X, Scale.Y, Scale.Z};
		for (int32 i = 0; i < 9 && i < Channels.Num(); ++i)
		{
			const auto Found = Entry.Channels.find(i);
			if (Found == Entry.Channels.end())
			{
				Channels[i]->SetDefault(Current[i]); // not driven: hold the level's value
				continue;
			}
			const FZtripResolvedTarget& Target = *Entry.Targets[i];
			for (const ztrip::host::BakeKey& Key : Found->second->keys)
			{
				Channels[i]->AddLinearKey(Frame(Key.time), Target.Map(Key.value));
			}
			++Baked;
			Keys += static_cast<int64>(Found->second->keys.size());
		}
	}

	Player.UpdateNow(); // drive the level again
	FString Message = FString::Printf(TEXT("ztrip: baked %d targets (%lld keys) into %s at %.3g fps."), Baked, Keys,
		*Sequence->GetName(), Settings.fps);
	if (Skipped.Num() > 0)
	{
		Message += TEXT(" Skipped: ") + FString::Join(Skipped, TEXT("; ")) + TEXT(".");
	}
	Message += TEXT(" Turn the player's Active off to watch the keys play on their own.");
	Notify(Message, Baked > 0);
}

#undef LOCTEXT_NAMESPACE
