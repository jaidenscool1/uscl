; USCL installer. Built by .github/workflows/windows-installer.yml: iscc /DStage=<folder with USCL's files> uscl.iss
; Installs per user (no admin prompt): RPCS3 keeps its settings next to rpcs3.exe, so Program Files won't do.
#ifndef Stage
  #error Pass /DStage=<staging folder>
#endif

[Setup]
AppId={{7B5F3C1E-0A44-4C55-9E1B-55C1A0D3F0A1}
AppName=USCL
AppVersion=0.1
AppPublisher=USCL
DefaultDirName={localappdata}\Programs\USCL
DefaultGroupName=USCL
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=USCL-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\rpcs3.exe

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe"; Flags: recursesubdirs ignoreversion
Source: "{#Stage}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\USCL"; Filename: "{app}\uscl.exe"; IconFilename: "{app}\rpcs3.exe"
Name: "{autoprograms}\USCL (classic window)"; Filename: "{app}\rpcs3.exe"
Name: "{autodesktop}\USCL"; Filename: "{app}\uscl.exe"; IconFilename: "{app}\rpcs3.exe"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"

[Run]
; the Visual C++ runtime RPCS3 needs; asks for admin only when it isn't installed yet
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Visual C++ runtime..."; Flags: waituntilterminated skipifdoesntexist shellexec; Check: NeedsVCRedist
Filename: "{app}\uscl.exe"; Description: "Start USCL"; Flags: nowait postinstall skipifsilent

[Code]
function NeedsVCRedist: Boolean;
var Installed: Cardinal;
begin
  Result := not (RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1));
end;
