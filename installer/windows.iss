; Inno Setup script: SpaceOpera VST3 installer for Windows (Ableton Live, FL Studio, Cubase, ...)
#define AppVer "1.0.0"

[Setup]
AppId={{9A4C2E11-5B7D-4F3A-8E21-5ACE0BE7A001}
AppName=SpaceOpera
AppVersion={#AppVer}
AppPublisher=RiotAudio
DefaultDirName={autopf}\RiotAudio\SpaceOpera
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=SpaceOpera-{#AppVer}-win-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "..\build-win\SpaceOpera_artefacts\Release\VST3\SpaceOpera.vst3\*"; DestDir: "{commoncf64}\VST3\SpaceOpera.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\SpaceOpera.vst3"
