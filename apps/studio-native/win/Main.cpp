// AIVERSE Studio, Windows shell: Win32 window + WebView2 UI + native Vulkan Scene / Game views (child windows
// over the DOM viewport rects, like the macOS Metal views). Everything that is not Win32 lives in
// apps/studio-native/StudioHost (shared with mac/Main.mm). The web UI is the same build as on macOS: a script
// injected at document creation gives it the window.webkit.messageHandlers.aiverse bridge it already uses.
// Engine headers first: <windows.h> defines near / far / small / interface as macros.
#include "apps/studio-native/StudioHost.h"
#include "apps/studio-native/StudioGameInput.h"
#include "apps/editor/core/BuildTool.h"
#include "engine/gfx/vulkan/Vulkan.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include "WebView2.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef AIVERSE_STUDIO_SOURCE_DIR
#define AIVERSE_STUDIO_SOURCE_DIR "."
#endif

using aiverse::Json;
namespace studio = aiverse::studio;
namespace fs = std::filesystem;

namespace {
// ---------------------------------------------------------------- small helpers
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string narrow(const wchar_t* w, int len = -1) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    if (len < 0 && !s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
// Strings WebView2 hands out are CoTaskMemAlloc'ed.
std::string takeString(LPWSTR w) { std::string s = narrow(w); CoTaskMemFree(w); return s; }
fs::path exeDir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}
fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = fs::path(p);
    CoTaskMemFree(p);
    return out;
}
fs::path studioDataDir() {
    fs::path dir = knownFolder(FOLDERID_LocalAppData);
    if (dir.empty()) dir = fs::temp_directory_path();
    dir /= L"AIVERSE";
    dir /= L"Studio";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}
void logLine(const std::string& line) {
    OutputDebugStringW(widen(line + "\n").c_str());
    std::fprintf(stderr, "%s\n", line.c_str());
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    std::ofstream(studioDataDir() / L"studio.log", std::ios::app) << line << "\n";
}

// Minimal owning COM pointer.
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset(T* v = nullptr) { if (p) p->Release(); p = v; }
    T** put() { reset(); return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};
// A WebView2 completion / event handler from a lambda. Invoke's signature comes from the template arguments,
// so it overrides the interface's pure virtual (no WRL: builds with MSVC and MinGW alike).
template <class I, class... Args> class Callback final : public I {
public:
    Callback(const IID& iid, std::function<HRESULT(Args...)> fn) : iid_(iid), fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid_)) { *out = static_cast<I*>(this); AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = --refs_; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_ ? fn_(args...) : S_OK; }
private:
    std::atomic<ULONG> refs_{1};
    const IID& iid_;
    std::function<HRESULT(Args...)> fn_;
};
template <class I, class... Args, class F> I* makeCallback(const IID& iid, F&& fn) {
    return new Callback<I, Args...>(iid, std::function<HRESULT(Args...)>(std::forward<F>(fn)));
}

constexpr UINT kRunTask = WM_APP + 1; // lParam: std::function<void()>* to run on the UI thread
constexpr UINT_PTR kTickTimer = 1;
constexpr wchar_t kMainClass[] = L"AiverseStudioWindow";
constexpr wchar_t kSurfaceClass[] = L"AiverseStudioSurface";
constexpr wchar_t kVirtualHost[] = L"studio.aiverse";
constexpr char kTrustedOrigin[] = "https://studio.aiverse/";
constexpr char kDevOrigin[] = "http://127.0.0.1:4173/";
// The web UI talks to window.webkit.messageHandlers.aiverse (WKWebView); WebView2 has chrome.webview.
constexpr wchar_t kBridgeShim[] =
    L"(function(){if(!window.chrome||!window.chrome.webview)return;"
    L"var h={postMessage:function(m){window.chrome.webview.postMessage(m);}};"
    L"window.webkit={messageHandlers:{aiverse:h}};"
    L"document.documentElement&&document.documentElement.classList.add('studio-windows');})();";

class WinShell;
WinShell* gShell = nullptr;

class WinShell final : public studio::StudioHostPlatform {
public:
    WinShell(std::string project, bool dev) : project_(std::move(project)), dev_(dev) {}

    int run() {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
            fatal("COM could not start");
            return 1;
        }
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = mainProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(14, 17, 23));
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.hIconSm = wc.hIcon;
        wc.lpszClassName = kMainClass;
        RegisterClassExW(&wc);
        WNDCLASSEXW sc{sizeof(sc)};
        sc.style = CS_OWNDC | CS_DBLCLKS;
        sc.lpfnWndProc = surfaceProc;
        sc.hInstance = instance;
        sc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        sc.lpszClassName = kSurfaceClass;
        RegisterClassExW(&sc);

        const UINT dpi = GetDpiForSystem();
        RECT r{0, 0, MulDiv(1440, int(dpi), 96), MulDiv(900, int(dpi), 96)};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
        main_ = CreateWindowExW(0, kMainClass, L"AIVERSE", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, nullptr);
        if (!main_) { fatal("The main window could not be created"); return 1; }
        for (int i = 0; i < 2; ++i) {
            surfaces_[i] = CreateWindowExW(0, kSurfaceClass, nullptr, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 16, 16,
                                           main_, nullptr, instance, nullptr);
            SetWindowLongPtrW(surfaces_[i], GWLP_USERDATA, LONG_PTR(i));
        }

        const std::string root = AIVERSE_STUDIO_SOURCE_DIR;
        core_ = std::make_unique<studio::StudioHost>(*this, root, false);
        const std::string exe = exeDir().string();
#ifdef AIVERSE_STUDIO_BUILD_TOOL
        core_->session().setBuildTool(AIVERSE_STUDIO_BUILD_TOOL);
#else
        core_->session().setBuildTool(aiverse::editor::findBuildTool(exe, root));
#endif
#ifdef AIVERSE_STUDIO_MCP_TOOL
        core_->session().setMcpTool(AIVERSE_STUDIO_MCP_TOOL);
#else
        core_->session().setMcpTool(exe + "\\aiversemcp.exe");
#endif
        if (project_.empty()) project_ = lastProject();
        else rememberLastProject(project_);
        core_->setPendingProject(project_);

        ShowWindow(main_, SW_SHOWDEFAULT);
        UpdateWindow(main_);
        if (!startWebView()) return 1;
        SetTimer(main_, kTickTimer, 15, nullptr); // ~60 Hz; WM_TIMER also runs inside modal move/resize loops

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        CoUninitialize();
        return int(msg.wParam);
    }

    // ------------------------------------------------------------ StudioHostPlatform
    void evaluateScript(const std::string& js, bool currentOnly) override {
        (void)currentOnly; // one web view: the main window's
        if (web_) web_->ExecuteScript(widen(js).c_str(), nullptr);
    }
    std::unique_ptr<aiverse::gfx::Device> createDevice(studio::StudioSurface surface) override {
        aiverse::gfx::VulkanWindow window;
        window.kind = aiverse::gfx::VulkanWindow::Kind::Win32;
        window.display = GetModuleHandleW(nullptr);
        window.window = uint64_t(uintptr_t(surfaces_[index(surface)]));
        std::string error;
        auto device = aiverse::gfx::createVulkanDevice(window, &error);
        if (!device) throw std::runtime_error("Vulkan is required for the Studio views: " + (error.empty() ? std::string("no device") : error));
        return device;
    }
    bool surfaceDrawable(studio::StudioSurface surface) override {
        return IsWindowVisible(surfaces_[index(surface)]) && !IsIconic(main_);
    }
    bool gameFocused() override {
        return GetForegroundWindow() == main_ && GetFocus() == surfaces_[1];
    }
    // The DOM rect is in CSS pixels (= DIPs, the web view is not zoomed); the child window takes physical pixels.
    bool placeSurface(studio::StudioSurface surface, const studio::ViewportRect& rect, studio::NativeViewportPlacement& out) override {
        HWND h = surfaces_[index(surface)];
        RECT client{};
        GetClientRect(main_, &client);
        const double scale = double(GetDpiForWindow(main_)) / 96.0;
        const double hostW = double(client.right) / scale, hostH = double(client.bottom) / scale;
        out = studio::viewportPlacement(rect, hostW, hostH, scale);
        if (!out.visible) {
            if (IsWindowVisible(h)) ShowWindow(h, SW_HIDE);
            return true;
        }
        const int left = int(std::lround(out.x * scale));
        const int top = int(std::lround((hostH - out.y - out.height) * scale));
        RECT now{};
        GetWindowRect(h, &now);
        MapWindowPoints(HWND_DESKTOP, main_, reinterpret_cast<POINT*>(&now), 2);
        const bool moved = now.left != left || now.top != top || now.right - now.left != out.pixelsW || now.bottom - now.top != out.pixelsH;
        // Above the WebView2 window (a sibling child): the native view covers its DOM rect.
        SetWindowPos(h, HWND_TOP, left, top, out.pixelsW, out.pixelsH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW | (moved ? 0 : SWP_NOMOVE | SWP_NOSIZE));
        return true;
    }
    double now() override {
        static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return double(t.QuadPart) / double(freq.QuadPart);
    }
    void runInBackground(std::function<void()> work, std::function<void()> then) override {
        HWND target = main_;
        std::thread([work = std::move(work), then = std::move(then), target]() mutable {
            work();
            postTask(target, std::move(then));
        }).detach();
    }
    bool confirmDiscardChanges(const std::string& title, const std::string& message, const std::string& discardLabel) override {
        const std::wstring text = widen(message + "\n\nChoose Yes to " + discardLabel + ".");
        return MessageBoxW(main_, text.c_str(), widen(title).c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
    }
    // Native dialogs run after the current bridge message returns (no re-entrant web messages during them).
    void chooseProject(std::function<void(std::string)> done) override {
        later([this, done = std::move(done)] { done(pickFolder(L"Open Project", {})); });
    }
    void chooseFolder(const std::string& message, const std::string& startDir, std::function<void(std::string)> done) override {
        later([this, message, startDir, done = std::move(done)] { done(pickFolder(widen(message), startDir)); });
    }
    void chooseSaveFile(const std::string& startDir, const std::string& defaultName, std::function<void(std::string)> done) override {
        later([this, startDir, defaultName, done = std::move(done)] {
            std::string result;
            Com<IFileSaveDialog> dialog;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog, reinterpret_cast<void**>(dialog.put())))) {
                const COMDLG_FILTERSPEC filter[] = {{L"Map (*.json)", L"*.json"}};
                dialog->SetFileTypes(1, filter);
                dialog->SetDefaultExtension(L"json");
                dialog->SetFileName(widen(defaultName).c_str());
                setFolder(dialog.p, startDir);
                if (SUCCEEDED(dialog->Show(main_))) result = resultPath(dialog.p);
            }
            done(result);
        });
    }
    void chooseImportSources(const std::string& startDir, std::function<void(std::vector<fs::path>)> done) override {
        later([this, startDir, done = std::move(done)] {
            std::vector<fs::path> sources;
            Com<IFileOpenDialog> dialog;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(dialog.put())))) {
                DWORD options = 0;
                dialog->GetOptions(&options);
                dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
                dialog->SetTitle(L"Import into content");
                setFolder(dialog.p, startDir);
                Com<IShellItemArray> items;
                if (SUCCEEDED(dialog->Show(main_)) && SUCCEEDED(dialog->GetResults(items.put()))) {
                    DWORD count = 0;
                    items->GetCount(&count);
                    for (DWORD i = 0; i < count && i < 65; ++i) {
                        Com<IShellItem> item;
                        PWSTR path = nullptr;
                        if (SUCCEEDED(items->GetItemAt(i, item.put())) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                            sources.emplace_back(narrow(path));
                        CoTaskMemFree(path);
                    }
                }
            }
            done(std::move(sources));
        });
    }
    void revealInFileManager(const fs::path& path) override {
        const std::wstring args = L"/select,\"" + path.wstring() + L"\"";
        ShellExecuteW(main_, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
    fs::path cacheDir() override {
        fs::path dir = knownFolder(FOLDERID_LocalAppData);
        return dir.empty() ? fs::temp_directory_path() : dir;
    }
    void rememberLastProject(const std::string& dir) override {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\AIVERSE\\Studio", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
        const std::wstring value = widen(dir);
        RegSetValueExW(key, L"LastProject", 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
    Json windowRequest(const std::string& method, const Json& params) override {
        (void)params;
        Json result = Json::object();
        if (method == "studio.windowDrag") {
            // Title-bar drag from HTML: the system moves the window while the button is held.
            ReleaseCapture();
            SendMessageW(main_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return result;
        }
        if (method == "studio.windowZoom") { ShowWindow(main_, IsZoomed(main_) ? SW_RESTORE : SW_MAXIMIZE); return result; }
        // Floating panel windows are macOS-only for now: the UI keeps the tab docked.
        if (method == "studio.windowOpen") { result.set("opened", Json::fromBool(false)); return result; }
        if (method == "studio.windowClose") { result.set("closed", Json::fromBool(false)); return result; }
        throw std::runtime_error("Floating windows are not available on Windows yet");
    }
    void log(const std::string& message) override { logLine(message); }

    // ------------------------------------------------------------ window procedures
    static LRESULT CALLBACK mainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        WinShell* s = gShell;
        if (!s) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
        case WM_TIMER:
            if (wp == kTickTimer && s->core_) s->core_->tick();
            return 0;
        case kRunTask: {
            std::unique_ptr<std::function<void()>> task(reinterpret_cast<std::function<void()>*>(lp));
            if (task && *task) (*task)();
            return 0;
        }
        case WM_SIZE:
            s->resizeWeb();
            return 0;
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_MOVE:
        case WM_MOVING:
            if (s->controller_) s->controller_->NotifyParentWindowPositionChanged();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && s->core_) s->core_->gameSuspend();
            break;
        case WM_CLOSE:
            if (s->core_ && !s->core_->confirmClose()) return 0;
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            KillTimer(h, kTickTimer);
            // Hosted game and Scene release their GPU devices before the child windows go away.
            if (s->core_) s->core_->shutdown();
            if (s->controller_) s->controller_->Close();
            s->web_.reset();
            s->controller_.reset(); // COM objects go before CoUninitialize
            PostQuitMessage(0);
            return 0;
        default: break;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
    static LRESULT CALLBACK surfaceProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        WinShell* s = gShell;
        if (s && s->core_) {
            const int which = int(GetWindowLongPtrW(h, GWLP_USERDATA));
            const LRESULT handled = which == 0 ? s->sceneInput(h, msg, wp, lp) : s->gameInput(h, msg, wp, lp);
            if (handled != LRESULT(-1)) return handled;
        }
        switch (msg) {
        case WM_ERASEBKGND: return 1; // the GPU paints every pixel
        case WM_PAINT: ValidateRect(h, nullptr); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        default: return DefWindowProcW(h, msg, wp, lp);
        }
    }

private:
    static int index(studio::StudioSurface s) { return s == studio::StudioSurface::Scene ? 0 : 1; }
    static void postTask(HWND target, std::function<void()> fn) {
        auto* task = new std::function<void()>(std::move(fn));
        if (!target || !PostMessageW(target, kRunTask, 0, LPARAM(task))) delete task;
    }
    void later(std::function<void()> fn) { postTask(main_, std::move(fn)); }
    void fatal(const std::string& message) {
        logLine("AIVERSE Studio: " + message);
        MessageBoxW(main_, widen(message).c_str(), L"AIVERSE Studio", MB_OK | MB_ICONERROR);
    }
    double scale() const { return double(GetDpiForWindow(main_)) / 96.0; }

    std::string lastProject() {
        wchar_t buf[4096];
        DWORD size = sizeof(buf);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\AIVERSE\\Studio", L"LastProject", RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
            return {};
        std::error_code ec;
        return fs::is_directory(fs::path(buf), ec) ? narrow(buf) : std::string();
    }
    void setFolder(IFileDialog* dialog, const std::string& dir) {
        if (dir.empty()) return;
        Com<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(widen(dir).c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(folder.put()))))
            dialog->SetFolder(folder.p);
    }
    std::string resultPath(IFileDialog* dialog) {
        Com<IShellItem> item;
        PWSTR path = nullptr;
        std::string out;
        if (SUCCEEDED(dialog->GetResult(item.put())) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) out = narrow(path);
        CoTaskMemFree(path);
        return out;
    }
    std::string pickFolder(const std::wstring& title, const std::string& startDir) {
        Com<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(dialog.put()))))
            return {};
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(title.c_str());
        setFolder(dialog.p, startDir);
        if (FAILED(dialog->Show(main_))) return {};
        return resultPath(dialog.p);
    }

    // ------------------------------------------------------------ WebView2
    fs::path webRoot() const {
        const fs::path bundled = exeDir() / L"studio-web";
        std::error_code ec;
        if (fs::is_regular_file(bundled / L"index.html", ec)) return bundled;
        return fs::path(AIVERSE_STUDIO_SOURCE_DIR) / "apps" / "studio-web" / "dist";
    }
    bool startWebView() {
        using CreateEnv = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                   ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
        HMODULE loader = LoadLibraryW((exeDir() / L"WebView2Loader.dll").c_str());
        if (!loader) loader = LoadLibraryW(L"WebView2Loader.dll");
        auto create = loader ? reinterpret_cast<CreateEnv>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
        if (!create) { fatal("WebView2Loader.dll is missing next to AiverseStudio.exe"); return false; }
        const std::wstring userData = (studioDataDir() / L"WebView2").wstring();
        auto* envDone = makeCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
            IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) {
                    fatal("Microsoft Edge WebView2 Runtime is not installed.\nInstall it from https://go.microsoft.com/fwlink/p/?LinkId=2124703");
                    PostQuitMessage(1);
                    return S_OK;
                }
                auto* controllerDone = makeCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                    IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                        if (FAILED(result) || !controller) { fatal("The WebView2 control could not be created"); PostQuitMessage(1); return S_OK; }
                        onController(controller);
                        return S_OK;
                    });
                env->CreateCoreWebView2Controller(main_, controllerDone);
                controllerDone->Release();
                return S_OK;
            });
        const HRESULT hr = create(nullptr, userData.c_str(), nullptr, envDone);
        envDone->Release();
        if (FAILED(hr)) {
            fatal("Microsoft Edge WebView2 Runtime is not available.\nInstall it from https://go.microsoft.com/fwlink/p/?LinkId=2124703");
            return false;
        }
        return true;
    }
    void onController(ICoreWebView2Controller* controller) {
        controller->AddRef();
        controller_.reset(controller);
        controller_->get_CoreWebView2(web_.put());
        {
            Com<ICoreWebView2Controller2> c2;
            if (SUCCEEDED(controller_->QueryInterface(IID_ICoreWebView2Controller2, reinterpret_cast<void**>(c2.put())))) {
                COREWEBVIEW2_COLOR dark{255, 14, 17, 23};
                c2->put_DefaultBackgroundColor(dark);
            }
            Com<ICoreWebView2Controller4> c4;
            // Files dropped from Explorer would navigate the UI away; Assets import goes through the dialog.
            if (SUCCEEDED(controller_->QueryInterface(IID_ICoreWebView2Controller4, reinterpret_cast<void**>(c4.put())))) c4->put_AllowExternalDrop(FALSE);
        }
        Com<ICoreWebView2Settings> settings;
        if (SUCCEEDED(web_->get_Settings(settings.put()))) {
            settings->put_AreDevToolsEnabled(dev_ ? TRUE : FALSE);
            settings->put_AreDefaultContextMenusEnabled(dev_ ? TRUE : FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
            settings->put_AreHostObjectsAllowed(FALSE);
        }
        web_->AddScriptToExecuteOnDocumentCreated(kBridgeShim, nullptr);
        // The built UI is served from its folder under a private host name (ES modules need a real origin).
        Com<ICoreWebView2_3> web3;
        const fs::path root = webRoot();
        if (!dev_) {
            std::error_code ec;
            if (!fs::is_regular_file(root / L"index.html", ec)) { fatal("The Studio UI is missing: " + root.string() + "\\index.html"); }
            if (FAILED(web_->QueryInterface(IID_ICoreWebView2_3, reinterpret_cast<void**>(web3.put())))) {
                fatal("This WebView2 Runtime is too old; update Microsoft Edge WebView2");
                PostQuitMessage(1);
                return;
            }
            web3->SetVirtualHostNameToFolderMapping(kVirtualHost, root.wstring().c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        }
        EventRegistrationToken token{};
        web_->add_WebMessageReceived(makeCallback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
            IID_ICoreWebView2WebMessageReceivedEventHandler, [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR source = nullptr, json = nullptr;
                if (FAILED(args->get_Source(&source))) return S_OK;
                const std::string origin = takeString(source);
                if (!trusted(origin)) return S_OK;
                if (FAILED(args->get_WebMessageAsJson(&json))) return S_OK;
                const std::string body = takeString(json);
                if (core_ && body.size() <= 1024 * 1024) core_->handleMessage(body);
                return S_OK;
            }), &token);
        web_->add_NavigationStarting(makeCallback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
            IID_ICoreWebView2NavigationStartingEventHandler, [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && !trusted(takeString(uri))) args->put_Cancel(TRUE);
                return S_OK;
            }), &token);
        web_->add_NewWindowRequested(makeCallback<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
            IID_ICoreWebView2NewWindowRequestedEventHandler, [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE); // no pop-up windows from the UI
                return S_OK;
            }), &token);
        web_->add_NavigationCompleted(makeCallback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
            IID_ICoreWebView2NavigationCompletedEventHandler, [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL ok = FALSE;
                args->get_IsSuccess(&ok);
                logLine(ok ? "[studio-web] navigation finished" : "[studio-web] navigation failed");
                if (ok && core_) core_->webReady();
                return S_OK;
            }), &token);
        web_->add_ProcessFailed(makeCallback<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*>(
            IID_ICoreWebView2ProcessFailedEventHandler, [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                logLine("[studio-web] WebView2 process failed");
                return S_OK;
            }), &token);
        resizeWeb();
        controller_->put_IsVisible(TRUE);
        const std::wstring url = dev_ ? widen(kDevOrigin) : std::wstring(L"https://") + kVirtualHost + L"/index.html";
        logLine("[studio-web] loading " + narrow(url.c_str()));
        web_->Navigate(url.c_str());
    }
    bool trusted(const std::string& uri) const {
        // Only the bundled UI (or the dev server with --dev) may talk to the host or be navigated to.
        auto starts = [&](const char* prefix) { return uri.rfind(prefix, 0) == 0; };
        if (starts(kTrustedOrigin)) return true;
        return dev_ && starts(kDevOrigin);
    }
    void resizeWeb() {
        if (!controller_) return;
        RECT r{};
        GetClientRect(main_, &r);
        controller_->put_Bounds(r);
    }

    // ------------------------------------------------------------ input
    studio::StudioSceneMods mods() const {
        studio::StudioSceneMods m;
        m.shift = GetKeyState(VK_SHIFT) < 0;
        m.ctrl = GetKeyState(VK_CONTROL) < 0;
        m.alt = GetKeyState(VK_MENU) < 0;
        m.command = m.ctrl; // Windows: Ctrl plays the role of Cmd (toggle select, duplicate, pan with Alt)
        return m;
    }
    void viewSize(HWND h, double& w, double& hgt) const {
        RECT r{};
        GetClientRect(h, &r);
        const double k = scale();
        w = double(r.right) / k;
        hgt = double(r.bottom) / k;
    }
    // Returns -1 when the message is not the Scene's.
    LRESULT sceneInput(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        const double k = scale();
        double w = 0, hh = 0;
        viewSize(h, w, hh);
        auto at = [&](double& x, double& y) { x = GET_X_LPARAM(lp) / k; y = GET_Y_LPARAM(lp) / k; };
        int button = -1, phase = -1;
        switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: button = 0; phase = 0; break;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: button = 1; phase = 0; break;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: button = 2; phase = 0; break;
        case WM_LBUTTONUP: button = 0; phase = 2; break;
        case WM_RBUTTONUP: button = 1; phase = 2; break;
        case WM_MBUTTONUP: button = 2; phase = 2; break;
        case WM_MOUSEMOVE: button = captured_ >= 0 ? captured_ : 0; phase = 1; break;
        default: break;
        }
        if (phase >= 0) {
            double x = 0, y = 0;
            at(x, y);
            if (phase == 0) {
                SetFocus(h);
                SetCapture(h);
                captured_ = button;
                last_ = {x, y};
            } else if (phase == 2) {
                if (button != captured_) return 0;
                captured_ = -1;
                ReleaseCapture();
            } else if (!tracking_) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
                tracking_ = TrackMouseEvent(&t) != FALSE;
            }
            const double dx = x - last_.first, dy = y - last_.second;
            last_ = {x, y};
            core_->sceneMouse(phase, button, x, y, dx, dy, w, hh, mods());
            return 0;
        }
        switch (msg) {
        case WM_MOUSELEAVE:
            tracking_ = false;
            if (captured_ < 0) core_->hover(-1, -1, w, hh);
            return 0;
        case WM_CAPTURECHANGED:
            if (captured_ >= 0 && HWND(lp) != h) { // capture taken away mid-drag: finish it
                const int b = captured_;
                captured_ = -1;
                core_->sceneMouse(2, b, last_.first, last_.second, 0, 0, w, hh, mods());
            }
            return 0;
        case WM_MOUSEWHEEL:
            core_->sceneWheel(double(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA);
            return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: {
            using K = studio::StudioSceneKey;
            K key = K::Other;
            switch (wp) {
            case 'A': key = K::A; break; case 'D': key = K::D; break; case 'E': key = K::E; break; case 'F': key = K::F; break;
            case 'Q': key = K::Q; break; case 'R': key = K::R; break; case 'S': key = K::S; break; case 'V': key = K::V; break;
            case 'W': key = K::W; break; case VK_LEFT: key = K::Left; break; case VK_RIGHT: key = K::Right; break;
            case VK_UP: key = K::Up; break; case VK_DOWN: key = K::Down; break; case VK_HOME: key = K::Home; break;
            case VK_DELETE: case VK_BACK: key = K::Delete; break; case VK_ESCAPE: key = K::Escape; break;
            case VK_OEM_4: key = K::BracketLeft; break; case VK_OEM_6: key = K::BracketRight; break;
            default: break;
            }
            const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            if (key != K::Other && core_->sceneKey(key, down, mods(), GetKeyState(VK_RBUTTON) < 0)) return 0;
            return -1;
        }
        default: return -1;
        }
    }
    static aiverse::platform::Key gameKey(WPARAM vk) {
        using aiverse::platform::Key;
        if (vk >= 'A' && vk <= 'Z') return Key(int(Key::A) + int(vk - 'A'));
        if (vk >= '0' && vk <= '9') return Key(int(Key::Num0) + int(vk - '0'));
        if (vk >= VK_F1 && vk <= VK_F12) return Key(int(Key::F1) + int(vk - VK_F1));
        switch (vk) {
        case VK_ESCAPE: return Key::Escape; case VK_RETURN: return Key::Enter; case VK_TAB: return Key::Tab;
        case VK_BACK: return Key::Backspace; case VK_DELETE: return Key::Delete; case VK_INSERT: return Key::Insert;
        case VK_SPACE: return Key::Space; case VK_LEFT: return Key::Left; case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up; case VK_DOWN: return Key::Down; case VK_HOME: return Key::Home; case VK_END: return Key::End;
        case VK_PRIOR: return Key::PageUp; case VK_NEXT: return Key::PageDown;
        case VK_SHIFT: return Key::LeftShift; case VK_CONTROL: return Key::LeftCtrl; case VK_MENU: return Key::LeftAlt;
        case VK_LWIN: return Key::LeftSuper; case VK_RWIN: return Key::RightSuper;
        case VK_OEM_MINUS: return Key::Minus; case VK_OEM_PLUS: return Key::Equal; case VK_OEM_COMMA: return Key::Comma;
        case VK_OEM_PERIOD: return Key::Period; case VK_OEM_2: return Key::Slash; case VK_OEM_1: return Key::Semicolon;
        case VK_OEM_4: return Key::LeftBracket; case VK_OEM_6: return Key::RightBracket; case VK_OEM_7: return Key::Apostrophe;
        case VK_OEM_5: return Key::Backslash; case VK_OEM_3: return Key::GraveAccent;
        default: return Key::Unknown;
        }
    }
    static uint8_t gameMods() {
        using namespace aiverse::platform;
        uint8_t m = 0;
        if (GetKeyState(VK_SHIFT) < 0) m |= kModShift;
        if (GetKeyState(VK_CONTROL) < 0) m |= kModCtrl;
        if (GetKeyState(VK_MENU) < 0) m |= kModAlt;
        if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) m |= kModSuper;
        return m;
    }
    LRESULT gameInput(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        using namespace aiverse::platform;
        const double k = scale();
        Event e;
        e.mods = gameMods();
        auto mouse = [&](EventType type, int button) {
            e.type = type;
            e.button = button;
            e.x = float(GET_X_LPARAM(lp) / k);
            e.y = float(GET_Y_LPARAM(lp) / k);
            e.deltaX = e.x - float(lastGame_.first);
            e.deltaY = e.y - float(lastGame_.second);
            lastGame_ = {e.x, e.y};
            if (type == EventType::MouseDown) { SetFocus(h); SetCapture(h); }
            if (type == EventType::MouseUp && !(wp & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) ReleaseCapture();
            core_->gameEvent(e);
            return LRESULT(0);
        };
        switch (msg) {
        case WM_LBUTTONDOWN: return mouse(EventType::MouseDown, 0);
        case WM_RBUTTONDOWN: return mouse(EventType::MouseDown, 1);
        case WM_MBUTTONDOWN: return mouse(EventType::MouseDown, 2);
        case WM_LBUTTONUP: return mouse(EventType::MouseUp, 0);
        case WM_RBUTTONUP: return mouse(EventType::MouseUp, 1);
        case WM_MBUTTONUP: return mouse(EventType::MouseUp, 2);
        case WM_MOUSEMOVE: return mouse(EventType::MouseMove, 0);
        case WM_MOUSEWHEEL:
            e.type = EventType::MouseWheel;
            e.x = float(lastGame_.first);
            e.y = float(lastGame_.second);
            e.wheelY = float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            core_->gameEvent(e);
            return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
            if (msg == WM_SYSKEYDOWN && wp == VK_F4) return -1; // keep Alt+F4
            e.type = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) ? EventType::KeyDown : EventType::KeyUp;
            e.key = gameKey(wp);
            e.repeat = (lp & (1 << 30)) != 0 && e.type == EventType::KeyDown;
            core_->gameEvent(e);
            return 0;
        case WM_CHAR: {
            uint32_t c = uint32_t(wp);
            if (c >= 0xd800 && c <= 0xdbff) { highSurrogate_ = c; return 0; }
            if (c >= 0xdc00 && c <= 0xdfff && highSurrogate_) { c = 0x10000 + ((highSurrogate_ - 0xd800) << 10) + (c - 0xdc00); highSurrogate_ = 0; }
            if (c >= 32 && !(e.mods & (kModCtrl | kModSuper))) { e.type = EventType::Char; e.codepoint = c; core_->gameEvent(e); }
            return 0;
        }
        case WM_KILLFOCUS:
            e.type = EventType::FocusLost;
            core_->gameEvent(e);
            return -1;
        default: return -1;
        }
    }

    std::string project_;
    bool dev_ = false;
    HWND main_ = nullptr;
    HWND surfaces_[2] = {nullptr, nullptr}; // Scene, Game
    Com<ICoreWebView2Controller> controller_;
    Com<ICoreWebView2> web_;
    std::unique_ptr<studio::StudioHost> core_;
    int captured_ = -1;
    bool tracking_ = false;
    std::pair<double, double> last_{0, 0}, lastGame_{0, 0};
    uint32_t highSurrogate_ = 0;
};
} // namespace

// PlatformWin32.cpp (aiverse_runtime) owns WinMain / main and forwards to aiverseMain().
int aiverseMain(int argc, char** argv) {
    // Per-monitor DPI (the manifest asks for it too; this covers a missing manifest).
    if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
        using SetContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto set = reinterpret_cast<SetContext>(reinterpret_cast<void*>(GetProcAddress(user, "SetProcessDpiAwarenessContext"))))
            set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    std::string project;
    bool dev = false;
    // The UTF-8 code page manifest makes argv UTF-8; read the wide command line anyway (older Windows).
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    for (int i = 1; i < wargc; ++i) {
        const std::wstring arg = wargv[i];
        if (arg == L"--dev") dev = true;
        else if (arg == L"--project" && i + 1 < wargc) project = narrow(wargv[++i]);
        else {
            LocalFree(wargv);
            MessageBoxW(nullptr, L"Usage: AiverseStudio.exe [--dev] [--project <folder>]", L"AIVERSE Studio", MB_OK);
            return 2;
        }
    }
    LocalFree(wargv);
    (void)argc; (void)argv;
    try {
        WinShell shell(project, dev);
        gShell = &shell;
        const int code = shell.run();
        gShell = nullptr;
        return code;
    } catch (const std::exception& e) {
        logLine(std::string("Studio startup failed: ") + e.what());
        MessageBoxW(nullptr, widen(e.what()).c_str(), L"AIVERSE Studio", MB_OK | MB_ICONERROR);
        return 1;
    }
}
