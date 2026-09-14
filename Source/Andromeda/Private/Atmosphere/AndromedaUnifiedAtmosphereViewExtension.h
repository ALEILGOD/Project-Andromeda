#pragma once

// =========================================================
// UNIFIED ATMOSPHERE VIEW EXTENSION (PHASE 2.1)
// =========================================================
// The ONLY scene view extension of the atmosphere system.
// Owns the single Tonemap subscription and dispatches the two
// ordered stages (Aerial -> Sky) of FUnifiedAtmosphereRenderer.
//
// The legacy extensions (FAndromedaAtmosphereViewExtension,
// FZephyrViewExtension) are DEPRECATED: their Register() entry
// points are no-ops and must not be called anymore.
// =========================================================

class ANDROMEDA_API FUnifiedAtmosphereViewExtension
{
public:
    static void Register();
    static void Unregister();
    static bool IsRegistered();
};
