# Fuck OneDrive

A Windows C++/ImGui utility for taking local user folders back from OneDrive known-folder redirection.

> "Fuck You Microsoft and OneDirve"

## What It Does

- Detects OneDrive process state, auto-start state, executable path, and local OneDrive root.
- Inspects `Desktop`, `Documents`, `Pictures`, `Music`, and `Videos` shell-folder routes.
- Restores known folders to `%USERPROFILE%` defaults for the current Windows user.
- Updates current-user `User Shell Folders` and `Shell Folders` registry values as a fallback.
- Builds a preview plan for files still living under OneDrive mirror folders.
- Copies files back to local folders without overwriting different local files.
- Verifies copied files with `SHA-256`.
- Writes JSON audit files and logs under `%LOCALAPPDATA%\\FuckOneDrive`.

## Safety Model

- The tool defaults to copy-first behavior.
- Existing local files are not overwritten.
- Filename conflicts are copied to a recovered filename after hash comparison.
- Source files are deleted only when the user enables that option and verification succeeds.
- System repair actions are current-user scoped.
- Some apps may still need Explorer restart or sign-out before they observe restored folder paths.

## Build

Prebuilt Windows x64 binaries are available from the GitHub Releases page.

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-release
```

Debug builds are also available:

```powershell
cmake --build --preset vs2026-debug
```

The Release executable is generated at:

```text
build/vs2026-x64/Release/fuck_onedrive.exe
```

## Dependencies

Dependencies are fetched by CMake:

- Dear ImGui
- GLFW
- nlohmann/json

Windows APIs used include Shell Known Folders, registry APIs, ToolHelp process enumeration, and BCrypt.

## Development Provenance

This repository was implemented end-to-end with OpenAI ChatGPT 5.4 Think, audited with OpenAI ChatGPT 5.5 Think, and completed in a 12-minute build session.

## License

MIT
