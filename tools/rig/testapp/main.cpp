// Stand-in for the game in the rig script tests (tools/rig/tests). One process that owns one top-level
// window, so the scripts can find it, ask it to close with WM_CLOSE and stop it after a timeout.
//
// By default the window is placed far off-screen, is not activated and has no taskbar button, so a test
// run never covers the owner's screen or takes the keyboard focus. Game-style "+cvar value" arguments
// are ignored. Options:
//   --visible        normal window at the default position (manual checks only)
//   --hang           ignore WM_CLOSE; only a forced stop ends the process
//   --exit-now       exit at once with code 3, without a window (a launch that fails)
//   --handoff        start a copy of this program without --handoff, then exit with code 0
//   --audio          keep a silent audio stream open, so the process owns an audio session
//   --audio-muted    with --audio: mute its own audio session first (a mute set in the Windows mixer)
//   --exit-after=MS  close the window by itself after MS milliseconds
//   --slow-exit=MS   after the window has closed, keep the process alive for MS milliseconds

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <objbase.h>

#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <cwchar>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kClassName[] = L"EvrRigTestApp";
constexpr wchar_t kTitle[] = L"EternalVR rig test app";
constexpr UINT_PTR kExitTimer = 1;

bool g_hang = false;

struct SilentAudio {
    HWAVEOUT device = nullptr;
    std::vector<short> samples;
    WAVEHDR header{};
};

SilentAudio g_audio;

// Mutes this process's default audio session on the default render device, as a mute set by the user
// in the Windows volume mixer would.
void MuteOwnSession() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        return;
    }
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioSessionManager* manager = nullptr;
    ISimpleAudioVolume* volume = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))) &&
        SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
        SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(&manager))) &&
        SUCCEEDED(manager->GetSimpleAudioVolume(nullptr, FALSE, &volume))) {
        volume->SetMute(TRUE, nullptr);
    }
    if (volume != nullptr) {
        volume->Release();
    }
    if (manager != nullptr) {
        manager->Release();
    }
    if (device != nullptr) {
        device->Release();
    }
    if (enumerator != nullptr) {
        enumerator->Release();
    }
}

void StartSilentAudio() {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 44100;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    if (waveOutOpen(&g_audio.device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_audio.device = nullptr;
        return;
    }
    g_audio.samples.assign(44100, 0);
    g_audio.header.lpData = reinterpret_cast<LPSTR>(g_audio.samples.data());
    g_audio.header.dwBufferLength = static_cast<DWORD>(g_audio.samples.size() * sizeof(short));
    waveOutPrepareHeader(g_audio.device, &g_audio.header, sizeof(WAVEHDR));
    // One second of silence looped (practically) forever keeps the stream, and with it the process's
    // audio session, alive until the program exits.
    g_audio.header.dwFlags |= WHDR_BEGINLOOP | WHDR_ENDLOOP;
    g_audio.header.dwLoops = 0xFFFFFFFF;
    waveOutWrite(g_audio.device, &g_audio.header, sizeof(WAVEHDR));
}

void StopSilentAudio() {
    if (g_audio.device == nullptr) {
        return;
    }
    waveOutReset(g_audio.device);
    waveOutUnprepareHeader(g_audio.device, &g_audio.header, sizeof(WAVEHDR));
    waveOutClose(g_audio.device);
    g_audio.device = nullptr;
}

LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CLOSE:
        if (g_hang) {
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_TIMER:
        if (wparam == kExitTimer) {
            KillTimer(window, kExitTimer);
            DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, msg, wparam, lparam);
    }
}

std::wstring Quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    for (wchar_t c : arg) {
        if (c == L'"') {
            out += L'\\';
        }
        out += c;
    }
    out += L'"';
    return out;
}

int HandOff(const std::vector<std::wstring>& args) {
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return 4;
    }
    std::wstring commandLine = Quote(path);
    for (const std::wstring& arg : args) {
        if (arg != L"--handoff") {
            commandLine += L" " + Quote(arg);
        }
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(path, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                        &info)) {
        return 4;
    }
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    std::vector<std::wstring> args;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
        LocalFree(argv);
    }

    bool visible = false;
    bool audio = false;
    bool audioMuted = false;
    bool handoff = false;
    UINT exitAfterMs = 0;
    DWORD slowExitMs = 0;
    for (const std::wstring& arg : args) {
        if (arg == L"--visible") {
            visible = true;
        } else if (arg == L"--hang") {
            g_hang = true;
        } else if (arg == L"--exit-now") {
            return 3;
        } else if (arg == L"--handoff") {
            handoff = true;
        } else if (arg == L"--audio") {
            audio = true;
        } else if (arg == L"--audio-muted") {
            audioMuted = true;
        } else if (arg.rfind(L"--exit-after=", 0) == 0) {
            exitAfterMs = static_cast<UINT>(std::wcstoul(arg.c_str() + 13, nullptr, 10));
        } else if (arg.rfind(L"--slow-exit=", 0) == 0) {
            slowExitMs = static_cast<DWORD>(std::wcstoul(arg.c_str() + 12, nullptr, 10));
        }
    }
    if (handoff) {
        return HandOff(args);
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return 1;
    }

    DWORD exStyle = 0;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (!visible) {
        exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        x = -30000;
        y = -30000;
    }
    HWND window = CreateWindowExW(exStyle, kClassName, kTitle, WS_OVERLAPPEDWINDOW, x, y, 320, 200, nullptr,
                                  nullptr, instance, nullptr);
    if (window == nullptr) {
        return 1;
    }
    ShowWindow(window, SW_SHOWNOACTIVATE);
    if (exitAfterMs > 0) {
        SetTimer(window, kExitTimer, exitAfterMs, nullptr);
    }
    if (audio) {
        // The mute comes first, so the session is already muted when anyone first sees it.
        if (audioMuted) {
            MuteOwnSession();
        }
        StartSilentAudio();
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    StopSilentAudio();
    if (slowExitMs > 0) {
        Sleep(slowExitMs);
    }
    return 0;
}
