# RemoteOps

RemoteOps is a C-based network programming project developed for the IE3090 Network Programming module. It uses an Agent–Controller architecture with TCP for authenticated remote management and UDP for periodic system monitoring.

## Personalised Values

- Port: 9410
- SID: 9842
- Token: OPS-2489
- Source suffix: 489

## Project Files

- `agent_489.c`
- `controller_489.c`
- `Makefile_489`
- `remoteops_IT24102489.log`

## Main Features

- AUTH authentication
- SYSINFO monitoring
- LISTPROC process listing
- Restricted EXEC commands
- PUT / GET file transfer
- UDP MONITOR START / STOP
- Persistent activity logging
- Multiple Controller support using POSIX threads

## Build

```bash
make -f Makefile_489

Activity log
remoteops_IT24102489.log

Concurrency
POSIX pthreads, Multiple Controllers
