# RamJet
Rice (Ananicy clone built in Rust) ported to C

This is another auto nice daemon but with the speed of C execution and compatibility with Ananicy rules.

## How to install on different distros:

### 1. Install the required dependencies:
# Debian / Ubuntu
```bash
sudo apt install gcc make libc6-dev linux-headers-generic
```
# Fedora / RHEL
```bash
sudo dnf install gcc make glibc-devel kernel-headers
```
# Arch Linux
```bash
sudo pacman -S gcc make glibc linux-headers
```
# Alpine
```bash
sudo apk add gcc make musl-dev linux-headers
```

### 2. Clone this repository
```bash
https://github.com/Alecaishere/RamJet.git
```

### 3. Go to the directory

Open a terminal an proceed to compile with the command:
```bash
make
```

### 4. After compile:

Move ramjet binary to /usr/local/bin/
```bash
sudo mv ramjet /usr/local/bin/
```

Move the .service file to /etc/systemd/system/ and enable it with:
```bash
sudo systemctl enable --now ramjet.service
```
# Remember to disable Ananicy/Ananicy-cpp or Rice to avoid stability problems
