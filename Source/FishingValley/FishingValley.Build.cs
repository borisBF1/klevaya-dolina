using UnrealBuildTool;

public class FishingValley : ModuleRules
{
	public FishingValley(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Every .cpp keeps file-local helpers in (anonymous) namespaces with common names: never merge them.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"Json",
			"JsonUtilities",
			"ImageWrapper",
			"ApplicationCore",
			"HTTP"
		});
	}
}
