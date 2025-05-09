// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "GS.h"
#include "Gif_Unit.h"
#include "MTVU.h"
#include "VUmicro.h"
#include "Vif_Dma.h"
#include "Vif_Dynarec.h"

u32 g_vif1Cycles = 0;

__fi void vif1FLUSH()
{
	if (VU0.VI[REG_VPU_STAT].UL & 0x500) // T bit stop or Busy
	{
		vif1.waitforvu = true;
		vif1.vifstalled.enabled = VifStallEnable(vif1ch);
		vif1.vifstalled.value = VIF_TIMING_BREAK;
		vif1Regs.stat.VEW = true;
	}
}

void vif1TransferToMemory()
{
	u128* pMem = (u128*)dmaGetAddr(vif1ch.madr, false);

	// VIF from gsMemory
	if (pMem == nullptr)
	{ // Is vif0ptag empty?
		Console.WriteLn("Vif1 Tag BUSERR");
		dmacRegs.stat.BEIS = true; // Bus Error
		vif1Regs.stat.FQC = 0;

		vif1ch.qwc = 0;
		vif1.done = true;
		CPU_INT(DMAC_VIF1, 0);
		return; // An error has occurred.
	}

	// MTGS concerns:  The MTGS is inherently disagreeable with the idea of downloading
	// stuff from the GS.  The *only* way to handle this case safely is to flush the GS
	// completely and execute the transfer there-after.
	//Console.Warning("Real QWC %x", vif1ch.qwc);
	const u32 size = std::min(vif1.GSLastDownloadSize, (u32)vif1ch.qwc);
	//const u128* pMemEnd  = vif1.GSLastDownloadSize + pMem;

#ifdef PCSX2_DEVBUILD
	if (size)
	{
		// Checking if any crazy game does a partial
		// gs primitive and then does a gs download...
		Gif_Path& p1 = gifUnit.gifPath[GIF_PATH_1];
		Gif_Path& p2 = gifUnit.gifPath[GIF_PATH_2];
		Gif_Path& p3 = gifUnit.gifPath[GIF_PATH_3];
		pxAssert(p1.isDone() || !p1.gifTag.isValid);
		pxAssert(p2.isDone() || !p2.gifTag.isValid);
		pxAssert(p3.isDone() || !p3.gifTag.isValid);
	}
#endif

	MTGS::InitAndReadFIFO(reinterpret_cast<u8*>(pMem), size);
	//	pMem += size;

	//Some games such as Alex Ferguson's Player Manager 2001 reads less than GSLastDownloadSize by VIF then reads the remainder by FIFO
	//Clearing the memory is clearing memory it shouldn't be and kills it.
	//The only scenario where this could be used is the transfer size really is less than QWC, not the other way around as it was doing
	//That said, I think this is pointless and a waste of cycles and could cause more problems than good. We will alert this situation below anyway.
	/*if (vif1.GSLastDownloadSize < vif1ch.qwc) {
		if (pMem < pMemEnd) {
			DevCon.Warning("GS Transfer < VIF QWC, Clearing end of space GST %x QWC %x", vif1.GSLastDownloadSize, (u32)vif1ch.qwc);

			__m128 zeroreg = _mm_setzero_ps();
			do {
				_mm_store_ps((float*)pMem, zeroreg);
			} while (++pMem < pMemEnd);
		}
	}*/

	g_vif1Cycles += size * 2;
	vif1ch.madr += size * 16; // mgs3 scene changes
	if (vif1.GSLastDownloadSize >= vif1ch.qwc)
	{
		vif1.GSLastDownloadSize -= vif1ch.qwc;
		vif1Regs.stat.FQC = std::min((u32)16, vif1.GSLastDownloadSize);
		vif1ch.qwc = 0;
	}
	else
	{
		vif1Regs.stat.FQC = 0;
		vif1ch.qwc -= vif1.GSLastDownloadSize;
		vif1.GSLastDownloadSize = 0;
		//This could be potentially bad and cause hangs. I guess we will find out.
		DevCon.Warning("QWC left on VIF FIFO Reverse");
	}
}

bool _VIF1chain()
{
	u32* pMem;

	if (vif1ch.qwc == 0)
	{
		vif1.inprogress &= ~1;
		vif1.irqoffset.value = 0;
		vif1.irqoffset.enabled = false;
		return true;
	}

	// Clarification - this is TO memory mode, for some reason i used the other way round >.<
	if (vif1.dmamode == VIF_NORMAL_TO_MEM_MODE)
	{
		vif1TransferToMemory();
		vif1.inprogress &= ~1;
		return true;
	}

	pMem = (u32*)dmaGetAddr(vif1ch.madr, !vif1ch.chcr.DIR);
	if (pMem == nullptr)
	{
		vif1.cmd = 0;
		vif1.tag.size = 0;
		vif1ch.qwc = 0;
		return true;
	}

	VIF_LOG("VIF1chain size=%d, madr=%lx, tadr=%lx",
		vif1ch.qwc, vif1ch.madr, vif1ch.tadr);

	if (vif1.irqoffset.enabled)
		return VIF1transfer(pMem + vif1.irqoffset.value, vif1ch.qwc * 4 - vif1.irqoffset.value, false);
	else
		return VIF1transfer(pMem, vif1ch.qwc * 4, false);
}

__fi void vif1SetupTransfer()
{
	tDMA_TAG* ptag;

	ptag = dmaGetAddr(vif1ch.tadr, false); //Set memory pointer to TADR

	if (!(vif1ch.transfer("Vif1 Tag", ptag)))
		return;

	vif1ch.madr = ptag[1]._u32; //MADR = ADDR field + SPR
	g_vif1Cycles += 1; // Add 1 g_vifCycles from the QW read for the tag
	vif1.inprogress &= ~1;

	VIF_LOG("VIF1 Tag %8.8x_%8.8x size=%d, id=%d, madr=%lx, tadr=%lx",
		ptag[1]._u32, ptag[0]._u32, vif1ch.qwc, ptag->ID, vif1ch.madr, vif1ch.tadr);

	if (!vif1.done && ((dmacRegs.ctrl.STD == STD_VIF1) && (ptag->ID == TAG_REFS))) // STD == VIF1
	{
		// there are still bugs, need to also check if gif->madr +16*qwc >= stadr, if not, stall
		if ((vif1ch.madr + vif1ch.qwc * 16) > dmacRegs.stadr.ADDR)
		{
			//DevCon.Warning("VIF1 DMA Stall");
			// stalled
			hwDmacIrq(DMAC_STALL_SIS);
			CPU_SET_DMASTALL(DMAC_VIF1, true);
			return;
		}
	}

	if (vif1ch.chcr.TTE)
	{
		// Transfer dma tag if tte is set

		bool ret;

		alignas(16) static u128 masked_tag;

		masked_tag._u64[0] = 0;
		masked_tag._u64[1] = *((u64*)ptag + 1);

		VIF_LOG("\tVIF1 SrcChain TTE=1, data = 0x%08x.%08x", masked_tag._u32[3], masked_tag._u32[2]);

		if (vif1.irqoffset.enabled)
		{
			ret = VIF1transfer((u32*)&masked_tag + vif1.irqoffset.value, 4 - vif1.irqoffset.value, true); //Transfer Tag on stall
			//ret = VIF1transfer((u32*)ptag + (2 + vif1.irqoffset), 2 - vif1.irqoffset);  //Transfer Tag on stall
		}
		else
		{
			// Some games (like killzone) do Tags mid unpack, the nops will just write blank data
			// to the VU's, which breaks stuff, this is where the 128bit packet will fail, so we ignore the first 2 words
			vif1.irqoffset.value = 2;
			vif1.irqoffset.enabled = true;
			ret = VIF1transfer((u32*)&masked_tag + 2, 2, true); //Transfer Tag
			//ret = VIF1transfer((u32*)ptag + 2, 2);  //Transfer Tag
		}

		if (!ret && vif1.irqoffset.enabled)
		{
			vif1.inprogress &= ~1; // Better clear this so it has to do it again (Jak 1)
			vif1ch.qwc = 0; // Gumball 3000 pauses the DMA when the tag stalls so we need to reset the QWC, it'll be gotten again later
			return; // IRQ set by VIFTransfer
		}
	}
	vif1.irqoffset.value = 0;
	vif1.irqoffset.enabled = false;

	vif1.done |= hwDmacSrcChainWithStack(vif1ch, ptag->ID);

	if (vif1ch.qwc > 0)
		vif1.inprogress |= 1;

	//Check TIE bit of CHCR and IRQ bit of tag
	if (vif1ch.chcr.TIE && ptag->IRQ)
	{
		VIF_LOG("dmaIrq Set");

		//End Transfer
		vif1.done = true;
		return;
	}
}

__fi void vif1VUFinish()
{
	// Sync up VU1 so we don't errantly wait.
	while (!THREAD_VU1 && (VU0.VI[REG_VPU_STAT].UL & 0x100))
	{
		const int cycle_diff = static_cast<int>(cpuRegs.cycle - VU1.cycle);

		if ((EmuConfig.Gamefixes.VUSyncHack && cycle_diff < VU1.nextBlockCycles) || cycle_diff <= 0)
			break;

		CpuVU1->ExecuteBlock();
	}

	if (VU0.VI[REG_VPU_STAT].UL & 0x500)
	{
		vu1Thread.Get_MTVUChanges();

		if (THREAD_VU1 && !INSTANT_VU1 && (VU0.VI[REG_VPU_STAT].UL & 0x100))
			CPU_INT(VIF_VU1_FINISH, cpuGetCycles(VU_MTVU_BUSY));
		else
			CPU_INT(VIF_VU1_FINISH, 128);
		CPU_SET_DMASTALL(VIF_VU1_FINISH, true);
		return;
	}

	if (VU0.VI[REG_VPU_STAT].UL & 0x100)
	{
		u32 _cycles = VU1.cycle;
		//DevCon.Warning("Finishing VU1");
		vu1Finish(false);
		if (THREAD_VU1 && !INSTANT_VU1 && (VU0.VI[REG_VPU_STAT].UL & 0x100))
			CPU_INT(VIF_VU1_FINISH, cpuGetCycles(VU_MTVU_BUSY));
		else
			CPU_INT(VIF_VU1_FINISH, VU1.cycle - _cycles);
		CPU_SET_DMASTALL(VIF_VU1_FINISH, true);
		return;
	}

	vif1Regs.stat.VEW = false;
	VIF_LOG("VU1 finished");

	if (vif1.waitforvu)
	{
		vif1.waitforvu = false;
		//Check if VIF is already scheduled to interrupt, if it's waiting, kick it :P
		if ((cpuRegs.interrupt & ((1 << DMAC_VIF1) | (1 << DMAC_MFIFO_VIF))) == 0 && vif1ch.chcr.STR && !vif1Regs.stat.test(VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
		{
			if (dmacRegs.ctrl.MFD == MFD_VIF1)
				vifMFIFOInterrupt();
			else
				vif1Interrupt();
		}
	}

	//DevCon.Warning("VU1 state cleared");
}

__fi void vif1Interrupt()
{
	VIF_LOG("vif1Interrupt: %8.8x chcr %x, done %x, qwc %x", cpuRegs.cycle, vif1ch.chcr._u32, vif1.done, vif1ch.qwc);

	g_vif1Cycles = 0;

	if (gifRegs.stat.APATH == 2 && gifUnit.gifPath[GIF_PATH_2].isDone())
	{
		gifRegs.stat.APATH = 0;
		gifRegs.stat.OPH = 0;
		vif1Regs.stat.VGW = false; //Let vif continue if it's stuck on a flush

		if (gifUnit.checkPaths(1, 0, 1))
			gifUnit.Execute(false, true);
	}
	//Some games (Fahrenheit being one) start vif first, let it loop through blankness while it sets MFIFO mode, so we need to check it here.
	if (dmacRegs.ctrl.MFD == MFD_VIF1)
	{
		//Console.WriteLn("VIFMFIFO\n");
		// Test changed because the Final Fantasy 12 opening somehow has the tag in *Undefined* mode, which is not in the documentation that I saw.
		if (vif1ch.chcr.MOD == NORMAL_MODE)
			Console.WriteLn("MFIFO mode is normal (which isn't normal here)! %x", vif1ch.chcr._u32);
		vif1Regs.stat.FQC = std::min((u32)0x10, vif1ch.qwc);
		vifMFIFOInterrupt();
		return;
	}

	// We need to check the direction, if it is downloading
	// from the GS then we handle that separately (KH2 for testing)
	if (vif1ch.chcr.DIR)
	{
		const bool isDirect = (vif1.cmd & 0x7f) == 0x50;
		const bool isDirectHL = (vif1.cmd & 0x7f) == 0x51;
		if ((isDirect && !gifUnit.CanDoPath2()) || (isDirectHL && !gifUnit.CanDoPath2HL()))
		{
			GUNIT_WARN("vif1Interrupt() - Waiting for Path 2 to be ready");
			CPU_INT(DMAC_VIF1, 128);
			if (gifRegs.stat.APATH == 3)
				vif1Regs.stat.VGW = 1; //We're waiting for path 3. Gunslinger II
			CPU_SET_DMASTALL(DMAC_VIF1, true);
			return;
		}
		vif1Regs.stat.VGW = 0; //Path 3 isn't busy so we don't need to wait for it.
		vif1Regs.stat.FQC = std::min(vif1ch.qwc, (u32)16);
		//Simulated GS transfer time done, clear the flags
	}

	if (vif1.waitforvu)
	{
		//DevCon.Warning("Waiting on VU1");
		//CPU_INT(DMAC_VIF1, 16);
		CPU_INT(VIF_VU1_FINISH, std::max(16, cpuGetCycles(VU_MTVU_BUSY)));
		CPU_SET_DMASTALL(DMAC_VIF1, true);
		return;
	}

	if (vif1Regs.stat.VGW)
	{
		CPU_SET_DMASTALL(DMAC_VIF1, true);
		return;
	}

	if (!vif1ch.chcr.STR)
	{
		Console.WriteLn("Vif1 running when CHCR == %x", vif1ch.chcr._u32);
		return;
	}

	if (vif1.irq && vif1.vifstalled.enabled && vif1.vifstalled.value == VIF_IRQ_STALL)
	{
		VIF_LOG("VIF IRQ Firing");
		if (!vif1Regs.stat.ER1)
			vif1Regs.stat.INT = true;

		//Yakuza watches VIF_STAT so lets do this here.
		if (((vif1Regs.code >> 24) & 0x7f) != 0x7)
		{
			vif1Regs.stat.VIS = true;
		}

		hwIntcIrq(VIF1intc);
		--vif1.irq;

		if (vif1Regs.stat.test(VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
		{
			//vif1Regs.stat.FQC = 0;

			//NFSHPS stalls when the whole packet has gone across (it stalls in the last 32bit cmd)
			//In this case VIF will end
			vif1Regs.stat.FQC = std::min((u32)0x10, vif1ch.qwc);
			if ((vif1ch.qwc > 0 || !vif1.done) && !CHECK_VIF1STALLHACK)
			{
				vif1Regs.stat.VPS = VPS_DECODING; //If there's more data you need to say it's decoding the next VIF CMD (Onimusha - Blade Warriors)
				VIF_LOG("VIF1 Stalled");
				CPU_SET_DMASTALL(DMAC_VIF1, true);
				return;
			}
		}
	}

	vif1.vifstalled.enabled = false;

	//Mirroring change to VIF0
	if (vif1.cmd)
	{
		if (vif1.done && (vif1ch.qwc == 0))
			vif1Regs.stat.VPS = VPS_WAITING;
	}
	else
	{
		vif1Regs.stat.VPS = VPS_IDLE;
	}

	if (vif1.inprogress & 0x1)
	{
		_VIF1chain();
		// VIF_NORMAL_FROM_MEM_MODE is a very slow operation.
		// Timesplitters 2 depends on this beeing a bit higher than 128.
		if (vif1ch.chcr.DIR)
			vif1Regs.stat.FQC = std::min(vif1ch.qwc, (u32)16);

		if (!(vif1Regs.stat.VGW && gifUnit.gifPath[GIF_PATH_3].state != GIF_PATH_IDLE)) //If we're waiting on GIF, stop looping, (can be over 1000 loops!)
		{
			if (vif1.waitforvu)
			{
				//if (cpuGetCycles(VU_MTVU_BUSY) > static_cast<int>(g_vif1Cycles))
				//	DevCon.Warning("Waiting %d instead of %d", cpuGetCycles(VU_MTVU_BUSY), static_cast<int>(g_vif1Cycles));
				CPU_INT(DMAC_VIF1, std::max(static_cast<int>(g_vif1Cycles), cpuGetCycles(VU_MTVU_BUSY)));
			}
			else
				CPU_INT(DMAC_VIF1, g_vif1Cycles);
		}
		return;
	}

	if (!vif1.done)
	{

		if (!(dmacRegs.ctrl.DMAE) || vif1Regs.stat.VSS) //Stopped or DMA Disabled
		{
			//Console.WriteLn("vif1 dma masked");
			return;
		}

		if ((vif1.inprogress & 0x1) == 0)
			vif1SetupTransfer();
		if (vif1ch.chcr.DIR)
			vif1Regs.stat.FQC = std::min(vif1ch.qwc, (u32)16);

		if (!(vif1Regs.stat.VGW && gifUnit.gifPath[GIF_PATH_3].state != GIF_PATH_IDLE)) //If we're waiting on GIF, stop looping, (can be over 1000 loops!)
		{
			if (vif1.waitforvu)
			{
				//if (cpuGetCycles(VU_MTVU_BUSY) > static_cast<int>(g_vif1Cycles))
				//	DevCon.Warning("Waiting %d instead of %d", cpuGetCycles(VU_MTVU_BUSY), static_cast<int>(g_vif1Cycles));
				CPU_INT(DMAC_VIF1, std::max(static_cast<int>(g_vif1Cycles), cpuGetCycles(VU_MTVU_BUSY)));
			}
			else
				CPU_INT(DMAC_VIF1, g_vif1Cycles);
		}
		return;
	}

	if (vif1.vifstalled.enabled && vif1.done)
	{
		DevCon.WriteLn("VIF1 looping on stall at end\n");
		CPU_INT(DMAC_VIF1, 0);
		CPU_SET_DMASTALL(DMAC_VIF1, true);
		return; //Dont want to end if vif is stalled.
	}
#ifdef PCSX2_DEVBUILD
	if (vif1ch.qwc > 0)
		DevCon.WriteLn("VIF1 Ending with %x QWC left", vif1ch.qwc);
	if (vif1.cmd != 0)
		DevCon.WriteLn("vif1.cmd still set %x tag size %x", vif1.cmd, vif1.tag.size);
#endif

	if ((vif1ch.chcr.DIR == VIF_NORMAL_TO_MEM_MODE) && vif1.GSLastDownloadSize <= 16)
	{
		//Reverse fifo has finished and nothing is left, so lets clear the outputting flag
		gifRegs.stat.OPH = false;
	}

	if (vif1ch.chcr.DIR)
		vif1Regs.stat.FQC = std::min(vif1ch.qwc, (u32)16);

	vif1ch.chcr.STR = false;
	vif1.vifstalled.enabled = false;
	vif1.irqoffset.enabled = false;
	if (vif1.queued_program)
		vifExecQueue(1);
	g_vif1Cycles = 0;
	VIF_LOG("VIF1 DMA End");
	hwDmacIrq(DMAC_VIF1);
	CPU_SET_DMASTALL(DMAC_VIF1, false);
}

void dmaVIF1()
{
	VIF_LOG("dmaVIF1 chcr = %lx, madr = %lx, qwc  = %lx\n"
			"        tadr = %lx, asr0 = %lx, asr1 = %lx",
		vif1ch.chcr._u32, vif1ch.madr, vif1ch.qwc,
		vif1ch.tadr, vif1ch.asr0, vif1ch.asr1);

	g_vif1Cycles = 0;
	vif1.inprogress = 0;
	CPU_SET_DMASTALL(DMAC_VIF1, false);

	if (vif1ch.qwc > 0) // Normal Mode
	{

		// ignore tag if it's a GS download (Def Jam Fight for NY)
		if (vif1ch.chcr.MOD == CHAIN_MODE && vif1ch.chcr.DIR)
		{
			vif1.dmamode = VIF_CHAIN_MODE;
			//DevCon.Warning(L"VIF1 QWC on Chain CHCR " + vif1ch.chcr.desc());

			if ((vif1ch.chcr.tag().ID == TAG_REFE) || (vif1ch.chcr.tag().ID == TAG_END) || (vif1ch.chcr.tag().IRQ && vif1ch.chcr.TIE))
			{
				vif1.done = true;
			}
			else
			{
				vif1.done = false;
			}
		}
		else //Assume normal mode for reverse FIFO and Normal.
		{
			if (dmacRegs.ctrl.STD == STD_VIF1)
				Console.WriteLn("DMA Stall Control on VIF1 normal not implemented - Report which game to PCSX2 Team");

			if (vif1ch.chcr.DIR) // from Memory
				vif1.dmamode = VIF_NORMAL_FROM_MEM_MODE;
			else
				vif1.dmamode = VIF_NORMAL_TO_MEM_MODE;

			if (vif1.irqoffset.enabled && !vif1.done)
				DevCon.Warning("Warning! VIF1 starting a Normal transfer with vif offset set (Possible force stop?)");
			vif1.done = true;
		}

		vif1.inprogress |= 1;
	}
	else
	{
		vif1.inprogress &= ~0x1;
		vif1.dmamode = VIF_CHAIN_MODE;
		vif1.done = false;
	}

	if (vif1ch.chcr.DIR)
		vif1Regs.stat.FQC = std::min((u32)0x10, vif1ch.qwc);

	// Check VIF isn't stalled before starting the loop.
	// Batman Vengence does something stupid and instead of cancelling a stall it tries to restart VIF, THEN check the stall
	// However if VIF FIFO is reversed, it can continue
	if (!vif1ch.chcr.DIR || !vif1Regs.stat.test(VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS))
		CPU_INT(DMAC_VIF1, 4);
}
