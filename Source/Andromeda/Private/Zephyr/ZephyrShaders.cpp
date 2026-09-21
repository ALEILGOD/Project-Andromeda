#include "Zephyr/ZephyrShaders.h"

IMPLEMENT_GLOBAL_SHADER(
	FZephyrPresentationCS,
	"/Andromeda/Zephyr/ZephyrPresentation.usf",
	"MainCS",
	SF_Compute);

bool FZephyrPresentationCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return true;
}
