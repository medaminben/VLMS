; VLMS — single-folder Windows installer (Inno Setup 6).
;
; Build via: scripts\build_windows_installer.ps1
;
; Everything the application needs lives in one folder beside vlms.exe:
; the Qt and OCR runtimes, schema.sql, database\vlms.db and resources\.
; Nothing is written to System32, PATH, or a machine-wide Tesseract install.
;
; An already-installed copy is found before anything is written, and setup asks
; what to do with it. The reason that question exists rather than a silent
; overwrite: the bundled catalogue is a *data* file, and a database written by
; an older build has a different shape. See the ExistingInstall page text.

#ifndef StageDir
  #define StageDir "..\build\installer\stage"
#endif

; Passed in by build_windows_installer.ps1 from the latest vX.Y.Z tag
; (vlms_version.txt); this default only serves a hand-run iscc.
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif

; PRAGMA user_version of the bundled database/schema.sql. Passed in by
; build_windows_installer.ps1, which reads it out of the .sql file, so the
; number the wizard shows is the one the application will actually demand.
#ifndef SchemaVersion
  #define SchemaVersion "3"
#endif

#define MyAppName "VLMS"
#define MyAppPublisher "VLMS"
#define MyAppExeName "vlms.exe"

; VLMS's own AppId, shared with no other product. Never change it: it is what
; lets this installer recognise every copy of VLMS it has installed.
#define MyAppId "{8F295604-21DB-42E6-AF37-063848A8B741}"

[Setup]
AppId={{#MyAppId}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}
DefaultDirName={userpf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
; Only via the command line (/ALLUSERS). A machine-wide install needs UAC, and
; the default per-user install is what makes this setup runnable on a library
; PC without calling anyone. The one case that needs it -- removing an older
; copy that *was* installed for all users -- is detected and explained.
PrivilegesRequiredOverridesAllowed=commandline
OutputDir=..\dist
OutputBaseFilename=VLMS_Setup_{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
ChangesAssociations=no
; A running vlms.exe holds vlms.db and the Qt DLLs open. Let the
; Restart Manager close it instead of failing halfway through the copy.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Program files: executable, Qt/OCR runtimes, tessdata, schema.sql. Always
; replaced -- none of it is user data.
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "\database\*,\resources\*"; Flags: ignoreversion recursesubdirs createallsubdirs

#if DirExists(StageDir + "\database")
; The catalogue. Replaced outright on a clean install; on "keep", only files
; that are not already there are written, so vlms.db survives untouched.
Source: "{#StageDir}\database\*"; DestDir: "{app}\database"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: ReplaceExistingData
Source: "{#StageDir}\database\*"; DestDir: "{app}\database"; Flags: onlyifdoesntexist recursesubdirs createallsubdirs; Check: PreserveExistingData
#endif

#if DirExists(StageDir + "\resources")
; Cover images and member photos. Same rule: a kept installation may hold
; covers someone scanned in by hand, and those are not ours to overwrite.
Source: "{#StageDir}\resources\*"; DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: ReplaceExistingData
Source: "{#StageDir}\resources\*"; DestDir: "{app}\resources"; Flags: onlyifdoesntexist recursesubdirs createallsubdirs; Check: PreserveExistingData
#endif

#ifdef WhatsNewFile
; The release entries, read by the existing-installation page and never installed.
Source: "{#WhatsNewFile}"; DestName: "whats_new.txt"; Flags: dontcopy
#endif

[Dirs]
; Created even when the installer ships without bundled data, so the first run
; writes into an existing tree rather than creating one under Program Files.
Name: "{app}\database"
Name: "{app}\resources\books"
Name: "{app}\resources\members"

[Registry]
; Read back by the next installer: the app version and the schema version of
; the database it shipped with, which is what the upgrade decision turns on.
Root: HKA; Subkey: "Software\VLMS"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\VLMS\{#MyAppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\VLMS\{#MyAppName}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"
Root: HKA; Subkey: "Software\VLMS\{#MyAppName}"; ValueType: string; ValueName: "Version"; ValueData: "{#MyAppVersion}"
Root: HKA; Subkey: "Software\VLMS\{#MyAppName}"; ValueType: string; ValueName: "SchemaVersion"; ValueData: "{#SchemaVersion}"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; SQLite side files and anything the application wrote after install. The
; database and cover trees themselves are handled in CurUninstallStepChanged,
; which asks first.
Type: files; Name: "{app}\database\vlms.db-journal"
Type: files; Name: "{app}\database\vlms.db-wal"
Type: files; Name: "{app}\database\vlms.db-shm"
Type: dirifempty; Name: "{app}\resources\books"
Type: dirifempty; Name: "{app}\resources\members"
Type: dirifempty; Name: "{app}\resources"
Type: dirifempty; Name: "{app}\database"
Type: dirifempty; Name: "{app}"

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nAll program files, libraries, and OCR data are installed into a single folder. No separate Qt, Tesseract, or other prerequisites are required.

[Code]
const
  UninstallSubKey =
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1';
  ProductSubKey = 'Software\VLMS\{#MyAppName}';

  { Result of comparing the installed version against {#MyAppVersion}. }
  RelOlder = -1;
  RelSame = 0;
  RelNewer = 1;

var
  PrevFound: Boolean;
  PrevMachineWide: Boolean;      { installed for all users -> removal needs UAC }
  PrevRegistered: Boolean;       { has an uninstall entry, as opposed to a leftover folder }
  PrevDirLooksRight: Boolean;    { checked before anything is deleted }
  PrevRelation: Integer;
  PrevVersion: String;           { '' when unknown }
  PrevSchema: String;            { '' when written before this key existed }
  PrevDir: String;
  PrevUninstaller: String;

  ExistingPage: TWizardPage;
  WhatsNewPage: TOutputMsgMemoWizardPage;
  RadRemove: TNewRadioButton;
  RadKeep: TNewRadioButton;
  ChkBackup: TNewCheckBox;
  BackupPath: String;            { set once the copy has been made, for the final message }

function CompareVersionStrings(const A, B: String): Integer;
var
  VA, VB: Int64;
begin
  { StrToVersion fails on anything that is not x.y.z.w; an unreadable version
    is treated as older, which is the conservative half -- it recommends
    removal rather than quietly keeping a database of unknown shape. }
  if not StrToVersion(A, VA) then
  begin
    Result := RelOlder;
    Exit;
  end;
  if not StrToVersion(B, VB) then
  begin
    Result := RelNewer;
    Exit;
  end;
  Result := ComparePackedVersion(VA, VB);
  if Result < 0 then
    Result := RelOlder
  else if Result > 0 then
    Result := RelNewer;
end;

function ReadUninstallEntry(RootKey: Integer): Boolean;
var
  Value: String;
begin
  Result := False;
  if not RegQueryStringValue(RootKey, UninstallSubKey, 'UninstallString', Value) then
    Exit;
  if Value = '' then
    Exit;

  PrevUninstaller := RemoveQuotes(Value);
  if not FileExists(PrevUninstaller) then
  begin
    { The entry outlived the files it points at -- a half-finished uninstall.
      Keep looking; the folder check below may still find something to clean. }
    PrevUninstaller := '';
    Exit;
  end;

  if not RegQueryStringValue(RootKey, UninstallSubKey, 'InstallLocation', PrevDir) then
    PrevDir := '';
  PrevDir := RemoveBackslashUnlessRoot(RemoveQuotes(PrevDir));
  if PrevDir = '' then
    PrevDir := ExtractFileDir(PrevUninstaller);

  if not RegQueryStringValue(RootKey, UninstallSubKey, 'DisplayVersion', PrevVersion) then
    PrevVersion := '';

  Result := True;
end;

{ True only for a directory that still looks like a VLMS installation.
  Everything destructive is gated on this: InstallLocation comes out of the
  registry, and a corrupt or hand-edited value must never turn into a DelTree
  of Program Files or a drive root. }
function LooksLikeAppDir(const Dir: String): Boolean;
var
  Normalised: String;
begin
  Result := False;
  if Dir = '' then
    Exit;

  Normalised := RemoveBackslashUnlessRoot(Dir);
  if Length(Normalised) < 4 then
    Exit;
  if Normalised = ExtractFileDrive(Normalised) then
    Exit;

  { Refuse the containers an install lives *in*, never *is*. }
  if CompareText(Normalised, ExpandConstant('{userpf}')) = 0 then Exit;
  if CompareText(Normalised, ExpandConstant('{commonpf}')) = 0 then Exit;
  if CompareText(Normalised, ExpandConstant('{commonpf32}')) = 0 then Exit;
  if CompareText(Normalised, ExpandConstant('{localappdata}')) = 0 then Exit;
  if CompareText(Normalised, ExpandConstant('{userappdata}')) = 0 then Exit;
  if CompareText(Normalised, ExpandConstant('{userdocs}')) = 0 then Exit;
  if CompareText(Normalised, ExpandConstant('{userdesktop}')) = 0 then Exit;
  if CompareText(Normalised, ExpandConstant('{win}')) = 0 then Exit;
  if CompareText(Normalised, ExpandConstant('{sys}')) = 0 then Exit;

  Result := FileExists(Normalised + '\{#MyAppExeName}') or
            FileExists(Normalised + '\unins000.exe') or
            FileExists(Normalised + '\database\vlms.db');
end;

{ The installed copy's database, or '' when it has none. }
function PrevDatabasePath: String;
begin
  if FileExists(PrevDir + '\database\vlms.db') then
    Result := PrevDir + '\database\vlms.db'
  else
    Result := '';
end;

procedure DetectPreviousInstall;
var
  Candidate: String;
begin
  PrevFound := False;
  PrevMachineWide := False;
  PrevRegistered := False;
  PrevDir := '';
  PrevUninstaller := '';
  PrevVersion := '';
  PrevSchema := '';

  { Per-user first: it is where this installer puts things by default. Both
    registry views are read because a 32-bit setup would have written the
    other one. Written out rather than chained with `or` so that each call is
    made only when the previous one found nothing -- these have side effects,
    and Pascal Script does not promise short-circuit evaluation. }
  PrevRegistered := ReadUninstallEntry(HKCU64);
  if not PrevRegistered then
    PrevRegistered := ReadUninstallEntry(HKCU32);

  if not PrevRegistered then
  begin
    PrevRegistered := ReadUninstallEntry(HKLM64);
    if not PrevRegistered then
      PrevRegistered := ReadUninstallEntry(HKLM32);
    PrevMachineWide := PrevRegistered;
  end;

  if PrevRegistered then
    PrevFound := True
  else
  begin
    { No uninstall entry. A folder left behind by a failed uninstall still
      holds a database this build cannot use, so it counts as found -- there
      is simply nothing to run, only files to delete. }
    Candidate := ExpandConstant('{userpf}\{#MyAppName}');
    if LooksLikeAppDir(Candidate) then
    begin
      PrevFound := True;
      PrevDir := Candidate;
    end;
  end;

  if not PrevFound then
    Exit;

  PrevDirLooksRight := LooksLikeAppDir(PrevDir);

  if not RegQueryStringValue(HKCU, ProductSubKey, 'SchemaVersion', PrevSchema) then
    if not RegQueryStringValue(HKLM, ProductSubKey, 'SchemaVersion', PrevSchema) then
      PrevSchema := '';

  if PrevVersion = '' then
    PrevRelation := RelOlder
  else
    PrevRelation := CompareVersionStrings(PrevVersion, '{#MyAppVersion}');
end;

function PrevVersionLabel: String;
begin
  if PrevVersion <> '' then
    Result := 'version ' + PrevVersion
  else
    Result := 'an unidentified version';
end;

{ What is known about the shape of the installed catalogue. The schema number
  is only there for installs made by 0.2.0 and later; before that the honest
  answer is that it predates the record. }
function PrevSchemaLabel: String;
begin
  if PrevSchema <> '' then
    Result := 'Its catalogue is at database schema version ' + PrevSchema +
              '; this version uses schema {#SchemaVersion}.'
  else
    Result := 'It predates the schema record, so its catalogue was written' +
              ' before database versioning existed; this version uses schema {#SchemaVersion}.';
end;

const
  LevelReinstall = 0;
  LevelPatch = 1;
  LevelMinor = 2;
  LevelMajor = 3;

{ One dotted component of a version, or -1 when it is missing or not a number. }
function VersionPart(const Version: String; Index: Integer): Integer;
var
  Rest: String;
  I, P: Integer;
begin
  Rest := Version;
  for I := 1 to Index do
  begin
    P := Pos('.', Rest);
    if P = 0 then
    begin
      Result := -1;
      Exit;
    end;
    Rest := Copy(Rest, P + 1, Length(Rest));
  end;
  P := Pos('.', Rest);
  if P > 0 then
    Rest := Copy(Rest, 1, P - 1);
  Result := StrToIntDef(Rest, -1);
end;

{ What this update is, read from the version digits. Every version is cut by
  release.py from the diff since the previous one, so the digits carry the
  level: only a major can change the database schema. Anything the digits
  cannot vouch for counts as major: an unreadable or missing version, a
  downgrade, or a recorded schema that is not this build's -- checked before
  the version comparison, so a reinstall is only a reinstall when the
  recorded schema is this build's, never on a schema mismatch alone. }
function UpdateLevel: Integer;
begin
  if PrevVersion = '' then
    Result := LevelMajor
  else if PrevSchema <> '{#SchemaVersion}' then
    Result := LevelMajor
  else if PrevRelation = RelNewer then
    Result := LevelMajor
  else if PrevRelation = RelSame then
    Result := LevelReinstall
  else if VersionPart(PrevVersion, 0) <> VersionPart('{#MyAppVersion}', 0) then
    Result := LevelMajor
  else if VersionPart(PrevVersion, 1) <> VersionPart('{#MyAppVersion}', 1) then
    Result := LevelMinor
  else
    Result := LevelPatch;
end;

function UpdateLevelName: String;
begin
  case UpdateLevel of
    LevelPatch: Result := 'patch';
    LevelMinor: Result := 'minor';
    LevelMajor: Result := 'major';
  else
    Result := 'reinstall';
  end;
end;

{ The wizard's recommendation: a clean install only for a major. A patch or
  minor holds the library's own records in a database this build reads as it
  is, and deleting it by default would throw a live catalogue away. }
function RemovalRecommended: Boolean;
begin
  Result := UpdateLevel = LevelMajor;
end;

{ The page's first line. }
function UpdateSummary: String;
begin
  if UpdateLevel = LevelReinstall then
    Result := 'Reinstalling version {#MyAppVersion}.'
  else if PrevVersion = '' then
    Result := 'Update from an unidentified version to {#MyAppVersion}: major.'
  else
  begin
    Result := 'Update from ' + PrevVersion + ' to {#MyAppVersion}: ' + UpdateLevelName + '.';
    if (UpdateLevel = LevelPatch) or (UpdateLevel = LevelMinor) then
      Result := Result + ' The catalogue''s structure is unchanged.';
  end;
end;

{ The release entries newer than the installed version, newest first, from the
  V|/W|/-| records release.py wrote. Empty when there are none. }
function WhatsNewText: String;
var
  Lines: TArrayOfString;
  I, P: Integer;
  Include: Boolean;
  Line, Rest, Version: String;
begin
  Result := '';
#ifdef WhatsNewFile
  ExtractTemporaryFile('whats_new.txt');
  if not LoadStringsFromFile(ExpandConstant('{tmp}\whats_new.txt'), Lines) then
    Exit;
  Include := False;
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    Line := Lines[I];
    if Copy(Line, 1, 2) = 'V|' then
    begin
      Rest := Copy(Line, 3, Length(Line));
      P := Pos('|', Rest);
      Version := Copy(Rest, 1, P - 1);
      Rest := Copy(Rest, P + 1, Length(Rest));
      StringChangeEx(Rest, '|', ', ', True);
      Include := (PrevVersion = '') or (CompareVersionStrings(PrevVersion, Version) = RelOlder);
      if Include then
      begin
        if Result <> '' then
          Result := Result + #13#10;
        Result := Result + Version + ' (' + Rest + ')' + #13#10;
      end;
    end
    else if Include and (Copy(Line, 1, 2) = 'W|') then
      Result := Result + '  Why: ' + Copy(Line, 3, Length(Line)) + #13#10
    else if Include and (Copy(Line, 1, 2) = '-|') then
      Result := Result + '  - ' + Copy(Line, 3, Length(Line)) + #13#10;
  end;
#endif
end;

function RemovalPossible: Boolean;
begin
  Result := (not PrevMachineWide) or IsAdminInstallMode;
end;

procedure UpdateBackupCheckboxState;
begin
  { Nothing to back up once the choice is to keep everything in place and no
    file will be touched -- but a kept database is about to be migrated in
    place by the new build, so the copy is worth having either way. }
  ChkBackup.Enabled := PrevDatabasePath <> '';
  if not ChkBackup.Enabled then
    ChkBackup.Checked := False;
end;

procedure ChoiceClicked(Sender: TObject);
begin
  UpdateBackupCheckboxState;
end;

function AddDescription(Page: TWizardPage; const Text: String; Top: Integer): TNewStaticText;
begin
  Result := TNewStaticText.Create(Page);
  Result.Parent := Page.Surface;
  Result.Left := ScaleX(16);
  Result.Top := Top;
  Result.Width := Page.SurfaceWidth - ScaleX(16);
  Result.WordWrap := True;
  Result.Caption := Text;
  Result.AdjustHeight;
end;

procedure CreateExistingInstallPage;
var
  Info: TNewStaticText;
  Detail: TNewStaticText;
  Warning: TNewStaticText;
  Top: Integer;
begin
  ExistingPage := CreateCustomPage(
    wpWelcome,
    'Existing installation found',
    'VLMS is already installed on this computer.');

  Info := TNewStaticText.Create(ExistingPage);
  Info.Parent := ExistingPage.Surface;
  Info.Left := 0;
  Info.Top := 0;
  Info.Width := ExistingPage.SurfaceWidth;
  Info.WordWrap := True;
  Info.Caption :=
    UpdateSummary + #13#10 +
    'Setup found ' + PrevVersionLabel + ' in' + #13#10 +
    PrevDir + #13#10 +
    PrevSchemaLabel;
  Info.AdjustHeight;
  Top := Info.Top + Info.Height + ScaleY(14);

  RadRemove := TNewRadioButton.Create(ExistingPage);
  RadRemove.Parent := ExistingPage.Surface;
  RadRemove.Left := 0;
  RadRemove.Top := Top;
  RadRemove.Width := ExistingPage.SurfaceWidth;
  RadRemove.OnClick := @ChoiceClicked;
  RadRemove.Caption := 'Delete it, including its database and cover images';
  if RemovalRecommended then
    RadRemove.Caption := RadRemove.Caption + '  (recommended)';
  Top := Top + RadRemove.Height + ScaleY(2);

  Detail := AddDescription(ExistingPage,
    'The installed copy is uninstalled first, then its database and resources folders are' +
    ' deleted, and {#MyAppVersion} is installed clean with the catalogue it ships with.' +
    ' Choose this when the catalogue''s structure changed (a major update) or to start' +
    ' again from the shipped catalogue.',
    Top);
  Top := Detail.Top + Detail.Height + ScaleY(14);

  RadKeep := TNewRadioButton.Create(ExistingPage);
  RadKeep.Parent := ExistingPage.Surface;
  RadKeep.Left := 0;
  RadKeep.Top := Top;
  RadKeep.Width := ExistingPage.SurfaceWidth;
  RadKeep.OnClick := @ChoiceClicked;
  RadKeep.Caption := 'Keep its database and cover images';
  if not RemovalRecommended then
    RadKeep.Caption := RadKeep.Caption + '  (recommended)';
  Top := Top + RadKeep.Height + ScaleY(2);

  Detail := AddDescription(ExistingPage,
    'Only the program files are replaced. The existing vlms.db and every cover image' +
    ' already on disk are left as they are, and {#MyAppName} migrates the database when it' +
    ' next starts if it needs to. If a record cannot be migrated the database is left' +
    ' untouched and the upgrade is retried on the following launch.',
    Top);
  Top := Detail.Top + Detail.Height + ScaleY(14);

  ChkBackup := TNewCheckBox.Create(ExistingPage);
  ChkBackup.Parent := ExistingPage.Surface;
  ChkBackup.Left := 0;
  ChkBackup.Top := Top;
  ChkBackup.Width := ExistingPage.SurfaceWidth;
  ChkBackup.Caption := 'Copy the existing database to Documents\VLMS Backups first';
  ChkBackup.Checked := True;
  Top := Top + ChkBackup.Height + ScaleY(12);

  if RemovalRecommended then
    RadRemove.Checked := True
  else
    RadKeep.Checked := True;

  if not RemovalPossible then
  begin
    RadRemove.Enabled := False;
    RadKeep.Checked := True;
    Warning := AddDescription(ExistingPage,
      'The installed copy was installed for all users, so removing it needs administrator' +
      ' rights. To delete it, close Setup and start it again with "Run as administrator".',
      Top);
    Warning.Font.Style := [fsBold];
    Warning.AdjustHeight;
    Top := Warning.Top + Warning.Height + ScaleY(12);
  end;

  UpdateBackupCheckboxState;
end;

function InitializeSetup: Boolean;
begin
  DetectPreviousInstall;
  Result := True;
end;

procedure InitializeWizard;
var
  WhatsNewContent: String;
begin
  if PrevFound then
  begin
    CreateExistingInstallPage;
    WhatsNewContent := WhatsNewText;
    if WhatsNewContent <> '' then
      WhatsNewPage := CreateOutputMsgMemoPage(ExistingPage.ID, 'What''s new',
        'Changes since ' + PrevVersionLabel + '.',
        'These are the releases between the installed copy and this one, newest first.',
        WhatsNewContent);
  end;
end;

{ Silent installs never see the page, so the command line stands in for it:
    /PREVIOUS=remove   delete the old version and its data
    /PREVIOUS=keep     install over it and leave the data alone
    (neither)          follow the recommendation: keep for a patch, minor
                       or reinstall, remove for a major
    /NOBACKUP          skip the database copy
  An interactive run ignores all three; the radio buttons win. }
function SilentChoiceIsKeep: Boolean;
var
  Choice: String;
begin
  Choice := ExpandConstant('{param:PREVIOUS|}');
  if Choice = '' then
    Result := not RemovalRecommended
  else
    Result := CompareText(Choice, 'keep') = 0;
end;

function KeepExistingData: Boolean;
begin
  if not PrevFound then
    Result := False
  else if RadKeep <> nil then
    Result := RadKeep.Checked
  else
    Result := SilentChoiceIsKeep or (not RemovalPossible);
end;

function ShouldBackupDatabase: Boolean;
begin
  if not PrevFound then
    Result := False
  else if ChkBackup <> nil then
    Result := ChkBackup.Checked and ChkBackup.Enabled
  else
    Result := (not WizardSilent) or (ExpandConstant('{param:NOBACKUP|0}') = '0');
end;

{ [Files] Check functions. }
function ReplaceExistingData: Boolean;
begin
  Result := not KeepExistingData;
end;

function PreserveExistingData: Boolean;
begin
  Result := KeepExistingData;
end;

function BackupExistingDatabase: Boolean;
var
  Source, DestDir, Stamp, Tag: String;
begin
  Result := True;
  Source := PrevDatabasePath;
  if Source = '' then
    Exit;

  DestDir := ExpandConstant('{userdocs}\VLMS Backups');
  if not ForceDirectories(DestDir) then
  begin
    Result := False;
    Exit;
  end;

  if PrevVersion <> '' then
    Tag := PrevVersion
  else
    Tag := 'unknown';
  Stamp := GetDateTimeString('yyyymmdd-hhnnss', #0, #0);
  BackupPath := DestDir + '\vlms-' + Tag + '-' + Stamp + '.db';

  Result := FileCopy(Source, BackupPath, False);
  if not Result then
  begin
    BackupPath := '';
    Exit;
  end;

  { A database closed mid-transaction leaves these behind, and the .db alone
    is not a restorable copy without them. Best effort: their absence is the
    normal case, not a failure. }
  if FileExists(Source + '-wal') then
    FileCopy(Source + '-wal', BackupPath + '-wal', False);
  if FileExists(Source + '-journal') then
    FileCopy(Source + '-journal', BackupPath + '-journal', False);
end;

{ Inno's uninstaller relaunches itself from a temporary copy, so Exec can
  return while the real work is still running. Wait for unins000.exe to go
  away, with a ceiling so a wedged uninstaller cannot hang setup forever. }
procedure WaitForUninstaller(const UninstallerPath: String);
var
  Waited: Integer;
begin
  Waited := 0;
  while FileExists(UninstallerPath) and (Waited < 180000) do
  begin
    Sleep(500);
    Waited := Waited + 500;
  end;
end;

function RemovePreviousInstall: String;
var
  ResultCode: Integer;
begin
  Result := '';

  if PrevRegistered and (PrevUninstaller <> '') then
  begin
    if not Exec(PrevUninstaller,
                '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART',
                ExtractFileDir(PrevUninstaller),
                SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    begin
      Result :=
        'Setup could not start the uninstaller of the installed version:' + #13#10 +
        PrevUninstaller + #13#10#13#10 +
        'Remove VLMS from Settings > Apps and run this installer again, or go back' + #13#10 +
        'and choose to keep the existing data.';
      Exit;
    end;
    WaitForUninstaller(PrevUninstaller);
  end;

  { Whatever the uninstaller left: the database and covers it did not install,
    SQLite side files, and the folder itself. Guarded by the check made before
    anything ran, when vlms.exe was still there to identify the folder. }
  if PrevDirLooksRight then
  begin
    DelTree(PrevDir + '\database', True, True, True);
    DelTree(PrevDir + '\resources', True, True, True);
    DelTree(PrevDir, True, True, True);
  end;

  if PrevDatabasePath <> '' then
    Result :=
      'The old database is still in use and could not be deleted:' + #13#10 +
      PrevDatabasePath + #13#10#13#10 +
      'Close VLMS and run this installer again.';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not PrevFound then
    Exit;

  if ShouldBackupDatabase then
    if not BackupExistingDatabase then
    begin
      Result :=
        'Setup could not copy the existing database to' + #13#10 +
        ExpandConstant('{userdocs}\VLMS Backups') + #13#10#13#10 +
        'Close VLMS if it is running, or go back and clear the backup option.';
      Exit;
    end;

  if not KeepExistingData then
    Result := RemovePreviousInstall;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  { A backup nobody knows about is not a backup. The finished page is where
    the path belongs -- it stays on screen, unlike a message box in the middle
    of the copy that gets clicked away. }
  if (CurPageID = wpFinished) and (BackupPath <> '') then
    WizardForm.FinishedLabel.Caption :=
      WizardForm.FinishedLabel.Caption + #13#10#13#10 +
      'The previous database was copied to:' + #13#10 + BackupPath;
end;

function UpdateReadyMemo(const Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := '';
  if PrevFound then
  begin
    Result := 'Existing installation (' + PrevVersionLabel + '):' + NewLine;
    if KeepExistingData then
      Result := Result + Space + 'Keep its database and cover images' + NewLine
    else
      Result := Result + Space + 'Delete it, including its database and cover images' + NewLine;
    if ShouldBackupDatabase then
      Result := Result + Space + 'Copy the database to Documents\VLMS Backups first' + NewLine;
    Result := Result + NewLine;
  end;

  Result := Result + MemoDirInfo;
  { DisableProgramGroupPage is on, so MemoGroupInfo is normally empty; appending
    it unconditionally would leave a blank block on the Ready page. }
  if MemoGroupInfo <> '' then
    Result := Result + NewLine + NewLine + MemoGroupInfo;
  if MemoTasksInfo <> '' then
    Result := Result + NewLine + NewLine + MemoTasksInfo;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir, ResourcesDir: String;
  DeleteData: Boolean;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;

  DataDir := ExpandConstant('{app}\database');
  ResourcesDir := ExpandConstant('{app}\resources');
  if not (DirExists(DataDir) or DirExists(ResourcesDir)) then
    Exit;

  if UninstallSilent then
    { This is the path setup itself takes when removing an older version, and
      the point of that removal is to leave nothing behind. /KEEPDATA opts out. }
    DeleteData := ExpandConstant('{param:KEEPDATA|0}') = '0'
  else
    DeleteData := MsgBox(
      'Also delete the library database and the cover images in' + #13#10 +
      ExpandConstant('{app}') + '?' + #13#10#13#10 +
      'Choose No to keep them for a future installation of VLMS.',
      mbConfirmation, MB_YESNO) = IDYES;

  if DeleteData then
  begin
    DelTree(DataDir, True, True, True);
    DelTree(ResourcesDir, True, True, True);
  end;
end;
