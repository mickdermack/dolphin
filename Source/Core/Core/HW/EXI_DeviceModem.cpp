// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "SFML/Network.hpp"

#include "Common/ChunkFile.h"
#include "Core/ConfigManager.h"
#include "Core/CoreTiming.h"
#include "Core/HW/EXI.h"
#include "Core/HW/EXI_Device.h"
#include "Core/HW/EXI_DeviceModem.h"
#include "Core/HW/Memmap.h"


CEXIModem* CEXIModem::instance = nullptr;
int CEXIModem::connect_event = -1;
int CEXIModem::recv_event = -1;


CEXIModem::CEXIModem()
	: parser(this)
{
	// We probably won't need more than one modem at a time and supporting that
	// would need some work.
	if (instance != nullptr)
	{
		ERROR_LOG(SP1, "Created a modem while another was already present");
	}

	instance = this;
	if (connect_event < 0)
	{
		connect_event = CoreTiming::RegisterEvent("ModemConnect", &ConnectCallback);
	}

	if (recv_event < 0)
	{
		recv_event = CoreTiming::RegisterEvent("ModemRecv", &RecvCallback);
	}
}

CEXIModem::~CEXIModem()
{
	if (instance == this)
	{
		instance = nullptr;
	}
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
		u8 writeSize = (u8)std::min(exi_state.pendingWriteBytes, (u16)size);
		exi_state.pendingWriteBytes -= writeSize;

		if (exi_state.currentRegister == ModemRegister::DATA)
		{
			DEBUG_LOG(SP1, "Buffer DATA write: %08x size %d", data, size);
			WriteData(data, writeSize);

			if (exi_state.pendingWriteBytes == 0)
			{
				DEBUG_LOG(SP1, "Write complete, sending");
				TransmitData();
			}
		}
		else if (exi_state.currentRegister == ModemRegister::AT_DATA)
		{
			DEBUG_LOG(SP1, "Buffer AT_DATA write: %08x size %d", data, size);
			WriteATCommand(data, writeSize);
		}
		else
		{
			if (writeSize > 1)
			{
				ERROR_LOG(SP1, "Data write to non-buffer register %s", GetRegisterName(exi_state.currentRegister));
				return;
			}

			u8 dataByte = data >> 24;

			INFO_LOG(SP1, "Write reg %s(%02x) size %d = %x", GetRegisterName(exi_state.currentRegister), exi_state.currentRegister, writeSize, dataByte);

			switch (exi_state.currentRegister)
			{
			case ModemRegister::EXI_ID:
				ERROR_LOG(SP1, "Register write to EXI ID");
				break;
			case ModemRegister::INTERRUPT_MASK:
				exi_state.interruptMask = dataByte;
				exi_state.interrupt = 0;
				break;
			case ModemRegister::INTERRUPT_CAUSE:
				exi_state.interrupt = dataByte;
				break;
			case ModemRegister::PENDING_AT_CMD:
			case ModemRegister::PENDING_AT_RES:
			case ModemRegister::UNKNOWN1:
			case ModemRegister::UNKNOWN2:
			case ModemRegister::SEND_PENDING_H:
			case ModemRegister::SEND_PENDING_L:
			case ModemRegister::RECV_PENDING_H:
			case ModemRegister::RECV_PENDING_L:
			case ModemRegister::ESR:
			case ModemRegister::SEND_THRESH_H:
			case ModemRegister::SEND_THRESH_L:
			case ModemRegister::RECV_THRESH_H:
			case ModemRegister::RECV_THRESH_L:
			case ModemRegister::RAW_STATUS:
				INFO_LOG(SP1, "Unhandled register write to %s", GetRegisterName(exi_state.currentRegister));
				break;
			case ModemRegister::FWT:
				exi_state.interrupt = 0;
				break;
			default:
				// Should not happen. Just for completeness.
				INFO_LOG(SP1, "Unhandled register write to %s", GetRegisterName(exi_state.currentRegister));
			}
		}

		return;
	}

	if (data & 0x80000000)
	{
		INFO_LOG(SP1, "Got imm write & 0x80000000, resetting modem");
		ResetComms();
		return;
	}

	u8 cmdByte = data >> 24;

	exi_state.currentRegister = (ModemRegister)(cmdByte & 0x1f);
	if (cmdByte & 0x40 && cmdByte & 0x20)
	{
		u16 bytes = (data >> 8) & 0xffff;

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
	case ModemRegister::EXI_ID:
		result = EXI_DEVTYPE_MODEM;
		break;
	case ModemRegister::INTERRUPT_MASK:
		result = exi_state.interruptMask;
		break;
	case ModemRegister::INTERRUPT_CAUSE:
		result = exi_state.interrupt;
		exi_state.interrupt = 0;
		break;
	case ModemRegister::AT_DATA:
		for (u32 i = 0; i < size; ++i)
		{
			result <<= 8;
			result |= ReadATResponse();
		}
		
		break;
	case ModemRegister::PENDING_AT_CMD:
		result = at_state.cmdPos;
		break;
	case ModemRegister::PENDING_AT_RES:
		result = at_state.resEnd - at_state.resPos;
		break;
	case ModemRegister::UNKNOWN1:
	case ModemRegister::UNKNOWN2:
		result = 0;
		break;
	case ModemRegister::DATA:
		for (u32 i = 0; i < size; ++i)
		{
			result <<= 8;
			result |= ReadData();
		}

		break;
	case ModemRegister::SEND_PENDING_H:
	case ModemRegister::SEND_PENDING_L:
		result = 0;
		break;
	case ModemRegister::RECV_PENDING_H:
		result = (u8)((data_state.input_end - data_state.input_pos) >> 8);
		break;
	case ModemRegister::RECV_PENDING_L:
		result = (u8)(data_state.input_end - data_state.input_pos);
		break;
	case ModemRegister::ESR:
	case ModemRegister::SEND_THRESH_H:
	case ModemRegister::SEND_THRESH_L:
	case ModemRegister::RECV_THRESH_H:
	case ModemRegister::RECV_THRESH_L:
	case ModemRegister::RAW_STATUS:
	case ModemRegister::FWT:
	default:
		return 0;
	}

	return result << (8 * (4 - size));
}

void CEXIModem::DMAWrite(u32 addr, u32 size)
{
	DEBUG_LOG(SP1, "DMA write: %08x %x", addr, size);

	// TODO: check size!
	Memory::CopyFromEmu(data_state.output_buffer + data_state.output_pos, addr, size);
	data_state.output_pos += size;
	
	exi_state.pendingWriteBytes -= size;
}

void CEXIModem::DMARead(u32 addr, u32 size)
{
	DEBUG_LOG(SP1, "DMA read: %08x %x", addr, size);

	Memory::CopyToEmu(addr, data_state.input_buffer + data_state.input_pos, size);

	data_state.input_pos += size;
}

void CEXIModem::DoState(PointerWrap &p)
{
	p.Do(exi_state);
	p.Do(at_state);
	p.Do(at_prev_cmd);
	p.Do(modem_state);
	p.Do(pending_connect_number);
}

void CEXIModem::WriteData(u32 data, u8 length)
{
	u8* writePtr = data_state.output_buffer + data_state.output_pos;
	u16 writeMax = (u16)MODEM_AT_BUFFER_SIZE - data_state.output_pos;

	for (int i = 0; i < length; i++)
	{
		if (i >= writeMax)
		{
			ERROR_LOG(SP1, "Modem Data buffer overflow");
			break;
		}

		u8 dataByte = (u8)(data >> 8*(3-i));
		writePtr[i] = dataByte;

		data_state.output_pos++;
	}
}

u8 CEXIModem::ReadData()
{
	u8 read = data_state.input_buffer[data_state.input_pos];

	data_state.input_pos++;

	if (data_state.input_pos >= data_state.input_end)
	{
		DEBUG_LOG(SP1, "Reset data input buf pos");

		data_state.input_pos = 0;
		data_state.input_end = 0;
	}
	return read;
}

void CEXIModem::WriteATCommand(u32 data, u8 length)
{
	u16 writeMax = (u16)MODEM_AT_BUFFER_SIZE - at_state.cmdPos;

	for (int i = 0; i < length; i++)
	{
		u8 dataByte = (u8)(data >> 8*(3-i));

		if (at_state.echo)
		{
			RespondATChar(dataByte);
		}

		if (at_state.at_command)
		{
			if (i >= writeMax)
			{
				if (dataByte != '\r')
				{
					if (!at_state.cmd_buffer_overflow)
					{
						WARN_LOG(SP1, "Modem AT Data buffer overflow");
						at_state.cmd_buffer_overflow = true;
					}
				}
				else
				{
					RespondAT("ERROR");
					at_state.cmdPos = 0;
					at_state.cmd_buffer_overflow = false;
					at_state.at_command = false;
				}

				continue;
			}

			at_state.cmdBuffer[at_state.cmdPos] = dataByte;
			at_state.cmdPos++;

			if (dataByte == '\r')
			{
				std::string cmd_string((char*)at_state.cmdBuffer, at_state.cmdPos - 1);

				std::string response = parser.HandleLine(cmd_string);

				if (!response.empty())
				{
					//RespondAT("\r\n");
					RespondAT(response.c_str());
					//RespondAT("\r\n");
				}

				at_prev_cmd = cmd_string;

				at_state.cmdPos = 0;
				at_state.at_command = false;
				writeMax = (u16)MODEM_AT_BUFFER_SIZE;
			}
		}
		else
		{
			if (dataByte == 'a' || dataByte == 'A')
			{
				at_state.received_a = true;
			}
			else
			{
				if (at_state.received_a)
				{
					if (dataByte == 't' || dataByte == 'T')
					{
						at_state.at_command = true;
					}
					else if (dataByte == '/')
					{
						parser.HandleLine(at_prev_cmd);
					}
				}

				at_state.received_a = false;
			}
		}
	}
}

u8 CEXIModem::ReadATResponse()
{
	u8 read = at_state.resBuffer[at_state.resPos];

	at_state.resPos++;

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
		ERROR_LOG(SP1, "Modem AT Response Data buffer overflow");
		return;
	}

	memcpy(at_state.resBuffer + at_state.resEnd, answer, len);
	at_state.resEnd += len;
}

void CEXIModem::RespondATChar(char answer)
{
	if (at_state.resEnd >= MODEM_AT_BUFFER_SIZE)
	{
		ERROR_LOG(SP1, "Modem AT Response Data buffer overflow");
		return;
	}

	at_state.resBuffer[at_state.resEnd] = answer;
	at_state.resEnd++;
}

void CEXIModem::HandleATCommand(const std::string& atCommand, const std::string& atParam, std::string* error_status)
{
	// Not much implemented
	switch (atCommand[0])
	{
	case 'E':
		if (!atParam.empty())
		{
			at_state.echo = (atParam[0] != '0');
		}
		else
		{
			at_state.echo = true;
		}
		break;
	case 'H':
		if (!atParam.empty() && atParam[0] == '0')
		{
			ResetComms();
		}
	}
}

bool CEXIModem::PhoneNumberToAddress(const std::string& number, std::string* ip, u16* port)
{
	*ip = "10.0.1.1";
	*port = 2468;
	return true;
}

void CEXIModem::PerformPendingConnect()
{
	if (modem_state.cancel_connect)
	{
		return;
	}

	std::string ip;
	u16 port;

	if (!PhoneNumberToAddress(pending_connect_number, &ip, &port))
	{
		ERROR_LOG(SP1, "Invalid phone number \"%s\", format is: *40*ip1*ip2*ip3*ip4*port", pending_connect_number.c_str());
		RespondAT("\r\nNO CARRIER\r\n");
		SetInterrupt(ModemInterrupt::LINE_STATE);
		return;
	}

	sf::IpAddress addr(ip);

	// Scope so sock doesn't stick around
	{
		std::unique_ptr<sf::TcpSocket> sock(new sf::TcpSocket());
		m_socket = std::move(sock);
	}

	if (m_socket->connect(addr, port) != sf::TcpSocket::Status::Done)
	{
		WARN_LOG(SP1, "Couldn't connect");
		RespondAT("\r\nNO CARRIER\r\n");
		SetInterrupt(ModemInterrupt::LINE_STATE);
		return;
	}

	INFO_LOG(SP1, "Connected");

	m_socket->setBlocking(false);
	
	// No idea what typically would be sent, so I made up something...
	RespondAT("\r\nCARRIER 33600\r\nPROTOCOL: LAPM\r\nCOMPRESSION: NONE\r\nCONNECT 33600\r\n");

	SetInterrupt(ModemInterrupt::LINE_STATE);

	CoreTiming::ScheduleEvent(50000000, recv_event);
}

void CEXIModem::SetInterrupt(ModemInterrupt interrupt)
{
	exi_state.interrupt |= (u8)interrupt;
	ExpansionInterface::ScheduleUpdateInterrupts(0);
}

void CEXIModem::HandleDial(const std::string& dial_string, std::string* error_status)
{
	std::string number = dial_string;

	if (number.length() >= 1 && (number[0] == 'T' || number[0] == 'P'))
	{
		number.erase(0, 1);
	}

	modem_state.cancel_connect = false;
	pending_connect_number = std::move(number);

	CoreTiming::ScheduleEvent(500000, connect_event);
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

void CEXIModem::ResetComms()
{
	if (m_socket)
	{
		m_socket->disconnect();
	}

	data_state.input_pos = 0;
	data_state.input_end = 0;
	data_state.output_pos = 0;

	CoreTiming::RemoveAllEvents(recv_event);
}

const char* CEXIModem::GetRegisterName(ModemRegister reg) const
{
#define STR_RETURN(x) case ModemRegister::x: return #x;

	switch (reg)
	{
		STR_RETURN(EXI_ID)
		STR_RETURN(INTERRUPT_MASK)
		STR_RETURN(INTERRUPT_CAUSE)
		STR_RETURN(AT_DATA)
		STR_RETURN(PENDING_AT_CMD)
		STR_RETURN(PENDING_AT_RES)
		STR_RETURN(UNKNOWN1)
		STR_RETURN(UNKNOWN2)
		STR_RETURN(DATA)
		STR_RETURN(SEND_PENDING_H)
		STR_RETURN(SEND_PENDING_L)
		STR_RETURN(RECV_PENDING_H)
		STR_RETURN(RECV_PENDING_L)
		STR_RETURN(ESR)
		STR_RETURN(SEND_THRESH_H)
		STR_RETURN(SEND_THRESH_L)
		STR_RETURN(RECV_THRESH_H)
		STR_RETURN(RECV_THRESH_L)
		STR_RETURN(RAW_STATUS)
		STR_RETURN(FWT)
	default: return "unknown";
	}

#undef STR_RETURN
}

bool CEXIModem::TransmitData()
{
	if (m_socket && m_socket->send(data_state.output_buffer, data_state.output_pos) == sf::Socket::Status::Done)
	{
		data_state.output_pos = 0;
		return true;
	}
	
	return false;
}

bool CEXIModem::ReceiveData()
{
	DEBUG_LOG(SP1, "Recv");

	size_t recv_max = MODEM_DATA_BUFFER_SIZE - data_state.input_end;

	if (recv_max == 0)
	{
		return true;
	}

	size_t received;
	sf::Socket::Status status = m_socket->receive(data_state.input_buffer, recv_max, received);

	if (m_socket && status == sf::Socket::Status::Done)
	{
		data_state.input_end += (u16)received;

		if (data_state.input_end - data_state.input_pos)
		{
			SetInterrupt(ModemInterrupt::RECV_THRESH);
		}

		return true;
	}

	if (status == sf::Socket::Status::NotReady)
	{
		return true;
	}

	return false;
}


// Event callbacks

void CEXIModem::ConnectCallback(u64 userdata, int cycles_late)
{
	if (instance)
	{
		instance->PerformPendingConnect();
	}
}

void CEXIModem::RecvCallback(u64 userdata, int cycles_late)
{
	if (instance)
	{
		if (instance->ReceiveData())
		{
			// Re-schedule if everything is alright
			CoreTiming::ScheduleEvent(50000000, recv_event);
		}
	}
}
