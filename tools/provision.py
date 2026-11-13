#!/usr/bin/env python3
"""Generate the firmware's local profile and credentials."""
import argparse
from datetime import date
from getpass import getpass
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--name", required=True)
parser.add_argument("--birth", required=True, type=date.fromisoformat)
args = parser.parse_args()

if not args.name.strip() or len(args.name.encode()) > 40:
    parser.error("El nombre debe ocupar entre 1 y 40 bytes.")
password = getpass("Contraseña de time_machine: ")
if not 8 <= len(password) <= 63 or any(not 32 <= ord(c) <= 126 for c in password):
    parser.error("La contraseña debe tener entre 8 y 63 caracteres ASCII imprimibles.")

target = Path(__file__).resolve().parents[1] / "firmware/main/local_config.h"
birth = args.birth.year * 10000 + args.birth.month * 100 + args.birth.day
target.touch(mode=0o600, exist_ok=True)
target.chmod(0o600)
target.write_text(
    "#pragma once\n"
    f"#define PROFILE_NAME {json.dumps(args.name, ensure_ascii=False)}\n"
    f"#define PROFILE_BIRTH {birth}\n"
    f"#define AP_PASSWORD {json.dumps(password)}\n",
    encoding="utf-8",
)
print("Configuración local guardada. Vuelve a compilar el firmware.")
