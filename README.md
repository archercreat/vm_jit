# vm_jit

PoC vm devirtualization based on `AsmJit`. The binary was taken from `YauzaCTF 2021` competition.
You are welcome to try to solve it yourself, the binary is in `challenge bin` folder.

## Dependencies

This project relies on two libraries `zydis` and `asmjit`. Install them via vcpkg:
```
vcpkg.exe install zydis
vcpkg.exe install asmjit
```

## Before

![](https://i.imgur.com/RNKUkui.png)

## After
![](https://i.imgur.com/Rm2eLDn.png)

