// Generic property binder: writes graph outputs to properties of actors and
// components through reflection (FProperty), plus adapters for transforms,
// dynamic material parameters, Material Parameter Collections, Niagara user
// parameters, particle bursts and function calls.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

#include "ztrip/graph/evaluator.hpp"
#include "ztrip/host/unreal_target.hpp"

#include <vector>

class AActor;
class FProperty;
class AZtripPlayer;
class UActorComponent;
class UFunction;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UPrimitiveComponent;
class UWorld;

// What one graph target resolved to. The binder writes through it; the
// editor's bake reads it to find what to key.
struct ZTRIPRUNTIME_API FZtripResolvedTarget
{
	FString Target;   // as the graph names it
	FString Resolved; // after the player's target map
	FString Problem;  // why it didn't resolve, or empty
	ztrip::host::UnrealTarget Parsed;
	double Scale = 1.0;
	double Offset = 0.0;

	TWeakObjectPtr<AActor> Actor;
	// The object written: the property's owner (actor or component), the
	// scene component of a transform, the primitive of a material, the
	// Niagara or particle component, or the function's owner.
	TWeakObjectPtr<UObject> Object;
	// Property: the top-level property, then struct fields down to the leaf.
	TArray<FProperty*> Chain;
	FString PropertyPath; // Property: "LightColor.R"
	// Property: a Set<Property> function taking the top-level value, called
	// instead of writing the memory so the engine sees the change. Call: the
	// function to call.
	UFunction* Function = nullptr;
	TWeakObjectPtr<UMaterialParameterCollection> Collection;

	bool IsOk() const { return Problem.IsEmpty(); }
	double Map(double Value) const { return Value * Scale + Offset; }
};

class ZTRIPRUNTIME_API FZtripBinder
{
public:
	// Finds what a target names in the player's world.
	static FZtripResolvedTarget Resolve(const AZtripPlayer& Player, const FString& Target);

	// Reads a resolved Property or transform target's current value.
	static bool ReadValue(const FZtripResolvedTarget& Target, double& Out);

	// Writes each output, binding targets the first time they are seen and
	// remembering the value they had then.
	void Apply(const AZtripPlayer& Player, const std::vector<ztrip::graph::TargetValue>& Outputs);

	// Writes the remembered values back and forgets every binding, so the
	// next Apply() binds afresh.
	void Restore();

	int32 BoundCount() const;
	void ForEachTarget(TFunctionRef<void(const FZtripResolvedTarget&)> Visit) const;
	// One line per target that couldn't be bound.
	TArray<FString> Problems() const;

private:
	struct FMaterialSlot
	{
		int32 Slot = 0;
		TWeakObjectPtr<UMaterialInstanceDynamic> Instance;
		TWeakObjectPtr<UMaterialInterface> Original; // set when the binder made the instance
		float Base = 0.0f;
	};

	struct FBinding
	{
		FZtripResolvedTarget Target;
		bool bHasBase = false;
		double Base = 0.0;
		double Last = 0.0;
		bool bHasLast = false;
		TArray<FMaterialSlot> Slots;
	};

	void Bind(const AZtripPlayer& Player, FBinding& Binding);
	void Write(FBinding& Binding, double Value);

	TWeakObjectPtr<UWorld> World;
	TMap<FString, int32> Index; // target -> Bindings index
	TArray<FBinding> Bindings;  // in first-seen order, restored in reverse
};
