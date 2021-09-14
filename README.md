# vm_jit

PoC vm devirtualization based on `AsmJit`. The binary was taken from `YauzaCTF 2021` competition.
You are welcome to try to solve it yourself, the binary is in `challenge bin` folder.

### Update 09/07/21
I've made llvm lifter, you can find it in `vm_jit/lifter` folder.
I've also attached devirtualized binaries, you can find them in `devirt` folder.

## Dependencies

This project relies on three libraries `zydis`, `llvm 12` and `asmjit`. Install them via vcpkg:
```
vcpkg.exe install zydis
vcpkg.exe install llvm
vcpkg.exe install asmjit
```

## Before

![](https://i.imgur.com/RNKUkui.png)

## Asmjit version
![](https://i.imgur.com/Rm2eLDn.png)

## LLVM version
![](https://i.imgur.com/o26e052.png)
