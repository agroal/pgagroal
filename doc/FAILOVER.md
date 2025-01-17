# pgagroal failover

pgagroal can failover a PostgreSQL instance if the clients can't write to it.

In `pgagroal.conf` define

```
failover = on
failover_script = /path/to/myscript.sh
```

The script will be run as the same user as the pgagroal process so proper
permissions (access and execution) must be in place.

The following information is passed to the script as parameters

1. Old primary host
2. Old primary port
3. New primary host
4. New primary port

so a script could look like

```sh
#!/bin/bash

OLD_PRIMARY_HOST=$1
OLD_PRIMARY_PORT=$2
NEW_PRIMARY_HOST=$3
NEW_PRIMARY_PORT=$4

ssh -tt -o StrictHostKeyChecking=no postgres@${NEW_PRIMARY_HOST} pg_ctl promote -D /mnt/pgdata

if [ $? -ne 0 ]; then
  exit 1
fi

exit 0
```

The script is assumed successful if it has an exit code of 0. Otherwise both servers will be
recorded as failed.
