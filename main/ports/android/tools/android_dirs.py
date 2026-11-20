"""Where each Android app's resources live: machines/consoles/<id> for the
consoles (main/drivers/consoles/consoles.mk's CONSOLES), machines/computers/<id>
for the computers (the arcades are the separate arcade/ app)."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINES = os.path.join(HERE, "..", "machines")
_MK = os.path.join(HERE, "..", "..", "..", "drivers", "consoles", "consoles.mk")


def consoles():
    for line in open(_MK, encoding="utf-8"):
        if line.startswith("CONSOLES := "):
            return line.split(":=", 1)[1].split()
    return []


CONSOLES = consoles()


def group(machine):
    return "consoles" if machine in CONSOLES else "computers"


def machine_dir(machine):
    return os.path.join(MACHINES, group(machine), machine)
