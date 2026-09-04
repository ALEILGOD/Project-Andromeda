# Beyond The Skies (BTS) — Regole permanenti del workspace

> Regole permanenti di progetto per lo sviluppo di **Beyond The Skies (BTS)**.
> Queste regole hanno precedenza sulle abitudini generiche quando il task riguarda questo workspace.

## Identità del progetto

- **Project name:** Beyond The Skies (BTS).
- **Unreal Engine version:** 5.8.2.
- **Unreal module:** `Andromeda`.
- Il progetto è sviluppato principalmente in **C++ con Unreal Engine** (UE 5.8, C++20).

## Terminologia

- **PCS** significa **Planet Creation System**.
- **PBS** significa **Planet Biome System**.
- Il **PCS** e il **PBS** sono **sistemi distinti**:
  - PCS = creazione/forme del pianeta (continenti, landform, orogenia, terreno).
  - PBS = classificazione dei biomi e dei dati climatici di superficie.

## Separazione dei sistemi

- Quando il task riguarda il **PBS**, **non modificare il PCS** per risolvere problemi del PBS, salvo esplicita autorizzazione.
- Quando il task riguarda il **PCS**, **non modificare il PBS** salvo necessità esplicita.
- Mantenere una chiara separazione tra:
  - generazione della geometria;
  - classificazione dei biomi (PBS);
  - rappresentazione dei dati;
  - rendering / materiale.
- Non confondere un problema del **materiale** con un problema del **PBS** o del **PCS**.
- Non confondere un problema del **PBS** con un problema della **generazione del terreno**.
- Per problemi visivi distinguere sempre tra: geometria, dati procedurali, materiali/shader, illuminazione e rendering **prima** di modificare il codice.

## Processo di lavoro

Regola fondamentale:
**Analizza → pianifica → modifica → compila/testa → verifica il risultato.**

- Prima di modificare codice esistente, **analizzare la pipeline e le dipendenze coinvolte**.
- Prima di modificare file importanti, spiegare brevemente cosa si intende cambiare e perché, a meno che il task sia già perfettamente specificato.
- Non considerare una modifica riuscita solo perché compila: **verificare anche il comportamento runtime** quando possibile.

## Stile e portata delle modifiche

- Preferire modifiche **mirate e incrementali** invece di grandi refactor.
- Preferire l'**implementazione più semplice** che rispetti l'architettura esistente.
- Non creare architetture parallele che duplicano sistemi già esistenti senza una ragione verificata.
- Quando esiste una soluzione già presente nel progetto, **preferirla** a una nuova implementazione duplicata.
- Non modificare file solo per "pulire" o rifattorizzare codice non coinvolto nel task.
- Non introdurre **dipendenze esterne** senza autorizzazione.
- Se una modifica può avere effetti su altri sistemi, **verificarli prima** di applicarla.
- Se una soluzione proposta contraddice l'architettura esistente, **fermarsi e segnalarlo** prima di procedere.

## Determinismo e architettura procedurale

- **Preservare il determinismo della generazione procedurale:** stesso seed + stessi parametri devono produrre lo stesso risultato.
- Il seed system e le funzioni procedurali devono rimanere **coerenti con l'architettura esistente**.
- La qualità visiva e la coerenza procedurale sono **priorità**: non sacrificare l'architettura procedurale per ottenere rapidamente un risultato visivo temporaneo.

## Convenzioni e compilazione

- Il codice Unreal deve rispettare le **convenzioni C++/UE5 già utilizzate dal progetto**.
- Quando viene modificato C++, fornire/modificare file completi quando necessario e **compilare il progetto dopo modifiche significative**.
- In caso di errori di compilazione, **analizzare l'errore reale** prima di proporre workaround casuali.
- Quando un'API Unreal Engine, C++ o HLSL/USF non è certa, **verificare la documentazione o cercare informazioni aggiornate** invece di inventare API o firme.
- Per HLSL/USF seguire **l'architettura shader di Unreal Engine 5.8.2**.
- Non modificare i sorgenti dell'Unreal Engine installato localmente (`C:\Unreal Engine\UE_5.8`) salvo esplicita autorizzazione.

## Stato del codice

- **Distinguere sempre** tra:
  - codice attivo;
  - codice sperimentale;
  - codice archiviato;
  - codice temporaneo/debug.
- Non assumere che un asset o file di debug rappresenti l'architettura definitiva.
- Non eliminare sistemi o file esistenti senza prima **verificare le loro dipendenze** e senza autorizzazione quando l'eliminazione è distruttiva.

## Git e operazioni di sistema

- Non eseguire **operazioni Git distruttive** (reset, checkout distruttivo, clean, force operations, ecc.) senza esplicita autorizzazione.
- È consentito usare: terminale, build, test, ricerca nel repository e strumenti web/documentazione per **verificare il comportamento del progetto**.

## Ambito architettonico

- **BTS NON utilizza il DL System:** il DL System appartiene a League of Times e non deve essere introdotto o considerato parte dell'architettura BTS.
- Fanno parte dell'architettura BTS: sistema stellare, generazione dei pianeti, generazione procedurale del terreno, **PCS**, **PBS**, atmosfera, illuminazione planetaria e sistemi correlati.