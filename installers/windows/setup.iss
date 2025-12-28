; Iterum VST3 Plugin Installer
; Inno Setup Script
;
; This script is compiled by the release workflow using Inno Setup
; to create a Windows installer for the Iterum VST3 plugin.
;
; Requirements:
; - FR-010: Use Inno Setup
; - FR-011: Install to C:\Program Files\Common Files\VST3\
; - FR-012: Create uninstaller entry
; - FR-013: Include plugin metadata
; - FR-014: Support silent install (/SILENT)
; - FR-015: Request admin privileges
; - FR-016: Support upgrade installations

#define AppName "Iterum"
#define AppPublisher "Krate Audio"
#define AppURL "https://github.com/rolandzwaga/iterum"
#define AppCopyright "Copyright (c) 2025 Krate Audio"
; Version is passed from the release workflow via /DVersion=x.x.x
#ifndef Version
  #define Version "0.0.0"
#endif

[Setup]
; Unique application ID (GUID) - generated for Iterum
AppId={{8F7C4A2B-3D5E-4F6A-9B8C-1D2E3F4A5B6C}
AppName={#AppName}
AppVersion={#Version}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
AppCopyright={#AppCopyright}

; Installation directory - VST3 standard location
DefaultDirName={commonpf}\Common Files\VST3
; No user-selectable program group needed for a plugin
DisableProgramGroupPage=yes

; Output file naming
OutputDir=.
OutputBaseFilename=Iterum-{#Version}-Windows-x64

; Compression settings
Compression=lzma2
SolidCompression=yes

; Require admin privileges for system-wide installation (FR-015)
PrivilegesRequired=admin

; Allow upgrade installations without uninstall (FR-016)
UsePreviousAppDir=yes

; Installer appearance
WizardStyle=modern
SetupIconFile=compiler:SetupClassicIcon.ico

; Architecture
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Copy the entire Iterum.vst3 bundle recursively
; Source path is relative to where the workflow runs (artifact extraction location)
Source: "Iterum.vst3\*"; DestDir: "{app}\Iterum.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; No desktop icons needed for a VST plugin

[Registry]
; No registry entries needed - VST3 hosts scan the standard directory

[UninstallDelete]
; Clean up the entire plugin directory on uninstall
Type: filesandordirs; Name: "{app}\Iterum.vst3"

[Code]
// Display a message after installation
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Installation complete - no action needed
  end;
end;
