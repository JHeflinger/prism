"""
author: Jason Heflinger
description: Runs a customized shell program to grow and download custom commands from
             a custom scripts repository.
"""

import os
import urllib.request
last_modified = "5/19/2025"

def gstr(str):
    return "\033[32m" + str + "\033[0m"

def sysprompt():
    print(f"Ultrashell {gstr("version 1.0")} authored by Jason Heflinger (https://github.com/JHeflinger)")
    print("Developed on Python 3.10")
    print(f"Last modified on {gstr(last_modified)}")
    print("Enter command \"help\" for usage details")

def download(command):
    try:
        urllib.request.urlretrieve(f"https://raw.githubusercontent.com/JHeflinger/Scripts/refs/heads/main/commands/{command}.py", f".scache/{command}.py")
    except:
        return False
    return True

def initcache():
    if os.path.isdir(".scache"):
        return True
    print("No script cache has been set up yet. Would you like to set one up now? (y/n)")
    response = ""
    while True:
        response = input("> ")
        if (response.lower() == "y" or response.lower() == "n"):
            response = response.lower()
            break
        else:
            print(f"Invalid response \"{response}\" detected. Please enter either \"y\" or \"n\".")
    if (response == "n"):
        return False 
    os.makedirs(".scache", exist_ok=True)
    if os.path.isdir(".git"):
        print("Current directory is a git repository. Would you like to add the script cache to .gitignore? (y/n)")
        response = ""
        while True:
            response = input("> ")
            if (response.lower() == "y" or response.lower() == "n"):
                response = response.lower()
                break
            else:
                print(f"Invalid response \"{response}\" detected. Please enter either \"y\" or \"n\".")
        if (response == "y"):
            print("Adding to .gitignore...")
            with open(".gitignore", "a") as f:
                f.write("\n.scache/*\n")
    print("Finished initializing script cache!")
    return True

def trycommand(script):
    if (not initcache()):
        print("Unable to find custom command in script cache.")
        return False
    if (os.path.isfile(f".scache/{script}.py")):
        return True
    print("Command was not found on disk. Would you like to attempt to download it? (y/n)")
    response = ""
    while True:
        response = input("> ")
        if (response.lower() == "y" or response.lower() == "n"):
            response = response.lower()
            break
        else:
            print(f"Invalid response \"{response}\" detected. Please enter either \"y\" or \"n\".")
    if (response == "y"):
        if download(script):
            print("Successfully downloaded command!")
            return True
        print("Unable to download command. Make sure you are connected to internet and that the command exists.")
        return False
    print("Unable to run command.")
    return False

def shell():
    cmd = ""
    while True:
        cmd = input("> ")
        topcmd = cmd.split(" ")[0]
        argind = cmd.find(" ")
        argstr = ""
        if (argind > 0):
            argstr = cmd[argind:]
        if (topcmd == "git" or topcmd == "g" or topcmd == "clear"):
            os.system(cmd)
        elif (topcmd == "build" or topcmd == "b"):
            if (os.name == "nt"):
                os.system("./build.bat " + argstr)
            else:
                os.system("./build.sh" + argstr)
        elif (topcmd == "run" or topcmd == "r"):
            if (os.name == "nt"):
                os.system("./run.bat " + argstr)
            else:
                os.system("./run.sh" + argstr)
        elif (topcmd == "clean" or topcmd == "c"):
            if (os.name == "nt"):
                os.system("./clean.bat " + argstr)
            else:
                os.system("./clean.sh" + argstr)
        elif (cmd == "exit" or cmd == "quit" or cmd == "q" or cmd == "e"):
            break
        else:
            if (trycommand(topcmd)):
                os.system("python .scache/" + topcmd + ".py" + argstr)

def goodbye():
    print("Exiting Ultrashell...")

if __name__ == "__main__":
    sysprompt()
    shell()
    goodbye()
