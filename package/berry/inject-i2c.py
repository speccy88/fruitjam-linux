#!/usr/bin/env python3
# Register the native i2c module into Berry default/be_modtab.c (idempotent).
import sys

path = sys.argv[1]
with open(path) as f:
    text = f.read()

extern = "be_extern_native_module(i2c);"
if extern not in text:
    text = text.replace(
        "/* user-defined modules declare start */",
        "/* user-defined modules declare start */\n" + extern,
        1,
    )

entry = "&be_native_module(i2c),"
if entry not in text:
    block = (
        "#if BE_USE_I2C_MODULE\n"
        "    &be_native_module(i2c),\n"
        "#endif\n"
    )
    text = text.replace(
        "    &be_native_module(undefined),",
        block + "    &be_native_module(undefined),",
        1,
    )

with open(path, "w") as f:
    f.write(text)
print("be_modtab.c: i2c module registered")
