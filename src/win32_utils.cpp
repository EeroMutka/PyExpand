#include "ds/ds.h"

#include "win32_utils.h"
#include <Windows.h>

wchar_t* OS_UTF8ToWide(DS_Arena* arena, DS_StringView str, int null_terminations)
{
	if (str.Size == 0) return (wchar_t*)L""; // MultiByteToWideChar does not accept 0-length strings

	int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.Data, (int)str.Size, NULL, 0);
	wchar_t* result = (wchar_t*)arena->PushUninitialized((size + null_terminations) * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.Data, (int)str.Size, result, size);

	for (int i = 0; i < null_terminations; i++)
		result[size + i] = 0;
	return result;
}

bool OS_RunConsoleCommand(DS_StringView command_string, bool wait_for_finish, uint32_t* out_exit_code, OS_RunProcessPrintCallback* print)
{
	DS_ScopedArena<1024> temp;
	wchar_t* command_string_wide = OS_UTF8ToWide(&temp, command_string, 1); // NOTE: CreateProcessW may write to command_string_wide in place!

	// https://learn.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output

	PROCESS_INFORMATION process_info = {0};

	// Initialize pipes
	SECURITY_ATTRIBUTES security_attrs = {0};
	security_attrs.nLength = sizeof(SECURITY_ATTRIBUTES);
	security_attrs.lpSecurityDescriptor = NULL;
	security_attrs.bInheritHandle = 1;

	HANDLE OUT_Rd = NULL, OUT_Wr = NULL;
	HANDLE ERR_Rd = NULL, ERR_Wr = NULL;

	bool ok = true;
	if (ok) ok = CreatePipe(&OUT_Rd, &OUT_Wr, &security_attrs, 0);
	if (ok) ok = CreatePipe(&ERR_Rd, &ERR_Wr, &security_attrs, 0);
	if (ok) ok = SetHandleInformation(OUT_Rd, HANDLE_FLAG_INHERIT, 0);
	if (ok) ok = SetHandleInformation(ERR_Rd, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW startup_info = {0};
	startup_info.cb = sizeof(STARTUPINFOW);
	startup_info.dwFlags = STARTF_USESTDHANDLES;
	startup_info.hStdOutput = OUT_Wr;
	startup_info.hStdError = ERR_Wr;
	startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

	if (ok) ok = CreateProcessW(NULL, command_string_wide, NULL, NULL, true, CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &startup_info, &process_info);

	// We don't need these handles for ourselves and we must close them to say that we won't be using them, to let `ReadFile` exit
	// when the process finishes instead of locking. At least that's how I think it works.
	if (OUT_Wr) CloseHandle(OUT_Wr);
	if (ERR_Wr) CloseHandle(ERR_Wr);

	if (ok) {
		if (wait_for_finish)
		{
			if (print)
			{
				char buf[512];
				uint32_t num_read_bytes;
				for (;;) {
					if (!ReadFile(OUT_Rd, buf, sizeof(buf) - 1, (DWORD*)&num_read_bytes, NULL)) break;

					buf[num_read_bytes] = 0; // null termination
					print->Print(print, buf);
				}
				for (;;) {
					if (!ReadFile(ERR_Rd, buf, sizeof(buf) - 1, (DWORD*)&num_read_bytes, NULL)) break;

					buf[num_read_bytes] = 0; // null termination
					print->Print(print, buf);
				}
			}

			WaitForSingleObject(process_info.hProcess, INFINITE);
		}

		if (out_exit_code && !GetExitCodeProcess(process_info.hProcess, (DWORD*)out_exit_code)) ok = false;

		CloseHandle(process_info.hProcess);
		CloseHandle(process_info.hThread);
	}

	if (OUT_Rd) CloseHandle(OUT_Rd);
	if (ERR_Rd) CloseHandle(ERR_Rd);
	return ok;
}

bool OS_DeleteFile(const char* filepath)
{
	DS_ScopedArena<1024> temp;
	wchar_t* filepath_wide = OS_UTF8ToWide(&temp, DS_StringView(filepath, strlen(filepath)), 1);
	BOOL ok = DeleteFileW(filepath_wide);
	return (bool)ok;
}
