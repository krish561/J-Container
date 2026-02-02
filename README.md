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

## ⚡ Performance Benchmarks

J-Container was benchmarked against industry-standard engines (Docker 24.0, Podman 4.9) over **100 iterations** using `hyperfine`.

**The Result:** J-Container is **~5.2x faster than Docker** and **~6.6x faster than Podman** for short-lived container tasks.

|     Runtime     | Mean Startup Time | Stability (σ) | Memory (RSS) |   Comparison   |
| --------------- | ----------------- | ------------- | ------------ | -------------- |
| **J-Container** |    **149.9 ms**   |   ± 16.0 ms   |    44.7 MB   | 🚀 **Fastest** |
| **Docker**      |      778.7 ms     |   ± 102.6 ms  |    28.7 MB*  |  ~5.2x Slower  |
| **Podman**      |      988.3 ms     |   ± 135.6 ms  |    41.9 MB   |  ~6.6x Slower  | 

> **Methodology:**
> * **Benchmark:** `hyperfine --warmup 5 --runs 100 '...'`
> * **Task:** Execute `/bin/true` (measures pure runtime initialization overhead).
> * **Environment:** Ubuntu Server 24.04 VM.
> * **Memory Note:** Docker's 28.7MB represents the CLI client only; the background daemon consumes significantly more RAM. J-Container and Podman are daemonless.

### 🔍 Analysis: Why is it faster?
Modern engines like Docker and Podman prioritize feature richness (OverlayFS, CNI Networking, SECCOMP profiles) over raw speed.
* **J-Container** demonstrates the raw performance of Linux Namespaces. The **149ms** startup is almost entirely JVM initialization; the actual C shim execution is **<10ms**.
* **Docker** incurs overhead from Client-to-Daemon IPC (Inter-Process Communication).
* **Podman** (Daemonless) incurs overhead from setting up the user namespace and rootless networking stack for every single run.

### 2. **Architecture Diagram**
````mermaid`
graph TD
    subgraph Host_User_Space ["Host User Space (Unprivileged)"]
        User([User]) -->|java JContainer run ...| Java[("☕ JContainer (Java)<br>Orchestrator")]
        Java -->|ProcessBuilder.start()| ShimParent["⚙️ C Shim (Parent)<br>(PID: 100)"]
        
        ShimParent --"1. clone(NEWUSER|NEWPID)"--> ShimChild
        ShimParent --"2. newuidmap / newgidmap"--> Map[("UID/GID Mapping<br>(Host User -> Root)")]
        ShimParent --"3. Write Pipe"--> Checkpoint((Checkpoint))
    end

    subgraph Container_Boundary ["📦 Container Boundary (Isolated Namespace)"]
        ShimChild["⚙️ C Shim (Child)<br>(PID: 1)"]
        Checkpoint --"4. Read Pipe (Unblock)"--> ShimChild
        
        ShimChild --"5. unshare(NEWNS)"--> MountNS["Mount Namespace"]
        ShimChild --"6. pivot_root"--> RootFS[("📂 RootFS<br>(Alpine)")]
        ShimChild --"7. mount /proc"--> ProcFS["/proc (Isolated)"]
        
        ShimChild --"8. fork() + execvp()"--> Shell["🚀 Target Process<br>(/bin/sh)"]
    end

    style Java fill:#f89820,stroke:#333,stroke-width:2px,color:white
    style ShimParent fill:#555,stroke:#333,stroke-width:2px,color:white
    style ShimChild fill:#468499,stroke:#333,stroke-width:4px,color:white
    style Shell fill:#2ecc71,stroke:#333,stroke-width:2px,color:white     
```mermaid`    
---

## Technical Depth Demonstrated

Building J-Container required understanding:

1. **Linux Security Model**: User namespaces allow unprivileged users to 
   gain capabilities inside isolated contexts—the foundation of rootless 
   containers (used by Podman, Kubernetes in restricted environments)

2. **Process Lifecycle Management**: Manual fork/exec/waitpid taught me 
   exactly how process reaping works and why Docker needs a PID 1 init

3. **Filesystem Semantics**: chroot vs pivot_root tradeoffs, bind mount 
   propagation, and why /proc must be mounted separately

4. **C ↔ Java Interop**: JNI/JNA would've been easier, but I chose 
   exec-based IPC to keep the shim standalone and debuggable

This knowledge directly translates to debugging production container 
issues (OOM kills, zombie processes, mount propagation bugs).

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
