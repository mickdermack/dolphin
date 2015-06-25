// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/ChunkFile.h"
#include "Core/ConfigManager.h"
#include "Core/HW/EXI.h"
#include "Core/HW/EXI_Device.h"
#include "Core/HW/EXI_DeviceModem.h"
#include "Core/HW/Memmap.h"


CEXIModem::CEXIModem()
	: parser(this)
{
}

CEXIModem::~CEXIModem()
{
}

void CEXIModem::SetCS(int cs) {
	//DEBUG_LOG(SP1, "Set CS: %08x", cs);
}

bool CEXIModem::IsPresent() const
{
	return true;
}

bool CEXIModem::IsInterruptSet()
{
	return !!(exi_state.interrupt & exi_state.interruptMask);
}

void CEXIModem::ImmWrite(u32 data, u32 size)
{
	//DEBUG_LOG(SP1, "Imm write: %08x size %d", data, size);

	if (exi_state.pendingWriteBytes > 0)
	{
		u8 writeSize = (u8)std::min(exi_state.pendingWriteBytes, (u16)4);

		if (exi_state.currentRegister == MODEM_REG_DATA)
		{
			DEBUG_LOG(SP1, "Buffer DATA write: %08x size %d", data, size);
			WriteData(data, writeSize);
		}
		else if (exi_state.currentRegister == MODEM_REG_AT_DATA)
		{
			DEBUG_LOG(SP1, "Buffer AT_DATA write: %08x size %d", data, size);
			WriteATData(data, writeSize);
		}
		else
		{
			if (writeSize > 1)
			{
				ERROR_LOG(SP1, "Data write to non-buffer register");
				return;
			}

			u8 dataByte = data >> 24;

			INFO_LOG(SP1, "Write reg %s(%02x) size %d = %x", GetRegisterName(exi_state.currentRegister), exi_state.currentRegister, writeSize, dataByte);

			switch (exi_state.currentRegister)
			{
			case MODEM_REG_EXI_ID:
				ERROR_LOG(SP1, "Register write to EXI ID");
				break;
			case MODEM_REG_INTERRUPT_MASK:
				exi_state.interruptMask = dataByte;
				break;
			case MODEM_REG_INTERRUPT_CAUSE:
				exi_state.interrupt = dataByte;
				break;
			case MODEM_REG_PENDING_AT_CMD:
				NOTICE_LOG(SP1, "Unhandled register write to MODEM_REG_PENDING_AT_CMD");
				break;
			case MODEM_REG_PENDING_AT_RES:
				NOTICE_LOG(SP1, "Unhandled register write to MODEM_REG_PENDING_AT_RES");
				break;
			case MODEM_REG_UNKNOWN1:
			case MODEM_REG_UNKNOWN2:
			case MODEM_REG_SEND_PENDING_H:
			case MODEM_REG_SEND_PENDING_L:
			case MODEM_REG_RECV_PENDING_H:
			case MODEM_REG_RECV_PENDING_L:
			case MODEM_REG_ESR:
			case MODEM_REG_SEND_THRESH_H:
			case MODEM_REG_SEND_THRESH_L:
			case MODEM_REG_RECV_THRESH_H:
			case MODEM_REG_RECV_THRESH_L:
			case MODEM_REG_RAW_STATUS:
				NOTICE_LOG(SP1, "Unhandled register write to %s", GetRegisterName(exi_state.currentRegister));
				break;
			case MODEM_REG_FWT:
				NOTICE_LOG(SP1, "Unhandled register write to MODEM_REG_FWT");
				exi_state.interrupt = 0;
				break;
			default:
				// Should not happen. Just for completeness.
				NOTICE_LOG(SP1, "Unhandled register write to %s", GetRegisterName(exi_state.currentRegister));
			}
		}

		exi_state.pendingWriteBytes -= writeSize;

		return;
	}

	if (data & 0x80000000)
	{
		NOTICE_LOG(SP1, "Got imm write & 0x80000000, resetting modem");
		ResetModem();
		return;
	}

	u8 cmdByte = data >> 24;

	exi_state.currentRegister = (ModemRegister)(cmdByte & 0x1f);
	if (cmdByte & 0x40 && cmdByte & 0x20)
	{
		u16 bytes = ((data & 0x00ff) << 8) | ((data & 0xff00) >> 8);

		DEBUG_LOG(SP1, "Start data write for register %s(%02x) size %hu", GetRegisterName(exi_state.currentRegister), exi_state.currentRegister, bytes);
		exi_state.pendingWriteBytes = bytes;
	}
	else if (cmdByte & 0x40)
	{
		DEBUG_LOG(SP1, "Start single write for register %s(%02x)", GetRegisterName(exi_state.currentRegister), exi_state.currentRegister);
		exi_state.pendingWriteBytes = 1;
	}
//	DEBUG_LOG(SP1, "Imm write: %08x %x", data, size);
}

u32 CEXIModem::ImmRead(u32 size)
{
	DEBUG_LOG(SP1, "Read reg %s(%02x) size %d", GetRegisterName(exi_state.currentRegister), exi_state.currentRegister, size);

	u32 result = 0;

	switch (exi_state.currentRegister)
	{
	case MODEM_REG_EXI_ID:
		result = EXI_DEVTYPE_MODEM;
		break;
	case MODEM_REG_INTERRUPT_MASK:
		result = exi_state.interruptMask;
		break;
	case MODEM_REG_INTERRUPT_CAUSE:
		result = exi_state.interrupt;
		exi_state.interrupt = 0;
		break;
	case MODEM_REG_AT_DATA:
		for (u32 i = 0; i < size; ++i)
		{
			result <<= 8;
			result |= ReadATData();
		}
		
		break;
	case MODEM_REG_PENDING_AT_CMD:
		result = at_state.cmdPos;
		break;
	case MODEM_REG_PENDING_AT_RES:
		result = at_state.resEnd - at_state.resPos;
		break;
	case MODEM_REG_UNKNOWN1:
	case MODEM_REG_UNKNOWN2:
	case MODEM_REG_DATA:
	case MODEM_REG_SEND_PENDING_H:
	case MODEM_REG_SEND_PENDING_L:
	case MODEM_REG_RECV_PENDING_H:
	case MODEM_REG_RECV_PENDING_L:
	case MODEM_REG_ESR:
	case MODEM_REG_SEND_THRESH_H:
	case MODEM_REG_SEND_THRESH_L:
	case MODEM_REG_RECV_THRESH_H:
	case MODEM_REG_RECV_THRESH_L:
	case MODEM_REG_RAW_STATUS:
	case MODEM_REG_FWT:
	default:
		return 0;
	}

	return result << (8 * (4 - size));
}

void CEXIModem::DMAWrite(u32 addr, u32 size)
{
	DEBUG_LOG(SP1, "DMA write: %08x %x", addr, size);
	exi_state.pendingWriteBytes -= size;
}

void CEXIModem::DMARead(u32 addr, u32 size)
{
	DEBUG_LOG(SP1, "DMA read: %08x %x", addr, size);
}

void CEXIModem::DoState(PointerWrap &p)
{
	p.Do(exi_state);
	// TODO ... the rest...
	ERROR_LOG(SP1, "CEXIModem::DoState not implemented!");
}

void CEXIModem::WriteData(u32 data, u8 length)
{
	return;
	u8* writePtr = at_state.cmdBuffer + at_state.cmdPos;
	u16 writeMax = (u16)MODEM_AT_BUFFER_SIZE - at_state.cmdPos;

	for (int i = 0; i < length; i++)
	{
		if (i >= writeMax)
		{
			PanicAlertT("Modem Data buffer overflow");
			break;
		}

		u8 dataByte = (u8)(data >> 8*(3-i));
		writePtr[i] = dataByte;
	}
}

void CEXIModem::WriteATData(u32 data, u8 length)
{
	u16 writeMax = (u16)MODEM_AT_BUFFER_SIZE - at_state.cmdPos;

	for (int i = 0; i < length; i++)
	{
		if (i >= writeMax)
		{
			PanicAlertT("Modem AT Data buffer overflow");
			break;
		}

		u8 dataByte = (u8)(data >> 8*(3-i));
		at_state.cmdBuffer[at_state.cmdPos] = dataByte;

		if (at_state.echo)
		{
			RespondATChar(dataByte);
		}

		at_state.cmdPos++;

		if (dataByte == '\r')
		{
			std::string cmdString((char*)at_state.cmdBuffer, at_state.cmdPos - 1);

			std::string response;
			response = parser.HandleLine(cmdString);

			if (!response.empty())
			{
				//RespondAT("\r\n");
				RespondAT(response.c_str());
				//RespondAT("\r\n");
			}

			at_state.cmdPos = 0;
			writeMax = (u16)MODEM_AT_BUFFER_SIZE;
		}
	}
}

u8 CEXIModem::ReadATData()
{
	u8 read = at_state.resBuffer[at_state.resPos];

	++at_state.resPos;

	if (at_state.resPos >= at_state.resEnd)
	{
		DEBUG_LOG(SP1, "Reset AT response buf pos");

		at_state.resPos = 0;
		at_state.resEnd = 0;
	}
	return read;
}

void CEXIModem::RespondAT(const char* answer)
{
	u8 len = (u8)strlen(answer);
	if (at_state.resEnd + len > MODEM_AT_BUFFER_SIZE)
	{
		PanicAlertT("Modem AT Response Data buffer overflow");
		return;
	}

	memcpy(at_state.resBuffer + at_state.resEnd, answer, len);
	at_state.resEnd += len;
}

void CEXIModem::RespondATChar(char answer)
{
	if (at_state.resEnd >= MODEM_AT_BUFFER_SIZE)
	{
		PanicAlertT("Modem AT Response Data buffer overflow");
		return;
	}

	at_state.resBuffer[at_state.resEnd] = answer;
	at_state.resEnd++;
}

void CEXIModem::HandleATCommand(const std::string& atCommand, const std::string& atParam, std::string* error_status)
{
	if (atCommand[0] == 'E')
	{
		if (!atParam.empty())
		{
			at_state.echo = (atParam[0] != '0');
		}
		else
		{
			at_state.echo = true;
		}
	}
}

void CEXIModem::SetConnInt(CEXIModem* obj)
{
	SLEEP(5000);
	obj->RespondAT("\r\nCARRIER 33600\r\nPROTOCOL: ALT\r\nCOMPRESSION: CLASS 5\r\nCONNECT 33600\r\n");
	obj->exi_state.interrupt |= 2;
	ExpansionInterface::ScheduleUpdateInterrupts_Threadsafe(0);
}

void CEXIModem::HandleDial(const std::string& dial_string, std::string* error_status)
{
	//error_status->assign("\r\nCARRIER 33600\r\nPROTOCOL: ALT\r\nCOMPRESSION: CLASS 5\r\nCONNECT 33600\r\n");

	std::thread thr(&SetConnInt, this);
	thr.detach();

	//exi_state.interrupt |= 0x2;
}

void CEXIModem::HandleExtendedCommand(const std::string& command, const std::vector<std::string>& args, std::string* error_status)
{

}

void CEXIModem::QueryExtendedCommand(const std::string& command, std::string* error_status)
{

}

void CEXIModem::GetExtendedParameter(const std::string& command, std::string* error_status)
{

}

void CEXIModem::SetSParameter(u16 param, u16 value, std::string* error_status)
{

}

void CEXIModem::GetSParameter(u16 param, std::string* error_status)
{

}

void CEXIModem::ResetSParameter(u16 param, std::string* error_status)
{

}

void CEXIModem::ResetModem()
{

}

const char* CEXIModem::GetRegisterName(ModemRegister reg) const
{
#define STR_RETURN(x) case x: return #x;

	switch (reg)
	{
		STR_RETURN(MODEM_REG_EXI_ID)
		STR_RETURN(MODEM_REG_INTERRUPT_MASK)
		STR_RETURN(MODEM_REG_INTERRUPT_CAUSE)
		STR_RETURN(MODEM_REG_AT_DATA)
		STR_RETURN(MODEM_REG_PENDING_AT_CMD)
		STR_RETURN(MODEM_REG_PENDING_AT_RES)
		STR_RETURN(MODEM_REG_UNKNOWN1)
		STR_RETURN(MODEM_REG_UNKNOWN2)
		STR_RETURN(MODEM_REG_DATA)
		STR_RETURN(MODEM_REG_SEND_PENDING_H)
		STR_RETURN(MODEM_REG_SEND_PENDING_L)
		STR_RETURN(MODEM_REG_RECV_PENDING_H)
		STR_RETURN(MODEM_REG_RECV_PENDING_L)
		STR_RETURN(MODEM_REG_ESR)
		STR_RETURN(MODEM_REG_SEND_THRESH_H)
		STR_RETURN(MODEM_REG_SEND_THRESH_L)
		STR_RETURN(MODEM_REG_RECV_THRESH_H)
		STR_RETURN(MODEM_REG_RECV_THRESH_L)
		STR_RETURN(MODEM_REG_RAW_STATUS)
		STR_RETURN(MODEM_REG_FWT)
	default: return "unknown";
	}

#undef STR_RETURN
}


// This function is on the critical path for receiving data.
// Be very careful about calling into the logger and other slow things
bool CEXIModem::RecvHandlePacket()
{

	return true;
}
