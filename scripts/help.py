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
                if ".h" in filepath:
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
                            if "calloc(" in line or "malloc(" in line or "free(" in line:
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

        quality = "\033[32m"
        if (vulnerabilities > 10):
            quality = "\033[31m"
        elif (vulnerabilities > 0):
            quality = "\033[33m"
        print("Audit finished - detected " + quality + str(vulnerabilities) + "\033[0m vulnerabilities")
        return True
        bake_required = False
        num_tiles = 0

        # get tile stats
        for root, dirs, files in os.walk("assets/tiles"):
            for file in files:
                filepath = os.path.join(root, file)
                if ".png" in filepath:
                    num_tiles += 1

        # construct pack
        pack_str  = "#ifndef PACK_H\n"
        pack_str += "#define PACK_H\n"
        pack_str += "\n"
        pack_str += f"#define TILE_ASSET_COUNT {num_tiles}\n"
        pack_str += "\n"
        pack_str += "#endif"

        if os.path.exists("src/assets/pack.h"):
            with open("src/assets/pack.h", 'r') as f:
                src = f.read()
                bake_required = src != pack_str
        else:
            bake_required = True

        if bake_required:
            print("Assets were detected to be out of date... baking assets now")
            with open("src/assets/pack.h", 'w') as f:
                f.write(pack_str)
            print("Assets \033[32msuccessfully\033[0m baked!")
        else:
            print("Assets are currently \033[32mup to date\033[0m - no baking needed!")
        return True
    return False

if (handle() == False):
    print("Invalid command detected - please use help command with no args for a list of available commands")
