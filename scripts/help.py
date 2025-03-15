"""
author: Jason Heflinger
description: Features several helper commands to easily manage the project, ranging
             from creation of scenes, ui, to editing levels and auditing syntax.
"""

import sys
import os
import re

def intstr(myint):
    if myint < 10:
        return "00" + str(myint)
    if myint < 100:
        return "0" + str(myint)
    return str(myint)

if (len(sys.argv) <= 1):
    print("Available syntax:")
    print("\tall                            | lists all available commands and their variations extensively")
    print("\thelp audit                     | Performs basic project analysis and suggests any basic missed work")
    exit()

if sys.argv[1] == 'all':
    print("Extensive commands:")
    print("\thelp all")
    print("\thelp audit")
    exit()

def handle():
    precursor = sys.argv[1]
    if (precursor == "audit"):
        print("Performing audit...")
        vulnerabilities = 0 

        # empty files
        for root, dirs, files in os.walk("src"):
            for file in files:
                filepath = os.path.join(root, file)
                with open(filepath, 'r') as file:
                    content = file.read().strip()
                    if content == "":
                        print("Detected empty file " + filepath)
                        vulnerabilities += 1

        # excessive white space
        for root, dirs, files in os.walk("src"):
            for file in files:
                filepath = os.path.join(root, file)
                if ".h" in filepath or ".c" in filepath:
                    prev_line = ""
                    with open(filepath, 'r') as file:
                        linecount = 0
                        for line in file:
                            linecount += 1
                            if ((line.strip() == "\n") or (line.strip() == "") or (len(line.strip()) == 0)) and ((prev_line.strip() == "\n") or (prev_line.strip() == "") or (len(prev_line.strip()) == 0)):
                                print("Detected excessive whitespace in " + filepath + " on line " + str(linecount))
                                vulnerabilities += 1
                            prev_line = line
        
        # header guards
        existing_guards = set()
        for root, dirs, files in os.walk("src"):
            for file in files:
                filepath = os.path.join(root, file)
                if ".h" in filepath:
                    prev_line = ""
                    with open(filepath, 'r') as file:
                        slash = "/"
                        if "\\" in filepath:
                            slash = "\\"
                        guard = filepath.split(slash)[-1].split(".")[0].upper()
                        guard += "_H"
                        lines = file.readlines()
                        if len(lines) < 2:
                            continue
                        found = False
                        if guard in existing_guards:
                            found = True
                            print(guard + " - duplicate header guard detected in " + filepath)
                        existing_guards.add(guard)
                        if not ("#ifndef " + guard in lines[0]):
                            found = True
                            print("Missing or incorrect header guard (#ifndef) detected in " + filepath)
                        if not ("#define " + guard in lines[1]):
                            found = True
                            print("Missing or incorrect header guard (#define) detected in " + filepath)
                        if not ("#endif" in lines[-1]):
                            found = True
                            print("Missing or incorrect header guard (#endif) detected in " + filepath)
                        if found:
                            vulnerabilities += 1
                        
        # calloc/malloc/free check
        for root, dirs, files in os.walk("src"):
            for file in files:
                filepath = os.path.join(root, file)
                if ".h" in filepath or ".c" in filepath:
                    with open(filepath, 'r') as file:
                        linecount = 0
                        for line in file:
                            linecount += 1
                            if "calloc(" in line or "malloc(" in line or ("free(" in line and "_free(" not in line):
                                print("Detected an unmonitored memory operation in " + filepath + "on line " + str(linecount) + ":\n  " + line[0:-1].strip())
                                vulnerabilities += 1

        # header implementation check
        for root, dirs, files in os.walk("src"):
            for file in files:
                filepath = os.path.join(root, file)
                if ".h" in filepath:
                    if ("/custom/" in filepath) or ("\\custom\\" in filepath):
                        continue
                    with open(filepath, 'r') as file:
                        linecount = 0
                        for line in file:
                            linecount += 1
                            interm = line
                            if interm[-1] == "\n":
                                interm = interm[:-1]
                            interm = interm.strip()
                            if "typedef" in interm:
                                continue
                            if len(interm) > 2 and interm[-2:] == ");" and interm.split(" ")[0][-1] != "," and interm.split(" ")[0][-1] != ";":
                                fp = filepath[:-2] + ".c"
                                if os.path.exists(fp):
                                    with open(fp, 'r') as srcfile:
                                        content = srcfile.read()
                                        if (interm[:-1] + " {") not in content:
                                            if (interm[:-1] + "{") not in content:
                                                print("Unable to detect an implementation for \"" + interm + "\" in " + filepath + " on line " + str(linecount))
                                            else:
                                                print("The implementation for \"" + interm + "\" has an improperly formatted \"{\", please put a space bar character between the function and the curly brace")
                                            vulnerabilities += 1

        # repetitive header audit for headers
        header_map = dict()
        for root, dirs, files in os.walk("src"):
            for file in files:
                filepath = os.path.join(root, file)
                if ".h" in filepath:
                    header_name = filepath[4:].replace('\\', '/')
                    header_map[header_name] = [set(), set()] # first is primary headers, second are flushed headers
                    with open(filepath, 'r') as file:
                        for line in file:
                            if ("#include " in line):
                                pinclude = line.strip().replace("#include ", "").replace("\"", "").replace("<", "").replace(">", "")
                                header_map[header_name][0].add(pinclude)
        recursive_failure = False
        for header in list(header_map.keys()):
            for primary in list(header_map[header][0]):
                if (primary in header_map.keys()):
                    def get_in_depth_headers(dive_header, update_header, hmap):
                        total_v = 0
                        for secondary in list(hmap[dive_header][0]):
                            if secondary == update_header:
                                total_v += 1
                                print(f"Recursive include detected from src/{update_header} in src/{dive_header}")
                                return [total_v, True]
                            hmap[update_header][1].add(secondary)
                            if secondary in hmap.keys():
                                res = get_in_depth_headers(secondary, update_header, hmap)
                                total_v += res[0]
                                if res[1]:
                                    return [total_v, True]
                        return [total_v, False]
                    result = get_in_depth_headers(primary, header, header_map)
                    vulnerabilities += result[0]
                    recursive_failure = result[1]
                if recursive_failure:
                    break
            if recursive_failure:
                break
        for header in list(header_map.keys()):
            for primary in list(header_map[header][0]):
                if (primary in header_map[header][1]):
                    vulnerabilities += 1
                    if (primary not in header_map.keys()):
                        print(f"Useless include detected from src/{header} - <{primary}> is not needed")
                    else:
                        print(f"Useless include detected from src/{header} - src/{primary} is not needed")
            
        # repetitive header audit for sources
        sources_map = dict()
        for root, dirs, files in os.walk("src"):
            for file in files:
                filepath = os.path.join(root, file)
                if ".c" in filepath:
                    source_name = filepath[4:].replace('\\', '/')
                    sources_map[source_name] = set()
                    with open(filepath, 'r') as file:
                        for line in file:
                            if ("#include " in line):
                                pinclude = line.strip().replace("#include ", "").replace("\"", "").replace("<", "").replace(">", "")
                                sources_map[source_name].add(pinclude)
        for source in list(sources_map.keys()):
            for inc in list(sources_map[source]):
                hkey = source.replace(".c", ".h")
                if hkey in header_map.keys():
                    if inc in header_map[hkey][1]:
                        vulnerabilities += 1
                        if (inc not in header_map.keys()):
                            print(f"Useless include detected from src/{source} - <{inc}> is not needed")
                        else:
                            print(f"Useless include detected from src/{source} - src/{inc} is not needed")

        quality = "\033[32m"
        if (vulnerabilities > 10):
            quality = "\033[31m"
        elif (vulnerabilities > 0):
            quality = "\033[33m"
        print("Audit finished - detected " + quality + str(vulnerabilities) + "\033[0m vulnerabilities")
        return True
    return False

if (handle() == False):
    print("Invalid command detected - please use help command with no args for a list of available commands")
