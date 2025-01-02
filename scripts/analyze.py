"""
author: Jason Heflinger
description: Provides basic project metrics
"""

import os
import re

statement = "\n"

def clean_sloc_line(input_string):
    pattern = r'[ \n\r\t{};()]'
    cleaned_string = re.sub(pattern, '', input_string)
    return cleaned_string

def trim_whitespace(input_string):
    pattern = r'[ \n\r\t]'
    cleaned_string = re.sub(pattern, '', input_string)
    return cleaned_string

def pad_num(num):
    num_spaces = max(0, 15 - len(str(num)))
    padded_string = ' ' * num_spaces + str(num)
    return padded_string

def analyze_code(directory, typedes):
    total_sloc = 0
    total_files = 0
    total_hdrs = 0
    total_srcs = 0
    total_classes = 0
    total_structs = 0
    total_typedefs = 0
    total_includes = 0
    total_lines = 0
    for root, dirs, files in os.walk(directory):
        for file in files:
            filepath = os.path.join(root, file)
            current_sloc = 0
            good_file = False
            if ".cpp" in filepath or ".c" in filepath:
                total_srcs += 1
                good_file = True
            if ".h" in filepath:
                total_hdrs += 1
                good_file = True
            total_files += 1
            if good_file:
                with open(filepath, 'r') as file:
                    for line in file:
                        total_lines += 1
                        if len(clean_sloc_line(line)) > 0:
                            current_sloc += 1
                        if "typedef struct" in line:
                            total_structs += 1
                        if len(line) > 7 and "typedef" == line[0:7]:
                            total_typedefs += 1
                        if len(line) > 8 and "#include" == line[0:8]:
                            total_includes += 1
            total_sloc += current_sloc
    return (typedes + " ANALYSIS: \n" + 
            "┌──────────────────┬───────────────┐\n"
            "│1. TOTAL LINES    │" + pad_num(total_lines) + "│\n" +
            "├──────────────────┼───────────────┤\n"
            "│2. TOTAL SLOC     │" + pad_num(total_sloc) + "│\n" +
            "├──────────────────┼───────────────┤\n"
            "│3. HEADER FILES   │" + pad_num(total_hdrs) + "│\n" +
            "├──────────────────┼───────────────┤\n"
            "│4. SOURCE FILES   │" + pad_num(total_srcs) + "│\n" +
            "├──────────────────┼───────────────┤\n"
            "│5. TOTAL FILES    │" + pad_num(total_files) + "│\n" +
            "├──────────────────┼───────────────┤\n"
            "│6. STRUCTS        │" + pad_num(total_structs) + "│\n" +
            "├──────────────────┼───────────────┤\n"
            "│7. TYPEDEFS       │" + pad_num(total_typedefs) + "│\n" +
            "├──────────────────┼───────────────┤\n"
            "│8. INCLUDES       │" + pad_num(total_includes) + "│\n" +
            "└──────────────────┴───────────────┘\n")

print("Performing project analysis...")

print("Analyzing project code...")
statement += analyze_code("src", "CODEBASE") + "\n"
print("Finished analyzing project code!")

print("Analyzing vendors...")
statement += analyze_code("vendor", "VENDOR") + "\n"
print("Finished analyzing vendor code!")

print(statement)
