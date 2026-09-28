; Установщик TLing (C++ Win32).
; По умолчанию — БЕЗ языковых моделей (папка portable-lite):
;   powershell -File cpp\package_win32.ps1 -OutputDir cpp\portable-lite
;   ISCC.exe cpp\TLing.iss
; Полная сборка с моделями (папка portable-full, флаг USE_FULL_PORTABLE):
;   powershell -File cpp\package_win32.ps1 -OutputDir cpp\portable-full -IncludeModels
;   ISCC.exe /DUSE_FULL_PORTABLE cpp\TLing.iss
;
; Деинсталлятор удаляет файлы из {app}, включая {app}\data (модели,
; скачанные в каталог программы). Каталог
; %USERPROFILE%\.local\share\offline-translator не трогается.

#define AppName "TLing"
#define AppVersion "0.99-beta"
#define AppPublisher "TAP3AH"
#define AppExeName "TLing.exe"

; Тестовая сборка (ISCC /DRESET_USER_STATE): гасит старые экземпляры,
; удаляет прежние настройки и пишет отчёт в data\install-test.log.
#ifdef RESET_USER_STATE
#define BuildSuffix "-test"
#else
#define BuildSuffix ""
#endif

; Полная сборка с моделями: ISCC /DUSE_FULL_PORTABLE
#ifdef USE_FULL_PORTABLE
#define PortableDir "portable-full"
#define ModelSuffix "-with-models"
#else
#define PortableDir "portable-lite"
#define ModelSuffix ""
#endif

[Setup]
AppId={{c4e8a91f-7b2d-4f15-9e3a-6d8c1b0a2475}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\Programs\TLing
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=installer-output
OutputBaseFilename=tling-0.99-beta{#ModelSuffix}{#BuildSuffix}-setup
SetupIconFile=..\assets\app.ico
LicenseFile=..\LICENSE
UninstallDisplayIcon={app}\{#AppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
; Язык установщика определяется по языку системы; ShowLanguageDialog=yes
; всегда показывает выбор языка (с уже выбранным системным).
ShowLanguageDialog=yes
VersionInfoVersion=0.99.0.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName}
VersionInfoProductName={#AppName}
VersionInfoProductVersion=0.99.0.0

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[CustomMessages]
russian.CreateDesktopIcon=Создать ярлык на рабочем столе
russian.DesktopGroup=Дополнительные значки:
russian.LaunchApp=Запустить {#AppName}
english.CreateDesktopIcon=Create a desktop shortcut
english.DesktopGroup=Additional icons:
english.LaunchApp=Launch {#AppName}
german.CreateDesktopIcon=Desktop-Verkn\u00fcpfung erstellen
german.DesktopGroup=Zus\u00e4tzliche Symbole:
german.LaunchApp={#AppName} starten
french.CreateDesktopIcon=Cr\u00e9er un raccourci sur le Bureau
french.DesktopGroup=Ic\u00f4nes suppl\u00e9mentaires :
french.LaunchApp=Lancer {#AppName}
spanish.CreateDesktopIcon=Crear un acceso directo en el escritorio
spanish.DesktopGroup=Iconos adicionales:
spanish.LaunchApp=Iniciar {#AppName}

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:DesktopGroup}"; Flags: unchecked

[Dirs]
Name: "{app}\data"

[Files]
Source: "{#PortableDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\Удалить {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent

; Только данные внутри каталога установки. Профиль пользователя не удалять.
[UninstallDelete]
Type: filesandordirs; Name: "{app}\data"
Type: dirifempty; Name: "{app}"

[Code]
function UserSettingsPath: String;
var
  HomeEnv: String;
begin
  HomeEnv := GetEnv('OFFLINE_TRANSLATOR_HOME');
  if HomeEnv <> '' then
  begin
    Result := AddBackslash(HomeEnv) + 'settings.json';
    if FileExists(Result) then
      Exit;
  end;
  Result := AddBackslash(GetEnv('USERPROFILE')) +
    '.local\share\offline-translator\settings.json';
end;

function InitializeSetup(): Boolean;
var
  ErrorCode: Integer;
begin
  Result := True;
#ifdef RESET_USER_STATE
  { Тестовая сборка: гасим все прежние экземпляры, чтобы они не
    перехватывали жесты и не показывали старые ошибки. }
  Exec(
    'taskkill.exe',
    '/F /IM {#AppExeName}',
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ErrorCode);
#endif
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  DataDir, AppSettings, ProfileSettings: String;
#ifdef RESET_USER_STATE
  Lines: TArrayOfString;
#endif
begin
  if CurStep <> ssPostInstall then
    Exit;
  DataDir := ExpandConstant('{app}\data');
  if not DirExists(DataDir) then
    ForceDirectories(DataDir);
  AppSettings := AddBackslash(DataDir) + 'settings.json';
  ProfileSettings := UserSettingsPath;
  { Перенос settings.json из профиля, если в data его ещё нет. }
  if FileExists(ProfileSettings) and (not FileExists(AppSettings)) then
    CopyFile(ProfileSettings, AppSettings, False);
#ifdef RESET_USER_STATE
  { Тестовая сборка: убираем прежние настройки и старую автозагрузку,
    чтобы исключить влияние старых версий. }
  DeleteFile(AppSettings);
  DeleteFile(ProfileSettings);
  RegDeleteValue(
    HKCU,
    'Software\Microsoft\Windows\CurrentVersion\Run',
    'TLing');
  SetArrayLength(Lines, 6);
  Lines[0] := GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') +
    ' тестовая сборка {#AppVersion}';
  Lines[1] := 'удалены настройки: ' + AppSettings;
  Lines[2] := 'удалены настройки профиля: ' + ProfileSettings;
  Lines[3] := 'снята автозагрузка TLing';
  Lines[4] := 'старые экземпляры завершены (taskkill)';
  Lines[5] := 'журнал приложения: ' + AddBackslash(DataDir) + 'app.log';
  SaveStringsToFile(AddBackslash(DataDir) + 'install-test.log',
    Lines, False);
#endif
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  RunCommand: String;
  AppDir: String;
begin
  if CurUninstallStep <> usUninstall then
    Exit;
  AppDir := ExpandConstant('{app}');
  if RegQueryStringValue(
       HKCU,
       'Software\Microsoft\Windows\CurrentVersion\Run',
       'TLing',
       RunCommand) then
  begin
    { Снимаем автозагрузку только если она указывает на этот каталог. }
    if Pos(AnsiLowercase(AppDir), AnsiLowercase(RunCommand)) > 0 then
      RegDeleteValue(
        HKCU,
        'Software\Microsoft\Windows\CurrentVersion\Run',
        'TLing');
  end;
end;
