# Audit Notes

## Fixed During Audit

- Corrected the `BCrypt` SHA-256 implementation so `BCRYPT_OBJECT_LENGTH` is used as the hash object buffer size instead of being treated as a generic property buffer length.
- Added COM initialization for shell known-folder calls.
- Normalized known-folder path equality checks so equivalent paths with different casing or separators do not get scanned as separate source folders.
- Renamed the project target and generated executable to `fuck_onedrive`.
- Kept generated build output out of version control through `.gitignore`.

## Validated

- `cmake --preset vs2026-x64`
- `cmake --build --preset vs2026-debug --parallel`
- `cmake --build --preset vs2026-release --parallel`
- Release executable launch smoke test

## Remaining Risk

- OneDrive may recreate user-level auto-start entries after the user signs back into OneDrive or re-enables folder backup.
- Cloud-only placeholder files may need hydration before they can be hashed and copied.
- Some Windows components and applications may need Explorer restart or user sign-out before restored known-folder paths are fully observed.
- This tool changes current-user shell-folder state and should be tested on disposable or backed-up profiles before broad use.
