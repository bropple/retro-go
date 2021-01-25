/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#ifndef _SNES9X_H_
#define _SNES9X_H_

#ifndef VERSION
#define VERSION	"1.60"
#endif

#include "port.h"
#include "65c816.h"
#include "messages.h"

#include <rg_system.h>

/* Experimental retro-go flags */

// RETRO_LESS_ACCURATE runs pending events only once per execution
// loop. This is much less accurate, but also much faster. to be
// seen how badly it affects game
#define RETRO_LESS_ACCURATE_CPU 1
#define RETRO_LESS_ACCURATE_MEM 1
#define RETRO_LESS_ACCURATE_APU 0


#ifdef ZLIB
#include <zlib.h>
#define FSTREAM					gzFile
#define READ_FSTREAM(p, l, s)	gzread(s, p, l)
#define WRITE_FSTREAM(p, l, s)	gzwrite(s, p, l)
#define GETS_FSTREAM(p, l, s)	gzgets(s, p, l)
#define GETC_FSTREAM(s)			gzgetc(s)
#define OPEN_FSTREAM(f, m)		gzopen(f, m)
#define REOPEN_FSTREAM(f, m)		gzdopen(f, m)
#define FIND_FSTREAM(f)			gztell(f)
#define REVERT_FSTREAM(s, o, p)	gzseek(s, o, p)
#define CLOSE_FSTREAM(s)			gzclose(s)
#else
#define FSTREAM					FILE *
#define READ_FSTREAM(p, l, s)	fread(p, 1, l, s)
#define WRITE_FSTREAM(p, l, s)	fwrite(p, 1, l, s)
#define GETS_FSTREAM(p, l, s)	fgets(p, l, s)
#define GETC_FSTREAM(s)			fgetc(s)
#define OPEN_FSTREAM(f, m)		fopen(f, m)
#define REOPEN_FSTREAM(f, m)		fdopen(f, m)
#define FIND_FSTREAM(s)			ftell(s)
#define REVERT_FSTREAM(s, o, p)	fseek(s, o, p)
#define CLOSE_FSTREAM(s)			fclose(s)
#endif

#include "stream.h"

#define STREAM					Stream *
#define READ_STREAM(p, l, s)	s->read(p,l)
#define WRITE_STREAM(p, l, s)	s->write(p,l)
#define GETS_STREAM(p, l, s)	s->gets(p,l)
#define GETC_STREAM(s)			s->get_char()
#define OPEN_STREAM(f, m)		openStreamFromFSTREAM(f, m)
#define REOPEN_STREAM(f, m)		reopenStreamFromFd(f, m)
#define FIND_STREAM(s)			s->pos()
#define REVERT_STREAM(s, o, p)	s->revert(p, o)
#define CLOSE_STREAM(s)			s->closeStream()

#define SNES_WIDTH					256
#define SNES_HEIGHT					224
#define SNES_HEIGHT_EXTENDED		239

#define	NTSC_MASTER_CLOCK			21477272.727272 // 21477272 + 8/11 exact
#define	PAL_MASTER_CLOCK			21281370.0

#define SNES_MAX_NTSC_VCOUNTER		262
#define SNES_MAX_PAL_VCOUNTER		312
#define SNES_HCOUNTER_MAX			341

#ifndef ALLOW_CPU_OVERCLOCK
#define ONE_CYCLE					6
#define SLOW_ONE_CYCLE				8
#define TWO_CYCLES					12
#else
#define ONE_CYCLE      (Settings.OneClockCycle)
#define SLOW_ONE_CYCLE (Settings.OneSlowClockCycle)
#define TWO_CYCLES     (Settings.TwoClockCycles)
#endif
#define	ONE_DOT_CYCLE				4

#define SNES_CYCLES_PER_SCANLINE	(SNES_HCOUNTER_MAX * ONE_DOT_CYCLE)
#define SNES_SCANLINE_TIME			(SNES_CYCLES_PER_SCANLINE / NTSC_MASTER_CLOCK)

#define SNES_WRAM_REFRESH_HC_v1		530
#define SNES_WRAM_REFRESH_HC_v2		538
#define SNES_WRAM_REFRESH_CYCLES	40

#define SNES_HBLANK_START_HC		1096					// H=274
#define	SNES_HDMA_START_HC			1106					// FIXME: not true
#define	SNES_HBLANK_END_HC			4						// H=1
#define	SNES_HDMA_INIT_HC			20						// FIXME: not true
#define	SNES_RENDER_START_HC		(128 * ONE_DOT_CYCLE)	// FIXME: Snes9x renders a line at a time.

#define SNES_TR_MASK		(1 <<  4)
#define SNES_TL_MASK		(1 <<  5)
#define SNES_X_MASK			(1 <<  6)
#define SNES_A_MASK			(1 <<  7)
#define SNES_RIGHT_MASK		(1 <<  8)
#define SNES_LEFT_MASK		(1 <<  9)
#define SNES_DOWN_MASK		(1 << 10)
#define SNES_UP_MASK		(1 << 11)
#define SNES_START_MASK		(1 << 12)
#define SNES_SELECT_MASK	(1 << 13)
#define SNES_Y_MASK			(1 << 14)
#define SNES_B_MASK			(1 << 15)

#define DEBUG_MODE_FLAG		(1 <<  0)	// debugger
#define TRACE_FLAG			(1 <<  1)	// debugger
#define SINGLE_STEP_FLAG	(1 <<  2)	// debugger
#define BREAK_FLAG			(1 <<  3)	// debugger
#define SCAN_KEYS_FLAG		(1 <<  4)	// CPU
#define HALTED_FLAG			(1 << 12)	// APU
#define FRAME_ADVANCE_FLAG	(1 <<  9)

#define ROM_NAME_LEN	23
#define AUTO_FRAMERATE	200

struct SCPUState
{
	uint32	Flags;
	int32	Cycles;
	int32	PrevCycles;
	int32	V_Counter;
	uint8	*PCBase;
	bool8	NMIPending;
	bool8	IRQLine;
	bool8	IRQTransition;
	bool8	IRQLastState;
	bool8	IRQExternal;
	int32	IRQPending;
	int32	MemSpeed;
	int32	MemSpeedx2;
	int32	FastROMSpeed;
	bool8	InDMA;
	bool8	InHDMA;
	bool8	InDMAorHDMA;
	bool8	InWRAMDMAorHDMA;
	uint8	HDMARanInDMA;
	int32	CurrentDMAorHDMAChannel;
	int32	WhichEvent;
	int32	NextEvent;
	bool8	WaitingForInterrupt;
	uint32	AutoSaveTimer;
	bool8	SRAMModified;
};

enum
{
	HC_HBLANK_START_EVENT = 1,
	HC_HDMA_START_EVENT   = 2,
	HC_HCOUNTER_MAX_EVENT = 3,
	HC_HDMA_INIT_EVENT    = 4,
	HC_RENDER_EVENT       = 5,
	HC_WRAM_REFRESH_EVENT = 6
};

enum
{
	IRQ_NONE        = 0x0,
	IRQ_SET_FLAG    = 0x1,
	IRQ_CLEAR_FLAG  = 0x2,
	IRQ_TRIGGER_NMI = 0x4
};

struct STimings
{
	int32	H_Max_Master;
	int32	H_Max;
	int32	V_Max_Master;
	int32	V_Max;
	int32	HBlankStart;
	int32	HBlankEnd;
	int32	HDMAInit;
	int32	HDMAStart;
	int32	NMITriggerPos;
	int32	NextIRQTimer;
	int32	IRQTriggerCycles;
	int32	WRAMRefreshPos;
	int32	RenderPos;
	bool8	InterlaceField;
	int32	DMACPUSync;		// The cycles to synchronize DMA and CPU. Snes9x cannot emulate correctly.
	int32	NMIDMADelay;	// The delay of NMI trigger after DMA transfers. Snes9x cannot emulate correctly.
	int32	IRQFlagChanging;	// This value is just a hack.
	int32	APUSpeedup;
	bool8	APUAllowTimeOverflow;
};

struct SSettings
{
	bool8	TraceDMA;
	bool8	TraceHDMA;
	bool8	TraceVRAM;
	bool8	TraceUnknownRegisters;
	bool8	TraceDSP;
	bool8	TraceHCEvent;
	bool8	TraceSMP;

	uint8	DSP;

	bool8	ForceLoROM;
	bool8	ForceHiROM;
	bool8	ForceHeader;
	bool8	ForceNoHeader;
	bool8	ForceInterleaved;
	bool8	ForceNotInterleaved;
	bool8	ForcePAL;
	bool8	ForceNTSC;
	bool8	PAL;
	uint32	FrameTimePAL;
	uint32	FrameTimeNTSC;
	uint32	FrameTime;

	bool8	SoundSync;
	bool8	SixteenBitSound;
	uint32	SoundPlaybackRate;
	uint32	SoundInputRate;
	bool8	Stereo;
	bool8	Mute;
	bool8	DynamicRateControl;
	int32	DynamicRateLimit; /* Multiplied by 1000 */
	int32	InterpolationMethod;

	bool8	Transparency;
	uint8	BG_Forced;
	bool8	DisableGraphicWindows;

	bool8	DisplayFrameRate;
	bool8	AutoDisplayMessages;
	uint32	InitialInfoStringTimeout;
	uint16	DisplayColor;

	bool8	ForcedPause;
	bool8	Paused;
	bool8	StopEmulation;

	uint32	SkipFrames;
	uint32	TurboSkipFrames;
	uint32	AutoMaxSkipFrames;
	bool8	TurboMode;
	bool8	FrameAdvance;

	bool8	FastSavestates;

	bool8	NoPatch;
	bool8	IgnorePatchChecksum;
	bool8	IsPatched;
	int32	AutoSaveDelay;

	bool8	DisableGameSpecificHacks;
	bool8	BlockInvalidVRAMAccess;
	int32	HDMATimingHack;
	uint8	SRAMInitialValue;
	uint8	UniracersHack;

	int	OneClockCycle;
	int	OneSlowClockCycle;
	int	TwoClockCycles;
	int	MaxSpriteTilesPerLine;
};

void S9xExit(void);
void S9xMessage(int, int, const char *);
void S9xInitSettings(void);

extern struct SSettings			Settings;
extern struct SCPUState			CPU;
extern struct STimings			Timings;
extern char						String[513];

#endif
