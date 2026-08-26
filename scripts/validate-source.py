#!/usr/bin/env python3
"""Static source-tree policy checks for PortableAVM.

This is intentionally dependency-free so both build entry points can execute it
before downloading or compiling third-party code.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

EXCLUDED_DIRS = {
    ".git", ".deps", ".syntax-deps", "build", "build-core", "build-linux",
    "build-windows", "dist", "out", "cmake-build-debug", "cmake-build-release",
    "__pycache__", ".idea", ".vscode",
}

BUNDLED_RUNTIME_SUFFIXES = {
    ".apk", ".aab", ".apks", ".obb", ".xapk",
    ".img", ".iso", ".qcow", ".qcow2", ".vdi", ".vmdk",
    ".exe", ".dll", ".pdb", ".lib", ".so", ".dylib", ".a",
    ".jks", ".keystore", ".pem", ".p12", ".pfx",
    ".zip", ".7z", ".rar", ".tgz", ".gz", ".bz2", ".xz",
}

QT_PATTERNS = {
    "Qt CMake package": re.compile(r"find_package\s*\(\s*Qt[56]", re.I),
    "Qt target": re.compile(r"\bQt[56]::"),
    "Qt include": re.compile(r"#\s*include\s*[<\"]Q(?:Application|Widget|Window|Object|String|Process|Network|Json|File|Dir|Settings|Url|Thread|Timer|Dialog|MainWindow)"),
    "Qt deployment": re.compile(r"\bwindeployqt\b", re.I),
}

UNSAFE_BUILD_PATTERNS = {
    "automatic Android license acceptance": re.compile(
        r"(?:echo\s+[yY]|yes(?:\.exe)?)\s*(?:\||>)\s*[^\r\n]*sdkmanager[^\r\n]*--licenses",
        re.I,
    ),
    "persistent environment mutation": re.compile(r"\bsetx\b|EnvironmentVariableTarget\s*::\s*(?:Machine|User)", re.I),
    "Windows registry environment mutation": re.compile(r"HK(?:CU|LM)\\[^\r\n]*(?:Environment|Session Manager)", re.I),
}

REQUIRED = [
    "CMakeLists.txt",
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "LEGAL.md",
    "README.ko.md",
    "src/main.cpp",
    "src/Application.cpp",
    "src/ui/SkiaRenderer.cpp",
    "src/ui/ImmediateUi.cpp",
    "src/core/PortablePaths.cpp",
    "src/core/LanguageManager.cpp",
    "src/core/LanguageManager.h",
    "resources/Data/lang/ko.lang",
    "resources/Data/lang/en.lang",
    "src/core/SdkManager.cpp",
    "src/core/AvdManager.cpp",
    "src/core/ApkInspector.cpp",
    "build-windows.bat",
    "scripts/build-windows.bat",
    "scripts/build-windows.ps1",
    "scripts/build-linux.sh",
]

IDENTITY_KEYS = [
    "ro.build.fingerprint",
    "ro.product.model",
    "ro.product.brand",
    "ro.product.manufacturer",
    "ro.boot.verifiedbootstate",
    "ro.boot.vbmeta",
    "ro.build.tags",
    "ro.build.type",
]


def manifest_paths(root: Path) -> set[str] | None:
    """Return the active packaged source paths when a manifest is present.

    PortableAVM is commonly upgraded by extracting a newer source archive over an
    existing working tree. Files removed by a newer release can therefore remain
    on disk. They are not part of the active package and must not make validation
    fail merely because they are stale overlay leftovers.
    """
    manifest = root / "SOURCE_MANIFEST.sha256"
    if not manifest.is_file():
        return None
    paths: set[str] = set()
    for raw in manifest.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        rel = parts[1].lstrip("* ").replace("\\", "/")
        if rel and rel != "SOURCE_MANIFEST.sha256":
            paths.add(rel)
    return paths


def iter_files(root: Path):
    active = manifest_paths(root)
    if active is not None:
        for rel_s in sorted(active):
            rel = Path(rel_s)
            path = root / rel
            if not path.is_file():
                continue
            if any(part in EXCLUDED_DIRS for part in rel.parts):
                continue
            yield path, rel
        return

    for path in root.rglob("*"):
        if not path.is_file():
            continue
        try:
            rel = path.relative_to(root)
        except ValueError:
            continue
        if any(part in EXCLUDED_DIRS for part in rel.parts):
            continue
        yield path, rel


def text_or_none(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return None



def check_sdl3_main_handling(root: Path, errors: list[str]) -> None:
    main_cpp = (root / "src" / "main.cpp").read_text(encoding="utf-8")
    app_cpp = (root / "src" / "Application.cpp").read_text(encoding="utf-8")
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    if "#include <SDL3/SDL_main.h>" not in main_cpp:
        errors.append("SDL3 SDL_main.h regression: src/main.cpp must include <SDL3/SDL_main.h>.")
    if "SDL_SetMainReady();" not in main_cpp:
        errors.append("SDL3 SDL_main.h regression: SDL_SetMainReady() must be called from src/main.cpp when SDL_MAIN_HANDLED is used.")
    if "SDL_SetMainReady();" in app_cpp:
        errors.append("SDL3 SDL_main.h regression: SDL_SetMainReady() must not be hidden in Application.cpp.")
    if "#define SDL_MAIN_HANDLED" in main_cpp:
        errors.append("SDL3 SDL_main.h regression: SDL_MAIN_HANDLED is already supplied by CMake and must not be redefined in main.cpp.")
    if "SDL_MAIN_HANDLED" not in cmake:
        errors.append("SDL3 SDL_main.h regression: CMake must define SDL_MAIN_HANDLED for the PortableAVM target.")

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--report", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    failures: list[str] = []
    warnings: list[str] = []
    checked_files = 0

    active_manifest = manifest_paths(root)
    if active_manifest is not None:
        missing_manifest_files = sorted(rel for rel in active_manifest if not (root / rel).is_file())
        for rel in missing_manifest_files:
            failures.append(f"manifest-listed source file is missing: {rel}")

    for required in REQUIRED:
        if not (root / required).is_file():
            failures.append(f"missing required file: {required}")

    source_texts: dict[str, str] = {}
    for path, rel in iter_files(root):
        checked_files += 1
        rel_s = rel.as_posix()
        suffix = path.suffix.lower()

        if suffix == ".ps1":
            # In expandable PowerShell strings, `$name:` is parsed as a scoped
            # variable reference. Ordinary variables must be delimited as
            # `${name}:` when immediately followed by a colon. Environment and
            # other scoped variables such as `$env:PATH` are valid and excluded.
            ps_ambiguous_var_colon = re.compile(
                r"\$(?!(?:env|global|script|local|private|using):)([A-Za-z_][A-Za-z0-9_]*)\:"
            )
            for line_no, line in enumerate(text_or_none(path).splitlines(), 1) if text_or_none(path) is not None else []:
                if ps_ambiguous_var_colon.search(line):
                    failures.append(
                        f"PowerShell ambiguous variable before colon at {rel_s}:{line_no}; use ${{name}}:"
                    )

        if suffix in {".bat", ".cmd"}:
            raw = path.read_bytes()
            # cmd.exe label scanning is unreliable with Unix-LF batch files.
            # Every LF in shipped Windows command files must therefore be CRLF.
            if b"\n" in raw and b"\r\n" not in raw:
                failures.append(f"Windows batch file uses LF-only line endings: {rel_s}")
            elif any(raw[i] == 0x0A and (i == 0 or raw[i - 1] != 0x0D) for i in range(len(raw))):
                failures.append(f"Windows batch file contains a bare LF line ending: {rel_s}")

        if suffix in BUNDLED_RUNTIME_SUFFIXES:
            failures.append(f"runtime/binary/archive payload must not be bundled in source: {rel_s}")

        text = text_or_none(path)
        if text is None:
            continue
        source_texts[rel_s] = text

        # Qt is prohibited in build/code/scripts. Documentation may mention the
        # migration for clarity without introducing a dependency.
        if rel.parts[0] in {"src", "cmake", "scripts", "tests"} or rel_s == "CMakeLists.txt":
            for label, pattern in QT_PATTERNS.items():
                if pattern.search(text):
                    failures.append(f"{label} detected: {rel_s}")

        if rel.parts[0] == "scripts":
            for label, pattern in UNSAFE_BUILD_PATTERNS.items():
                if pattern.search(text):
                    failures.append(f"{label} detected: {rel_s}")

    payload_text = "\n".join(text for name, text in source_texts.items() if name != "scripts/validate-source.py")
    if "commandlinetools-win-15859902_latest.zip" in payload_text:
        failures.append("the user-supplied command-line tools archive is referenced as a bundled payload")

    sdk_text = source_texts.get("src/core/SdkManager.cpp", "")
    app_text = source_texts.get("src/Application.cpp", "")
    dialogs_text = source_texts.get("src/platform/NativeDialogs.cpp", "")
    resource_text = source_texts.get("resources/PortableAVM.rc.in", "")
    if "importCommandLineToolsArchive" not in sdk_text or "ArchiveExtractor::extractZipSafely" not in sdk_text:
        failures.append("Command-line Tools ZIP import path is missing or does not use safe ZIP extraction")
    if "selectZipFile" not in app_text or 'tr("setup.browse_cli")' not in app_text:
        failures.append("application does not expose user-selected Command-line Tools ZIP import")
    if "repository2-3.xml" in sdk_text or "downloadToFile" in sdk_text:
        failures.append("SdkManager still contains automatic Command-line Tools download logic")
    if "Skia UI" in app_text or "Skia + SDL3" in app_text or "PortableAVM 0.3" in app_text:
        failures.append("application UI still exposes build/rendering/version branding requested for removal")
    if 'ICON "@PAVM_ICON_FILE@"' not in resource_text:
        failures.append("Windows resource script does not embed replaceable PortableAVM.ico")
    if 'configs / "PortableAVM.ico"' not in app_text or "WM_SETICON" not in app_text:
        failures.append("runtime replaceable window icon support is missing")
    if "build-manifest.txt" in source_texts.get("scripts/build-windows.ps1", ""):
        failures.append("Windows distribution still emits build-manifest.txt")

    portable_paths_text = source_texts.get("src/core/PortablePaths.cpp", "")
    cmake_text = source_texts.get("CMakeLists.txt", "")
    immediate_cpp = source_texts.get("src/ui/ImmediateUi.cpp", "")
    if 'paths.root / "Data"' not in portable_paths_text:
        failures.append("portable runtime root must be named Data")
    if "resources/Data/lang/ DESTINATION Data/lang" not in cmake_text:
        failures.append("CMake install tree must deploy only resources/Data/lang into Data/lang")
    if "install(DIRECTORY resources/Data/ DESTINATION Data)" in cmake_text:
        failures.append("CMake still deploys the entire resources/Data tree instead of the minimal lang subtree")
    gitignore_text = source_texts.get(".gitignore", "")
    if "/Data/" not in gitignore_text or "/data/" in gitignore_text:
        failures.append("runtime Data directory ignore rule has stale casing")
    if active_manifest is not None:
        active_lower_data = any(rel == "resources/data" or rel.startswith("resources/data/") for rel in active_manifest)
        active_keep = any(Path(rel).name == ".keep" for rel in active_manifest)
        if active_lower_data or active_keep:
            failures.append("lowercase data resources and .keep placeholders are forbidden")
        allowed_data_prefix = "resources/Data/lang/"
        unexpected_data = [rel for rel in active_manifest
                           if rel.startswith("resources/Data/") and not rel.startswith(allowed_data_prefix)]
        if unexpected_data:
            failures.append("resources/Data must contain only language resources: " + ", ".join(unexpected_data[:8]))
    else:
        if (root / "resources" / "data").exists() or list(root.rglob(".keep")):
            failures.append("lowercase data resources and .keep placeholders are forbidden")
        data_root = root / "resources" / "Data"
        if data_root.exists():
            unexpected = [p for p in data_root.rglob("*") if p.is_file() and "lang" not in p.relative_to(data_root).parts[:1]]
            if unexpected:
                failures.append("resources/Data must contain only language resources")
    if "make(data);" not in portable_paths_text or "const std::array<std::filesystem::path, 18> directories" in portable_paths_text:
        failures.append("startup Data layout must be lazy and create only the Data root eagerly")
    if "moveOrCopyDirectory(source, prepared)" not in sdk_text:
        failures.append("Command-line Tools import does not prefer staging-directory move before copy fallback")
    for jdk_guard in (
        'ensureDirectoryExists(paths_.temp, "JDK 임시 폴더")',
        'ensureDirectoryExists(paths_.jdk.parent_path(), "JDK 런타임 폴더")',
        'std::filesystem::equivalent(source, paths_.jdk, ec)',
        'moveOrCopyDirectory(staging, paths_.jdk)',
    ):
        if jdk_guard not in sdk_text:
            failures.append(f"lazy JDK import guard missing: {jdk_guard}")
    if 'std::filesystem::equivalent(source, paths_.jdk))' in sdk_text:
        failures.append("throwing JDK equivalent() check remains and breaks fresh Data imports")
    if "Directory copy failed" in sdk_text:
        failures.append("legacy opaque Directory copy failed path remains in SdkManager")
    if "settingsSidebarExpanded_" in app_text or "toggle-settings-sidebar" in app_text:
        failures.append("legacy collapsible settings sidebar remains; navigation must use permanent side tabs")
    if "toggle-emulator-sidebar" in app_text or "emulatorSidebarExpanded_" in app_text:
        failures.append("legacy collapsible emulator sidebar remains")
    if "renderRightPanel" in app_text or "handleEmbedding" in app_text or "WindowEmbedder" in app_text:
        failures.append("legacy embedded-emulator UI/path remains; PortableAVM must operate as a launcher")
    if "statusUi.begin(canvas, input, 0, workHeight, width, kStatusBarHeight)" not in app_text:
        failures.append("fixed bottom status bar is missing")
    if "kSidebarWidth" not in app_text or "settingsUi.begin(canvas, input, contentX, 0" not in app_text:
        failures.append("permanent side-tab layout is missing")
    for side_nav in ("side-devices", "side-sdk", "side-setup", "side-apps", "side-logs", "side-language"):
        if side_nav not in app_text:
            failures.append(f"permanent side navigation item missing: {side_nav}")
    if "ImmediateUi navigationUi" not in app_text or "ImmediateUi settingsUi" not in app_text:
        failures.append("fixed navigation and scrollable settings content must use separate UI regions")
    if "launchReady()" not in app_text or "launchReadinessText()" not in app_text:
        failures.append("emulator launch readiness gate is missing")
    for launch_requirement in ("jdkInstalled()", "commandLineToolsInstalled()", "emulatorInstalled()",
                               "platformToolsInstalled()", "packageInstalled(config_.systemImagePackage())",
                               "avd_.launchable(config_)"):
        if launch_requirement not in app_text:
            failures.append(f"emulator launch readiness requirement missing: {launch_requirement}")
    if "canvas_->clipRect" not in immediate_cpp or "while (fontSize > 10.0f" not in immediate_cpp:
        failures.append("button labels are not shrink-and-clipped to their control bounds")
    if "hamburgerButton" in immediate_cpp or "folderTab" in immediate_cpp:
        failures.append("legacy hamburger/folder-tab navigation helpers remain")
    if 'cycle("main-tab"' in app_text:
        failures.append("legacy main-tab cycle control remains instead of folder-tab navigation")
    for nav_id in ("side-devices", "side-sdk", "side-setup", "side-apps", "side-logs", "side-language"):
        if nav_id not in app_text:
            failures.append(f"side navigation item missing: {nav_id}")
    if "scrollbarDragging_" not in immediate_cpp or "thumbHeight" not in immediate_cpp:
        failures.append("scrollable settings panels do not expose a draggable visible scrollbar")

    paths_text = source_texts.get("src/core/PortablePaths.cpp", "") + "\n" + source_texts.get("src/core/PortablePaths.h", "")
    if 'paths.lang = paths.data / "lang"' not in paths_text or 'std::filesystem::path lang;' not in paths_text:
        failures.append("PortablePaths does not own Data/lang")
    language_text = source_texts.get("src/core/LanguageManager.cpp", "") + "\n" + source_texts.get("src/core/LanguageManager.h", "")
    config_text = source_texts.get("src/core/Config.cpp", "") + "\n" + source_texts.get("src/core/Config.h", "")
    if 'std::string language = "ko"' not in config_text or 'out << "language="' not in config_text:
        failures.append("language preference is not persisted in Data/Configs/settings.ini")
    if 'language_(paths_.lang)' not in app_text or 'language_.setLanguage(config_.language)' not in app_text:
        failures.append("Application does not load Data/lang and persisted language")
    if 'availableLanguages()' not in app_text or '"language-" + code' not in app_text:
        failures.append("dynamic .lang-based language selection UI is missing")
    setup_start = app_text.find("void Application::renderSetup")
    avd_start = app_text.find("void Application::renderAvd")
    language_start = app_text.find("void Application::renderLanguage")
    if language_start < 0 or 'selectedTab_ == "language"' not in app_text or 'tr("nav.language")' not in app_text:
        failures.append("language selection must live in its own permanent Language tab")
    setup_end_candidates = [pos for pos in (language_start, avd_start) if pos > setup_start]
    setup_end = min(setup_end_candidates) if setup_end_candidates else -1
    if setup_start >= 0 and setup_end > setup_start and '"language-" + code' in app_text[setup_start:setup_end]:
        failures.append("language selection still appears inside the Setup tab")
    for caret_pattern in ("caretByField_", "previousUtf8Boundary", "nextUtf8Boundary",
                          "input_.moveLeft", "input_.moveRight", "input_.moveHome", "input_.moveEnd",
                          "input_.deleteKey", "value.insert(caret", "value.erase(previous"):
        if caret_pattern not in immediate_cpp:
            failures.append(f"text field caret editing support missing: {caret_pattern}")
    for input_pattern in ("SDLK_LEFT", "SDLK_RIGHT", "SDLK_HOME", "SDLK_END", "SDLK_DELETE"):
        if input_pattern not in app_text:
            failures.append(f"Application keyboard input routing missing: {input_pattern}")
    if 'language.name' not in language_text or 'english_' not in language_text or 'return key;' not in language_text:
        failures.append("language display-name/fallback chain (current -> English -> key) is missing")
    if 'resources/Data/lang' not in cmake_text or 'copy_directory' not in cmake_text or '$<TARGET_FILE_DIR:PortableAVM>/Data/lang' not in cmake_text:
        failures.append("language files are not copied into Data/lang")
    if 'install(DIRECTORY resources/lang/' in cmake_text or 'DESTINATION lang' in cmake_text:
        failures.append("legacy root lang deployment remains; language files must live under Data/lang")
    paths_text = source_texts.get("src/core/PortablePaths.cpp", "")
    if 'ensureChildEnvironmentLayout();' not in paths_text:
        failures.append("Android child-process environment paths are not created before use")
    for path_token in ("homeAndroid", "homeEmulator", "homeAdb", "homeUser", "gradleHome",
                       "appDataRoaming", "appDataLocal", "temp", "logs", "avd"):
        if path_token not in paths_text:
            failures.append(f"portable child-environment path missing from lazy pre-create layout: {path_token}")
    readme_text = source_texts.get("README.md", "")
    for token in ("## 프로젝트 폴더 역할", "## 런타임 `Data/` 폴더 역할", "Data/Runtime/Android/sdk/",
                  "Data/Runtime/jdk/", "Data/AVD/", "Data/Logs/", "src/core/", "scripts/"):
        if token not in readme_text:
            failures.append(f"README.md folder-role documentation missing: {token}")
    # Specific real-device preset remnants are intentionally forbidden. Exclude this validator
    # so the regression tokens themselves do not trigger the check.
    combined_release_text = "\n".join(text for path, text in source_texts.items() if path != "scripts/validate-source.py")
    forbidden_preset_tokens = (
        "S" + "22", "Galaxy " + "S22", "SM-" + "S901",
        "game-" + "preset", "applyGame" + "Preset", "avd." + "preset",
        "1080" + "×2340", "1080" + "x2340",
    )
    for forbidden in forbidden_preset_tokens:
        if forbidden in combined_release_text:
            failures.append(f"specific-device preset/remnant is forbidden: {forbidden}")

    process_text = source_texts.get("src/core/Process.cpp", "")
    if 'cmd.exe /D /K' in process_text:
        failures.append("SDK interactive terminal still uses /K and will remain open after licenses finish")
    if 'cmd.exe /D /C' not in process_text:
        failures.append("SDK interactive terminal must use cmd.exe /D /C for automatic close")
    if "embed_window" in config_text or "embedWindow" in config_text:
        failures.append("legacy embedded-window configuration remains")
    if "WindowEmbedder.cpp" in cmake_text:
        failures.append("CMake still builds the removed WindowEmbedder")
    build_bat_text = source_texts.get("build-windows.bat", "")
    if "pause >nul" not in build_bat_text or 'set "PAVM_RC=%errorlevel%"' not in build_bat_text or 'exit /b %PAVM_RC%' not in build_bat_text:
        failures.append("build-windows.bat must pause at the end while preserving the build exit code")
    emulator_text = source_texts.get("src/core/EmulatorManager.cpp", "")
    if 'PortableAVM.EmulatorHost' in emulator_text or 'PortableAVM.EmulatorHost' in cmake_text or \
       "src/platform/EmulatorHostWin.cpp" in source_texts:
        failures.append("legacy PortableAVM.EmulatorHost code/build rules remain")
    if 'ProcessRunner::spawn(sdk_.emulatorExecutable(), args, environment, logPath)' not in emulator_text:
        failures.append("PortableAVM must launch emulator.exe directly through ProcessRunner")

    windows_job_requirements = [
        "CREATE_SUSPENDED",
        "AssignProcessToJobObject",
        "ResumeThread",
        "QueryInformationJobObject",
        "accounting.ActiveProcesses > 0",
        "JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE",
    ]
    for token in windows_job_requirements:
        if token not in process_text:
            failures.append(f"Windows emulator process-tree tracking regression: missing {token}")
    for token in ("STARTF_USESHOWWINDOW", "SW_SHOWNORMAL", "ShowWindowAsync", "EnumWindows",
                  'CreateFileW(L"NUL"', "&inheritable, OPEN_ALWAYS", "workingDirectory = executable.parent_path()"):
        if token not in process_text:
            failures.append(f"Windows visible Emulator launch regression: missing {token}")
    if "CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | CREATE_NO_WINDOW" not in process_text:
        failures.append("direct emulator.exe launch must suppress its console with CREATE_NO_WINDOW")
    for token in ('ConsoleWindowClass', 'SW_HIDE', 'SW_SHOW', 'SW_RESTORE'):
        if token not in process_text:
            failures.append(f"console-hidden / GUI-visible Emulator launch regression: missing {token}")
    if "process_.showMainWindow()" not in emulator_text:
        failures.append("EmulatorManager does not reveal the native Emulator window")
    for token in ("confirmCloseIfEmulatorRunning", "SDL_ShowMessageBox", "emulator_.stop()",
                  'if (buttonId != 1) return false'):
        if token not in app_text:
            failures.append(f"running-Emulator close confirmation regression: missing {token}")
    for token in ("logSelectionAnchor_", "logSelectionCaret_", "logSelecting_",
                  "SDL_SetClipboardText", "input_.selectAll", "input_.copy"):
        if token not in immediate_cpp:
            failures.append(f"selectable/copyable log view regression: missing {token}")
    for token in ("SDLK_C", "SDLK_A", "SDL_KMOD_CTRL"):
        if token not in app_text:
            failures.append(f"log clipboard keyboard routing missing: {token}")
    if "ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept" not in process_text:
        failures.append("ChildProcess move assignment must explicitly release previous process/job handles")

    avd_text = source_texts.get("src/core/AvdManager.cpp", "")
    if 'ProcessRunner::run(sdk_.avdManagerExecutable(), args' in avd_text:
        failures.append("AVD creation must not depend on avdmanager create in portable mode")
    for required_avd_entry in ('values["image.sysdir.1"]', 'values["abi.type"]',
                               'descriptor << "path="', 'descriptor << "target=android-"'):
        if required_avd_entry not in avd_text:
            failures.append(f"portable AVD descriptor entry missing: {required_avd_entry}")
    if 'targetDirectory / "config.ini"' not in avd_text or 'descriptorFile' not in avd_text:
        failures.append("portable AVD creation does not verify config.ini and the AVD descriptor")
    if 'std::filesystem::absolute(imageDirectory)' not in avd_text:
        failures.append("portable AVD config must pin image.sysdir.1 to the absolute system-image directory")
    if "launchProblem" not in avd_text or 'image / "system.img"' not in avd_text:
        failures.append("AVD launchability validation is missing")
    config_h = source_texts.get("src/core/Config.h", "")
    repository_parser = source_texts.get("src/core/RepositoryParser.cpp", "")
    if "int apiLevel = 30;" not in config_h or 'std::string imageTag = "google_apis_playstore";' not in config_h:
        failures.append("fresh configurations must default to API 30 Google Play for broad app compatibility")
    if 'ProcessRunner::run(\n            sdk_.avdManagerExecutable()' not in avd_text or '"--device", config.hardwareProfile' not in avd_text:
        failures.append("AVD creation must prefer avdmanager with the selected official hardware profile")
    for token in ('values["hw.gps"]', 'values["hw.gsmModem"]', 'values["hw.accelerometer"]', 'values["hw.gyroscope"]'):
        if token not in avd_text:
            failures.append(f"portable Phone-profile fallback is missing hardware capability: {token}")
    if "parseHardwareProfiles" not in repository_parser or "isPhoneHardwareProfile" not in repository_parser:
        failures.append("official Phone hardware-profile discovery/parser is missing")
    if "availableHardwareProfiles" not in sdk_text or '{"list", "device"}' not in sdk_text:
        failures.append("SdkManager does not query official avdmanager hardware profiles")
    for token in ("refreshHardwareProfiles", "applyCompatibilityDefaults", 'config_.apiLevel = 30', 'config_.imageTag = "google_apis_playstore"'):
        if token not in app_text:
            failures.append(f"general compatibility/profile UI regression: missing {token}")
    product_text = "\n".join(text for name, text in source_texts.items() if name != "scripts/validate-source.py")
    for forbidden_name in ("트릭컬", "trickcal", "trickcalrevive"):
        if forbidden_name.lower() in product_text.lower():
            failures.append("product UI/docs must not contain app-specific compatibility wording")

    identity_text = avd_text + "\n" + source_texts.get("src/core/Config.cpp", "")
    if "isForbiddenIdentityProperty" not in avd_text:
        failures.append("AVD identity-property deny-list is missing")
    for key in IDENTITY_KEYS:
        if key not in identity_text:
            warnings.append(f"identity deny-list key not visible in AVD/config implementation: {key}")

    bootstrap_text = source_texts.get("scripts/bootstrap-windows-tools.ps1", "")
    windows_build_text = source_texts.get("scripts/build-windows.ps1", "")
    if "python*._pth" not in bootstrap_text or "import site" not in bootstrap_text:
        failures.append("portable Python bootstrap does not enable site initialization")
    if "callable(getattr(builtins, 'exit', None))" not in windows_build_text:
        failures.append("Windows build does not robustly verify normal Python site initialization before Skia sync")
    if "hasattr(builtins, exit)" in bootstrap_text or "hasattr(builtins, exit)" in windows_build_text:
        failures.append("Windows Python site check contains an invalid non-string hasattr attribute name")
    if "hasattr(builtins, \"exit\")" in bootstrap_text or "hasattr(builtins, \"exit\")" in windows_build_text:
        warnings.append("Windows Python -c site check uses nested double quotes; prefer PowerShell double-quoted argument with Python single-quoted literals")
    if "python3.exe" not in bootstrap_text or "Copy-Item -LiteralPath $python -Destination $python3" not in bootstrap_text:
        failures.append("portable Python bootstrap does not create a project-local python3.exe alias for Skia/GN")
    if "Get-Command python3.exe" not in windows_build_text or "Skia python3:" not in windows_build_text:
        failures.append("Windows build does not verify that Skia resolves project-local python3.exe before gn gen")
    if "skia-deps.sha256" not in windows_build_text or "Get-FileHash -LiteralPath $SkiaDepsFile -Algorithm SHA256" not in windows_build_text:
        failures.append("Windows build does not fingerprint Skia DEPS before deciding whether dependency sync is needed")
    if "Skia dependencies already match the current DEPS fingerprint; skipping git-sync-deps." not in windows_build_text:
        failures.append("Windows incremental build does not skip redundant Skia git-sync-deps")
    if "Run-LoggedWithRetry $Python @('tools\\git-sync-deps')" not in windows_build_text or "skia-git-sync-deps.log" not in windows_build_text:
        failures.append("Windows Skia dependency sync does not retry and preserve a dedicated failure log")
    if "Adopted existing completed Skia dependency checkout" not in windows_build_text:
        failures.append("Windows build cannot adopt a pre-stamp completed Skia checkout")
    if '--args=$gnArgs' in windows_build_text or '"--args=$gnArgs"' in windows_build_text:
        failures.append("Windows Skia build passes quoted GN args through native argv; write args.gn directly instead")
    if "Join-Path $SkiaOut 'args.gn'" not in windows_build_text or "Run $GnExe @('gen',$SkiaOut)" not in windows_build_text:
        failures.append("Windows Skia build does not use a generated args.gn file")
    for required_gn_arg in (
        'skia_use_system_libjpeg_turbo=false',
        'skia_use_system_libpng=false',
        'skia_use_system_libwebp=false',
        'skia_use_system_zlib=false',
        'skia_use_system_expat=false',
    ):
        if required_gn_arg not in windows_build_text:
            failures.append(f"Windows Skia build does not force bundled third-party dependency: {required_gn_arg}")

    emulator_text = source_texts.get("src/core/EmulatorManager.cpp", "")
    if '"-sysdir"' not in emulator_text or "std::filesystem::absolute(imageDirectory)" not in emulator_text:
        failures.append("emulator launch does not pin the selected system image with -sysdir")
    if '"-datadir"' not in emulator_text or "std::filesystem::absolute(avdDirectory)" not in emulator_text:
        failures.append("emulator launch does not pin the portable AVD data directory with -datadir")
    if "milliseconds(1800)" not in emulator_text or "tailFile(logPath)" not in emulator_text:
        failures.append("emulator launch does not detect immediate startup failure and surface the log tail")

    process_cpp = source_texts.get("src/core/Process.cpp", "")
    immediate_h = source_texts.get("src/ui/ImmediateUi.h", "")
    immediate_cpp = source_texts.get("src/ui/ImmediateUi.cpp", "")
    renderer_h = source_texts.get("src/ui/SkiaRenderer.h", "")
    renderer_cpp = source_texts.get("src/ui/SkiaRenderer.cpp", "")
    if "#include <array>" not in process_cpp:
        failures.append("Process.cpp uses std::array without including <array>")
    if "values.erase(wideKey)" not in process_cpp or "unsetenv(key.c_str())" not in process_cpp:
        failures.append("Process environment overrides cannot remove inherited legacy variables")
    if 'include/core/SkRefCnt.h' not in immediate_h or 'include/core/SkRefCnt.h' not in renderer_h:
        failures.append("UI headers store sk_sp<T> by value without including SkRefCnt.h")
    if 'include/core/SkPath.h' not in immediate_cpp:
        failures.append("ImmediateUi.cpp uses SkPath without including SkPath.h")
    if "SkFontMgr::RefDefault()" in immediate_cpp:
        failures.append("ImmediateUi.cpp uses removed SkFontMgr::RefDefault() API")
    if "SkFontMgr_New_DirectWrite()" not in immediate_cpp:
        failures.append("Windows UI does not use Skia DirectWrite font manager factory")
    if "SkFontMgr_New_FontConfig(nullptr)" not in immediate_cpp:
        failures.append("Linux UI does not use Skia FontConfig font manager factory")
    if "flushAndSubmit" in renderer_cpp:
        failures.append("SkiaRenderer.cpp calls GPU flushAndSubmit on the CPU raster surface; raster present must use peekPixels directly")

    # Dynamic DPI regressions
    if "SDL_WINDOW_HIGH_PIXEL_DENSITY" not in app_text:
        failures.append("GUI must request SDL high pixel density")
    if "SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED" not in app_text or "SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED" not in app_text:
        failures.append("GUI must react to dynamic display/pixel scale changes")
    if "SDL_GetWindowDisplayScale" not in renderer_cpp or "SDL_GetWindowSizeInPixels" not in renderer_cpp:
        failures.append("Skia renderer must use per-window display scale and pixel size")
    if "windowToContent" not in renderer_cpp or "windowToContent" not in renderer_h:
        failures.append("mouse coordinates must be converted for HiDPI")
    if "wm density " not in app_text or "wm density reset" not in app_text:
        failures.append("runtime Android density controls are missing")

    if 'sanitizedInheritedPath' not in sdk_text or 'looksLikeForeignAndroidToolPath' not in sdk_text:
        failures.append("Android child-process PATH does not filter foreign SDK tool directories")
    for required_env in ('ANDROID_SDK_HOME', 'ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT'):
        if required_env not in sdk_text:
            failures.append(f"Android child-process isolation is missing environment override: {required_env}")
    if '.latest-new-' not in sdk_text or '.latest-old-' not in sdk_text:
        failures.append("Command-line Tools ZIP import is not staged/rollback-safe before replacing latest")
    for required_path_guard in (
        'ensureDirectoryExists(paths_.data',
        'ensureDirectoryExists(paths_.temp',
        'ensureDirectoryExists(paths_.sdk',
        'ensureDirectoryExists(paths_.sdk / "cmdline-tools"',
        'ensureDirectoryExists(staging.parent_path()',
    ):
        if required_path_guard not in sdk_text:
            failures.append(f"Command-line Tools ZIP import does not auto-create required path: {required_path_guard}")
    archive_text = source_texts.get("src/core/ArchiveExtractor.cpp", "")
    if 'Cannot create extraction directory:' not in archive_text:
        failures.append("Archive extraction does not report/create a missing destination directory robustly")

    check_sdl3_main_handling(root, failures)

    combined_scripts = "\n".join(
        source_texts.get(name, "")
        for name in ("scripts/build-windows.ps1", "scripts/build-linux.sh")
    )
    if "validate-source.py" not in combined_scripts:
        failures.append("build entry points do not invoke validate-source.py")

    result = {
        "project": "PortableAVM",
        "root": str(root),
        "checked_files": checked_files,
        "status": "PASS" if not failures else "FAIL",
        "failures": failures,
        "warnings": warnings,
    }

    if args.json:
        rendered = json.dumps(result, ensure_ascii=False, indent=2)
    else:
        lines = [
            "PortableAVM source validation",
            "=============================",
            f"Root: {root}",
            f"Checked files: {checked_files}",
            f"Result: {result['status']}",
            "",
        ]
        if failures:
            lines.append("Failures:")
            lines.extend(f"- {item}" for item in failures)
            lines.append("")
        if warnings:
            lines.append("Warnings:")
            lines.extend(f"- {item}" for item in warnings)
            lines.append("")
        if not failures and not warnings:
            lines.append("No policy violations or packaging hazards were detected.")
        rendered = "\n".join(lines).rstrip() + "\n"

    print(rendered, end="")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered, encoding="utf-8")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
