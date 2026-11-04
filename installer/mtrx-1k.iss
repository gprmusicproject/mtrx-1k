; Setup of MTRX-1K for Windows, made with Inno Setup 6.
;
;   ISCC.exe mtrx-1k.iss
;
; What it does
;   - the standalone app and the user manual go to Program Files\GPR Music Project\MTRX-1K
;   - the VST3 plug-in goes to Program Files\Common Files\VST3\GPR Music Project
;   - the data folders ROM, Patch and MIDI are made in the Documents folder of the user who
;     started Setup, empty but for the bank GPR Music Project Signature in Patch, which is
;     copied only when no file of that name is there. No firmware is part of the package.
;     The bank is not part of the source: it goes in only when the file is in the root of
;     the checkout as the setup is built: it comes with the compiled package only.
;   - MTRX-1K is listed among the installed apps of Windows and removed from there; that
;     entry is the only thing written to the registry
;   - on removal the user is asked whether MIDI profiles and sounds go too
;
; Defines
;   /DTestRoot=<folder>   builds a setup that needs no administrator and installs under that
;                         folder instead of the real places: for trying it out
;   /DBuildDir=<folder>   where the built app and plug-in are
;   /DManualPdf=<file>    the user manual

#define AppName      "MTRX-1K"
#define AppVersion   "1.0.0"
#define AppNumber    "1.0.0"
#define AppFileVersion "1.0.0"
#define Publisher    "GPR Music Project"
#define ManualName   "MTRX-1K User Manual.pdf"
#define NoteName     "Put your firmware here - read me.txt"
#define BankName     "GPR Music Project Signature.syx"
#if FileExists(AddBackslash(SourcePath) + "..\" + BankName)
  #define HaveBank
#endif
#define DataSub      "\Documents\GPR Music Project\MTRX-1K"

#ifndef BuildDir
  #define BuildDir "..\build\win-release\Retromulator_artefacts\Release"
#endif
#ifndef ManualPdf
  #define ManualPdf "..\MTRX-1K_User_Manual_v" + AppFileVersion + ".pdf"
#endif

#ifdef TestRoot
  #define AppDir    TestRoot + "\Program Files\GPR Music Project\MTRX-1K"
  #define Vst3Dir   TestRoot + "\Program Files\Common Files\VST3\GPR Music Project"
  #define Vst3Root  TestRoot + "\Program Files\Common Files\VST3"
  #define MenuDir   TestRoot + "\Start Menu\MTRX-1K"
#else
  #define AppDir    "{autopf}\GPR Music Project\MTRX-1K"
  #define Vst3Dir   "{commoncf}\VST3\GPR Music Project"
  #define Vst3Root  "{commoncf}\VST3"
  #define MenuDir   "{group}"
#endif

[Setup]
#ifdef TestRoot
; the try-out has an entry of its own in the list of installed apps, in the user's part
AppId=GPRMusicProject.MTRX-1K.Test
UninstallDisplayName={#AppName} (test build)
#else
AppId=GPRMusicProject.MTRX-1K
UninstallDisplayName={#AppName}
#endif
UninstallDisplayIcon={app}\MTRX-1K.exe
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
VersionInfoVersion={#AppNumber}
VersionInfoCompany={#Publisher}
VersionInfoDescription={#AppName} Setup
VersionInfoProductName={#AppName}
DefaultDirName={#AppDir}
DefaultGroupName={#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
DisableWelcomePage=no
AllowNoIcons=no
#ifdef TestRoot
PrivilegesRequired=lowest
#else
PrivilegesRequired=admin
#endif
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; the entry in the list of installed apps is all that goes to the registry
CreateUninstallRegKey=yes
UsePreviousAppDir=no
UsePreviousGroup=no
UsePreviousSetupType=no
UsePreviousTasks=no
UsePreviousLanguage=no
ChangesAssociations=no
ChangesEnvironment=no
CloseApplications=no
RestartApplications=no
Uninstallable=yes
UninstallFilesDir={app}
; the data folders are the user's on purpose
UsedUserAreasWarning=no
SetupIconFile=images\mtrx-1k.ico
WizardStyle=modern
WizardSizePercent=130,140
WizardImageFile=images\wizard_side.bmp
WizardSmallImageFile=images\wizard_small.bmp
WizardImageStretch=yes
ShowLanguageDialog=auto
Compression=lzma2/max
SolidCompression=yes
OutputDir=..\build\installer\out
#ifdef TestRoot
OutputBaseFilename=MTRX-1K_{#AppFileVersion}_Setup_test
#else
OutputBaseFilename=MTRX-1K_{#AppFileVersion}_Setup
#endif

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

[Messages]
#ifdef HaveBank
en.WelcomeLabel2=This will install [name/ver]: the standalone app with its user manual, the VST3 plug-in and GPR Music Project Signature, a bank of 41 sounds by GPR Music Project.%n%nClose your host before you go on.
it.WelcomeLabel2=Verrà installato [name/ver]: l'applicazione standalone con il manuale utente, il plug-in VST3 e GPR Music Project Signature, un banco di 41 suoni di GPR Music Project.%n%nChiudi il tuo host prima di continuare.
#else
en.WelcomeLabel2=This will install [name/ver]: the standalone app with its user manual, and the VST3 plug-in.%n%nClose your host before you go on.
it.WelcomeLabel2=Verrà installato [name/ver]: l'applicazione standalone con il manuale utente e il plug-in VST3.%n%nChiudi il tuo host prima di continuare.
#endif

[CustomMessages]
en.ReadyApp=Standalone app and user manual
en.ReadyVst3=VST3 plug-in
#ifdef HaveBank
en.ReadyData=Folders for your firmware, sounds and MIDI profiles, with the bank GPR Music Project Signature (what is there stays)
#else
en.ReadyData=Folders for your firmware, sounds and MIDI profiles (made empty, what is there stays)
#endif
en.ReadyRemove=To remove MTRX-1K: Windows Settings, Apps, Installed apps.
en.OpenManual=Open the user manual
en.StartApp=Start MTRX-1K
en.MenuManual=MTRX-1K User Manual
en.MenuUninstall=Uninstall MTRX-1K
en.InUse=MTRX-1K is in use. Close the standalone app and your host, then start Setup again.%n%nFile in use:%n%1
en.InUseUninstall=MTRX-1K is in use. Close the standalone app and your host, then start the removal again.%n%nFile in use:%n%1
en.OldCopy=Another copy of the plug-in was found in%n%1%n%nA host must not find two copies of MTRX-1K. Remove that copy now?
en.RemoveData=Do you also want to remove your MIDI mappings and your own sounds?%n%nYes: the folders MIDI and Patch in%n%1%nare deleted, with your MIDI profiles, your banks and the patches you saved, and your favourites. This cannot be undone.%n%nNo: they are kept for a later installation.%n%nThe firmware files in the folder ROM are kept in both cases.
it.ReadyApp=Applicazione standalone e manuale utente
it.ReadyVst3=Plug-in VST3
#ifdef HaveBank
it.ReadyData=Cartelle per firmware, suoni e profili MIDI, con il banco GPR Music Project Signature (quello che c'è resta)
#else
it.ReadyData=Cartelle per firmware, suoni e profili MIDI (create vuote, quello che c'è resta)
#endif
it.ReadyRemove=Per rimuovere MTRX-1K: Impostazioni di Windows, App, App installate.
it.OpenManual=Apri il manuale utente
it.StartApp=Avvia MTRX-1K
it.MenuManual=Manuale utente di MTRX-1K
it.MenuUninstall=Disinstalla MTRX-1K
it.InUse=MTRX-1K è in uso. Chiudi l'applicazione standalone e il tuo host, poi avvia di nuovo il Setup.%n%nFile in uso:%n%1
it.InUseUninstall=MTRX-1K è in uso. Chiudi l'applicazione standalone e il tuo host, poi avvia di nuovo la rimozione.%n%nFile in uso:%n%1
it.OldCopy=È stata trovata un'altra copia del plug-in in%n%1%n%nUn host non deve trovare due copie di MTRX-1K. Rimuovere ora quella copia?
it.RemoveData=Vuoi rimuovere anche i tuoi mapping MIDI e i tuoi suoni personalizzati?%n%nSì: le cartelle MIDI e Patch in%n%1%nvengono cancellate, con i profili MIDI, i banchi e le patch che hai salvato, e i tuoi preferiti. L'operazione non si può annullare.%n%nNo: restano per una prossima installazione.%n%nI file del firmware nella cartella ROM restano in entrambi i casi.

[Files]
Source: "{#BuildDir}\Standalone\MTRX-1K.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ManualPdf}"; DestDir: "{app}"; DestName: "{#ManualName}"; Flags: ignoreversion
Source: "..\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "third_party_notices.txt"; DestDir: "{app}"; DestName: "Third-party notices.txt"; Flags: ignoreversion
Source: "firmware_note.txt"; DestDir: "{app}"; DestName: "{#NoteName}"; Flags: ignoreversion
#ifdef HaveBank
Source: "..\{#BankName}"; DestDir: "{app}"; Flags: ignoreversion
#endif
Source: "{#BuildDir}\VST3\MTRX-1K.vst3\*"; DestDir: "{#Vst3Dir}\MTRX-1K.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
; the picture of the plugin on the first page of the wizard
Source: "images\plugin_100.bmp"; Flags: dontcopy
Source: "images\plugin_150.bmp"; Flags: dontcopy
Source: "images\plugin_200.bmp"; Flags: dontcopy

[Icons]
Name: "{#MenuDir}\{#AppName}"; Filename: "{app}\MTRX-1K.exe"
Name: "{#MenuDir}\{cm:MenuManual}"; Filename: "{app}\{#ManualName}"
Name: "{#MenuDir}\{cm:MenuUninstall}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#ManualName}"; Description: "{cm:OpenManual}"; Flags: postinstall shellexec skipifsilent unchecked nowait
Filename: "{app}\MTRX-1K.exe"; Description: "{cm:StartApp}"; Flags: postinstall skipifsilent unchecked nowait

[Code]
var
  PluginImage: TBitmapImage;

// The folder the plugin keeps its data in: Documents of the profile, by its plain path, as
// the plugin itself works it out.
function DataDir: String;
begin
#ifdef TestRoot
  Result := '{#TestRoot}\Profile{#DataSub}';
#else
  Result := GetEnv('USERPROFILE') + '{#DataSub}';
#endif
end;

function IsLocked(const FileName: String): Boolean;
var
  Stream: TFileStream;
begin
  Result := False;
  if not FileExists(FileName) then
    Exit;
  try
    Stream := TFileStream.Create(FileName, fmOpenReadWrite or fmShareExclusive);
    Stream.Free;
  except
    Result := True;
  end;
end;

// The first of the installed binaries that is open in a running program, or nothing.
function BinaryInUse: String;
var
  Names: array [0..1] of String;
  I: Integer;
begin
  Result := '';
  Names[0] := ExpandConstant('{app}\MTRX-1K.exe');
  Names[1] := ExpandConstant('{#Vst3Dir}\MTRX-1K.vst3\Contents\x86_64-win\MTRX-1K.vst3');
  for I := 0 to 1 do
    if IsLocked(Names[I]) then
    begin
      Result := Names[I];
      Exit;
    end;
end;

// ── the wizard ──

procedure InitializeWizard;
var
  Name: String;
  Room, W, H: Integer;
begin
  // the picture of the plugin, at the size that suits the scaling of the screen
  if ScaleX(100) >= 190 then
    Name := 'plugin_200.bmp'
  else if ScaleX(100) >= 140 then
    Name := 'plugin_150.bmp'
  else
    Name := 'plugin_100.bmp';
  ExtractTemporaryFile(Name);

  PluginImage := TBitmapImage.Create(WizardForm);
  PluginImage.Parent := WizardForm.WelcomePage;
  PluginImage.Bitmap.LoadFromFile(ExpandConstant('{tmp}\' + Name));
  PluginImage.Stretch := True;
  // as wide as the text over it, unless that leaves the text less than a few lines
  W := WizardForm.WelcomeLabel2.Width;
  H := W * PluginImage.Bitmap.Height div PluginImage.Bitmap.Width;
  Room := WizardForm.WelcomePage.ClientHeight - WizardForm.WelcomeLabel2.Top - ScaleY(96);
  if H > Room then
  begin
    H := Room;
    W := H * PluginImage.Bitmap.Width div PluginImage.Bitmap.Height;
  end;
  PluginImage.Width := W;
  PluginImage.Height := H;
  PluginImage.Left := WizardForm.WelcomeLabel2.Left + (WizardForm.WelcomeLabel2.Width - W) div 2;
  PluginImage.Top := WizardForm.WelcomePage.ClientHeight - H - ScaleY(14);
  WizardForm.WelcomeLabel2.Height := PluginImage.Top - WizardForm.WelcomeLabel2.Top - ScaleY(8);
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := CustomMessage('ReadyApp') + ':' + NewLine + Space + ExpandConstant('{app}') + NewLine + NewLine
          + CustomMessage('ReadyVst3') + ':' + NewLine + Space + ExpandConstant('{#Vst3Dir}') + NewLine + NewLine
          + CustomMessage('ReadyData') + ':' + NewLine + Space + DataDir + NewLine + NewLine
          + CustomMessage('ReadyRemove');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  InUse: String;
begin
  Result := '';
  InUse := BinaryInUse;
  if InUse <> '' then
    Result := FmtMessage(CustomMessage('InUse'), [InUse]);
end;

// The data folders are made by the user who started Setup, also when an administrator of
// another name lent the rights: the command runs as that user, and cmd puts in the profile.
procedure CreateDataFolders;
var
  Base, Note, Bank, Command: String;
  ResultCode: Integer;
begin
#ifdef TestRoot
  Base := '{#TestRoot}\Profile{#DataSub}';
#else
  Base := '%USERPROFILE%{#DataSub}';
#endif
  Note := ExpandConstant('{app}\{#NoteName}');
  // the bank goes in once: a copy the user changed, or saved patches into, is left alone
  Bank := ExpandConstant('{app}\{#BankName}');
  Command := '/s /c "(if not exist "' + Base + '\ROM" mkdir "' + Base + '\ROM")'
           + ' & (if not exist "' + Base + '\Patch" mkdir "' + Base + '\Patch")'
           + ' & (if not exist "' + Base + '\MIDI" mkdir "' + Base + '\MIDI")'
           + ' & (if not exist "' + Base + '\ROM\{#NoteName}" copy /y "' + Note + '" "' + Base + '\ROM\" >nul)'
           + ' & (if not exist "' + Base + '\Patch\{#BankName}" copy /y "' + Bank + '" "' + Base + '\Patch\" >nul)"';
  ExecAsOriginalUser(ExpandConstant('{cmd}'), Command, '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Old: String;
begin
  if CurStep = ssInstall then
  begin
    // a copy put by hand straight into the VST3 folder would be found next to the new one
    Old := ExpandConstant('{#Vst3Root}\MTRX-1K.vst3');
    if DirExists(Old) or FileExists(Old) then
      if SuppressibleMsgBox(FmtMessage(CustomMessage('OldCopy'), [Old]), mbConfirmation, MB_YESNO, IDNO) = IDYES then
        DelTree(Old, True, True, True);
  end;
  if CurStep = ssPostInstall then
    CreateDataFolders;
end;

// ── removal ──

var
  RemoveData: Boolean;
  BankUnchanged: Boolean;

function HasFiles(const Folder: String): Boolean;
var
  Rec: TFindRec;
begin
  Result := False;
  if FindFirst(Folder + '\*', Rec) then
  begin
    try
      repeat
        if (Rec.Name <> '.') and (Rec.Name <> '..') then
          Result := True;
      until Result or not FindNext(Rec);
    finally
      FindClose(Rec);
    end;
  end;
end;

function InitializeUninstall: Boolean;
var
  InUse: String;
begin
  Result := True;
  RemoveData := False;
  InUse := BinaryInUse;
  if InUse <> '' then
  begin
    MsgBox(FmtMessage(CustomMessage('InUseUninstall'), [InUse]), mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Data, Tail: String;
begin
  Data := DataDir;
  if CurUninstallStep = usUninstall then
  begin
    // the bank goes with the app only as Setup put it; once changed it is the user's
    BankUnchanged := FileExists(Data + '\Patch\{#BankName}') and FileExists(ExpandConstant('{app}\{#BankName}'))
      and (GetSHA256OfFile(Data + '\Patch\{#BankName}') = GetSHA256OfFile(ExpandConstant('{app}\{#BankName}')));
    // asked after the removal itself was confirmed; a silent removal keeps the data unless told
    Tail := Uppercase(GetCmdTail);
    if Pos('/REMOVEDATA=1', Tail) > 0 then
      RemoveData := True
    else if (Pos('/REMOVEDATA=0', Tail) = 0) and (not UninstallSilent)
         and (HasFiles(Data + '\MIDI') or HasFiles(Data + '\Patch')) then
      RemoveData := MsgBox(FmtMessage(CustomMessage('RemoveData'), [Data]), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
  end;
  if CurUninstallStep = usPostUninstall then
  begin
    DeleteFile(Data + '\ROM\{#NoteName}');
    if BankUnchanged then
      DeleteFile(Data + '\Patch\{#BankName}');
    if RemoveData then
    begin
      DelTree(Data + '\MIDI', True, True, True);
      DelTree(Data + '\Patch', True, True, True);
      DeleteFile(Data + '\settings.xml');
      DeleteFile(Data + '\presets.xml');
    end;
    // what is left empty goes; RemoveDir leaves alone a folder with anything in it
    RemoveDir(Data + '\MIDI');
    RemoveDir(Data + '\Patch');
    RemoveDir(Data + '\ROM');
    RemoveDir(Data);
    RemoveDir(ExtractFileDir(Data));
    RemoveDir(ExpandConstant('{#Vst3Dir}'));
    RemoveDir(ExtractFileDir(ExpandConstant('{app}')));
  end;
end;
