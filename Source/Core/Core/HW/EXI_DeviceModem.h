// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <mutex>
#include <atomic>

#include "SFML/Network.hpp"

#include "Common/Thread.h"
#include "Core/HW/EXI_Device.h"
#include "Core/HW/EXI_DeviceModem_ATParser.h"

class PointerWrap;

/*
* Info from tueidj:
*
* GC MODEM registers:
* 0x01: interrupt mask
* 0x02: interrupt cause
* 0x03: AT command/response data
* 0x04: length of pending AT commands
* 0x05: length of pending AT responses
* 0x06: unknown (set to 0x32)
* 0x07: unknown (set to 0x40), do these relate to AT command/response buffer sizes?
* 0x08: serial data in/out
* 0x09: number of bytes queued to be sent (high byte)
* 0x0A: number of bytes queued to be sent (low byte)
* 0x0B: number of bytes waiting to be read (high byte)
* 0x0C: number of bytes waiting to be read (low byte)
* 0x0D: ESR ??
* 0x0E: send buffer threshold (high byte)
* 0x0F: send buffer threshold (low byte)
* 0x10: recv buffer threshold (high byte)
* 0x11: recv buffer threshold (low byte)
* 0x12: raw status ??
* 0x13: FWT ?? (set to 5 after reset, 0 after connecting)
* registers OR'd with 0x40 are being written to
* registers OR'd with 0x20 means next two bytes are the payload length
* i.e. 0x681234 = write x1234 bytes of serial data (data will follow)
* 0x284312 = read x4312 bytes of serial data from internal buffer (wouldn't happen, buffer seems to be 512 bytes max)
*
* "four known interrupt lines: 0x2 = connect/line state change, 0x10 = send threshold clear,
* 0x20 = recv threshold reached, 0x40 = recv buffer overflow? (not sure, can't confirm without hardware)"
*/

// Interrupt Bitflag values
enum class ModemInterrupt : u8
{
	LINE_STATE =  0x02,
	RECV_THRESH = 0x20
};


// Register numbers
enum class ModemRegister : u8
{
	EXI_ID,
	INTERRUPT_MASK,
	INTERRUPT_CAUSE,
	AT_DATA,
	PENDING_AT_CMD,
	PENDING_AT_RES,
	UNKNOWN1,
	UNKNOWN2,
	DATA,
	SEND_PENDING_H,
	SEND_PENDING_L,
	RECV_PENDING_H,
	RECV_PENDING_L,
	ESR,
	SEND_THRESH_H,
	SEND_THRESH_L,
	RECV_THRESH_H,
	RECV_THRESH_L,
	RAW_STATUS,
	FWT
};

enum
{
	MODEM_AT_BUFFER_SIZE = 0x200,
	MODEM_DATA_BUFFER_SIZE = 0x200
};

enum
{
	EXI_DEVTYPE_MODEM = 0x02020000
};


class CEXIModem : public IEXIDevice, IATHandler
{
public:
	CEXIModem();
	virtual ~CEXIModem();

	// IEXIDevice
	void SetCS(int cs) override;
	bool IsPresent() const override;
	bool IsInterruptSet() override;
	void ImmWrite(u32 data,  u32 size) override;
	u32  ImmRead(u32 size) override;
	void DMAWrite(u32 addr, u32 size) override;
	void DMARead(u32 addr, u32 size) override;
	void DoState(PointerWrap &p) override;

	// IATHandler
	void HandleATCommand(const std::string& command, const std::string& arg, std::string* error_status) override;

	void HandleDial(const std::string& dial_string, std::string* error_status) override;

	void HandleExtendedCommand(const std::string& command, const std::vector<std::string>& args, std::string* error_status) override;
	void QueryExtendedCommand(const std::string& command, std::string* error_status) override;
	void GetExtendedParameter(const std::string& command, std::string* error_status) override;

	void SetSParameter(u16 param, u16 value, std::string* error_status) override;
	void GetSParameter(u16 param, std::string* error_status) override;
	void ResetSParameter(u16 param, std::string* error_status) override;

private:
	struct EXIState
	{
		u8 interruptMask = 0;
		u8 interrupt = 0;

		ModemRegister currentRegister = ModemRegister::EXI_ID;
		u16 pendingWriteBytes = 0;

		EXIState()
		{
			interruptMask = 0;
			interrupt = 0;

			currentRegister = ModemRegister::EXI_ID;
			pendingWriteBytes = 0;
		}
	} exi_state;

	struct ModemState
	{
		bool cancel_connect;
	} modem_state;

	std::string pending_connect_number;

	struct ATState
	{
		bool at_command;
		bool received_a;

		u8 cmdBuffer[MODEM_AT_BUFFER_SIZE];
		u16 cmdPos;
		bool cmd_buffer_overflow;

		u8 resBuffer[MODEM_AT_BUFFER_SIZE];
		u16 resPos;
		u16 resEnd;

		// Whether AT input is echoed; controlled via ATE
		bool echo;

		ATState()
		{
			cmdPos = 0;
			resPos = 0;
			resEnd = 0;

			// Recommended default per V.250 6.2.4
			echo = true;
		}
	} at_state;

	struct DataState
	{
		u8 output_buffer[MODEM_DATA_BUFFER_SIZE];
		u16 output_pos;

		u8 input_buffer[MODEM_DATA_BUFFER_SIZE];
		u16 input_pos;
		u16 input_end;
		bool input_buffer_overflow;

		DataState()
		{
			output_pos = 0;
			input_pos = 0;
			input_end = 0;

			input_buffer_overflow = false;
		}
	} data_state;

	// Previous command, to support A/
	std::string at_prev_cmd;

	CATParser parser;
	std::unique_ptr<sf::TcpSocket> m_socket;

	static CEXIModem* instance;
	static int connect_event;
	static int recv_event;

	void WriteData(u32 data, u8 length);
	u8 ReadData();

	void WriteATCommand(u32 data, u8 length);
	u8 ReadATResponse();

	void RespondAT(const char* answer);
	void RespondATChar(char answer);

	void ResetComms();

	const char* GetRegisterName(ModemRegister reg) const;

	static bool PhoneNumberToAddress(const std::string& number, std::string* ip, u16* port);
	void PerformPendingConnect();
	
	void SetInterrupt(ModemInterrupt interrupt);

	bool TransmitData();
	bool ReceiveData();

	static void ConnectCallback(u64 userdata, int cycles_late);
	static void RecvCallback(u64 userdata, int cycles_late);
};
