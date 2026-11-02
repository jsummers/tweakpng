; NSIS script for the TweakPNG installer. The release workflow passes
; /DOUTFILE=<path to the installer> and /DSOURCEDIR=<deployed files>.

Unicode true
!include "MUI2.nsh"

!define APPNAME "TweakPNG"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\TweakPNG"

Name "${APPNAME}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\TweakPNG"
InstallDirRegKey HKCU "Software\TweakPNG" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!define MUI_ICON "tweakpng.ico"
!define MUI_UNICON "tweakpng.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\tweakpng.exe"

!insertmacro MUI_PAGE_LICENSE "..\..\COPYING.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "TweakPNG"
	SetOutPath "$INSTDIR"
	File /r "${SOURCEDIR}\*"
	WriteUninstaller "$INSTDIR\uninstall.exe"
	WriteRegStr HKCU "Software\TweakPNG" "InstallDir" "$INSTDIR"
	WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "TweakPNG"
	WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\tweakpng.exe"
	WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
	WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
	WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
	CreateShortcut "$SMPROGRAMS\TweakPNG.lnk" "$INSTDIR\tweakpng.exe"
SectionEnd

Section "Uninstall"
	Delete "$SMPROGRAMS\TweakPNG.lnk"
	RMDir /r "$INSTDIR"
	DeleteRegKey HKCU "${UNINSTKEY}"
	DeleteRegKey HKCU "Software\TweakPNG"
SectionEnd
