#include "ZtripBinder.h"

#include "ZtripPlayer.h"
#include "ZtripStrings.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/UnrealType.h"

#include <cmath>
#include <string>

namespace
{
	using Kind = ztrip::host::UnrealTarget::Kind;

	FString Quote(const FString& Name) { return TEXT("\"") + Name + TEXT("\""); }

	bool IsLeaf(const FProperty* Property)
	{
		return CastField<FNumericProperty>(Property) != nullptr || CastField<FBoolProperty>(Property) != nullptr;
	}

	double ReadLeaf(const FProperty* Leaf, const void* Value)
	{
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Leaf))
		{
			return Bool->GetPropertyValue(Value) ? 1.0 : 0.0;
		}
		const FNumericProperty* Number = CastField<FNumericProperty>(Leaf);
		if (Number->IsFloatingPoint())
		{
			return Number->GetFloatingPointPropertyValue(Value);
		}
		return static_cast<double>(Number->GetSignedIntPropertyValue(Value));
	}

	void WriteLeaf(const FProperty* Leaf, void* Value, double V)
	{
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Leaf))
		{
			Bool->SetPropertyValue(Value, V >= 0.5);
			return;
		}
		const FNumericProperty* Number = CastField<FNumericProperty>(Leaf);
		if (Number->IsFloatingPoint())
		{
			Number->SetFloatingPointPropertyValue(Value, V);
			return;
		}
		double Rounded = std::round(V);
		if (CastField<FByteProperty>(Leaf) != nullptr)
		{
			Rounded = FMath::Clamp(Rounded, 0.0, 255.0); // FColor channels and enums
		}
		Number->SetIntPropertyValue(Value, static_cast<int64>(Rounded));
	}

	// The leaf's memory, starting from the top-level property's value.
	void* LeafFrom(const TArray<FProperty*>& Chain, void* TopValue)
	{
		void* Value = TopValue;
		for (int32 i = 1; i < Chain.Num(); ++i)
		{
			Value = Chain[i]->ContainerPtrToValuePtr<void>(Value);
		}
		return Value;
	}

	// Looks the property path up on one object's class: a property, then
	// fields of nested structs, ending on a number or bool.
	bool FindChain(const UObject* Object, const std::vector<std::string>& Path, TArray<FProperty*>& Chain, FString& Why)
	{
		Chain.Reset();
		const UStruct* Scope = Object->GetClass();
		for (size_t i = 0; i < Path.size(); ++i)
		{
			const FString Name = Ztrip::ToFString(Path[i]);
			FProperty* Property = FindFProperty<FProperty>(Scope, FName(*Name));
			if (!Property)
			{
				Why = (i == 0 ? FString(TEXT("no property ")) : FString(TEXT("no field "))) + Quote(Name);
				return false;
			}
			Chain.Add(Property);
			if (i + 1 < Path.size())
			{
				const FStructProperty* Struct = CastField<FStructProperty>(Property);
				if (!Struct)
				{
					Why = Quote(Name) + TEXT(" has no fields");
					return false;
				}
				Scope = Struct->Struct;
			}
		}
		if (!IsLeaf(Chain.Last()))
		{
			Why = Quote(Chain.Last()->GetName()) + TEXT(" is not a number or bool; drive one of its fields, e.g. ") +
				Chain.Last()->GetName() + TEXT(".X or .R");
			return false;
		}
		return true;
	}

	// Set<Property>(Value) with exactly one parameter of the property's type,
	// for numbers and structs (bool setters often take other names).
	UFunction* FindSetter(UObject* Object, FProperty* Top)
	{
		if (CastField<FBoolProperty>(Top))
		{
			return nullptr;
		}
		UFunction* Function = Object->FindFunction(FName(*(FString(TEXT("Set")) + Top->GetName())));
		if (!Function || Function->NumParms != 1)
		{
			return nullptr;
		}
		FProperty* Param = CastField<FProperty>(Function->ChildProperties);
		if (!Param || !Param->HasAnyPropertyFlags(CPF_Parm) || Param->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm))
		{
			return nullptr;
		}
		return Param->SameType(Top) ? Function : nullptr;
	}

	// Calls a function whose first parameter takes the value.
	void CallWithValue(UObject* Object, UFunction* Function, TFunctionRef<void(FProperty* Param, void* Value)> Fill)
	{
		uint8* Params = static_cast<uint8*>(FMemory_Alloca(FMath::Max<int32>(Function->ParmsSize, 1)));
		FMemory::Memzero(Params, FMath::Max<int32>(Function->ParmsSize, 1));
		FProperty* Param = Function->NumParms > 0 ? CastField<FProperty>(Function->ChildProperties) : nullptr;
		if (Param)
		{
			Param->InitializeValue_InContainer(Params);
			Fill(Param, Param->ContainerPtrToValuePtr<void>(Params));
		}
		Object->ProcessEvent(Function, Params);
		if (Param)
		{
			Param->DestroyValue_InContainer(Params);
		}
	}

	AActor* FindActor(const AZtripPlayer& Player, UWorld* World, const FString& Name)
	{
		AActor* ByName = nullptr;
		AActor* ByTag = nullptr;
		const FName Tag(*Name);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
#if WITH_EDITOR
			if (Actor->GetActorLabel() == Name)
			{
				return Actor;
			}
#endif
			if (!ByName && Actor->GetName() == Name)
			{
				ByName = Actor;
			}
			if (!ByTag && Actor->ActorHasTag(Tag))
			{
				ByTag = Actor;
			}
		}
		if (ByName || ByTag)
		{
			return ByName ? ByName : ByTag;
		}
		// Packaged games have no labels: use what the editor found.
		if (const TSoftObjectPtr<AActor>* Cached = Player.ActorCache.Find(Name))
		{
			AActor* Actor = Cached->Get();
			if (Actor && Actor->GetWorld() == World)
			{
				return Actor;
			}
		}
		return nullptr;
	}

	UActorComponent* FindComponent(AActor* Actor, const FString& Name)
	{
		TInlineComponentArray<UActorComponent*> Components(Actor);
		for (UActorComponent* Component : Components)
		{
			if (Component && Component->GetName() == Name)
			{
				return Component;
			}
		}
		for (UActorComponent* Component : Components)
		{
			if (Component && Component->GetClass()->GetName() == Name)
			{
				return Component;
			}
		}
		return nullptr;
	}

	// The actor's components, root first, then the rest in their order.
	TArray<UActorComponent*> ComponentsOf(AActor* Actor)
	{
		TArray<UActorComponent*> Out;
		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			Out.Add(Root);
		}
		TInlineComponentArray<UActorComponent*> Components(Actor);
		for (UActorComponent* Component : Components)
		{
			if (Component)
			{
				Out.AddUnique(Component);
			}
		}
		return Out;
	}

	template <typename T>
	T* FirstComponent(AActor* Actor)
	{
		for (UActorComponent* Component : ComponentsOf(Actor))
		{
			if (T* Typed = Cast<T>(Component))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	bool HasScalarParameter(const UMaterialInterface* Material, FName Name)
	{
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		Material->GetAllScalarParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			if (Info.Name == Name)
			{
				return true;
			}
		}
		return false;
	}

	double ReadTransform(const USceneComponent* Scene, Kind TargetKind, int32 Axis)
	{
		if (TargetKind == Kind::Location)
		{
			return Scene->GetRelativeLocation()[Axis];
		}
		if (TargetKind == Kind::Scale)
		{
			return Scene->GetRelativeScale3D()[Axis];
		}
		const FRotator Rotation = Scene->GetRelativeRotation();
		return Axis == 0 ? Rotation.Roll : Axis == 1 ? Rotation.Pitch : Rotation.Yaw;
	}

	void WriteTransform(USceneComponent* Scene, Kind TargetKind, int32 Axis, double Value)
	{
		if (TargetKind == Kind::Location)
		{
			FVector Location = Scene->GetRelativeLocation();
			Location[Axis] = Value;
			Scene->SetRelativeLocation(Location);
		}
		else if (TargetKind == Kind::Scale)
		{
			FVector Scale = Scene->GetRelativeScale3D();
			Scale[Axis] = Value;
			Scene->SetRelativeScale3D(Scale);
		}
		else
		{
			FRotator Rotation = Scene->GetRelativeRotation();
			(Axis == 0 ? Rotation.Roll : Axis == 1 ? Rotation.Pitch : Rotation.Yaw) = Value;
			Scene->SetRelativeRotation(Rotation);
		}
	}
}

// ---- Resolving -------------------------------------------------------------------

FZtripResolvedTarget FZtripBinder::Resolve(const AZtripPlayer& Player, const FString& Target)
{
	FZtripResolvedTarget Out;
	Out.Target = Target;
	Out.Resolved = Target;
	for (const FZtripTargetRemap& Remap : Player.TargetMap)
	{
		if (Remap.From == Target && !Remap.To.IsEmpty())
		{
			Out.Resolved = Remap.To;
			Out.Scale = Remap.Scale;
			Out.Offset = Remap.Offset;
			break;
		}
	}

	std::string Error;
	const auto Parsed = ztrip::host::parse_unreal_target(Ztrip::ToStd(Out.Resolved), Error);
	if (!Parsed)
	{
		Out.Problem = Ztrip::ToFString(Error);
		return Out;
	}
	Out.Parsed = *Parsed;
	UWorld* InWorld = Player.GetWorld();
	if (!InWorld)
	{
		Out.Problem = TEXT("the player is not in a world");
		return Out;
	}

	if (Parsed->kind == Kind::Collection)
	{
		const FString Wanted = Ztrip::ToFString(Parsed->collection);
		UMaterialParameterCollection* Collection = nullptr;
		for (UMaterialParameterCollection* Candidate : Player.ParameterCollections)
		{
			if (Candidate && (Wanted.IsEmpty() || Candidate->GetName() == Wanted))
			{
				Collection = Candidate;
				break;
			}
		}
		if (!Collection)
		{
			Out.Problem = Wanted.IsEmpty() ? FString(TEXT("add a Material Parameter Collection to the player's Parameter Collections"))
				: TEXT("no collection named ") + Quote(Wanted) + TEXT(" in the player's Parameter Collections");
			return Out;
		}
		const FName Param(*Ztrip::ToFString(Parsed->parameter));
		if (!Collection->GetScalarParameterByName(Param))
		{
			Out.Problem = Collection->GetName() + TEXT(" has no scalar parameter ") + Quote(Param.ToString());
			return Out;
		}
		Out.Collection = Collection;
		return Out;
	}

	const FString ActorName = Ztrip::ToFString(Parsed->actor);
	AActor* Actor = FindActor(Player, InWorld, ActorName);
	if (!Actor)
	{
		Out.Problem = TEXT("no actor labelled, named or tagged ") + Quote(ActorName);
		return Out;
	}
	Out.Actor = Actor;
	UActorComponent* Component = nullptr;
	if (!Parsed->component.empty())
	{
		const FString ComponentName = Ztrip::ToFString(Parsed->component);
		Component = FindComponent(Actor, ComponentName);
		if (!Component)
		{
			Out.Problem = ActorName + TEXT(" has no component named ") + Quote(ComponentName);
			return Out;
		}
	}

	switch (Parsed->kind)
	{
	case Kind::Property:
	{
		TArray<UObject*> Candidates;
		if (Component)
		{
			Candidates.Add(Component);
		}
		else
		{
			Candidates.Add(Actor);
			for (UActorComponent* Each : ComponentsOf(Actor))
			{
				Candidates.Add(Each);
			}
		}
		FString FirstWhy;
		for (UObject* Candidate : Candidates)
		{
			FString Why;
			if (FindChain(Candidate, Parsed->path, Out.Chain, Why))
			{
				Out.Object = Candidate;
				Out.Function = FindSetter(Candidate, Out.Chain[0]);
				TArray<FString> Names;
				for (const FProperty* Property : Out.Chain)
				{
					Names.Add(Property->GetName());
				}
				Out.PropertyPath = FString::Join(Names, TEXT("."));
				return Out;
			}
			if (FirstWhy.IsEmpty() || !Why.StartsWith(TEXT("no property")))
			{
				FirstWhy = Why; // prefer "has no fields" over "no property" from other components
			}
		}
		Out.Problem = (Component ? Component->GetName() : ActorName) + TEXT(": ") + FirstWhy;
		Out.Chain.Reset();
		return Out;
	}
	case Kind::Location:
	case Kind::Rotation:
	case Kind::Scale:
	{
		USceneComponent* Scene = Component ? Cast<USceneComponent>(Component) : Actor->GetRootComponent();
		if (!Scene)
		{
			Out.Problem = Component ? Component->GetName() + TEXT(" has no transform") : ActorName + TEXT(" has no root component");
			return Out;
		}
		Out.Object = Scene;
		return Out;
	}
	case Kind::Material:
	{
		UPrimitiveComponent* Primitive = Component ? Cast<UPrimitiveComponent>(Component) : FirstComponent<UPrimitiveComponent>(Actor);
		if (!Primitive)
		{
			Out.Problem = (Component ? Component->GetName() : ActorName) + TEXT(" has no materials");
			return Out;
		}
		if (Parsed->slot >= Primitive->GetNumMaterials())
		{
			Out.Problem = FString::Printf(TEXT("%s has %d material slots"), *Primitive->GetName(), Primitive->GetNumMaterials());
			return Out;
		}
		Out.Object = Primitive;
		return Out;
	}
	case Kind::Niagara:
	{
		UNiagaraComponent* Niagara = Component ? Cast<UNiagaraComponent>(Component) : FirstComponent<UNiagaraComponent>(Actor);
		if (!Niagara)
		{
			Out.Problem = (Component ? Component->GetName() : ActorName) + TEXT(" is not a Niagara system");
			return Out;
		}
		Out.Object = Niagara;
		return Out;
	}
	case Kind::Burst:
	{
		UFXSystemComponent* Fx = Component ? Cast<UFXSystemComponent>(Component) : FirstComponent<UFXSystemComponent>(Actor);
		if (!Fx)
		{
			Out.Problem = (Component ? Component->GetName() : ActorName) + TEXT(" has no Niagara or particle system");
			return Out;
		}
		Out.Object = Fx;
		return Out;
	}
	case Kind::Call:
	{
		UObject* Owner = Component ? static_cast<UObject*>(Component) : static_cast<UObject*>(Actor);
		const FString Name = Ztrip::ToFString(Parsed->parameter);
		UFunction* Function = Owner->FindFunction(FName(*Name));
		if (!Function)
		{
			Out.Problem = Owner->GetName() + TEXT(" has no function ") + Quote(Name);
			return Out;
		}
		const FProperty* Param = Function->NumParms > 0 ? CastField<FProperty>(Function->ChildProperties) : nullptr;
		const bool bTakesNumber = Function->NumParms == 1 && Param && CastField<FNumericProperty>(Param) &&
			!Param->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm);
		if (Function->NumParms != 0 && !bTakesNumber)
		{
			Out.Problem = Name + TEXT(" must take no parameters, or one number");
			return Out;
		}
		Out.Object = Owner;
		Out.Function = Function;
		return Out;
	}
	case Kind::Collection:
		break;
	}
	Out.Problem = TEXT("unsupported target");
	return Out;
}

bool FZtripBinder::ReadValue(const FZtripResolvedTarget& Target, double& Out)
{
	UObject* Object = Target.Object.Get();
	if (!Target.IsOk() || !Object)
	{
		return false;
	}
	switch (Target.Parsed.kind)
	{
	case Kind::Property:
		if (Target.Chain.Num() == 0)
		{
			return false;
		}
		Out = ReadLeaf(Target.Chain.Last(), LeafFrom(Target.Chain, Target.Chain[0]->ContainerPtrToValuePtr<void>(Object)));
		return true;
	case Kind::Location:
	case Kind::Rotation:
	case Kind::Scale:
		Out = ReadTransform(CastChecked<USceneComponent>(Object), Target.Parsed.kind, Target.Parsed.axis);
		return true;
	default:
		return false;
	}
}

// ---- Applying --------------------------------------------------------------------

void FZtripBinder::Bind(const AZtripPlayer& Player, FBinding& Binding)
{
	FZtripResolvedTarget& Target = Binding.Target;
	if (!Target.IsOk())
	{
		return;
	}
	if (Target.Parsed.kind == Kind::Collection)
	{
		UWorld* InWorld = Player.GetWorld();
		UMaterialParameterCollectionInstance* Instance = InWorld ? InWorld->GetParameterCollectionInstance(Target.Collection.Get()) : nullptr;
		float Base = 0.0f;
		if (Instance && Instance->GetScalarParameterValue(FName(*Ztrip::ToFString(Target.Parsed.parameter)), Base))
		{
			Binding.Base = Base;
			Binding.bHasBase = true;
		}
		return;
	}
	if (Target.Parsed.kind == Kind::Material)
	{
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Target.Object.Get());
		if (!Primitive)
		{
			return;
		}
		const FName Param(*Ztrip::ToFString(Target.Parsed.parameter));
		const int32 First = Target.Parsed.slot >= 0 ? Target.Parsed.slot : 0;
		const int32 Last = Target.Parsed.slot >= 0 ? Target.Parsed.slot : Primitive->GetNumMaterials() - 1;
		for (int32 Slot = First; Slot <= Last; ++Slot)
		{
			UMaterialInterface* Material = Primitive->GetMaterial(Slot);
			if (!Material || !HasScalarParameter(Material, Param))
			{
				continue;
			}
			FMaterialSlot Entry;
			Entry.Slot = Slot;
			UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Material);
			if (!Instance)
			{
				Instance = Primitive->CreateAndSetMaterialInstanceDynamic(Slot);
				Entry.Original = Material;
			}
			if (!Instance)
			{
				continue;
			}
			Entry.Instance = Instance;
			Entry.Base = Instance->K2_GetScalarParameterValue(Param);
			Binding.Slots.Add(Entry);
		}
		if (Binding.Slots.Num() == 0)
		{
			Target.Problem = Primitive->GetName() + TEXT(" has no material with a scalar parameter ") + Quote(Param.ToString());
		}
		return;
	}
	double Base = 0.0;
	if (ReadValue(Target, Base))
	{
		Binding.Base = Base;
		Binding.bHasBase = true;
	}
}

void FZtripBinder::Write(FBinding& Binding, double Value)
{
	const FZtripResolvedTarget& Target = Binding.Target;
	switch (Target.Parsed.kind)
	{
	case Kind::Property:
	{
		UObject* Object = Target.Object.Get();
		if (!Object || Target.Chain.Num() == 0)
		{
			return;
		}
		void* TopValue = Target.Chain[0]->ContainerPtrToValuePtr<void>(Object);
		if (Target.Function)
		{
			// Through the setter, so lights, cameras and the like update.
			CallWithValue(Object, Target.Function, [&](FProperty* Param, void* ParamValue)
			{
				Param->CopyCompleteValue(ParamValue, TopValue);
				WriteLeaf(Target.Chain.Last(), LeafFrom(Target.Chain, ParamValue), Value);
			});
		}
		else
		{
			WriteLeaf(Target.Chain.Last(), LeafFrom(Target.Chain, TopValue), Value);
			if (UActorComponent* Component = Cast<UActorComponent>(Object))
			{
				Component->MarkRenderStateDirty();
			}
		}
		return;
	}
	case Kind::Location:
	case Kind::Rotation:
	case Kind::Scale:
		if (USceneComponent* Scene = Cast<USceneComponent>(Target.Object.Get()))
		{
			WriteTransform(Scene, Target.Parsed.kind, Target.Parsed.axis, Value);
		}
		return;
	case Kind::Material:
	{
		const FName Param(*Ztrip::ToFString(Target.Parsed.parameter));
		for (const FMaterialSlot& Slot : Binding.Slots)
		{
			if (UMaterialInstanceDynamic* Instance = Slot.Instance.Get())
			{
				Instance->SetScalarParameterValue(Param, static_cast<float>(Value));
			}
		}
		return;
	}
	case Kind::Collection:
	{
		UWorld* InWorld = World.Get();
		UMaterialParameterCollection* Collection = Target.Collection.Get();
		if (UMaterialParameterCollectionInstance* Instance = InWorld && Collection ? InWorld->GetParameterCollectionInstance(Collection) : nullptr)
		{
			Instance->SetScalarParameterValue(FName(*Ztrip::ToFString(Target.Parsed.parameter)), static_cast<float>(Value));
		}
		return;
	}
	case Kind::Niagara:
		if (UNiagaraComponent* Niagara = Cast<UNiagaraComponent>(Target.Object.Get()))
		{
			// "User.Rate" is set as "Rate"; the component adds the namespace.
			std::string Name = Target.Parsed.parameter.substr(5);
			Niagara->SetVariableFloat(FName(*Ztrip::ToFString(Name)), static_cast<float>(Value));
		}
		return;
	case Kind::Burst:
	{
		const bool bRose = Value >= 0.5 && (!Binding.bHasLast || Binding.Last < 0.5);
		if (bRose)
		{
			if (UFXSystemComponent* Fx = Cast<UFXSystemComponent>(Target.Object.Get()))
			{
				Fx->Activate(true);
			}
		}
		return;
	}
	case Kind::Call:
	{
		UObject* Object = Target.Object.Get();
		if (!Object || !Target.Function)
		{
			return;
		}
		if (Target.Function->NumParms == 0)
		{
			if (Value >= 0.5 && (!Binding.bHasLast || Binding.Last < 0.5))
			{
				CallWithValue(Object, Target.Function, [](FProperty*, void*) {});
			}
		}
		else if (!Binding.bHasLast || Binding.Last != Value)
		{
			CallWithValue(Object, Target.Function, [&](FProperty* Param, void* ParamValue) { WriteLeaf(Param, ParamValue, Value); });
		}
		return;
	}
	}
}

void FZtripBinder::Apply(const AZtripPlayer& Player, const std::vector<ztrip::graph::TargetValue>& Outputs)
{
	if (World.Get() != Player.GetWorld())
	{
		Restore(); // bindings belong to one world
		World = Player.GetWorld();
	}
	for (const ztrip::graph::TargetValue& Output : Outputs)
	{
		const FString Target = Ztrip::ToFString(Output.target);
		int32* Found = Index.Find(Target);
		if (!Found)
		{
			FBinding Binding;
			Binding.Target = Resolve(Player, Target);
			Bind(Player, Binding);
			Found = &Index.Add(Target, Bindings.Add(MoveTemp(Binding)));
		}
		FBinding& Binding = Bindings[*Found];
		if (!Binding.Target.IsOk())
		{
			continue;
		}
		const double Value = Binding.Target.Map(Output.value);
		Write(Binding, Value);
		Binding.Last = Value;
		Binding.bHasLast = true;
	}
}

void FZtripBinder::Restore()
{
	// Newest first: a later binding on the same material slot found the
	// instance an earlier one made, and the earlier one holds the original.
	for (int32 i = Bindings.Num() - 1; i >= 0; --i)
	{
		FBinding& Binding = Bindings[i];
		if (!Binding.Target.IsOk())
		{
			continue;
		}
		if (Binding.Target.Parsed.kind == Kind::Material)
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Binding.Target.Object.Get());
			const FName Param(*Ztrip::ToFString(Binding.Target.Parsed.parameter));
			for (const FMaterialSlot& Slot : Binding.Slots)
			{
				if (Primitive && Slot.Original.IsValid())
				{
					Primitive->SetMaterial(Slot.Slot, Slot.Original.Get());
				}
				else if (UMaterialInstanceDynamic* Instance = Slot.Instance.Get())
				{
					Instance->SetScalarParameterValue(Param, Slot.Base);
				}
			}
			continue;
		}
		if (!Binding.bHasBase)
		{
			continue; // Niagara parameters, bursts and calls have nothing to put back
		}
		Write(Binding, Binding.Base);
	}
	Bindings.Reset();
	Index.Reset();
}

int32 FZtripBinder::BoundCount() const
{
	int32 Count = 0;
	for (const FBinding& Binding : Bindings)
	{
		Count += Binding.Target.IsOk() ? 1 : 0;
	}
	return Count;
}

void FZtripBinder::ForEachTarget(TFunctionRef<void(const FZtripResolvedTarget&)> Visit) const
{
	for (const FBinding& Binding : Bindings)
	{
		Visit(Binding.Target);
	}
}

TArray<FString> FZtripBinder::Problems() const
{
	TArray<FString> Out;
	for (const FBinding& Binding : Bindings)
	{
		if (!Binding.Target.IsOk())
		{
			Out.Add(Binding.Target.Target + TEXT(": ") + Binding.Target.Problem);
		}
	}
	return Out;
}
