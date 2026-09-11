## 🛠️ Technologies Used

| Area | Technology |
| :--- | :--- |
| **Language** | C++17 |
| **Platform** | Windows 10 / 11 (x64) |
| **Build System** | CMake, MSVC, MinGW |
| **KM/UM Bridge** | WDM driver interface via custom IOCTL protocol |
| **Concurrency** | `std::thread`, `std::atomic` |
| **Console UI** | ANSI escape codes, custom loading bars |
| **Analysis Tools** | Toolhelp32, WinAPI process and module enumeration |

---

## 🎯 Purpose & Scope

This project was developed strictly as an educational artifact to study low-level systems programming. The primary learning objectives include:

1. **System Boundaries:** Understanding how Windows separates User Mode (UM) and Kernel Mode (KM), and how data is securely routed across this boundary using IOCTLs.
2. **Memory Interaction:** Exploring how external diagnostic tools interface with the memory space of a secondary process.
3. **Real-Time Processing:** Architecting non-blocking, multi-threaded feature loops (e.g., memory scan → decision matrix → action execution) without degrading main thread performance.
4. **3D Mathematics:** Implementing World-to-Screen transformations using view matrices and vector mathematics.
5. **Project Architecture:** Managing a modular, multi-component C++ codebase using CMake.

> **Note:** This is a theoretical learning artifact, not a consumer product.

---

## ⚠️ Legal & Ethical Disclaimer

This repository is published **strictly for educational research and reverse-engineering analysis**. 

It is explicitly **NOT** intended to:
* Cheat or gain unfair advantages in online multiplayer games.
* Bypass, manipulate, or analyze commercial anti-cheat systems.
* Facilitate unauthorized access to computer systems or proprietary software.
* Disrupt the normal operation of any online service.

**Critical Notice:** 
* No kernel-mode drivers (`.sys`), runtime offsets, or functional executable binaries are provided.
* The architectural skeleton provided here cannot be used "out-of-the-box" for malicious purposes.
* Anyone attempting to weaponize or misuse this research does so at their own legal risk and in direct violation of applicable platform Terms of Service.

The author strictly condemns the use of this material for cheating, malware development, or any unauthorized system manipulation.

---

## 👨‍💻 Author

**osFERPHY**  
Self-taught C++ and systems programming researcher.  
This project was built as part of a personal deep-dive into Windows OS internals and game-engine architecture.
