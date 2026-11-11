# Elden PMIC GLINK board adapter

`ChargeRead` implements only scalar property GET messages. It emits no charger,
power-path, firmware update, GPIO or shipping command. A transport is still
required; this codec does not qualify device charging.

The stock TB322FC module confirms owner 32778, Lenovo GET/SET opcodes 0x60/0x61,
24-byte requests and global input-suspend property 27. The remaining diagnostic
IDs are pinned vendor-source candidates and require live readback. `input_suspend`
is boolean in the stock interface, not a per-port mask.

Keep firmware-owned thermal, overvoltage and reverse-current protection. Preserve
the `ChargeRead` GET-only boundary while qualifying the original charging path.
