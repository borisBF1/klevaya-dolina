using UnrealBuildTool;
using System.Collections.Generic;

public class FishingValleyTarget : TargetRules
{
	public FishingValleyTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("FishingValley");
	}
}
