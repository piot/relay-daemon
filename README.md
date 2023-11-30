<div align="center">
<img src="https://raw.githubusercontent.com/piot/relay/main/docs/images/logo.svg" width="192" />
</div>

# Relay Daemon

## Install

Extract the release zip containing the `relay` linux executable and `relay.service` [systemd unit file](https://www.freedesktop.org/software/systemd/man/systemd.unit.html).

```console
sudo chmod a+x relay
sudo cp relay /usr/local/sbin/
sudo cp relay.service /usr/local/lib/systemd/system/
```

* Make systemd reload all .service-files

```console
systemctl daemon-reload
```

* Check that systemd has read the .service-file:

```console
systemctl status relay --output cat
```

It should report something similar to:

```console
○ relay.service - UDP Relay
     Loaded: loaded (/usr/local/lib/systemd/system/relay.service; disabled; preset: disabled)
     Active: inactive (dead)
```

* Make sure it is started, if machine is rebooted in the future:

```console
systemctl enable relay
```

* Start it now:

```console
systemctl start relay
```

## Useful commands

### journalctl

[journalctl](https://www.freedesktop.org/software/systemd/man/journalctl.html) outputs the log entries stored in the journal.

```console
journalctl -u relay -b --output cat -f
```