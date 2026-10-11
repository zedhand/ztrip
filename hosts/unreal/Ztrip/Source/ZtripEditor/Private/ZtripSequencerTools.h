// Level Sequence tools behind the player's editor buttons.
#pragma once

#include "CoreMinimal.h"

class AZtripPlayer;

namespace ZtripSequencer
{
	// Puts the song in the player's Level Sequence: an audio track with the
	// player's Sound, a Song Time track from 0 to the song's end (so the
	// level follows the sequence when it plays or scrubs), the playback range
	// and a marker on every bar of the song's tempo map. Song time 0 sits at
	// the start of the playback range.
	void SetUp(AZtripPlayer& Player);

	// Evaluates the graph at the sequence's display rate over the bake range
	// and keys every target that a Level Sequence can animate natively:
	// float and double properties of actors and components, and transforms.
	// The keys play without ztrip installed.
	void Bake(AZtripPlayer& Player);
}
