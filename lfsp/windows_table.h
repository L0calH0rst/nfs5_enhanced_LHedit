// Erzeugt mit gen_windows_table.py aus "Mod-Window Opener" (owindow vs. cwindow), nicht von Hand ändern.
// Mesh-Variante der Seitenscheiben im offenen Zustand: Fahrzeug.N = außen (Teil 35), innen (Teil 47),
// N wie [styleN] in der .tpg; 0 = Teil nicht ändern. lfsp.ini [Windows] kann einzelne Einträge ersetzen.
#pragma once

struct WindowTableEntry
{
    const char * key;
    int outer;
    int inner;
};

constexpr WindowTableEntry kWindowTable[] = {
    { "356_1.2",     1, 1 }, // Cabrio
    { "356a.1",      3, 3 }, // 13Cabrio
    { "356a.2",      3, 3 }, // Super13Cab
    { "356a.3",      3, 3 }, // 16Cabrio
    { "356a.4",      3, 3 }, // Super16Cab
    { "356a.5",      3, 3 }, // ConvtD
    { "356a.6",      3, 3 }, // SuperConvtD
    { "356a.11",     3, 3 }, // Speeds1600
    { "356a.12",     3, 3 }, // SuperSpeed
    { "356b.1",      1, 1 }, // Cabrio
    { "356b.2",      1, 1 }, // CabrioSuper
    { "356b.3",      1, 1 }, // CabrioSuper90
    { "356b.11",     1, 1 }, // Roadster
    { "356b.12",     1, 1 }, // RoadsterSuper
    { "356b.13",     1, 1 }, // RoadsterSuper90
    { "901.3",       1, 1 }, // Targa 2.0
    { "901.4",       1, 1 }, // Targa 2.0 LS
    { "901.6",       1, 1 }, // Targa 2.4
    { "914.1",       3, 3 }, // 914 1.7
    { "914.2",       1, 1 }, // 914 2.0
    { "914.3",       1, 1 }, // 914 1.8
    { "944.7",       3, 3 }, // cabrioturbo
    { "944.8",       3, 3 }, // S2cabrio
    { "964.1",       1, 1 }, // Carrera2Cab
    { "964.3",       1, 1 }, // Targa
    { "964.4",       1, 1 }, // Carrera4Cab
    { "964.6",       1, 1 }, // Carrera4Targa
    { "993.2",       1, 1 }, // Carreratarga
    { "993.3",       1, 1 }, // Carrera4cab
    { "993.6",       1, 1 }, // Carreracabrio
    { "996.1",       1, 1 }, // Carrera4Cabrio
    { "996.3",       1, 1 }, // Carrera3.4Cabrio
    { "boxster.1",   1, 1 }, // Boxster
    { "boxster.2",   1, 1 }, // BoxsterS
    { "boxster.3",   1, 1 }, // Bat
    { "fboxster.1",  1, 1 }, // Boxster
    { "fboxster.2",  1, 1 }, // BoxsterS
    { "fboxster.3",  1, 1 }, // Bat
};
