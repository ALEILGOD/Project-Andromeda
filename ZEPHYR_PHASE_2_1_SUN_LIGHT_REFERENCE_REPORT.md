# ZEPHYR PHASE 2.1 — SUN LIGHT REFERENCE REPORT

**Data:** 2026-09-14
**Task:** Atmosphere Light Reference fix (sole come source of truth)
**Target:** Andromeda.uproject (UE 5.8)

---

## 1. Root cause confermata

`ASun` era solo una posizione (spawn a `StarSystem` center, `StarSystem.cpp:94-132`).
L'atmosfera ricostruiva `SunDirWorld = (Star − Center)` (convenzione A, corretta)
ma in `ZephyrRenderer.cpp:1831` la ruotava con `PlanetRot.Inverse()` in planet
frame, mentre TUTTA la geometria shader restava world frame. Il pattern
giorno/notte + disco solare risultavano ruotati dello spin planetario inverso
(a ~180°: "sole invertito"; sempre: disaccordo con l'illuminazione terreno, che
usa la direzione world non ruotata).

Precisazione Phase 1: la posizione sorgente NON era sbagliata (StarSystem
location ≡ Sun actor location, entrambi al centro, statici). Il bug era
esclusivamente la conversione di frame. Nessuna differenza trovata rispetto
all'analisi pre-implementazione.

## 2. Convenzione precedente

`SunDirWorld = (StarWorldPosition − PlanetCenter).GetSafeNormal()` → A
(Planet→Sun), poi `PlanetRot.Inverse().RotateVector(...)` → planet frame in
Packed6. Ricostruzione autonoma del renderer, senza proprietario.

## 3. Frame precedente di `SunDir`

Planet Space (solo Packed6; tutto il resto world).

## 4. Frame di `Up`

World Space, sempre: pixel `Up = −RelCenter/CamDist` (`ZephyrSky.usf:363`);
bake `UpCam = −RelCenter/CamDistCm` (`ZephyrSkyView.usf:98`).

## 5. Frame di `RayDir`

World Space, sempre: ricostruito da InvViewProjection world-stabilizzata in
entrambi gli shader; marcia `Pc = ViewDir·t` contro `RelCenter` world nel bake.

## 6. Perché `PlanetRot.Inverse()` era errato in quel punto

Sole Case A direzionale + shell a simmetria sferica + camera/geometria world ⇒
il cielo è invariante sotto spin planetario; la rotazione va applicata ZERO
volte sul path solare (continua a pilotare la mesh terreno, intatta). La
convenzione `CurrentRotation = TiltQuat·SpinQuat` (`StarSystem.cpp:975-1044`)
è corretta e NON è stata cambiata. Non è un problema di segno: nessuno
`*= −1` è stato introdotto da nessuna parte.

## 7. Nuovo `UAtmosphereLightReferenceComponent`

`Public/Atmosphere/AtmosphereLightReferenceComponent.h` +
`Private/Atmosphere/AtmosphereLightReferenceComponent.cpp` (nuovi).
`USceneComponent` su `ASun` ("AtmosphereLightReference", identity, no tick):
`GetEmissionPointWorld()`, `GetDirectionTowardSunWorld()` (A),
`GetLightTravelDirectionWorld()` (= −A, derivata qui una sola volta),
`IsReferenceValid()`, `GetReferenceSummary()`, choke point statico
`ComputeDirectionTowardSunWorld()` (safe-normal + fallback (0,0,1) anti-NaN).
Nessuna luce, nessuna simulazione, nessuno STARMAP duplicato.

## 8. Nuova source of truth

```text
ASun → AtmosphereLightReference → SunDirectionWorld (per planet)
→ FZephyrPlanetSnapshotEntry → mailbox → Packed6 → GPU
```

Il renderer non ricostruisce più nulla (grep: zero `Inverse().RotateVector`,
zero `Star−Center` nel path di render; resta solo il teleport diagnostico
`ZephyrSubstellarUp`, già in convenzione A e ora coerente).

## 9. API World Space

Entrambe le API documentano frame world + enum
`EAtmosphereLightDirectionConvention { TowardSun, LightTravel }`.
Il render thread non tocca mai UObject (bake nel Registry, game thread).

## 10. Snapshot integration

`FZephyrPlanetSnapshotEntry::SunDirectionWorld` (nuovo UPROPERTY, normalizzato e
validato al bake). Registry: `ResolveSunLightReference()` (cache via
`AStarSystem::GetSunActor()`, MAI discovery globale) + bake per pianeta +
emission point autoritativo nello snapshot; fallback documentato (StarSystem
location, log una tantum) se il Sun/reference manca.

## 11. GPU integration

`BuildGPUData`: Packed6 = snapshot verbatim (+ guardia legacy via owned helper
e fallback (0,0,1) anti-NaN da normalize shader). Nomi campi `SunDirPlanet*`
conservati per non toccare la matematica Hillaire; semantica aggiornata nei
commenti (CPU + `ZephyrCommon.ush`). Effetto collaterale positivo: lo spin non
agita più la view-key (meno rebake LUT).

## 12. Shader matematicamente inalterati

`ZephyrSky.usf`, `ZephyrSkyView.usf`, `ZephyrCommon.ush`: SOLO commenti di
contratto frame (`SunDir/Up/RayDir` world; `mu/dot` invariati; Rayleigh/Mie/
occultazione/disco/transmittance/SkyView/dual-scatter/Case A intatti).

## 13. Multi-planet behavior

Direzione ricalcolata per centro pianeta (parallasse conservata), nessun
hardcode globale (confermato: nessuna uniform globale sole), profili/raggi/
rotazioni/orbite/governing invariati.

## 14. Terrain consistency

`PlanetaryLightingComponent.cpp:81` (`ToStar`, world, non ruotato) intatto:
terreno e atmosfera ora leggono la stessa direzione fisica (stessa stella,
stessa convenzione A, stesso frame). Nessuna duplicazione nel terreno.

## 15. Debug command

`DebugMode 3` esteso (dir baked esatte = contenuto Packed6 + centro +
rotazione solo-informativa + governing + frame dichiarato);
nuovo `r.AndromedaAtmosphere.SunReference` (attore, emission vs mailbox star,
convention, check UNIT per pianeta). Namespace unificato, no spam (comandi
manuali + log once).

## 16. Build result

```text
Build.bat AndromedaEditor Win64 Development … -waitmutex
Result: Succeeded — 0 errors (UHT 9 file, 21 azioni)
```

Unico warning: deprecazione `UTexture::GetAssetRegistryTags` in header engine
(pre-esistente, non correlato). Prova binaria: `r.AndromedaAtmosphere.
SunReference`, `AtmosphereLightReference`, `GetDirectionTowardSunWorld`,
`SunDirectionWorld`, `GetSunActor` PRESENTI nel DLL linkato (03:20).

## 17. Runtime result

PIE non eseguibile in ambiente headless: `Validate`/`Status`/`SunReference`
verificati a livello di registrazione binaria, NON di output visivo.
Protocollo pronto per l'utente (punto 18-19). Task implementativa completa;
validazione visiva pendente.

## 18. Alba/tramonto result

Pendente PIE. Atteso dal frame unificato: alba sul bordo verso il Sole
(`mu_s` e disco ora concordi col PointLight del terreno).

## 19. Rotazione result

Pendente PIE. Atteso: spin/tilt non spostano più cielo/sole (verifica con
`DebugMode 3` a rotazioni diverse: dir baked costanti a pianeta fermo in
orbita).

## 20. Problemi residui (indipendenti, NON toccati per spec)

1. Cielo nero per scala radiometrica (`ZEPHYR_RADIANCE_SCALE` inutilizzato,
   `Exposure` 1.0) — diagnosi separata esistente; maschera la verifica
   visiva di QUESTA fix (consiglio: alzare temporaneamente `Exposure` solo
   come controllo, senza committarlo come fix).
2. Componente terreno ancora istanziato (`Planet.cpp:64`, deprecato).
3. Nomi campi `SunDirPlanet*` legacy (solo nomi).
4. Rilievi oltre `TerrainHeight` analitico vs bake buried (caso limite noto).
