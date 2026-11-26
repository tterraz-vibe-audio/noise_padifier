; Noise Padifier - Windows installer (NSIS 3)
; Copyright (C) 2026 Padifier authors - SPDX-License-Identifier: GPL-3.0-or-later
; Build: makensis installer/padifier.nsi  (from ../build.sh, after the Windows build)

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!define NAME     "Noise Padifier"
!define VERSION  "1.0.1"
!define BUNDLE   "noise_padifier"
!define MANUAL   "Noise-Padifier-manuel.pdf"
!define MANUAL_EN "Noise-Padifier-manual.pdf"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\NoisePadifier"

Name "${NAME} ${VERSION}"
OutFile "../bin/Noise-Padifier-${VERSION}-setup.exe"
RequestExecutionLevel admin
InstallDir "$COMMONFILES64\VST3"
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_LICENSE "../COPYING"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "${NAME} needs 64-bit Windows."
        Abort
    ${EndIf}
    SetRegView 64
FunctionEnd

Function un.onInit
    SetRegView 64
FunctionEnd

Section "VST3" SecVST3
    SectionIn RO
    SetOutPath "$COMMONFILES64\VST3"
    File /r "../bin/windows/${BUNDLE}.vst3"
    SetOutPath "$PROGRAMFILES64\Padifier"
    File "../COPYING"
    File "../THIRD_PARTY_LICENSES.txt"
    WriteUninstaller "$PROGRAMFILES64\Padifier\uninstall.exe"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${NAME}"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "Padifier"
    WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$PROGRAMFILES64\Padifier\uninstall.exe"'
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "CLAP" SecCLAP
    SetOutPath "$COMMONFILES64\CLAP"
    File "../bin/windows/${BUNDLE}.clap"
SectionEnd

; user manuals (English and French, PDF) next to the licence, with Start menu shortcuts for all
; users
Section "User manuals (PDF)" SecManual
    SetShellVarContext all
    SetOutPath "$PROGRAMFILES64\Padifier"
    File "/oname=${MANUAL_EN}" "../doc/manual.pdf"
    File "/oname=${MANUAL}" "../doc/manuel.pdf"
    CreateDirectory "$SMPROGRAMS\${NAME}"
    CreateShortcut "$SMPROGRAMS\${NAME}\${NAME} - manual.lnk" "$PROGRAMFILES64\Padifier\${MANUAL_EN}"
    CreateShortcut "$SMPROGRAMS\${NAME}\${NAME} - manuel.lnk" "$PROGRAMFILES64\Padifier\${MANUAL}"
SectionEnd

Section "Uninstall"
    SetShellVarContext all
    Delete "$SMPROGRAMS\${NAME}\${NAME} - manual.lnk"
    Delete "$SMPROGRAMS\${NAME}\${NAME} - manuel.lnk"
    RMDir "$SMPROGRAMS\${NAME}"
    Delete "$PROGRAMFILES64\Padifier\${MANUAL_EN}"
    Delete "$PROGRAMFILES64\Padifier\${MANUAL}"
    RMDir /r "$COMMONFILES64\VST3\${BUNDLE}.vst3"
    Delete "$COMMONFILES64\CLAP\${BUNDLE}.clap"
    Delete "$PROGRAMFILES64\Padifier\COPYING"
    Delete "$PROGRAMFILES64\Padifier\THIRD_PARTY_LICENSES.txt"
    Delete "$PROGRAMFILES64\Padifier\uninstall.exe"
    RMDir "$PROGRAMFILES64\Padifier"
    DeleteRegKey HKLM "${UNINSTKEY}"
SectionEnd
