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
LICENSE  THIRD_PARTY_NOTICES.txt
licenses\                      licence texts, BUNDLED_FILES.txt, msys2\<package>\
database\vlms.db          the catalogue this release ships with
resources\books\               cover images
resources\members\             member photos
```

`licenses\BUNDLED_FILES.txt` is written by `scripts\write_bundled_licenses.ps1`
once the stage is complete. It lists every DLL with where it came from (Qt, an
MSYS2 package, or the GCC runtime) and copies each MSYS2 package's licence
files beside it. A DLL it cannot place is listed as UNKNOWN with a warning in
the build log; find its licence before releasing.

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
any `-wal`/`-journal` beside it) and the installation's `resources` folder to
`Documents\VLMS Backups\` first. It is on by default and applies to both
choices — a kept database is about to be migrated in place, which is also worth
a copy. The images are copied even when the installation has no `vlms.db`. If
the copy fails, Setup says which part failed and changes nothing.

If VLMS is still open, Setup asks to close it before it copies or deletes
anything, and offers *Retry* and *Cancel*. It knows from `database\vlms.lock`,
which a running VLMS holds open. The uninstaller asks the same. A run with
`/SUPPRESSMSGBOXES` cannot ask, so it stops with an error instead of waiting.

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

## Testing an update by hand

None of the paths above run in CI: they need Windows, an older installer and a
database with someone's work in it. Before a release that changes the
installer or the schema, run this on a throwaway Windows machine or VM. Use
the sample catalogue, never a library's real one.

**Prepare**

1. Install the previous release from its GitHub release page.
2. Start it once. Add a member with a photograph and an identity-card image,
   give a book a cover, and lend a copy. Close VLMS.
3. Write down the member count, the open loan, and the three image files under
   `resources\`.

**Keep its database and cover images**

4. Start VLMS, then run the new `VLMS_Setup_X.Y.Z.exe`. The page shows the
   installed version, the update level and the recorded schema. Leave the
   backup box ticked and choose *Keep*. Setup says VLMS is still open: close
   it and click *Retry*.
5. Check `Documents\VLMS Backups\` now holds a `vlms-*.db` and a
   `vlms-*-resources` folder with the three images from step 3.
6. Start VLMS. The member, their two images, the cover and the open loan are
   all there. If the schema changed, `database\` also holds a
   `vlms.db.bak-v<version>-<time>`.

**Restore the copy**

7. Close VLMS. Rename `database\vlms.db` aside. The copy in
   `Documents\VLMS Backups\` is named `vlms-<version>-<stamp>.db`: put it in
   `database\` and rename it to `vlms.db`. Then put the contents of the matching
   `vlms-…-resources` folder back into the installation's `resources\` folder.
8. Start VLMS. It opens, migrates again if it has to, and shows the data from
   step 2.

**Delete it, including its database and cover images**

9. Run the installer again and choose *Delete*. Setup finishes without an
   error, `database\` holds the shipped catalogue, and the member from step 2
   is gone — along with their photograph and identity-card image. Their copies
   are in the `vlms-*-resources` folder in `Documents\VLMS Backups\` from
   step 4.

**Uninstall**

10. Start VLMS and uninstall from *Settings → Apps*: the uninstaller says VLMS
    is still open. Close it, click *Retry*, and answer *No* to deleting the
    data: `database\` and `resources\` stay. Reinstall, then uninstall again
    answering *Yes*: the folder is gone.

Anything that does not match is a release blocker.

## Building it

### On a Windows machine

```powershell
powershell -File scripts\build_windows_installer.ps1 -Toolchain MinGW -QtDir 'C:\Qt\6.8.1\mingw_64'
```

Needs CMake 3.21+, a Qt 6 MinGW kit, Inno Setup 6, and
`third_party\tesseract\windows` populated (`scripts\fetch_windows_ocr.ps1 -Toolchain MinGW`).
The script builds Release, stages into `build\installer\stage`, runs
`windeployqt`, verifies the stage, and compiles `dist\VLMS_Setup_<version>.exe`.

Version numbers come from the repository, not from the script: the latest
`vX.Y.Z` tag sets `project(VLMS VERSION ...)` in the top-level `CMakeLists.txt`
(`cmake/GitVersion.cmake`), which becomes `MyAppVersion`, and the last
`PRAGMA user_version = N` in `database/schema.sql` becomes `SchemaVersion`. Both are passed to `ISCC` as `/D` defines.

### From Linux, via GitHub Actions

```bash
./scripts/build_windows_installer_from_linux.sh --push
```

Dispatches `.github/workflows/windows-installer.yml`, watches the run, and
downloads the `.exe` into `dist/`.

### Sample data

The installer ships the sample catalogue: `database/vlms.db`, which is tracked,
and `resources/books/` and `resources/members/`, which are not. The images live
in `VLMS_installer_data.zip` on the release named by `DataTag` in
`scripts/fetch_installer_data.ps1`. CI runs that script, which downloads the zip
(or `VLMS_INSTALLER_DATA_URL`) into a checkout without the images. On Linux,
`scripts/fetch_runtime_data.sh` does the same for a development checkout.

To change the sample images:

1. Edit `resources/books/` or `resources/members/` locally.
2. Run `scripts/package_installer_data.sh` to build `dist/VLMS_installer_data.zip`.
3. Upload it to a new release, for example
   `gh release create installer-data-3 dist/VLMS_installer_data.zip --latest=false --prerelease`.
   The tag must not start with `v`: release tags are `vX.Y.Z` and cut
   automatically. `--latest=false` keeps the newest program release the latest.
4. Bump `DataTag` in `fetch_installer_data.ps1` and `DATA_TAG` in
   `fetch_runtime_data.sh` to the new tag.

## The staleness guard

These checks exist because one thing went wrong quietly and would again:
the July 0.1.0 data zip held a database at `user_version` 0, and nothing in the
pipeline noticed.

1. The data zip no longer carries a database; `database/vlms.db` is tracked and
   built from the checkout.
2. `scripts/verify_installer_stage.ps1` reads the staged database's
   `user_version` straight out of the SQLite header (offset 60) and compares it
   to the staged `schema.sql`. A mismatch is fatal under `-RequireInstallerData`,
   which is what CI passes.

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
