#include "Application.h"
#include "core/PortablePaths.h"
#include "platform/SingleInstance.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <exception>
#include <iostream>

namespace {
int portableAvmMain() {
    SDL_SetMainReady();
    try {
        const auto paths = pavm::PortablePaths::discover();
        paths.ensureLayout();
        pavm::SingleInstance instance(paths.locks / "PortableAVM.instance.lock");
        if (!instance.acquired()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "PortableAVM",
                                     "PortableAVM이 이미 실행 중입니다.", nullptr);
            return 2;
        }
        pavm::Application application;
        return application.run();
    } catch (const std::exception& error) {
        std::cerr << "PortableAVM fatal error: " << error.what() << std::endl;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PortableAVM 오류", error.what(), nullptr);
        return 1;
    }
}
} // namespace

#ifdef _WIN32
#include <windows.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return portableAvmMain();
}
#else
int main(int, char**) {
    return portableAvmMain();
}
#endif
