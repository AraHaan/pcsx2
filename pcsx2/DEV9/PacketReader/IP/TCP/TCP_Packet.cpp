// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "TCP_Packet.h"
#include "DEV9/PacketReader/NetLib.h"

#include "common/BitUtils.h"
#include "common/Console.h"

namespace PacketReader::IP::TCP
{
	//Need flags
	bool TCP_Packet::GetNS() const
	{
		return (dataOffsetAndNS_Flag & 1);
	}
	void TCP_Packet::SetNS(bool value)
	{
		dataOffsetAndNS_Flag = (dataOffsetAndNS_Flag & ~0x1) | (value & 0x1);
	}

	bool TCP_Packet::GetCWR() const
	{
		return (flags & (1 << 7));
	}
	void TCP_Packet::SetCWR(bool value)
	{
		flags = (flags & ~(0x1 << 7)) | ((value & 0x1) << 7);
	}

	bool TCP_Packet::GetECE() const
	{
		return (flags & (1 << 6));
	}
	void TCP_Packet::SetECE(bool value)
	{
		flags = (flags & ~(0x1 << 6)) | ((value & 0x1) << 6);
	}

	bool TCP_Packet::GetURG() const
	{
		return (flags & (1 << 5));
	}
	void TCP_Packet::SetURG(bool value)
	{
		flags = (flags & ~(0x1 << 5)) | ((value & 0x1) << 5);
	}

	bool TCP_Packet::GetACK() const
	{
		return (flags & (1 << 4));
	}
	void TCP_Packet::SetACK(bool value)
	{
		flags = (flags & ~(0x1 << 4)) | ((value & 0x1) << 4);
	}

	bool TCP_Packet::GetPSH() const
	{
		return (flags & (1 << 3));
	}
	void TCP_Packet::SetPSH(bool value)
	{
		flags = (flags & ~(0x1 << 3)) | ((value & 0x1) << 3);
	}

	bool TCP_Packet::GetRST() const
	{
		return (flags & (1 << 2));
	}
	void TCP_Packet::SetRST(bool value)
	{
		flags = (flags & ~(0x1 << 2)) | ((value & 0x1) << 2);
	}

	bool TCP_Packet::GetSYN() const
	{
		return (flags & (1 << 1));
	}
	void TCP_Packet::SetSYN(bool value)
	{
		flags = (flags & ~(0x1 << 1)) | ((value & 0x1) << 1);
	}

	bool TCP_Packet::GetFIN() const
	{
		return (flags & 1);
	}
	void TCP_Packet::SetFIN(bool value)
	{
		flags = (flags & ~0x1) | (value & 0x1);
	}

	TCP_Packet::TCP_Packet(Payload* data)
		: headerLength{20}
		, payload{data}
	{
	}
	TCP_Packet::TCP_Packet(const u8* buffer, int bufferSize)
	{
		int offset = 0;
		//Bits 0-31
		NetLib::ReadUInt16(buffer, &offset, &sourcePort);
		NetLib::ReadUInt16(buffer, &offset, &destinationPort);

		//Bits 32-63
		NetLib::ReadUInt32(buffer, &offset, &sequenceNumber);

		//Bits 64-95
		NetLib::ReadUInt32(buffer, &offset, &acknowledgementNumber);

		//Bits 96-127
		NetLib::ReadByte08(buffer, &offset, &dataOffsetAndNS_Flag);
		headerLength = (dataOffsetAndNS_Flag >> 4) << 2;
		NetLib::ReadByte08(buffer, &offset, &flags);
		NetLib::ReadUInt16(buffer, &offset, &windowSize);

		//Bits 127-159
		NetLib::ReadUInt16(buffer, &offset, &checksum);
		NetLib::ReadUInt16(buffer, &offset, &urgentPointer);

		//Bits 160+
		if (headerLength > 20) //TCP options
		{
			bool opReadFin = false;
			do
			{
				const u8 opKind = buffer[offset];
				const u8 opLen = buffer[offset + 1];
				switch (opKind)
				{
					case 0:
						opReadFin = true;
						break;
					case 1:
						options.push_back(new TCPopNOP());
						offset += 1;
						continue;
					case 2:
						options.push_back(new TCPopMSS(buffer, offset));
						break;
					case 3:
						options.push_back(new TCPopWS(buffer, offset));
						break;
					case 8:
						options.push_back(new TCPopTS(buffer, offset));
						break;
					default:
						Console.Error("Got Unknown TCP Option %d with len %d", opKind, opLen);
						options.push_back(new IPopUnk(buffer, offset));
						break;
				}
				offset += opLen;
				if (offset == headerLength)
					opReadFin = true;
			} while (opReadFin == false);
		}
		offset = headerLength;

		payload = std::make_unique<PayloadPtr>(&buffer[offset], bufferSize - offset);
		//AllDone
	}

	TCP_Packet::TCP_Packet(const TCP_Packet& original)
		: sourcePort{original.sourcePort}
		, destinationPort{original.destinationPort}
		, sequenceNumber{original.sequenceNumber}
		, acknowledgementNumber{original.acknowledgementNumber}
		, dataOffsetAndNS_Flag{original.dataOffsetAndNS_Flag}
		, headerLength{original.headerLength}
		, flags{original.flags}
		, windowSize{original.windowSize}
		, checksum{original.checksum}
		, urgentPointer{original.urgentPointer}
		, payload{original.payload->Clone()}
	{
		//Clone options
		options.reserve(original.options.size());
		for (size_t i = 0; i < options.size(); i++)
			options.push_back(original.options[i]->Clone());
	}

	Payload* TCP_Packet::GetPayload() const
	{
		return payload.get();
	}

	int TCP_Packet::GetLength()
	{
		ReComputeHeaderLen();
		return headerLength + payload->GetLength();
	}

	void TCP_Packet::WriteBytes(u8* buffer, int* offset)
	{
		const int startOff = *offset;
		NetLib::WriteUInt16(buffer, offset, sourcePort);
		NetLib::WriteUInt16(buffer, offset, destinationPort);
		NetLib::WriteUInt32(buffer, offset, sequenceNumber);
		NetLib::WriteUInt32(buffer, offset, acknowledgementNumber);
		NetLib::WriteByte08(buffer, offset, dataOffsetAndNS_Flag);
		NetLib::WriteByte08(buffer, offset, flags);
		NetLib::WriteUInt16(buffer, offset, windowSize);
		NetLib::WriteUInt16(buffer, offset, checksum);
		NetLib::WriteUInt16(buffer, offset, urgentPointer);

		//options
		for (size_t i = 0; i < options.size(); i++)
			options[i]->WriteBytes(buffer, offset);

		//Zero alignment bytes
		if (*offset != startOff + headerLength)
			memset(&buffer[*offset], 0, startOff + headerLength - *offset);

		*offset = startOff + headerLength;

		payload->WriteBytes(buffer, offset);
	}

	TCP_Packet* TCP_Packet::Clone() const
	{
		return new TCP_Packet(*this);
	}

	u8 TCP_Packet::GetProtocol() const
	{
		return (u8)protocol;
	}

	void TCP_Packet::ReComputeHeaderLen()
	{
		int opOffset = 20;
		for (size_t i = 0; i < options.size(); i++)
			opOffset += options[i]->GetLength();

		//needs to be a whole number of 32bits
		headerLength = Common::AlignUpPow2(opOffset, 4);

		//Also write into dataOffsetAndNS_Flag
		const u8 ns = dataOffsetAndNS_Flag & 1;
		dataOffsetAndNS_Flag = (headerLength >> 2) << 4;
		dataOffsetAndNS_Flag |= ns;
	}

	void TCP_Packet::CalculateChecksum(IP_Address srcIP, IP_Address dstIP)
	{
		ReComputeHeaderLen();
		int pHeaderLen = (12) + headerLength + payload->GetLength();
		if ((pHeaderLen & 1) != 0)
			pHeaderLen += 1;

		u8* headerSegment = new u8[pHeaderLen];
		int counter = 0;

		NetLib::WriteIPAddress(headerSegment, &counter, srcIP);
		NetLib::WriteIPAddress(headerSegment, &counter, dstIP);
		NetLib::WriteByte08(headerSegment, &counter, 0);
		NetLib::WriteByte08(headerSegment, &counter, (u8)protocol);
		NetLib::WriteUInt16(headerSegment, &counter, GetLength());

		//Pseudo Header added
		//Rest of data is normal Header+data (with zerored checksum feild)
		checksum = 0;
		WriteBytes(headerSegment, &counter);

		//Zero alignment byte
		if (counter != pHeaderLen)
			NetLib::WriteByte08(headerSegment, &counter, 0);

		checksum = IP_Packet::InternetChecksum(headerSegment, pHeaderLen);
		delete[] headerSegment;
	}
	bool TCP_Packet::VerifyChecksum(IP_Address srcIP, IP_Address dstIP)
	{
		ReComputeHeaderLen();
		int pHeaderLen = (12) + headerLength + payload->GetLength();
		if ((pHeaderLen & 1) != 0)
			pHeaderLen += 1;

		u8* headerSegment = new u8[pHeaderLen];
		int counter = 0;

		NetLib::WriteIPAddress(headerSegment, &counter, srcIP);
		NetLib::WriteIPAddress(headerSegment, &counter, dstIP);
		NetLib::WriteByte08(headerSegment, &counter, 0);
		NetLib::WriteByte08(headerSegment, &counter, (u8)protocol);
		NetLib::WriteUInt16(headerSegment, &counter, GetLength());

		//Pseudo Header added
		//Rest of data is normal Header+data
		WriteBytes(headerSegment, &counter);

		//Zero alignment byte
		if (counter != pHeaderLen)
			NetLib::WriteByte08(headerSegment, &counter, 0);

		const u16 csumCal = IP_Packet::InternetChecksum(headerSegment, pHeaderLen);
		delete[] headerSegment;

		return (csumCal == 0);
	}
} // namespace PacketReader::IP::TCP
