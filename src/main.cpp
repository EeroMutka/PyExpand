#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

#include "ds/ds.h"

#include "win32_utils.h"

static bool ReadEntireFile(DS_Arena* arena, const char* filepath, DS_StringView* out_data)
{
	FILE* f = NULL;
	errno_t err = fopen_s(&f, filepath, "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		long fsize = ftell(f);
		fseek(f, 0, SEEK_SET);

		char* data = arena->PushUninitialized(fsize);
		fread(data, fsize, 1, f);

		fclose(f);
		*out_data = {data, fsize};
	}
	return f != NULL;
}

// Usage:
// PyExpand my_file.cpp
int main(int argc, const char** argv)
{
    DS_ScopedArena<2048> arena;

    if (argc != 2)
    {
        printf("Please provide exactly one argument (the file name)!\n");
        return 1;
    }

    const char* filepath = argv[1];
    
    DS_StringView file_data;
    if (!ReadEntireFile(&arena, filepath, &file_data))
    {
        printf("Failed to read file '%s'!\n", filepath);
        return 1;
    }
    
    DS_Array<DS_StringView> ranges_to_keep(&arena);
    DS_Array<DS_StringView> python_strings(&arena);
    DS_Array<bool> python_strings_is_multiline(&arena);
    DS_Array<DS_StringView> python_results(&arena);

    DS_StringView remaining = file_data;
    for (;;)
    {
        DS_StringView pyexpand_keyword = "/*.py";
        intptr_t pyexpand_offset = remaining.Find(pyexpand_keyword);
        if (pyexpand_offset == remaining.Size)
            break;

        intptr_t end_comment_offset = remaining.Find("*/", pyexpand_offset + pyexpand_keyword.Size);
        DS_StringView python_string = remaining.Slice(pyexpand_offset + pyexpand_keyword.Size, end_comment_offset);
        ranges_to_keep.Add(remaining.Slice(0, end_comment_offset + 2));

        intptr_t terminator_comment_offset = remaining.Find("/*", end_comment_offset + 2);
        remaining = remaining.Slice(terminator_comment_offset);

        DS_DynamicString new_python_string(&arena);

        bool is_multiline = python_string.Find("return") != python_string.Size;
        if (is_multiline)
        {
            new_python_string.Add("def user_fn():\n");
            DS_StringView lines = python_string;
            while (lines.Size > 0)
            {
                DS_StringView line = lines.Split("\n");
                if (line.Size > 0 && line.Data[line.Size - 1] == '\r')
                    line.Size -= 1;
                
                if (line.Size > 0)
                {
                    if (line.Data[0] != '\t' && line.Data[0] != ' ')
                        new_python_string.Add("\t");
                    new_python_string.Add(line);
                    new_python_string.Add("\n");
                }
            }
            new_python_string.Addf("print(user_fn())\n");
        }
        else
        {
            DS_StringView lines = python_string;
            int lines_count = 0;
            while (lines.Size > 0)
            {
                DS_StringView line = lines.Split("\n");
                lines_count += 1;
            }
            if (lines_count <= 1)
            {
                new_python_string.Addf("print(%.*s)\n", python_string.Size, python_string.Data);
            }
            else
                new_python_string.Add("print('Error: No return statement found in a multiline code block!')");
        }

        python_strings.Add(new_python_string);
        python_strings_is_multiline.Add(is_multiline);
    }
    ranges_to_keep.Add(remaining);

    for (int i = 0; i < python_strings.Size; i++)
    {
        FILE* f = fopen("__pyexpand_temp.py", "wb");
        if (!f)
        {
            printf("Failed to create a temporary python file for evaluating python expressions!\n");
            return 1;
        }
        DS_StringView python_str = python_strings[i];
        fprintf(f, "%.*s\n", (int)python_str.Size, python_str.Data);
        fclose(f);

        struct PrintCallback {
            OS_RunProcessPrintCallback Base;
            DS_DynamicString Result;
        } print_callback;
        print_callback.Result.Init(&arena);
        print_callback.Base.Print = [](OS_RunProcessPrintCallback* self, const char* message) {
            ((PrintCallback*)self)->Result.Addf("%s", message);
        };
        
        uint32_t exit_code;
        if (!OS_RunConsoleCommand("py __pyexpand_temp.py", true, &exit_code, &print_callback.Base))
        {
            printf("Failed to call python. Do you have python installed?\n");
            return 1;
        }
        printf("Python exit code: %d\n", exit_code);
        printf("Python exit str: %s\n", print_callback.Result.CStr());

        DS_StringView python_result = print_callback.Result;
        if (python_result.Slice(python_result.Size - 2) == "\r\n")
            python_result = python_result.Slice(0, python_result.Size - 2);

        python_results.Add(python_result);
    }
    
    OS_DeleteFile("__pyexpand_temp.py");

    {
        DS_DynamicString result(&arena);

        for (int i = 0; i < ranges_to_keep.Size; i++)
        {
            if (i > 0)
            {
                DS_StringView python_string = python_results[i - 1];
                int indent = 0;
                for (; indent < python_string.Size; indent++)
                    if (python_string.Data[indent] != ' ' && python_string.Data[indent] != '\t')
                        break;
                DS_StringView indent_str = python_string.Slice(0, python_strings_is_multiline[i - 1] ? indent : 0);

                result.Add(python_strings_is_multiline[i - 1] ? "\n" : " ");
                result.Add(python_results[i - 1]);
                result.Add(python_strings_is_multiline[i - 1] ? "\n" : " ");
                result.Add(indent_str);
            }
            result.Add(ranges_to_keep[i]);
        }

        FILE* f = fopen(filepath, "wb");
        if (!f)
        {
            printf("Failed to open the target file for writing the result!\n");
            return 1;
        }
        fprintf(f, "%s", result.CStr());

        fclose(f);
    }

    return 0;
}