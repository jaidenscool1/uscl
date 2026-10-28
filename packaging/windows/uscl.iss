; USCL installer. Built by .github/workflows/windows-installer.yml: iscc /DStage=<folder with USCL's files> uscl.iss
; Installs per user (no admin prompt): RPCS3 keeps its settings next to rpcs3.exe, so Program Files won't do.
#ifndef Stage
  #error Pass /DStage=<staging folder>
#endif

[Setup]
AppId={{7B5F3C1E-0A44-4C55-9E1B-55C1A0D3F0A1}
AppName=USCL
#ifndef Version
  #define Version "0.0.0"
#endif
AppVersion={#Version}
CloseApplications=yes
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
; only offered when missing: online play runs over Steam, party chat over Discord
Name: steam; Description: "Install Steam (needed to play online with friends)"; Check: not SteamInstalled and not WizardSilent
Name: discord; Description: "Install Discord (party and voice chat)"; Check: not DiscordInstalled and not WizardSilent

[Run]
; the Visual C++ runtime RPCS3 needs; asks for admin only when it isn't installed yet
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Visual C++ runtime..."; Flags: waituntilterminated skipifdoesntexist shellexec; Check: NeedsVCRedist
; Valve's and Discord's own installers, downloaded on the Ready page (see NextButtonClick)
Filename: "{tmp}\SteamSetup.exe"; StatusMsg: "Installing Steam..."; Flags: waituntilterminated skipifdoesntexist; Tasks: steam
Filename: "{tmp}\DiscordSetup.exe"; StatusMsg: "Installing Discord..."; Flags: waituntilterminated skipifdoesntexist; Tasks: discord
Filename: "{app}\uscl.exe"; Description: "Start USCL"; Flags: nowait postinstall skipifsilent
; an update from inside USCL installs silently: start the new USCL when it's done
Filename: "{app}\uscl.exe"; Flags: nowait; Check: WizardSilent

[Code]
var
  Downloads: TDownloadWizardPage;

function SteamInstalled: Boolean;
var Exe: String;
begin
  Result := RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamExe', Exe) and FileExists(Exe);
end;

function DiscordInstalled: Boolean;
begin
  Result := FileExists(ExpandConstant('{localappdata}\Discord\Update.exe'));
end;

procedure InitializeWizard;
begin
  Downloads := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), nil);
end;

// Fetch the Steam / Discord installers the player asked for; if that fails, USCL still installs without them
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID <> wpReady) or not (WizardIsTaskSelected('steam') or WizardIsTaskSelected('discord')) then
    Exit;
  Downloads.Clear;
  if WizardIsTaskSelected('steam') then
    Downloads.Add('https://cdn.cloudflare.steamstatic.com/client/installer/SteamSetup.exe', 'SteamSetup.exe', '');
  if WizardIsTaskSelected('discord') then
    Downloads.Add('https://discord.com/api/downloads/distributions/app/installers/latest?channel=stable&platform=win&arch=x64', 'DiscordSetup.exe', '');
  Downloads.Show;
  try
    try
      Downloads.Download;
    except
      SuppressibleMsgBox('Couldn''t download: ' + GetExceptionMessage + #13#10 + 'USCL installs anyway; get Steam / Discord from their websites.', mbError, MB_OK, IDOK);
    end;
  finally
    Downloads.Hide;
  end;
end;

function NeedsVCRedist: Boolean;
var Installed: Cardinal;
begin
  Result := not (RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1));
end;
