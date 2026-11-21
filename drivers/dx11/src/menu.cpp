// menu.cpp – dx11z: settings menu in the game (Dear ImGui, external/imgui, MIT licence).
//
// '[Menu] Key' in lfsp.ini (default F8) opens and closes it. The sliders change the live settings of dx11z, so the look
// changes at once; "Speichern" writes them back into lfsp.ini (only these keys, comments and everything else stay).
// Drawn last over the back buffer in PresentBackBuffer, after the frame and the distance probe. While it is open it takes
// mouse and keyboard messages of the window (WindowProc); the game's own DirectInput keys still reach the game.

#include "driver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND window, UINT message, WPARAM wparam, LPARAM lparam );

namespace dx11z
{
    namespace
    {
        // Number for lfsp.ini: as few digits as needed.
        std::string IniNumber( float value )
        {
            char text[32] = {};
            std::snprintf( text, sizeof( text ), "%.4f", value );
            std::string result = text;
            while (result.size() > 1 && result.back() == '0')
                result.pop_back();
            if (!result.empty() && result.back() == '.')
                result.pop_back();
            return result;
        }

        bool Toggle( const char * label, int & value )
        {
            bool on = value != 0;
            if (!ImGui::Checkbox( label, &on ))
                return false;
            value = on ? 1 : 0;
            return true;
        }

        void Hint( const char * text )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(?)" );
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip( "%s", text );
        }
    }


    void Driver::MenuShutdown()
    {
        if (!m_MenuReady)
            return;
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_MenuReady = false;
        m_MenuOpen = false;
    }


    bool Driver::MenuMessage(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam
    )
    {
        if (!m_MenuReady || !m_MenuOpen)
            return false;

        ImGui_ImplWin32_WndProcHandler( window, message, wparam, lparam );

        // Mouse and typing belong to the menu while it is open; activation, sizing and Alt keys stay with the game.
        switch (message)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
            return true;
        default:
            return false;
        }
    }


    void Driver::MenuSave()
    {
        if (m_LfspIni.empty())
            return;

        const char * ini = m_LfspIni.c_str();
        bool ok = true;
        const auto put = [&]( const char * section, const char * key, const std::string & value )
        {
            ok = ::WritePrivateProfileStringA( section, key, value.c_str(), ini ) != FALSE && ok;
        };
        const auto put_int = [&]( const char * section, const char * key, int value ) { put( section, key, std::to_string( value ) ); };
        const auto put_float = [&]( const char * section, const char * key, float value ) { put( section, key, IniNumber( value ) ); };

        put_int( "AmbientOcclusion", "Enable", m_ConfigAo );
        put_int( "AmbientOcclusion", "Quality", m_ConfigAoQuality );
        ::WritePrivateProfileStringA( "AmbientOcclusion", "SampleQuality", nullptr, ini ); // old key, replaced by 'Quality'
        put_int( "AmbientOcclusion", "ShadingRate", m_ConfigAoShadingRate );
        put_float( "AmbientOcclusion", "SampleRadius", m_ConfigAoRadius );
        put_int( "AmbientOcclusion", "IncreaseRadiusWithDistance", m_ConfigAoGrow );
        put_float( "AmbientOcclusion", "Amount", m_ConfigAoAmount );
        put_float( "AmbientOcclusion", "FadeDepth", m_ConfigAoFade );
        put_int( "AmbientOcclusion", "FilterQuality", m_ConfigAoFilter );
        put_int( "AmbientOcclusion", "Debug", m_ConfigAoDebug );
        put_int( "AmbientOcclusion", "Method", m_ConfigAoMethod );
        put_float( "AmbientOcclusion", "Thickness", m_ConfigAoThickness );
        put_float( "AmbientOcclusion", "AngleBias", m_ConfigAoAngleBias );
        put_int( "AmbientOcclusion", "DepthMips", m_ConfigAoDepthMips );
        put_int( "AmbientOcclusion", "Temporal", m_ConfigAoTemporal );
        put_float( "AmbientOcclusion", "TemporalBlend", m_ConfigAoTemporalBlend );
        put_int( "AmbientOcclusion", "MultiBounce", m_ConfigAoMultiBounce );
        put_int( "AmbientOcclusion", "FogFade", m_ConfigAoFogFade );

        put_float( "CarReflections", "Strength", m_ConfigCarReflStrength );
        put_float( "CarReflections", "Reflectance", m_ConfigCarReflF0 );
        put_float( "CarReflections", "Fresnel", m_ConfigCarReflFresnel );
        put_float( "CarReflections", "Blur", m_ConfigCarReflBlur );
        put_float( "CarReflections", "Ground", m_ConfigCarReflGround );
        put_int( "CarReflections", "Sky", m_ConfigCarReflSky );
        put_int( "CarReflections", "BlurChain", m_ConfigCarReflBlurChain );
        put_float( "CarReflections", "HighlightBoost", m_ConfigCarReflHighlightBoost );
        put_float( "CarReflections", "HighlightThreshold", m_ConfigCarReflHighlightThreshold );
        put_float( "CarReflections", "CreaseAngle", m_ConfigCarReflCreaseDeg );
        put_int( "CarReflections", "ClearCoat", m_ConfigCarReflClearCoat );
        put_float( "CarReflections", "BaseCoat", m_ConfigCarReflBaseCoat );
        put_float( "CarReflections", "BaseCoatBlur", m_ConfigCarReflBaseBlur );
        put_float( "CarReflections", "BaseCoatTint", m_ConfigCarReflBaseTint );
        put_int( "CarReflections", "Debug", m_ConfigCarReflDebug );

        put_float( "Water", "ReflectionStrength", m_ConfigReflectionStrength );
        put_float( "Water", "ReflectionStrengthNight", m_ConfigReflectionStrengthNight );
        put_float( "Water", "ReflectionFresnel", m_ConfigReflectionFresnel );
        put_float( "Water", "ReflectionGrazing", m_ConfigReflectionGrazing );
        put_float( "Water", "ReflectionBlur", m_ConfigReflectionBlur );
        put_float( "Water", "ReflectionWaves", m_ConfigReflectionWaves );
        put_int( "Water", "ReflectionWorldWaves", m_ConfigReflectionWorldWaves );
        put_float( "Water", "ReflectionWaveSize", m_ConfigReflectionWaveSize );
        put_float( "Water", "ReflectionWaveDirection", m_ConfigReflectionWaveDirection );
        put_float( "Water", "ReflectionWaveSpread", m_ConfigReflectionWaveSpread );
        put_float( "Water", "ReflectionWaveSlope", m_ConfigReflectionWaveSlope );
        put_float( "Water", "ReflectionWaveHeight", m_ConfigReflectionWaveHeight );
        put_float( "Water", "ReflectionSunGlint", m_ConfigReflectionSunGlint );
        put_float( "Water", "ReflectionLightBoost", m_ConfigReflectionLightBoost );
        put_float( "Water", "ReflectionLightStreak", m_ConfigReflectionLightStreak );

        m_MenuStatus = ok ? "Gespeichert in lfsp.ini" : "Speichern fehlgeschlagen";
        Log( "Menu: settings saved to %s (%s)", ini, ok ? "ok" : "FAILED" );
    }


    void Driver::MenuFrame()
    {
        if (m_MenuKey == 0 || !m_Device || !m_Context || !m_BackBufferView || m_MenuFailed)
            return;

        const bool down = ::GetForegroundWindow() == m_Window && (::GetAsyncKeyState( m_MenuKey ) & 0x8000) != 0;
        if (down && !m_MenuKeyDown)
        {
            m_MenuOpen = !m_MenuOpen;
            Log( "Menu %s", m_MenuOpen ? "open" : "closed" );
        }
        m_MenuKeyDown = down;

        if (!m_MenuOpen)
            return;

        if (!m_MenuReady)
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO & io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            ImGui::StyleColorsDark();
            // Readable at 1440 lines and more.
            const float scale = std::max( 1.0f, static_cast<float>(m_BackBufferHeight) / 900.0f );
            ImGui::GetStyle().ScaleAllSizes( scale );
            io.FontGlobalScale = scale;
            if (!ImGui_ImplWin32_Init( m_Window ) || !ImGui_ImplDX11_Init( m_Device.Get(), m_Context.Get() ))
            {
                Log( "Menu: Dear ImGui could not be set up" );
                ImGui::DestroyContext();
                m_MenuFailed = true;
                m_MenuOpen = false;
                return;
            }
            m_MenuReady = true;
            Log( "Menu: Dear ImGui %s ready, scale %.2f", ImGui::GetVersion(), scale );
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::GetIO().MouseDrawCursor = true; // the game hides the Windows cursor
        ImGui::NewFrame();

        ImGui::SetNextWindowPos( ImVec2( 40.0f, 40.0f ), ImGuiCond_FirstUseEver );
        ImGui::SetNextWindowSize( ImVec2( 620.0f * ImGui::GetIO().FontGlobalScale, 760.0f * ImGui::GetIO().FontGlobalScale ), ImGuiCond_FirstUseEver );
        ImGui::Begin( "dx11z Einstellungen", &m_MenuOpen );
        ImGui::TextDisabled( "Änderungen wirken sofort. Taste zum Schließen wie zum Öffnen." );

        if (ImGui::CollapsingHeader( "Umgebungsschatten (AO)", ImGuiTreeNodeFlags_DefaultOpen ))
        {
            ImGui::PushID( "ao" );
            Toggle( "An", m_ConfigAo );
            static const char * const qualities[] = { "Niedrig", "Mittel", "Hoch" };
            m_ConfigAoQuality = std::clamp( m_ConfigAoQuality, 0, 2 );
            ImGui::Combo( "Qualität", &m_ConfigAoQuality, qualities, 3 );
            Toggle( "Halbe Shading-Rate (Schachbrett)", m_ConfigAoShadingRate );
            ImGui::SliderFloat( "Radius", &m_ConfigAoRadius, 0.5f, 10.0f, "%.3f" );
            Toggle( "Radius wächst mit der Entfernung", m_ConfigAoGrow );
            ImGui::SliderFloat( "Stärke", &m_ConfigAoAmount, 0.0f, 2.0f, "%.2f" );
            ImGui::SliderFloat( "Ausblenden (Tiefe)", &m_ConfigAoFade, 0.01f, 1.0f, "%.3f" );
            ImGui::SliderInt( "Filter", &m_ConfigAoFilter, 0, 2 );
            Toggle( "Nur Schatten zeigen (Prüfbild)", m_ConfigAoDebug );
            static const char * const methods[] = { "Wie MXAO", "Exakt (GT-VBAO)" };
            m_ConfigAoMethod = std::clamp( m_ConfigAoMethod, 0, 1 );
            ImGui::Combo( "Gewichtung", &m_ConfigAoMethod, methods, 2 );
            ImGui::SliderFloat( "Dicke der Objekte", &m_ConfigAoThickness, 0.1f, 4.0f, "%.2f" );
            ImGui::SliderFloat( "Winkel-Schwelle", &m_ConfigAoAngleBias, 0.0f, 30.0f, "%.1f Grad" );
            Toggle( "Tiefenpyramide (schneller)", m_ConfigAoDepthMips );
            Toggle( "Zeitliche Glättung", m_ConfigAoTemporal );
            ImGui::SliderFloat( "Anteil Vorbild", &m_ConfigAoTemporalBlend, 0.0f, 0.97f, "%.2f" );
            Toggle( "Mehrfachstreuung (hell, farbig)", m_ConfigAoMultiBounce );
            Toggle( "Im Nebel ausblenden", m_ConfigAoFogFade );
            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader( "Autoreflexion und Klarlack", ImGuiTreeNodeFlags_DefaultOpen ))
        {
            ImGui::PushID( "car" );
            ImGui::SliderFloat( "Stärke", &m_ConfigCarReflStrength, 0.0f, 1.0f, "%.2f" );
            ImGui::SliderFloat( "Reflectance (von vorn)", &m_ConfigCarReflF0, 0.0f, 1.0f, "%.3f" );
            ImGui::SliderFloat( "Fresnel-Exponent", &m_ConfigCarReflFresnel, 1.0f, 8.0f, "%.2f" );
            ImGui::SliderFloat( "Unschärfe", &m_ConfigCarReflBlur, 0.0f, 8.0f, "%.2f" );
            ImGui::SliderFloat( "Nach unten (Boden)", &m_ConfigCarReflGround, 0.0f, 1.0f, "%.2f" );
            Toggle( "Echter Himmel", m_ConfigCarReflSky );
            Toggle( "Weiche Unschärfe-Kette", m_ConfigCarReflBlurChain );
            ImGui::Separator();
            ImGui::SliderFloat( "Glanzlichter Boost", &m_ConfigCarReflHighlightBoost, 0.0f, 16.0f, "%.2f" );
            ImGui::SliderFloat( "Glanzlichter Schwelle", &m_ConfigCarReflHighlightThreshold, 0.0f, 1.0f, "%.2f" );
            if (ImGui::SliderFloat( "Kantenwinkel (Grad)", &m_ConfigCarReflCreaseDeg, 0.0f, 90.0f, "%.0f" ))
                m_ConfigCarReflCreaseCos = m_ConfigCarReflCreaseDeg > 0.0f ? std::cos( m_ConfigCarReflCreaseDeg * 0.01745329f ) : -2.0f;
            Hint( "0 = alle Flächen eines Punkts gemittelt; kleiner = mehr scharfe Kanten" );
            Toggle( "Normalen im Teilraum (9.15e)", m_ConfigCarReflObjectNormals );
            Hint( "feste Ausrichtung je Teil, einmal gerechnet und behalten; ruhiger und schneller" );
            Toggle( "Mittelnaht glätten", m_ConfigCarReflSeamFix );
            ImGui::Separator();
            Toggle( "Klarlack (zwei Schichten)", m_ConfigCarReflClearCoat );
            ImGui::SliderFloat( "Grundlack Stärke", &m_ConfigCarReflBaseCoat, 0.0f, 1.0f, "%.2f" );
            ImGui::SliderFloat( "Grundlack Unschärfe", &m_ConfigCarReflBaseBlur, 0.0f, 6.0f, "%.1f" );
            ImGui::SliderFloat( "Grundlack Lackfarbe", &m_ConfigCarReflBaseTint, 0.0f, 1.0f, "%.2f" );
            static const char * const views[] = { "Normal", "Normalen", "Spiegelbild", "Fresnel" };
            ImGui::Combo( "Prüfbild", &m_ConfigCarReflDebug, views, 4 );
            ImGui::PopID();
        }

        if (ImGui::CollapsingHeader( "Wasser" ))
        {
            ImGui::PushID( "water" );
            ImGui::SliderFloat( "Stärke", &m_ConfigReflectionStrength, 0.0f, 1.0f, "%.2f" );
            ImGui::SliderFloat( "Stärke bei Nacht", &m_ConfigReflectionStrengthNight, 0.0f, 1.0f, "%.2f" );
            ImGui::SliderFloat( "Fresnel (von oben)", &m_ConfigReflectionFresnel, 0.0f, 1.0f, "%.2f" );
            ImGui::SliderFloat( "Flacher Blick", &m_ConfigReflectionGrazing, 0.0f, 1.0f, "%.2f" );
            ImGui::SliderFloat( "Unschärfe", &m_ConfigReflectionBlur, 0.0f, 20.0f, "%.1f" );
            ImGui::Separator();
            Toggle( "Wellen fest auf dem Wasser", m_ConfigReflectionWorldWaves );
            ImGui::SliderFloat( "Wellen im Bild (ohne Weltwellen)", &m_ConfigReflectionWaves, 0.0f, 20.0f, "%.1f" );
            ImGui::SliderFloat( "Wellengröße", &m_ConfigReflectionWaveSize, 0.25f, 16.0f, "%.2f" );
            ImGui::SliderFloat( "Wellenrichtung (Grad)", &m_ConfigReflectionWaveDirection, 0.0f, 360.0f, "%.0f" );
            ImGui::SliderFloat( "Streuung", &m_ConfigReflectionWaveSpread, 0.0f, 3.0f, "%.2f" );
            ImGui::SliderFloat( "Neigung", &m_ConfigReflectionWaveSlope, 0.0f, 0.5f, "%.3f" );
            ImGui::SliderFloat( "Relief-Höhe (m)", &m_ConfigReflectionWaveHeight, 0.0f, 6.0f, "%.2f" );
            ImGui::SliderFloat( "Sonnenglanz", &m_ConfigReflectionSunGlint, 0.0f, 20.0f, "%.2f" );
            ImGui::SliderFloat( "Lichter Verstärkung", &m_ConfigReflectionLightBoost, 0.0f, 16.0f, "%.1f" );
            ImGui::SliderFloat( "Lichter Streifen", &m_ConfigReflectionLightStreak, 0.0f, 40.0f, "%.1f" );
            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button( "Speichern" ))
            MenuSave();
        ImGui::SameLine();
        ImGui::TextUnformatted( m_MenuStatus.c_str() );
        ImGui::TextDisabled( "Gesperrte Schalter (Spiegelung an/aus, Lichter im Wasser) brauchen einen Neustart." );
        ImGui::End();

        ImGui::Render();
        ID3D11RenderTargetView * target = m_BackBufferView.Get();
        m_Context->OMSetRenderTargets( 1, &target, nullptr );
        ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
    }
}
