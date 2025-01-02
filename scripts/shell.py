"""
author: Jason Heflinger
description: Runs a customized shell program with extremely limited functionality. Features
             all the basic commands needed to contribute, build, run, and clean the project.
"""

from subprocess import run
from subprocess import Popen
import sys
import os

cmd = ""
avail = ["build", "analyze", "help", "clean", "run"]
shell = ""
prev = ""
if len(sys.argv) > 1:
    shell = sys.argv[1]
cmd = input(">>> ")
while(cmd != "exit" and cmd != "q" and cmd != "e" and cmd != "quit"):
    if cmd == "\x1b[A":
        cmd = prev
    prev = cmd
    if cmd.split(" ")[0] == "g" or cmd.split(" ")[0] == "git":
        cmdlinep = cmd.split(" ")[1:]
        quoteopen = False
        genargs = []
        qstr = ""
        for arg in cmdlinep:
            if "\"" in arg:
                quoteopen = not quoteopen
                if not quoteopen:
                    qstr += arg[:-1]
                    genargs.append(qstr)
                    continue
                else:
                    qstr += arg[1:] + " "
                    continue
            if quoteopen:
                qstr += arg + " "
            else:
                genargs.append(arg)
        cmdlinep = genargs
        run(["git"] + cmdlinep, shell=False)
        cmd = input(">>> ")
        continue
    if cmd == "cl" or cmd == "clear":
        if shell == "":
            run(["clear"], shell=False)
        else:
            run([shell, "clear"], shell=False)
        cmd = input(">>> ")
        continue
    valid = False
    spcmd = cmd.split(" ")
    for a in avail:
        if spcmd[0] == a or spcmd[0] == a[0]:
            if a == "help" or a == "analyze":
                run(["python", "scripts/" + a + ".py"] + spcmd[1:], shell=False)
            else:
                ncmd = ""
                if len(spcmd) > 1:
                    for c in spcmd[1:]:
                        ncmd += " " + c 
                if shell == "":
                    spcmd[0] = a + ".sh"
                    run(["sh"] + spcmd, shell=False)
                else:
                    run([shell, "./" + a + ncmd], shell=False)
            valid = True
            break
    if valid:
        cmd = input(">>> ")
        continue
    print("\033[31munrecognized command detected\033[0m")
    print("available commands:\n\t- quit\n\t- clear\n\t- git") 
    for a in avail:
        print("\t- " + a)
    cmd = input(">>> ")
print("quitting shell...")
