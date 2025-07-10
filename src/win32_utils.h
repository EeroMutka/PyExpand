
struct OS_RunProcessPrintCallback {
	void (*Print)(OS_RunProcessPrintCallback* self, const char* message);
};

// NOTE: command_string may be modified by OS_RunCommand! Internally, CreateProcessW may write to it.
bool OS_RunConsoleCommand(DS_StringView command_string, bool wait_for_finish, uint32_t* out_exit_code = NULL, OS_RunProcessPrintCallback* print = NULL);

bool OS_DeleteFile(const char* filepath);
