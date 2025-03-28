# Running pgagroal with Docker

You can run `pgagroal` using Docker instead of compiling it manually.

## Prerequisites

- **Docker** or **Podman** must be installed on the server where PostgreSQL is running.
- Ensure PostgreSQL is configured to allow external connections.

---

## Step 1: Enable External PostgreSQL Access

Modify the local PostgreSQL server's `postgresql.conf` file to allow connections from outside:
```ini
listen_addresses = '*'
```

Update `pg_hba.conf` to allow remote connections:
```ini
host    all    all    0.0.0.0/0    scram-sha-256
```

 Follow [GETTING STARTED](https://github.com/agroal/pgagroal/blob/main/doc/GETTING_STARTED.md) for further server setup 


Then, restart PostgreSQL for the changes to take effect:
```sh
sudo systemctl restart postgresql
```

---

## Step 2: Clone the Repository
```sh
git clone https://github.com/agroal/pgagroal.git
cd pgagroal
```

---

## Step 3: Build the Docker Image

There are two Dockerfiles available:
1. **Alpine-based image**  
   **Using Docker**
   ```sh
   docker build -t pgagroal:latest -f ./contrib/docker/Dockerfile.alpine .
   ```
   **Using Podman**
   ```sh
   podman build -t pgagroal:latest -f ./contrib/docker/Dockerfile.alpine .
   ```

2. **Rocky Linux 9-based image**  
   ```sh
   docker build -t pgagroal:latest -f ./contrib/docker/Dockerfile.rocky9 .
   ```
   **Using Podman**
   ```sh
   podman build -t pgagroal:latest -f ./contrib/docker/Dockerfile.rocky9 .
   ```

---

## Step 4: Run pgagroal as a Docker Container

Once the image is built, run the container using:
- **Using Docker**
   ```sh
   docker run -d --name pgagroal --network host pgagroal:latest
   ```
- **Using Podman**
   ```sh
   podman run -d --name pgagroal --network host pgagroal:latest
   ```

---

## Step 5: Verify the Container

Check if the container is running: 

- **Using Docker**
   ```sh
   docker ps | grep pgagroal
   ```
- **Using Podman**
   ```sh
   podman ps | grep pgagroal
   ```

Check logs for any errors: 
- **Using Docker**
   ```sh
   docker logs pgagroal
   ```
- **Using Podman**
   ```sh
   podman logs pgagroal
   ```


We will assume that we have a user called `test` with the password `test` in our
[PostgreSQL](https://www.postgresql.org) instance. See their
[documentation](https://www.postgresql.org/docs/current/index.html) on how to setup
[PostgreSQL](https://www.postgresql.org), [add a user](https://www.postgresql.org/docs/current/app-createuser.html)
and [add a database](https://www.postgresql.org/docs/current/app-createdb.html).

We will connect to [**pgagroal**](https://github.com/agroal/pgagroal) using the [psql](https://www.postgresql.org/docs/current/app-psql.html)
application.

```
psql -h localhost -p 2345 -U test test
```
