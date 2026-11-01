# Contributing to xwa-arm64

Thank you for your interest in improving **xwa-arm64**! We welcome bug reports, documentation updates, performance improvements, and feature additions.

Because this project operates as a user-mode driver and hardware transceiver service on Windows on ARM64, contributions require careful attention to system stability, memory safety, and hardware compatibility.

---

## Guidelines for Contributions

### 1. Hardware Verification
* Changes touching dongle communication (`src/dongle/`), Game Input Protocol (`src/controller/`), or virtual gamepad emulation (`src/gamepad/`) should be compiled and tested on physical Windows on ARM64 hardware whenever possible.
* If you do not have access to physical ARM64 hardware or an Xbox Wireless Adapter, please state this clearly in your Pull Request description so maintainers can test your changes prior to merging.

### 2. Code Standards
* **Language Standard**: C++17.
* **Architecture**: Must compile cleanly for ARM64 using MSVC / CMake.
* **Dependencies**: Minimize external runtime dependencies. Leverage native Win32 APIs, WinUSB, and standard C++ libraries where possible.
* **Safety**: Avoid undefined behavior, handle edge cases in USB packet parsing, and properly manage buffer boundaries when processing MT7612U Wi-Fi frames or encrypted GIP packets.

### 3. AI Usage & Transparency Policy
We welcome the use of AI tools (such as GitHub Copilot, Google Antigravity, Gemini, or Claude) to accelerate development, assist with boilerplate, or draft tests, subject to the following principles:

* **Transparency & Disclosure**: If a Pull Request or commit was generated or substantially assisted by an AI tool, please disclose this in the PR description or commit message.
* **Human Responsibility & Code Review**: Do not submit unreviewed or hallucinated AI code dumps. You, as the contributor, must read, understand, and be able to explain every line of code submitted, and take full responsibility for its correctness and security.
* **License Compliance**: Ensure that any code produced with AI assistance does not violate third-party intellectual property or incompatible open-source licenses. All contributions must comply with the **GNU General Public License v2.0 (GPLv2)**.
* **Git Commit Attribution**: When an AI agent was used to co-author a commit, we encourage including standard git trailers, for example:
  ```text
  Co-authored-by: AssistantName <noreply@example.com>
  ```

---

## Submitting Pull Requests

1. **Fork & Branch**: Create a feature branch from `main` (`feature/my-feature` or `fix/my-bug`).
2. **Build & Test**: Ensure `cmake -B build -A ARM64 -DCMAKE_BUILD_TYPE=Release` configures and builds without errors or warnings.
3. **Commit Cleanly**: Write concise, descriptive commit messages.
4. **Open a PR**: Fill out the PR description with:
   - What problem this PR solves or what feature it adds.
   - Hardware tested (device model, adapter model, controller type).
   - Any AI assistance utilized during development.
