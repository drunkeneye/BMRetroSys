//
// atarist_bmx_stubs.c
//
// Link-time stubs for Hatari subsystems excluded from the BMX build
// (Falcon VIDEL/DSP/crossbar, ACSI/SCSI/IDE hard disks, IPF CapsLib,
// microphone). Plain C: Hatari headers are C-only. All entries are
// unreachable on ST/STE/MegaSTE machine types: call sites are either
// Falcon/TT-gated at runtime or belong to excluded dialog/HD flows.
// Signatures mirror the vendored headers exactly; boolean/pointer/
// integer defaults are safe no-op values.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "main.h"
#include "configuration.h"
#include "falcon/videl.h"
#include "falcon/dsp.h"
#include "falcon/crossbar.h"
#include "hdc.h"
#include "ide.h"
#include "ncr5380.h"
#include "floppies/ipf.h"
#include "microphone.h"

// --- falcon/videl.h ---
bool VIDEL_renderScreen(void) { return false; }
void Videl_Init(void) {}
void VIDEL_reset(void) {}
void Videl_ScreenModeChanged(bool bForceChange) {}
void VIDEL_UpdateColors(void) {}
void VIDEL_RestartVideoCounter(void) {}
void VIDEL_VideoRasterHBL(void) {}
int VIDEL_Get_VFreq(void) { return 0; }
bool VIDEL_Use_STShifter(void) { return true; }  // ST modes always use the ST shifter
void VIDEL_Monitor_WriteByte(void) {}
void VIDEL_SyncMode_WriteByte(void) {}
void VIDEL_ScreenBase_WriteByte(void) {}
void VIDEL_ScreenCounter_ReadByte(void) {}
void VIDEL_ScreenCounter_WriteByte(void) {}
void VIDEL_StColorRegsWrite(void) {}
void VIDEL_FalconColorRegsWrite(void) {}
void VIDEL_LineOffset_WriteWord(void) {}
void VIDEL_LineOffset_ReadWord(void) {}
void VIDEL_Line_Width_WriteWord(void) {}
void VIDEL_HorScroll64_WriteByte(void) {}
void VIDEL_HorScroll65_WriteByte(void) {}
void VIDEL_ST_ShiftModeWriteByte(void) {}
void VIDEL_Falcon_ShiftMode_WriteWord(void) {}
void VIDEL_HHC_WriteWord(void) {}
void VIDEL_HHT_WriteWord(void) {}
void VIDEL_HBB_WriteWord(void) {}
void VIDEL_HBE_WriteWord(void) {}
void VIDEL_HDB_WriteWord(void) {}
void VIDEL_HDE_WriteWord(void) {}
void VIDEL_HSS_WriteWord(void) {}
void VIDEL_HFS_WriteWord(void) {}
void VIDEL_HEE_WriteWord(void) {}
void VIDEL_VFC_ReadWord(void) {}
void VIDEL_VFT_WriteWord(void) {}
void VIDEL_VBB_WriteWord(void) {}
void VIDEL_VBE_WriteWord(void) {}
void VIDEL_VDB_WriteWord(void) {}
void VIDEL_VDE_WriteWord(void) {}
void VIDEL_VSS_WriteWord(void) {}
void VIDEL_VCO_WriteWord(void) {}
void VIDEL_VMD_WriteWord(void) {}
void Videl_Color0_WriteWord(void) {}
void Videl_Color1_WriteWord(void) {}
void Videl_Color2_WriteWord(void) {}
void Videl_Color3_WriteWord(void) {}
void Videl_Color4_WriteWord(void) {}
void Videl_Color5_WriteWord(void) {}
void Videl_Color6_WriteWord(void) {}
void Videl_Color7_WriteWord(void) {}
void Videl_Color8_WriteWord(void) {}
void Videl_Color9_WriteWord(void) {}
void Videl_Color10_WriteWord(void) {}
void Videl_Color11_WriteWord(void) {}
void Videl_Color12_WriteWord(void) {}
void Videl_Color13_WriteWord(void) {}
void Videl_Color14_WriteWord(void) {}
void Videl_Color15_WriteWord(void) {}
void VIDEL_InterruptHandler_HalfLine(void) {}
void VIDEL_MemorySnapShot_Capture(bool bSave) {}
void Videl_Info(FILE *fp, uint32_t dummy) {}

// --- falcon/dsp.h ---
bool bDspEnabled;
bool bDspHostInterruptPending;
uint64_t DSP_CyclesGlobalClockCounter;
bool DSP_ProcessIRQ(void) { return false; }
void DSP_Init(void) {}
void DSP_UnInit(void) {}
void DSP_Reset(void) {}
void DSP_Enable(void) {}
void DSP_Disable(void) {}
void DSP_Run(int nHostCycles) {}
void DSP_MemorySnapShot_Capture(bool bSave) {}
void DSP_SetDebugging(bool enabled) {}
uint16_t DSP_GetPC(void) { return 0; }
uint16_t DSP_GetNextPC(uint16_t pc) { return 0; }
uint16_t DSP_GetInstrCycles(void) { return 0; }
uint32_t DSP_ReadMemory(uint16_t addr, char space, const char **mem_str) { return 0; }
uint16_t DSP_DisasmMemory(FILE *fp, uint16_t dsp_memdump_addr, uint16_t dsp_memdump_upper, char space) { return 0; }
uint16_t DSP_DisasmAddress(FILE *out, uint16_t lowerAdr, uint16_t UpperAdr) { return 0; }
void DSP_Info(FILE *fp, uint32_t dummy) {}
void DSP_DisasmRegisters(FILE *fp) {}
int DSP_GetRegisterAddress(const char *arg, uint32_t **addr, uint32_t *mask) { return 0; }
bool DSP_Disasm_SetRegister(const char *arg, uint32_t value) { return false; }
uint32_t DSP_SsiReadTxValue(void) { return 0; }
void DSP_SsiWriteRxValue(uint32_t value) {}
void DSP_SsiReceive_SC0(void) {}
void DSP_SsiReceive_SC1(uint32_t value) {}
void DSP_SsiReceive_SC2(uint32_t value) {}
void DSP_SsiReceive_SCK(void) {}
void DSP_SsiTransmit_SC0(void) {}
void DSP_SsiTransmit_SC1(void) {}
void DSP_SsiTransmit_SC2(uint32_t frame) {}
void DSP_SsiTransmit_SCK(void) {}
void DSP_HandleReadAccess(void) {}
void DSP_HandleWriteAccess(void) {}
uint8_t DSP_GetHREQ(void) { return 0; }
int DSP_ProcessIACK(void) { return 0; }

// --- falcon/crossbar.h ---
uint16_t nCbar_DmaSoundControl;
void Crossbar_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate) {}
void Crossbar_Recalculate_Clocks_Cycles(void) {}
void Crossbar_Reset(bool bCold) {}
void Crossbar_MemorySnapShot_Capture(bool bSave) {}
void Crossbar_BufferInter_WriteByte(void) {}
void Crossbar_DmaCtrlReg_WriteByte(void) {}
void Crossbar_FrameStartHigh_ReadByte(void) {}
void Crossbar_FrameStartHigh_WriteByte(void) {}
void Crossbar_FrameStartMed_ReadByte(void) {}
void Crossbar_FrameStartMed_WriteByte(void) {}
void Crossbar_FrameStartLow_ReadByte(void) {}
void Crossbar_FrameStartLow_WriteByte(void) {}
void Crossbar_FrameCountHigh_ReadByte(void) {}
void Crossbar_FrameCountHigh_WriteByte(void) {}
void Crossbar_FrameCountMed_ReadByte(void) {}
void Crossbar_FrameCountMed_WriteByte(void) {}
void Crossbar_FrameCountLow_ReadByte(void) {}
void Crossbar_FrameEndHigh_ReadByte(void) {}
void Crossbar_FrameCountLow_WriteByte(void) {}
void Crossbar_FrameEndHigh_WriteByte(void) {}
void Crossbar_FrameEndMed_ReadByte(void) {}
void Crossbar_FrameEndMed_WriteByte(void) {}
void Crossbar_FrameEndLow_ReadByte(void) {}
void Crossbar_FrameEndLow_WriteByte(void) {}
void Crossbar_DmaTrckCtrl_WriteByte(void) {}
void Crossbar_SoundModeCtrl_WriteByte(void) {}
void Crossbar_SrcControler_WriteWord(void) {}
void Crossbar_DstControler_WriteWord(void) {}
void Crossbar_FreqDivExt_WriteByte(void) {}
void Crossbar_FreqDivInt_WriteByte(void) {}
void Crossbar_TrackRecSelect_WriteByte(void) {}
void Crossbar_CodecInput_WriteByte(void) {}
void Crossbar_AdcInput_WriteByte(void) {}
void Crossbar_InputAmp_WriteByte(void) {}
void Crossbar_OutputReduct_WriteWord(void) {}
void Crossbar_CodecStatus_WriteWord(void) {}
uint8_t Crossbar_Get_SNDINT_Line(void) { return 0; }
void Crossbar_Microwire_WriteWord(void) {}
void Crossbar_InterruptHandler_25Mhz(void) {}
void Crossbar_InterruptHandler_32Mhz(void) {}
void Crossbar_InterruptHandler_Microwire(void) {}
void Crossbar_Info(FILE *fp, uint32_t dummy) {}

// --- hdc.h ---
int nAcsiPartitions;
bool bAcsiEmuOn;
bool HDC_Init(void) { return false; }
void HDC_UnInit(void) {}
int HDC_InitDevice(const char *hdtype, SCSI_DEV *dev, CNF_SCSIDEV *conf) { return 0; }
void HDC_UnInitDevice(SCSI_DEV *dev) {}
void HDC_ResetCommandStatus(void) {}
short int HDC_ReadCommandByte(int addr) { return 0; }
void HDC_WriteCommandByte(int addr, uint8_t byte) {}
int HDC_PartitionCount(FILE *fp, const uint64_t tracelevel, int *pIsByteSwapped) { return 0; }
off_t HDC_CheckAndGetSize(const char *hdtype, const char *filename, unsigned long blockSize) { return 0; }
bool HDC_WriteCommandPacket(SCSI_CTRLR *ctr, uint8_t b) { return false; }
void HDC_DmaTransfer(void) {}
void HDC_ACSI_InterruptHandler_Update( void ) {}

// --- ide.h ---
int nIDEPartitions;
void Ide_Init(void) {}
void Ide_UnInit(void) {}
bool Ide_IsAvailable(void) { return false; }
bool Ide_IsValid(uaecptr addr) { return false; }
uae_u32 REGPARAM3 Ide_Mem_bget(uaecptr addr) { return 0; }
uae_u32 REGPARAM3 Ide_Mem_wget(uaecptr addr) { return 0; }
uae_u32 REGPARAM3 Ide_Mem_lget(uaecptr addr) { return 0; }
void REGPARAM3 Ide_Mem_bput(uaecptr addr, uae_u32 val) { (void)addr; (void)val; }
void REGPARAM3 Ide_Mem_wput(uaecptr addr, uae_u32 val) { (void)addr; (void)val; }
void REGPARAM3 Ide_Mem_lput(uaecptr addr, uae_u32 val) { (void)addr; (void)val; }

// --- ncr5380.h ---
int nScsiPartitions;
bool bScsiEmuOn;

// --- floppies/ipf.h ---
void IPF_MemorySnapShot_Capture(bool bSave) {}
bool IPF_FileNameIsIPF(const char *pszFileName, bool bAllowGZ) { return false; }
uint8_t * IPF_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType) { return NULL; }
bool IPF_WriteDisk(int Drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize) { return false; }
bool IPF_Init( void ) { return true; }  // no CAPS lib: ST/MSA only
void IPF_Exit( void ) {}
bool IPF_Insert( int Drive , uint8_t *pImageBuffer , long ImageSize ) { return false; }
bool IPF_Eject( int Drive ) { return false; }
void IPF_Reset( bool bCold ) {}
void IPF_Drive_Set_Enable( int Drive , bool value ) {}
void IPF_Drive_Set_DoubleSided( int Drive , bool value ) {}
void IPF_SetDriveSide( uint8_t io_porta_old , uint8_t io_porta_new ) {}
void IPF_FDC_WriteReg( uint8_t Reg , uint8_t Byte ) {}
uint8_t IPF_FDC_ReadReg( uint8_t Reg ) { return 0; }
void IPF_FDC_StatusBar( uint8_t *pCommand , uint8_t *pHead , uint8_t *pTrack , uint8_t *pSector , uint8_t *pSide ) {}
void IPF_Emulate( void ) {}
int FDC_GetBytesPerTrack_IPF( uint8_t Drive , uint8_t Track , uint8_t Side ) { return 0; }
uint32_t FDC_GetCyclesPerRev_FdcCycles_IPF( uint8_t Drive , uint8_t Track , uint8_t Side ) { return 0; }

// --- microphone.h ---
bool Microphone_Start(int sampleRate) { return false; }
void Microphone_Stop(void) {}

// --- includes/ncr5380.h (bare declarations, no extern keyword) ---
bool Ncr5380_Init(void) { return false; }
void Ncr5380_UnInit(void) {}
void Ncr5380_Reset(void) {}
void Ncr5380_WriteByte(int addr, uint8_t byte) { (void)addr; (void)byte; }
uint8_t Ncr5380_ReadByte(int addr) { (void)addr; return 0; }
void Ncr5380_DmaTransfer_Falcon(void) {}
void Ncr5380_IoMemTT_WriteByte(void) {}
void Ncr5380_IoMemTT_ReadByte(void) {}
void Ncr5380_TT_DMA_Ctrl_WriteWord(void) {}

// --- falcon/crossbar.h (bare declarations) ---
void Crossbar_Compute_Ratio(void) {}
void Crossbar_DmaPlayInHandShakeMode(void) {}
void Crossbar_DmaRecordInHandShakeMode_Frame(uint32_t frame) { (void)frame; }
void Crossbar_EnqueueMicrophoneData(int16_t *micro_buffer, uint32_t microBuffer_size) {
  (void)micro_buffer;
  (void)microBuffer_size;
}

// --- cpu disassembler options (debugger-only, see debug/68kDisass.h) ---
int Disasm_GetOptions(void) { return 0; }
void Disasm_Init(void) {}

// --- file archives (unsupported: HAVE_LIBARCHIVE off) ---
bool Archive_FileNameIsSupported(const char *FileName) {
  (void)FileName;
  return false;
}
uint8_t *Archive_ReadDisk(int Drive, const char *FileName,
                          const char *ArchivePath, long *pImageSize,
                          int *pImageType) {
  (void)Drive;
  (void)FileName;
  (void)ArchivePath;
  (void)pImageSize;
  (void)pImageType;
  return NULL;
}
bool Archive_WriteDisk(int Drive, const char *FileName, unsigned char *pBuffer,
                       int ImageSize) {
  (void)Drive;
  (void)FileName;
  (void)pBuffer;
  (void)ImageSize;
  return false;
}

// --- includes/scu_vme.h (MegaSTE SCU/VME: excluded, ST never maps $ff8e01+) ---
bool SCU_IsEnabled( void ) { return false; }
void SCU_Reset( bool bCold ) {}
void SCU_UpdatePendingInts_CPU( void ) {}
void SCU_SetIRQ_CPU( int IntNr ) {}
void SCU_ClearIRQ_CPU( int IntNr ) {}
void SCU_SysIntMask_ReadByte( void ) {}
void SCU_SysIntMask_WriteByte( void ) {}
void SCU_SysIntState_ReadByte( void ) {}
void SCU_SysIntState_WriteByte( void ) {}
void SCU_SysInterrupter_ReadByte( void ) {}
void SCU_SysInterrupter_WriteByte( void ) {}
void SCU_VmeInterrupter_ReadByte( void ) {}
void SCU_VmeInterrupter_WriteByte( void ) {}
void SCU_GPR1_ReadByte( void ) {}
void SCU_GPR1_WriteByte( void ) {}
void SCU_GPR2_ReadByte( void ) {}
void SCU_GPR2_WriteByte( void ) {}
void SCU_VmeIntMask_Readyte( void ) {}
void SCU_VmeIntMask_WriteByte( void ) {}
void SCU_VmeIntState_ReadByte( void ) {}
void SCU_VmeIntState_WriteByte( void ) {}
void SCU_MemorySnapShot_Capture( bool bSave ) {}
void SCU_Info( FILE *fp, uint32_t arg ) {}

// --- debugger CPU/DSP/profile/debug-info/vars/console/disasm (no debugger) ---
void DebugCpu_Check(void) {}
void DebugCpu_SetDebugging(void) {}
void DebugDsp_SetDebugging(void) {}
bool Profile_CpuAddr_HasData(uint32_t addr) { (void)addr; return false; }
int Profile_CpuAddr_DataStr(char *buffer, int maxlen, uint32_t addr) {
  (void)buffer; (void)maxlen; (void)addr; return 0;
}
uint32_t DebugInfo_GetTEXT(void) { return 0; }
uint32_t DebugInfo_GetTEXTEnd(void) { return 0; }
uint32_t DebugInfo_GetDATA(void) { return 0; }
uint32_t DebugInfo_GetBSS(void) { return 0; }
uint32_t Vars_GetAesOpcode(void) { return 0; }
uint32_t Vars_GetVdiOpcode(void) { return 0; }
bool Console_SetDevice(int dev) { (void)dev; return false; }
const char *Disasm_ParseOption(const char *arg) { (void)arg; return NULL; }

// --- debugger console/profile/eval remainder (no debugger) ---
bool Profile_CpuStart(void) { return false; }
void Profile_CpuStop(void) {}
uint32_t DebugInfo_GetBSSEnd(void) { return 0; }
char *DebugUI_MatchHelper(const char **strings, int items, const char *text, int state) {
  (void)strings; (void)items; (void)text; (void)state; return NULL;
}
int DebugUI_GetPageLines(int config, int defvalue) {
  (void)config; (void)defvalue; return 24;
}
int DebugUI_PrintCmdHelp(const char *psCmd) { (void)psCmd; return 0; }
bool DebugUI_DoQuitQuery(const char *info) { (void)info; return false; }
const char *Eval_Expression(const char *expression, uint32_t *result, int *offset, bool bForDsp) {
  (void)expression; (void)result; (void)offset; (void)bForDsp; return "no debugger";
}
