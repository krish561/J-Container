# J-Container 🐳

**A lightweight, rootless container runtime built from scratch in C and Java.**

J-Container demonstrates how modern container engines (like Docker or Podman) work under the hood. It bridges high-level process orchestration (Java) with low-level Linux Kernel isolation primitives (C).

---

##  Architecture

The project consists of two layers:

1.  **The Orchestrator (Java):** Handles CLI argument parsing, binary location, and user interaction.
2.  **The Shim (C):** Interacts directly with the Linux Kernel to create isolation using Namespaces and Chroots.

**Key Technologies Used:**
* **Linux Namespaces:** `CLONE_NEWUSER` (Rootless), `CLONE_NEWPID` (Process isolation).
* **Filesystem Isolation:** `chroot` (Jail) + `bind mounts` (for `/proc`).
* **Rootless Mapping:** `newuidmap` / `newgidmap` to map unprivileged users to `root` inside the container.
* **Process Injection:** `fork` / `exec` / `waitpid` lifecycle management.

---

## Quick Start

### Prerequisites
* Linux (Ubuntu 24.04 / Fedora recommended)
* GCC Compiler
* Java JDK 17+
* `uidmap` tools (`sudo apt install uidmap`)

### 1. Setup the RootFS
We use a minimal Alpine Linux filesystem.
```bash
chmod +x setup.sh
./setup.sh
```
2. Compile the Native Shim

```Bash
gcc -o container-shim container-shim.c
```
3. Run a Container
Compile and run the Java Orchestrator:
```Bash
cd java
javac JContainer.java
java JContainer run ../rootfs /bin/sh
```
You should see the container prompt:
```Plaintext
/ # id
uid=0(root) gid=0(root) groups=0(root)
```
(Note: You are root inside the container, but an unprivileged user on the host!)
🚧 Limitations & Roadmap

This is an educational implementation. While fully functional for basic commands, some features were intentionally deferred (because writing C system calls is exhausting).
1. AppArmor Bypass

Currently, Ubuntu 24.04 applies strict AppArmor policies to unprivileged user namespaces.

    Current Workaround: We disable the restriction globally via sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0.

    Future Fix: Implement a proper AppArmor profile or profile transition in the C shim.

2. Resource Limits (Cgroups)

Currently, the container has unlimited access to the host's CPU and RAM.

    Missing: Implementation of cgroup v2 controllers to limit usage (e.g., --memory 512mb).

3. Networking

The container currently shares the network stack or is isolated without a loopback interface.

    Missing: Creation of veth pairs and a bridge interface to allow internet access (e.g., ping google.com).

🧠 Why this project?

I built this to demystify "Container Magic." By writing the clone, setgroups, and uid_map logic manually, I gained deep insight into how the Linux Kernel handles isolation—knowledge that goes far beyond writing a Dockerfile.


### Step 3: Save, Commit, and Push

1.  **Save:** `Ctrl+O`, `Enter`, `Ctrl+X`.
2.  **Push to GitHub:**
    ```bash
    git add README.md
    git commit -m "Add documentation and roadmap"
    git push
    ```

---

### You are done. 🎓

You now have a clean, documented C/Java systems project on your GitHub. It is "interview ready."

**Are you ready to switch gears?**
Let's close the SSH window, go back to your Fedora machine, and start the **RAG AI Search Engine**.
**Type "Ready" when you have your Fedora terminal open.**
