// FString <-> UTF-8 std::string and filesystem paths, for calls into the
// engine-free ztrip core.
#pragma once

#include "CoreMinimal.h"

#include "ztrip/host/analysis_cache.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace Ztrip
{
	inline std::string ToStd(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		return std::string(reinterpret_cast<const char*>(Utf8.Get()), static_cast<size_t>(Utf8.Length()));
	}

	inline FString ToFString(std::string_view Text)
	{
		const FUTF8ToTCHAR Wide(reinterpret_cast<const ANSICHAR*>(Text.data()), static_cast<int32>(Text.size()));
		FString Out;
		Out.AppendChars(Wide.Get(), Wide.Length());
		return Out;
	}

	inline std::filesystem::path ToPath(const FString& Path)
	{
		return ztrip::host::path_from_utf8(ToStd(Path));
	}
}
