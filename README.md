# Hexalinq Drive FUSE driver

## About
This is a user mode file system driver for Hexalinq Drive, the cloud storage, which is used by [Binary Workbench](https://bw.hexalinq.com/) to store your projects. Currently provides read-only access.

## Building
- You'll need make, gcc, libfuse, and libcurl installed:
  - Arch Linux and derivatives (Manjaro, BlackArch, etc.): `pacman -Sy make gcc fuse3 curl`
  - Debian and derivatives (Ubuntu, Linux Mint, Kali Linux, etc.): `apt install make gcc libfuse3-dev libcurl-dev`

- Type `make && make install` to build and install the driver.

## Usage
- Create an API token with Binary Workbench:
  
![API tokens](/docs/apikeys.png)

- Mount your file system: `mount -t hexalinq-drive -o token=API_TOKEN /srv/binwb /path/to/an/empty/directory`

## To do
- [ ] Expose project metadata in `/srv/binwb/projects.json` and `/srv/binwb/projects/<uid>/info.json`
- [ ] Create projects using `mkdir /srv/binwb/projects/<name>`
- [ ] Delete projects using `rmdir /srv/binwb/projects/<uid>`
- [ ] Write access to files under `/srv/binwb/projects/<uid>/src`
- [ ] `/srv/binwb/projects/by-name` and `/srv/binwb/projects/<uid>/sections/by-name` pseudo-directories with symlinks
- [ ] Expose the root directory of the file system
- [ ] Allow arbitrary file creation at `/home`
