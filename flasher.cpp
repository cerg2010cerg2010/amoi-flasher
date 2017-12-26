#include <windows.h>
#include <winusb.h>
#include <setupapi.h>

#include <stdio.h>

typedef struct WINUSB_DEVICE_CONTEXT
{   
	BSTR bszPath;
	WINUSB_INTERFACE_HANDLE Dev;
	UCHAR BulkOutPipe;
	ULONG BulkOutMaxPacket;
	UCHAR BulkInPipe;
	ULONG BulkInMaxPacket;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;



//void ReadData(HANDLE hDriver, DEVICE_CONTEXT winusbContext, OVERLAPPED readOverlapped);
void ReadData(HANDLE hDriver, DEVICE_CONTEXT winusbContext, OVERLAPPED readOverlapped, BYTE *paBuffer, DWORD cbBuffer);
ULONG WriteData(HANDLE hDriver, DEVICE_CONTEXT winusbContext, OVERLAPPED writeOverlapped, BYTE *pbData, ULONG cData);
TCHAR * GetDevicePnpPath();

// This was found in default AmoiFlash R04
#define BLOCK_SIZE 0x40000
static void flash_file(HANDLE hDriver, DEVICE_CONTEXT winusbContext, OVERLAPPED readOverlapped, OVERLAPPED writeOverlapped, BYTE *paBuffer, const UINT cbBuffer) {
	unsigned int to_write = 0;
	unsigned int write_sz = 0;
	unsigned int cmd_buf[3] = {0};
	unsigned char dummy_buf[16] = {0};
	unsigned long block_count = 0;
	printf("Start Flashing!\n");

	if (cbBuffer == 0)
		return;

	to_write = cbBuffer;
	cmd_buf[0] = to_write / BLOCK_SIZE;
	if (to_write % BLOCK_SIZE)
		++cmd_buf[0];
	printf("Total blocks to write: %d, total bytes: %d\n", cmd_buf[0], cbBuffer);
	while (to_write > 0) {
		memset(dummy_buf, 0, sizeof(dummy_buf));
		ReadData(hDriver, winusbContext, readOverlapped, dummy_buf, sizeof(dummy_buf));

		write_sz = BLOCK_SIZE;
		if (!write_sz || to_write <= write_sz)
			write_sz = to_write;
		to_write -= write_sz;
		cmd_buf[2] = 0x123;
		cmd_buf[1] = write_sz;
		--cmd_buf[0];
		WriteData(hDriver, winusbContext, writeOverlapped, (BYTE*) cmd_buf, sizeof(cmd_buf));

		WriteData(hDriver, winusbContext, writeOverlapped, paBuffer, write_sz);
		paBuffer += write_sz;
		printf("Block %ld written, bytes remaining: %d\n", block_count++, to_write);
	}
	printf("Flashed!\n");
}

static void print_error(DWORD code, char* str) {
	LPSTR lpMsgBuf = NULL;

	FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | 
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			code,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			// MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR) &lpMsgBuf,
			0, NULL );

	printf("%s %s\n", str, lpMsgBuf);
	LocalFree(lpMsgBuf);
}

void ReadData(HANDLE hDriver, DEVICE_CONTEXT winusbContext, OVERLAPPED readOverlapped, BYTE *paBuffer, DWORD cbBuffer)
{
	ULONG cBytes            = 0;
	BOOL  fResult           = FALSE;
#if 0
	if (cbBuffer <= winusbContext.BulkInMaxPacket)
	{
		printf("ReadData: cbBuffer <= winusbContext.BulkInMaxPacket\n");
		goto Error;
	}
#endif
	fResult = WinUsb_ReadPipe(winusbContext.Dev,
			winusbContext.BulkInPipe,
			paBuffer,
			cbBuffer,
			&cBytes,
			&readOverlapped);

	if (!fResult)
	{
		DWORD code = GetLastError();
		if (code == ERROR_IO_PENDING)
		{
			WaitForSingleObject(readOverlapped.hEvent, INFINITE);

			fResult = GetOverlappedResult(hDriver, &readOverlapped, &cBytes, FALSE);
			if (!fResult)
			{
				print_error(GetLastError(), "Error: WinUsb_ReadPipe, GetOverlappedResult, WaitForSingleObject:");
				goto Error;
			}
		}
		else
		{
			print_error(code, "Error: WinUsb_ReadPipe:");
			goto Error;
		}
	}

Error:

	return;
}

#define WRITE_SIZE 4096
// #define WRITE_SIZE (winusbContext.BulkInMaxPacket)
ULONG WriteData(HANDLE hDriver, DEVICE_CONTEXT winusbContext, OVERLAPPED writeOverlapped, BYTE *pbData, ULONG cData)
{
	ULONG   cbWritten = 0;
	ULONG   cbWrite = 0;
	ULONG   cbTotalWritten = 0;
	BOOL    fResult   = FALSE;

	//
	// In case the data is bigger than the max packet, write in a loop
	//
	while (cData)
	{
		if (cData > WRITE_SIZE)
		{
			cbWrite = WRITE_SIZE;
		}
		else
		{
			cbWrite = cData;
		}

		fResult = WinUsb_WritePipe(winusbContext.Dev,
				winusbContext.BulkOutPipe,
				pbData,
				cbWrite,
				&cbWritten,
				&writeOverlapped);

		if (!fResult)
		{
			DWORD code = GetLastError();
			if (code == ERROR_IO_PENDING)
			{
				WaitForSingleObject(writeOverlapped.hEvent, INFINITE);

				fResult = GetOverlappedResult(hDriver, &writeOverlapped, &cbWritten, FALSE);
				if (!fResult)
				{
					print_error(GetLastError(), "Error: WinUsb_ReadPipe, GetOverlappedResult, WaitForSingleObject:");
					cbTotalWritten = 0;
					goto Error;
				}
			}
			else
			{
				print_error(code, "Error: WinUsb_ReadPipe:");
				cbTotalWritten = 0;
				goto Error;
			}
		}

		cData -= cbWritten;
		pbData += cbWritten;
		cbTotalWritten += cbWritten;
	}

Error:

	return cbTotalWritten;
}

//
//  DeviceInterface GUID for CE USB serial devices
//                        
GUID IID_CEUSBDEVICE = {0x25dbce51,0x6c8f,0x4a72,0x8a,0x6d,0xb5,0x4c,0x2b,0x4f,0xc8,0x35};

TCHAR * GetDevicePnpPath()
{
	HRESULT                             hr                  = E_FAIL;
	DWORD                               dwError             = 0;
	DWORD                               iInterface          = 0;
	HDEVINFO                            hdevClassInfo       = NULL;
	SP_INTERFACE_DEVICE_DATA            InterfaceDeviceData = {0};
	PSP_INTERFACE_DEVICE_DETAIL_DATA    pDeviceDetailData   = {0};

	//initialize variables
	InterfaceDeviceData.cbSize = sizeof(SP_INTERFACE_DEVICE_DATA);

	//
	//  Open the device for serial communications.
	//

	hdevClassInfo = SetupDiGetClassDevs(&IID_CEUSBDEVICE,
			NULL,
			NULL,
			DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);

	if (hdevClassInfo == INVALID_HANDLE_VALUE) {
		print_error(GetLastError(), "Error: hdevClassInfo == INVALID_HANDLE_VALUE:");
		goto Error;
	}

	while (SetupDiEnumDeviceInterfaces(hdevClassInfo,
				NULL,
				(LPGUID)&IID_CEUSBDEVICE,
				iInterface,
				&InterfaceDeviceData))
	{
		DWORD               DetailSize      = 0;
		ULONG               ulStatus        = 0;
		ULONG               ulProblem       = 0;
		SP_DEVINFO_DATA     DeviceInfoData  = {0};
		//CONFIGRET           cfgRet          = CR_SUCCESS;

		SetupDiGetDeviceInterfaceDetail(hdevClassInfo,
				&InterfaceDeviceData,
				NULL,
				0,
				&DetailSize,
				NULL);                 

		//Allocate memory for the Device Detail Data
		if ((pDeviceDetailData = (PSP_INTERFACE_DEVICE_DETAIL_DATA)
					malloc(DetailSize)) == NULL)
		{
			printf("GetDevicePnpPath malloc == NULL\n");
			goto Error;
		}

		// Initialize the device detail data structure
		pDeviceDetailData->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);
		DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

		// Get the Device Detail Data
		//  This gives us the path that we can open with CreateFile
		if (SetupDiGetDeviceInterfaceDetail(hdevClassInfo,
					&InterfaceDeviceData,
					pDeviceDetailData,
					DetailSize,
					NULL,
					&DeviceInfoData))
		{
			TCHAR *ret;

			printf("Found device: %s\n", pDeviceDetailData->DevicePath);
			ret = (TCHAR*) malloc(strlen(pDeviceDetailData->DevicePath) + 1);
			strcpy(ret, pDeviceDetailData->DevicePath);
			free(pDeviceDetailData);
			return ret;
		}
		else
		{
			dwError = GetLastError();
			print_error(dwError, "SetupDiGetDeviceInterfaceDetail:");
		}

		iInterface++;
		free(pDeviceDetailData);
		pDeviceDetailData = NULL;
	}

	dwError = GetLastError();
	if (dwError != ERROR_NO_MORE_ITEMS)
	{
		print_error(GetLastError(), "Error: SetupDiGetDeviceInterfaceDetail:");
		goto Error;
	}

Error:
	if(pDeviceDetailData)
	{
		free(pDeviceDetailData);
	}

	if(hdevClassInfo)
	{
		SetupDiDestroyDeviceInfoList(hdevClassInfo);
	}

	return NULL;
} 

#if 0
int WINAPI WinMain(HINSTANCE hInstance,      // handle to current instance
		HINSTANCE hPrevInstance,  // handle to previous instance
		LPTSTR lpCmdLine,          // pointer to command line
		int nCmdShow)             // show state of window
{
	ReadWriteWinusb();
	return 0;
}
#endif

int main(int argc, char** argv) {
	HANDLE      hDriver         = INVALID_HANDLE_VALUE;
	OVERLAPPED  readOverlapped  = {0};
	OVERLAPPED  writeOverlapped = {0};
	DEVICE_CONTEXT winusbContext        = {0};
	WINUSB_PIPE_INFORMATION pipe        = {UsbdPipeTypeControl};
	WINUSB_INTERFACE_HANDLE hInt        = NULL;
	USB_INTERFACE_DESCRIPTOR usbDescriptor = {0};
	DWORD   iEndpoint           = 0;
	DWORD err = 0;
	TCHAR *devicePath = NULL;
	BYTE *paBuffer = NULL;
	DWORD cbBuffer = 0;
	int i = 0;
	HANDLE file_handle = INVALID_HANDLE_VALUE;
	DWORD file_size_high = 0;
	HANDLE file_mapping = INVALID_HANDLE_VALUE;

	if (argc != 2) {
		printf("Usage: %s <file>\n", argv[0]);
		goto Error;
	}

	file_handle = CreateFile(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
			FILE_FLAG_SEQUENTIAL_SCAN
			| FILE_ATTRIBUTE_ARCHIVE 
			| FILE_ATTRIBUTE_SYSTEM
			| FILE_ATTRIBUTE_HIDDEN
			| FILE_ATTRIBUTE_READONLY,
			NULL);
	if (file_handle == INVALID_HANDLE_VALUE) {
		print_error(GetLastError(), "Error opening firmware file:");
		goto Error;
	}
	cbBuffer = GetFileSize(file_handle, &file_size_high);

	file_mapping = CreateFileMapping(file_handle, NULL, PAGE_READONLY, 0, 0, "maketest");
	if (file_mapping == NULL) {
		print_error(GetLastError(), "Error creating file mapping:");
		goto Error;
	}

	paBuffer = (BYTE*) MapViewOfFile(file_mapping, FILE_MAP_READ, 0, 0, cbBuffer);
	if (paBuffer == NULL) {
		print_error(GetLastError(), "Mapped file is NULL:");
		goto Error;
	}

	devicePath = GetDevicePnpPath();
	if (devicePath == NULL) {
		printf("Could not find device, check your USB cable!\n");
		goto Error;
	}
	hDriver = CreateFile(devicePath, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 0);

	err = GetLastError();
	if (err == 0x05) {
		printf("Access denied, make sure you have stopped the mobile device center cervice WcesComm.\n");
		goto Error;
	}

	if (hDriver == INVALID_HANDLE_VALUE) {
		printf("Error: CreateFile: INVALID_HANDLE_VALUE\n");
		goto Error;
	}

	//
	// Open the driver with WinUSB
	//
	if (!WinUsb_Initialize(hDriver, &(winusbContext.Dev)))
	{
		print_error(GetLastError(), "Error: WinUsb_Initialize:");
		goto Error;
	}

	//
	// We should have exactly one interface
	//
	if (!WinUsb_QueryInterfaceSettings(winusbContext.Dev,
				0,
				&usbDescriptor))
	{
		print_error(GetLastError(), "Error: WinUsb_QueryInterfaceSettings:");
		goto Error;
	}

	//
	// Get the read and write pipes to the device
	//
	for (iEndpoint = 0; iEndpoint < usbDescriptor.bNumEndpoints; iEndpoint++)
	{
		if (!WinUsb_QueryPipe(winusbContext.Dev,
					0,
					(UCHAR)iEndpoint,
					&pipe))
		{
			print_error(GetLastError(), "Error: WinUsb_QueryPipe:");
			goto Error;
		}

		if ((pipe.PipeType == UsbdPipeTypeBulk) && (USB_ENDPOINT_DIRECTION_OUT(pipe.PipeId)))
		{

			winusbContext.BulkOutPipe = pipe.PipeId;
			winusbContext.BulkOutMaxPacket = pipe.MaximumPacketSize;
		}
		else if (pipe.PipeType == UsbdPipeTypeBulk)
		{
			winusbContext.BulkInPipe = pipe.PipeId;
			winusbContext.BulkInMaxPacket = pipe.MaximumPacketSize;
		}
	}

	//
	//  Create an event to block while waiting on a read overlapped I/O to complete.
	//
	readOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (readOverlapped.hEvent == NULL) {
		print_error(GetLastError(), "Error: readOverlapped.CreateEvent:");
		goto Error;
	}

	//
	//  Create an event to block while waiting on a write overlapped I/O to complete.
	//
	writeOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (writeOverlapped.hEvent == NULL) {
		print_error(GetLastError(), "Error: WinUsb_QueryInterfaceSettings:");
		goto Error;
	}

	flash_file(hDriver, winusbContext, readOverlapped, writeOverlapped, paBuffer, cbBuffer);
	//
	// Read some data from the device
	//
	//ReadData(hDriver, winusbContext, readOverlapped, paBuffer, cbBuffer);

	//
	// Write Some data to the device
	//
	//WriteData(hDriver, winusbContext, writeOverlapped);

	//
	// Close the device
	//
Error:
	if (winusbContext.Dev)
	{
		if (!WinUsb_Free(winusbContext.Dev))
		{
			print_error(GetLastError(), "Error: WinUsb_Free:");
			//printf("-WinUsb_Free failed");
		}
		winusbContext.Dev = NULL;
	}

	if (hDriver != INVALID_HANDLE_VALUE) {
		CloseHandle(hDriver);
	}

	//
	//  Release the overlapped I/O event handle.
	//
	if (readOverlapped.hEvent)
	{
		CloseHandle(readOverlapped.hEvent);
		readOverlapped.hEvent = NULL;
	}

	if (writeOverlapped.hEvent)
	{
		CloseHandle(writeOverlapped.hEvent);
		writeOverlapped.hEvent = NULL;
	}

	if (devicePath != NULL) {
		free(devicePath);
	}

	if (paBuffer != NULL) {
		UnmapViewOfFile((LPCVOID) paBuffer);
	}

	if (file_mapping != INVALID_HANDLE_VALUE) {
		CloseHandle(file_mapping);
	}

	if (file_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(file_handle);
	}
	return 0;
}
