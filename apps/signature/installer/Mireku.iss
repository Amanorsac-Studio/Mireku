; Mireku.iss - the Windows installer for MIREKU.
;
; Written against the Amanorsac Studio Installer & Packaging Standard 1.0.
;
;   build:  scripts\build-windows-installer.ps1      (stages, compiles, signs)
;   or:     ISCC /DBuildDir=... /DCrtDir=... Mireku.iss
;
; Pages, in the standard's order: Welcome, Licence, Components, Installing, Done.
;
; WHAT IS NOT IN HERE: the sample library. MIREKU's instruments are .jmi bundles
; in Documents, they are the client's own recorded content, and they run to
; several gigabytes - larger than a GitHub release asset may be. They are
; delivered separately and the app's Settings tab holds the library folder, so a
; tester points MIREKU at wherever the bundles were put. Nothing here writes into
; Documents, so an install or an uninstall never touches them.

#define AppName      "MIREKU"
#define AppVersion   "1.0.3"
#define Publisher    "Amanorsac Studio"

; A TEST build (build-windows-installer.ps1 -Tester) differs in exactly three
; ways, all of them said to the person installing it: its file name, its Welcome
; page and its README. Nothing else about the installer changes.
#ifdef Tester
  #define FileTag    "-Tester"
  #define ReadmeSrc  "README-tester.txt"
  #define LicenceSrc "LICENCE-tester.txt"
#else
  #define FileTag    ""
  #define ReadmeSrc  "README.txt"
  #define LicenceSrc "LICENCE.txt"
#endif

#ifndef BuildDir
  #error Pass /DBuildDir=<folder holding Standalone\ and VST3\>
#endif
#ifndef CrtDir
  #error Pass /DCrtDir=<folder holding msvcp140.dll, vcruntime140.dll ...>
#endif
#ifndef OutDir
  #define OutDir "..\release"
#endif

[Setup]
AppId={{3F1C9A52-7D48-4A16-9C33-2B8E5D4A7F10}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL=https://amanorsac.studio
AppSupportURL=https://amanorsac.studio
AppContact=hello@amanorsac.studio
AppCopyright=Copyright (C) 2026 Amanorsac Studio
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany={#Publisher}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} {#AppVersion} Setup
OutputDir={#OutDir}
OutputBaseFilename=Mireku-{#AppVersion}{#FileTag}-Windows
WizardStyle=modern dark
LicenseFile={#LicenceSrc}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
DefaultDirName={commonpf64}\Amanorsac Studio\MIREKU
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableWelcomePage=no
UsePreviousAppDir=yes
#ifdef Tester
UninstallDisplayName={#AppName} (test build)
#else
UninstallDisplayName={#AppName}
#endif
Compression=lzma2/max
SolidCompression=yes
#ifdef SignCmd
SignTool=studio {#SignCmd}
SignedUninstaller=yes
#endif

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
#ifdef Tester
WelcomeLabel1={#AppName} {#AppVersion} test build
WelcomeLabel2=A test build of MIREKU, a sampled keyboard instrument. It is not the released product.%n%nIt requires the test key sent with it, which MIREKU asks for when it first opens, and it produces no sound once that key expires.%n%nThe sample library is delivered separately. Set its folder under Settings when MIREKU first opens.%n%nWindows asks for permission once, because plug-ins are installed for every user of this computer.
#else
WelcomeLabel1={#AppName} {#AppVersion}
WelcomeLabel2=A sampled keyboard instrument: four voices, an effects rack and a performance stage.%n%nWindows asks for permission once, because plug-ins are installed for every user of this computer.
#endif
WizardLicense=Licence Agreement
LicenseLabel=Read the following before continuing.
LicenseLabel3=MIREKU is covered by the Amanorsac Studio licence agreement at amanorsac.studio/legal. Read the following, then choose whether to accept.
LicenseAccepted=I &accept the agreement
LicenseNotAccepted=I &do not accept
FinishedHeadingLabel={#AppName} is installed
ClickFinish=
ButtonInstall=&Install

[Files]
; ---- documents
Source: "{#ReadmeSrc}";  DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion
; MIT notices for Signalsmith Stretch / signalsmith-linear (the shimmer's pitch
; shifter). The licence requires the notice to accompany every copy.
Source: "THIRD-PARTY-LICENCES.txt"; DestDir: "{app}"; Flags: ignoreversion

; ---- standalone application
Source: "{#BuildDir}\Standalone\MIREKU.exe"; DestDir: "{app}"; Flags: ignoreversion signonce; Check: WantApp
Source: "{#CrtDir}\msvcp140.dll";            DestDir: "{app}"; Flags: ignoreversion; Check: WantApp
Source: "{#CrtDir}\msvcp140_1.dll";          DestDir: "{app}"; Flags: ignoreversion; Check: WantApp
Source: "{#CrtDir}\vcruntime140.dll";        DestDir: "{app}"; Flags: ignoreversion; Check: WantApp
Source: "{#CrtDir}\vcruntime140_1.dll";      DestDir: "{app}"; Flags: ignoreversion; Check: WantApp

; ---- VST3, as a BUNDLE, in the folder hosts scan. Not editable: a VST3
;      anywhere else is a plug-in the DAW never finds.
Source: "{#BuildDir}\VST3\MIREKU.vst3\*"; DestDir: "{commoncf64}\VST3\MIREKU.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: WantVst
Source: "{#CrtDir}\msvcp140.dll";         DestDir: "{commoncf64}\VST3\MIREKU.vst3\Contents\x86_64-win"; Flags: ignoreversion; Check: WantVst
Source: "{#CrtDir}\msvcp140_1.dll";       DestDir: "{commoncf64}\VST3\MIREKU.vst3\Contents\x86_64-win"; Flags: ignoreversion; Check: WantVst
Source: "{#CrtDir}\vcruntime140.dll";     DestDir: "{commoncf64}\VST3\MIREKU.vst3\Contents\x86_64-win"; Flags: ignoreversion; Check: WantVst
Source: "{#CrtDir}\vcruntime140_1.dll";   DestDir: "{commoncf64}\VST3\MIREKU.vst3\Contents\x86_64-win"; Flags: ignoreversion; Check: WantVst

[Icons]
Name: "{autoprograms}\MIREKU"; Filename: "{app}\MIREKU.exe"; Check: WantApp

; Nothing here touches Documents\MIREKU or Documents\JM Signature. The library,
; the presets and the performances are left exactly where they are.

[Code]
var
  CompPage: TWizardPage;
  AppCheck, VstCheck: TNewCheckBox;
  AppDirEdit: TNewEdit;

function WantApp: Boolean; begin Result := AppCheck.Checked; end;
function WantVst: Boolean; begin Result := VstCheck.Checked; end;

procedure AddLine(var S: String; const L: String);
begin
  if S <> '' then S := S + #13#10;
  S := S + L;
end;

procedure InitializeWizard;
var
  Y: Integer;
  L: TNewStaticText;
begin
  CompPage := CreateCustomPage(wpLicense, 'Components',
    'Choose what to install. Every destination is shown.');

  Y := 0;
  AppCheck := TNewCheckBox.Create(CompPage);
  AppCheck.Parent := CompPage.Surface;
  AppCheck.Top := Y;
  AppCheck.Width := CompPage.SurfaceWidth;
  AppCheck.Height := ScaleY(17);
  AppCheck.Caption := 'Standalone application';
  AppCheck.Checked := True;
  Y := Y + ScaleY(21);

  AppDirEdit := TNewEdit.Create(CompPage);
  AppDirEdit.Parent := CompPage.Surface;
  AppDirEdit.Top := Y;
  AppDirEdit.Left := ScaleX(20);
  AppDirEdit.Width := CompPage.SurfaceWidth - ScaleX(20);
  AppDirEdit.Text := ExpandConstant('{commonpf64}\Amanorsac Studio\MIREKU');
  AppDirEdit.ReadOnly := True;
  Y := Y + ScaleY(30);

  VstCheck := TNewCheckBox.Create(CompPage);
  VstCheck.Parent := CompPage.Surface;
  VstCheck.Top := Y;
  VstCheck.Width := CompPage.SurfaceWidth;
  VstCheck.Height := ScaleY(17);
  VstCheck.Caption := 'VST3 plug-in';
  VstCheck.Checked := True;
  Y := Y + ScaleY(21);

  L := TNewStaticText.Create(CompPage);
  L.Parent := CompPage.Surface;
  L.Top := Y;
  L.Left := ScaleX(20);
  L.Width := CompPage.SurfaceWidth - ScaleX(20);
  L.AutoSize := False;
  L.Height := ScaleY(30);
  L.Caption := ExpandConstant('{commoncf64}\VST3\MIREKU.vst3') + #13#10 +
               'Fixed: a VST3 elsewhere is a plug-in no DAW finds.';
  Y := Y + ScaleY(38);

  L := TNewStaticText.Create(CompPage);
  L.Parent := CompPage.Surface;
  L.Top := Y;
  L.Width := CompPage.SurfaceWidth;
  L.AutoSize := False;
  L.Height := ScaleY(46);
  L.WordWrap := True;
  L.Caption := 'The sample library is delivered separately and is not part of this installer. ' +
               'Set its folder under Settings when MIREKU first opens. Installing and uninstalling ' +
               'never touch your library, presets or performances.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = CompPage.ID then
  begin
    if (not AppCheck.Checked) and (not VstCheck.Checked) then
    begin
      MsgBox('Select at least one component to install.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
var
  S: String;
begin
  if CurPageID = wpFinished then
  begin
    S := '';
    if WantApp then AddLine(S, 'Application  ' + ExpandConstant('{app}'));
    if WantVst then AddLine(S, 'VST3         ' + ExpandConstant('{commoncf64}\VST3\MIREKU.vst3'));
    AddLine(S, '');
    AddLine(S, 'Library      set under Settings (delivered separately)');
    WizardForm.FinishedLabel.Caption := S;
  end;
end;
