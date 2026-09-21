#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

// ZEPHYR module-wide log category. Deliberately NOT shared with the ATMOS
// plugin's LogHillaireAtmosphere to avoid duplicate-category definition
// across module boundaries.
DECLARE_LOG_CATEGORY_EXTERN(LogZephyr, Log, All);