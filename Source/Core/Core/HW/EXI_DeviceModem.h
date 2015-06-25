// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/Thread.h"
#include "Core/HW/EXI_Device.h"
#include "Core/HW/EXI_DeviceModem_ATParser.h"

class PointerWrap;


// Interrupt Mask Register
// Interrupt Register
enum ModemInterrupts
{
	
};


// Register numbers
enum ModemRegister
{
	MODEM_REG_EXI_ID,
	MODEM_REG_INTERRUPT_MASK,
	MODEM_REG_INTERRUPT_CAUSE,
	MODEM_REG_AT_DATA,
	MODEM_REG_PENDING_AT_CMD,
	MODEM_REG_PENDING_AT_RES,
	MODEM_REG_UNKNOWN1,
	MODEM_REG_UNKNOWN2,
	MODEM_REG_DATA,
	MODEM_REG_SEND_PENDING_H,
	MODEM_REG_SEND_PENDING_L,
	MODEM_REG_RECV_PENDING_H,
	MODEM_REG_RECV_PENDING_L,
	MODEM_REG_ESR,
	MODEM_REG_SEND_THRESH_H,
	MODEM_REG_SEND_THRESH_L,
	MODEM_REG_RECV_THRESH_H,
	MODEM_REG_RECV_THRESH_L,
	// ??
	MODEM_REG_RAW_STATUS,
	// ??
	MODEM_REG_FWT
};

enum
{
	MODEM_AT_BUFFER_SIZE = 0x200
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
	void SetCS(int cs) override;
	bool IsPresent() const override;
	bool IsInterruptSet() override;
	void ImmWrite(u32 data,  u32 size) override;
	u32  ImmRead(u32 size) override;
	void DMAWrite(u32 addr, u32 size) override;
	void DMARead(u32 addr, u32 size) override;
	void DoState(PointerWrap &p) override;

	static void SetConnInt(CEXIModem* obj);

	void HandleATCommand(const std::string& command, const std::string& arg, std::string* error_status) override;

	void HandleDial(const std::string& dial_string, std::string* error_status) override;

	void HandleExtendedCommand(const std::string& command, const std::vector<std::string>& args, std::string* error_status) override;
	void QueryExtendedCommand(const std::string& command, std::string* error_status) override;
	void GetExtendedParameter(const std::string& command, std::string* error_status) override;

	void SetSParameter(u16 param, u16 value, std::string* error_status) override;
	void GetSParameter(u16 param, std::string* error_status) override;
	void ResetSParameter(u16 param, std::string* error_status) override;

private:
	struct ATState
	{
		u8 cmdBuffer[MODEM_AT_BUFFER_SIZE];
		u16 cmdPos;
		// Previous command, to support A/
		std::string prevCmd;


		u8 resBuffer[MODEM_AT_BUFFER_SIZE];
		u8 resPos;
		u8 resEnd;

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

	struct EXIState
	{
		u8 interruptMask = 0;
		u8 interrupt = 0;

		ModemRegister currentRegister = MODEM_REG_EXI_ID;
		u16 pendingWriteBytes = 0;

		EXIState()
		{
			interruptMask = 0;
			interrupt = 0;

			currentRegister = MODEM_REG_EXI_ID;
			pendingWriteBytes = 0;
		}
	} exi_state;

	CATParser parser;

	void WriteData(u32 data, u8 length);
	void WriteATData(u32 data, u8 length);

	u8 ReadATData();

	void RespondAT(const char* answer);
	void RespondATChar(char answer);

	void ResetModem();

	const char* GetRegisterName(ModemRegister reg) const;
	bool RecvHandlePacket();

	u8 *tx_fifo;

	
	
};
