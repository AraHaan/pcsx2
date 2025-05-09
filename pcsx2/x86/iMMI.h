// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

namespace R5900 {
namespace Dynarec {
namespace OpcodeImpl {

	// These are instructions contained the MMI "opcode space" but are not
	// actually MMI instructions.  They are just specialized versions of standard
	// instructions that "fit" into the second pipeline of the EE.

	void recMADD1();
	void recMADDU1();
	void recMADD();
	void recMADDU();

	void recMTHI1();
	void recMTLO1();
	void recMFHI1();
	void recMFLO1();
	void recMULT1();
	void recMULTU1();
	void recDIV1();
	void recDIVU1();

namespace MMI {

	void recPLZCW();
	void recMMI0();
	void recMMI1();
	void recMMI2();
	void recMMI3();
	void recPMFHL();
	void recPMTHL();
	void recPMAXW();
	void recPMINW();
	void recPPACW();
	void recPEXTLH();
	void recPPACH();
	void recPEXTLB();
	void recPPACB();
	void recPEXT5();
	void recPPAC5();
	void recPABSW();
	void recPADSBH();
	void recPABSH();
	void recPADDUW();
	void recPSUBUW();
	void recPSUBUH();
	void recPEXTUH();
	void recPSUBUB();
	void recPEXTUB();
	void recQFSRV();
	void recPMADDW();
	void recPSLLVW();
	void recPSRLVW();
	void recPMSUBW();
	void recPINTH();
	void recPMULTW();
	void recPDIVW();
	void recPMADDH();
	void recPHMADH();
	void recPMSUBH();
	void recPHMSBH();
	void recPEXEH();
	void recPREVH();
	void recPMULTH();
	void recPDIVBW();
	void recPEXEW();
	void recPROT3W();
	void recPMADDUW();
	void recPSRAVW();
	void recPINTEH();
	void recPMULTUW();
	void recPDIVUW();
	void recPEXCH();
	void recPEXCW();

	void recPSRLH();
	void recPSRLW();
	void recPSRAH();
	void recPSRAW();
	void recPSLLH();
	void recPSLLW();
	void recPMAXH();
	void recPCGTB();
	void recPCGTH();
	void recPCGTW();
	void recPADDSB();
	void recPADDSH();
	void recPADDSW();
	void recPSUBSB();
	void recPSUBSH();
	void recPSUBSW();
	void recPADDB();
	void recPADDH();
	void recPADDW();
	void recPSUBB();
	void recPSUBH();
	void recPSUBW();
	void recPEXTLW();
	void recPEXTUW();
	void recPMINH();
	void recPCEQB();
	void recPCEQH();
	void recPCEQW();
	void recPADDUB();
	void recPADDUH();
	void recPMFHI();
	void recPMFLO();
	void recPAND();
	void recPXOR();
	void recPCPYLD();
	void recPNOR();
	void recPMTHI();
	void recPMTLO();
	void recPCPYUD();
	void recPOR();
	void recPCPYH();

} // namespace MMI
} // namespace OpcodeImpl
} // namespace Dynarec
} // namespace R5900
