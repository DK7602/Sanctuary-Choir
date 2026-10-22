; Inno Setup script for Sanctuary Choir (Windows, 64-bit).
; Built by CI:  iscc /DAppVersion=1.0.0 /DSourceDir=<folder with the plug-in files> SanctuaryChoir.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\Sanctuary-Choir-Windows"
#endif

[Setup]
AppId={{6E0B3C52-9F0D-4C1E-9A5B-5A9C1C0FE7A1}
AppName=Sanctuary Choir
AppVersion={#AppVersion}
AppPublisher=Sanctuary Audio
AppPublisherURL=https://github.com/DK7602/Sanctuary-Choir
DefaultDirName={autopf}\Sanctuary Audio\Sanctuary Choir
DefaultGroupName=Sanctuary Choir
DisableProgramGroupPage=yes
OutputBaseFilename=Sanctuary Choir Setup
OutputDir=..\..\dist
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayName=Sanctuary Choir

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full
Name: "voices"; Description: "Sanctuary Voices: real recorded singers for the choir (13 MB)"; Types: full custom

[Files]
Source: "{#SourceDir}\Sanctuary Choir.vst3\*"; DestDir: "{commoncf64}\VST3\Sanctuary Choir.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Sanctuary Choir.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#SourceDir}\SanctuaryVoices\*"; DestDir: "{commonappdata}\Sanctuary Choir\Voices\SanctuaryVoices"; Components: voices; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Sanctuary Choir"; Filename: "{app}\Sanctuary Choir.exe"; Components: standalone
Name: "{autodesktop}\Sanctuary Choir"; Filename: "{app}\Sanctuary Choir.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the standalone app"; Components: standalone; Flags: unchecked

[Run]
Filename: "{app}\Sanctuary Choir.exe"; Description: "Open Sanctuary Choir now"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked
