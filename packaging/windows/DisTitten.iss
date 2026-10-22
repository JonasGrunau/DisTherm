; Inno Setup script for the Windows installer (VST3).
; Built by .github/workflows/release.yml:
;   iscc /DAppVersion=1.2.0 /DArtefacts=<repo>\build\DisTitten_artefacts\Release /O<out-dir> packaging\windows\DisTitten.iss
;
; Template: save as packaging/windows/DisTitten.iss (release.yml calls it by
; that name). DCF56C5B-1755-4DA6-9948-EBC780C2210B is a fresh GUID per product (`uuidgen`); never reuse
; another product's AppId, Windows would treat both as one app. Inno writes the
; AppId as {{GUID} (the doubled brace is a literal "{"), hence the extra braces
; around the placeholder below.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\DisTitten_artefacts\Release"
#endif

[Setup]
AppId={{DCF56C5B-1755-4DA6-9948-EBC780C2210B}
AppName=DisTitten
AppVersion={#AppVersion}
AppPublisher=Jonas Grunau
DefaultDirName={commoncf64}\VST3\DisTitten.vst3
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputBaseFilename=DisTitten-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
Uninstallable=yes

[Files]
Source: "{#Artefacts}\VST3\DisTitten.vst3\*"; DestDir: "{commoncf64}\VST3\DisTitten.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

; ---------------------------------------------------------------------------
; STANDALONE alternative (PsyBox: "VST3 + Standalone"). Replace the [Setup]
; DefaultDirName and the [Files] section above with the lines below (drop
; Uninstallable=yes, it is the default). The first type is the default, so
; the standalone app starts unticked.
;
; DefaultDirName={autopf}\DisTitten
;
; [Types]
; Name: "plugin"; Description: "VST3 plug-in"
; Name: "full"; Description: "VST3 plug-in and standalone app"
; Name: "custom"; Description: "Custom"; Flags: iscustom
;
; [Components]
; Name: "vst3"; Description: "VST3 plug-in (Common Files\VST3)"; Types: plugin full
; Name: "standalone"; Description: "Standalone app"; Types: full
;
; [Files]
; Source: "{#Artefacts}\VST3\DisTitten.vst3\*"; DestDir: "{commoncf64}\VST3\DisTitten.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
; Source: "{#Artefacts}\Standalone\DisTitten.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
;
; [Icons]
; Name: "{autoprograms}\DisTitten"; Filename: "{app}\DisTitten.exe"; Components: standalone
