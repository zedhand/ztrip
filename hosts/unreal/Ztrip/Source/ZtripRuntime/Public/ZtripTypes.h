#pragma once

#include "CoreMinimal.h"

#include "ZtripTypes.generated.h"

// Sends a graph target somewhere else, optionally scaled: lets a graph made
// for another scene (or for Godot, in metres) drive this one unchanged. For
// example From "Camera:position:x", To "CineCamera:location.y", Scale 100.
USTRUCT(BlueprintType)
struct ZTRIPRUNTIME_API FZtripTargetRemap
{
	GENERATED_BODY()

	// The target as the graph names it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ztrip")
	FString From;

	// The Unreal target to drive instead, e.g. "KeyLight:Intensity".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ztrip")
	FString To;

	// The value written is Value * Scale + Offset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ztrip")
	float Scale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ztrip")
	float Offset = 0.0f;
};
