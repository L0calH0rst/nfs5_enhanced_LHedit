// lfsp.asi – LFSP (Love for Speed Porsche, bis v6a nfsp_carparts.asi): Erweiterungen für Need for Speed Porsche.
// Seitenscheiben und Verdeck im Rennen per Taste (Phase 8), Fahrzeug-Seite des Lichts (Phase 9.8 ff.), Messlog.
// Ziel: Porsche.exe v3.5 (TimeDateStamp 0x393C2E8F). Herleitung: doc/FENSTER-VERDECK.md.
// Eigene Inline-Hooks ohne Fremdbibliotheken; jede Hook-Stelle wird vor dem Schreiben gegen ihre Bytes geprüft.

#include <windows.h>
#include <mmsystem.h>

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <intrin.h>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "windows_table.h"
#include "carlimit_table.h"

namespace
{
    // ---- Adressen Porsche.exe v3.5 ----
    constexpr std::uint32_t kExpectedTimestamp = 0x393C2E8F;

    constexpr std::uintptr_t kModelLoad   = 0x441A10; // Modell-vtable Slot 1: Laden(Name)
    constexpr std::uintptr_t kModelUpdate = 0x438440; // Modell-vtable Slot 9: Update je Bild
    constexpr std::uintptr_t kFeSetRoof   = 0x4D1800; // Frontend: Car.Roof setzen
    constexpr std::uintptr_t kBuildList   = 0x447C50; // Teile-Liste aus Varianten-Tabelle bauen
    constexpr std::uintptr_t kPaintList   = 0x448010; // Teile-Liste in Textur-Seiten zeichnen
    constexpr std::uintptr_t kSetAnimSlot = 0x4380D0; // Mesh-Animation: Bild setzen (Modell, Slot, Wert)
    constexpr std::uintptr_t kFeGlobal    = 0x65B928; // != 0 im Frontend
    constexpr std::uintptr_t kBuildGeo    = 0x43C1B0; // Render-Schicht: Geometrie-Teile aus .crp nach Varianten-Tabelle
    constexpr std::uintptr_t kRecreateLayers = 0x4425B0; // Modell: alle Render-Schichten löschen, neu anlegen, aufbauen
    constexpr std::uintptr_t kTrackLod    = 0x450E86; // Strecken-Abschnitt: gewählte Detailstufe -> Zeiger (FUN_00450de0)
    constexpr std::uintptr_t kTrackCutoff = 0x450E7D; // Strecken-Abschnitt: jenseits der letzten Reichweite -> return
    constexpr std::uintptr_t kTrackDist   = 0x450E48; // Strecken-Abschnitt: Entfernung² für den Reichweiten-Vergleich
    constexpr std::uintptr_t kTrackFlags  = 0x450F81; // Strecken-Objekt: Prüfung 0x40000 + Detail-Schalter (sonst verworfen)
    constexpr std::uintptr_t kCrpFind     = 0x59F530; // .crp: erster Eintrag mit Kennung
    constexpr std::uintptr_t kCrpBase     = 0x59F870; // .crp: Anfang der Eintragstabelle
    constexpr std::uint32_t kTagAtri      = 0x41727469; // Kennung der Teile-Einträge
    constexpr std::uint32_t kTagBase      = 0x42617365; // Unter-Eintrag eines Teils: +0x44 Teil, +0x45 Variante
    constexpr std::uintptr_t kCrpChild    = 0x59F7C0; // .crp-Eintrag: Unter-Eintrag mit Kennung
    constexpr std::uintptr_t kKeyBase     = 0x5E5084; // Kennungs-Vorlage (obere 16 Bit) für Unter-Einträge
    constexpr std::size_t kLayerEntrySize = 0x54;     // Listeneintrag: +0 Teil-Eintrag, +0x44 -> Kopf (Byte 9 = Animation)
    constexpr std::uintptr_t kAlloc       = 0x59EFF0; // Speicher der Engine anlegen (cdecl, Größe), wie 0x43C1B0
    constexpr std::uintptr_t kFree        = 0x59F050; // Speicher freigeben (cdecl); FUN_0043e0c0 gibt Eintrag + 0x40 so frei

    // Render-Schicht (vtable 0x5B36AC): +4 Modell, +8 Schicht-Id, +0xC Liste, +0x10 Anzahl

    // Offsets Modell-Objekt
    constexpr std::size_t kModelCar       = 0xF4;  // -> Fahrzeug-Objekt
    constexpr std::size_t kModelListOn    = 0x47C; // Teile-Liste aktiv
    constexpr std::size_t kModelList      = 0x98;
    constexpr std::size_t kModelMesh      = 0x90;
    constexpr std::size_t kModelLodByte   = 0xAA;
    constexpr std::size_t kModelTpages    = 0x78;
    constexpr std::size_t kModelAnimSlots = 0x568; // 8 Mesh-Animationen je 0x14: Bild, nächstes Bild, Anteil, max
    constexpr std::size_t kAnimSlotSize   = 0x14;
    constexpr int kAnimSlotCount = 8;
    constexpr int kAnimSlotRoof  = 1;              // Menü: FUN_004d1990(…, 1, 0..255)
    constexpr float kRoofEndMargin = 0.001f;       // Abstand zum letzten Bild der Verdeck-Animation

    // Offsets Fahrzeug-Objekt
    constexpr std::size_t kCarConfig      = 0x558;
    constexpr std::size_t kCarStyle       = 0x568; // 0-basiert, .tpg [styleN] = Style + 1
    constexpr std::size_t kCarPartTable   = 0x56C; // 120 int: Variante je Teil
    constexpr std::size_t kCarHasRoof     = 0x594;
    constexpr std::size_t kCarColours     = 0x4E4;
    constexpr std::size_t kCarFlags       = 0x52C; // Rennen: Spieler 0x105, Gegner 0xB (Testlauf 8.3)
    constexpr std::size_t kCfgRoof        = 0x510;
    constexpr std::size_t kCarLampBits    = 0xD87; // Bit 0 Lichtschalter, Bit 3/4 Blinker links/rechts (Leuchten-Logik 0x439360)
    constexpr std::size_t kModelTweens    = 0x64;  // -> zeitgesteuerte Kanäle (FUN_00438440)
    constexpr std::size_t kModelPopUpOpen = 0x70;  // Öffnungswinkel der Klappscheinwerfer, 0 = keine
    constexpr std::size_t kModelPopUpState = 0x567; // letzter Lichtschalter, den die Klappen-Logik gesehen hat
    constexpr std::size_t kPopUpTween     = 0x110; // Klappen-Kanal: Startzeit, Dauer, Von, Ziel
    constexpr std::uintptr_t kGameTime    = 0x53BFD0; // Spielzeit in ms (wie in 0x439531)
    constexpr DWORD kPopUpTimeMs          = 1000;  // Dauer des ganzen Wegs wie im Spiel (0x3E8)
    constexpr std::size_t kCarSpecs       = 0x760; // -> carSpecs: Kopie der .sim (0x1C4), vorn der Name "1956 356 A coupe 1.6L"
    constexpr unsigned kCarFlagPlayer     = 0x100; // Annahme aus 8.3, siehe ApplyTo

    constexpr int kPartWindowOuter = 35;
    constexpr int kPartWindowInner = 47;
    constexpr int kPartStaticRoof  = 41; // starres Verdeck (Rennen), im Menü aus
    constexpr int kGeometryParts   = 0x3C; // Tabelle: 0..0x3B Geometrie, ab 0x3C Texturen

    using LoadFn        = int (__thiscall *)(void * model, const char * name);
    using UpdateFn      = void (__thiscall *)(void * model);
    using SetRoofFn     = void (__thiscall *)(void * feCar, int roof);
    using BuildListFn   = void (__thiscall *)(void * list, std::uint32_t * mesh, std::uint32_t * table);
    using PaintListFn   = void (__thiscall *)(void * list, int tpages, int colours);
    using SetAnimSlotFn = void (__thiscall *)(void * model, int slot, float value);
    using BuildGeoFn    = void (__thiscall *)(void * layer, void * crp);
    using CrpFindFn     = unsigned char * (__thiscall *)(void * crp, std::uint32_t tag, int a, int b);
    using CrpBaseFn     = unsigned char * (__thiscall *)(void * crp);
    using CrpChildFn    = unsigned char * (__thiscall *)(void * entry, std::uint32_t tag);
    using AllocFn       = void * (__cdecl *)(std::size_t size);
    using FreeFn        = void (__cdecl *)(void * p);

    LoadFn     g_origLoad     = nullptr;
    UpdateFn   g_origUpdate   = nullptr;
    SetRoofFn  g_origSetRoof  = nullptr;
    BuildGeoFn g_origBuildGeo = nullptr;

    // ---- Konfiguration ----
    struct WindowValues
    {
        int outer = 0;
        int inner = 0;
    };

    int g_measure     = 1;
    int g_windowKey   = 'F';
    int g_roofKey     = 'V';
    int g_roofTimeMs  = 2200;
    int g_rebuildMode = 1;
    int g_rebuildMethod = 1; // 1 = vorhandene Schichten neu befüllen; 2 = neu anlegen (FUN_004425b0, setzt aber die
                             //     Varianten-Tabelle über FUN_0043ae50 zurück, Fenster/Verdeck wirken dann nicht; 8.3d v3m)
    int g_applyTo     = 0; // 0 = im Rennen nur Spielerauto, 1 = alle Autos
    // Geräusche über MCI (WAV und MP3, mehrere gleichzeitig); Dateien relativ zum Plugin-Ordner
    std::string g_soundDir;
    std::string g_roofOpenSound  = "lfsp_roof_open.wav";  // fesfx.bnk Nr. 14
    std::string g_roofCloseSound = "lfsp_roof_close.wav"; // fesfx.bnk Nr. 15
    std::string g_windowSound    = "lfsp_window.wav";
    int g_roofVolume   = 40; // Prozent
    int g_windowVolume = 50;
    int g_roofInMenu  = 0; // 1 = Verdeck-Taste auch im Menü (das Menü setzt am Ende seinen eigenen Stand)
    // Scheiben-Animation (8.3e): beim Öffnen absenken, dann Variante "offen"; beim Schließen umgekehrt
    int g_windowAnimate   = 1;     // 0 = Scheiben ohne Bewegung umschalten
    int g_windowAxis      = 1;     // Achse "oben" der Modelldaten (y, Log v4a)
    int g_windowDownSign  = -1;    // Richtung "unten" auf dieser Achse
    float g_windowDepth   = 1.05f; // Weg in Scheibenhöhen (etwas mehr als 1: Oberkante verschwindet sicher in der Tür)
    int g_windowTimeMs    = 1200;  // Dauer für den ganzen Weg
    int g_windowSideAxis  = 0;     // Querachse (x): Scheibe folgt beim Absenken ihrer Neigung
    float g_windowTilt    = 1.0f;  // Anteil der gemessenen Neigung (0 = senkrecht nach unten)
    float g_windowSink    = 0.33f; // so weit (Scheibenhöhen) darf die Unterkante unter die Brüstung sinken
    float g_windowForward = 0.15f; // Versatz nach vorn (Scheibenhöhen) am Ende des Wegs der Oberkante
    int g_windowForwardSign = -1;  // Richtung "vorn" auf der Längsachse (z; Heckspoiler Boxster bei z +1,9)
    std::map< std::string, WindowValues > g_openValues; // Schlüssel "964.1" = Fahrzeug.[styleN]

    // ---- Zustand je Modell ----
    struct AnimSlotSnapshot
    {
        int frame = -1;
        float frac = 0.0f;
        float max = 0.0f;
    };

    // Ein Geometrie-Teil aus der .crp (Kopf bei Teil + 0x44)
    struct PartInfo
    {
        int part = 0;
        int variant = 0;
        int slot = 0;   // Mesh-Animation (0 = keine)
        int flags = 0;
    };

    struct Layer
    {
        void * layer = nullptr;
        void * crp = nullptr;
        std::uint32_t vtable = 0; // beim Aufbau; ein gelöschtes Objekt hat danach eine andere
    };

    // Eine Scheibe in einer Render-Schicht: Listeneintrag, Positions-Puffer (Eintrag + 0x40), Höhe auf der Achse
    struct ShiftEntry
    {
        unsigned char * entry = nullptr;
        float * buf = nullptr;
        std::uint32_t n = 0;
        float height = 0.0f;
        float slope = 0.0f; // Änderung der Querachse je Einheit auf der Hochachse (Ausgleichsgerade der Vertices)
        float beltA = 0.0f; // Brüstungslinie (Unterkante der geschlossenen Scheibe): hoch = beltA + beltB · längs
        float beltB = 0.0f;
        std::vector< float > orig; // Positionen beim Sammeln (mit etwaigen Schadens-Verformungen)
        bool own = false; // vom Plugin angelegt (sonst vom Spiel für das Schadensmodell)
    };

    struct ModelState
    {
        std::string name;
        std::vector< Layer > layers;
        std::vector< std::pair< int, int > > roofSaved; // Teil, Original-Variante
        float roofStartValue = -1.0f;                    // Stellung nach dem Einschalten (offen/geschlossen)
        int jiggle = 0;                                  // 2: nach Neuaufbau Animationen verstellen, 1: zurückstellen
        float jiggleFrom[8] = {};
        float jiggleTo[8] = {};
        float slotValues[8] = {};
        bool windowsOpen = false;
        bool windowShiftOnly = false; // keine offene Variante ([Windows] fehlt): Scheiben bleiben unten in der Tür
        bool reapplyShift = false;    // nach einem Neuaufbau die abgesenkten Scheiben wieder absenken
        WindowValues closed;
        unsigned windowToggleSeen = 0;
        unsigned roofToggleSeen = 0;
        std::vector< ShiftEntry > shifts; // verschobene Scheiben (Messbuild 8.3e)
        float shiftApplied = 0.0f;        // aktuell angewandter Weg in Scheibenhöhen
        float shiftFrom = 0.0f;
        float shiftTo = 0.0f;
        DWORD shiftStart = 0;
        bool shiftAnimating = false;
        bool roofAnimating = false;
        DWORD roofStart = 0;
        float roofFrom = 0.0f;
        float roofTo = 0.0f;
        AnimSlotSnapshot slots[kAnimSlotCount];
        DWORD slotLogged[kAnimSlotCount] = {};
        int roof = 0x7FFFFFFF;
        int style = -1;
        int t35 = -1;
        int t47 = -1;
        bool seen = false;
        int year = -1;            // Baujahr aus carSpecs (0 = nicht lesbar), -1 = noch nicht gelesen (9.8)
        void * yearCar = nullptr; // Fahrzeug-Objekt, zu dem year gehört
    };

    std::map< void *, ModelState > g_models;
    std::map< std::string, std::vector< PartInfo > > g_catalog;   // je Fahrzeugname: alle Teil/Variante-Paare
    std::map< std::string, std::vector< PartInfo > > g_animParts; // je Fahrzeugname: gebaute Teile mit Mesh-Animation
    std::map< std::string, std::vector< int > > g_feTables;          // "Auto.N": Varianten-Tabelle aus dem Menü
    int g_playerYear = 0; // Baujahr des Spielerautos im Rennen, 0 = unbekannt (Export LfspPlayerCarYear)

    // [Lights] Lichtfarbe nach Baujahr (9.9, doc/LICHT.md §8.3): Stützpunkte Jahr -> Farbe, Helligkeit
    struct YearColour
    {
        int year;
        float rgb[3];
        float strength;
    };
    int g_colourByYear = 1;
    // [World] TrackDetail (B19, doc/BUGS.md §16): 1 = Strecke immer in der feinsten Detailstufe,
    // 2 = zusätzlich ohne Reichweiten-Grenze je Abschnitt
    int g_trackDetail = 1;
    // [World] TrackRange: Faktor auf die Reichweite je Abschnitt (nur TrackDetail = 1); als 1/Faktor² auf die Entfernung²
    float g_trackRange = 1.5f;
    float g_trackDistScale = 1.0f;
    // [World] TrackDiag (B21, doc/BUGS.md §18): 1 = vom Spiel verworfene Strecken-Objekte auf Taste protokollieren,
    // 2 = zusätzlich zeichnen (wie deckende Objekte)
    int g_trackDiag = 0;
    int g_trackDiagKey = 'G';
    // [World] ClipDiag (B21): Dreieck-Clipper von EAGL (0x54C030/0x5498B0) gegenprüfen; verworfene sichtbare Dreiecke ins Log
    int g_clipDiag = 0;
    bool g_guardBand = true; // [World] GuardBand (B15): Verwerfen erst am Guard-Band
    float g_cullRadiusScale = 2.0f; // [World] CullRadiusScale (B15): Kugeltest der Objektauswahl mit größerem Radius

    // B15: FUN_00539480 (thiscall: Kamera, Kugelmitte, Radius) verwirft Objekte, deren Kugel ganz außerhalb des
    // Sichtfelds liegt. Knapp neben dem Bild stehende Objekte spiegeln sich aber ins Bild (Wasserspiegelung, dx11z).
    void * g_sphereOrig = nullptr;
    unsigned __fastcall SphereHook(void * self, void *, const float * centre, float radius)
    {
        using Fn = unsigned(__fastcall *)(void *, void *, const float *, float);
        return reinterpret_cast<Fn>(g_sphereOrig)(self, nullptr, centre, radius * g_cullRadiusScale);
    }
    // B15: FUN_00540820 (thiscall) legt das Clip-Rechteck der Polygone fest (+0x2C/+0x30 min, +0x38/+0x3C max in
    // Bildpunkten) = Bildrand. Gespiegelt endet alles an dieser Kante; erweitert um [World] ClipMargin (Anteil der
    // Bildgröße je Seite) kommen die Teile neben dem Bild bei dx11z an, das sie selbst am Bildrand abschneidet.
    float g_clipMargin = 0.5f;
    void * g_clipRectOrig = nullptr;
    void __fastcall ClipRectHook(unsigned char * self, void *)
    {
        using Fn = void(__fastcall *)(void *, void *);
        reinterpret_cast<Fn>(g_clipRectOrig)(self, nullptr);
        float * min = reinterpret_cast<float *>(self + 0x2C);
        float * max = reinterpret_cast<float *>(self + 0x38);
        for (int i = 0; i < 2; ++i)
        {
            const float extra = (max[i] - min[i]) * g_clipMargin;
            min[i] -= extra;
            max[i] += extra;
        }
    }
    std::vector< YearColour > g_yearColours;

    // [Lights] Fernlicht (9.10, doc/LICHT.md §8.4): Taste gedrückt halten; L ist im Spiel der Lichtschalter.
    int g_highKey = 'K';
    int g_highFadeMs = 120;
    float g_highBeam = 0.0f;   // 0 = Abblendlicht, 1 = Fernlicht, weich überblendet
    DWORD g_highTick = 0;
    bool g_highLogged = false;
    bool g_highDown = false;   // Taste gerade gedrückt (ohne Überblenden)
    // Lichthupe öffnet Klappscheinwerfer (9.10e), ohne den Lichtschalter zu ändern
    int g_flashPopUps = 1;
    int g_signalsInSync = 1; // 11.5f: Blinker hinten (6/7) im Takt der vorderen statt gegenphasig
    int g_thirdBrakeOnly = 1; // 11.5i: mittiges 3. Bremslicht nur beim Bremsen, nicht als Rücklicht bei Licht an
    bool g_highBroken = false; // 11.5i: beide Scheinwerfer des Spielerautos kaputt, kein Fernlicht
    int g_gaugesFollowLights = 1; // HUD-Instrumente folgen dem Lichtschalter (11.3)
    unsigned char * g_flashCar = nullptr; // Auto, dessen Klappen LFSP gerade für die Lichthupe geöffnet hat
    // [Camera] Cinematic (11.1) und Action (11.2), doc/KAMERA.md
    int g_cineInCycle = 1;     // Cinematic als fünfte Ansicht der Kamerataste des Spiels (C)
    int g_cineKey = 0;         // eigene Taste zum Umschalten, 0 = keine
    int g_action = 1;          // Verfolgerkameras reagieren auf die Fahrt
    int g_actionKey = 0;       // Taste: Action an/aus, 0 = keine
    unsigned g_actionCams = (1u << 2) | (1u << 3) | (1u << 4); // Kameratypen mit Action-Verhalten (TailCam0..2)
    float g_actSide = 1.0f;    // Seitversatz in Kurven, Meter bei voller Querbeschleunigung
    float g_actLook = 7.0f;    // Blick in die Kurve, Grad bei voller Querbeschleunigung
    float g_actHeight = 0.25f; // Kamera höher als im Spiel, Meter
    float g_actBrake = 0.8f;   // Heranrücken beim Bremsen, Meter bei voller Verzögerung
    float g_actBounce = 1.0f;  // Mitschwingen bei Sprüngen, Anteil (0 = aus)
    float g_actFov = 0.20f;    // FOV-Zuwachs bei hoher Geschwindigkeit (Anteil)
    float g_actShake = 1.0f;   // Wackeln auf unebenem Grund, Anteil (0 = aus)
    float g_actRoll = 2.5f;    // Rollen zur Kurveninnenseite, Grad bei voller Querbeschleunigung
    float g_actOvershoot = 0.5f; // Schwung beim Einlenken und Nachschwingen (0 = ohne, 2 = stark)
    float g_actBuildUp = 1.2f; // Sekunden, bis der Kurven-Effekt beim Lenken aufgebaut ist
    float g_actSpeedBoost = 0.35f; // Kurven-Effekte bei hohem Tempo stärker (Anteil)
    float g_cockpitFov = 85.0f; // Innenansichten: FOV in Grad (Spiel: 75), 0 = unverändert
    float g_actImpact = 1.0f;  // Stoß-Wackeln bei Aufprall und Landung, Anteil (0 = aus)
    bool g_cineKeyDown = false;
    bool g_actionKeyDown = false;
    unsigned g_windowToggles = 0;
    unsigned g_roofToggles = 0;
    bool g_windowKeyDown = false;
    bool g_roofKeyDown = false;

    // Verkehr (Phase 12): Messung von Anzahl und Modellwahl
    int g_trafficMeasure = 1; // 1 = Rennaufbau, Modell-Lookup und Simulationsautos ins Log
    int g_trafficRandom = 1;  // 1 = Verkehrsmodelle zufällig aus dem Pool statt fest aus der Strecke (i % 7)
    std::vector<std::string> g_trafficPoolNames; // leer = alle Verkehrsmodelle des Spiels
    std::vector<std::string> g_trafficTrackOnly;  // nur auf Strecken, die das Modell selbst eintragen (Schneepflug EAS4)
    int g_trafficCount[4] = { 0, 2, 5, 8 };       // Verkehrsautos je Menüstufe Kein/Leicht/Mittel/Schwer (Spiel: 0/2/5/7)
    int g_trafficActive = 5;                      // höchstens gleichzeitig fahrende Verkehrsautos (Spiel: 5)

    // Mehr als 9 Autos (12.4, VERKEHR.md §6.6): Tabellen pro Auto liegen dann in eigenem Speicher.
    constexpr int kGameCars = 9;
    int g_maxCars = kGameCars;                              // [Cars] MaxCars; 9 = Spiel unverändert
    std::uint8_t * g_carLimitNew[kCarLimitRegionCount] = {}; // neuer Ort je Bereich (nullptr = nicht verlegt)

    // Alte Spieladresse -> aktuelle Adresse (verlegt oder unverändert). Für alle festen Adressen in LFSP,
    // die in einem verlegten Bereich liegen (Liste aller Autos 0x5E8B34, Beschreibungen ab 0x65808C).
    std::uintptr_t CarAddr(std::uintptr_t addr)
    {
        for (std::uint32_t r = 0; r < kCarLimitRegionCount; ++r)
        {
            if (g_carLimitNew[r] && addr >= kCarLimitRegionStart[r] && addr < kCarLimitRegionEnd[r])
            {
                return reinterpret_cast<std::uintptr_t>(g_carLimitNew[r]) + (addr - kCarLimitRegionStart[r]);
            }
        }
        return addr;
    }

    // Auto Nr. i aus der Liste aller Autos (0x5E8B34, Zähler 0x5E8B58), nullptr außerhalb
    unsigned char * CarAt(std::uint32_t i)
    {
        if (i >= static_cast<std::uint32_t>(g_maxCars))
        {
            return nullptr;
        }
        return reinterpret_cast<unsigned char **>(CarAddr(0x5E8B34))[i];
    }

    FILE * g_log = nullptr;

    void Log(const char * fmt, ...)
    {
        if (!g_log)
        {
            return;
        }
        std::fprintf(g_log, "[%10lu] ", GetTickCount());
        va_list args;
        va_start(args, fmt);
        std::vfprintf(g_log, fmt, args);
        va_end(args);
        std::fputc('\n', g_log);
        std::fflush(g_log);
    }

    template <typename T>
    T & At(void * base, std::size_t offset)
    {
        return *reinterpret_cast<T *>(static_cast<unsigned char *>(base) + offset);
    }

    bool InFrontend()
    {
        return *reinterpret_cast<int *>(kFeGlobal) != 0;
    }

    // "964.tpg", ".\\gamedata\\carmodel\\964" -> "964"
    std::string CarKey(const char * name)
    {
        std::string s = name ? name : "";
        const auto slash = s.find_last_of("\\/");
        if (slash != std::string::npos)
        {
            s.erase(0, slash + 1);
        }
        const auto dot = s.find('.');
        if (dot != std::string::npos)
        {
            s.erase(dot);
        }
        for (auto & c : s)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }

    // "F", "f", "7", "F5", "0x46"
    int ParseKey(const char * text)
    {
        std::string s = text;
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        {
            s.pop_back();
        }
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        {
            s.erase(0, 1);
        }
        if (s.size() == 1 && std::isalnum(static_cast<unsigned char>(s[0])))
        {
            return std::toupper(static_cast<unsigned char>(s[0]));
        }
        if (s.size() >= 2 && (s[0] == 'F' || s[0] == 'f') && std::isdigit(static_cast<unsigned char>(s[1])))
        {
            const int n = std::atoi(s.c_str() + 1);
            if (n >= 1 && n <= 24)
            {
                return VK_F1 + n - 1;
            }
        }
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        {
            return static_cast<int>(std::strtol(s.c_str(), nullptr, 16));
        }
        return 0;
    }

    int ReadKey(const char * ini, const char * name, const char * fallback, const char * section = "General")
    {
        char text[64] = {};
        GetPrivateProfileStringA(section, name, fallback, text, sizeof(text), ini);
        const int vk = ParseKey(text);
        if (vk == 0)
        {
            Log("%s '%s' unbekannt, nehme %s", name, text, fallback);
            return ParseKey(fallback);
        }
        return vk;
    }

    // Kommagetrennte Namensliste aus der INI (Leerzeichen am Rand werden entfernt)
    std::vector<std::string> ReadNameList(const char * ini, const char * section, const char * key, const char * fallback)
    {
        char list[512] = {};
        GetPrivateProfileStringA(section, key, fallback, list, sizeof(list), ini);
        std::vector<std::string> names;
        std::string cur;
        for (const char * c = list;; ++c)
        {
            if (*c == ',' || *c == 0)
            {
                while (!cur.empty() && std::isspace(static_cast<unsigned char>(cur.back())))
                {
                    cur.pop_back();
                }
                if (!cur.empty())
                {
                    names.push_back(cur);
                }
                cur.clear();
                if (*c == 0)
                {
                    break;
                }
            }
            else if (!cur.empty() || !std::isspace(static_cast<unsigned char>(*c)))
            {
                cur += *c;
            }
        }
        return names;
    }

    void LoadConfig(const std::string & iniPath)
    {
        const char * ini = iniPath.c_str();
        g_measure = GetPrivateProfileIntA("General", "Measure", 1, ini);
        g_rebuildMode = GetPrivateProfileIntA("General", "RebuildMode", 1, ini);
        g_rebuildMethod = GetPrivateProfileIntA("General", "RebuildMethod", 1, ini);
        g_applyTo = GetPrivateProfileIntA("General", "ApplyTo", 0, ini);
        g_roofTimeMs = GetPrivateProfileIntA("General", "RoofTime", 2200, ini);
        char file[MAX_PATH] = {};
        GetPrivateProfileStringA("General", "RoofSoundOpen", g_roofOpenSound.c_str(), file, sizeof(file), ini);
        g_roofOpenSound = file;
        GetPrivateProfileStringA("General", "RoofSoundClose", g_roofCloseSound.c_str(), file, sizeof(file), ini);
        g_roofCloseSound = file;
        GetPrivateProfileStringA("General", "WindowSound", g_windowSound.c_str(), file, sizeof(file), ini);
        g_windowSound = file;
        g_roofVolume = GetPrivateProfileIntA("General", "RoofVolume", 40, ini);
        g_windowVolume = GetPrivateProfileIntA("General", "WindowVolume", 50, ini);
        g_roofInMenu = GetPrivateProfileIntA("General", "RoofKeyInMenu", 0, ini);
        if (g_roofTimeMs < 1)
        {
            g_roofTimeMs = 1;
        }
        g_windowKey = ReadKey(ini, "WindowKey", "F");
        g_roofKey = ReadKey(ini, "RoofKey", "V");
        g_windowAnimate = GetPrivateProfileIntA("General", "WindowAnimate", 1, ini);
        g_windowAxis = GetPrivateProfileIntA("General", "WindowAxis", 1, ini);
        // GetPrivateProfileIntA liefert UINT: -1 kommt als 0xFFFFFFFF an (v4b fuhr deshalb nach oben)
        g_windowDownSign = static_cast<int>(GetPrivateProfileIntA("General", "WindowDownSign", -1, ini)) < 0 ? -1 : 1;
        g_windowTimeMs = GetPrivateProfileIntA("General", "WindowTime", 1200, ini);
        g_windowSideAxis = GetPrivateProfileIntA("General", "WindowSideAxis", 0, ini);
        char tilt[32] = {};
        GetPrivateProfileStringA("General", "WindowTilt", "1.0", tilt, sizeof(tilt), ini);
        g_windowTilt = static_cast<float>(std::atof(tilt));
        char sink[32] = {};
        GetPrivateProfileStringA("General", "WindowSink", "0.33", sink, sizeof(sink), ini);
        g_windowSink = static_cast<float>(std::atof(sink));
        if (g_windowSink < 0.0f)
        {
            g_windowSink = 0.0f;
        }
        char forward[32] = {};
        GetPrivateProfileStringA("General", "WindowForward", "0.15", forward, sizeof(forward), ini);
        g_windowForward = static_cast<float>(std::atof(forward));
        // UINT-Rückgabe: -1 kommt als 0xFFFFFFFF an
        g_windowForwardSign =
            static_cast<int>(GetPrivateProfileIntA("General", "WindowForwardSign", -1, ini)) < 0 ? -1 : 1;
        if (g_windowSideAxis < 0 || g_windowSideAxis > 2)
        {
            g_windowSideAxis = 0;
        }
        char depth[32] = {};
        GetPrivateProfileStringA("General", "WindowDepth", "1.05", depth, sizeof(depth), ini);
        g_windowDepth = static_cast<float>(std::atof(depth));
        if (g_windowAxis < 0 || g_windowAxis > 2)
        {
            g_windowAxis = 1;
        }
        if (g_windowTimeMs < 1)
        {
            g_windowTimeMs = 1;
        }

        g_trackDetail = GetPrivateProfileIntA("World", "TrackDetail", 1, ini);
        char range[32] = {};
        GetPrivateProfileStringA("World", "TrackRange", "1.5", range, sizeof(range), ini);
        g_trackRange = static_cast<float>(std::atof(range));
        if (g_trackRange < 1.0f || g_trackRange > 8.0f)
        {
            g_trackRange = 1.5f;
        }
        g_trackDistScale = 1.0f / (g_trackRange * g_trackRange);
        g_trackDiag = GetPrivateProfileIntA("World", "TrackDiag", 0, ini);
        g_trackDiagKey = ReadKey(ini, "TrackDiagKey", "G", "World");
        g_clipDiag = GetPrivateProfileIntA("World", "ClipDiag", 0, ini);
        g_guardBand = GetPrivateProfileIntA("World", "GuardBand", 1, ini) != 0;
        {
            char scale[32] = {};
            GetPrivateProfileStringA("World", "CullRadiusScale", "2.0", scale, sizeof(scale), ini);
            g_cullRadiusScale = std::min(std::max(static_cast<float>(std::atof(scale)), 1.0f), 8.0f);
        }
        {
            char margin[32] = {};
            GetPrivateProfileStringA("World", "ClipMargin", "0.5", margin, sizeof(margin), ini);
            g_clipMargin = std::min(std::max(static_cast<float>(std::atof(margin)), 0.0f), 2.0f);
        }
        Log("Strecke: TrackDetail %d, TrackRange %.2f, TrackDiag %d (Taste 0x%02X), ClipDiag %d",
            g_trackDetail, g_trackRange, g_trackDiag, g_trackDiagKey, g_clipDiag);

        auto readFloat = [ini](const char * name, const char * fallback, float lo, float hi) {
            char text[32] = {};
            GetPrivateProfileStringA("Camera", name, fallback, text, sizeof(text), ini);
            return std::fmax(lo, std::fmin(static_cast<float>(std::atof(text)), hi));
        };
        auto readOptionalKey = [ini](const char * name) {
            char text[64] = {};
            GetPrivateProfileStringA("Camera", name, "", text, sizeof(text), ini);
            return text[0] ? ParseKey(text) : 0;
        };
        g_cineInCycle = GetPrivateProfileIntA("Camera", "CinematicInCycle", 1, ini);
        g_cineKey = readOptionalKey("CinematicKey");
        g_action = GetPrivateProfileIntA("Camera", "Action", 1, ini);
        g_actionKey = readOptionalKey("ActionKey");
        char cams[128] = {};
        GetPrivateProfileStringA("Camera", "ActionCams", "2, 3, 4", cams, sizeof(cams), ini);
        g_actionCams = 0;
        for (const char * p = cams; *p;)
        {
            char * end = nullptr;
            const long type = std::strtol(p, &end, 10);
            if (end == p)
            {
                ++p;
                continue;
            }
            if (type >= 0 && type < 32)
            {
                g_actionCams |= 1u << type;
            }
            p = end;
        }
        g_actSide = readFloat("ActionSide", "1.0", 0.0f, 3.0f);
        g_actLook = readFloat("ActionLook", "7.0", 0.0f, 15.0f);
        g_actHeight = readFloat("ActionHeight", "0.25", -0.5f, 1.5f);
        g_actBrake = readFloat("ActionBrake", "0.8", 0.0f, 3.0f);
        g_actBounce = readFloat("ActionBounce", "1.0", 0.0f, 3.0f);
        g_actFov = readFloat("ActionFov", "0.20", 0.0f, 0.5f);
        g_actShake = readFloat("ActionShake", "1.0", 0.0f, 3.0f);
        g_actRoll = readFloat("ActionRoll", "2.5", -10.0f, 10.0f);
        g_actOvershoot = readFloat("ActionOvershoot", "0.5", 0.0f, 2.0f);
        g_actBuildUp = readFloat("ActionBuildUp", "1.2", 0.05f, 5.0f);
        g_actSpeedBoost = readFloat("ActionSpeedBoost", "0.35", 0.0f, 2.0f);
        g_cockpitFov = readFloat("CockpitFov", "90", 0.0f, 150.0f);
        if (g_cockpitFov > 0.0f && g_cockpitFov < 40.0f)
        {
            g_cockpitFov = 40.0f;
        }
        Log("Kamera: SpeedBoost %.2f, CockpitFov %.0f", g_actSpeedBoost, g_cockpitFov);
        g_actImpact = readFloat("ActionImpact", "1.0", 0.0f, 3.0f);
        Log("Kamera: Roll %.1f, Overshoot %.2f, BuildUp %.2f, Impact %.2f", g_actRoll, g_actOvershoot, g_actBuildUp, g_actImpact);
        Log("Kamera: CinematicInCycle %d, CinematicKey 0x%02X, Action %d, ActionKey 0x%02X, ActionCams 0x%X, "
            "Side %.2f Look %.1f Height %.2f Brake %.2f Bounce %.2f Fov %.2f Shake %.2f",
            g_cineInCycle, g_cineKey, g_action, g_actionKey, g_actionCams, g_actSide, g_actLook, g_actHeight, g_actBrake,
            g_actBounce, g_actFov, g_actShake);

        g_highKey = ReadKey(ini, "HeadlightsHighKey", "K", "Lights");
        g_highFadeMs = GetPrivateProfileIntA("Lights", "HeadlightsHighFade", 100, ini);
        g_flashPopUps = GetPrivateProfileIntA("Lights", "HeadlightsFlashPopUps", 1, ini);
        g_signalsInSync = GetPrivateProfileIntA("Lights", "SignalsInSync", 1, ini);
        g_thirdBrakeOnly = GetPrivateProfileIntA("Lights", "ThirdBrakeLightBrakingOnly", 1, ini);
        g_gaugesFollowLights = GetPrivateProfileIntA("Lights", "InstrumentsFollowLights", 1, ini);
        Log("Licht: Fernlicht-Taste 0x%02X, Ueberblenden %d ms", g_highKey, g_highFadeMs);
        g_colourByYear = GetPrivateProfileIntA("Lights", "HeadlightsColourByYear", 1, ini);
        g_yearColours.clear();
        static const char * const kDefaultStops[] = { "1950, FFA54F, 0.5", "1975, FFC184, 0.8", "1995, FFCC99, 1.0",
                                                      "1997, FFF0E0, 1.3" };
        for (int i = 1; i <= 16; ++i)
        {
            char key[16] = {};
            char value[64] = {};
            std::snprintf(key, sizeof(key), "Year%d", i);
            GetPrivateProfileStringA("Lights", key, i <= 4 ? kDefaultStops[i - 1] : "", value, sizeof(value), ini);
            YearColour stop = {};
            unsigned rgb = 0;
            if (std::sscanf(value, " %d , %x , %f", &stop.year, &rgb, &stop.strength) != 3)
            {
                continue;
            }
            stop.rgb[0] = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
            stop.rgb[1] = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
            stop.rgb[2] = static_cast<float>(rgb & 0xFF) / 255.0f;
            stop.strength = std::fmax(0.0f, std::fmin(stop.strength, 4.0f));
            auto at = g_yearColours.begin();
            while (at != g_yearColours.end() && at->year < stop.year)
            {
                ++at;
            }
            g_yearColours.insert(at, stop);
            Log("Licht: Stuetzpunkt %d = %06X, Helligkeit %.2f", stop.year, rgb, stop.strength);
        }
        Log("Licht: HeadlightsColourByYear=%d, %u Stuetzpunkte", g_colourByYear,
            static_cast<unsigned>(g_yearColours.size()));

        // Fenster-Tabelle fest eingebaut (windows_table.h); [Windows] in der INI ersetzt oder ergänzt Einträge
        g_openValues.clear();
        for (const auto & e : kWindowTable)
        {
            g_openValues[e.key] = WindowValues{ e.outer, e.inner };
        }
        static char section[32768];
        const DWORD len = GetPrivateProfileSectionA("Windows", section, sizeof(section), ini);
        for (const char * p = section; p < section + len && *p; p += std::strlen(p) + 1)
        {
            const char * eq = std::strchr(p, '=');
            if (!eq || *p == ';')
            {
                continue;
            }
            WindowValues v;
            if (std::sscanf(eq + 1, " %d , %d", &v.outer, &v.inner) != 2)
            {
                continue;
            }
            std::string k(p, eq);
            while (!k.empty() && std::isspace(static_cast<unsigned char>(k.back())))
            {
                k.pop_back();
            }
            for (auto & c : k)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            g_openValues[k] = v;
        }
        g_trafficMeasure = GetPrivateProfileIntA("Traffic", "Measure", 1, ini);
        g_trafficRandom = GetPrivateProfileIntA("Traffic", "RandomModels", 1, ini);
        g_trafficPoolNames = ReadNameList(ini, "Traffic", "Models", "");
        g_trafficTrackOnly = ReadNameList(ini, "Traffic", "TrackOnly", "EAS4");
        g_maxCars = static_cast<int>(GetPrivateProfileIntA("Cars", "MaxCars", kGameCars, ini));
        g_maxCars = g_maxCars < kGameCars ? kGameCars : (g_maxCars > 16 ? 16 : g_maxCars);
        Log("Autos: MaxCars %d", g_maxCars);
        g_trafficCount[1] = GetPrivateProfileIntA("Traffic", "Light", 2, ini);
        g_trafficCount[2] = GetPrivateProfileIntA("Traffic", "Medium", 5, ini);
        g_trafficCount[3] = GetPrivateProfileIntA("Traffic", "Heavy", 8, ini);
        for (int i = 1; i < 4; ++i)
        {
            // Obergrenze MaxCars - 1 (Spieler); das Spiel kürzt ohnehin auf MaxCars Autos gesamt (VERKEHR.md §2)
            const int most = g_maxCars - 1;
            g_trafficCount[i] = g_trafficCount[i] < 0 ? 0 : (g_trafficCount[i] > most ? most : g_trafficCount[i]);
        }
        Log("Verkehr: Stufen Leicht %d, Mittel %d, Schwer %d", g_trafficCount[1], g_trafficCount[2], g_trafficCount[3]);
        g_trafficActive = GetPrivateProfileIntA("Traffic", "Active", 5, ini);
        g_trafficActive = g_trafficActive < 1 ? 1 : (g_trafficActive > g_maxCars - 1 ? g_maxCars - 1 : g_trafficActive);
        Log("Verkehr: gleichzeitig hoechstens %d", g_trafficActive);
        Log("Verkehr: Measure %d, RandomModels %d, Models %u Namen (leer = alle), TrackOnly %u Namen", g_trafficMeasure,
            g_trafficRandom, static_cast<unsigned>(g_trafficPoolNames.size()),
            static_cast<unsigned>(g_trafficTrackOnly.size()));
        Log("Konfig:Measure=%d WindowKey=0x%02X RoofKey=0x%02X RoofTime=%d RebuildMode=%d ApplyTo=%d, "
            "%u Fenster-Eintraege",
            g_measure, g_windowKey, g_roofKey, g_roofTimeMs, g_rebuildMode, g_applyTo,
            static_cast<unsigned>(g_openValues.size()));
    }

    // Wirkt die Taste auf dieses Modell? Menü: jedes Auto; Rennen: Spielerauto (oder alle mit ApplyTo = 1).
    bool IsTarget(void * model, const ModelState & st)
    {
        if (st.name == "garage")
        {
            return false;
        }
        auto * car = At<unsigned char *>(model, kModelCar);
        if (!car)
        {
            return false;
        }
        if (InFrontend() || g_applyTo == 1)
        {
            return true;
        }
        return (*reinterpret_cast<unsigned *>(car + kCarFlags) & kCarFlagPlayer) != 0;
    }

    // ---- Geometrie neu aufbauen (je Render-Schicht; FUN_0043c1b0 räumt die alte Liste selbst ab) ----
    // Liest vtable und Modell einer Schicht; false, wenn der Speicher nicht lesbar ist.
    bool ReadLayerHead(void * layer, std::uint32_t & vtable, void *& model)
    {
        __try
        {
            vtable = *static_cast<std::uint32_t *>(layer);
            model = *reinterpret_cast<void **>(static_cast<unsigned char *>(layer) + 4);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Name aus carSpecs (car + 0x760, gesetzt von FUN_00410d60 für jedes Rennauto); false, wenn nicht lesbar.
    bool ReadSpecsName(unsigned char * car, char (&name)[32])
    {
        __try
        {
            const char * specs = *reinterpret_cast<const char **>(car + kCarSpecs);
            if (!specs)
            {
                return false;
            }
            for (std::size_t i = 0; i < sizeof(name); ++i)
            {
                name[i] = specs[i];
            }
            name[sizeof(name) - 1] = 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Baujahr = die ersten vier Ziffern des .sim-Namens (alle 87 Dateien in CarData, auch "1951356 coupe1.3L").
    int ParseYear(const char * name)
    {
        int year = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (name[i] < '0' || name[i] > '9')
            {
                return 0;
            }
            year = year * 10 + (name[i] - '0');
        }
        return year >= 1900 && year <= 2100 ? year : 0;
    }

    // Farbton für ein Baujahr: Stützpunkte linear überblendet, außerhalb der erste/letzte. Auf Luminanz 1 normiert
    // (gleiche Helligkeit wie das graue Licht des Spiels) und mit der Helligkeit des Stützpunkts multipliziert.
    bool YearTint(int year, float (&tint)[3])
    {
        if (g_yearColours.empty() || year <= 0)
        {
            return false;
        }
        const YearColour * a = &g_yearColours.front();
        const YearColour * b = a;
        float t = 0.0f;
        if (year >= g_yearColours.back().year)
        {
            a = b = &g_yearColours.back();
        }
        else if (year > a->year)
        {
            for (std::size_t i = 1; i < g_yearColours.size(); ++i)
            {
                if (year < g_yearColours[i].year)
                {
                    a = &g_yearColours[i - 1];
                    b = &g_yearColours[i];
                    t = static_cast<float>(year - a->year) / static_cast<float>(b->year - a->year);
                    break;
                }
            }
        }
        float rgb[3] = {};
        for (int c = 0; c < 3; ++c)
        {
            rgb[c] = a->rgb[c] + (b->rgb[c] - a->rgb[c]) * t;
        }
        const float strength = a->strength + (b->strength - a->strength) * t;
        const float luma = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
        if (luma <= 0.0f)
        {
            return false;
        }
        for (int c = 0; c < 3; ++c)
        {
            tint[c] = rgb[c] / luma * strength;
        }
        return true;
    }

    float UpdateHighBeam();
    void UpdateGauges(void * model, struct ModelState & st);

    // Lichthupe öffnet Klappscheinwerfer (9.10e, doc/LICHT.md §9.4): Die Leuchten-Logik des Spiels (0x439360) startet
    // bei einem Wechsel des Lichtschalters die Klappen-Animation im Kanal [model + 0x64] + 0x110 (Startzeit, Dauer,
    // Von, Ziel; Ziel = Öffnungswinkel model + 0x70 bzw. 0, 1000 ms, Zeit FUN_0053bfd0). LFSP fährt bei ausgeschaltetem
    // Licht nur diese Animation selbst, der Lichtschalter (und damit die Heckleuchten) bleibt unberührt. Das Spiel
    // greift nicht ein, solange Schalter und model + 0x567 gleich bleiben.
    float PopUpValue(const unsigned char * tween, DWORD now)
    {
        const DWORD start = *reinterpret_cast<const DWORD *>(tween);
        const DWORD duration = *reinterpret_cast<const DWORD *>(tween + 4);
        const float from = *reinterpret_cast<const float *>(tween + 8);
        const float to = *reinterpret_cast<const float *>(tween + 0xC);
        if (duration == 0 || now - start >= duration)
        {
            return to;
        }
        return from + (to - from) * static_cast<float>(now - start) / static_cast<float>(duration);
    }

    void StartPopUp(unsigned char * tween, float target, float open, DWORD now)
    {
        const float current = PopUpValue(tween, now);
        const float way = open != 0.0f ? std::fabs(target - current) / std::fabs(open) : 1.0f;
        *reinterpret_cast<float *>(tween + 8) = current;
        *reinterpret_cast<float *>(tween + 0xC) = target;
        *reinterpret_cast<DWORD *>(tween) = now;
        *reinterpret_cast<DWORD *>(tween + 4) = static_cast<DWORD>(kPopUpTimeMs * std::fmin(1.0f, way)) + 1;
    }

    void UpdateFlasher(void * model, ModelState & st)
    {
        auto * car = At<unsigned char *>(model, kModelCar);
        if (!g_flashPopUps || !car || st.name == "garage" || InFrontend() ||
            (*reinterpret_cast<unsigned *>(car + kCarFlags) & kCarFlagPlayer) == 0)
        {
            return;
        }
        auto * tweens = At<unsigned char *>(model, kModelTweens);
        const float open = At<float>(model, kModelPopUpOpen);
        if (!tweens || open == 0.0f)
        {
            return; // kein Auto mit Klappscheinwerfern
        }
        UpdateHighBeam();
        unsigned char * tween = tweens + kPopUpTween;
        const DWORD now = reinterpret_cast<DWORD(__cdecl *)()>(kGameTime)();
        const bool switchOn = (car[kCarLampBits] & 1) != 0;
        if (switchOn || At<unsigned char>(model, kModelPopUpState) != 0)
        {
            // Licht mit L an (oder das Spiel ist noch im Wechsel): die Klappen gehören dem Spiel.
            if (g_flashCar == car)
            {
                g_flashCar = nullptr;
                Log("Licht: Lichthupe endet, Lichtschalter an (Klappen wie im Spiel)");
            }
            return;
        }
        if (g_highDown && g_flashCar != car)
        {
            g_flashCar = car;
            StartPopUp(tween, open, open, now);
            Log("Licht: Lichthupe oeffnet die Klappscheinwerfer (Modell %p '%s', Winkel %.3f)", model, st.name.c_str(), open);
        }
        else if (!g_highDown && g_flashCar == car)
        {
            g_flashCar = nullptr;
            StartPopUp(tween, 0.0f, open, now);
            Log("Licht: Lichthupe schliesst die Klappscheinwerfer");
        }
    }

    void UpdateYear(void * model, ModelState & st)
    {
        auto * car = At<unsigned char *>(model, kModelCar);
        if (!car || st.name == "garage" || InFrontend())
        {
            return;
        }
        const bool player = (*reinterpret_cast<unsigned *>(car + kCarFlags) & kCarFlagPlayer) != 0;
        if (st.yearCar != car)
        {
            char name[32] = {};
            st.yearCar = car;
            st.year = ReadSpecsName(car, name) ? ParseYear(name) : 0;
            Log("Baujahr: Modell %p '%s' %s: carSpecs '%s' -> %d", model, st.name.c_str(), player ? "Spieler" : "Gegner",
                name, st.year);
            float tint[3] = {};
            if (player && g_colourByYear && YearTint(st.year, tint))
            {
                Log("Licht: Spielerauto %d -> Farbton %.2f %.2f %.2f", st.year, tint[0], tint[1], tint[2]);
            }
        }
        if (player)
        {
            g_playerYear = st.year;
        }
    }

    void LogLayerParts(void * model, void * layer, const char * why);
    void PlayCarSound(const char * alias);
    void LogPoses(void * model, struct ModelState & st, const char * why);
    float SlotMax(void * model, int slot);
    float SlotValue(void * model, int slot);

    // Nach einem Neuaufbau zeigt die Engine ein animiertes Teil (Heckspoiler) erst wieder, wenn es sich über eine
    // ganze Bildgrenze bewegt: FUN_0043e120 liest die Bilddaten über einen Merker je Listeneintrag, der nur bei einem
    // Wechsel der Bildnummer neu gesucht wird (8.3d; Anstoßen um 0,01 bzw. 0,25 innerhalb desselben Bildes half
    // nicht). Deshalb nach dem Engine-Update jede Animation für ein Bild um ein ganzes Bild verstellen und im
    // nächsten Bild zurückstellen, sofern die Engine sie nicht selbst gesetzt hat.
    void JiggleAnimations(void * model, ModelState & st)
    {
        auto set = reinterpret_cast<SetAnimSlotFn>(kSetAnimSlot);
        if (st.jiggle == 2)
        {
            for (int s = 0; s < kAnimSlotCount; ++s)
            {
                st.jiggleTo[s] = -1.0f;
                if (SlotMax(model, s) <= 0.0f || (s == kAnimSlotRoof && st.roofAnimating))
                {
                    continue;
                }
                const float v = SlotValue(model, s);
                const float max = SlotMax(model, s);
                if (max < 1.0f)
                {
                    continue;
                }
                st.jiggleFrom[s] = v;
                st.jiggleTo[s] = v + 1.0f <= max - kRoofEndMargin ? v + 1.0f : v - 1.0f;
                set(model, s, st.jiggleTo[s]);
            }
            st.jiggle = 1;
        }
        else if (st.jiggle == 1)
        {
            if (g_measure)
            {
                LogPoses(model, st, "naechstes Bild");
            }
            for (int s = 0; s < kAnimSlotCount; ++s)
            {
                if (st.jiggleTo[s] >= 0.0f && !(s == kAnimSlotRoof && st.roofAnimating)
                    && std::fabs(SlotValue(model, s) - st.jiggleTo[s]) < 1e-3f)
                {
                    set(model, s, st.jiggleFrom[s]);
                }
            }
            st.jiggle = 0;
        }
    }

    void LogTable(void * model, const ModelState & st);
    float SlotValue(void * model, int slot);

    // ---- Verformte Puffer animierter Teile über den Neuaufbau retten ----
    // FUN_0043e120 schreibt die verformten Vertices eines animierten Teils in den Puffer Eintrag + 0x40 (16 Byte je
    // Vertex). Für ein Bild ohne Daten (die Engine setzt den Heckspoiler ausgefahren auf Bild = max, z. B. 4 bei 4
    // Bildern) schreibt sie nichts und lässt den Puffer stehen. Ein frisch aufgebauter Puffer bleibt dann leer, der
    // Spoiler fehlt bis zur nächsten Bewegung (8.3d). Deshalb Puffer vorher sichern und danach zurückschreiben.
    struct SavedPose
    {
        unsigned char * atri = nullptr;
        std::vector< unsigned char > data;
    };

    // Vertexanzahl wie in FUN_0043e120: Unter-Eintrag (dword[0x5E5084] & 0xFFFF0000 | Schicht-Id), drittes Dword.
    std::uint32_t AnimVertexCount(unsigned char * atri, unsigned layerId)
    {
        if (!atri || layerId == 0xFFFFFFFFu)
        {
            return 0;
        }
        const std::uint32_t key = (*reinterpret_cast<std::uint32_t *>(kKeyBase) & 0xFFFF0000u) | (layerId & 0xFFFFu);
        auto * child = reinterpret_cast<CrpChildFn>(kCrpChild)(atri, key);
        auto * first = reinterpret_cast<CrpBaseFn>(kCrpBase)(atri);
        auto * end = first + ((At<std::uint32_t>(atri, 4) >> 5) << 4) + (At<std::uint32_t>(atri, 8) << 4);
        if (!child || child == end)
        {
            return 0;
        }
        const std::uint32_t n = At<std::uint32_t>(child, 8);
        return n < 65536 ? n : 0;
    }

    template <typename F>
    void ForAnimatedEntries(void * layer, F f)
    {
        auto * list = At<unsigned char *>(layer, 0xC);
        const int count = At<int>(layer, 0x10);
        for (int i = 0; list && i < count; ++i)
        {
            auto * e = list + i * kLayerEntrySize;
            auto * hdr = At<unsigned char *>(e, 0x44);
            if (hdr && hdr[9] && At<unsigned char *>(e, 0) && At<float *>(e, 0x40))
            {
                f(e, hdr[9]);
            }
        }
    }

    std::vector< SavedPose > SavePoses(void * layer)
    {
        std::vector< SavedPose > poses;
        const unsigned id = At<unsigned>(layer, 8);
        ForAnimatedEntries(layer, [&](unsigned char * e, int) {
            auto * atri = At<unsigned char *>(e, 0);
            const std::uint32_t n = AnimVertexCount(atri, id);
            if (n == 0)
            {
                return;
            }
            SavedPose pose;
            pose.atri = atri;
            pose.data.resize(n * 16u);
            std::memcpy(pose.data.data(), At<float *>(e, 0x40), pose.data.size());
            poses.push_back(std::move(pose));
        });
        return poses;
    }

    unsigned RestorePoses(void * layer, const std::vector< SavedPose > & poses)
    {
        unsigned restored = 0;
        const unsigned id = At<unsigned>(layer, 8);
        ForAnimatedEntries(layer, [&](unsigned char * e, int) {
            auto * atri = At<unsigned char *>(e, 0);
            for (const auto & pose : poses)
            {
                if (pose.atri == atri && AnimVertexCount(atri, id) * 16u == pose.data.size())
                {
                    std::memcpy(At<float *>(e, 0x40), pose.data.data(), pose.data.size());
                    ++restored;
                    break;
                }
            }
        });
        return restored;
    }

    // Messlog: erster Vertex jedes animierten Teils der Schicht 1 (ohne Verdeck)
    void LogPoses(void * model, ModelState & st, const char * why)
    {
        for (const auto & l : st.layers)
        {
            if (At<unsigned>(l.layer, 8) != 1)
            {
                continue;
            }
            ForAnimatedEntries(l.layer, [&](unsigned char * e, int slot) {
                if (slot == kAnimSlotRoof)
                {
                    return;
                }
                const float * v = At<float *>(e, 0x40);
                Log("  Pose %s: Modell %p Animation %d Puffer %p (%u Vertices) v0 = %.3f %.3f %.3f, Wert %.2f", why,
                    model, slot, v, AnimVertexCount(At<unsigned char *>(e, 0), 1), v[0], v[1], v[2],
                    SlotValue(model, slot));
            });
        }
    }

    void RebuildGeometry(void * model, ModelState & st)
    {
        // Der Aufbau gibt Eintrag + 0x40 selbst frei und legt die Puffer aus der .crp neu an
        st.shifts.clear();
        st.shiftApplied = st.shiftFrom = st.shiftTo = 0.0f;
        st.shiftAnimating = false;
        st.reapplyShift = st.windowShiftOnly && st.windowsOpen;
        // Der Aufbau setzt animierte Teile auf Bild 0; Stellungen merken und danach wiederherstellen.
        float slotValues[kAnimSlotCount];
        for (int s = 0; s < kAnimSlotCount; ++s)
        {
            slotValues[s] = SlotValue(model, s);
        }
        unsigned rebuilt = 0;
        if (g_rebuildMethod == 2)
        {
            // Wie das Menü bei einem Verdeck-Wechsel (FUN_004d1800) und das Spiel beim Laden (0x4420A3):
            // Schichten zerstören und frisch anlegen. Ein bloßes Neubefüllen (Methode 1) lässt den Heckspoiler
            // bis zu seiner nächsten Bewegung unsichtbar (8.3d, v3i–v3l). Der Aufbau-Hook trägt die neuen
            // Schichten wieder ein.
            st.layers.clear();
            reinterpret_cast<void (__fastcall *)(void *)>(kRecreateLayers)(model);
            rebuilt = static_cast<unsigned>(st.layers.size());
            for (const auto & l : st.layers)
            {
                if (At<unsigned>(l.layer, 8) == 1)
                {
                    LogLayerParts(model, l.layer, "nach Neuanlage");
                }
            }
        }
        for (auto it = st.layers.begin(); g_rebuildMethod != 2 && it != st.layers.end();)
        {
            std::uint32_t vtable = 0;
            void * owner = nullptr;
            if (!ReadLayerHead(it->layer, vtable, owner) || vtable != it->vtable || owner != model)
            {
                Log("  Schicht %p nicht mehr gueltig (vtable %08X statt %08X, Modell %p), entfernt", it->layer, vtable,
                    it->vtable, owner);
                it = st.layers.erase(it);
                continue;
            }
            Log("  Schicht %p Id 0x%X: Neuaufbau", it->layer, At<unsigned>(it->layer, 8));
            if (At<unsigned>(it->layer, 8) == 1 && g_measure)
            {
                LogPoses(model, st, "vorher");
            }
            const auto poses = SavePoses(it->layer);
            g_origBuildGeo(it->layer, it->crp);
            const unsigned restored = RestorePoses(it->layer, poses);
            if (!poses.empty())
            {
                Log("  Schicht %p: %u von %u verformten Puffern zurueckgeschrieben", it->layer, restored,
                    static_cast<unsigned>(poses.size()));
            }
            if (At<unsigned>(it->layer, 8) == 1)
            {
                LogLayerParts(model, it->layer, "nach Neuaufbau");
            }
            ++rebuilt;
            ++it;
        }
        for (int s = 0; s < kAnimSlotCount; ++s)
        {
            reinterpret_cast<SetAnimSlotFn>(kSetAnimSlot)(model, s, slotValues[s]);
            st.slotValues[s] = slotValues[s];
        }
        st.jiggle = 2;
        if (g_measure)
        {
            LogPoses(model, st, "nachher");
        }
        Log("  Modell %p: Geometrie neu aufgebaut (Methode %d, %u Schichten), Verdeck-Slot max %.2f", model,
            g_rebuildMethod, rebuilt,
            At<float>(model, kModelAnimSlots + kAnimSlotRoof * kAnimSlotSize + 0xC));
    }

    // ---- Fenster ----
    // Textur-Teile aus <auto>D/L.fsh (nur RebuildMode = 2)
    void RebuildPartList(void * model, unsigned char * car)
    {
        if (At<unsigned char>(model, kModelListOn) == 0)
        {
            Log("  Modell %p: Teile-Liste inaktiv (+0x47C = 0), kein Neuaufbau", model);
            return;
        }
        const bool hiLod = At<unsigned char>(model, kModelLodByte) > 0x50;
        auto * mesh = At<std::uint32_t *>(model, kModelMesh + (hiLod ? 4 : 0));
        auto * table = reinterpret_cast<std::uint32_t *>(car + kCarPartTable);
        auto * list = static_cast<unsigned char *>(model) + kModelList;
        const int before = At<int>(list, 0);
        reinterpret_cast<BuildListFn>(kBuildList)(list, mesh, table);
        if (g_rebuildMode >= 2)
        {
            reinterpret_cast<PaintListFn>(kPaintList)(list, At<int>(model, kModelTpages),
                                                      reinterpret_cast<int>(car + kCarColours));
        }
        Log("  Modell %p: Teile-Liste neu gebaut (Mesh %p, Modus %d, Gruppen %d -> %d)",
            model, mesh, g_rebuildMode, before, At<int>(list, 0));
    }

    // Varianten der Seitenscheiben aus [Windows] für dieses Auto und diesen Style; nullptr, wenn keine
    const WindowValues * OpenValues(void * model, const ModelState & st, bool log = true)
    {
        auto * car = At<unsigned char *>(model, kModelCar);
        const int style = *reinterpret_cast<int *>(car + kCarStyle) + 1;
        const std::string key = st.name + "." + std::to_string(style);
        const auto it = g_openValues.find(key);
        if (it == g_openValues.end())
        {
            if (log)
            {
                Log("Fenster: Modell %p '%s' [style%d]: kein Eintrag [Windows] %s", model, st.name.c_str(), style,
                    key.c_str());
            }
            return nullptr;
        }
        return &it->second;
    }

    // Scheiben-Varianten umschalten (zu <-> offen) und Geometrie neu aufbauen
    void SwitchWindowVariants(void * model, ModelState & st)
    {
        const WindowValues * open = OpenValues(model, st);
        if (!open)
        {
            return;
        }
        auto * car = At<unsigned char *>(model, kModelCar);
        const int style = *reinterpret_cast<int *>(car + kCarStyle) + 1;
        auto * table = reinterpret_cast<int *>(car + kCarPartTable);

        if (!st.windowsOpen)
        {
            st.closed = { table[kPartWindowOuter], table[kPartWindowInner] };
            if (open->outer)
            {
                table[kPartWindowOuter] = open->outer;
            }
            if (open->inner)
            {
                table[kPartWindowInner] = open->inner;
            }
            st.windowsOpen = true;
        }
        else
        {
            table[kPartWindowOuter] = st.closed.outer;
            table[kPartWindowInner] = st.closed.inner;
            st.windowsOpen = false;
        }
        Log("Fenster: Modell %p '%s' [style%d] Flags 0x%X: %s (35=%d, 47=%d)",
            model, st.name.c_str(), style, *reinterpret_cast<unsigned *>(car + kCarFlags),
            st.windowsOpen ? "AUF" : "ZU", table[kPartWindowOuter], table[kPartWindowInner]);
        RebuildGeometry(model, st);
        if (g_rebuildMode >= 2)
        {
            RebuildPartList(model, car);
        }
    }

    // ---- Verdeck (Mesh-Animation Slot 1) ----
    float SlotValue(void * model, int slot)
    {
        const std::size_t base = kModelAnimSlots + slot * kAnimSlotSize;
        return static_cast<float>(At<int>(model, base)) + At<float>(model, base + 8);
    }

    float SlotMax(void * model, int slot)
    {
        return At<float>(model, kModelAnimSlots + slot * kAnimSlotSize + 0xC);
    }

    // Geräusch unter einem MCI-Alias öffnen (Typ mpegvideo spielt WAV und MP3 und kann die Lautstärke setzen).
    void OpenSound(const char * alias, const std::string & file, int volume)
    {
        if (file.empty())
        {
            return;
        }
        const std::string path = (file.find(':') != std::string::npos ? "" : g_soundDir) + file;
        std::string use = path;
        if (GetFileAttributesA(use.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            const auto dot = use.find_last_of('.');
            const std::string ext = dot == std::string::npos ? "" : use.substr(dot);
            const std::string other = _stricmp(ext.c_str(), ".mp3") == 0 ? ".wav" : ".mp3";
            use = (dot == std::string::npos ? use : use.substr(0, dot)) + other;
            if (GetFileAttributesA(use.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                Log("Geraeusch %s: %s fehlt (auch %s)", alias, path.c_str(), other.c_str());
                return;
            }
        }
        const std::string open = "open \"" + use + "\" type mpegvideo alias " + alias;
        const MCIERROR err = mciSendStringA(open.c_str(), nullptr, 0, nullptr);
        if (err != 0)
        {
            char text[256] = {};
            mciGetErrorStringA(err, text, sizeof(text));
            Log("Geraeusch %s: %s nicht geoeffnet (%s)", alias, use.c_str(), text);
            return;
        }
        const std::string vol = std::string("setaudio ") + alias + " volume to " + std::to_string(volume * 10);
        mciSendStringA(vol.c_str(), nullptr, 0, nullptr);
        Log("Geraeusch %s: %s (Lautstaerke %d %%)", alias, use.c_str(), volume);
    }

    void PlayCarSound(const char * alias)
    {
        const std::string play = std::string("play ") + alias + " from 0";
        const MCIERROR err = mciSendStringA(play.c_str(), nullptr, 0, nullptr);
        if (err != 0 && g_measure)
        {
            Log("Geraeusch %s: Abspielen fehlgeschlagen (%lu)", alias, static_cast<unsigned long>(err));
        }
    }

    // Im Rennen fehlt das animierte Verdeck-Teil in der Geometrie (andere Variante als im Menü).
    // Varianten mit Mesh-Animation Slot 1 aus dem Katalog einschalten und neu aufbauen.
    // Wert von geometry <part> in [styleN] der .tpg, -1 wenn nicht genannt.
    int TpgStyleValue(const std::string & name, int style, int part)
    {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string tpg = exe;
        tpg.erase(tpg.find_last_of("\\/") + 1);
        tpg += "GameData\\CarModel\\" + name + ".tpg";
        const std::string sec = "style" + std::to_string(style);
        const int count = GetPrivateProfileIntA(sec.c_str(), "count", 0, tpg.c_str());
        int value = -1;
        for (int i = 1; i <= count; ++i)
        {
            char key[32];
            std::snprintf(key, sizeof(key), "geometry%d", i);
            if (static_cast<int>(GetPrivateProfileIntA(sec.c_str(), key, -1, tpg.c_str())) == part)
            {
                std::snprintf(key, sizeof(key), "type%d", i);
                value = GetPrivateProfileIntA(sec.c_str(), key, 0, tpg.c_str());
            }
        }
        return value;
    }

    // Das Menü zeigt das animierte Verdeck (Teil 37) ohne starres Verdeck (Teil 41). Nicht animierte
    // Geometrie-Teile, die im Menü aus und im Rennen an sind, werden ausgeschaltet. Nur ausschalten:
    // Teile, die nur das Menü zeigt (Boxster 42), verdecken im Rennen den Heckspoiler (8.3d, v3h).
    // Ausgenommen: Seitenscheiben (vom Plugin geschaltet) und Textur-Einträge.
    // Startstellung: Teil 41 steht im Rennen auf der geschlossenen Variante ([styleN]-Wert, sonst 1)
    // oder auf einer offenen.
    void ApplyMenuParts(void * model, ModelState & st, int * table)
    {
        auto * car = At<unsigned char *>(model, kModelCar);
        const int style = *reinterpret_cast<int *>(car + kCarStyle) + 1;
        const auto fe = g_feTables.find(st.name + "." + std::to_string(style));
        if (fe == g_feTables.end())
        {
            Log("Verdeck: Modell %p '%s' [style%d]: keine Menue-Tabelle", model, st.name.c_str(), style);
            return;
        }
        std::set< int > animated;
        for (const auto & p : g_animParts[st.name])
        {
            animated.insert(p.part);
        }

        const int closed41 = TpgStyleValue(st.name, style, kPartStaticRoof);
        const int closedVariant = closed41 > 0 ? closed41 : 1;
        const int race41 = table[kPartStaticRoof];
        st.roofStartValue = (race41 != 0 && race41 != closedVariant) ? 1.0f : 0.0f;
        Log("Verdeck: Modell %p '%s': Teil %d im Rennen %d, geschlossen = %d -> Start %s", model, st.name.c_str(),
            kPartStaticRoof, race41, closedVariant, st.roofStartValue > 0.0f ? "offen" : "geschlossen");

        for (int i = 1; i < kGeometryParts; ++i)
        {
            const int menu = fe->second[i];
            if (menu != 0 || table[i] == 0 || animated.count(i) || i == kPartWindowOuter || i == kPartWindowInner)
            {
                continue;
            }
            Log("Verdeck: Modell %p '%s': Teil %d Variante %d -> %d (wie Menue)", model, st.name.c_str(), i, table[i],
                menu);
            st.roofSaved.emplace_back(i, table[i]);
            table[i] = menu;
        }
    }

    void EnableRoofParts(void * model, ModelState & st)
    {
        const auto it = g_animParts.find(st.name);
        auto * car = At<unsigned char *>(model, kModelCar);
        if (it == g_animParts.end() || !car)
        {
            Log("Verdeck: Modell %p '%s': keine animierten Teile bekannt (Auto vorher im Menue ansehen)", model,
                st.name.c_str());
            return;
        }
        auto * table = reinterpret_cast<int *>(car + kCarPartTable);
        // Nur Styles mit Verdeck: [styleN] der .tpg nennt das animierte Teil mit genau dieser Variante (Cabrio 37=1).
        // Coupés desselben Modells (356B CoupeGSCar2) nennen es nicht, Roadster mit einer anderen Variante (8.3e v5c).
        const int style = *reinterpret_cast<int *>(car + kCarStyle) + 1;
        std::set< int > done;
        bool styleHasRoof = false;
        for (const auto & p : it->second)
        {
            if (p.slot == kAnimSlotRoof && p.part >= 0 && p.part < 120
                && TpgStyleValue(st.name, style, p.part) == p.variant)
            {
                styleHasRoof = true;
            }
        }
        if (!styleHasRoof)
        {
            Log("Verdeck: Modell %p '%s' [style%d]: Style ohne Verdeck (.tpg nennt kein animiertes Verdeck-Teil)", model,
                st.name.c_str(), style);
            return;
        }
        for (const auto & p : it->second)
        {
            if (p.slot != kAnimSlotRoof || p.part < 0 || p.part >= 120 || done.count(p.part)
                || TpgStyleValue(st.name, style, p.part) != p.variant)
            {
                continue;
            }
            done.insert(p.part);
            if (table[p.part] == p.variant)
            {
                continue;
            }
            Log("Verdeck: Modell %p '%s': Teil %d Variante %d -> %d (animiert)", model, st.name.c_str(), p.part,
                table[p.part], p.variant);
            st.roofSaved.emplace_back(p.part, table[p.part]);
            table[p.part] = p.variant;
        }
        if (st.roofSaved.empty())
        {
            Log("Verdeck: Modell %p '%s': kein Teil mit Animation %d umschaltbar", model, st.name.c_str(),
                kAnimSlotRoof);
            return;
        }
        ApplyMenuParts(model, st, table);
        RebuildGeometry(model, st);
        if (st.roofStartValue >= 0.0f)
        {
            const float value = st.roofStartValue > 0.0f ? SlotMax(model, kAnimSlotRoof) - kRoofEndMargin : 0.0f;
            reinterpret_cast<SetAnimSlotFn>(kSetAnimSlot)(model, kAnimSlotRoof, value);
            st.slotValues[kAnimSlotRoof] = value;
            Log("Verdeck: Modell %p '%s': Startstellung %s (Bild %.2f)", model, st.name.c_str(),
                value > 0.0f ? "offen" : "geschlossen", value);
        }
    }

    void ToggleRoof(void * model, ModelState & st)
    {
        if (SlotMax(model, kAnimSlotRoof) <= 0.0f && st.roofSaved.empty())
        {
            EnableRoofParts(model, st);
        }
        const float max = SlotMax(model, kAnimSlotRoof);
        if (max <= 0.0f)
        {
            Log("Verdeck: Modell %p '%s': keine Verdeck-Animation (Slot 1 max = %.2f)", model, st.name.c_str(), max);
            return;
        }
        const float now = SlotValue(model, kAnimSlotRoof);
        // Läuft schon eine Animation, kehrt die Taste sie vom aktuellen Bild aus zum anderen Ende um. (Bis v5a ging es
        // zurück zum Startbild der laufenden Animation; nach mehreren Umkehrungen lag das mitten im Weg und das
        // Verdeck blieb halb offen stehen.)
        const float target = st.roofAnimating ? (st.roofTo > 0.0f ? 0.0f : max) : (now < max * 0.5f ? max : 0.0f);
        st.roofFrom = now;
        st.roofTo = target;
        st.roofStart = GetTickCount();
        st.roofAnimating = true;
        if (st.jiggle == 1)
        {
            st.jiggle = 0;
        }
        PlayCarSound(target > now ? "roofopen" : "roofclose");
        Log("Verdeck: Modell %p '%s': Bild %.2f -> %.2f (max %.2f, %d ms)", model, st.name.c_str(), now, target, max,
            g_roofTimeMs);
    }

    void AnimateRoof(void * model, ModelState & st)
    {
        if (!st.roofAnimating)
        {
            return;
        }
        const float span = std::fabs(st.roofTo - st.roofFrom) / (SlotMax(model, kAnimSlotRoof) > 0.0f
                                                                     ? SlotMax(model, kAnimSlotRoof) : 1.0f);
        const float duration = static_cast<float>(g_roofTimeMs) * (span > 0.0f ? span : 1.0f);
        float t = static_cast<float>(GetTickCount() - st.roofStart) / duration;
        if (t >= 1.0f)
        {
            t = 1.0f;
            st.roofAnimating = false;
        }
        const float eased = t * t * (3.0f - 2.0f * t);
        float value = st.roofFrom + (st.roofTo - st.roofFrom) * eased;
        // Genau auf dem letzten Bild zeigt die Engine für ein Bild die geschlossene Fassung (Bild 4 + 1,0,
        // Folgebild 5; 8.3c v3f). Knapp darunter bleiben.
        value = std::fmin(value, SlotMax(model, kAnimSlotRoof) - kRoofEndMargin);
        reinterpret_cast<SetAnimSlotFn>(kSetAnimSlot)(model, kAnimSlotRoof, value);
        st.slotValues[kAnimSlotRoof] = value; // ein Neuaufbau während der Animation stellt diesen Wert her
        if (!st.roofAnimating)
        {
            Log("Verdeck: Modell %p '%s': fertig bei Bild %.2f", model, st.name.c_str(), SlotValue(model, kAnimSlotRoof));
        }
    }

    // ---- Tasten ----
    bool KeyPressed(int vk, bool & wasDown)
    {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        const bool down = pid == GetCurrentProcessId() && (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool pressed = down && !wasDown;
        wasDown = down;
        return pressed;
    }

    void PollKeys()
    {
        if (KeyPressed(g_windowKey, g_windowKeyDown))
        {
            ++g_windowToggles;
            Log("Fenster-Taste gedrueckt (%s)", InFrontend() ? "Menue" : "Rennen");
        }
        if (KeyPressed(g_roofKey, g_roofKeyDown))
        {
            ++g_roofToggles;
            Log("Verdeck-Taste gedrueckt (%s)", InFrontend() ? "Menue" : "Rennen");
        }
    }

    // Schaden der Scheinwerfer wie in der Leuchten-Logik des Spiels (0x4398C6, doc/LICHT.md §9.3): Ist der Wert
    // [[car + 0x74C] + 0xF4] + 0x4E4 (links) bzw. + 0x4EC (rechts) größer als [0x5B2408], setzt das Spiel Byte 0 bzw. 1
    // auf 0. Das Licht des Spiels brennt, solange eines der beiden Bytes an ist; das Fernlicht ebenso (11.5i).
    bool PlayerHeadlightsBroken()
    {
        const auto player = *reinterpret_cast<const std::uint32_t *>(0x65741C);
        const auto car = CarAt(player);
        auto * model = car ? At<unsigned char *>(car, 0x74C) : nullptr;
        auto * state = model ? At<unsigned char *>(model, kModelCar) : nullptr;
        if (!state)
        {
            return false;
        }
        const float limit = *reinterpret_cast<const float *>(0x5B2408);
        return At<float>(state, 0x4E4) > limit && At<float>(state, 0x4EC) > limit;
    }

    // Fernlicht: solange die Taste gehalten wird (nur Rennen, Spielfenster aktiv), weich über g_highFadeMs.
    // Sind beide Scheinwerfer kaputt, bleibt es aus wie das Licht des Spiels; die Lichthupe öffnet die Klappen trotzdem.
    float UpdateHighBeam()
    {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        const bool down = !InFrontend() && pid == GetCurrentProcessId() && (GetAsyncKeyState(g_highKey) & 0x8000) != 0;
        const DWORD now = GetTickCount();
        const float step = g_highTick != 0 && g_highFadeMs > 0
            ? static_cast<float>(now - g_highTick) / static_cast<float>(g_highFadeMs) : 1.0f;
        g_highTick = now;
        g_highDown = down;
        const bool broken = !InFrontend() && PlayerHeadlightsBroken();
        if (broken != g_highBroken)
        {
            g_highBroken = broken;
            Log("Licht: Scheinwerfer des Spielerautos %s", broken ? "kaputt, kein Fernlicht" : "wieder heil");
        }
        g_highBeam = broken ? 0.0f : down ? std::fmin(1.0f, g_highBeam + step) : std::fmax(0.0f, g_highBeam - step);
        if (down && !g_highLogged)
        {
            g_highLogged = true;
            Log("Licht: Fernlicht zum ersten Mal eingeschaltet");
        }
        return g_highBeam;
    }

    // ---- Messlog ----
    void MeasureModel(void * model, ModelState & st)
    {
        const bool fe = InFrontend();
        auto * car = At<unsigned char *>(model, kModelCar);
        if (!car || st.name == "garage")
        {
            return;
        }
        auto * cfg = *reinterpret_cast<unsigned char **>(car + kCarConfig);
        const int style = *reinterpret_cast<int *>(car + kCarStyle);
        const int roof = cfg ? *reinterpret_cast<int *>(cfg + kCfgRoof) : -999;
        const int * table = reinterpret_cast<int *>(car + kCarPartTable);

        if (!st.seen && IsTarget(model, st))
        {
            LogTable(model, st);
        }
        if (fe && st.name != "garage")
        {
            g_feTables[st.name + "." + std::to_string(style + 1)].assign(table, table + 120);
        }
        if (!st.seen || roof != st.roof || style != st.style || table[35] != st.t35 || table[47] != st.t47)
        {
            Log("Modell %p '%s' %s: [style%d], Car.Roof(+0x510) %d, +0x594 %d, 35=%d 47=%d, Liste %d, Flags 0x%X",
                model, st.name.c_str(), fe ? "Menue" : "Rennen", style + 1, roof,
                *reinterpret_cast<int *>(car + kCarHasRoof), table[35], table[47],
                At<unsigned char>(model, kModelListOn), *reinterpret_cast<unsigned *>(car + kCarFlags));
            st.roof = roof;
            st.style = style;
            st.t35 = table[35];
            st.t47 = table[47];
        }

        // Mesh-Animationen; im Rennen laufen 4–7 ständig (Nadeln, Lichter), dort nur 1 und 2.
        const DWORD now = GetTickCount();
        for (int s = 0; s < kAnimSlotCount; ++s)
        {
            if (!fe && s != 1 && s != 2)
            {
                continue;
            }
            const std::size_t base = kModelAnimSlots + s * kAnimSlotSize;
            AnimSlotSnapshot cur{ At<int>(model, base), At<float>(model, base + 8), At<float>(model, base + 0xC) };
            AnimSlotSnapshot & last = st.slots[s];
            const bool changed = cur.frame != last.frame || std::fabs(cur.frac - last.frac) > 1e-3f
                                 || cur.max != last.max;
            if (changed && (now - st.slotLogged[s] >= 100 || !st.seen))
            {
                Log("Modell %p '%s' Animation %d: Bild %d + %.3f (max %.2f)", model, st.name.c_str(), s, cur.frame,
                    cur.frac, cur.max);
                last = cur;
                st.slotLogged[s] = now;
            }
        }
        st.seen = true;
    }

    // ---- Teile-Katalog (alle Geometrie-Teile der .crp, wie FUN_0043c1b0 sie durchläuft) ----
    // Daten eines Teil-Eintrags: Unter-Eintrag 'Base', Teil/Variante bei +0x44/+0x45 (wie 0x43C282)
    bool ReadPartBase(unsigned char * entry, int & part, int & variant)
    {
        auto * child = reinterpret_cast<CrpChildFn>(kCrpChild)(entry, kTagBase);
        auto * first = reinterpret_cast<CrpBaseFn>(kCrpBase)(entry);
        auto * end = first + ((At<std::uint32_t>(entry, 4) >> 5) << 4) + (At<std::uint32_t>(entry, 8) << 4);
        if (!child || child == end)
        {
            return false;
        }
        auto offset = At<std::uintptr_t>(child, 0xC);
        const auto * data = reinterpret_cast<unsigned char *>((child[4] & 2) ? reinterpret_cast<std::uintptr_t>(child) + offset
                                                                             : offset);
        part = data[0x44];
        variant = data[0x45];
        return true;
    }

    // Gebaute Liste einer Schicht durchsuchen: Teile mit Mesh-Animation merken (vor allem im Menü).
    void ScanAnimatedParts(void * layer, const std::string & name)
    {
        auto * list = At<unsigned char *>(layer, 0xC);
        const int count = At<int>(layer, 0x10);
        for (int i = 0; list && i < count; ++i)
        {
            auto * e = list + i * kLayerEntrySize;
            auto * hdr = At<unsigned char *>(e, 0x44);
            auto * entry = At<unsigned char *>(e, 0);
            if (!hdr || !entry || hdr[9] == 0)
            {
                continue;
            }
            PartInfo info;
            if (!ReadPartBase(entry, info.part, info.variant))
            {
                continue;
            }
            info.slot = hdr[9];
            auto & known = g_animParts[name];
            bool seen = false;
            for (const auto & k : known)
            {
                seen = seen || (k.part == info.part && k.variant == info.variant && k.slot == info.slot);
            }
            if (!seen)
            {
                known.push_back(info);
                Log("Animiertes Teil '%s': Teil %d Variante %d -> Mesh-Animation %d", name.c_str(), info.part,
                    info.variant, info.slot);
            }
        }
    }

    void LogLayerParts(void * model, void * layer, const char * why)
    {
        auto * list = At<unsigned char *>(layer, 0xC);
        const int count = At<int>(layer, 0x10);
        std::string line;
        for (int i = 0; list && i < count; ++i)
        {
            auto * e = list + i * kLayerEntrySize;
            auto * hdr = At<unsigned char *>(e, 0x44);
            int part = -1;
            int variant = -1;
            if (!At<unsigned char *>(e, 0) || !ReadPartBase(At<unsigned char *>(e, 0), part, variant))
            {
                continue;
            }
            char buf[32];
            if (hdr && hdr[9])
            {
                std::snprintf(buf, sizeof(buf), " %d/%d/A%d", part, variant, hdr[9]);
            }
            else
            {
                std::snprintf(buf, sizeof(buf), " %d/%d", part, variant);
            }
            line += buf;
        }
        Log("Teile Schicht 1 Modell %p (%s, %d):%s", model, why, count, line.c_str());
    }

    void LogTable(void * model, const ModelState & st)
    {
        auto * car = At<unsigned char *>(model, kModelCar);
        if (!car)
        {
            return;
        }
        const int * table = reinterpret_cast<int *>(car + kCarPartTable);
        std::string line;
        for (int i = 0; i < 120; ++i)
        {
            if (table[i] != 0)
            {
                char buf[24];
                std::snprintf(buf, sizeof(buf), " %d=%d", i, table[i]);
                line += buf;
            }
        }
        Log("Tabelle Modell %p '%s' (%s):%s", model, st.name.c_str(), InFrontend() ? "Menue" : "Rennen", line.c_str());
    }

    std::vector< PartInfo > ReadCatalog(void * crp)
    {
        std::vector< PartInfo > parts;
        auto * base = reinterpret_cast<CrpBaseFn>(kCrpBase)(crp);
        auto * end = base + ((At<std::uint32_t>(crp, 4) >> 5) << 4);
        for (auto * e = reinterpret_cast<CrpFindFn>(kCrpFind)(crp, kTagAtri, -1, -1); e && e < end; e += 16)
        {
            std::uint32_t tag = *reinterpret_cast<std::uint32_t *>(e);
            if (e[4] & 1)
            {
                tag = (tag & 0xFFFF2020u) | 0x2020u;
            }
            if (tag != kTagAtri)
            {
                continue;
            }
            PartInfo info;
            if (ReadPartBase(e, info.part, info.variant))
            {
                parts.push_back(info);
            }
        }
        return parts;
    }

    void LogCatalog(const std::string & name, const std::vector< PartInfo > & parts)
    {
        Log("Katalog '%s': %u Teile (Teil/Variante)", name.c_str(),
            static_cast<unsigned>(parts.size()));
        std::string line;
        for (const auto & p : parts)
        {
            char buf[32];
            if (p.slot)
            {
                std::snprintf(buf, sizeof(buf), " %d/%d/A%d", p.part, p.variant, p.slot);
            }
            else
            {
                std::snprintf(buf, sizeof(buf), " %d/%d", p.part, p.variant);
            }
            line += buf;
            if (line.size() > 150)
            {
                Log(" %s", line.c_str());
                line.clear();
            }
        }
        if (!line.empty())
        {
            Log(" %s", line.c_str());
        }
    }

    // ---- Messbuild 8.4: Scheiben verschieben ----
    // Die Zeichenfunktion FUN_0043c830 nimmt die Vertex-Positionen eines Listeneintrags aus Eintrag + 0x40, wenn
    // dort ein Puffer steht (Strom 'vt', 16 Byte je Vertex), sonst aus der .crp. Nicht animierte Teile haben keinen
    // Puffer. Ein eigener Puffer je Eintrag gehört zur Render-Schicht dieses Modells; Gegner mit demselben Auto
    // teilen nur die .crp-Daten und bleiben unverändert.
    struct Box
    {
        float lo[3] = { 1e30f, 1e30f, 1e30f };
        float hi[3] = { -1e30f, -1e30f, -1e30f };
        bool empty = true;
        void Add(const float * v)
        {
            for (int a = 0; a < 3; ++a)
            {
                lo[a] = std::fmin(lo[a], v[a]);
                hi[a] = std::fmax(hi[a], v[a]);
            }
            empty = false;
        }
        void Add(const Box & b)
        {
            if (!b.empty)
            {
                Add(b.lo);
                Add(b.hi);
            }
        }
        float Size(int a) const { return hi[a] - lo[a]; }
        float Mid(int a) const { return (hi[a] + lo[a]) * 0.5f; }
    };

    // Ursprüngliche Positionen eines Teils in einer Schicht: Unter-Eintrag 'vt' | Schicht-Id (wie FUN_0043e120 Bild 0)
    const float * VertexData(unsigned char * atri, unsigned layerId, std::uint32_t & count)
    {
        count = 0;
        if (!atri || layerId == 0xFFFFFFFFu)
        {
            return nullptr;
        }
        const std::uint32_t key = (*reinterpret_cast<std::uint32_t *>(kKeyBase) & 0xFFFF0000u) | (layerId & 0xFFFFu);
        auto * child = reinterpret_cast<CrpChildFn>(kCrpChild)(atri, key);
        auto * first = reinterpret_cast<CrpBaseFn>(kCrpBase)(atri);
        auto * end = first + ((At<std::uint32_t>(atri, 4) >> 5) << 4) + (At<std::uint32_t>(atri, 8) << 4);
        if (!child || child == end)
        {
            return nullptr;
        }
        const std::uint32_t n = At<std::uint32_t>(child, 8);
        auto offset = At<std::uintptr_t>(child, 0xC);
        if (n == 0 || n >= 65536)
        {
            return nullptr;
        }
        count = n;
        return reinterpret_cast<const float *>((child[4] & 2) ? reinterpret_cast<std::uintptr_t>(child) + offset : offset);
    }

    Box VertexBox(const float * v, std::uint32_t n)
    {
        Box b;
        for (std::uint32_t i = 0; v && i < n; ++i)
        {
            b.Add(v + i * 4);
        }
        return b;
    }

    template <typename F>
    void ForLayerEntries(void * model, ModelState & st, F f)
    {
        for (const auto & l : st.layers)
        {
            std::uint32_t vtable = 0;
            void * owner = nullptr;
            if (!ReadLayerHead(l.layer, vtable, owner) || vtable != l.vtable || owner != model)
            {
                continue;
            }
            auto * list = At<unsigned char *>(l.layer, 0xC);
            const int count = At<int>(l.layer, 0x10);
            for (int i = 0; list && i < count; ++i)
            {
                auto * e = list + i * kLayerEntrySize;
                int part = -1;
                int variant = -1;
                if (At<unsigned char *>(e, 0) && ReadPartBase(At<unsigned char *>(e, 0), part, variant))
                {
                    f(l.layer, i, e, part, variant);
                }
            }
        }
    }

    bool IsWindowPart(int part)
    {
        return part == kPartWindowOuter || part == kPartWindowInner;
    }

    // Scheiben dieses Modells sammeln. Das Spiel legt für Teile mit Kopf-Byte 10 Bit 8 (Scheiben: 0x1A/0x1B) bei
    // aktivem Schadensmodell (dword[0x6197C4] > 0) schon eine Kopie der Positionen in Eintrag + 0x40 an (0x43C58B);
    // sonst legt das Plugin eine an.
    void CollectWindows(void * model, ModelState & st)
    {
        Box glass;
        ForLayerEntries(model, st, [&](void * layer, int index, unsigned char * e, int part, int variant) {
            if (!IsWindowPart(part))
            {
                return;
            }
            const unsigned id = At<unsigned>(layer, 8);
            auto * atri = At<unsigned char *>(e, 0);
            std::uint32_t n = 0;
            const float * v = VertexData(atri, id, n);
            if (!v)
            {
                return;
            }
            const Box b = VertexBox(v, n);
            glass.Add(b);
            ShiftEntry se;
            se.entry = e;
            se.n = n;
            se.height = b.Size(g_windowAxis);
            // Neigung der Scheibe: Ausgleichsgerade Querachse über Hochachse. Oben sind Scheiben nach innen geneigt,
            // eine absinkende Scheibe wandert so leicht nach außen in die Tür.
            if (g_windowSideAxis != g_windowAxis)
            {
                double my = 0.0;
                double mx = 0.0;
                for (std::uint32_t i = 0; i < n; ++i)
                {
                    my += v[i * 4 + g_windowAxis];
                    mx += v[i * 4 + g_windowSideAxis];
                }
                my /= n;
                mx /= n;
                double cov = 0.0;
                double var = 0.0;
                for (std::uint32_t i = 0; i < n; ++i)
                {
                    const double dy = v[i * 4 + g_windowAxis] - my;
                    cov += dy * (v[i * 4 + g_windowSideAxis] - mx);
                    var += dy * dy;
                }
                const double slope = var > 1e-9 ? cov / var : 0.0;
                se.slope = static_cast<float>(slope > 1.0 ? 1.0 : (slope < -1.0 ? -1.0 : slope));
            }
            se.buf = At<float *>(e, 0x40);
            if (!se.buf)
            {
                se.buf = static_cast<float *>(reinterpret_cast<AllocFn>(kAlloc)(n * 16u));
                if (!se.buf)
                {
                    return;
                }
                std::memcpy(se.buf, v, n * 16u);
                At<float *>(e, 0x40) = se.buf;
                se.own = true;
            }
            se.orig.assign(se.buf, se.buf + n * 4u);
            // Brüstungslinie: Ausgleichsgerade durch die untersten Vertices (unteres Viertel der Höhe) über der
            // Längsachse. Hier tritt die Scheibe aus der Tür; tiefer als diese Linie wird nichts verschoben, die
            // Scheibe verschwindet also im Türschlitz statt durch das Blech zu stechen.
            const int lengthAxis = 3 - g_windowAxis - g_windowSideAxis;
            const float down = static_cast<float>(g_windowDownSign);
            const float edge = down < 0.0f ? b.lo[g_windowAxis] : b.hi[g_windowAxis];
            se.beltA = edge;
            if (lengthAxis >= 0 && lengthAxis <= 2 && lengthAxis != g_windowAxis)
            {
                double sz = 0.0, sy = 0.0, szz = 0.0, szy = 0.0;
                int m = 0;
                for (std::uint32_t i = 0; i < n; ++i)
                {
                    const float y = v[i * 4 + g_windowAxis];
                    if (std::fabs(y - edge) <= se.height * 0.25f)
                    {
                        const double z = v[i * 4 + lengthAxis];
                        sz += z;
                        sy += y;
                        szz += z * z;
                        szy += z * y;
                        ++m;
                    }
                }
                const double den = m * szz - sz * sz;
                if (m >= 2 && std::fabs(den) > 1e-9)
                {
                    se.beltB = static_cast<float>((m * szy - sz * sy) / den);
                    se.beltA = static_cast<float>((sy - se.beltB * sz) / m);
                }
                else if (m >= 1)
                {
                    se.beltA = static_cast<float>(sy / m);
                }
            }
            st.shifts.push_back(se);
            if (g_measure)
            {
                Log("  Scheibe: Schicht Id 0x%X Eintrag %d Teil %d/%d, %u Vertices, Hoehe %.3f, Neigung %+.3f, Quer %.3f..%.3f, "
                    "Bruestung %.3f %+.3f*laengs, Puffer %p (%s)", id, index, part, variant, n, se.height, se.slope,
                    b.lo[g_windowSideAxis], b.hi[g_windowSideAxis], se.beltA, se.beltB, se.buf,
                    se.own ? "eigener" : "vom Spiel");
            }
        });
        Log("Scheiben: Modell %p '%s': %u Eintraege, Achse %d, unten %+d, Bereich %.3f..%.3f", model, st.name.c_str(),
            static_cast<unsigned>(st.shifts.size()), g_windowAxis, g_windowDownSign,
            glass.empty ? 0.0f : glass.lo[g_windowAxis], glass.empty ? 0.0f : glass.hi[g_windowAxis]);
    }

    // Weg (in Scheibenhöhen) auf alle gesammelten Scheiben anwenden, ausgehend von den gesicherten Positionen.
    // Jeder Vertex sinkt höchstens bis zur Brüstungslinie (ist er schon tiefer, bleibt er stehen); die Querbewegung
    // folgt dem tatsächlichen Weg des Vertex. Bei Weg 0 stehen genau die gesicherten Positionen im Puffer.
    void ApplyShift(void * model, ModelState & st, float fraction)
    {
        if (fraction == st.shiftApplied)
        {
            return;
        }
        const int lengthAxis = 3 - g_windowAxis - g_windowSideAxis;
        const bool hasLength = lengthAxis >= 0 && lengthAxis <= 2 && lengthAxis != g_windowAxis;
        const float down = static_cast<float>(g_windowDownSign);
        ForLayerEntries(model, st, [&](void *, int, unsigned char * e, int, int) {
            for (const auto & se : st.shifts)
            {
                if (se.entry != e || At<float *>(e, 0x40) != se.buf || se.orig.size() != se.n * 4u)
                {
                    continue;
                }
                // Weg: Brüstung + WindowSink, damit die Oberkante am Ende die Kappungslinie erreicht
                const float delta = down * se.height * fraction * (1.0f + g_windowSink);
                const float sinkBy = down * se.height * g_windowSink;
                for (std::uint32_t i = 0; i < se.n; ++i)
                {
                    const float * o = &se.orig[i * 4u];
                    float * out = se.buf + i * 4u;
                    const float y0 = o[g_windowAxis];
                    // Kappungslinie: Brüstung, um WindowSink abgesenkt. Bis dahin fährt die Scheibe als Ganzes nach
                    // unten (Textur wandert mit), darunter wird sie gestaucht.
                    float belt = se.beltA + (hasLength ? se.beltB * o[lengthAxis] : 0.0f) + sinkBy;
                    belt = down < 0.0f ? std::fmin(belt, y0) : std::fmax(belt, y0);
                    float y = y0 + delta;
                    y = down < 0.0f ? std::fmax(y, belt) : std::fmin(y, belt);
                    out[g_windowAxis] = y;
                    if (g_windowSideAxis != g_windowAxis)
                    {
                        out[g_windowSideAxis] = o[g_windowSideAxis] + se.slope * g_windowTilt * (y - y0);
                    }
                    // Leicht nach vorn, im Verhältnis zum tatsächlichen Weg des Vertex: die Oberkante erreicht am
                    // Ende WindowForward Scheibenhöhen, gekappte Vertices wandern nicht weiter.
                    if (hasLength && lengthAxis != g_windowSideAxis)
                    {
                        out[lengthAxis] = o[lengthAxis] + static_cast<float>(g_windowForwardSign) * g_windowForward
                                                              * std::fabs(y - y0) / (1.0f + g_windowSink);
                    }
                }
            }
        });
        st.shiftApplied = fraction;
    }

    void ReleaseShift(void * model, ModelState & st)
    {
        ForLayerEntries(model, st, [&](void *, int, unsigned char * e, int, int) {
            for (const auto & se : st.shifts)
            {
                if (se.own && se.entry == e && At<float *>(e, 0x40) == se.buf)
                {
                    At<float *>(e, 0x40) = nullptr;
                    reinterpret_cast<FreeFn>(kFree)(se.buf);
                }
            }
        });
        st.shifts.clear();
    }

    void AnimateShift(void * model, ModelState & st)
    {
        if (!st.shiftAnimating)
        {
            return;
        }
        const float span = std::fabs(st.shiftTo - st.shiftFrom) / (g_windowDepth > 0.0f ? g_windowDepth : 1.0f);
        const float duration = static_cast<float>(g_windowTimeMs) * (span > 0.0f ? span : 1.0f);
        float t = static_cast<float>(GetTickCount() - st.shiftStart) / duration;
        if (t >= 1.0f)
        {
            t = 1.0f;
            st.shiftAnimating = false;
        }
        const float eased = t * t * (3.0f - 2.0f * t);
        ApplyShift(model, st, st.shiftFrom + (st.shiftTo - st.shiftFrom) * eased);
        if (!st.shiftAnimating)
        {
            Log("Scheiben: Modell %p '%s': fertig bei Weg %.2f", model, st.name.c_str(), st.shiftApplied);
            if (st.shiftTo > 0.0f)
            {
                if (st.windowShiftOnly)
                {
                    // Keine offene Variante: die Scheiben bleiben gestaucht in der Tür.
                    st.windowsOpen = true;
                }
                else
                {
                    // Unten angekommen: auf die Variante "offen" umschalten (der Neuaufbau verwirft die Verschiebung).
                    SwitchWindowVariants(model, st);
                }
            }
            else
            {
                if (st.windowShiftOnly)
                {
                    st.windowsOpen = false;
                }
                ReleaseShift(model, st);
            }
        }
    }

    void StartShift(void * model, ModelState & st, float target)
    {
        st.shiftFrom = st.shiftApplied;
        st.shiftTo = target;
        st.shiftStart = GetTickCount();
        st.shiftAnimating = true;
        Log("Scheiben: Modell %p '%s': Weg %.2f -> %.2f (%d ms)", model, st.name.c_str(), st.shiftFrom, st.shiftTo,
            g_windowTimeMs);
    }

    // Taste F. Öffnen: Scheiben absenken, unten auf "offen" umschalten. Schließen: auf "zu" umschalten, die Scheiben
    // sofort ganz nach unten setzen (vor dem ersten Zeichnen) und hochfahren. Während einer Bewegung kehrt die
    // Taste sie um.
    void ToggleWindows(void * model, ModelState & st)
    {
        if (st.shiftAnimating)
        {
            PlayCarSound("window");
            StartShift(model, st, st.shiftTo > 0.0f ? 0.0f : g_windowDepth);
            return;
        }
        // Ohne [Windows]-Eintrag (Coupés, Targas ohne Opener-Werte) gibt es keine offene Variante: die Scheiben
        // werden nur abgesenkt und bleiben gestaucht in der Tür (8.3e v5d).
        if (!st.windowsOpen)
        {
            st.windowShiftOnly = OpenValues(model, st, false) == nullptr;
        }
        if (st.windowShiftOnly)
        {
            if (!g_windowAnimate || g_windowDepth <= 0.0f)
            {
                Log("Fenster: Modell %p '%s': keine offene Variante und WindowAnimate = 0", model, st.name.c_str());
                return;
            }
            PlayCarSound("window");
            if (st.shifts.empty())
            {
                CollectWindows(model, st);
                if (st.shifts.empty())
                {
                    Log("Fenster: Modell %p '%s': keine Scheiben in der Geometrie", model, st.name.c_str());
                    return;
                }
                if (st.windowsOpen)
                {
                    ApplyShift(model, st, g_windowDepth);
                }
            }
            StartShift(model, st, st.windowsOpen ? 0.0f : g_windowDepth);
            return;
        }
        PlayCarSound("window");
        if (!g_windowAnimate || g_windowDepth <= 0.0f)
        {
            SwitchWindowVariants(model, st);
            return;
        }
        if (!st.windowsOpen)
        {
            CollectWindows(model, st);
            if (st.shifts.empty())
            {
                SwitchWindowVariants(model, st);
                return;
            }
            StartShift(model, st, g_windowDepth);
        }
        else
        {
            SwitchWindowVariants(model, st);
            CollectWindows(model, st);
            if (st.shifts.empty())
            {
                return;
            }
            ApplyShift(model, st, g_windowDepth);
            StartShift(model, st, 0.0f);
        }
    }

    // ---- Detours ----
    void __fastcall HookBuildGeo(void * layer, void *, void * crp)
    {
        g_origBuildGeo(layer, crp);
        void * model = At<void *>(layer, 4);
        auto it = g_models.find(model);
        if (it == g_models.end())
        {
            return;
        }
        auto & st = it->second;
        st.shifts.clear();
        st.shiftApplied = st.shiftFrom = st.shiftTo = 0.0f;
        st.shiftAnimating = false;
        st.reapplyShift = st.windowShiftOnly && st.windowsOpen;
        bool known = false;
        for (auto & l : st.layers)
        {
            if (l.layer == layer)
            {
                l.crp = crp;
                l.vtable = At<std::uint32_t>(layer, 0);
                known = true;
            }
        }
        if (!known)
        {
            st.layers.push_back({ layer, crp, At<std::uint32_t>(layer, 0) });
            if (g_measure)
            {
                Log("Schicht: Modell %p '%s' Schicht %p vtable %08X Id 0x%X: %d Teile", model, st.name.c_str(), layer,
                    At<unsigned>(layer, 0), At<unsigned>(layer, 8), At<int>(layer, 0x10));
            }
        }
        ScanAnimatedParts(layer, st.name);
        if (g_measure && At<unsigned>(layer, 8) == 1 && IsTarget(model, st))
        {
            LogLayerParts(model, layer, "Aufbau durch Spiel");
        }
        if (crp && !g_catalog.count(st.name))
        {
            g_catalog[st.name] = ReadCatalog(crp);
            if (g_measure)
            {
                LogCatalog(st.name, g_catalog[st.name]);
            }
        }
    }

    int __fastcall HookLoad(void * model, void *, const char * name)
    {
        auto & st = g_models[model];
        st = ModelState{};
        st.name = CarKey(name);
        st.windowToggleSeen = g_windowToggles;
        st.roofToggleSeen = g_roofToggles;
        Log("Laden: Modell %p Name '%s' -> '%s' (%s)", model, name ? name : "(null)", st.name.c_str(),
            InFrontend() ? "Menue" : "Rennen");
        return g_origLoad(model, name);
    }

    void __fastcall HookUpdate(void * model, void *)
    {
        static bool soundsOpen = false;
        if (!soundsOpen)
        {
            soundsOpen = true;
            OpenSound("roofopen", g_roofOpenSound, g_roofVolume);
            OpenSound("roofclose", g_roofCloseSound, g_roofVolume);
            OpenSound("window", g_windowSound, g_windowVolume);
        }
        PollKeys();
        auto it = g_models.find(model);
        if (it != g_models.end())
        {
            auto & st = it->second;
            const bool target = IsTarget(model, st);
            if (st.windowToggleSeen != g_windowToggles)
            {
                st.windowToggleSeen = g_windowToggles;
                if (target)
                {
                    ToggleWindows(model, st);
                }
            }
            if (st.roofToggleSeen != g_roofToggles)
            {
                st.roofToggleSeen = g_roofToggles;
                if (target && (!InFrontend() || g_roofInMenu))
                {
                    ToggleRoof(model, st);
                }
            }
            AnimateRoof(model, st);
            if (st.reapplyShift && !st.shiftAnimating)
            {
                // Nach einem Neuaufbau (Verdeck, Spiel) die abgesenkten Scheiben wieder absenken
                st.reapplyShift = false;
                CollectWindows(model, st);
                ApplyShift(model, st, g_windowDepth);
            }
            AnimateShift(model, st);
            UpdateYear(model, st);
            UpdateFlasher(model, st);
            UpdateGauges(model, st);
            if (g_measure)
            {
                MeasureModel(model, st);
            }
        }
        g_origUpdate(model);
        auto after = g_models.find(model);
        if (after != g_models.end() && after->second.jiggle > 0)
        {
            JiggleAnimations(model, after->second);
        }
    }

    void __fastcall HookSetRoof(void * feCar, void *, int roof)
    {
        Log("Menue: Car.Roof setzen (FUN_004D1800) Wert %d", roof);
        g_origSetRoof(feCar, roof);
    }

    // ---- Byte-Patch: nur wenn die Originalbytes passen ----
    bool PatchBytes(const char * name, std::uintptr_t target, const unsigned char * expected,
                    const unsigned char * replacement, std::size_t size)
    {
        if (std::memcmp(reinterpret_cast<void *>(target), expected, size) != 0)
        {
            Log("Patch %s @%08X: Bytes passen nicht, uebersprungen", name, static_cast<unsigned>(target));
            return false;
        }
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void *>(target), size, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(reinterpret_cast<void *>(target), replacement, size);
        VirtualProtect(reinterpret_cast<void *>(target), size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(target), size);
        Log("Patch %s @%08X installiert", name, static_cast<unsigned>(target));
        return true;
    }

    // B19: Entfernung² des Abschnitts vor dem Vergleich mit den Reichweiten skalieren (ersetzt 0x450E48..0x450E50:
    // FILD qword [EBP-0x10]; LEA EAX,[ESI+ESI*2]; FSTP float [EBP-0xC]). [EBP-0xC] dient nur diesem Vergleich,
    // die Sortier-Entfernung der Warteschlange (param_2) bleibt unverändert.
    // B21: Strecken-Objekte ohne 0x20000/0x10000/0x100000 kommen hier an (0x450F81). Das Spiel zeichnet sie nur
    // mit 0x40000 und gesetztem Detail-Schalter 0x6197E6, alle anderen verwirft es stillschweigend.
    // Rückgabe 1 = zeichnen (weiter bei 0x450F91, deckende Warteschlange), 0 = verwerfen (0x450FA0).
    std::set< std::uint32_t > g_skipFlagsSeen;
    std::set< const void * > g_skipDumped;
    DWORD g_skipKeyTick = 0;
    DWORD g_skipDumpUntil = 0;
    bool g_skipKeyDown = false;
    unsigned g_skipDumpCount = 0;

    // B21: Auswahl der Detailstufe je Abschnitt (ersetzt 0x450E86..0x450E8C: LEA EAX,[ESI+ESI*2];
    // LEA EDX,[EDI+EAX*4+0x64]). header = Kopf des Abschnitts (EDI), chosen = Stufe des Spiels (ESI),
    // dist2 = Entfernung² (param_2). Rückgabe: zu zeichnende Stufe. Auf Taste werden die Stufen-Tabellen
    // der Abschnitte nahe der Kamera protokolliert.
    std::set< const void * > g_levelDumped;
    DWORD g_levelKeyTick = 0;
    DWORD g_levelDumpUntil = 0;
    bool g_levelKeyDown = false;
    unsigned g_levelDumpCount = 0;

    unsigned __cdecl TrackLevelSelect(const std::uint8_t * header, unsigned chosen, float dist2)
    {
        const unsigned count = *reinterpret_cast<const std::uint32_t *>(header + 0x0C);
        unsigned use = chosen;
        if (g_trackDetail == 1 || g_trackDetail == 2)
        {
            use = 0;
        }
        else if (g_trackDetail == 3 && count > 0)
        {
            use = count - 1;
        }

        const DWORD now = GetTickCount();
        if (now - g_levelKeyTick >= 15)
        {
            g_levelKeyTick = now;
            const bool down = (GetAsyncKeyState(g_trackDiagKey) & 0x8000) != 0;
            if (down && !g_levelKeyDown)
            {
                g_levelDumpUntil = now + 100;
                g_levelDumped.clear();
                g_levelDumpCount = 0;
                const auto * cam = reinterpret_cast<const float *>(
                    *reinterpret_cast<const std::uintptr_t *>(*reinterpret_cast<const std::uintptr_t *>(0x628130) + 0x10) +
                    0x34);
                Log("B21: Messung (Taste), Kamera %.1f %.1f %.1f, TrackDetail %d", cam[0], cam[1], cam[2], g_trackDetail);
            }
            g_levelKeyDown = down;
        }
        const float dist = dist2 > 0.0f ? std::sqrt(dist2) : 0.0f;
        if (static_cast<int>(g_levelDumpUntil - now) > 0 && dist < 150.0f && g_levelDumpCount < 200 &&
            g_levelDumped.insert(header).second)
        {
            ++g_levelDumpCount;
            Log("B21:   Abschnitt %p Flags 0x%08X, Abstand %.1f, %u Stufen, Spiel %u, gezeichnet %u", static_cast<const void *>(header),
                *reinterpret_cast<const std::uint32_t *>(header), dist, count, chosen, use);
            for (unsigned i = 0; i < count && i < 8; ++i)
            {
                const std::uint8_t * e = header + 0x64 + i * 12;
                Log("B21:     Stufe %u: Objekte %u, Meshes %u, Ids %u..%u, Reichweite %u", i, e[0], e[1],
                    *reinterpret_cast<const std::uint16_t *>(e + 4), *reinterpret_cast<const std::uint16_t *>(e + 6),
                    *reinterpret_cast<const std::uint16_t *>(e + 10));
            }
        }
        return use;
    }

    __declspec(naked) void TrackLevelDetour()
    {
        __asm
        {
            pushad
            push dword ptr [ebp + 0x0C]
            push esi
            push edi
            call TrackLevelSelect
            add esp, 12
            mov [esp + 28], eax
            popad
            lea eax, [eax + eax * 2]
            lea edx, [edi + eax * 4 + 0x64]
            push 0x450E8D
            ret
        }
    }

    int __cdecl TrackSkipCheck(std::uint32_t flags, const float * item)
    {
        const bool detail = *reinterpret_cast<const volatile std::uint8_t *>(0x6197E6) != 0;
        if ((flags & 0x40000) != 0 && detail)
        {
            return 1;
        }
        if (g_skipFlagsSeen.insert(flags).second)
        {
            Log("B21: verworfenes Strecken-Objekt, neue Flags 0x%08X (Detail-Schalter %d), erstes %p bei %.1f %.1f %.1f",
                flags, detail ? 1 : 0, static_cast<const void *>(item), item[2], item[3], item[4]);
        }

        // Taste: ein kurzes Fenster lang alle verworfenen Objekte nahe der Kamera protokollieren.
        const DWORD now = GetTickCount();
        if (now - g_skipKeyTick >= 15)
        {
            g_skipKeyTick = now;
            const bool down = (GetAsyncKeyState(g_trackDiagKey) & 0x8000) != 0;
            if (down && !g_skipKeyDown)
            {
                g_skipDumpUntil = now + 100;
                g_skipDumped.clear();
                g_skipDumpCount = 0;
                const auto * cam = reinterpret_cast<const float *>(
                    *reinterpret_cast<const std::uintptr_t *>(*reinterpret_cast<const std::uintptr_t *>(0x628130) + 0x10) +
                    0x34);
                Log("B21: Messung (Taste), Kamera %.1f %.1f %.1f", cam[0], cam[1], cam[2]);
            }
            g_skipKeyDown = down;
        }
        if (static_cast<int>(g_skipDumpUntil - now) > 0 && g_skipDumpCount < 300 && g_skipDumped.insert(item).second)
        {
            const auto * cam = reinterpret_cast<const float *>(
                *reinterpret_cast<const std::uintptr_t *>(*reinterpret_cast<const std::uintptr_t *>(0x628130) + 0x10) + 0x34);
            const float dx = item[2] - cam[0];
            const float dy = item[3] - cam[1];
            const float dz = item[4] - cam[2];
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < 200.0f)
            {
                ++g_skipDumpCount;
                Log("B21:   verworfen %p Flags 0x%08X +4 0x%08X bei %.1f %.1f %.1f, Abstand %.1f", static_cast<const void *>(item),
                    flags, reinterpret_cast<const std::uint32_t *>(item)[1], item[2], item[3], item[4], dist);
            }
        }
        return g_trackDiag == 2 ? 1 : 0;
    }

    // Ersetzt 0x450F81..0x450F90 (TEST EAX,0x40000; JZ; MOV AL,[0x6197E6]; TEST AL,AL; JZ). EAX = Flags, ESI = Objekt.
    __declspec(naked) void TrackFlagDetour()
    {
        __asm
        {
            pushad
            push esi
            push eax
            call TrackSkipCheck
            add esp, 8
            mov [esp + 28], eax
            popad
            test eax, eax
            jz skip
            push 0x450F91
            ret
        skip:
            push 0x450FA0
            ret
        }
    }

    __declspec(naked) void TrackDistDetour()
    {
        __asm
        {
            fild qword ptr [ebp - 0x10]
            fmul dword ptr [g_trackDistScale]
            lea eax, [esi + esi * 2]
            fstp dword ptr [ebp - 0x0C]
            push 0x450E51
            ret
        }
    }

    // ---- B15: Welche Liste der Render-Warteschlange zeichnet gerade? (doc/BUGS.md §12) ----
    // FUN_00466660 leert vier Listen nacheinander: 1 = Strecken-Objekte mit Flag 0x20000 (darunter die fertigen
    // Wasserspiegelungen), 2 = Flag 0x100000 (Zustand 0x24 = 0xA0), 3 = 0x10000/0x40000 (deckende Szenerie), 4 = weitere.
    // 0 = außerhalb der Warteschlange (Autos, Himmel, HUD). dx11z fragt das je Draw über LfspQueueList ab.
    volatile int g_queueList = 0;

    // Ersetzt CALL dword ptr [0x6BD97C] (6 Byte) vor Liste 1/2/3: Liste merken, Aufruf mit der alten Rücksprungadresse.
    __declspec(naked) void QueueList1Detour()
    {
        __asm
        {
            mov dword ptr [g_queueList], 1
            push 0x466673
            jmp dword ptr ds:[0x6BD97C]
        }
    }

    __declspec(naked) void QueueList2Detour()
    {
        __asm
        {
            mov dword ptr [g_queueList], 2
            push 0x4666BC
            jmp dword ptr ds:[0x6BD97C]
        }
    }

    __declspec(naked) void QueueList3Detour()
    {
        __asm
        {
            mov dword ptr [g_queueList], 3
            push 0x466709
            jmp dword ptr ds:[0x6BD97C]
        }
    }

    // Ersetzt 0x466744..0x466748 (MOV ECX,[ESI+0x10]; MOV EDX,[ECX]) vor Liste 4.
    __declspec(naked) void QueueList4Detour()
    {
        __asm
        {
            mov dword ptr [g_queueList], 4
            mov ecx, dword ptr [esi + 0x10]
            mov edx, dword ptr [ecx]
            push 0x466749
            ret
        }
    }

    // Ersetzt JMP 0x466660 in FUN_00467290: Warteschlange leeren lassen, danach Markierung zurücksetzen.
    __declspec(naked) void QueueDrainDetour()
    {
        __asm
        {
            mov eax, 0x466660
            call eax
            mov dword ptr [g_queueList], 0
            ret
        }
    }

    // ---- B21: Dreieck-Clipper von EAGL gegenprüfen (ClipDiag) ----
    // FUN_00543ae0 schickt jedes Dreieck, das einen Rand schneidet, an den Clipper (cdecl, 14 Argumente):
    // Indizes a, b, c in die Vertex-Liste v (8 Floats je Vertex: x, y, z im Kamera-Raum mit Sichtfeld |x|, |y| <= z,
    // dann 1/z), OR der Clip-Codes, Guard-Faktor, Attribut-Schalter, near, far; Ausgabe: Zeiger auf das Polygon und Anzahl.
    // LFSP rechnet das Dreieck selbst gegen near/far und |x|, |y| <= z und vergleicht die sichtbare Fläche (x/z, y/z).
    using ClipFn = void(__cdecl *)(int, int, int, unsigned, float, float *, int, int, int, int, float, float, float **, int *);
    void * g_clipOrig[2] = {};
    unsigned g_clipLost = 0;
    unsigned g_clipLogged = 0;
    DWORD g_clipTick = 0;
    DWORD g_clipKeyTick = 0;
    DWORD g_clipDumpUntil = 0;
    bool g_clipKeyDown = false;

    struct ClipPt { float x, y, z; };

    int ClipPlane(const ClipPt * in, int n, ClipPt * out, float a, float b, float c, float d)
    {
        // behält a*x + b*y + c*z + d >= 0
        int m = 0;
        for (int i = 0; i < n; ++i)
        {
            const ClipPt & p = in[i];
            const ClipPt & q = in[(i + 1) % n];
            const float dp = a * p.x + b * p.y + c * p.z + d;
            const float dq = a * q.x + b * q.y + c * q.z + d;
            if (dp >= 0.0f)
            {
                out[m++] = p;
            }
            if ((dp >= 0.0f) != (dq >= 0.0f))
            {
                const float t = dp / (dp - dq);
                out[m++] = { p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t };
            }
        }
        return m;
    }

    float ScreenArea(const ClipPt * p, int n)
    {
        float area = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const ClipPt & a = p[i];
            const ClipPt & b = p[(i + 1) % n];
            if (a.z <= 0.0f || b.z <= 0.0f)
            {
                return 0.0f;
            }
            area += (a.x / a.z) * (b.y / b.z) - (b.x / b.z) * (a.y / a.z);
        }
        return std::fabs(area) * 0.5f;
    }

    void __cdecl ClipCheck(int which, int ia, int ib, int ic, unsigned codes, float guard, const float * v, float nearz,
                           float farz, float * const * outV, const int * outN)
    {
        ClipPt buf[2][16];
        int n = 3;
        const int idx[3] = { ia, ib, ic };
        for (int i = 0; i < 3; ++i)
        {
            const float * p = v + idx[i] * 8;
            buf[0][i] = { p[0], p[1], p[2] };
        }
        n = ClipPlane(buf[0], n, buf[1], 0.0f, 0.0f, 1.0f, -nearz);
        n = ClipPlane(buf[1], n, buf[0], 0.0f, 0.0f, -1.0f, farz);
        n = ClipPlane(buf[0], n, buf[1], -1.0f, 0.0f, 1.0f, 0.0f);
        n = ClipPlane(buf[1], n, buf[0], 1.0f, 0.0f, 1.0f, 0.0f);
        n = ClipPlane(buf[0], n, buf[1], 0.0f, -1.0f, 1.0f, 0.0f);
        n = ClipPlane(buf[1], n, buf[0], 0.0f, 1.0f, 1.0f, 0.0f);
        const float mine = n >= 3 ? ScreenArea(buf[0], n) : 0.0f;

        const int gn = *outN;
        float game = 0.0f;
        if (gn >= 3 && gn <= 16 && *outV != nullptr)
        {
            ClipPt g[16];
            for (int i = 0; i < gn; ++i)
            {
                const float * p = *outV + i * 8;
                g[i] = { p[0], p[1], p[2] };
            }
            game = ScreenArea(g, gn);
        }

        const DWORD now = GetTickCount();
        if (now - g_clipKeyTick >= 15)
        {
            g_clipKeyTick = now;
            const bool down = (GetAsyncKeyState(g_trackDiagKey) & 0x8000) != 0;
            if (down && !g_clipKeyDown)
            {
                g_clipDumpUntil = now + 100;
                Log("B21 Clip: Messung (Taste)");
            }
            g_clipKeyDown = down;
        }

        // Fläche in (x/z, y/z), das Bild ist 2 x 2; 1e-4 sind grob 20 Pixel.
        if (mine > 1e-4f && game < mine * 0.5f)
        {
            ++g_clipLost;
            const bool dump = static_cast<int>(g_clipDumpUntil - now) > 0;
            if (g_clipLogged < 40 || (dump && g_clipLogged < 400))
            {
                ++g_clipLogged;
                const float * a = v + ia * 8;
                const float * b = v + ib * 8;
                const float * c = v + ic * 8;
                Log("B21 Clip%c: Dreieck verloren, Codes 0x%02X, Guard %.2f, near %.3f far %.1f, Flaeche LFSP %.4f (%d Ecken) Spiel %.4f (%d Ecken)",
                    which ? 'B' : 'A', codes, guard, nearz, farz, mine, n, game, gn);
                Log("B21 Clip:   (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f)", a[0], a[1], a[2], b[0], b[1], b[2], c[0],
                    c[1], c[2]);
            }
        }
        if (now - g_clipTick >= 2000)
        {
            if (g_clipLost != 0)
            {
                Log("B21 Clip: %u sichtbare Dreiecke in %u ms verloren", g_clipLost, static_cast<unsigned>(now - g_clipTick));
            }
            g_clipLost = 0;
            g_clipTick = now;
        }
    }

    template < int Which >
    void __cdecl ClipHook(int ia, int ib, int ic, unsigned codes, float guard, float * v, int p7, int p8, int p9, int p10,
                          float nearz, float farz, float ** outV, int * outN)
    {
        reinterpret_cast<ClipFn>(g_clipOrig[Which])(ia, ib, ic, codes, guard, v, p7, p8, p9, p10, nearz, farz, outV, outN);
        ClipCheck(Which, ia, ib, ic, codes, guard, v, nearz, farz, outV, outN);
    }

    // ---- Sprung in eigenen Code: nur wenn die Originalbytes passen, Rest mit NOP ----
    bool PatchJump(const char * name, std::uintptr_t target, const unsigned char * expected, std::size_t size, void * dest)
    {
        if (size < 5 || std::memcmp(reinterpret_cast<void *>(target), expected, size) != 0)
        {
            Log("Patch %s @%08X: Bytes passen nicht, uebersprungen", name, static_cast<unsigned>(target));
            return false;
        }
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void *>(target), size, PAGE_EXECUTE_READWRITE, &old);
        auto * p = reinterpret_cast<unsigned char *>(target);
        p[0] = 0xE9;
        *reinterpret_cast<std::int32_t *>(p + 1) =
            static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(dest) - (target + 5));
        for (std::size_t i = 5; i < size; ++i)
        {
            p[i] = 0x90;
        }
        VirtualProtect(reinterpret_cast<void *>(target), size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, size);
        Log("Patch %s @%08X installiert", name, static_cast<unsigned>(target));
        return true;
    }

    // ---- Inline-Hook ----
    bool InstallHook(const char * name, std::uintptr_t target, const unsigned char * expected, std::size_t size,
                     void * detour, void ** original)
    {
        if (std::memcmp(reinterpret_cast<void *>(target), expected, size) != 0)
        {
            Log("Hook %s @%08X: Bytes passen nicht, uebersprungen", name, static_cast<unsigned>(target));
            return false;
        }
        auto * tramp = static_cast<unsigned char *>(
            VirtualAlloc(nullptr, size + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!tramp)
        {
            return false;
        }
        std::memcpy(tramp, expected, size);
        tramp[size] = 0xE9;
        *reinterpret_cast<std::int32_t *>(tramp + size + 1) =
            static_cast<std::int32_t>(target + size - (reinterpret_cast<std::uintptr_t>(tramp) + size + 5));

        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void *>(target), size, PAGE_EXECUTE_READWRITE, &old);
        auto * p = reinterpret_cast<unsigned char *>(target);
        p[0] = 0xE9;
        *reinterpret_cast<std::int32_t *>(p + 1) =
            static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(detour) - (target + 5));
        for (std::size_t i = 5; i < size; ++i)
        {
            p[i] = 0x90;
        }
        VirtualProtect(reinterpret_cast<void *>(target), size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, size);
        *original = tramp;
        Log("Hook %s @%08X installiert", name, static_cast<unsigned>(target));
        return true;
    }

    // ---- Leuchten am Modell (11.5d, doc/LICHT.md §9.6) ----
    // Die Leuchten-Schicht des Automodells (ab 0x43D800) prüft jede Leuchte mit FUN_00443660(model, Nummer) gegen
    // ihr Byte in [model + 0x2D4] (0x43D8C4; gespiegelte Hälfte mit eigener Nummer an +0x4E bei 0x43DAC9, x negiert).
    // EBP zeigt dabei auf die Leuchte: Lage +0 (x, y, z am Modell), Farbe +0x40 (0xAARRGGBB, das Spiel nimmt RGB).
    // LFSP merkt sich je Modell Nummer, Lage und Farbe; dx11z fragt das über LfspModelLamps ab.
    constexpr std::uintptr_t kLampTest     = 0x443660;
    constexpr std::uintptr_t kLampCall     = 0x43D8C4;
    constexpr std::uintptr_t kLampCallMirr = 0x43DAC9;
    constexpr int kLampSlots = 16;

    struct LampInfo
    {
        const unsigned char * lamp = nullptr;
        float pos[3] = {};
        std::uint32_t colour = 0;
    };

    struct ModelLamps
    {
        LampInfo slots[kLampSlots];
    };

    std::map< const void *, ModelLamps > g_modelLamps;
    int g_lampLogLines = 0;

    void __cdecl RecordLamp(int mirrored, const void * model, unsigned number, const unsigned char * lamp)
    {
        if (number >= static_cast<unsigned>(kLampSlots) || !model || !lamp)
        {
            return;
        }
        LampInfo & slot = g_modelLamps[model].slots[number];
        if (slot.lamp == lamp)
        {
            return;
        }
        const auto * f = reinterpret_cast<const float *>(lamp);
        slot.lamp = lamp;
        slot.pos[0] = mirrored ? -f[0] : f[0];
        slot.pos[1] = f[1];
        slot.pos[2] = f[2];
        slot.colour = *reinterpret_cast<const std::uint32_t *>(lamp + 0x40);
        if (g_measure && g_lampLogLines < 400)
        {
            ++g_lampLogLines;
            Log("Leuchten: Modell %p Nr %2u%s bei %6.2f %6.2f %6.2f, Farbe %08X / %08X, Typ %u, Flags %02X", model, number,
                mirrored ? " (gespiegelt)" : "", slot.pos[0], slot.pos[1], slot.pos[2], slot.colour,
                *reinterpret_cast<const std::uint32_t *>(lamp + 0x44), lamp[0x4C], lamp[0x48]);
        }
    }

    // 11.5i: Bei eingeschaltetem Licht setzt das Spiel die Bremsleuchten 2/3 und 10/11 auch ohne Bremsen auf 1, nur mit
    // gedimmter Stärke [0x5B35A8] = 0,6 in [model + 0x2D8] (0x4396FC, doc/LICHT.md §9.3), damit sie als Rücklicht
    // leuchten. Das mittige 3. Bremslicht (|x| < kThirdBrakeX) hat keine Rücklicht-Aufgabe: LFSP meldet es dann als aus.
    // Beim Bremsen (und bei 10/11 als Blinker) ist die Stärke 1. Im Menü (Garage) bleibt es wie im Spiel.
    constexpr float kThirdBrakeX = 0.25f;
    std::set< const unsigned char * > g_thirdBrakeLogged;

    std::uint32_t __cdecl LampTest(int mirrored, void * model, unsigned number, const unsigned char * lamp)
    {
        RecordLamp(mirrored, model, number, lamp);
        const std::uint32_t on = reinterpret_cast<std::uint32_t (__thiscall *)(void *, unsigned)>(kLampTest)(model, number);
        if (!g_thirdBrakeOnly || (on & 0xFF) == 0 || !lamp ||
            (number != 2 && number != 3 && number != 10 && number != 11) ||
            std::fabs(*reinterpret_cast<const float *>(lamp)) >= kThirdBrakeX || InFrontend())
        {
            return on;
        }
        const auto * strength = At<const float *>(model, 0x2D8);
        if (!strength || strength[number] > 0.99f)
        {
            return on;
        }
        if (g_thirdBrakeLogged.size() < 32 && g_thirdBrakeLogged.insert(lamp).second)
        {
            Log("Leuchten: 3. Bremslicht Modell %p Nr %u bei x %.2f, Staerke %.2f -> nur beim Bremsen", model, number,
                *reinterpret_cast<const float *>(lamp), strength[number]);
        }
        return on & ~0xFFu;
    }

    // Ersetzt CALL FUN_00443660 (thiscall, 1 Argument) an beiden Stellen: ECX = Modell, [ESP + 4] = Lampennummer,
    // EBP = Leuchte. LampTest prüft die Leuchte über das Original, Rückgabe in AL.
    __declspec(naked) void LampTestDetour()
    {
        __asm
        {
            push ebp
            push dword ptr [esp + 8]
            push ecx
            push 0
            call LampTest
            add esp, 16
            ret 4
        }
    }

    __declspec(naked) void LampTestMirroredDetour()
    {
        __asm
        {
            push ebp
            push dword ptr [esp + 8]
            push ecx
            push 1
            call LampTest
            add esp, 16
            ret 4
        }
    }

    // ---- HUD-Instrumente nach Lichtschalter (11.3, doc/LICHT.md §10) ----
    // Tacho (FUN_0042e710) und Drehzahlmesser (FUN_0042f3a0) wählen beim Aufbau des HUD Zifferblatt und Zeiger nach dem
    // Nachtflag 0x628A28 aus Namenstabellen [Tag, Nacht] und legen über FUN_004212a0 vier Bild-Objekte an (Instrument
    // + 0xC4/0xC8/0xCC/0xD0 = Felder 0x31..0x34; 0x31 Zifferblatt, 0x33/0x34 Zeiger, 0x32 Rahmen gleich). LFSP legt
    // dazu die Bilder der anderen Fassung an und tauscht im Rennen je nach Lichtschalter (car + 0xD87 Bit 0, nur L;
    // die Lichthupe ändert den Schalter nicht). Vor dem Abbau stellt LFSP die Bilder des Spiels zurück und gibt die
    // eigenen über ihren Destruktor (vtable Slot 0, Argument 1) frei, wie FUN_004215f0 es mit seinen macht.
    constexpr std::uintptr_t kGaugeCreate     = 0x4212A0;
    constexpr std::uintptr_t kGaugeCallSpeedo = 0x42E7FB;
    constexpr std::uintptr_t kGaugeCallTach   = 0x42F464;
    constexpr std::uintptr_t kGaugeDtorDelete = 0x421580; // vtable 0x5B2AAC Slot 0 (thiscall, Flags)
    constexpr std::uintptr_t kGaugeDtor       = 0x4215F0; // von den Destruktoren der Tacho-/Drehzahl-Klassen
    constexpr std::uintptr_t kHudAlloc        = 0x59EF90; // cdecl(Größe)
    constexpr std::uintptr_t kHudImage        = 0x422DF0; // Bild-Objekt anlegen und Grafik laden (thiscall, 10 Args)
    constexpr std::size_t kGaugeSlots[3]      = { 0xC4, 0xCC, 0xD0 };
    // Bildnummer: FUN_00422df0 vergibt image + 0x100 = [0x60C9A0]++; FUN_0041a180 schreibt die Quads jedes Bildes in
    // einen Puffer mit fünf Plätzen je Nummer (0x60AB18 …), angelegt für die Bilder des HUD. Zusätzliche Bilder dürfen
    // keine neuen Nummern verbrauchen (v6k: Absturz 0x53879E, weil spätere HUD-Bilder hinter den Puffer kamen).
    constexpr std::uintptr_t kHudImageIndex   = 0x60C9A0;
    constexpr std::size_t kImageIndex         = 0x100;
    // Ein Bild darf im Rennen nicht freigegeben werden: sein Destruktor (FUN_00423690) leert den ganzen Grafik-Cache
    // 0x60C99C aller HUD-Bilder. Die eigenen Bilder entstehen deshalb beim Aufbau und gehen erst mit dem Instrument.
    // Namenstabellen [Tag, Nacht]: tach, needle, speedK, speedM, needle2
    constexpr std::uintptr_t kGaugeNames[5]   = { 0x5CCFCC, 0x5CCFD4, 0x5CCEFC, 0x5CCF04, 0x5CCF0C };

    using GaugeCreateFn = void * (__thiscall *)(void * gauge, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    using HudAllocFn    = void * (__cdecl *)(std::size_t size);
    using HudImageFn    = void * (__thiscall *)(void * image, const char * name, std::uint32_t, std::uint32_t, std::uint32_t,
                                                std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                                std::uint32_t);
    using DeleteFn      = void (__thiscall *)(void * object, int flags);
    using GaugeDtorFn   = void (__fastcall *)(void * gauge);

    struct GaugeVariants
    {
        unsigned char * gauge = nullptr;
        int created = 0;                 // Fassung, die das Spiel angelegt hat (0 Tag, 1 Nacht)
        int shown = 0;
        void * images[2][3] = {};
    };
    std::vector< GaugeVariants > g_gauges;
    DeleteFn g_origGaugeDtorDelete = nullptr;
    GaugeDtorFn g_origGaugeDtor = nullptr;

    // Anderer Name zum Tabellenzeiger name: 0 Tag / 1 Nacht als variant, nullptr wenn unbekannt.
    const char * OtherGaugeName(std::uint32_t name, int & variant)
    {
        for (std::uintptr_t table : kGaugeNames)
        {
            const auto * pair = reinterpret_cast<const std::uint32_t *>(table);
            for (int v = 0; v < 2; ++v)
            {
                if (pair[v] == name)
                {
                    variant = v;
                    return reinterpret_cast<const char *>(pair[1 - v]);
                }
            }
        }
        return nullptr;
    }

    void * NewHudImage(const char * name, std::uint32_t p2, std::uint32_t p3, std::uint32_t p4, std::uint32_t p5,
                       std::uint32_t p6, std::uint32_t p10)
    {
        void * memory = reinterpret_cast<HudAllocFn>(kHudAlloc)(0x10C);
        if (!memory)
        {
            return nullptr;
        }
        return reinterpret_cast<HudImageFn>(kHudImage)(memory, name, p2, p3, p4, p5, p6, 0, 1, 0, p10);
    }

    void * __fastcall HookGaugeCreate(void * gauge, void *, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7, std::uint32_t a8, std::uint32_t a9, std::uint32_t a10, std::uint32_t a11, std::uint32_t a12, std::uint32_t a13, std::uint32_t a14, std::uint32_t a15, std::uint32_t a16, std::uint32_t a17, std::uint32_t a18, std::uint32_t a19, std::uint32_t a20, std::uint32_t a21, std::uint32_t a22, std::uint32_t a23, std::uint32_t a24)
    {
        void * result = reinterpret_cast<GaugeCreateFn>(kGaugeCreate)(gauge, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24);
        // Argumente wie FUN_004212a0: a3 Zifferblatt (Feld 0x31), a5 Zeiger (0x34), a7 Zeiger (0x33)
        int vDial = -1;
        int vNeedle = -1;
        int vNeedle2 = -1;
        const char * dial = OtherGaugeName(a3, vDial);
        const char * needle = OtherGaugeName(a5, vNeedle);
        const char * needle2 = OtherGaugeName(a7, vNeedle2);
        if (!g_gaugesFollowLights || !dial || !needle || !needle2 || vDial != vNeedle || vDial != vNeedle2)
        {
            Log("Instrumente: Instrument %p ohne Tag-/Nacht-Paar (%s), nicht umschaltbar", gauge,
                a3 ? reinterpret_cast<const char *>(a3) : "-");
            return result;
        }
        GaugeVariants g;
        g.gauge = static_cast<unsigned char *>(gauge);
        g.created = vDial;
        g.shown = vDial;
        for (int k = 0; k < 3; ++k)
        {
            g.images[vDial][k] = At<void *>(gauge, kGaugeSlots[k]);
        }
        const int other = 1 - vDial;
        auto & nextIndex = *reinterpret_cast<int *>(kHudImageIndex);
        const int savedIndex = nextIndex;
        g.images[other][0] = NewHudImage(dial, a4, a9, a10, a22, a23, 1);
        g.images[other][1] = NewHudImage(needle2, a8, a9, a10, a15, a16, 0);
        g.images[other][2] = NewHudImage(needle, a6, a9, a10, 0x14, 0x14, 0);
        // Gleiche Nummer wie das Bild, das ersetzt wird (es zeichnet immer nur eines davon), Zähler wie im Original.
        nextIndex = savedIndex;
        for (int k = 0; k < 3; ++k)
        {
            if (g.images[other][k] && g.images[vDial][k])
            {
                At<int>(g.images[other][k], kImageIndex) = At<int>(g.images[vDial][k], kImageIndex);
            }
        }
        if (!g.images[other][0] || !g.images[other][1] || !g.images[other][2])
        {
            for (void * image : g.images[other])
            {
                if (image)
                {
                    reinterpret_cast<DeleteFn>(**reinterpret_cast<void ***>(image))(image, 1);
                }
            }
            Log("Instrumente: Instrument %p: andere Fassung nicht angelegt", gauge);
            return result;
        }
        g_gauges.push_back(g);
        Log("Instrumente: Instrument %p '%s' (%s): auch '%s' geladen, Bildnummern %d/%d/%d, naechste %d", gauge,
            reinterpret_cast<const char *>(a3), vDial ? "Nacht" : "Tag", dial, At<int>(g.images[other][0], kImageIndex),
            At<int>(g.images[other][1], kImageIndex), At<int>(g.images[other][2], kImageIndex), savedIndex);
        return result;
    }

    // Vor dem Abbau: Bilder des Spiels zurück, eigene freigeben, Eintrag entfernen.
    void ReleaseGauge(void * gauge)
    {
        for (auto it = g_gauges.begin(); it != g_gauges.end(); ++it)
        {
            if (it->gauge != gauge)
            {
                continue;
            }
            for (int k = 0; k < 3; ++k)
            {
                At<void *>(gauge, kGaugeSlots[k]) = it->images[it->created][k];
            }
            for (void * image : it->images[1 - it->created])
            {
                if (image)
                {
                    reinterpret_cast<DeleteFn>(**reinterpret_cast<void ***>(image))(image, 1);
                }
            }
            g_gauges.erase(it);
            Log("Instrumente: Instrument %p abgebaut, eigene Bilder freigegeben", gauge);
            return;
        }
    }

    void __fastcall HookGaugeDtorDelete(void * gauge, void *, int flags)
    {
        ReleaseGauge(gauge);
        g_origGaugeDtorDelete(gauge, flags);
    }

    void __fastcall HookGaugeDtor(void * gauge)
    {
        ReleaseGauge(gauge);
        g_origGaugeDtor(gauge);
    }

    // Je Modell-Update des Spielerautos: Fassung nach dem Lichtschalter (Bit 0 von car + 0xD87).
    void UpdateGauges(void * model, ModelState & st)
    {
        auto * car = At<unsigned char *>(model, kModelCar);
        if (g_gauges.empty() || !car || st.name == "garage" || InFrontend() ||
            (*reinterpret_cast<unsigned *>(car + kCarFlags) & kCarFlagPlayer) == 0)
        {
            return;
        }
        const int want = (car[kCarLampBits] & 1) != 0 ? 1 : 0;
        for (auto & g : g_gauges)
        {
            if (g.shown == want)
            {
                continue;
            }
            for (int k = 0; k < 3; ++k)
            {
                At<void *>(g.gauge, kGaugeSlots[k]) = g.images[want][k];
            }
            g.shown = want;
            Log("Instrumente: Instrument %p auf %s-Fassung (Lichtschalter %s)", g.gauge, want ? "Nacht" : "Tag",
                want ? "an" : "aus");
        }
    }

    // ---- Kamera: Cinematic (11.1) und Action (11.2), doc/KAMERA.md ----
    // Kamera-Zustand je Spieler ab 0x627E80 (Abstand 0x10C): +0 Zielauto, +0x24 Position, +0x38 FOV-Ziel, +0x58 FOV
    // (halber Winkel in Grad), +0x7C Drehung 3x3, +0xBC Typ (Wort, Namen wie fe.txt: 2..4 TailCam, 12 TVCam …),
    // +0xBE Platz in der Liste der Kamerataste, +0xC0 Flags (Bit 7 = Wiederholung). FUN_00463c30 rechnet je Takt die
    // Kamera nach Typ, FUN_00464f80 baut daraus je Bild die View-Matrix, FUN_00465a60 ist die Kamerataste.
    // Cinematic nutzt den Regisseur der Wiederholung: FUN_00413b80 prüft die Trigger aus *_cameras.scn gegen jedes
    // Auto (car + 0x10E4 = Kamera der letzten Zone) und stellt mit FUN_00413960 die Streckenkamera ein.
    constexpr std::uintptr_t kCamTick     = 0x463C30;
    constexpr std::uintptr_t kCamView     = 0x464F80; // cdecl(Spieler, out, Spiegel)
    constexpr std::uintptr_t kCamCycle    = 0x465A60; // cdecl(Spieler)
    constexpr std::uintptr_t kCamSet      = 0x465790; // cdecl(Spieler, Typ)
    constexpr std::uintptr_t kCamDirector = 0x413B80; // cdecl(char anwenden)
    constexpr std::uintptr_t kCamApplyElem = 0x413960; // cdecl(short Kamera-Element)
    constexpr std::uintptr_t kCamFirstElem = 0x46D890; // erstes passendes Kamera-Element
    constexpr std::uintptr_t kCamState    = 0x627E80;
    constexpr std::uintptr_t kCamElems    = 0x628168; // -> vector der Kamera-Elemente
    constexpr std::uintptr_t kCamTriggers = 0x628174; // -> vector der Trigger
    constexpr std::uintptr_t kCars        = 0x5E8C00; // Autos im Rennen
    constexpr std::uintptr_t kCarCount    = 0x5E8BFC;
    constexpr std::uintptr_t kPlayerIndex = 0x65741C;
    constexpr std::uintptr_t kFollowIndex = 0x606858; // Auto, dem der Regisseur folgt
    constexpr std::uintptr_t kGameMode    = 0x5E99F4; // < 2 Rennen, >= 2 Wiederholung
    constexpr std::uintptr_t kFrameViews  = 0x628140; // -> +4 Haupt-View (View + 0x130 = halbes FOV)
    constexpr std::uintptr_t kCamList     = 0x658294; // car[0].camera: vier Kameratypen der Kamerataste
    constexpr std::size_t kCarPos = 0x330, kCarVel = 0x33C, kCarRight = 0x364, kCarUp = 0x370, kCarFwd = 0x37C;
    constexpr std::size_t kCarCamElem = 0x10E4;
    constexpr std::uintptr_t kCamElemVtable = 0x5B4228; // CCameraElem: +8 Position, +0x78 Typ, +0x7C setzt Kamera

    using CamTickFn  = void (__cdecl *)();
    using CamViewFn  = void (__cdecl *)(int player, float * out, int mirror);
    using CamCycleFn = void (__cdecl *)(int player);
    using CamSetFn   = void (__cdecl *)(int player, int type);
    using DirectorFn = void (__cdecl *)(int apply);
    using ApplyElemFn = void (__cdecl *)(int elem);
    using FirstElemFn = unsigned (__cdecl *)();
    using TransposeFn = void (__cdecl *)(const float * src, float * dst);
    using VecMatFn    = void (__cdecl *)(int count, const float * vec, const float * mat, float * out);
    using SetRotFn    = void (__thiscall *)(float * matrix, const float * rot);
    using SetPosFn    = void (__thiscall *)(float * matrix, const float * pos);

    CamTickFn  g_origCamTick = nullptr;
    CamViewFn  g_origCamView = nullptr;
    CamCycleFn g_origCamCycle = nullptr;

    bool g_cineActive = false;
    int g_cineLastElem = -1;
    bool g_actionOn = true;
    DWORD g_camLogTick = 0;

    struct ActionState
    {
        bool valid = false;
        LARGE_INTEGER last = {};
        float carPos[3] = {};
        float fwd[3] = {};
        float vel[3] = {};
        float sampleAge = 0.0f;   // Sekunden seit der letzten Änderung der Fahrzeugdaten
        float accFwd = 0.0f;      // Längsbeschleunigung m/s² (geglättet)
        float accUp = 0.0f;
        float accUpSlow = 0.0f;
        float bump = 0.0f;        // Unebenheit (Hochpass der Vertikalbeschleunigung)
        float lat = 0.0f;         // Querbeschleunigung m/s², + = Rechtskurve
        float side = 0.0f, look = 0.0f, pull = 0.0f, fov = 0.0f, roll = 0.0f;
        float sideV = 0.0f, lookV = 0.0f, rollV = 0.0f; // Geschwindigkeiten der Kurven-Federn
        float latSlow = 0.0f;     // träge Querbeschleunigung, Differenz = Anstoß beim Einlenken
        float latBuild = 0.0f;    // allmählich aufgebaute Querbeschleunigung (ActionBuildUp)
        float impact = 0.0f;      // Stoß-Wackeln, klingt ab
        float springY = 0.0f, springV = 0.0f; // nachschwingende Höhe der Kamera
        float shakeAmpS = 0.0f;   // geglätteter Ausschlag des Wackelns auf unebenem Grund
        float shakeT = 0.0f;
    } g_act;

    float Dot(const float * a, const float * b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    unsigned char * CamState(int player)
    {
        return reinterpret_cast<unsigned char *>(kCamState + static_cast<std::size_t>(player) * 0x10C);
    }

    short CamType(int player)
    {
        return At<short>(CamState(player), 0xBC);
    }

    bool InRace()
    {
        return !InFrontend() && *reinterpret_cast<int *>(kGameMode) < 2;
    }

    int VectorCount(std::uintptr_t global)
    {
        const auto * v = *reinterpret_cast<int **>(global);
        return v ? (v[1] - v[0]) / 4 : 0;
    }

    unsigned char * PlayerCar()
    {
        const int index = *reinterpret_cast<int *>(kPlayerIndex);
        if (index < 0 || index >= *reinterpret_cast<int *>(kCarCount))
        {
            return nullptr;
        }
        return reinterpret_cast<unsigned char **>(kCars)[index];
    }

    // Regisseur der Wiederholung im Rennen: folgt dem Spielerauto; apply = 0 merkt nur die Zonen.
    void RunDirector(bool apply)
    {
        int & follow = *reinterpret_cast<int *>(kFollowIndex);
        const int saved = follow;
        follow = *reinterpret_cast<int *>(kPlayerIndex);
        reinterpret_cast<DirectorFn>(kCamDirector)(apply ? 1 : 0);
        follow = saved;
    }

    void EnterCinematic()
    {
        unsigned char * car = PlayerCar();
        const int elems = VectorCount(kCamElems);
        if (!car || elems <= 0 || VectorCount(kCamTriggers) <= 0)
        {
            Log("Kamera: Cinematic nicht moeglich (Auto %p, %d Kameras, %d Trigger)", car, elems,
                VectorCount(kCamTriggers));
            return;
        }
        RunDirector(false);
        // Zonen-Kamera, wenn sie eine Streckenkamera setzt und nah ist; sonst die nächste Streckenkamera (v6l blieb
        // bis zur ersten Zone auf der Verfolgerkamera, car + 0x10E4 ist zu Rennbeginn 0)
        const float * carPos = &At<float>(car, kCarPos);
        // [0x628168] -> vector { Anfang, Ende, Kapazität } (wie FUN_0046d980); v6n las den Vektor selbst als Liste
        auto * const * list = *reinterpret_cast<unsigned char * const * const *>(*reinterpret_cast<std::uintptr_t *>(kCamElems));
        auto distance2 = [&](int i) {
            unsigned char * e = list[i];
            if (!e || At<std::uintptr_t>(e, 0) != kCamElemVtable || At<int>(e, 0x7C) == 0)
            {
                return -1.0f; // kein CCameraElem oder setzt keine Kamera
            }
            const float d[3] = { At<float>(e, 8) - carPos[0], At<float>(e, 0xC) - carPos[1], At<float>(e, 0x10) - carPos[2] };
            return Dot(d, d);
        };
        int elem = At<int>(car, kCarCamElem);
        if (elem < 0 || elem >= elems || distance2(elem) < 0.0f || distance2(elem) > 250.0f * 250.0f)
        {
            int best = -1;
            float bestD = 0.0f;
            for (int i = 0; i < elems; ++i)
            {
                const float d = distance2(i);
                if (d >= 0.0f && (best < 0 || d < bestD))
                {
                    best = i;
                    bestD = d;
                }
            }
            elem = best >= 0 ? best : static_cast<int>(reinterpret_cast<FirstElemFn>(kCamFirstElem)());
            At<int>(car, kCarCamElem) = elem;
        }
        g_cineActive = true;
        g_cineLastElem = elem;
        reinterpret_cast<ApplyElemFn>(kCamApplyElem)(elem);
        Log("Kamera: Cinematic an, Streckenkamera %d von %d (Typ %d), %d Trigger", elem, elems, CamType(0),
            VectorCount(kCamTriggers));
    }

    void LeaveCinematic(bool restore)
    {
        g_cineActive = false;
        if (restore)
        {
            // zurück auf die Ansicht des aktuellen Platzes der Kamerataste (Liste je Kamera ab 0x658294, Abstand 0x580)
            const int slot = At<short>(CamState(0), 0xBE) & 3;
            reinterpret_cast<CamSetFn>(kCamSet)(0, *reinterpret_cast<short *>(CarAddr(kCamList) + slot * 4));
        }
        Log("Kamera: Cinematic aus, Typ %d", CamType(0));
    }

    void __cdecl HookCamCycle(int player)
    {
        if (player == 0 && InRace())
        {
            if (g_cineActive)
            {
                LeaveCinematic(false); // das Spiel schaltet gleich auf den nächsten Platz (3 -> 0)
            }
            else if (g_cineInCycle && (At<short>(CamState(0), 0xBE) & 3) == 3 &&
                     (At<unsigned char>(CamState(0), 0xC0) & 0x80) == 0)
            {
                EnterCinematic();
                if (g_cineActive)
                {
                    return; // Platz bleibt 3, der nächste Druck geht auf Platz 0
                }
            }
        }
        g_origCamCycle(player);
        g_act.valid = false;
    }

    void __cdecl HookCamTick()
    {
        g_origCamTick();
        if (!InRace())
        {
            g_cineActive = false;
            return;
        }
        if (g_cineKey && KeyPressed(g_cineKey, g_cineKeyDown))
        {
            if (g_cineActive)
            {
                LeaveCinematic(true);
            }
            else
            {
                EnterCinematic();
            }
        }
        if (g_actionKey && KeyPressed(g_actionKey, g_actionKeyDown))
        {
            g_actionOn = !g_actionOn;
            g_act.valid = false;
            Log("Kamera: Action %s", g_actionOn ? "an" : "aus");
        }
        if (g_cineActive)
        {
            RunDirector(true); // wechselt die Streckenkamera, sobald das Auto in eine neue Zone fährt
            unsigned char * car = PlayerCar();
            if (car && At<int>(car, kCarCamElem) != g_cineLastElem)
            {
                g_cineLastElem = At<int>(car, kCarCamElem);
                Log("Kamera: Cinematic wechselt auf Streckenkamera %d (Typ %d)", g_cineLastElem, CamType(0));
            }
        }
        else if (g_cineInCycle || g_cineKey)
        {
            RunDirector(false); // Zonen mitführen, damit Cinematic sofort die passende Kamera hat
        }
    }

    // Glättung unabhängig von der Bildrate: nähert value in tau Sekunden an target an.
    void Smooth(float & value, float target, float tau, float dt)
    {
        value += (target - value) * (1.0f - std::exp(-dt / std::fmax(tau, 1e-3f)));
    }

    // Gedämpfte Feder (w in rad/s, zeta < 1 schwingt über), halbimplizit.
    void Spring(float & value, float & speed, float target, float w, float zeta, float dt)
    {
        speed += (w * w * (target - value) - 2.0f * zeta * w * speed) * dt;
        value += speed * dt;
    }

    // Dreht v um die senkrechte Weltachse; sin/cos des Winkels.
    void Yaw(float * v, float s, float c)
    {
        const float x = v[0];
        const float z = v[2];
        v[0] = x * c + z * s;
        v[2] = -x * s + z * c;
    }

    void __cdecl HookCamView(int player, float * out, int mirror)
    {
        g_origCamView(player, out, mirror);
        if (player != 0 || mirror == 1)
        {
            return; // Rückspiegel und zweiter Spieler bleiben wie im Spiel
        }
        unsigned char * car = *reinterpret_cast<unsigned char **>(kCamState);
        unsigned char * view = nullptr;
        if (const auto frame = *reinterpret_cast<unsigned char **>(kFrameViews))
        {
            view = At<unsigned char *>(frame, 4);
        }
        // Cockpit-FOV (Innenansichten BumperCam/BumperCam2): view + 0x130 = halber Winkel, das Spiel nimmt 37,5
        // fester Wert, kein Zuwachs mit dem Tempo (👤: der gehört nur zur Action-Kamera)
        if (g_cockpitFov > 0.0f && view && car && !g_cineActive && InRace() && CamType(0) <= 1)
        {
            At<float>(view, 0x130) = g_cockpitFov * 0.5f;
        }
        if (!g_action || !g_actionOn || g_cineActive || !InRace() || !car ||
            (g_actionCams & (1u << (CamType(0) & 31))) == 0)
        {
            g_act.valid = false;
            return;
        }
        ActionState & s = g_act;
        LARGE_INTEGER now = {};
        LARGE_INTEGER freq = {};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        const float * pos = &At<float>(car, kCarPos);
        const float * vel = &At<float>(car, kCarVel);
        const float * right = &At<float>(car, kCarRight);
        const float * fwd = &At<float>(car, kCarFwd);
        if (!s.valid)
        {
            s = ActionState();
            s.valid = true;
            s.last = now;
            std::memcpy(s.carPos, pos, sizeof(s.carPos));
            std::memcpy(s.fwd, fwd, sizeof(s.fwd));
            std::memcpy(s.vel, vel, sizeof(s.vel));
            s.springY = pos[1];
            s.springV = vel[1];
            return;
        }
        const float dt = std::fmin(0.1f, static_cast<float>(now.QuadPart - s.last.QuadPart) / static_cast<float>(freq.QuadPart));
        s.last = now;
        if (dt <= 0.0f)
        {
            return;
        }

        // Fahrzeugdaten ändern sich nur mit dem Simulationstakt; Ableitungen nur bei neuen Werten bilden.
        s.sampleAge += dt;
        if (std::memcmp(s.carPos, pos, sizeof(s.carPos)) != 0)
        {
            const float h = std::fmax(s.sampleAge, 1e-3f);
            float dv[3] = { (vel[0] - s.vel[0]) / h, (vel[1] - s.vel[1]) / h, (vel[2] - s.vel[2]) / h };
            float df[3] = { fwd[0] - s.fwd[0], fwd[1] - s.fwd[1], fwd[2] - s.fwd[2] };
            const float yawRate = Dot(df, right) / h; // + = Rechtskurve
            const float vFwd = Dot(vel, fwd);
            Smooth(s.accFwd, Dot(dv, fwd), 0.12f, h);
            Smooth(s.accUp, dv[1], 0.04f, h);
            Smooth(s.accUpSlow, dv[1], 0.4f, h);
            Smooth(s.lat, vFwd * yawRate, 0.15f, h);
            Smooth(s.bump, std::fabs(s.accUp - s.accUpSlow), 0.25f, h);
            // Stoß (Aufprall, Landung): Geschwindigkeitssprung zwischen zwei Simulationswerten. v6o teilte durch die
            // Zeit seit dem letzten neuen Wert; das machte Rauschen im Stand und beim Bremsen zum Stoß (👤 "immer beim
            // Bremsen, auch langsam"). Vollbremsung ≈ 0,2 m/s je Schritt, Aufprall/Landung mehrere m/s.
            const float jump2 = (pos[0] - s.carPos[0]) * (pos[0] - s.carPos[0]) + (pos[2] - s.carPos[2]) * (pos[2] - s.carPos[2]);
            if (jump2 > 20.0f * 20.0f)
            {
                s.valid = false; // Auto versetzt (Neustart nach Unfall): nicht als Stoß werten, Federn neu
                return;
            }
            const float dvel[3] = { vel[0] - s.vel[0], vel[1] - s.vel[1], vel[2] - s.vel[2] };
            const float hitDv = std::sqrt(Dot(dvel, dvel));
            if (hitDv > 3.0f)
            {
                if (s.impact < 0.05f)
                {
                    Log("Kamera: Stoss %.1f m/s bei %.1f m/s", hitDv, std::sqrt(Dot(vel, vel)));
                }
                s.impact = std::fmax(s.impact, std::fmin((hitDv - 3.0f) / 9.0f, 1.0f));
            }
            std::memcpy(s.carPos, pos, sizeof(s.carPos));
            std::memcpy(s.fwd, fwd, sizeof(s.fwd));
            std::memcpy(s.vel, vel, sizeof(s.vel));
            s.sampleAge = 0.0f;
        }
        const float speed = std::sqrt(Dot(vel, vel));

        // 1 Kurve: Kamera nach außen, Auto bleibt zur Kurveninnenseite im Bild, Blick leicht in die Kurve, leichtes
        // Rollen zur Innenseite. Unterdämpfte Federn: beim Einlenken nimmt die Kamera den Schwung mit (Anstoß aus der
        // Änderung der Querbeschleunigung), schwingt etwas über und folgt dann ruhig der Kurve.
        const float latN = std::fmax(-1.0f, std::fmin(s.lat / 9.81f, 1.0f)); // 1 g = voll
        Smooth(s.latSlow, latN, 0.5f, dt);
        // v6p fühlte sich gummiartig an (👤): Anstoß 0,6 auch beim Aufrichten (zielte über die Mitte hinaus) und
        // Dämpfung 0,5. Seit v6q: Anstoß 0,3 nur beim Einlenken (Querbeschleunigung nimmt zu), Dämpfung 0,7;
        // zurück zur Mitte langsam und ohne Überschwingen (2,2 rad/s, kritisch gedämpft).
        // v6r: Mit der Tastatur springt die Querbeschleunigung fast sofort auf ihren Wert (👤: Einlenken zu heftig).
        // Der Effekt baut sich deshalb über ActionBuildUp auf, wird also mit längerem Lenken stärker; Abbau 0,6 s.
        const bool turningIn = latN * (latN - s.latSlow) > 0.0f;
        const bool building = std::fabs(latN) > std::fabs(s.latBuild) && latN * s.latBuild >= 0.0f;
        Smooth(s.latBuild, latN, building ? g_actBuildUp : 0.6f, dt);
        const float kick = turningIn ? 0.3f * g_actOvershoot : 0.0f;
        const float latT = std::fmax(-1.3f, std::fmin(s.latBuild + (latN - s.latSlow) * kick, 1.3f));
        const float zetaIn = std::fmax(0.35f, std::fmin(1.0f - 0.3f * g_actOvershoot, 1.0f));
        auto turnSpring = [&](float & value, float & speed, float target) {
            const bool back = std::fabs(target) < std::fabs(value);
            Spring(value, speed, target, back ? 2.2f : 4.0f, back ? 1.0f : zetaIn, dt);
        };
        // v6s: mit dem Tempo etwas stärker (ab 10 m/s, voll bei 60 m/s)
        const float speedN = std::fmax(0.0f, std::fmin((speed - 10.0f) / 50.0f, 1.0f));
        const float boost = 1.0f + g_actSpeedBoost * speedN;
        turnSpring(s.side, s.sideV, -latT * g_actSide * boost);
        turnSpring(s.look, s.lookV, latT * g_actLook * boost);
        turnSpring(s.roll, s.rollV, latT * g_actRoll * boost);
        // 2 Bremsen/Aufprall: heranrücken (schnell hin, langsam zurück)
        const float brakeN = std::fmax(0.0f, std::fmin((-s.accFwd - 2.0f) / 10.0f, 1.0f));
        const float pullTarget = brakeN * g_actBrake;
        Smooth(s.pull, pullTarget, pullTarget > s.pull ? 0.15f : 0.6f, dt);
        // 3 Sprünge/Kuppen: Kamerahöhe als gedämpfte Feder hinter dem Auto her. Die Feder folgt auch der
        // Steiggeschwindigkeit des Autos, gleichmäßige Steigungen ergeben also keinen Versatz (v6l hing bergauf
        // bis 0,8 m tiefer); sie schwingt nur bei Änderungen (Kuppe, Absprung, Landung). Nach unten nur wenig,
        // damit der Blick nach vorn frei bleibt.
        // v6y: Die Fahrzeughöhe ändert sich nur im Simulationstakt. Gegen die gestufte Höhe gerechnet sprang der
        // Versatz je Schritt um Steiggeschwindigkeit × Schrittzeit (👤: Zittern ab ≈ 200 km/h, bei hohen FPS wie
        // Unschärfe). Deshalb zwischen den Schritten mit der Steiggeschwindigkeit fortschreiben.
        const float yNow = pos[1] + vel[1] * std::fmin(s.sampleAge, 0.05f);
        const float w = 7.0f;
        const float zeta = std::fmax(0.25f, std::fmin(0.85f - 0.3f * g_actOvershoot, 1.0f));
        s.springV += (w * w * (yNow - s.springY) + 2.0f * zeta * w * (vel[1] - s.springV)) * dt;
        s.springY += s.springV * dt;
        const float bounce = std::fmax(-0.25f, std::fmin((s.springY - yNow) * g_actBounce, 0.8f)) + g_actHeight;
        // 4 FOV: weiter mit der Geschwindigkeit, ab 8 m/s, voll bei 60 m/s (v6l–v6r linear bis 70 m/s, 👤 zu wenig)
        Smooth(s.fov, std::fmax(0.0f, std::fmin((speed - 8.0f) / 52.0f, 1.0f)) * g_actFov, 0.8f, dt);
        // 5 Wackeln auf unebenem Grund (Offroad, Randsteine)
        s.shakeT += dt;
        // v6y: erst über einer Schwelle (Rauschen der Fahrdaten wächst mit dem Tempo), Ausschlag geglättet, 4–7 Hz
        // statt 7–13 Hz (👤: Zittern offroad, bei hohen FPS wie Unschärfe)
        Smooth(s.shakeAmpS, std::fmax(0.0f, std::fmin((s.bump - 4.0f) / 36.0f, 1.0f)) * 0.04f * g_actShake, 0.3f, dt);
        const float shakeAmp = s.shakeAmpS;
        // 6 Stoß bei Aufprall und Landung: kräftiger, langsamer und schnell abklingend
        s.impact *= std::exp(-dt / 0.35f);
        const float hit = s.impact * g_actImpact;
        const float shakeX = shakeAmp * (std::sin(s.shakeT * 41.0f) * 0.6f + std::sin(s.shakeT * 26.0f + 1.3f) * 0.4f) +
                             hit * 0.12f * (std::sin(s.shakeT * 47.0f + 0.4f) * 0.7f + std::sin(s.shakeT * 29.0f) * 0.3f);
        const float shakeY = shakeAmp * (std::sin(s.shakeT * 45.0f + 0.7f) * 0.6f + std::sin(s.shakeT * 29.0f) * 0.4f) +
                             hit * 0.12f * (std::sin(s.shakeT * 53.0f + 2.1f) * 0.7f + std::sin(s.shakeT * 31.0f + 1.0f) * 0.3f);
        const float rollDeg = s.roll + hit * 0.8f * std::sin(s.shakeT * 41.0f + 0.9f);

        float toCar[3] = { pos[0] - out[0], pos[1] - out[1], pos[2] - out[2] };
        const float dist = std::sqrt(Dot(toCar, toCar));
        if (dist > 0.5f)
        {
            const float pull = std::fmin(s.pull, dist - 1.5f > 0.0f ? dist - 1.5f : 0.0f) / dist;
            for (int i = 0; i < 3; ++i)
            {
                out[i] += toCar[i] * pull;
            }
        }
        out[0] += right[0] * (s.side + shakeX);
        out[1] += right[1] * (s.side + shakeX) + bounce + shakeY;
        out[2] += right[2] * (s.side + shakeX);

        // Blick in die Kurve: alle Zeilen der Drehung um die Hochachse drehen; Richtung so, dass "vorn" zur
        // Kurveninnenseite wandert (unabhängig von der Händigkeit der Engine).
        if (std::fabs(s.look) > 0.01f)
        {
            float probe[3] = { fwd[0], fwd[1], fwd[2] };
            const float a = s.look * 0.0174533f;
            Yaw(probe, std::sin(a), std::cos(a));
            const float sign = (Dot(probe, right) - Dot(fwd, right)) * s.look > 0.0f ? 1.0f : -1.0f;
            const float sa = std::sin(a * sign);
            const float ca = std::cos(a * sign);
            for (int row = 0; row < 3; ++row)
            {
                Yaw(out + 3 + row * 3, sa, ca);
            }
        }

        // Rollen: Zeile 0 (rechts) und Zeile 1 (oben) um die Blickachse drehen; Richtung so, dass "oben" zur
        // Kurveninnenseite kippt (Zeile 0 kann durch die Spiegel-Einstellung des Spiels umgedreht sein).
        if (std::fabs(rollDeg) > 0.01f)
        {
            float * r0 = out + 3;
            float * r1 = out + 6;
            // "oben" wandert um sin(a) · (Zeile 0 · rechts) nach rechts; gewünscht: nach rechts, wenn rollDeg > 0
            const float a = std::fabs(rollDeg) * 0.0174533f * (Dot(r0, right) * rollDeg >= 0.0f ? 1.0f : -1.0f);
            const float sa = std::sin(a);
            const float ca = std::cos(a);
            for (int i = 0; i < 3; ++i)
            {
                const float x = r0[i];
                const float y = r1[i];
                r0[i] = x * ca - y * sa;
                r1[i] = y * ca + x * sa;
            }
        }

        // Rest der View wie am Ende von FUN_00464f80 neu aufbauen
        reinterpret_cast<TransposeFn>(0x532A00)(out + 3, out + 0xF);
        std::memcpy(out + 0x18, out + 0xF, 9 * sizeof(float));
        reinterpret_cast<SetRotFn>(0x466280)(out + 0x21, out + 3);
        reinterpret_cast<SetPosFn>(0x466220)(out + 0x21, out);
        const float negPos[3] = { -out[0], -out[1], -out[2] };
        reinterpret_cast<VecMatFn>(0x532A40)(1, negPos, out + 0xF, out + 0xC);
        if (view)
        {
            At<float>(view, 0x130) *= 1.0f + s.fov;
        }

        const DWORD tick = GetTickCount();
        if (g_measure && tick - g_camLogTick >= 1000)
        {
            g_camLogTick = tick;
            Log("Kamera: Typ %d v %.1f m/s, quer %.1f, laengs %.1f, Unebenheit %.1f -> seitlich %.2f, Blick %.1f, "
                "Rollen %.1f, heran %.2f, Hoehe %.2f, FOV +%.0f%%, Wackeln %.3f, Stoss %.2f",
                CamType(0), speed, s.lat, s.accFwd, s.bump, s.side, s.look, s.roll, s.pull, bounce, s.fov * 100.0f,
                shakeAmp, s.impact);
        }
    }

    // Menü-Listen scrollen (10.1, doc/MENU.md): seit v7s in nfs5_enhanced (LHedit, [Menu] in nfs5_enhanced.ini).

    // ---- Verkehr (Phase 12): Messung ----
    // Rennaufbau im Menü FUN_004ECF00 schreibt die Befehlsliste für das Rennen; Verkehrsautos legt er mit
    // FUN_004EEDA0 (Typ 4, Name "Traffic") an, Modell aus dem Strecken-Datensatz +0x7C (7 IDs, i % 7).
    constexpr std::uintptr_t kRaceSetup   = 0x4ECF00;
    constexpr std::uintptr_t kRaceCar     = 0x4EEDA0;
    constexpr std::uintptr_t kModelById   = 0x4F4400;
    constexpr std::uintptr_t kSimAiConfig = 0x4068F0;
    constexpr std::uintptr_t kFeMode      = 0x690AAC; // Spielart im Menü (0 Einzelrennen, 4 Turnier ...)
    constexpr std::uintptr_t kCarModels   = 0x6919FC; // Zeiger auf Vektor der Fahrzeugmodelle (erstes short = ID)

    using RaceSetupFn   = void(__cdecl *)(int *);
    using RaceCarFn     = void(__fastcall *)(void *, void *, int, int *, int, const char *, int, char, char, char);
    using ModelByIdFn   = void *(__cdecl *)(short);
    using SimAiConfigFn = void(__cdecl *)();
    RaceSetupFn g_origRaceSetup = nullptr;
    RaceCarFn g_origRaceCar = nullptr;
    ModelByIdFn g_origModelById = nullptr;
    SimAiConfigFn g_origSimAiConfig = nullptr;
    std::uint32_t g_raceSetupEcx = 0;

    // Erste lesbare Zeichenkette in den ersten 0x100 Byte eines Datensatzes (nur Messung, Layout unbekannt)
    std::string FirstText(const void * rec)
    {
        const auto * b = static_cast<const unsigned char *>(rec);
        for (int i = 0; b && i < 0x100; ++i)
        {
            int n = 0;
            while (i + n < 0x100 && b[i + n] >= 0x20 && b[i + n] < 0x7F)
            {
                ++n;
            }
            if (n >= 3 && i + n < 0x100 && b[i + n] == 0)
            {
                char buf[64] = {};
                std::snprintf(buf, sizeof(buf), "+0x%X '%.*s'", i, n > 40 ? 40 : n, reinterpret_cast<const char *>(b + i));
                return buf;
            }
            i += n;
        }
        return "-";
    }

    void LogModelList()
    {
        static bool done = false;
        auto * vec = *reinterpret_cast<void ***>(kCarModels);
        if (done || !vec)
        {
            return;
        }
        done = true;
        auto ** begin = static_cast<void **>(vec[0]);
        auto ** end = static_cast<void **>(vec[1]);
        Log("Verkehr: %d Fahrzeugmodelle", static_cast<int>(end - begin));
        for (auto ** it = begin; it < end; ++it)
        {
            Log("Verkehr:  Modell ID %d @%08X %s", *static_cast<short *>(*it), reinterpret_cast<unsigned>(*it),
                FirstText(*it).c_str());
        }
    }

    // Name eines Verkehrsmodells: Zeichenkette ab Datensatz + 2 (wie FEData/Data/nfs5.car, z. B. "EAS10", "Eden 1 Traffic")
    const char * ModelName(void * rec)
    {
        return static_cast<const char *>(rec) + 2;
    }

    bool NameInList(const std::vector<std::string> & list, void * rec)
    {
        for (const auto & n : list)
        {
            if (_stricmp(n.c_str(), ModelName(rec)) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // Darf das Modell auf dieser Strecke fahren? track = Strecken-Datensatz (7 Verkehrs-IDs bei +0x7C, nfs5.trk)
    bool InTrafficPool(void * rec, const char * track)
    {
        if (!g_trafficPoolNames.empty() && !NameInList(g_trafficPoolNames, rec))
        {
            return false;
        }
        if (!NameInList(g_trafficTrackOnly, rec))
        {
            return true;
        }
        const short id = *static_cast<short *>(rec);
        const auto * ids = reinterpret_cast<const short *>(track + 0x7C);
        for (int i = 0; track && i < 7; ++i)
        {
            if (ids[i] == id)
            {
                return true;
            }
        }
        return false;
    }

    // Zufälliges Verkehrsmodell aus dem Pool; Beutel-Verfahren: jedes Modell einmal, dann neu mischen.
    // Der Beutel gilt je Strecke: neue Strecke = neu füllen (Schneepflug-Regel hängt an der Strecke).
    void * PickTrafficModel(const char * track)
    {
        static std::vector<void *> bag;
        static const char * bagTrack = nullptr;
        static std::uint32_t seed = GetTickCount() | 1;
        auto * vec = *reinterpret_cast<void ***>(kCarModels);
        if (!vec)
        {
            return nullptr;
        }
        if (track != bagTrack)
        {
            bag.clear();
            bagTrack = track;
        }
        if (bag.empty())
        {
            for (auto ** it = static_cast<void **>(vec[0]); it < static_cast<void **>(vec[1]); ++it)
            {
                if (*it && InTrafficPool(*it, track))
                {
                    bag.push_back(*it);
                }
            }
            if (bag.empty())
            {
                Log("Verkehr: Models passt auf kein Modell, nehme die Strecke");
                return nullptr;
            }
        }
        seed = seed * 1664525u + 1013904223u;
        const std::size_t i = (seed >> 8) % bag.size();
        void * rec = bag[i];
        bag.erase(bag.begin() + static_cast<std::ptrdiff_t>(i));
        return rec;
    }

    // Rücksprung des Modell-Lookups in der Verkehrsschleife von FUN_004ECF00 (Aufruf bei 0x4ED4DB)
    constexpr std::uintptr_t kTrafficLookupReturn = 0x4ED4E0;

    // ESI an der Aufrufstelle 0x4ED4DB = Strecken-Datensatz (das Spiel liest dort mov dx,[esi+edx*2+0x7C])
    std::uintptr_t g_lookupEsi = 0;

    void * __cdecl HookModelById(short id)
    {
        void * rec = g_origModelById(id);
        const bool traffic = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == kTrafficLookupReturn;
        void * pick = (traffic && g_trafficRandom) ? PickTrafficModel(reinterpret_cast<const char *>(g_lookupEsi)) : nullptr;
        if (g_trafficMeasure)
        {
            const bool found = rec && *static_cast<short *>(rec) == id;
            Log("Verkehr: Modell-Lookup ID %d -> @%08X%s %s%s%s", id, reinterpret_cast<unsigned>(rec),
                found ? "" : " (nicht gefunden, erstes Modell)", rec ? FirstText(rec).c_str() : "",
                pick ? ", zufaellig ersetzt durch " : "", pick ? ModelName(pick) : "");
        }
        return pick ? pick : rec;
    }

    // ---- Verkehr (12.2): Anzahl je Menüstufe ----
    // FUN_004FC0B0 (cdecl, ECX wird durchgereicht) liefert in *traffic die Anzahl; im Einzelrennen 0/2/5/7 je Stufe.
    // Nur der Aufruf im Rennaufbau (0x4ED2B5) wird umgerechnet, danach kürzt das Spiel auf 9 Autos gesamt.
    constexpr std::uintptr_t kTrackRecord = 0x4FC0B0;
    constexpr std::uintptr_t kTrafficCountReturn = 0x4ED2BA;
    using TrackRecordFn = char *(__cdecl *)(int, int *, char *, char *, int *);
    void * g_origTrackRecord = nullptr;
    std::uint32_t g_trackRecordEcx = 0;

    __declspec(naked) void TrackRecordOrig()
    {
        __asm {
            mov ecx, g_trackRecordEcx
            jmp g_origTrackRecord
        }
    }

    char * __cdecl HookTrackRecord(int mode, int * laps, char * rev, char * mirror, int * traffic)
    {
        const bool setup = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == kTrafficCountReturn;
        char * rec = reinterpret_cast<TrackRecordFn>(&TrackRecordOrig)(mode, laps, rev, mirror, traffic);
        // Spielart 0/1 Einzelrennen, 2 Knockout: Menüwert. Turnier/Karriere (Tabellen 0/2/4/8) bleiben.
        if (setup && traffic && mode >= 0 && mode <= 2)
        {
            static const int kGame[4] = { 0, 2, 5, 7 };
            for (int stage = 1; stage < 4; ++stage)
            {
                if (*traffic == kGame[stage])
                {
                    Log("Verkehr: Stufe %d, Spiel %d -> %d Autos", stage, *traffic, g_trafficCount[stage]);
                    *traffic = g_trafficCount[stage];
                    break;
                }
            }
        }
        return rec;
    }

    __declspec(naked) void HookTrackRecordStub()
    {
        __asm {
            mov g_trackRecordEcx, ecx
            jmp HookTrackRecord
        }
    }

    // Vorstufe: ESI des Aufrufers sichern, dann normal in HookModelById (cdecl, gleiche Argumente)
    __declspec(naked) void HookModelByIdStub()
    {
        __asm {
            mov g_lookupEsi, esi
            jmp HookModelById
        }
    }

    void __fastcall HookRaceCar(void * obj, void *, int slot, int * cursor, int type, const char * name, int mode,
                                char p6, char p7, char p8)
    {
        if (obj)
        {
            Log("Verkehr: Auto Slot %d Typ %d Name '%s' Datei '%s' Spec '%s'", slot, type, name ? name : "",
                static_cast<char *>(obj) + 0x38, static_cast<char *>(obj) + 0x83);
        }
        g_origRaceCar(obj, nullptr, slot, cursor, type, name, mode, p6, p7, p8);
    }

    void __cdecl RaceSetupBefore(int *)
    {
        Log("Verkehr: Rennaufbau beginnt, Spielart %d", *reinterpret_cast<int *>(kFeMode));
        LogModelList();
    }

    void __cdecl RaceSetupAfter(int * cursor)
    {
        // Letzter Befehl des Aufbaus ist (0x26, Gesamtzahl der Autos)
        const int * end = reinterpret_cast<int *>(*cursor);
        Log("Verkehr: Rennaufbau fertig, letzter Befehl 0x%X = %d", end[-2], end[-1]);
    }

    // FUN_004ECF00 endet mit ret (cdecl, ein Argument), Ghidra zeigt es als thiscall. v7k/v7l hängten sich als
    // thiscall ein (ret 4) und zerstörten den Stapel. ECX wird unverändert ans Original weitergegeben.
    __declspec(naked) void HookRaceSetup()
    {
        __asm {
            mov  g_raceSetupEcx, ecx
            push dword ptr [esp + 4]
            call RaceSetupBefore
            add  esp, 4
            mov  ecx, g_raceSetupEcx
            push dword ptr [esp + 4]
            call g_origRaceSetup
            add  esp, 4
            push dword ptr [esp + 4]
            call RaceSetupAfter
            add  esp, 4
            ret
        }
    }

    void __cdecl HookSimAiConfig()
    {
        g_origSimAiConfig();
        const int count = *reinterpret_cast<int *>(0x5E8B58);
        Log("Verkehr: Simulation %d Autos (NUMCARS %d, Objekte %d), TRAFFIC_DENSITY %d", count,
            *reinterpret_cast<int *>(0x658080), *reinterpret_cast<int *>(0x65807C), *reinterpret_cast<int *>(0x657420));
        for (int i = 0; i < count && i < g_maxCars; ++i)
        {
            auto * car = reinterpret_cast<char *>(CarAt(static_cast<std::uint32_t>(i)));
            if (car)
            {
                const unsigned flags = *reinterpret_cast<unsigned *>(car + 0x52C);
                Log("Verkehr:  Auto %d @%08X Flags 0x%X%s", i, reinterpret_cast<unsigned>(car), flags,
                    (flags & 0x10) ? " (Verkehr)" : (flags & 0x20) ? " (Polizei)" : "");
            }
        }
    }

    // ---- Mehr als 9 Autos (12.4, VERKEHR.md §6.6) ----
    // Die Beschreibungen (0x65808C, 9 x 0x580) sind das Ende des Rennzustands 0x6573E8 (0x3E24 Byte). Der Block wird
    // gelöscht (2 x PUSH 0x3E24) und für Wiederholung/Neustart nach 0x5E9A20 kopiert (3 x sichern, 1 x zurück). Der
    // Rennzustand bleibt am Platz; an diesen Stellen gleicht LFSP die verlegten Beschreibungen ab: 0..8 mit dem alten
    // Bereich (damit die Kopie und damit Wiederholungsdateien die ersten 9 enthalten), 9.. mit einem Schattenpuffer.
    constexpr std::uint32_t kDescRegion = 7;           // Index von DESC in carlimit_table.h
    constexpr std::uintptr_t kDescOld = 0x65808C;
    constexpr std::uint32_t kDescSize = 0x580;
    std::uint8_t * g_descShadow = nullptr;

    std::uint32_t RegionNewSize(std::uint32_t r)
    {
        return kCarLimitRegionElem[r] * kCarLimitRegionFactor[r] * static_cast<std::uint32_t>(g_maxCars);
    }

    void __cdecl DescSave()
    {
        std::memcpy(reinterpret_cast<void *>(kDescOld), g_carLimitNew[kDescRegion], kGameCars * kDescSize);
        std::memcpy(g_descShadow, g_carLimitNew[kDescRegion] + kGameCars * kDescSize, (g_maxCars - kGameCars) * kDescSize);
    }

    void __cdecl DescRestore()
    {
        std::memcpy(g_carLimitNew[kDescRegion], reinterpret_cast<void *>(kDescOld), kGameCars * kDescSize);
        std::memcpy(g_carLimitNew[kDescRegion] + kGameCars * kDescSize, g_descShadow, (g_maxCars - kGameCars) * kDescSize);
    }

    void __cdecl DescClear()
    {
        std::memset(g_carLimitNew[kDescRegion], 0, static_cast<std::size_t>(g_maxCars) * kDescSize);
    }

    // Ersetzt MOV ECX,0xF89 vor REP MOVSD Rennzustand -> 0x5E9A20
    __declspec(naked) void DescSaveStub()
    {
        __asm {
            pushad
            pushfd
            call DescSave
            popfd
            popad
            mov ecx, 0xF89
            ret
        }
    }

    // Ersetzt MOV dword ptr [0x5E99FC],0 / REP MOVSD (0x5E9A20 -> Rennzustand, 0x413DC5) und gleicht danach ab
    __declspec(naked) void DescRestoreStub()
    {
        __asm {
            mov dword ptr ds:[0x5E99FC], 0
            rep movsd
            pushad
            pushfd
            call DescRestore
            popfd
            popad
            ret
        }
    }

    // Ersetzt PUSH 0x3E24 (Länge beim Löschen des Rennzustands): legt 0x3E24 unter die Rücksprungadresse
    __declspec(naked) void DescClearStub()
    {
        __asm {
            push dword ptr [esp]
            mov dword ptr [esp + 4], 0x3E24
            pushad
            pushfd
            call DescClear
            popfd
            popad
            ret
        }
    }

    // dx11z und nfs5_enhanced (Interpolation) lesen die Liste aller Autos selbst an 0x5E8B34, höchstens 9 Einträge.
    // Nach jedem Eintrag (FUN_0040F160, cdecl) spiegelt LFSP die ersten 9 der verlegten Liste dorthin zurück.
    using CarAddFn = void(__cdecl *)(int);
    CarAddFn g_origCarAdd = nullptr;

    void __cdecl HookCarAdd(int car)
    {
        g_origCarAdd(car);
        std::memcpy(reinterpret_cast<void *>(0x5E8B34), g_carLimitNew[0], kGameCars * 4);
    }

    // Gespeicherte Wiederholungen: die Datei enthält nur die Rennzustands-Kopie mit 9 Beschreibungen. LFSP legt
    // die übrigen in lfsp_replays\<Prüfsumme der Kopie>.bin ab und lädt sie wieder (FUN_0059E170 schreibt,
    // FUN_004146F0 lädt nach 0x5E9A20). Fehlt die Datei bei mehr als 9 Autos, wird NUMCARS der Kopie auf 9 gekürzt.
    constexpr std::uintptr_t kReplayBuf = 0x5E9A20;
    constexpr std::uint32_t kRaceStateSize = 0x3E24;
    constexpr std::uint32_t kReplayNumCars = 0x658080 - 0x6573E8; // NUMCARS in der Rennzustands-Kopie
    using FileWriteFn = void(__cdecl *)(const char *, const void *, std::uint32_t);
    using ReplayLoadFn = void(__cdecl *)();
    FileWriteFn g_origFileWrite = nullptr;
    ReplayLoadFn g_origReplayLoad = nullptr;

    std::string ReplaySidecar()
    {
        std::uint32_t h = 2166136261u; // FNV-1a über die Rennzustands-Kopie
        const auto * b = reinterpret_cast<const unsigned char *>(kReplayBuf);
        for (std::uint32_t i = 0; i < kRaceStateSize; ++i)
        {
            h = (h ^ b[i]) * 16777619u;
        }
        char name[32] = {};
        std::snprintf(name, sizeof(name), "%08X.bin", h);
        const std::string dir = g_soundDir + "lfsp_replays\\";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + name;
    }

    void __cdecl HookFileWrite(const char * name, const void * data, std::uint32_t size)
    {
        g_origFileWrite(name, data, size);
        if (reinterpret_cast<std::uintptr_t>(data) != kReplayBuf || !g_descShadow)
        {
            return;
        }
        const std::string path = ReplaySidecar();
        if (FILE * f = std::fopen(path.c_str(), "wb"))
        {
            const std::uint32_t head[2] = { 0x5053464Cu /* LFSP */, static_cast<std::uint32_t>(g_maxCars) };
            std::fwrite(head, sizeof(head), 1, f);
            std::fwrite(g_descShadow, kDescSize, g_maxCars - kGameCars, f);
            std::fclose(f);
            Log("Autos: Wiederholung '%s' gespeichert, Beschreibungen ab Auto 10 in %s", name ? name : "", path.c_str());
        }
    }

    void __cdecl HookReplayLoad()
    {
        g_origReplayLoad();
        auto & numCars = *reinterpret_cast<int *>(kReplayBuf + kReplayNumCars);
        if (numCars <= kGameCars)
        {
            return;
        }
        const std::string path = ReplaySidecar();
        std::uint32_t head[2] = {};
        FILE * f = std::fopen(path.c_str(), "rb");
        const bool ok = f && std::fread(head, sizeof(head), 1, f) == 1 && head[0] == 0x5053464Cu &&
                        static_cast<int>(head[1]) >= numCars && static_cast<int>(head[1]) <= g_maxCars;
        if (ok)
        {
            std::memset(g_descShadow, 0, (g_maxCars - kGameCars) * kDescSize);
            std::fread(g_descShadow, kDescSize, head[1] - kGameCars, f);
            Log("Autos: Wiederholung mit %d Autos geladen, Beschreibungen aus %s", numCars, path.c_str());
        }
        else
        {
            Log("Autos: Wiederholung mit %d Autos ohne passende %s, gekürzt auf 9", numCars, path.c_str());
            numCars = kGameCars;
        }
        if (f)
        {
            std::fclose(f);
        }
    }

    struct CarLimitCode
    {
        std::uintptr_t at;
        unsigned char expected[12];
        std::uint32_t size;
        void * stub;     // nullptr: nur Direktwert ersetzen (value)
        int value;       // neuer Wert für imm8/imm32 bei stub == nullptr
        std::uint32_t immOffset;
        std::uint32_t immSize;
    };

    void InstallCarLimit()
    {
        if (g_maxCars <= kGameCars)
        {
            return;
        }
        const CarLimitCode code[] = {
            // Menü: Summe aller Autos höchstens 9 (FUN_004ECF00)
            { 0x4ED389, { 0x83, 0xF9, 0x09 }, 3, nullptr, g_maxCars, 2, 1 },
            { 0x4ED38E, { 0xB8, 0x09, 0x00, 0x00, 0x00 }, 5, nullptr, g_maxCars, 1, 4 },
            // Ton je Auto 0x6563C8: Löschlänge 8 Dwords
            { 0x4A876E, { 0xB9, 0x08, 0x00, 0x00, 0x00 }, 5, nullptr, g_maxCars, 1, 4 },
            // Pool der KI-Zustände (FUN_0040B490/FUN_0040B4B0): Löschlänge 10 Belegt-Flags
            { 0x40B491, { 0xB9, 0x0A, 0x00, 0x00, 0x00 }, 5, nullptr, 2 * g_maxCars, 1, 4 },
            { 0x40B4B1, { 0xB9, 0x0A, 0x00, 0x00, 0x00 }, 5, nullptr, 2 * g_maxCars, 1, 4 },
            // Beschreibungen: sichern, zurückholen, löschen
            { 0x413E7A, { 0xB9, 0x89, 0x0F, 0x00, 0x00 }, 5, reinterpret_cast<void *>(&DescSaveStub), 0, 0, 0 },
            { 0x413FF1, { 0xB9, 0x89, 0x0F, 0x00, 0x00 }, 5, reinterpret_cast<void *>(&DescSaveStub), 0, 0, 0 },
            { 0x414687, { 0xB9, 0x89, 0x0F, 0x00, 0x00 }, 5, reinterpret_cast<void *>(&DescSaveStub), 0, 0, 0 },
            { 0x413DC5, { 0xC7, 0x05, 0xFC, 0x99, 0x5E, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0xA5 }, 12,
              reinterpret_cast<void *>(&DescRestoreStub), 0, 0, 0 },
            { 0x4B4DAA, { 0x68, 0x24, 0x3E, 0x00, 0x00 }, 5, reinterpret_cast<void *>(&DescClearStub), 0, 0, 0 },
            { 0x4B6AB3, { 0x68, 0x24, 0x3E, 0x00, 0x00 }, 5, reinterpret_cast<void *>(&DescClearStub), 0, 0, 0 },
        };

        // 1. Alles prüfen, bevor etwas geschrieben wird
        int bad = 0;
        for (const auto & site : kCarLimitSites)
        {
            if (*reinterpret_cast<const std::uint32_t *>(site.site) != site.value)
            {
                if (bad++ < 5)
                {
                    Log("Autos: Stelle %08X hat %08X statt %08X", site.site,
                        *reinterpret_cast<const std::uint32_t *>(site.site), site.value);
                }
            }
        }
        for (const auto & c : code)
        {
            if (std::memcmp(reinterpret_cast<const void *>(c.at), c.expected, c.size) != 0)
            {
                Log("Autos: Code %08X passt nicht", static_cast<unsigned>(c.at));
                ++bad;
            }
        }
        if (bad)
        {
            Log("Autos: %d Abweichungen, MaxCars bleibt 9 (nichts geändert)", bad);
            g_maxCars = kGameCars;
            return;
        }

        // 2. Neuen Speicher anlegen und bisherigen Inhalt übernehmen
        for (std::uint32_t r = 0; r < kCarLimitRegionCount; ++r)
        {
            const std::uint32_t oldSize = kCarLimitRegionEnd[r] - kCarLimitRegionStart[r];
            const std::uint32_t newSize = RegionNewSize(r);
            auto * mem = static_cast<std::uint8_t *>(VirtualAlloc(nullptr, newSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!mem)
            {
                Log("Autos: kein Speicher, MaxCars bleibt 9");
                g_maxCars = kGameCars;
                for (std::uint32_t k = 0; k < r; ++k)
                {
                    VirtualFree(g_carLimitNew[k], 0, MEM_RELEASE);
                    g_carLimitNew[k] = nullptr;
                }
                return;
            }
            std::memcpy(mem, reinterpret_cast<const void *>(kCarLimitRegionStart[r]), oldSize < newSize ? oldSize : newSize);
            g_carLimitNew[r] = mem;
        }
        g_descShadow = static_cast<std::uint8_t *>(VirtualAlloc(nullptr, (g_maxCars - kGameCars) * kDescSize,
                                                                 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

        // 3. Code umschreiben
        auto write = [](std::uintptr_t at, const void * bytes, std::size_t size)
        {
            DWORD old = 0;
            VirtualProtect(reinterpret_cast<void *>(at), size, PAGE_EXECUTE_READWRITE, &old);
            std::memcpy(reinterpret_cast<void *>(at), bytes, size);
            VirtualProtect(reinterpret_cast<void *>(at), size, old, &old);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(at), size);
        };
        for (const auto & site : kCarLimitSites)
        {
            // Schleifenende (CMP reg,Ende): neues Ende; sonst gleicher Abstand zum Anfang des zugeordneten Bereichs.
            // Der Abstand kann negativ sein (gefaltete Basis, z. B. [ESI*4 + Liste - 0x48] im Ton, VERKEHR.md §6.7).
            const auto base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_carLimitNew[site.region]));
            const std::uint32_t value = site.end ? base + RegionNewSize(site.region)
                                                 : base + (site.value - kCarLimitRegionStart[site.region]);
            write(site.site, &value, 4);
        }
        for (const auto & c : code)
        {
            if (c.stub)
            {
                unsigned char call[12];
                std::memset(call, 0x90, sizeof(call));
                call[0] = 0xE8;
                const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(c.stub) - (c.at + 5));
                std::memcpy(call + 1, &rel, 4);
                write(c.at, call, c.size);
            }
            else if (c.immSize == 1)
            {
                const auto v = static_cast<unsigned char>(c.value);
                write(c.at + c.immOffset, &v, 1);
            }
            else
            {
                write(c.at + c.immOffset, &c.value, 4);
            }
        }

        static const unsigned char kCarAddBytes[] = { 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C };
        static const unsigned char kFileWriteBytes[] = { 0x8B, 0x44, 0x24, 0x04, 0x81, 0xEC, 0x04, 0x01, 0x00, 0x00 };
        static const unsigned char kReplayLoadBytes[] = { 0x83, 0xEC, 0x54, 0xA1, 0x60, 0xB3, 0x65, 0x00 };
        InstallHook("Autos Wiederholung speichern", 0x59E170, kFileWriteBytes, sizeof(kFileWriteBytes),
                    reinterpret_cast<void *>(&HookFileWrite), reinterpret_cast<void **>(&g_origFileWrite));
        InstallHook("Autos Wiederholung laden", 0x4146F0, kReplayLoadBytes, sizeof(kReplayLoadBytes),
                    reinterpret_cast<void *>(&HookReplayLoad), reinterpret_cast<void **>(&g_origReplayLoad));
        InstallHook("Autos Liste spiegeln", 0x40F160, kCarAddBytes, sizeof(kCarAddBytes),
                    reinterpret_cast<void *>(&HookCarAdd), reinterpret_cast<void **>(&g_origCarAdd));

        // 4. Befehlstabelle des Menüs (0x5D1E40…): schon eingetragene Adressen der Beschreibungen umrechnen
        int table = 0;
        for (std::uintptr_t at = 0x5D1E40; at < 0x5D4000; at += 4)
        {
            auto & v = *reinterpret_cast<std::uint32_t *>(at);
            if (v >= kCarLimitRegionStart[kDescRegion] && v < kCarLimitRegionEnd[kDescRegion])
            {
                v = static_cast<std::uint32_t>(CarAddr(v));
                ++table;
            }
        }
        Log("Autos: MaxCars %d aktiv, %u Code-Stellen, %u Sonderstellen, %d Tabelleneinträge umgerechnet", g_maxCars,
            static_cast<unsigned>(sizeof(kCarLimitSites) / sizeof(kCarLimitSites[0])),
            static_cast<unsigned>(sizeof(code) / sizeof(code[0])), table);
    }

    void InstallTrafficCount()
    {
        static const unsigned char kTrackBytes[] = { 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00 };
        InstallHook("Verkehr Anzahl je Stufe", kTrackRecord, kTrackBytes, sizeof(kTrackBytes),
                    reinterpret_cast<void *>(&HookTrackRecordStub), &g_origTrackRecord);
    }

    // 12.4g: Einsetzen der Verkehrsautos (VERKEHR.md §4d). Ein Auto, das aus dem Bild fällt, wartet im Zustand
    // FUN_0040DE10 zufällig 1..Tabelle[Anzahl Verkehrsautos] Takte (0x5CC4A0, 12 Einträge 25..1200) und fährt wieder
    // ein, solange weniger als Tabelle 0x5CC480 (5, im Modus 1: 1) fahren. Ab 12 Verkehrsautos las das Spiel hinter
    // der Tabelle (Index 15 = 12 Takte): alle warteten fast nicht, mehrere setzten an derselben Stelle ein.
    std::int32_t g_trafficDelay[16 + 1] = {};
    std::int32_t g_trafficActiveTable[8] = {};

    void InstallTrafficSpawn()
    {
        static const unsigned char kDelayBytes[]  = { 0x0F, 0xAF, 0x04, 0x8D, 0xA0, 0xC4, 0x5C, 0x00 };
        static const unsigned char kActiveBytes[] = { 0x8B, 0x3C, 0x95, 0x80, 0xC4, 0x5C, 0x00 };
        const auto * game = reinterpret_cast<const std::int32_t *>(0x5CC4A0);
        for (int i = 0; i <= 16; ++i)
        {
            g_trafficDelay[i] = i < 12 ? game[i] : game[11] + 100 * (i - 11);
        }
        const auto * active = reinterpret_cast<const std::int32_t *>(0x5CC480);
        for (int i = 0; i < 8; ++i)
        {
            g_trafficActiveTable[i] = active[i] == 5 ? g_trafficActive : active[i];
        }
        unsigned char delay[sizeof(kDelayBytes)];
        unsigned char act[sizeof(kActiveBytes)];
        std::memcpy(delay, kDelayBytes, sizeof(delay));
        std::memcpy(act, kActiveBytes, sizeof(act));
        const auto delayAddr = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_trafficDelay));
        const auto activeAddr = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_trafficActiveTable));
        std::memcpy(delay + 4, &delayAddr, 4);
        std::memcpy(act + 3, &activeAddr, 4);
        if (g_maxCars > kGameCars)
        {
            PatchBytes("Verkehr Wartezeit je Anzahl", 0x40DEA0, kDelayBytes, delay, sizeof(delay));
        }
        if (g_trafficActive != 5)
        {
            PatchBytes("Verkehr gleichzeitig", 0x40E09C, kActiveBytes, act, sizeof(act));
        }
    }

    void InstallTrafficModels()
    {
        static const unsigned char kModelBytes[] = { 0xA1, 0xFC, 0x19, 0x69, 0x00 };
        InstallHook("Verkehr Modell-Lookup", kModelById, kModelBytes, sizeof(kModelBytes),
                    reinterpret_cast<void *>(&HookModelByIdStub), reinterpret_cast<void **>(&g_origModelById));
    }

    void InstallTrafficMeasure()
    {
        static const unsigned char kSetupBytes[] = { 0x83, 0xEC, 0x2C, 0x53, 0x55 };
        static const unsigned char kCarBytes[]   = { 0x81, 0xEC, 0x28, 0x01, 0x00, 0x00 };
        static const unsigned char kSimBytes[]   = { 0x83, 0xEC, 0x64, 0x53, 0x56 };
        InstallHook("Verkehr Rennaufbau", kRaceSetup, kSetupBytes, sizeof(kSetupBytes),
                    reinterpret_cast<void *>(&HookRaceSetup), reinterpret_cast<void **>(&g_origRaceSetup));
        InstallHook("Verkehr Auto-Befehle", kRaceCar, kCarBytes, sizeof(kCarBytes),
                    reinterpret_cast<void *>(&HookRaceCar), reinterpret_cast<void **>(&g_origRaceCar));
        InstallHook("Verkehr KI-Aufbau", kSimAiConfig, kSimBytes, sizeof(kSimBytes),
                    reinterpret_cast<void *>(&HookSimAiConfig), reinterpret_cast<void **>(&g_origSimAiConfig));
    }

    void Init(HMODULE self)
    {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string base = path;
        base.erase(base.find_last_of('.'));
        g_log = std::fopen((base + ".log").c_str(), "w");
        Log("LFSP v7y gestartet");

        auto * exe = reinterpret_cast<unsigned char *>(GetModuleHandleA(nullptr));
        const auto * dos = reinterpret_cast<IMAGE_DOS_HEADER *>(exe);
        const auto * nt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(exe + dos->e_lfanew);
        if (reinterpret_cast<std::uintptr_t>(exe) != 0x400000 || nt->FileHeader.TimeDateStamp != kExpectedTimestamp)
        {
            Log("Falsche Porsche.exe (TimeDateStamp 0x%08X), keine Hooks", nt->FileHeader.TimeDateStamp);
            return;
        }

        LoadConfig(base + ".ini");
        // 12.4: vor allen anderen Eingriffen und bevor das Spiel die Tabellen benutzt
        InstallCarLimit();
        g_soundDir = base.substr(0, base.find_last_of("\\/") + 1);

        static const unsigned char kLoadBytes[]    = { 0x81, 0xEC, 0xB4, 0x00, 0x00, 0x00 };
        static const unsigned char kUpdateBytes[]  = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x8C, 0x00, 0x00, 0x00 };
        static const unsigned char kSetRoofBytes[] = { 0x56, 0x8B, 0xF1, 0x33, 0xD2 };
        InstallHook("Modell-Laden", kModelLoad, kLoadBytes, sizeof(kLoadBytes),
                    reinterpret_cast<void *>(&HookLoad), reinterpret_cast<void **>(&g_origLoad));
        InstallHook("Modell-Update", kModelUpdate, kUpdateBytes, sizeof(kUpdateBytes),
                    reinterpret_cast<void *>(&HookUpdate), reinterpret_cast<void **>(&g_origUpdate));
        InstallHook("Car.Roof-Menue", kFeSetRoof, kSetRoofBytes, sizeof(kSetRoofBytes),
                    reinterpret_cast<void *>(&HookSetRoof), reinterpret_cast<void **>(&g_origSetRoof));
        static const unsigned char kBuildGeoBytes[] = { 0x83, 0xEC, 0x1C, 0x53, 0x8B, 0xD9 };
        InstallHook("Geometrie-Aufbau", kBuildGeo, kBuildGeoBytes, sizeof(kBuildGeoBytes),
                    reinterpret_cast<void *>(&HookBuildGeo), reinterpret_cast<void **>(&g_origBuildGeo));

        // Phase 12: Verkehr messen
        if (g_trafficMeasure)
        {
            InstallTrafficMeasure();
        }
        // 12.2: Verkehrsautos je Menüstufe
        if (g_trafficCount[1] != 2 || g_trafficCount[2] != 5 || g_trafficCount[3] != 7)
        {
            InstallTrafficCount();
        }
        // 12.4g: Wartezeit beim Einsetzen und Zahl gleichzeitig fahrender Verkehrsautos
        if (g_maxCars > kGameCars || g_trafficActive != 5)
        {
            InstallTrafficSpawn();
        }
        // 12.3: Verkehrsmodelle zufällig
        if (g_trafficMeasure || g_trafficRandom)
        {
            InstallTrafficModels();
        }

        // 11.1/11.2: Kamera (Takt für Cinematic und Tasten, View-Aufbau für Action, Kamerataste für den Wechsel)
        if (g_cineInCycle || g_cineKey || g_action || g_cockpitFov > 0.0f)
        {
            static const unsigned char kCamTickBytes[]  = { 0xA0, 0x40, 0x7F, 0x62, 0x00 };
            static const unsigned char kCamViewBytes[]  = { 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00 };
            static const unsigned char kCamCycleBytes[] = { 0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10 };
            InstallHook("Kamera-Takt", kCamTick, kCamTickBytes, sizeof(kCamTickBytes),
                        reinterpret_cast<void *>(&HookCamTick), reinterpret_cast<void **>(&g_origCamTick));
            InstallHook("Kamera-View", kCamView, kCamViewBytes, sizeof(kCamViewBytes),
                        reinterpret_cast<void *>(&HookCamView), reinterpret_cast<void **>(&g_origCamView));
            InstallHook("Kamerataste", kCamCycle, kCamCycleBytes, sizeof(kCamCycleBytes),
                        reinterpret_cast<void *>(&HookCamCycle), reinterpret_cast<void **>(&g_origCamCycle));
        }

        // 11.3: HUD-Instrumente nach Lichtschalter (Aufbau an beiden Aufrufstellen, Abbau an beiden Destruktoren)
        if (g_gaugesFollowLights)
        {
            static const unsigned char kGaugeDtorBytes[] = { 0x56, 0x8B, 0xF1, 0x8B, 0x8E, 0xC4, 0x00, 0x00, 0x00 };
            const bool dtors =
                InstallHook("Instrument-Abbau", kGaugeDtorDelete, kGaugeDtorBytes, sizeof(kGaugeDtorBytes),
                            reinterpret_cast<void *>(&HookGaugeDtorDelete), reinterpret_cast<void **>(&g_origGaugeDtorDelete)) &&
                InstallHook("Instrument-Abbau 2", kGaugeDtor, kGaugeDtorBytes, sizeof(kGaugeDtorBytes),
                            reinterpret_cast<void *>(&HookGaugeDtor), reinterpret_cast<void **>(&g_origGaugeDtor));
            for (std::uintptr_t site : { kGaugeCallSpeedo, kGaugeCallTach })
            {
                if (!dtors)
                {
                    break; // ohne Abbau-Hook keine eigenen Bilder (sonst Zeiger auf freigegebenen Speicher)
                }
                unsigned char expected[5] = { 0xE8 };
                unsigned char replacement[5] = { 0xE8 };
                const auto rel = static_cast<std::int32_t>(kGaugeCreate - (site + 5));
                const auto relHook = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&HookGaugeCreate) - (site + 5));
                std::memcpy(expected + 1, &rel, 4);
                std::memcpy(replacement + 1, &relHook, 4);
                PatchBytes("Instrument-Aufbau", site, expected, replacement, sizeof(expected));
            }
        }

        // 11.5d: Lage und Farbe jeder Leuchte für den Glow von dx11z (Blinker vorn und hinten, Farbe der Leuchte)
        for (const auto & site : { std::make_pair(kLampCall, reinterpret_cast<void *>(&LampTestDetour)),
                                   std::make_pair(kLampCallMirr, reinterpret_cast<void *>(&LampTestMirroredDetour)) })
        {
            unsigned char expected[5] = { 0xE8 };
            unsigned char replacement[5] = { 0xE8 };
            const auto rel = static_cast<std::int32_t>(kLampTest - (site.first + 5));
            const auto relHook = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(site.second) - (site.first + 5));
            std::memcpy(expected + 1, &rel, 4);
            std::memcpy(replacement + 1, &relHook, 4);
            PatchBytes("Leuchten-Abfrage", site.first, expected, replacement, sizeof(expected));
        }

        // 11.5f: Die Leuchten-Logik (0x439360) setzt die Blinker vorn (4/5) auf den Takt P, hinten (6/7) auf "nicht P"
        // (MOV EAX,EDI; NOT EAX; AND EAX,1). Ohne das NOT blinken alle Leuchten einer Seite gleichzeitig.
        if (g_signalsInSync)
        {
            static const unsigned char kNotBytes[] = { 0x8B, 0xC7, 0xF7, 0xD0, 0x83, 0xE0, 0x01 };
            static const unsigned char kNotPatch[] = { 0x8B, 0xC7, 0x90, 0x90, 0x83, 0xE0, 0x01 };
            PatchBytes("Blinker hinten links im Takt", 0x439438, kNotBytes, kNotPatch, sizeof(kNotBytes));
            PatchBytes("Blinker hinten rechts im Takt", 0x439470, kNotBytes, kNotPatch, sizeof(kNotBytes));
        }

        // B19: Das Spiel nimmt je Strecken-Abschnitt die erste Detailstufe, deren Reichweite die Entfernung
        // abdeckt; die gröberen Stufen schließen nicht dicht an (Himmel scheint durch). Die Schleife davor
        // bleibt (jenseits der letzten Reichweite wird nichts gezeichnet), nur der Zeiger zeigt immer auf Stufe 0:
        // LEA EAX,[ESI+ESI*2]; LEA EDX,[EDI+EAX*4+0x64]  ->  LEA EDX,[EDI+0x64]; NOP x4
        if (g_trackDetail >= 1 || g_trackDiag >= 1)
        {
            static const unsigned char kLodBytes[] = { 0x8D, 0x04, 0x76, 0x8D, 0x54, 0x87, 0x64 };
            static const unsigned char kLodPatch[] = { 0x8D, 0x57, 0x64, 0x90, 0x90, 0x90, 0x90 };
            // Mit Diagnose (B21) oder TrackDetail = 3 (gröbste Stufe, Test) wählt TrackLevelSelect die Stufe,
            // sonst reicht der Byte-Patch auf Stufe 0.
            const bool lod = (g_trackDiag >= 1 || g_trackDetail == 3)
                                 ? PatchJump("Strecke Detailstufe (Diagnose)", kTrackLod, kLodBytes, sizeof(kLodBytes),
                                             reinterpret_cast<void *>(&TrackLevelDetour))
                                 : PatchBytes("Strecke feinste Detailstufe", kTrackLod, kLodBytes, kLodPatch, sizeof(kLodBytes));

            // TrackDetail = 2: Liegt der Abschnitt jenseits der letzten Reichweite, zeichnet das Spiel ihn nicht
            // (POP EDI; POP ESI -> return). Stattdessen zum Zeichnen springen; das geht nur zusammen mit dem
            // Patch oben, denn ESI zeigt dann hinter die Tabelle. Welche Abschnitte in Frage kommen, entscheidet
            // weiter die Chunk-Auswahl (0x47F550) bzw. DrawDistanceScale.
            if (g_trackDetail == 2 && lod)
            {
                static const unsigned char kCutBytes[] = { 0x5F, 0x5E, 0x5B };
                static const unsigned char kCutPatch[] = { 0xEB, 0x07, 0x5B }; // JMP 0x450E86
                PatchBytes("Strecke ohne Reichweiten-Grenze", kTrackCutoff, kCutBytes, kCutPatch, sizeof(kCutBytes));
            }

            // TrackDetail = 1 mit TrackRange > 1: Die Grenze je Abschnitt bleibt, liegt aber um den Faktor weiter.
            // Fängt Abschnitte, die knapp vor ihren Nachbarn wegfallen (fehlendes Dreieck), ohne alles Ausgewählte
            // ungefiltert zu zeichnen wie TrackDetail = 2.
            if (g_trackDetail == 1 && lod && g_trackRange > 1.0f)
            {
                static const unsigned char kDistBytes[] = { 0xDF, 0x6D, 0xF0, 0x8D, 0x04, 0x76, 0xD9, 0x5D, 0xF4 };
                PatchJump("Strecke Reichweite x TrackRange", kTrackDist, kDistBytes, sizeof(kDistBytes),
                          reinterpret_cast<void *>(&TrackDistDetour));
            }
        }

        // B21-Diagnose: Dreieck-Clipper von EAGL gegenprüfen.
        if (g_clipDiag >= 1)
        {
            static const unsigned char kClipA[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14 };
            static const unsigned char kClipB[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18 };
            InstallHook("EAGL-Clipper A (B21)", 0x54C030, kClipA, sizeof(kClipA), reinterpret_cast<void *>(&ClipHook<0>),
                        &g_clipOrig[0]);
            InstallHook("EAGL-Clipper B (B21)", 0x5498B0, kClipB, sizeof(kClipB), reinterpret_cast<void *>(&ClipHook<1>),
                        &g_clipOrig[1]);
        }

        // B21-Diagnose: verworfene Strecken-Objekte protokollieren (1) bzw. zusätzlich zeichnen (2).
        if (g_trackDiag >= 1)
        {
            static const unsigned char kFlagBytes[] = { 0xA9, 0x00, 0x00, 0x04, 0x00, 0x74, 0x18, 0xA0, 0xE6, 0x97,
                                                        0x61, 0x00, 0x84, 0xC0, 0x74, 0x0F };
            PatchJump("Strecke Objekt-Diagnose (B21)", kTrackFlags, kFlagBytes, sizeof(kFlagBytes),
                      reinterpret_cast<void *>(&TrackFlagDetour));
        }

        // B15: Listen der Render-Warteschlange für dx11z markieren (nur Markierung, das Spiel zeichnet unverändert).
        {
            static const unsigned char kStateCall[] = { 0xFF, 0x15, 0x7C, 0xD9, 0x6B, 0x00 };
            static const unsigned char kList4[] = { 0x8B, 0x4E, 0x10, 0x8B, 0x11 };
            static const unsigned char kDrain[] = { 0xE9, 0xC8, 0xF3, 0xFF, 0xFF };
            PatchJump("Warteschlange Liste 1 (B15)", 0x46666D, kStateCall, sizeof(kStateCall), reinterpret_cast<void *>(&QueueList1Detour));
            PatchJump("Warteschlange Liste 2 (B15)", 0x4666B6, kStateCall, sizeof(kStateCall), reinterpret_cast<void *>(&QueueList2Detour));
            PatchJump("Warteschlange Liste 3 (B15)", 0x466703, kStateCall, sizeof(kStateCall), reinterpret_cast<void *>(&QueueList3Detour));
            PatchJump("Warteschlange Liste 4 (B15)", 0x466744, kList4, sizeof(kList4), reinterpret_cast<void *>(&QueueList4Detour));
            PatchJump("Warteschlange Ende (B15)", 0x467293, kDrain, sizeof(kDrain), reinterpret_cast<void *>(&QueueDrainDetour));
        }

        // B15: Mit Guard-Band (dx11z, [World] GuardBand) verwirft FUN_005408f0 Meshes und Dreiecke erst hinter den
        // Guard-Grenzen (Clip-Code Byte 0, 0x3C) statt schon hinter dem Bildrand (Byte 1, 0x3C00). Teile knapp außerhalb
        // des Bildes kommen so bei dx11z an und erscheinen in der Wasserspiegelung. near/far (0x03) bleiben.
        if (g_guardBand)
        {
            static const unsigned char kMeshBytes[] = { 0xF7, 0xC3, 0x00, 0x00, 0x03, 0x3C };
            static const unsigned char kMeshPatch[] = { 0xF7, 0xC3, 0x00, 0x00, 0x3F, 0x00 };
            static const unsigned char kTriBytes[] = { 0x81, 0xE3, 0x03, 0x3C, 0x00, 0x00 };
            static const unsigned char kTriPatch[] = { 0x81, 0xE3, 0x3F, 0x00, 0x00, 0x00 };
            PatchBytes("Mesh-Verwerfen am Guard-Band (B15)", 0x5409E4, kMeshBytes, kMeshPatch, sizeof(kMeshBytes));
            for (std::uintptr_t site : { 0x540A79u, 0x540AD1u, 0x540B34u, 0x540B84u })
                PatchBytes("Dreieck-Verwerfen am Guard-Band (B15)", site, kTriBytes, kTriPatch, sizeof(kTriBytes));
        }

        if (g_guardBand && g_clipMargin > 0.0f)
        {
            static const unsigned char kClipRect[] = { 0x8B, 0x01, 0x57, 0xBA, 0x01, 0x00, 0x00, 0x00 };
            InstallHook("Clip-Rechteck mit Rand (B15)", 0x540820, kClipRect, sizeof(kClipRect), reinterpret_cast<void *>(&ClipRectHook),
                        &g_clipRectOrig);
            Log("B15: Clip-Rechteck + %.2f je Seite", g_clipMargin);
        }

        if (g_cullRadiusScale > 1.0f)
        {
            static const unsigned char kSphere[] = { 0x83, 0xEC, 0x0C, 0x8D, 0x44, 0x24, 0x00, 0x56 };
            InstallHook("Objekt-Kugeltest mit Rand (B15)", 0x539480, kSphere, sizeof(kSphere), reinterpret_cast<void *>(&SphereHook),
                        &g_sphereOrig);
            Log("B15: Kugeltest-Radius x %.2f", g_cullRadiusScale);
        }
    }
}

// 12.4: Liste aller Autos (Spiel: 0x5E8B34, Zähler 0x5E8B58) und ihre Kapazität (9 oder [Cars] MaxCars), für
// dx11z und nfs5_enhanced, die die Liste selbst lesen. *list zeigt danach auf die aktuelle Liste.
extern "C" __declspec(dllexport) int LfspCarList(void *** list)
{
    if (list)
    {
        *list = reinterpret_cast<void **>(CarAddr(0x5E8B34));
    }
    return g_maxCars;
}

// B15: Liste der Render-Warteschlange, die gerade gezeichnet wird (0 = keine), für dx11z.
extern "C" __declspec(dllexport) int LfspQueueList()
{
    return g_queueList;
}

// Baujahr des Spielerautos im laufenden Rennen (0 = unbekannt / Menü), für dx11z (Lichtfarbe nach Baujahr, 9.9).
extern "C" __declspec(dllexport) int LfspPlayerCarYear()
{
    return InFrontend() ? 0 : g_playerYear;
}

// Farbton des Scheinwerferlichts nach Baujahr des Spielerautos (9.9): rgb[3] = Faktoren für die Lichtfarbe
// des Treibers. Rückgabe: Baujahr, 0 = kein Farbton (aus, Menü, Jahr unbekannt); rgb bleibt dann unverändert.
extern "C" __declspec(dllexport) int LfspHeadlightTint(float * rgb)
{
    float tint[3] = {};
    if (!rgb || !g_colourByYear || InFrontend() || !YearTint(g_playerYear, tint))
    {
        return 0;
    }
    rgb[0] = tint[0];
    rgb[1] = tint[1];
    rgb[2] = tint[2];
    return g_playerYear;
}

// Farbton für ein beliebiges Rennauto (Gegner-Scheinwerfer, 9.11): car = Fahrzeug-Objekt aus 0x5E8B34.
// Rückgabe: Baujahr, 0 = kein Farbton (aus, Menü, Auto unbekannt); rgb bleibt dann unverändert.
extern "C" __declspec(dllexport) int LfspHeadlightTintForCar(const void * car, float * rgb)
{
    if (!car || !rgb || !g_colourByYear || InFrontend())
    {
        return 0;
    }
    for (const auto & entry : g_models)
    {
        float tint[3] = {};
        if (entry.second.yearCar == car && YearTint(entry.second.year, tint))
        {
            rgb[0] = tint[0];
            rgb[1] = tint[1];
            rgb[2] = tint[2];
            return entry.second.year;
        }
    }
    return 0;
}

// Fernlicht des Spielerautos (9.10): 0 = Abblendlicht, 1 = Fernlicht, dazwischen beim Überblenden.
// dx11z fragt das je Bild ab; die Taste wird bei jedem Aufruf gelesen.
extern "C" __declspec(dllexport) float LfspHighBeam()
{
    return UpdateHighBeam();
}

// Leuchten eines Modells (11.5d), model = [car + 0x74C]: je bekannte Leuchte Nummer (Byte in [model + 0x2D4]),
// Lage am Modell und Farbe 0xAARRGGBB. Bekannt ist eine Leuchte, sobald das Spiel sie einmal gezeichnet hat.
// Rückgabe: Anzahl der Einträge in out (höchstens max).
struct LfspLamp
{
    int number;
    float pos[3];
    unsigned colour;
};

extern "C" __declspec(dllexport) int LfspModelLamps(const void * model, LfspLamp * out, int max)
{
    const auto it = g_modelLamps.find(model);
    if (!out || max <= 0 || it == g_modelLamps.end())
    {
        return 0;
    }
    int count = 0;
    for (int n = 0; n < kLampSlots && count < max; ++n)
    {
        const LampInfo & slot = it->second.slots[n];
        if (!slot.lamp)
        {
            continue;
        }
        out[count].number = n;
        out[count].pos[0] = slot.pos[0];
        out[count].pos[1] = slot.pos[1];
        out[count].pos[2] = slot.pos[2];
        out[count].colour = slot.colour;
        ++count;
    }
    return count;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        Init(module);
    }
    return TRUE;
}
