# ZEPHYR PHASE 2.1 — RUNTIME SKY DIAGNOSIS (DebugMode 0 black / DebugMode 5 visible)

**Data:** 2026-09-14
**Stato PIE riportato:** tutti i proof YES (7 istanze, snapshot, star, GPU, LUT,
SkyView, aerial, sky, composite, governing 0)
**Sintomo:** unified `DebugMode 0` → nessun cielo visibile;
unified `DebugMode 5` → metà schermo con gradiente tipo alba.
**Metodo:** sola analisi statica del path (nessuna modifica al codice).

> Nota mapping (fondamentale per leggere il sintomo): unified `DebugMode 5`
> → `SetStageDebugMode(1)` → **Zephyr `DebugMode == 1` = raw Transmittance LUT**
> (`ZephyrSky.usf:434-444`), NON il cielo. Unified `DebugMode 0` → Zephyr
> `DebugMode == 0` = cielo normale (`ZephyrSky.usf:494-497`).

---

## Observed behavior

1. `DebugMode 0`: sky pixels senza cielo (nero/spazio invariato) nonostante
   `GetSkyPassCount > 0` e `GetLUTRegenCount > 0`.
2. `DebugMode 5` (= Zephyr mode 1): su ~metà schermo appare il contenuto grezzo
   della Transmittance LUT (asse X = coseno zenitale sole −1..+1, asse Y =
   altezza²): metà scura (lato notte, T≈0) + metà luminosa con gradiente
   (lato giorno) → aspetto "alba". L'altra metà è geometria con depth
   (passthrough) e/o raggi deep-space-miss (solo sole).
3. Geometria attesa più probabile: vista vicino alla superficie / transition zone
   (metà terreno con depth, metà cielo), pianeta governante 0.

## Confirmed working stages

| Stadio | Prova (codice + sintomo) |
|--------|--------------------------|
| Snapshot → GPU data (planet 0 valido) | `BuildGPUData` (`ZephyrRenderer.cpp:1749+`); mode 5 campiona la slice 0 della Transmittance e mostra un gradiente pulito giorno/notte → slice, binding SRV, dati GPU del pianeta 0 validi |
| Transmittance LUT bake | Visibile in mode 5 → bake 40-step + resource + binding `TransmittanceLUT` (`ZephyrRenderer.cpp:2472`) validi |
| Governing planet selection | Mode 5 usa `GoverningSlice` (`ZephyrSky.usf:439-441`); se fosse −1 si ritornerebbe a riga 322. LUT visibile ⇒ governing trovato |
| Ray reconstruction / UV / viewport | `UV = SvPosition/ViewportSize`, `ViewportSize = Output.ViewRect.Size()` (`ZephyrRenderer.cpp:2431`); mode 5 mappa correttamente ⇒ ray, UV, viewport, fullscreen dispatch ok |
| Sky pixel pass → Tonemap → schermo | Mode 5 arriva allo schermo ⇒ output texture, `FScreenPassTexture(Output.Texture, Output.ViewRect)` (`ZephyrRenderer.cpp:2546`), chaining Aerial→Sky, hook Tonemap: tutto funzionante |
| Depth test | Identico nei due modi (`ZephyrSky.usf:138-170, 355-359`); i pixel che mostrano la LUT in mode 5 hanno `bHasDepth == false` anche in mode 0 |
| Blend/alpha/render target | `OutColor = float4(..., 1.0f)`, pass opaque, nessun blend; provato dal mode 5 |
| Early return / star / planet count | `StarValid/PlanetCount` (`ZephyrSky.usf:86`), ray guards (112/123), ground-hit (297), limb gate (306), deep-space (322), sky mask (355): **tutti condivisi** fra mode 0 e mode 5. L'unica divergenza per lo stesso pixel sono le righe 434-444 vs 494-497 |

## Exact failing stage

**Il calcolo della radianza subirà da SkyView LUT in `DebugMode == 0`:**

```hlsl
// ZephyrSky.usf:389-394 — il campionamento AVVIENE davvero:
float3 SkySS = ZephyrSampleSkySlice(SkySingleLUT, ... Azimuth01, VCoord, GoverningSlice, ...);
float3 SkyMS = ZephyrSampleSkySlice(SkyMultiLUT,   ... Azimuth01, VCoord, GoverningSlice, ...);
...
// ZephyrSky.usf:496-497 — e SOVRASCRIVE il pixel:
float3 SkyRadiance = SkySS + SkyMS + SunDisk;
OutColor = float4(SkyRadiance * Exposure, 1.0f);
```

SkyView LUT: 192×(112×N) (`SkyViewWidth/SliceHeight`, N = ActiveCount = 7 ⇒
192×784), slice = `GoverningSlice`, U = azimuth/π su 192 texel, V = elevation
horizon-concentrated nella slice (`ZephyrCommon.ush:430-448`) — matematica
coerente con il bake (`ZephyrSkyView.usf:90-94`). Risorsa valida (creata o da
history-cache con `check` a `ZephyrRenderer.cpp:2515-2518`, regen confermata).
**Il valore campionato è però ~10⁻²** (vedi Root cause), quindi dopo
`× Exposure (1.0)` e tonemap UE il pixel è nero.

## Root cause

**Il guadagno display `ZEPHYR_RADIANCE_SCALE` (24.0) è definito ma applicato
in NESSUN punto del pipeline.**

Catena dell'evidenza:

1. `ZephyrCommon.ush:63`: `#define ZEPHYR_RADIANCE_SCALE 24.0f`, con nota di
   calibrazione dell'autore: *"With physically normalized phases, 8.0 leaves
   even an Earth-column sky at ~0.1 display (near-black); 24.0 restores the
   intended mapping"*.
2. `ZephyrSkyView.usf:213-216`: *"Output LINEAR radiance (no
   ZEPHYR_RADIANCE_SCALE here). Exposure is applied in the final pixel
   shader."* — il bake scrive `SSAccum/MSAccum` puri.
3. Pixel shader (`ZephyrSky.usf:496-497`): applica solo `Exposure` =
   `r.AndromedaZephyr.Exposure` = **1.0** (`ZephyrRenderer.cpp:85-90`).
4. `grep ZEPHYR_RADIANCE_SCALE` su tutti gli shader: **definizione + 1 commento,
   zero usi**. Il 24× non vive da nessuna parte.
5. Aritmetica degli ordini di grandezza (colonna Earth-like):
   σ_R≈10⁻² km⁻¹ × fase normalizzata 3/(16π)·(1+cos²)≈10⁻¹ × path decine di km
   ⇒ `SSAccum` ≈ 10⁻². La nota dell'autore prevede ~0.1 già a scala 8
   ("near-black"); a scala effettiva **1.0** ⇒ ~0.01 ⇒ nero dopo tonemap.
6. Perché mode 5 funziona: la Transmittance è 0..1 per costruzione, non ha
   bisogno di scala ⇒ visibile. Questo isola il guasto **esattamente** al
   fattore di scala della radianza, non a snapshot/LUT/binding/UV/depth/hook.

Cause secondarie / concomitanti (ordinate per probabilità):

| # | Ipotesi | Probabilità | Note |
|---|---------|-------------|------|
| 1 | **Scala 24× mancante** (sopra) | **~85%** | Spiega tutto da sola; confermata da define-inutilizzato + nota autore + aritmetica + isolamento mode5/mode0 |
| 2 | **Overwrite che maschera lo stage aerial** — l'aerial (fase Rayleigh NON normalizzata × `ATMOS_RADIANCE_SCALE 8.0`, `AndromedaAtmosphere.usf:912`) produce per costruzione un cielo O(1) sui pixel sky, ma lo sky stage gira dopo e lo sovrascrive col suo ~nero | Contribuente, non root | By-design (single composite); sparisce da sola con la fix di #1. Discriminatore runtime: `r.AndromedaZephyr.Enable 0` → se appare il cielo Rayleigh-only, aerial ok e maschera confermata |
| 3 | Gate `AtmosEnabled` sul limb da spazio (`ZephyrSky.usf:306-310`) | Bassa per questo sintomo | Colpisce solo i raggi limb deep-space; non spiega cielo mancante in inside/transition, dove i pixel mostrano la LUT in mode 5 |
| 4 | Slice/UV/viewport/dispatch/dimensioni | **Esclusa** | Stessa matematica e stessi binding del mode 5 funzionante |
| 5 | Binding null / cache invalida / LUT nera | **Esclusa come primaria** | `check` + regen counter + LUT transmittance valida; il bake SkyView accumula J1>0 sul lato giorno per costruzione (σ, densità, fasi, T★ tutti non-zero dai profili) |
| 6 | Alpha/blend/write-mask / scene-color misuse / doppio tonemap | **Esclusa** | Mode 5 prova l'intero output path; `SunLuminance 20` e Transmittance 0..1 passano correttamente |

Risposte puntuali ai quesiti 7–16:

- **(7) Ordine:** un hook Tonemap, delegati Aerial→Sky in subscription order; chaining UE deterministico. Corretto.
- **(8)(9) Render target:** `Output` = `OverrideOutput` o nuova texture `ZephyrSkyOutput`, `ELoad`/`ENoAction` coerenti; `SceneColorTexture` = output dello stage precedente; return propagato. Nessun misuse; provato dal mode 5.
- **(10) Depth:** reversed-Z, sky ⇔ `SceneDepthDeviceZ ≤ 1e-7`; identico nei due modi.
- **(11) Governing 0:** dati GPU validi (gradiente transmittance pulito sulla slice 0).
- **(12) Early return mode-0:** nessuno oltre quelli condivisi col mode 5.
- **(13) Alpha/blend:** opaque, alpha 1. Escluso.
- **(14) Metà schermo in mode 5:** NON bug UV — il debug Transmittance plotta `LUT(UV.x→Mu, UV.y→H)` su ogni pixel senza depth con governing; l'altra metà ha depth (terreno) o è deep-space-miss. Geometria di vista, non bug.
- **(15) Viewport/dispatch:** `ViewRect.Size()`, dispatch SkyView (24, 14·N, 1), LUT 192×(112·N). Coerenti.
- **(16) Campionamento in mode 0:** SÌ, righe 389-394 — il problema è il VALORE (~10⁻²), non il campionamento.

## Evidence

- `Shaders/Andromeda/Zephyr/ZephyrCommon.ush:48-63` — scala 24.0 documentata, mai usata.
- `Shaders/Andromeda/Zephyr/ZephyrSkyView.usf:213-216` — bake senza scala.
- `Shaders/Andromeda/Zephyr/ZephyrSky.usf:494-497` — composite con solo `Exposure`.
- `Source/Andromeda/Private/Planet/Zephyr/ZephyrRenderer.cpp:85-90` — `Exposure` default 1.0.
- `grep ZEPHYR_RADIANCE_SCALE` → 1 define + 1 commento, 0 usi computazionali.
- Sintomo mode 5 (funzionante) vs mode 0 (nero) a parità di path fino a riga 433.

## Proposed minimal fix (NON applicata — in attesa di decisione)

**Opzione A (raccomandata, 2 righe shader, intento dell'autore):**
in `ZephyrSkyView.usf:215-216` scrivere
`SSAccum/MSAccum × ZEPHYR_RADIANCE_SCALE`. La J2 interna resta pre-scala
(coerente: la MS LUT alimenta solo il bake); Transmittance (0..1), SunDisk
(`SunLuminance`) e debug Transmittance/Absorption restano intoccati.

**Opzione B (equivalente, pixel shader):** in `ZephyrSky.usf:496` usare
`(SkySS + SkyMS) × ZEPHYR_RADIANCE_SCALE + SunDisk` — stesso effetto, più righe.

**NON raccomandata:** alzare il default di `r.AndromedaZephyr.Exposure` a 24 —
moltiplicherebbe anche SunDisk (20→480, sole distrutto) e i debug LUT grezzi
(Transmittance×24 = bianco clippato).

**Discriminatori runtime da 10 secondi (zero modifiche):**

1. `r.AndromedaZephyr.Exposure 24` → se il cielo appare (slavato ma visibile),
   causa #1 confermata definitivamente.
2. `r.AndromedaZephyr.Enable 0` → se appare il cielo Rayleigh-only dell'aerial,
   causa #2 (mascheramento) confermata e aerial assolto.

**Follow-up diagnostico (non bloccante):** anche i debug Zephyr 4/5
(Mie/Rayleigh diagnostici, gain ad-hoc 0.25/2.0 su σ≈10⁻²–10⁻³) sono
sotto-scala di ~100× e appariranno neri; dopo la fix del cielo, allinearne i
gain se devono restare utili.
