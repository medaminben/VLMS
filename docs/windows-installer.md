# VLMS — Windows installer

One `.exe` that puts the application, the catalogue database and the cover
images into a single folder, and that knows what to do when VLMS is
already on the machine.

## What ships

Everything lives beside `vlms.exe` in one folder — no shared Qt, no
machine-wide Tesseract, no Visual C++ Redistributable:

```
vlms.exe
Qt6*.dll  platforms\  styles\  imageformats\
libsqlite3-0.dll
libtesseract-*.dll  liblept-*.dll  (+ their MinGW dependencies)
tessdata\{ara,fra,eng}.traineddata
schema.sql
database\vlms.db          the catalogue this release ships with
resources\books\               cover images
resources\members\             member photos
```

The application resolves all of this from `QCoreApplication::applicationDirPath()`
(`Paths::projectRoot()`), which is why the layout is flat and the install
directory has to be writable — hence a per-user install under
`%LOCALAPPDATA%\Programs` rather than `Program Files`.

## The existing-installation question

`AppId` has not changed since 0.1.0, so every copy this installer has ever
made is visible to the next one. Setup reads the uninstall entry (`HKCU` then
`HKLM`, both registry views) before writing anything, and shows a page with
the version it found, where it found it, and the database schema that version
recorded. Two choices:

The page opens with what the update is, read from the version digits:
`Update from 0.3.1 to 0.4.0: minor. The catalogue's structure is unchanged.`
Every version is cut automatically from the diff since the previous one (see
*Releases* below), so the digits carry the level: a patch fixes, a minor adds,
and only a major may change the database schema.

**Keep its database and cover images** — recommended for a patch, a minor and
a reinstall of the same version. The program files are replaced and nothing
under `database\` or `resources\` is touched. The application migrates the
database on its next launch if it needs to (`Database::upgradeSchemaIfNeeded`);
if a row cannot be migrated the database is left exactly as it was and the
attempt is repeated on the following launch.

**Delete it, including its database and cover images** — recommended for a
major, and for anything the digits cannot vouch for: an installed copy with no
readable version, a downgrade, or a recorded schema that is not this build's
(0.1.0 predates the record). Setup runs the old uninstaller silently, waits for
it, deletes whatever it left in `database\` and `resources\`, and installs clean
with the catalogue this version ships with.

The next page, **What's new**, lists the release entries between the
installed version and this one; it is skipped when there are none.

Either way, the checkbox on that page copies the existing `vlms.db` (plus
any `-wal`/`-journal` beside it) to `Documents\VLMS Backups\` first. It is
on by default and applies to both choices — a kept database is about to be
migrated in place, which is also worth a copy.

Two cases the page adapts to:

- **A newer version is installed.** Also recommends deletion: this build
  refuses to open a database stamped with a schema it does not understand.
- **The old copy was installed for all users.** Removing it needs UAC. When
  setup is not elevated the delete option is disabled with an explanation;
  re-run setup with `/ALLUSERS` (or "Run as administrator") to get it back.

A folder left behind by a half-finished uninstall — files present, no registry
entry — counts as an existing installation too. There is no uninstaller to
run, only files to delete.

Everything destructive is gated on `LooksLikeAppDir`: the path comes out of
the registry, and it is only deleted if it still contains `vlms.exe`,
`unins000.exe` or `database\vlms.db`, and is not itself a drive root or a
well-known folder.

### Silent installs

The page never appears, so the command line stands in for it:

```
VLMS_Setup_X.Y.Z.exe /VERYSILENT /PREVIOUS=remove
VLMS_Setup_X.Y.Z.exe /VERYSILENT /PREVIOUS=keep /NOBACKUP
```

Without `/PREVIOUS`, a silent install follows the recommendation: keep for a
patch, minor or reinstall, remove for a major.

### Uninstalling

The uninstaller asks whether to delete the database and cover images, and
keeps them if you say no. A silent uninstall deletes them unless `/KEEPDATA`
is passed — that is the path setup itself uses when clearing an old version.

## Building it

### On a Windows machine

```powershell
powershell -File scripts\build_windows_installer.ps1 -Toolchain MinGW -QtDir 'C:\Qt\6.8.1\mingw_64'
```

Needs CMake 3.21+, a Qt 6 MinGW kit, Inno Setup 6, and
`third_party\tesseract\windows` populated (`scripts\fetch_windows_ocr.ps1 -Toolchain MinGW`).
The script builds Release, stages into `build\installer\stage`, runs
`windeployqt`, verifies the stage, and compiles `dist\VLMS_Setup_<version>.exe`.

Version numbers come from the repository, not from the script:
`project(VLMS VERSION ...)` in the top-level `CMakeLists.txt` becomes
`MyAppVersion`, and the last `PRAGMA user_version = N` in `database/schema.sql`
becomes `SchemaVersion`. Both are passed to `ISCC` as `/D` defines.

### From Linux, via GitHub Actions

```bash
./scripts/build_windows_installer_from_linux.sh --push
```

Dispatches `.github/workflows/windows-installer.yml`, watches the run, and
downloads the `.exe` into `dist/`.

CI has no access to `database/vlms.db` or `resources/books/` — both are
gitignored, and the database holds real member records. It gets them from the
`VLMS_installer_data.zip` asset on the latest GitHub release. Refresh that
asset whenever the catalogue or the schema changes:

```bash
./scripts/package_installer_data.sh
```

then upload `dist/VLMS_installer_data.zip` to the release.

## The staleness guard

Three checks exist because one thing went wrong quietly and would again:
the July 0.1.0 data zip held a database at `user_version` 0, and nothing in the
pipeline noticed.

1. `scripts/package_installer_data.sh` refuses to pack a database whose
   `user_version` does not match `database/schema.sql`.
2. `scripts/verify_installer_stage.ps1` reads the staged database's
   `user_version` straight out of the SQLite header (offset 60) and compares it
   to the staged `schema.sql`. A mismatch is fatal under `-RequireInstallerData`,
   which is what CI passes.
3. The CI cache key for the installer data is keyed on the hash of
   `schema.sql`, so a schema change cannot be served an old cached catalogue.

A bundled database at the wrong schema is not fatal to the *user* — the app
would migrate it on first launch — but it means every fresh install starts by
migrating, which is exactly the state the existing-installation page exists to
avoid.

## Releases

Nobody picks a version number. A merge into `main` runs
`.github/workflows/release.yml`, which asks `scripts/release/release.py` what
changed since the latest `vX.Y.Z` tag:

| Level | When |
|---|---|
| major | the schema's `PRAGMA user_version` changes, a library target is added or removed, a dependency is added, a public header is removed or renamed |
| minor | a source file is added or removed, a string key is added or removed, a new design spec |
| patch | any other change to what ships |
| none | only docs, tests, CI or the session log changed; no release |

It then tags the merge commit, creates the GitHub release with the entry as its
notes and `CHANGELOG.md` attached, builds this installer from the tag and
attaches the `.exe`. The version the build uses comes from that tag
(`cmake/GitVersion.cmake`); a build past the latest tag reports
`X.Y.Z+N.gSHA`. Each CI run on `Beta` shows the level a merge would release
under **Next release** in its summary.

The catalogue data zip stays on release `v0.1.0`; the build fetches it from
there, not from the latest release.
